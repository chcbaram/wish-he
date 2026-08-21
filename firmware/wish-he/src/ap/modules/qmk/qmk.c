/*
 * qmk.c  —  QMK 코어 구동
 *
 * 이 보드에서 QMK 가 맡는 일은 **매트릭스 위쪽**뿐이다. 키맵, 레이어, 탭홀드,
 * 매크로, 키 오버라이드, 그리고 VIA 프로토콜.
 *
 * 아래쪽 — ADC 스캔, 깊이 판정, 래피드 트리거(13편) — 은 keys.c 가 자기 주기로
 * 돌린다. QMK 는 그 결과 비트마스크만 본다 (port/matrix.c).
 *
 * 왜 이렇게 갈랐나:
 *
 *   - QMK 의 디바운스는 HE 에 해롭다. 접점이 없어 바운스가 없는데 지연만 는다.
 *   - 래피드 트리거는 0.1mm 단위 방향 반전을 스캔 주기(26kHz)로 봐야 한다.
 *     QMK 루프에 올리면 루프 주기에 묶인다.
 *   - 반대로 키맵·레이어는 키가 바뀔 때만 도는 일이라 QMK 에 맡겨도 싸다.
 */

#include "qmk.h"
#include "host.h"
#include "eeprom.h"
#include "via_hid.h"
#include "cli.h"
#include "log.h"
#include "usb/cherryusb/hid_kbd_if.h"
#include "usb/cherryusb/hid_exk_if.h"
#include "ghost.h"
#include "matrix.h"
#include "dynamic_keymap.h"
#include "keycode_config.h"
#ifdef RGB_MATRIX_ENABLE
#include "rgb_matrix.h"
#endif
#include <string.h>


/* 125us = 8kHz 마이크로프레임. 이걸 넘긴 회차는 리포트가 한 프레임 늦는다. */
#define QMK_OVER_US   125

/* 재개 신호를 겹치지 않게 두는 간격 (자는 호스트를 키로 깨울 때) */
#define QMK_WAKE_RETRY_MS   100


extern void viaPortInit(void);   /* keyboards/<보드>/via_port.c */


extern host_driver_t usb_driver;   /* port/driver_usb.c */

static void cliQmk(cli_args_t *args);
static void qmkIdleTask(void);


/* QMK 코어가 올라와 있나 — 안 올라왔으면 qmkUpdate() 를 돌리면 안 된다 */
static bool is_qmk_on = false;

/*
 * keyboard_task() 한 바퀴 시간.
 *
 * ★ 이걸 재려고 넣었다.
 *
 *   QMK 를 얹기 전에 "루프 한 바퀴가 몇 us 인지는 얹어봐야 안다"고 적어뒀다.
 *   125us 폴링을 쓰는데 루프가 그보다 길면 리포트가 폴링을 놓친다. RGB 를 켜고
 *   끄며 이 값을 비교하면 LED 프레임 생성이 루프에 주는 부담도 갈라 보인다.
 */
static volatile uint32_t task_us_last = 0;
static volatile uint32_t task_us_max  = 0;
static volatile uint32_t task_us_sum  = 0;
static volatile uint32_t task_us_cnt  = 0;

/*
 * ★ 최대치 하나로는 아무것도 못 정한다.
 *
 *   max 308us 를 보고도 대응을 못 골랐다. 180만 번 중 3번이면 부팅 잡음이고
 *   5000번이면 구조 문제인데, 최대치는 그 둘을 구분해 주지 않는다. 그래서
 *   "폴링 주기를 넘긴 횟수"(QMK_OVER_US)를 같이 센다.
 */
static volatile uint32_t task_over_cnt = 0;

/* 넘긴 순간이 언제였나 — 부팅 직후만인지 계속인지 가른다 */
static volatile uint32_t task_over_ms_first = 0;
static volatile uint32_t task_over_ms_last  = 0;

/*
 * 메인 루프가 한 바퀴 도는 데 걸린 시간 — keyboard_task 만이 아니라 **전부**.
 *
 * ★ task_us 로는 안 보이는 것이 있다.
 *
 *   루프의 다른 일이 오래 붙들면 keyboard_task 자체는 짧은데 **다음 호출이 늦는다.**
 *   그동안 스캔도 리포트도 멎는데 task_us 에는 한 글자도 안 나타난다. 실제로 설정
 *   응답을 못 실으면 그 자리에서 최대 100ms 를 돌던 코드가 있었다(B11) — 스캔이
 *   26us 마다 도는 루프에서 4000 바퀴다.
 *
 * ★ 플래시 쓰기는 정상적으로 넘긴다. XIP 라 쓰는 동안 인터럽트를 막아야 해서
 *   설정 저장·프로파일 전환마다 6ms 가 통째로 멎는다. 그래서 초과 기준은 그보다
 *   넉넉히 위에 둔다 — 여기서 잡고 싶은 것은 "수십 ms" 급이다.
 */
#define QMK_LOOP_OVER_US   20000

static volatile uint32_t loop_us_max  = 0;
static volatile uint32_t loop_over_cnt = 0;

/* rgb_matrix_task 몫. keyboard_task 평균에 묻혀 안 보이므로 따로 센다. */
static volatile uint32_t rgb_us_last = 0;
static volatile uint32_t rgb_us_max  = 0;
static volatile uint32_t rgb_us_sum  = 0;
static volatile uint32_t rgb_us_cnt  = 0;
static volatile uint32_t rgb_over_cnt = 0;


static bool qmkInit(void)
{
  /*
   * 단계마다 로그를 남긴다. 어딘가에서 죽으면 마지막으로 찍힌 줄이 범인이다.
   * (로그는 .noinit RAM 링버퍼라 워엄 리셋 뒤에도 `log` 로 읽을 수 있다)
   */
  logPrintf("[  ] qmk 1 eeprom_init\n");
  eeprom_init();

  logPrintf("[  ] qmk 2 via_hid_init\n");
  via_hid_init();

  logPrintf("[  ] qmk 3 host_set_driver\n");
  host_set_driver(&usb_driver);

  logPrintf("[  ] qmk 4 keyboard_setup\n");
  keyboard_setup();

  logPrintf("[  ] qmk 5 keyboard_init\n");
  keyboard_init();

  /* 보드별 VIA 커스텀 메뉴 초기화 (있으면) — EEPROM 이 준비된 뒤여야 한다 */
  viaPortInit();

  logPrintf("[  ] qmk 6 done\n");

  logPrintf("[  ] qmkInit()\n");
  logPrintf("     MATRIX %d x %d, 디바운스 없음\n", MATRIX_ROWS, MATRIX_COLS);
  logPrintf("     레이어 %d, EEPROM %d B\n",
            DYNAMIC_KEYMAP_LAYER_COUNT, TOTAL_EEPROM_BYTE_COUNT);

  return true;
}

/*
 * 루프 통계를 한 덩어리로. keys 쪽과 같은 이유로 함께 준다.
 */
void qmkGetStat(qmk_stat_t *p_stat)
{
  if (p_stat == NULL) return;

  p_stat->task_us      = task_us_last;
  p_stat->task_us_max  = task_us_max;
  p_stat->task_us_avg  = task_us_cnt ? (task_us_sum / task_us_cnt) : 0;
  p_stat->task_over    = task_over_cnt;
  p_stat->task_cnt     = task_us_cnt;
  p_stat->rgb_us       = rgb_us_last;
  p_stat->rgb_us_max   = rgb_us_max;
  p_stat->rgb_us_avg   = rgb_us_cnt ? (rgb_us_sum / rgb_us_cnt) : 0;
}

/* 누적값만 지운다 — keys 쪽과 같은 규칙 */
void qmkClearStat(void)
{
  task_us_max   = 0;
  task_us_sum   = 0;
  task_us_cnt   = 0;
  task_over_cnt = 0;
  loop_us_max   = 0;
  loop_over_cnt = 0;
  rgb_us_max    = 0;
  rgb_us_sum    = 0;
  rgb_us_cnt    = 0;
  rgb_over_cnt  = 0;
}

void qmkRgbStat(uint32_t us)
{
  rgb_us_last = us;
  if (us > rgb_us_max) rgb_us_max = us;
  rgb_us_sum += us;
  rgb_us_cnt++;
  if (us > QMK_OVER_US) rgb_over_cnt++;
}

/* CLI 만 먼저 등록한다. QMK 를 켜기 전에도 `qmk start` 를 칠 수 있어야 한다. */
bool qmkCliInit(void)
{
  cliAdd("qmk", cliQmk);

  /*
   * 여기서 QMK 를 켠다. CLI 등록을 먼저 하는 것은, 기동에 실패해도 `qmk info` 로
   * 상태를 볼 수 있게 하기 위해서다.
   */
  return qmkStart();
}

/*
 * QMK 기동.
 *
 * 이식 중에는 부팅 때 켜지 않고 `qmk start` 로만 켰다. qmkInit() 안에서 죽으면
 * USB 가 통째로 안 올라와 부트로더 핀을 눌러야만 살아나기 때문이다. 지금은
 * 동작이 확인돼서 부팅 때 켠다.
 *
 * `qmk start` 는 남겨둔다 — 앞으로 QMK 쪽을 건드리다 부팅이 막히면 다시
 * 수동으로 돌릴 수 있게 하는 게 싸다.
 */
bool qmkStart(void)
{
  if (is_qmk_on) return true;

  is_qmk_on = qmkInit();
  return is_qmk_on;
}

bool qmkIsOn(void)
{
  return is_qmk_on;
}

void qmkUpdate(void)
{
#if _USE_HW_PERF_STAT
  uint32_t t0 = micros();
  uint32_t dt;

  /* 지난 바퀴에서 여기까지 — 루프가 통째로 멎은 시간이 여기에만 남는다 */
  {
    static uint32_t loop_prev = 0;

    if (loop_prev != 0)
    {
      uint32_t gap = t0 - loop_prev;

      if (gap > loop_us_max)        loop_us_max = gap;
      if (gap > QMK_LOOP_OVER_US)   loop_over_cnt++;
    }
    loop_prev = t0;
  }
#endif

  /*
   * ★ 호스트가 HID 프로토콜을 바꾸면 눌린 키를 비운다.
   *
   *   부트 <-> 리포트로 갈리면 리포트가 나가는 인터페이스가 통째로 바뀐다
   *   (IF0 6KRO <-> IF1 NKRO). 그때 눌려 있던 키는 **옛 인터페이스에 남은 채로
   *   아무도 안 뗀다.** QMK 가 nkro 비트를 토글할 때 clear_keyboard() 를 먼저
   *   부르는 것과 같은 이유다.
   *
   *   하드웨어 층은 값만 기록한다. 변화 감지는 여기서 한다 — 그쪽에서 QMK 함수를
   *   부르면 층이 뒤집히고, 게다가 거기는 제어 전송 안이다.
   */
  {
    static uint8_t proto_p = 1;
    uint8_t        proto   = hidKbdGetProtocol();

    if (proto != proto_p)
    {
      proto_p = proto;
      clear_keyboard();
      logPrintf("[  ] HID 프로토콜 %s\n", proto ? "report" : "boot");
    }
  }

  keyboard_task();

#if _USE_HW_PERF_STAT
  dt = micros() - t0;
  task_us_last = dt;
  if (dt > task_us_max) task_us_max = dt;
  task_us_sum += dt;
  task_us_cnt++;

  if (dt > QMK_OVER_US)
  {
    if (task_over_cnt == 0) task_over_ms_first = millis();
    task_over_ms_last = millis();
    task_over_cnt++;
  }
#endif

  eeprom_task();
  qmkIdleTask();
}

/*
 * 서스펜드 들고 남.
 *
 * ★ 이 신호를 아무도 안 받고 있었다. 상류 QMK 는 프로토콜 계층(ChibiOS/LUFA)이
 *   USB 서스펜드를 보고 suspend_power_down() 을 돌리는데, 우리 포트에는 그 계층이
 *   없다. 그래서 호스트가 자도 LED 가 그대로 켜져 있었다.
 *
 *   rgb_matrix_set_suspend_state() 를 직접 부르지 않고 **QMK 표준 경로**를 탄다.
 *   suspend_power_down_quantum() 이 백라이트·LED 표시등·오디오까지 같이 처리하므로
 *   나중에 뭘 더 얹어도 자동으로 따라온다.
 */
static void qmkIdleTask(void)
{
  static bool     prev    = false;
  static uint32_t wake_ms = 0;
  bool            now     = hidKbdIsSuspended();

  /*
   * 자는 호스트를 키로 깨운다.
   *
   * ★ 재개 신호는 그 자체가 1~15ms 짜리라, 매 바퀴 부르면 신호가 겹친다. 눌려
   *   있는 동안 계속 부르되 QMK_WAKE_RETRY_MS 간격을 둔다. 한 번에 안 깨어나는
   *   호스트가 있어서 "한 번만" 은 모자란다.
   *
   * ★ 여기서 재는 것은 **매트릭스**다. 리포트가 아니다 — 자는 동안에는 리포트가
   *   못 나가므로 리포트로 재면 영영 조건이 안 선다.
   */
  if (now && (millis() - wake_ms) >= QMK_WAKE_RETRY_MS)
  {
    for (uint8_t r = 0; r < MATRIX_ROWS; r++)
    {
      if (matrix_get_row(r) == 0) continue;

      wake_ms = millis();
      hidKbdRemoteWakeup();       /* 호스트가 안 켜 줬으면 거짓 — 고장이 아니다 */
      break;
    }
  }

  if (now == prev) return;
  prev = now;

  if (now) suspend_power_down();
  else     suspend_wakeup_init();
}


static void cliQmk(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "start"))
  {
    cliPrintf("qmkInit() ...\n");
    cliPrintf("%s\n", qmkStart() ? "OK" : "실패");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    uint32_t avg = task_us_cnt ? (task_us_sum / task_us_cnt) : 0;

    cliPrintf("MATRIX        : %d x %d\n", MATRIX_ROWS, MATRIX_COLS);
    cliPrintf("레이어        : %d\n", DYNAMIC_KEYMAP_LAYER_COUNT);
    cliPrintf("EEPROM        : %d B, 소거 %d 회, dirty 0x%X\n",
              TOTAL_EEPROM_BYTE_COUNT,
              (int)eepromGetFlushCount(), (unsigned)eepromGetDirtyMask());
    cliPrintf("keyboard_task : last %d us, avg %d us, max %d us  (n=%d)\n",
              (int)task_us_last, (int)avg, (int)task_us_max, (int)task_us_cnt);
    cliPrintf("  %dus 초과   : %d 회  (첫 %d ms, 마지막 %d ms, 지금 %d ms)\n",
              QMK_OVER_US, (int)task_over_cnt,
              (int)task_over_ms_first, (int)task_over_ms_last, (int)millis());
    cliPrintf("루프 한 바퀴  : max %d us,  %dus 초과 %d 회\n",
              (int)loop_us_max, QMK_LOOP_OVER_US, (int)loop_over_cnt);
    /*
     * 매직 스왑이 켜져 있으면 매트릭스도 키맵도 맞는데 나가는 코드만 달라진다.
     * 한 번 당했으니 늘 보이게 둔다.
     */
#if defined(RGB_MATRIX_ENABLE) && _USE_HW_PERF_STAT
    cliPrintf("rgb_task      : last %d us, avg %d us, max %d us  (n=%d, %dus 초과 %d)\n",
              (int)rgb_us_last, (int)(rgb_us_cnt ? rgb_us_sum / rgb_us_cnt : 0),
              (int)rgb_us_max, (int)rgb_us_cnt, QMK_OVER_US, (int)rgb_over_cnt);
#endif
    /*
     * 연타 — **친 박자**와 **나간 리포트**를 나란히 둔다.
     *
     * 한 박자는 리포트 두 개다(빈 것 -> 원래대로). 아래가 큐가 아니라 최신 상태
     * 한 칸이라, 앞 전송이 나가 있으면 빈 리포트가 덮여 그 박자가 사라진다.
     * 나간 것만 세면 안 나간 것이 안 보인다 — 두 값의 비가 진단이다.
     */
    {
      uint32_t beat = ghostGetBeatCount();
      uint32_t nk   = hidExkGetSentCountOf(EXK_REPORT_ID_NKRO);

      cliPrintf("연타          : 박자 %d,  NKRO 전송 %d  (박자당 %d.%02d)\n",
                (int)beat, (int)nk,
                (int)(beat ? nk / beat : 0),
                (int)(beat ? (nk * 100 / beat) % 100 : 0));
    }
    cliPrintf("keymap_config : 0x%04X  (nkro %d, 매직 스왑 %s)\n",
              (unsigned)keymap_config.raw, keymap_config.nkro,
              (keymap_config.raw & ~0x0080u) ? "★ 켜짐 — 비정상" : "없음");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rate"))
  {
    cliPrintf("keyboard_task 시간 — Ctrl-C 로 끝낸다\n");
    while (cliKeepLoop())
    {
      uint32_t avg = task_us_cnt ? (task_us_sum / task_us_cnt) : 0;

      cliPrintf("last %4d us   avg %4d us   max %4d us\r",
                (int)task_us_last, (int)avg, (int)task_us_max);
      cliLoopIdle();
      delay(200);
    }
    cliPrintf("\n");
    ret = true;
  }

  /*
   * 실제로 USB 로 나가는 부트 리포트를 그대로 찍는다.
   *
   * 스캔·매트릭스·키맵이 다 맞는데 화면에 다른 글자가 나오면 남는 건 이 바이트뿐이다.
   * 여기서 기대한 코드가 나오면 장치는 결백하고 호스트 해석 문제이며, 다른 코드가
   * 나오면 QMK 에서 리포트를 만드는 사이가 잘못된 것이다.
   */
  /*
   * 호스트가 지금 눌린 것으로 아는 키를 그대로 보여준다.
   *
   * ★ "키가 눌린 채로 남았다" 를 가리는 유일한 자리다.
   *
   *   매트릭스(`keys show`)는 장치가 보는 것이고, 이쪽은 **호스트가 보는 것**이다.
   *   둘이 갈리면 그게 곧 버그다 — 손을 다 뗐는데 호스트에는 남아 있는 상태.
   *
   *   `qmk log` 로는 못 본다. 그쪽은 부트 키보드(IF0) 섀도를 읽는데 NKRO 가 켜져
   *   있으면 QMK 가 6KRO 를 아예 안 보내서 IF0 이 죽어 있다. 실측으로 KBD 는
   *   sent 1, EXK NKRO 는 sent 6440 이었다.
   */
  /*
   * HID 프로토콜을 보고 바꾼다 — **시험용이다.**
   *
   * 평소에는 호스트가 SET_PROTOCOL 로 정한다. 그런데 그것을 요구하는 것은 BIOS·
   * 부트로더뿐이라 책상에서는 재현할 방법이 없다. 같은 함수를 직접 불러 흉내 낸다.
   *
   *   0 = 부트    IF0 으로 6KRO 가 나가야 한다
   *   1 = 리포트  IF1 로 NKRO 가 나간다 (평소)
   */
  if (args->argc >= 1 && args->isStr(0, "proto"))
  {
    if (args->argc == 2)
    {
      hidKbdSetProtocol((uint8_t)args->getData(1));
    }
    cliPrintf("HID 프로토콜 : %d (%s)\n", hidKbdGetProtocol(),
              hidKbdGetProtocol() ? "report — NKRO" : "boot — 6KRO");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "held"))
  {
    uint8_t  buf[32];
    uint8_t  n = hidExkGetReportRaw(EXK_REPORT_ID_NKRO, buf, sizeof(buf));
    uint32_t cnt = 0;

    if (n == 0)
    {
      cliPrintf("NKRO 리포트가 아직 안 실렸다\n");
    }
    else
    {
      cliPrintf("mods 0x%02X   눌린 키 : ", buf[1]);
      for (uint32_t b = 2; b < n; b++)
      {
        for (uint32_t i = 0; i < 8; i++)
        {
          if ((buf[b] & (1U << i)) == 0) continue;
          cliPrintf("0x%02X ", (unsigned)((b - 2) * 8 + i));
          cnt++;
        }
      }
      cliPrintf("%s(%d 개)\n", cnt ? "" : "없음  ", (int)cnt);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "log"))
  {
    uint8_t  prev[8] = { 0, };
    uint32_t prev_sent = hidKbdGetSentCount();
    uint32_t prev_ms   = millis();

    cliPrintf("키를 누르면 리포트를 찍는다 — Ctrl-C 로 끝낸다\n");
    /*
     * ★ 'tx' 는 직전 줄 이후 **실제로 전송 완료된** 리포트 수다.
     *
     *   shadow 만 보면 QMK 가 "보내라"고 준 것까지만 보인다. 전송이 진행 중일 때
     *   상태가 또 바뀌면 shadow 가 덮여 가운데 값이 통째로 사라지는데, 그건 여기
     *   tx=0 으로 드러난다. 상태 변화 한 번에 전송 한 번이 정상이다.
     */
    cliPrintf("  mods  keys[6]              tx    ms  간격\n");

    while (cliKeepLoop())
    {
      uint8_t  now[8];
      uint32_t sent;

      hidKbdGetReportRaw(now);
      sent = hidKbdGetSentCount();
      if (memcmp(now, prev, sizeof(now)) != 0)
      {
        cliPrintf("  0x%02X  ", now[0]);
        for (uint32_t i = 2; i < 8; i++) cliPrintf("%02X ", now[i]);
        cliPrintf(" tx%-3d%s %6d %5d", (int)(sent - prev_sent),
                  (sent - prev_sent) == 1 ? " " : "★",
                  (int)millis(), (int)(millis() - prev_ms));
        prev_sent = sent;
        prev_ms   = millis();

        /*
         * 같은 순간의 매트릭스와 그 자리의 키맵 값을 나란히 찍는다.
         * 셋을 따로 재면 어느 단계에서 어긋났는지 못 가른다.
         */
        cliPrintf("  <-");
        for (uint32_t r = 0; r < MATRIX_ROWS; r++)
        {
          matrix_row_t bits = matrix_get_row(r);

          for (uint32_t c = 0; c < MATRIX_COLS; c++)
          {
            if (bits & (1U << c))
            {
              cliPrintf(" (%d,%d)=0x%04X", (int)r, (int)c,
                        (unsigned)dynamic_keymap_get_keycode(0, r, c));
            }
          }
        }
        cliPrintf("\n");
        memcpy(prev, now, sizeof(now));
      }
      cliLoopIdle();
    }
    ret = true;
  }

#ifdef RGB_MATRIX_ENABLE
  /*
   * RGB 켜고 끄기 · 모드 고르기.
   *
   * ★ 성능을 잴 때 이게 있어야 한다. 껐다 켠 **같은 자리에서** 루프 속도를 비교해야
   *   RGB 몫이 나온다. rgb_matrix 없이 빌드한 펌웨어와 비교하면 다른 것도 같이
   *   달라져 몫이 안 갈린다.
   */
  if (args->argc >= 1 && args->isStr(0, "rgb"))
  {
    if (args->argc == 2 && args->isStr(1, "on"))       rgb_matrix_enable_noeeprom();
    else if (args->argc == 2 && args->isStr(1, "off")) rgb_matrix_disable_noeeprom();
    else if (args->argc == 3 && args->isStr(1, "mode"))
      rgb_matrix_mode_noeeprom((uint8_t)args->getData(2));
    else if (args->argc == 3 && args->isStr(1, "val"))
      rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue(), rgb_matrix_get_sat(),
                                 (uint8_t)args->getData(2));
    else if (args->argc != 1)
    {
      cliPrintf("qmk rgb on|off | mode <n> | val <0~%d>\n", RGB_MATRIX_MAXIMUM_BRIGHTNESS);
      return;
    }

    cliPrintf("rgb    : %s\n", rgb_matrix_is_enabled() ? "켬" : "끔");
    cliPrintf("mode   : %d / %d\n", rgb_matrix_get_mode(), RGB_MATRIX_EFFECT_MAX - 1);
    /*
     * ★ flags 를 같이 찍는다.
     *
     *   rgb_matrix 는 LED 마다 g_led_config.flags 를 갖고, 렌더할 때
     *   rgb_matrix_config.flags 와 겹치는 것만 그린다. 이 값이 EEPROM 에
     *   저장되므로 예전 값이 남아 **키 LED 만 통째로 안 그려지는** 일이 생긴다.
     *   0xFF = 전부, 0x02 = 언더글로우, 0x04 = 키.
     */
    cliPrintf("flags  : 0x%02X  (0xFF=전부, 0x04=키, 0x02=언더글로우)\n",
              rgb_matrix_get_flags());
    cliPrintf("hsv    : %d %d %d  (밝기 상한 %d)\n",
              rgb_matrix_get_hue(), rgb_matrix_get_sat(), rgb_matrix_get_val(),
              RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    ret = true;
  }
#endif

#ifdef RGB_MATRIX_ENABLE
  /* 효과가 보는 깊이를 그 자리에서 그대로 찍는다 */
  if (args->argc == 1 && args->isStr(0, "led"))
  {
    extern void heDbgLed(uint8_t, uint8_t *, uint8_t *, uint16_t *, uint16_t *, uint8_t *);
    {
      /*
       * ★ 위쪽과 언더글로우를 **따로** 센다.
       *
       *   언더글로우는 판 전체의 최대를 돌려주므로 언제나 개별 키보다 크거나
       *   같다. 전체 최대만 찍으면 항상 언더글로우가 이겨서, 정작 보려던
       *   "위쪽이 0인가" 를 전혀 못 본다. 처음에 그렇게 재고 헤맸다.
       */
      uint8_t  kbest = 0, kr = 0, kc = 0, ki = 0, ubest = 0;
      uint16_t ktv = 0, kdp = 0;

      for (uint32_t i = 0; i < RGB_MATRIX_LED_COUNT; i++)
      {
        uint8_t r, c, d8; uint16_t tv, dp;

        heDbgLed((uint8_t)i, &r, &c, &tv, &dp, &d8);
        if (r == 0xFF) { if (d8 > ubest) ubest = d8; continue; }
        if (d8 > kbest) { kbest = d8; ki = (uint8_t)i; kr = r; kc = c; ktv = tv; kdp = dp; }
      }
      cliPrintf("위쪽 최대 led %d (%d,%d) travel %d depth %d d8 %d   |   언더 d8 %d\n",
                ki, kr, kc, ktv, kdp, kbest, ubest);
    }
    ret = true;
  }
#endif

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    /*
     * ★ 여기서 필드를 손으로 지우지 않는다.
     *
     *   예전에는 이 자리와 qmkClearStat() 두 곳이 같은 목록을 따로 들고 있었다.
     *   그래서 루프 간격 카운터를 넣었을 때 한쪽에만 들어가 **`qmk reset` 이
     *   그것만 안 지웠다** — 재고 있는 값이 늘 옛날 최대치라 새로 잰 창인 줄
     *   알고 한참 헤맸다. 목록은 한 곳에만 둔다.
     */
    qmkClearStat();
    task_us_last       = 0;
    rgb_us_last        = 0;
    task_over_ms_first = 0;
    task_over_ms_last  = 0;
    cliPrintf("통계 리셋\n");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "clear") && args->isStr(1, "eeprom"))
  {
    eeconfig_init();
    eeprom_flush();
    cliPrintf("EEPROM 초기화\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("qmk start\n");
    cliPrintf("qmk info\n");
#ifdef RGB_MATRIX_ENABLE
    cliPrintf("qmk rgb on|off | mode <n> | val <n>\n");
#endif
    cliPrintf("qmk rate\n");
    cliPrintf("qmk log        USB 로 나가는 부트 리포트를 찍는다\n");

    cliPrintf("qmk held      호스트가 눌린 것으로 아는 키 (NKRO 리포트)\n");
    cliPrintf("qmk proto [0|1]  HID 프로토콜 (시험용 — 0 은 BIOS 가 쓰는 부트)\n");
    cliPrintf("qmk reset\n");
    cliPrintf("qmk clear eeprom\n");
  }
}
