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
    cliPrintf("qmk reset\n");
    cliPrintf("qmk clear eeprom\n");
  }
}
