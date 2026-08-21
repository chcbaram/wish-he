/*
 * port/matrix.c  —  QMK matrix <-> HE 스캐너(keys.c) 다리
 *
 * ★ 디바운스를 부르지 않는다.
 *
 *   QMK 의 matrix 계층은 접점 스위치를 전제하고 만들어졌다. 채터링을 걸러내려고
 *   변화를 몇 ms 붙잡아 두는데, 홀이펙트는 접점이 없어 바운스가 없다. 그러니
 *   디바운스는 걸러낼 것 없이 **지연만 더한다** (기본 5ms, 게이밍 프리셋도 수 ms).
 *
 *   대신 잡음은 keys.c 가 이미 처리한다 — 데드밴드 필터로 ADC 흔들림을 죽이고,
 *   눌림/해제 임계값을 갈라(히스테리시스) 경계에서 떨리지 않게 한다. 지연이 0 인
 *   방식들이라 디바운스를 대신하기에 알맞다.
 *
 * 스캔도 여기서 돌리지 않는다. keys.c 가 자유 구동으로 26kHz 쯤 돌고 있고, 우리는
 * 그 결과 비트마스크를 가져오기만 한다. QMK 루프 주기와 스캔 주기를 떼어놓는 것이
 * 핵심이다 — QMK 가 느려도 래피드 트리거 판정은 제 주기로 돈다.
 */

#include "matrix.h"
#include "keyboard.h"
#include "util.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cli.h"
#include "keys.h"


static matrix_row_t matrix[MATRIX_ROWS];
static uint32_t     scan_us_last = 0;
static uint32_t     scan_us_max  = 0;

static void cliMatrix(cli_args_t *args);


void matrix_init(void)
{
  memset(matrix, 0, sizeof(matrix));

  /* keysInit() 은 hw.c 에서 이미 끝났다. 여기서 하드웨어를 또 건드리지 않는다. */

  cliAdd("matrix", cliMatrix);
}

void matrix_print(void)
{
}

bool matrix_can_read(void)
{
  return true;
}

matrix_row_t matrix_get_row(uint8_t row)
{
  return matrix[row];
}

uint8_t matrix_scan(void)
{
  matrix_row_t curr[MATRIX_ROWS];
  matrix_row_t changed = 0;
  uint32_t     t0 = micros();
  uint32_t     dt;

  keysLatMarkScan();      /* 지연 눈금 ②의 앞 (keys.c 의 지연 절) */

  /*
   * keys 명령(매핑·보정·관측)이 도는 동안에는 전부 안 눌린 것으로 준다.
   *
   * 측정하려고 누른 키가 호스트로 입력되면 터미널이 엉켜 측정을 못 한다. 여기서
   * 0 을 주면 QMK 가 스스로 "다 뗐다"고 보고 빈 리포트를 내보낸다 — 눌린 채로
   * 남는 키가 없다.
   */
  bool live = keysIsReportEnabled();

  for (uint8_t r = 0; r < MATRIX_ROWS; r++)
  {
    curr[r]  = live ? keysGetRow(r) : 0;   /* 비트 순서가 같아 그대로 받는다 */
    changed |= (matrix_row_t)(curr[r] ^ matrix[r]);
  }

  if (changed) memcpy(matrix, curr, sizeof(matrix));

  keysLatMarkConv();      /* 지연 눈금 ②의 뒤 */

  dt = micros() - t0;
  scan_us_last = dt;
  if (dt > scan_us_max) scan_us_max = dt;

  return (changed != 0);
}


static void cliMatrix(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("MATRIX      : %d x %d\n", MATRIX_ROWS, MATRIX_COLS);
    cliPrintf("디바운스    : 없음 (HE 는 바운스가 없다)\n");
    cliPrintf("matrix_scan : last %d us, max %d us\n",
              (int)scan_us_last, (int)scan_us_max);
    cliPrintf("keys 스캔   : %d us / 바퀴\n", (int)keysGetScanTime());
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {
    cliPrintf("Ctrl-C 로 끝낸다\n");
    while (cliKeepLoop())
    {
      cliPrintf("\x1B[%dA", MATRIX_ROWS);
      for (uint8_t r = 0; r < MATRIX_ROWS; r++)
      {
        cliPrintf("  r%d ", r);
        for (uint8_t c = 0; c < MATRIX_COLS; c++)
        {
          cliPrintf("%c", (matrix[r] & (1U << c)) ? '#' : '.');
        }
        cliPrintf("\n");
      }
      delay(50);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    scan_us_max = 0;
    cliPrintf("max reset\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("matrix info\n");
    cliPrintf("matrix show\n");
    cliPrintf("matrix reset\n");
  }
}
