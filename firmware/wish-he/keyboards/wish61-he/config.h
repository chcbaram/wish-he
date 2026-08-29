/*
 * wish61-he — 61키 HE 보드 설정
 *
 * ★ 브링업 단계다. 아직 드라이버가 이 값을 쓰지 않는다 — 알아낸 것을 먼저 적어둔다.
 *
 *   이 파일은 빌드가 모든 C 파일 맨 앞에 강제로 넣는다 (-include config.h).
 *   키 스캔 · RGB · QMK · VIA 는 이 저장소에서 지웠으므로 그쪽 설정도 아직 없다.
 *   드라이버를 하나씩 되살릴 때 여기 값을 쓰도록 고쳐 나간다.
 *
 * ★ 모르는 것은 적지 않는다. 빈칸이 틀린 값보다 안전하다.
 */

#ifndef CONFIG_H_
#define CONFIG_H_


/* ------------------------------------------------------------------ *
 *  MCU
 * ------------------------------------------------------------------ *
 *  HPM5361 LQFP100, 24MHz XTAL(74/75핀), SiP 내장 XIP NOR 1MB
 *  JTAG  TDI=PA05(1)  TDO=PA04(2)  TMS=PA07(99)  TCK=PA06(100)
 *        ★ PA05/PA06 을 GPIO 로 리먹싱하지 말 것 — JTAG 이 죽는다
 *  UART0 PA00(TXD, 25핀) / PA01(RXD, 24핀) — 벤더 IAP 도 콘솔로 쓰는 핀이다
 * ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ *
 *  아날로그 — MUX 8개 x 8스텝 = 64채널로 61키
 * ------------------------------------------------------------------ *
 *
 *  벤더 앱 디스어셈블에서 뽑았다. 근거는 세 갈래로 맞아떨어진다.
 *
 *    1. 패드 인덱스가 `포트*32 + 핀` 이라는 규칙이 세 테이블에서 일관된다
 *       (0x1C0 = 14*32 = PY00,  0x009 = 0*32+9 = PA09,  0x022 = 1*32+2 = PB02)
 *    2. 테이블의 두 번째 워드가 ADC 채널이고, 값이 전부 유효 범위(0~15)이며
 *       중복이 없다 — 8개 채널이 8개 패드에 1:1 로 붙는다
 *    3. MUX 주소를 GPIOY 에 두는 것이 우리 설계(keys.c)와 같다
 *
 *  아날로그 입력 — IOC 패드 0x022~0x029 를 FUNC_CTL=0x100(ANALOG) 로 설정한다
 *
 *      MAG_CH   패드    핀     ADC 채널
 *        0      0x022  PB02      IN12
 *        1      0x023  PB03      IN8
 *        2      0x024  PB04      IN0
 *        3      0x025  PB05      IN13
 *        4      0x026  PB06      IN9
 *        5      0x027  PB07      IN10
 *        6      0x028  PB08      IN11
 *        7      0x029  PB09      IN1
 *
 *  ★ 실측으로 확인했다 (2026-08-28). 위 값 그대로 스캔이 돈다.
 *
 *      64칸 전부 2124~2714 (12비트 환산) 에 안정적으로 들어온다.
 *      재측정 차이 평균 28/40000 = 0.07%.
 *      키를 하나 누르니 **step5 ch3 만 -911 카운트** 움직이고 나머지 63칸은 0~-4 였다.
 *      -> 패드·채널·MUX 주소가 모두 맞고, 잡음 바닥이 눌림의 0.4% 로 조용하다.
 *      -> **누르면 값이 준다** (자석이 가까워질수록 감소).
 *
 *  ★★ 남은 것 — **채널 <-> 핀 대응의 출처가 둘인데 서로 다르다.**
 *
 *      hall-keyboard-design-spec.md 11.2   PB02=IN12  PB03=IN8  PB04=IN0 ...
 *      wish-he 의 keys.c 주석              ch15 -> PB07,  ch8 -> PB00 ...
 *
 *    ADC 채널과 패드의 대응은 실리콘이 정하므로 둘 다 맞을 수 없다. SDK 헤더에는
 *    아날로그가 ALT 가 아니라 별도 비트(0x100)로 돼 있어 매핑이 없다 —
 *    **데이터시트로 확인해야 한다.**
 *
 *    설계 문서 쪽이 근거가 두 겹이다 (11.2 "데이터시트 확인" + 2.1절 EVK 회로도
 *    역추출과 16핀 전부 일치). keys.c 주석이 옛 리비전 잔재일 가능성이 크지만
 *    확인 전에는 어느 쪽도 확정으로 쓰지 않는다.
 *
 *    ★ 위의 "핀" 열은 설계 문서 표를 따른 것이다. **패드 번호(0x022~0x029)와 채널
 *      번호는 벤더 바이너리에서 직접 읽은 값이라 확실하다** — 흔들리는 것은 그 둘을
 *      잇는 대응뿐이고, 스캔이 실제로 도는 것을 확인했으므로 **실용적으로는 막히지
 *      않는다.** 드라이버는 패드와 채널만 쓴다. 어느 물리 키가 어느 (step, ch) 인지는
 *      키를 하나씩 눌러 움직이는 칸을 보면 된다.
 *
 *  ★ ADC0 / ADC1 배분도 아직 모른다. 채널 0·1 은 ADC0 대역, 8~13 은 ADC1 대역이라
 *    6:2 로 갈리는 셈인데 그대로일지 봐야 한다.
 *    (우리 설계는 ADC0=IN0~7 / ADC1=IN8~15 로 완전 분리했다)
 *
 *  ★ wish60-he 와 비교
 *      MUX 주소   GPIOY PY00~ 로 **동일** (wish60 은 4핀, 여기는 3핀)
 *      채널       {0,8,9,10,12,13} 6개 공통. wish60 만 {14,15}, 여기만 {1,11}
 *      패드 설정  wish60 은 PB00~PB15 16개 전부 ANALOG, 여기는 8개만
 */
/* HW_KEYS_CH_MAX / HW_KEYS_STEP_MAX 는 hw_def.h 가 갖고 있다 (둘 다 8). */

#define HW_KEYS_ANALOG_PAD_FIRST    0x022   /* IOC_PAD_PB02 */
#define HW_KEYS_ANALOG_PAD_CNT      8       /* PB02 ~ PB09 연속 */

/* ADC 채널 — 위 표의 순서 그대로 */
#define HW_KEYS_ADC_CH_LIST         { 12, 8, 0, 13, 9, 10, 11, 1 }

/*
 * ADC0 / ADC1 분배 — 앞 4개와 뒤 4개.
 *
 * ★ 실측으로 확인했다 (2026-08-28). `scan cmp` 로 두 ADC 에 같은 8채널을 물려 재니
 *   여덟 칸 모두 값이 같았다 (한 칸만 5 차이, 잡음 바닥 안). **같은 PB 패드가
 *   ADC0.INx 와 ADC1.INx 로 둘 다 보인다** — 분할을 어떻게 잡든 자유롭다.
 *
 * ★ 벤더도 똑같이 가른다. 앱의 핀맵 표(0x8003AE44, 8바이트 x 8)를 4개씩 끊어
 *   앞 넷은 ADC0, 뒤 넷은 ADC1 에 시퀀스로 넣는다. 우연이 아니라 표 순서가 곧
 *   패드 순서(PB02~PB09)이기 때문이다.
 *
 *   ADC0  PB02 PB03 PB04 PB05   ch 12  8  0 13
 *   ADC1  PB06 PB07 PB08 PB09   ch  9 10 11  1
 */
#define HW_KEYS_ADC0_SEQ_CH         { 12, 8, 0, 13 }
#define HW_KEYS_ADC1_SEQ_CH         { 9, 10, 11, 1 }

/*
 * 부팅 보정 이상치 문턱 (12비트 눈금).
 *
 * 기준값이 전체 중앙값보다 이만큼 아래면 "그 키는 눌린 채로 측정됐다" 고 본다.
 * **정상 편차와 스트로크 사이**에 놓여야 하는데, 그 구간이 보드마다 다르다.
 *
 * ★ wish60-he 값(500)을 그대로 쓰면 안 된다 — 실측으로 확인했다 (2026-08-28).
 *
 *     정상 편차 최대   499   (RShift, s7ch0)   ← 중앙값보다 이만큼 낮게 태어났다
 *     wish60-he 문턱   500                     ← 여유 1카운트. 온도만 변해도 뒤집힌다
 *     스트로크         836
 *
 *   RShift 가 유난히 낮다. 다음으로 낮은 RAlt 가 325 이니 그 하나가 튀는 것이고,
 *   둘 다 바닥 오른쪽(ch0) 무리다. 2.75u 라 스태빌라이저가 있는 키인데 원인은
 *   확인하지 못했다 — **원인과 무관하게 문턱은 실측 위에 놓여야 한다.**
 *
 * ★ 650 은 두 경계의 가운데다.  499 <-- 151 -- 650 -- 186 --> 836
 *   양쪽 여유가 비슷하다. 편차가 커지거나 스위치가 바뀌면 다시 잰다:
 *
 *       cli# keys base        (전 셀 기준값 -> 중앙값과의 차)
 *       cli# keys noise 3000  (스트로크)
 */
#define HW_KEYS_CAL_OUTLIER_12B     650

/*
 * MUX 세틀링 — 주소를 쓴 뒤 아날로그가 안정될 때까지 도는 nop 수.
 *
 * ★ wish60-he 와 같은 16 이면 충분하다. **재 보고 확인했다** (2026-08-28).
 *
 *   Fn 을 누르면 스캔 순서로 이웃한 세 칸이 같이 움직여서 세틀링을 의심했는데,
 *   `keys settle` 로 16 -> 3000 까지 올려도 세 값이 카운트 단위로 똑같았다.
 *   세틀링이 아니라 **미연결 칸의 샘플홀드 잔류 전하**였다 (cellmap.h 참조).
 *   기다릴 대상이 없으니 시간을 줘도 변할 리가 없다.
 *
 * ★ `keys settle <n>` 은 남겨 둔다. 이런 증상에서 세틀링인지 아닌지를 굽지 않고
 *   한 번에 가를 수 있는 것이 값이 있다 — 실제로 이걸로 갈랐다.
 */
#define HW_KEYS_SETTLE_CYCLES       16

/*
 * 아날로그 패드를 잡는 개수.
 *
 * wish60-he 는 PB00~PB15 열여섯을 전부 ANALOG 로 두지만 여기는 여덟뿐이다.
 * 남는 PB 핀이 무엇에 쓰이는지 모르므로 **필요한 만큼만 건드린다.**
 */


/* ------------------------------------------------------------------ *
 *  RGB — SPI MOSI 2채널 + DMA
 * ------------------------------------------------------------------ *
 *
 *  초기화가 `0x80033314` 에 있다. 16바이트 엔트리 표를 `0x8002ECC6` 이 소비한다.
 *
 *      0x8003AD98  { SPI3(0xF007C000), 0, pad 0x0D, alt 5 }  -> PA13 = SPI3_MOSI
 *      0x8003ADA8  { SPI2(0xF0078000), 1, pad 0x17, alt 5 }  -> PA23 = SPI2_MOSI
 *
 *  소비 함수는 `IOC->PAD[pad].FUNC_CTL = alt` 를 쓰고 SPI 베이스로 클럭을 고른다.
 *  체인 2개, SPI + DMA — **wish-he 와 같은 구조다.** PA23/SPI2 는 wish-he 의 오른쪽
 *  채널과 아예 같은 핀이다 (wish-he 왼쪽은 PA29/SPI1, 여기는 PA13/SPI3).
 *
 *  ★ 그래서 wish-he 의 ws2812.c 를 거의 그대로 쓸 수 있다. SPI 인스턴스와 핀만 바꾸면
 *    된다 (SPI1->SPI3, PA29->PA13). 비트 인코딩과 DMA 경로는 손댈 것이 없다.
 *
 *  ★★ RGB 는 JTAG 을 잡아먹지 않는다.
 *
 *    JTAG 이 죽는 원인은 따로 있다 — 벤더가 ADC 를 주기 트리거하려고
 *    (GPTMR1 -> TRGM0 -> ADC) `IOC->PAD[0x05] = ALT1`(GPTMR1_COMP_2) 을 쓰는데,
 *    PA05 가 JTAG TDI 다. RGB 와는 무관하다.
 *
 *    우리는 wish-he 처럼 ADC 를 소프트웨어로 트리거하면 되므로(adc16_trigger_seq_by_sw)
 *    **PA05 를 건드릴 이유가 없다. RGB 두 채널과 JTAG 을 동시에 갖는다.**
 *
 *  ★ LED 개수와 키-LED 배치는 아직 모른다. 핀을 잡고 실측으로 센다.
 */
#define HW_RGB_SPI_CH0              0xF007C000  /* SPI3 */
#define HW_RGB_PAD_CH0              0x00D       /* PA13 = SPI3_MOSI */
#define HW_RGB_SPI_CH1              0xF0078000  /* SPI2 */
#define HW_RGB_PAD_CH1              0x017       /* PA23 = SPI2_MOSI */
#define HW_RGB_PAD_ALT              5           /* 둘 다 ALT5 가 MOSI 다 */

/*
 * 체인별 LED 개수 — **실측 확정** (2026-08-29).
 *
 *   체인 0 = 키 LED     65개.  Backspace 에서 시작해 왼쪽으로 간다
 *   체인 1 = 언더글로우 39개.  Backspace 근처에서 외곽을 오른쪽으로 돈다
 *
 * ★ "키마다 LED 2개" 는 체인 위치가 둘이라는 뜻이 아니다.
 *
 *   `ws2812 range 0 64` 65개로 **전 키가 켜진다.** 즉 한 키의 두 LED 는 같은 체인
 *   위치를 나눠 갖는다 — 데이터를 물고 다음으로 넘기는 칩 하나에 다른 하나가
 *   같은 신호로 붙어 **늘 같은 색**으로 켜진다. 다른 것은 전원 도메인뿐이다
 *   (PA10 = 위, PA11 = 아래). 그래서 소프트웨어에서는 **한 자리로 다룬다.**
 *
 * ★ 키가 61개인데 자리가 65개인 것은 넓은 키에 여러 개가 들어가서다.
 *   어느 키에 몇 개인지는 아직 안 세었다 — RGB 배치를 만들 때 확인한다.
 */
/*
 * LED 전원 — GPIOA 출력, HIGH 가 켬. **실측으로 갈랐다** (2026-08-29).
 *
 *   PA09  언더글로우
 *   PA10  키 LED 위쪽   (각인을 비춘다)
 *   PA11  키 LED 아래쪽 (옆/아래를 비춘다)
 *
 * ★ 안 세우면 SPI 는 나가는데 아무것도 안 켜진다. 실제로 그 증상으로 한참 헤맸다.
 *
 * ★ 벤더도 같다 — 앱의 GPIO 초기화 표(0x8003ADB8)에 셋이 {FUNC_CTL=0(GPIO),
 *   초기값 1} 로 들어 있다. 바로 옆 표(0x8003ADF4)의 MUX 주소 핀은 초기값 0 이라
 *   마지막 워드가 레벨인 것이 확실하다.
 *
 * ★ PWM 이 아니다. IOC 가 GPIO 로 잡혀 있어 주변장치가 못 물고, GPIOA 의 DO
 *   set/clear 를 쓰는 코드가 초기화 헬퍼뿐이다. 즉 **한 번 세우고 그대로 둔다.**
 *   밝기는 우리도 소프트웨어 전류 리미터로 잡으므로 시분할이 필요 없다.
 */
/*
 * 전류 모델 — **전류계로 실측했다** (2026-08-29). 전 LED 흰색, 리미터 해제 상태.
 *
 *     level     0    16    32    64    96
 *     mA      126   297   410   636   853
 *
 *   직선이다. 최소자승으로 **기울기 6.92 mA/level, 절편 189 mA**.
 *
 * ★ 절편이 소등값(126)보다 62 높다. **LED 가 켜지는 순간 붙는 오버헤드**다
 *   (WS2812 칩 구동분). 모델에 안 넣으면 리미터가 딱 그만큼 초과한다.
 *   그래서 IDLE 에 접어 넣는다 — 소등일 때는 62 를 과대평가하지만, 그때는 제한할
 *   것이 없으므로 손해가 없고 **켜졌을 때 정확해진다.**
 *
 * ★ 무리별 배분도 따로 쟀다 (흰색 96 기준).
 *
 *     키 65개만    680 mA      언더글로우 39개만   309 mA
 *     둘의 LED 분 합 737 vs 전체 727 — 1.4% 안에서 맞는다 (오버헤드 이중계산분)
 *
 *   255 환산하면 채널당 **키 6.89 mA / 언더 3.83 mA**. 비율 1.80 으로,
 *   "키 한 자리에 물리 LED 2개" 와 맞는다.
 *
 * ★ wish60-he 값(11.51 / 4.66 / 269)을 그대로 쓰면 안 된다. 그 보드는 LED 83개에
 *   키당 1개고 보드 소비도 269 mA 다 — 여기에 쓰면 3.5배 과대평가해서 필요 없는
 *   밝기 제한이 걸린다. 실제로 그 상태에서 `limit hit` 이 계속 올라갔다.
 *
 *   다시 재려면 : `ws2812 limit 3000` 으로 풀고 `ws2812 all <v> <v> <v>` 를
 *   몇 단계 돌리며 전류계를 읽는다. 끝나면 `ws2812 limit 450`.
 */
#define HW_RGB_IDLE_MA              188     /* 소등 126 + 켜짐 오버헤드 62 */
#define HW_RGB_CH_FULL_UA_KEY      6890     /* 키 LED 채널 1개 풀스케일 */
#define HW_RGB_CH_FULL_UA_UNDER    3830     /* 언더글로우 채널 1개 풀스케일 */

#define HW_RGB_PWR_PORT             0       /* GPIO_DO_GPIOA */
#define HW_RGB_PWR_PIN_FIRST        9       /* PA09 ~ PA11 */
#define HW_RGB_PWR_PIN_CNT          3

#define HW_RGB_LED_CNT_CH0          65
#define HW_RGB_LED_CNT_CH1          39

/*
 * 체인 표. 한 줄이 체인 하나다 —
 *   { SPI, 클럭, DMA 채널, DMAMUX 소스, 패드, ALT, 전역 LED 시작 번호, 개수 }
 *
 * 전역 LED 인덱스는 체인 순서대로 이어 붙는다. rgb_buf 는 하나고 체인이 나눠 갖는다.
 */
#define HW_RGB_CHAIN_CNT            2
#define HW_RGB_CHAINS                                                          \
{                                                                              \
  { HPM_SPI3, clock_spi3, HW_DMA_CH_WS2812_0, HPM_DMA_SRC_SPI3_TX,             \
    IOC_PAD_PA13, 5, 0,                  HW_RGB_LED_CNT_CH0 },                 \
  { HPM_SPI2, clock_spi2, HW_DMA_CH_WS2812_1, HPM_DMA_SRC_SPI2_TX,             \
    IOC_PAD_PA23, 5, HW_RGB_LED_CNT_CH0, HW_RGB_LED_CNT_CH1 },                 \
}

/* 앞에서부터 이만큼이 키 LED — 여기서는 체인 0 이 통째로 키다 */
#define HW_RGB_KEY_LED_CNT          HW_RGB_LED_CNT_CH0


/* ------------------------------------------------------------------ *
 *  MUX 주소 — PY00 / PY01 / PY02  (3비트 = 8스텝)
 * ------------------------------------------------------------------ *
 *  출력으로 잡고 초기값 LOW. 패드가 0x1BF 를 넘으므로 PIOC 도 함께 설정해야
 *  한다 (전원 도메인 패드라 그렇다 — 벤더 앱도 그 분기를 탄다).
 */
#define HW_KEYS_MUX_PORT            14      /* GPIO_DO_GPIOY */
#define HW_KEYS_MUX_PAD_FIRST       0x1C0   /* IOC_PAD_PY00 */
#define HW_KEYS_MUX_PIN_CNT         3


/* ------------------------------------------------------------------ *
 *  아직 모르는 것
 * ------------------------------------------------------------------ *
 *
 *  PA05
 *      벤더가 GPTMR1_COMP_2(ALT1)로 잡는다. ADC 주기 트리거(GPTMR1 -> TRGM0 -> ADC)
 *      쪽 코드다. **PA05 는 JTAG TDI 이므로 우리는 건드리지 않는다** — ADC 는 소프트웨어
 *      트리거로 충분하다.
 *
 *  PA09 / PA10 / PA11
 *      벤더 앱이 출력으로 잡고 **초기값 HIGH** 로 둔다. 셋이라 MUX 인에이블
 *      (active-low) 이나 센서·LED 전원 스위치로 보이는데 확인하지 못했다.
 *      우리 설계에서는 이 세 핀이 MUX 주소였다 — 헷갈리기 쉬우니 주의.
 *
 *  RGB — LED 개수와 배치만 미상. 핀은 찾았다 (아래 참조).
 *
 *  I2C 장치
 *      앱 문자열에 `i2c reset` / `i2c loss` 가 있다. 무엇이 붙어 있는지 미상.
 *
 *  스토리지
 *      0xA0000~0x100000 384KB 가 우리 것이다 (벤더 IAP 가 안 건드린다).
 */


/* ------------------------------------------------------------------ *
 *  QMK / VIA
 * ------------------------------------------------------------------ *
 *  매트릭스는 하드웨어 그대로다. row = 스캔 루프 인덱스, col = ADC 채널.
 *  배치·키맵은 전부 layout-kle.json 에서 생성된다 (tools/gen_keymap.py).
 * ------------------------------------------------------------------ */

/*
 * ★ layout.h 의 KEYS_LAYOUT_ROWS/COLS 와 같은 값이어야 한다. 참조하지 않고 적는
 *   이유는 이 파일이 **-include 로 모든 C 파일 맨 앞에 강제로 들어가서** layout.h
 *   보다 먼저 오기 때문이다. 8x8 은 하드웨어(MUX 8스텝 x ADC 8채널)라 안 변한다.
 */
#define MATRIX_ROWS                 8
#define MATRIX_COLS                 8

/*
 * ★ 디바운스를 쓰지 않는다. HE 는 접점이 없어 바운스가 없다.
 *   port/matrix.c 가 debounce() 를 아예 부르지 않으므로 QMK 헤더용 자리표시자다.
 */
#define DEBOUNCE                    0

/* EEPROM — 플래시 0x0C4000 에 16KB (hw_def.h 의 HW_FLASH_E2P_*) */
#define TOTAL_EEPROM_BYTE_COUNT     16384
#define DYNAMIC_KEYMAP_LAYER_COUNT  8

/* 키맵도 프로파일마다 한 벌 둔다 — 게임용에서는 배치도 함께 달라진다 */
#define KEYMAP_PROFILE_COUNT        4

#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR                                     \
  (DYNAMIC_KEYMAP_EEPROM_ADDR +                                                \
   (KEYMAP_PROFILE_COUNT * DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS *          \
    MATRIX_COLS * 2))
#define EECONFIG_USER_DATA_SIZE     512

#define VIA_FIRMWARE_VERSION        1

/*
 * RGB 매트릭스 — **실측 104개** (키 65 + 언더글로우 39).
 *
 * 배치(g_led_config)는 rgb_config.c 에 생성된다. ★ 아직 `gen_keymap.py` 가
 * wish60-he 의 기하 규칙(키당 1개 + 넓은 키 3개, 언더글로우 18)으로 뽑고 있어
 * 개수가 안 맞는다 — **체인 순서를 눈으로 확인하고 생성기를 고쳐야 한다.**
 */
#define RGB_MATRIX_LED_COUNT        (HW_RGB_LED_CNT_CH0 + HW_RGB_LED_CNT_CH1)

/*
 * 밝기 상한 — **실측 전류 모델에서 뽑았다.**
 *
 *   상한 450 mA 에서 IDLE(188)을 빼면 LED 몫이 262 mA 다. 전 LED 흰색이면
 *   level 당 7.03 mA 를 쓰므로 **흰색으로 안 걸리는 최대는 37** 이다.
 *
 * ★ 그런데 37 로 두면 안 된다. 최악(흰색)은 거의 오지 않는다 — 유채색은 RGB 세
 *   채널 중 하나만 켜지므로 같은 밝기에서 전류가 1/3 이다. 37 로 자르면 실제로
 *   쓰는 색에서 슬라이더가 아무 의미 없이 어둡기만 하다.
 *
 *   그래서 **3배인 110** 으로 둔다. 유채색은 여기까지 리미터에 안 걸리고, 흰색은
 *   소프트웨어 리미터가 알아서 깎는다. wish60-he 도 같은 논리로 18 -> 54 였다.
 *
 *   확인은 `ws2812 info` 의 frame current 와 limit hit 으로 한다.
 */
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS   110

/* 호스트가 자면 LED 를 끈다 */
#define RGB_MATRIX_SLEEP
/* 반응형 효과가 눌린 키를 알아야 한다 */
#define RGB_MATRIX_KEYPRESSES

/* 효과 — 전부 켜지 않는다. 성격이 겹치지 않는 것만 고른다 */
#define ENABLE_RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define ENABLE_RGB_MATRIX_CYCLE_ALL
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#define ENABLE_RGB_MATRIX_PIXEL_FLOW

/* HE 라서 만들 수 있는 것들 (rgb_matrix_kb.inc) — 깊이가 연속값이라 손가락을 따라간다 */
#define RGB_MATRIX_CUSTOM_KB
#define ENABLE_RGB_MATRIX_HE_DEPTH
#define ENABLE_RGB_MATRIX_HE_DEPTH_HUE
#define ENABLE_RGB_MATRIX_HE_DEPTH_RIPPLE
#define RGB_MATRIX_DEFAULT_MODE     RGB_MATRIX_CUSTOM_HE_DEPTH

#define RGB_MATRIX_LED_PROCESS_LIMIT    ((RGB_MATRIX_LED_COUNT + 4) / 5)
#define RGB_MATRIX_LED_FLUSH_LIMIT      16



/*
 * QMK 의 LAYOUT() 매크로. keymap.c 가 쓴다.
 *
 * ★ 맨 끝이어야 한다 — 위의 MATRIX_ROWS/COLS 를 참조한다.
 *   이 파일이 -include 로 모든 C 파일 맨 앞에 들어가므로 keymap.c 도 자동으로 본다.
 */
#include "layout_qmk.h"


#endif
