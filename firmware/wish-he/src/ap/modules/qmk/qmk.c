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
#include "ap.h"
#include "usb/cherryusb/hid_kbd_if.h"
#include "matrix.h"
#include "dynamic_keymap.h"
#include "keycode_config.h"
#include <string.h>

extern void viaPortInit(void);   /* keyboards/<보드>/via_port.c */


extern host_driver_t usb_driver;   /* port/driver_usb.c */

static void cliQmk(cli_args_t *args);

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


/* CLI 만 먼저 등록한다. QMK 를 켜기 전에도 `qmk start` 를 칠 수 있어야 한다. */
bool qmkCliInit(void)
{
  cliAdd("qmk", cliQmk);

  /*
   * 여기서 QMK 를 켠다. CLI 등록을 먼저 하는 것은, 기동에 실패해도 `qmk info` 로
   * 상태를 볼 수 있게 하기 위해서다.
   */
  return apQmkStart();
}

bool qmkInit(void)
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

void qmkUpdate(void)
{
  uint32_t t0 = micros();
  uint32_t dt;

  keyboard_task();

  dt = micros() - t0;
  task_us_last = dt;
  if (dt > task_us_max) task_us_max = dt;
  task_us_sum += dt;
  task_us_cnt++;

  eeprom_task();
}


static void cliQmk(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "start"))
  {
    cliPrintf("qmkInit() ...\n");
    cliPrintf("%s\n", apQmkStart() ? "OK" : "실패");
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
    cliPrintf("               (max 가 125us 를 넘으면 폴링을 놓친다)\n");
    /*
     * 매직 스왑이 켜져 있으면 매트릭스도 키맵도 맞는데 나가는 코드만 달라진다.
     * 한 번 당했으니 늘 보이게 둔다.
     */
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
  if (args->argc == 1 && args->isStr(0, "log"))
  {
    uint8_t prev[8] = { 0, };

    cliPrintf("키를 누르면 리포트를 찍는다 — Ctrl-C 로 끝낸다\n");
    cliPrintf("  mods  keys[6]\n");

    while (cliKeepLoop())
    {
      uint8_t now[8];

      hidKbdGetReportRaw(now);
      if (memcmp(now, prev, sizeof(now)) != 0)
      {
        cliPrintf("  0x%02X  ", now[0]);
        for (uint32_t i = 2; i < 8; i++) cliPrintf("%02X ", now[i]);

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

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    task_us_last = 0;
    task_us_max  = 0;
    task_us_sum  = 0;
    task_us_cnt  = 0;
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
    cliPrintf("qmk rate\n");
    cliPrintf("qmk log        USB 로 나가는 부트 리포트를 찍는다\n");
    cliPrintf("qmk reset\n");
    cliPrintf("qmk clear eeprom\n");
  }
}
