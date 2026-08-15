/*
 * keys.c  —  홀이펙트 키 스캔 (ADC 시퀀스 + 아날로그 MUX)
 *
 * 64키를 8채널 x 8스텝으로 읽는다. ADC0/ADC1 이 각각 4채널을 동시에 훑고,
 * 3비트 MUX 주소(PY00~PY02)를 8번 돌려 스텝을 바꾼다.
 *
 *   for step in 0..7:
 *       DO[PY].VALUE = mux_addr[step]        주소 쓰기
 *       세틀링 대기
 *       ADC0 / ADC1 시퀀스 SW 트리거
 *       완료 대기 (ISR 이 세우는 플래그를 스핀)
 *       DO[PY].VALUE = mux_addr[step + 1]    ★ 다음 주소를 결과 읽기 "전에"
 *       결과 버퍼에서 8채널 읽기
 *
 * 마지막 두 줄의 순서가 핵심이다. 다음 스텝의 세틀링이 이번 스텝의 결과 처리와
 * 겹쳐서 공짜가 된다. 그래서 주소 테이블에 되돌이용 원소가 하나 더 붙는다.
 *
 * ★ 시퀀스 DMA 는 HDMA 채널을 쓰지 않는다. ADC 가 자체 AHB 라이터로 직접 메모리에
 *   쓰므로 hw_def.h 의 DMA 채널 배분과 무관하다.
 */

#include "keys.h"


#ifdef _USE_HW_KEYS

#include "cli.h"
#include "hpm_adc16_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_interrupt.h"
#include "hpm_soc_feature.h"

/* keyboards/<모델>/layout.h — tools/gen_keymap.py 가 KLE 에서 생성한다 */
#include "layout.h"
#include "flash.h"
#include "hpm_crc32.h"


#define KEYS_SEQ_LEN        KEYS_CH_MAX_PER_ADC   /* ADC 하나가 훑는 채널 수 */

/*
 * MUX 주소 순서 — 그레이 코드.
 *
 * 스텝당 1비트만 바뀌어 인접 채널 크로스토크가 적다. 9번째 원소는 마지막 스텝에서
 * "다음 주소"를 미리 쓰기 위한 되돌이값이다 (0번 스텝 주소와 같아야 한다).
 */
static const uint8_t mux_addr[KEYS_STEP_MAX + 1] =
{
  6, 7, 5, 4, 0, 1, 3, 2,   6
};

/*
 * ADC 시퀀스 채널.
 *
 * ★ 채널 번호는 패드 번호와 다르다. PB00~PB07 -> ch8~ch15, PB08~PB15 -> ch0~ch7 로
 *   8만큼 돌아가 있다. 순서도 오름차순이 아니므로 이 배열 그대로 써야 한다.
 */
static const uint8_t adc0_seq_ch[KEYS_SEQ_LEN] = { 15, 14, 12,  8 };  /* PB07 PB06 PB04 PB00 */
static const uint8_t adc1_seq_ch[KEYS_SEQ_LEN] = {  0, 13,  9, 10 };  /* PB08 PB05 PB01 PB02 */

/* 아날로그로 잡을 패드. 실제 등록은 8개지만 16개 전부를 잡아 둔다. */
#define KEYS_ANALOG_PAD_FIRST   IOC_PAD_PB00
#define KEYS_ANALOG_PAD_CNT     16

/* MUX 주소 핀 — PY00~PY03. 주소는 3비트지만 4번째 핀도 출력으로 고정한다. */
#define KEYS_MUX_GPIO_PORT      GPIO_DO_GPIOY
#define KEYS_MUX_PAD_FIRST      IOC_PAD_PY00
#define KEYS_MUX_PIN_CNT        4
#define KEYS_MUX_PIN_MASK       ((1U << KEYS_MUX_PIN_CNT) - 1U)

/* PIOC 의 ALT3 = "패드를 SoC 쪽에 넘긴다". PIOC 를 안 건드리면 IOC 설정이 먹지 않는다. */
#define KEYS_PIOC_SOC_CTRL      IOC_PAD_FUNC_CTL_ALT_SELECT_SET(3)

/*
 * 세틀링 — 주소를 쓴 뒤 아날로그가 안정될 때까지.
 *
 * 홀 센서 자체의 응답은 0.1us 미만이고 MUX 스위칭도 그 수준이라 매우 짧다.
 * 400MHz 에서 16 사이클 = 40ns.
 */
#define KEYS_SETTLE_CYCLES      16

/* 완료를 기다리다 이만큼 돌면 포기한다. 스핀이 영원히 걸리는 것만 막으면 된다. */
#define KEYS_WAIT_LIMIT         100000


/*
 * 누적 개수 — 표본 몇 개를 더해서 하나로 쓰나.
 *
 * ★ 왜 더하나. 잡음이 sqrt(N) 배로만 커지기 때문이다.
 *
 *   표본 3개를 더하면 신호는 3배, 잡음은 sqrt(3)=1.73 배가 된다.
 *
 *   ★ 실측은 1.31 배였다 — 이론의 1.73 배가 아니다.
 *
 *     누적 1개   p-p 평균 17,  눈금 0~4095    스트로크 836  대비 2.03 %
 *     누적 3개   p-p 평균 39,  눈금 0~12285   스트로크 2508 대비 1.55 %
 *
 *   생값 비율이 39/17 = 2.29 다. 완전 무상관이면 1.73, 완전 상관이면 3.0 이므로
 *   연속 스캔 사이에 상관계수 0.45 쯤이 남아 있다. 5편에서 잡은 센서 상관시간
 *   10.6us 에 스캔 간격 28us 면 상관이 0.07 이어야 하니, 이 잔여분은 센서 열잡음이
 *   아니라 전원·기준전압·MUX 쪽의 더 느린 요동이다. 누적으로는 못 지운다.
 *
 *   그래서 개수를 5, 7 로 늘려도 비례해서 좋아지지 않는다. 더 필요하면 상관 성분
 *   자체를 봐야 한다.
 *
 *   그래도 남길 값어치는 있다. 래피드 트리거가 0.1mm 를 다루는데
 *
 *     잡음 sigma (p-p/6)   0.0135mm  ->  0.0102mm     여유 7.4 sigma -> 10 sigma
 *     데드밴드 / 스트로크     0.84 %   ->   0.48 %
 *
 *   방향 반전 판정이 잡음을 따라갈 확률이 그만큼 준다.
 *
 * ★ 블록이 아니라 이동합이다.
 *
 *   3개를 모아 한 번씩 판정하면 판정 주기가 1/3 로 떨어진다. 스캔이 35kHz 라
 *   최근 3개의 합을 매 스캔 갱신해도 부담이 없으므로, 판정 횟수를 잃지 않고 같은
 *   sqrt(3) 을 얻는다.
 *
 *     블록    [1 2 3] 판정   [4 5 6] 판정
 *     이동합  [1 2 3] 판정 / [2 3 4] 판정 / [3 4 5] 판정 ...
 *
 *   대가는 이동평균의 군지연 1샘플 = 28us 다. 6편에서 IIR 을 버린 이유(63% 에
 *   152us, 90% 에 342us)와는 자릿수가 다르다.
 */
#define KEYS_ACC_CNT            3

/*
 * 부팅 씨앗값 — 두 단계다. 성격이 달라서 나눠 놓았다.
 *
 *   버리는 스캔 : 전원 인가 직후 ADC 레퍼런스·센서 바이어스가 자리잡는 시간.
 *                 평균에 넣으면 기준값이 오염되므로 그냥 버린다.
 *   평균낼 스캔 : 노이즈 감쇠. 샘플 수의 제곱근에 비례한다 (1024회 = 32배).
 *
 * ★ keysInit() 은 usbInit() 앞에 있다. 여기 쓰는 시간이 그대로 USB 열거 지연이 된다.
 *   38us 스캔 기준으로 1024+128 회면 약 44ms — 체감되지 않는다.
 *   더 길게 잡는 구현도 있지만, 우리는 러닝 최대값 추적이 계속 보정하므로
 *   씨앗값의 정밀도에 그만큼 기대지 않는다. 더 올리려면 이 숫자만 키우면 된다.
 */
#define KEYS_CAL_DISCARD        128
#define KEYS_CAL_SAMPLES        1024

/*
 * keys map 의 보고 임계값.
 *
 * 이 명령은 매 프레임 64셀 중 최댓값을 고르므로 사실상 노이즈의 극단을 본다.
 * 실측 노이즈 바닥이 약 300 이라 300 으로 두면 쉬지 않고 찍힌다. 넉넉히 띄운다.
 */
#define KEYS_MAP_REPORT_MIN     75

/* keys noise 측정 시간 */
#define KEYS_NOISE_MS           3000

/*
 * 보정 — 이 깊이까지 눌러야 "끝까지 눌렀다" 로 본다.
 *
 * 실측 풀 스트로크가 약 838 이라 그 60% 다. 너무 높게 잡으면 사용자가 아무리 눌러도
 * 안 끝나고, 낮게 잡으면 덜 눌린 값이 바닥값으로 저장된다.
 */
#define KEYS_CAL_STROKE_MIN     (500 * KEYS_ACC_CNT)

/*
 * 보정 종료 제스처. 터미널 없이 키보드만 있을 때도 끝낼 수 있어야 한다.
 * 보정 중에는 리포트를 막아두므로 조합이 호스트로 새어나가지 않는다.
 *
 * ★ 저장과 취소를 갈라야 한다. 조합을 누르는 순간 그 키들도 보정되므로, 하나로
 *   묶어두면 "취소하려고 눌렀는데 두 키짜리 보정이 저장되는" 일이 생긴다.
 */
#define KEYS_MOD_KC             0xE0   /* Left Ctrl */
#define KEYS_CANCEL_KC          0x29   /* Esc   — 취소 (저장 안 함) */
#define KEYS_SAVE_KC            0x28   /* Enter — 여기까지 저장하고 끝 */

/*
 * keys bar — 눌린 깊이를 가로 막대로.
 *
 * 스트로크가 실측 838 이라 상한을 900 으로 두면 끝까지 눌러도 막대가 넘치지 않는다.
 * 새 슬롯을 잡는 하한은 노이즈(±6)보다 충분히 커야 손 뗀 셀이 끼어들지 않는다.
 */
#define KEYS_BAR_SLOTS          6
#define KEYS_BAR_W              40
#define KEYS_BAR_FULL           (900 * KEYS_ACC_CNT)
#define KEYS_BAR_MIN            (30 * KEYS_ACC_CNT)

/* keys layout — 화면에 그릴 때 1 키유닛을 몇 칸으로 볼 것인가 */
#define KEYS_GEO_UNIT           4       /* layout.h 좌표 단위 (1키 = 4) */
#define KEYS_VIEW_UNIT          3       /* 화면 칸 */
#define KEYS_VIEW_W             72

/*
 * ★ 이 아래 숫자는 전부 12비트 영역이다 (원시 16비트를 >>4 한 값).
 *
 *   왜 버리는가 — 실측 노이즈 p-p 가 16비트로 약 200 이다. 하위 4비트(16)는 그보다
 *   한참 작아서 정보가 없다. 12비트로 내리면 테이블이 절반이 되고 상수가 읽히며,
 *   8편 EEPROM 저장량도 절반이 된다.
 *
 * 실측 (12비트 환산)
 *   기준값(무압)      약 2500 ~ 2880    (셀 간 편차 약 360)
 *   풀 스트로크       약 838            (누르면 값이 내려간다)
 *   무압 노이즈 p-p   약 12   (= ±6)
 *
 * 스트로크의 30% 에서 눌림, 19% 에서 해제. 둘 사이 간격이 히스테리시스다.
 */
/*
 * ★ 이제 판정에 쓰이지 않는다.
 *
 *   8편에 실측으로 정한 값이고 오래 기본값 노릇을 했다. 지금은 설정(0.01mm)을
 *   키별 스트로크로 풀어 만든 thr[] 이 판정을 맡는다 — VIA 슬라이더가 판정에
 *   닿게 하려면 그래야 했다. 근거를 남기려고 값만 둔다.
 *
 *     250 * 3 = 750 카운트 ~= 1.20mm,  156 * 3 = 468 ~= 0.75mm
 */
#define KEYS_PRESS_LEVEL        (250 * KEYS_ACC_CNT)
#define KEYS_RELEASE_LEVEL      (156 * KEYS_ACC_CNT)

/*
 * 기준값 추적 — 안 눌린 상태가 물리적 극단(자석이 가장 멀다)이므로 러닝 최대값이다.
 * 이 덕에 키를 누른 채 부팅해도 손을 떼는 순간 기준값이 제자리를 찾는다.
 */
/*
 * 드리프트 보정.
 *
 * 기준값이 러닝 최대값이라 구조적으로 노이즈 꼭대기에 걸린다. 평활 후 평상시 편차가
 * 약 100 남으므로 그걸 천천히 걷어내야 한다.
 *
 * ★ 밴드는 노이즈(약 120)와 액추에이션(4000) 사이여야 한다.
 *   해제 임계값(2500)까지 열면 손가락을 살짝 얹은 상태(2000)까지 보정 대상이 되어
 *   기준값이 눌린 쪽으로 끌려간다.
 *
 * ★ 주기는 스캔 횟수가 아니라 실제 시간으로 센다.
 *   스캔 속도가 호출자마다 1000배 넘게 다르다 (CLI 20회/초 vs 메인 루프 26000회/초).
 *   온도 드리프트는 물리 현상이니 ms 로 세는 게 맞다.
 */
#define KEYS_DRIFT_BAND         (50 * KEYS_ACC_CNT)
#define KEYS_DRIFT_MS           512     /* 이 시간마다 기준값을 한 걸음 움직인다 */

/*
 * ★ 걸음은 눈금을 따라간다.
 *
 *   누적을 넣으면서 눈금이 3배가 됐는데 걸음이 1카운트 그대로면 **물리적으로 3배
 *   느려진다.** 한 걸음이 스트로크의 1/836 에서 1/2508 로 줄기 때문이다. 온도
 *   드리프트를 따라잡는 속도가 그만큼 처진다.
 *
 *   그래서 걸음도 KEYS_ACC_CNT 만큼 준다 — 512ms 에 스트로크의 1/836, 누적 전과 같다.
 */
#define KEYS_DRIFT_STEP         KEYS_ACC_CNT

/*
 * 이보다 크게 해제 방향으로 벌어지면 "진짜 해제"로 보고 즉시 기준값을 옮긴다.
 * 노이즈(±6)보다 충분히 크고 스트로크(838)보다 충분히 작아야 한다.
 * 누른 채 부팅한 키가 손을 뗄 때 수백 카운트가 뛰므로 여기에 걸린다.
 */
#define KEYS_LATCH_JUMP         (31 * KEYS_ACC_CNT)

/*
 * 부팅 캘리브레이션 이상치 판정.
 *
 * 기준값이 전체 중앙값보다 이만큼 아래면 "그 키는 눌린 채로 측정됐다"고 본다.
 * 스트로크(838)와 정상 편차(360) 사이라 양쪽 모두와 안전한 거리가 있다.
 */
#define KEYS_CAL_OUTLIER        (500 * KEYS_ACC_CNT)

/* 원시 16비트를 이만큼 내려 12비트 영역으로 쓴다 */
#define KEYS_RAW_SHIFT          4



/*
 * ★ 이 아래 임계값들은 전부 "누적된 합" 눈금이다.
 *
 *   12비트 표본 하나가 아니라 3개의 합이므로 범위가 0~12285 다. 값의 근거는 8편
 *   실측(12비트)에 있으므로 그 값을 그대로 두고 KEYS_ACC_CNT 를 곱한다 —
 *   누적 개수를 바꿔도 따라오고, 원래 근거가 어디서 왔는지도 남는다.
 */

/*
 * 데드밴드 폭.
 *
 * ★ 왜 IIR 이 아니라 데드밴드인가 — 지연 때문이다.
 *
 *   처음에는 1차 IIR(1/4)을 썼다. 노이즈는 잘 줄었지만 정착이 63% 에 4스캔(152us),
 *   90% 에 9스캔(342us) 걸린다. 키를 누르는 내내 뒤처진 값을 내므로 그대로 입력
 *   지연이 된다. 8kHz 저지연이 이 보드의 존재 이유인데 필터 하나로 절반을 까먹는다.
 *
 *   데드밴드는 밴드보다 큰 변화에는 같은 샘플에서 즉시 따라간다 — 지연 0. 대신
 *   ±BAND 로 양자화된다. 밴드 7 이면 스트로크 838 을 120단계로 나누므로 충분하다.
 *
 *   기준값 추적이 노이즈 꼭대기를 붙잡는 걸 막는다는 목적도 그대로 달성된다.
 *   밴드 안쪽 움직임은 출력에 아예 반영되지 않아 추적기가 노이즈를 보지 못한다.
 *
 *   12비트 영역에서 ±7 이다. 실측 노이즈 ±6 바로 위다.
 */
/*
 * ★ 여기만 3배가 아니다.
 *
 *   눈금은 3배가 되지만 잡음은 sqrt(3)=1.73 배만 커진다. 밴드는 잡음을 덮는 물건이니
 *   잡음을 따라가야 한다 — 3배로 키우면 애써 줄인 잡음만큼 분해능을 도로 버린다.
 *
 *     7 x 3 / 1.73 = 12.1  ->  12
 *
 *   결과적으로 밴드가 스트로크에서 차지하는 비율이 7/838 에서 12/2514 로 줄어,
 *   같은 지연 0 을 유지하면서 분해능이 1.7배 좋아진다.
 */
#define KEYS_DEADBAND           12


/*
 * 스위치 종류표 — 펌웨어 상수.
 *
 * 보정을 안 해도 mm 환산이 되도록 공칭 스트로크를 갖고 있는다. 사용자가 보정하면
 * 키별 실측값이 이걸 대신한다. 키마다 종류 인덱스를 저장해두고 설정은 mm 로 다룬다.
 *
 * travel_um 은 0.01mm 단위다 (400 = 4.00mm).
 *
 * stroke_cnt 는 그 행정에 해당하는 ADC 카운트(12비트)다. 보정하지 않은 키를 mm 로
 * 환산하려면 이게 있어야 한다 — 카운트만으로는 몇 mm 인지 알 수 없기 때문이다.
 * 값의 근거는 8편의 실측이다: 61키 보정에서 최소 757 / 최대 893 / 평균 836.
 * 그래서 4.0mm 자리에 836 을 넣고 나머지는 행정에 비례시켰다.
 */
typedef struct
{
  const char *name;
  uint16_t    travel_um;
  uint16_t    stroke_cnt;   /* 미보정 키의 mm 환산 기준 (12비트 카운트) */
} keys_switch_t;

static const keys_switch_t keys_switch[] =
{
  { "generic 4.0mm", 400, 836 * KEYS_ACC_CNT },   /* 0 — 기본값. 실제 스위치가 정해지면 채운다 */
  { "generic 3.5mm", 350, 731 * KEYS_ACC_CNT },   /* 1 */
  { "generic 3.0mm", 300, 627 * KEYS_ACC_CNT },   /* 2 */
};

#define KEYS_SWITCH_CNT   (sizeof(keys_switch) / sizeof(keys_switch[0]))


/*
 * 저장 레코드.
 *
 * ★ 부팅 경로에서는 읽기만 한다. 읽기는 XIP 라 인터럽트를 막지 않으므로 안전하다.
 *   쓰기는 사용자가 명시할 때만 (keys save). 부팅 중 플래시를 쓰다 실패하면
 *   어디로 갈지 설계가 어려워지고, 실제로 그것 때문에 한 번 브릭을 만들었다.
 */
/*
 * cfg.rt_flags
 *
 *   KEYS_RT_CONT — 연속 RT.
 *
 *     보통 RT 는 입력지점 아래에서만 살아 있다. 키가 그 위로 돌아오면 풀리고,
 *     다시 입력지점을 넘어야 붙는다. 연속 RT 는 전 구간에서 계속 살려 둔다 —
 *     얕게 톡톡 치는 구간에서도 방향 반전만으로 입력·해제가 난다.
 */
#define KEYS_RT_ON         (1U << 0)
#define KEYS_RT_BOTTOM     (1U << 1)
#define KEYS_RT_CONT       (1U << 2)

#define KEYS_CFG_MAGIC     0x4746434BUL   /* 'KCFG' */
/*
 * 2: 누적으로 눈금 3배   3: 래피드 트리거 항목   4: 전 항목을 키별로
 *
 * ★ 4 를 마지막으로 삼는다. 올릴 때마다 사용자 보정값이 버려지므로 앞으로 필요할
 *   자리를 keys_key_cfg_t 에 미리 잡아 뒀다.
 */
#define KEYS_CFG_VERSION   4

/*
 * 키 하나의 설정.
 *
 * ★ 키별이 기본이다. 전역은 "모두 선택"일 뿐이다.
 *
   *   키를 골라 설정하는 화면에서는 "모두 선택"이 곧 전역이다. 전역 설정이라는
 *   개념을 따로 두면 키별 값과 어느 쪽이 이기는지가 계속 문제가 된다.
 *
 *   그래서 판정은 언제나 이 구조체를 본다. 아래 cfg 의 전역 필드는 (a) 새 설정을
 *   만들 때의 기본값이고 (b) VIA 가 전역으로 읽고 쓸 때 64키에 뿌리는 값이다.
 *
 * ★ 자리를 넉넉히 잡아 둔다.
 *
 *   버전을 올릴 때마다 사용자 보정값이 버려진다. 이미 두 번 그랬다. 지금 필요한
 *   값을 전부 넣어 두어 다음 기능 때 또 올리지 않게 한다.
 */
typedef struct
{
  uint16_t cal_max;        /* 무압 실측 (0 = 미보정) */
  uint16_t cal_min;        /* 바닥 실측 (0 = 미보정) */

  uint16_t press_um;       /* 입력지점        0.01mm */
  uint16_t release_um;     /* 해제지점        0.01mm */
  uint16_t rt_press_um;    /* RT 재입력       0.01mm */
  uint16_t rt_release_um;  /* RT 입력 해제    0.01mm */
  uint16_t bottom_um;      /* 바닥 보호       0.01mm */
  uint16_t dead_um;        /* 데드존          0.01mm */

  uint8_t  sw_type;        /* 스위치 종류 인덱스 */
  uint8_t  flags;          /* bit0 = 보정됨 */
  uint8_t  rt_flags;       /* KEYS_RT_* */
  uint8_t  rsv[5];
} keys_key_cfg_t;          /* 24 바이트 x 64 키 = 1536 B */

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t length;       /* 이 구조체 전체 크기 — 버전이 올라가도 읽을 수 있게 */
  uint32_t seq;          /* 핑퐁 선택 기준. 큰 쪽이 최신 */

  uint16_t press_um;     /* 입력지점    0.01mm — 절대 위치 */
  uint16_t release_um;   /* 해제지점    0.01mm — 절대 위치 */

  /*
   * 래피드 트리거 — 절대 위치가 아니라 "방향이 바뀐 뒤 얼마나 움직였나"로 판정한다.
   *
   * ★ 누름과 해제를 따로 둔다.
   *
   *   하나로 묶는 구현이 많지만 게임에서는 비대칭이 유리한 경우가 실제로 있다
   *   (빠르게 떼고 천천히 누르기).
   *   UI 에서는 "고급" 뒤에 숨겨 하나처럼 보이게 할 수 있으니, 필드는 지금 넣어
   *   나중에 버전을 또 올리며 재보정을 요구하지 않게 한다.
   */
  uint16_t rt_press_um;   /* 되눌림 반응 행정  0.01mm */
  uint16_t rt_release_um; /* 되뗌   반응 행정  0.01mm */

  /*
   * 바닥 보호 — 바닥에서 이만큼 안쪽에서는 RT 해제를 끈다.
   *
   *   RT 해제는 "가장 깊었던 지점에서 얼마나 올라왔나"로 판정한다. 바닥까지 눌러
   *   붙잡고 있으면 그 기준점이 바닥에 고정되는데, 손가락은 누르는 동안에도
   *   0.1~0.3mm 씩 미세하게 풀린다. 그 이완만으로 해제가 나가 키가 툭툭 끊긴다.
   *   자석이 가장 가까운 구간에서 홀 출력이 평평해지는 것도 겹친다.
   *
   *   일반 모드에서는 해제 지점이 절대값이라 바닥에서 멀어 안 생긴다. RT 특유의
   *   문제다. 잡음 때문이 아니다 — 0.1mm 는 9 sigma 라 잡음은 못 넘는다.
   */
  uint16_t bottom_um;    /* 바닥 보호 구간  0.01mm */

  /*
   * 데드존 — 쉬는 위치 근처에서 이만큼은 아예 안 본다.
   *
   *   ★ 바닥 보호와 다른 물건이다. 이쪽은 **위**다.
   *
   *     데드존     진동·공차로 값이 흔들려도 우발적 입력이 안 나가게 막는다
   *     바닥 보호   끝까지 눌러 붙잡을 때 손가락 이완으로 RT 해제가 나가는 걸 막는다
   *
   *   기본값 0 이다 — 우리 기준값이 키마다 러닝 최대로 따라가므로 평소에는 필요
   *   없고, 진동이 있는 환경에서 사용자가 올린다.
   */
  uint16_t dead_um;      /* 데드존  0.01mm */

  uint8_t  sw_type_def;  /* 기본 스위치 종류 */
  uint8_t  rt_flags;     /* bit0 = RT 켬, bit1 = 바닥 보호 켬 */

  keys_key_cfg_t key[KEYS_MAX];

  uint32_t crc;          /* 이 필드 앞까지의 CRC32 */
} keys_cfg_t;

static keys_cfg_t cfg;


/*
 * ADC 시퀀스 DMA 버퍼.
 *
 * ADC 가 직접 쓰므로 캐시에 걸리면 안 된다. .noncacheable.non_init 은 NOLOAD 라
 * 0 초기화되지 않는다 — 첫 스캔 전에는 쓰레기값이 들어 있다고 봐야 한다.
 * 결과는 32비트 워드에 패킹되고 하위 16비트가 값이다 (dma_seq16bit 미사용).
 *
 * ★ volatile 이어야 한다.
 *   ATTR_PLACE_AT_NONCACHEABLE_BSS 는 **하드웨어 캐시** 이야기라 컴파일러와는 무관하다.
 *   컴파일러 눈에는 아무도 쓰지 않는 배열이라, -O2 에서는 읽기를 합치거나 루프 밖으로
 *   끌어낼 수 있다. 특히 "채워질 때까지 기다리는" 회전은 volatile 없이는 성립하지 않는다.
 */
ATTR_PLACE_AT_NONCACHEABLE_BSS __attribute__((aligned(ADC_SOC_DMA_ADDR_ALIGNMENT)))
static volatile uint32_t adc0_buf[KEYS_SEQ_LEN];

ATTR_PLACE_AT_NONCACHEABLE_BSS __attribute__((aligned(ADC_SOC_DMA_ADDR_ALIGNMENT)))
static volatile uint32_t adc1_buf[KEYS_SEQ_LEN];


/*
 * 누적 링 — 셀마다 최근 KEYS_ACC_CNT 개의 12비트 표본을 들고 있다.
 *
 * 합을 매번 다시 더하지 않는다. 가장 오래된 값을 빼고 새 값을 더한다 (스캔당 셀마다
 * 뺄셈 하나, 덧셈 하나). 링의 어느 칸을 덮어쓸지는 스캔 단위로 정해지므로 8x8 이
 * 같은 칸을 쓴다 — 인덱스를 셀마다 들 필요가 없다.
 */
static uint16_t acc_hist[KEYS_STEP_MAX][KEYS_CH_MAX][KEYS_ACC_CNT];
static uint16_t acc_sum[KEYS_STEP_MAX][KEYS_CH_MAX];
static uint32_t acc_idx = 0;

/*
 * 키별 임계값 — 카운트 단위로 미리 풀어 둔다.
 *
 * 설정은 0.01mm 인데 판정은 카운트로 한다. 환산에는 키별 스트로크가 필요해서
 * 나눗셈이 들어가는데, 그걸 35kHz x 64셀 루프 안에서 할 수는 없다. 설정이나
 * 보정이 바뀔 때만 다시 만든다.
 */
typedef struct
{
  uint16_t press;        /* 입력지점 (깊이 카운트) */
  uint16_t release;      /* 해제지점 */
  uint16_t rt_press;     /* RT 재입력 반응 행정 */
  uint16_t rt_release;   /* RT 입력 해제 반응 행정 */
  uint16_t bottom_lo;    /* 이 깊이 이상이면 바닥 보호 구간 */
  uint16_t dead;         /* 이 깊이 미만은 아예 안 본다 */
  uint8_t  rt_flags;     /* 키별 KEYS_RT_* */
} keys_thr_t;

static keys_thr_t thr[KEYS_MAX];

static void            keysThrRebuild(void);
static void            keysCfgFanout(void);
static uint16_t        keysStrokeCnt(uint32_t i);
static inline uint8_t  keysSwType(uint32_t i);

/*
 * RT 반응 행정의 하한 (카운트).
 *
 *   실측 잡음 p-p 가 40 이다. 반응 행정이 그보다 작으면 RT 가 잡음을 방향 반전으로
 *   읽어 키가 제멋대로 눌렸다 떼진다. 잡음의 1.5배를 하한으로 둔다 — 0.096mm 쯤이라
 *   0.096mm 쯤이다.
 */
#define KEYS_RT_MIN_CNT    60

/*
 * RT 판정용 상태.
 *
 *   peak    눌린 동안은 가장 깊었던 지점, 떼진 동안은 가장 얕았던 지점
 *   rt_arm  RT 가 걸려 있는가 (연속 RT 가 아니면 입력지점 위로 돌아올 때 풀린다)
 */
static uint16_t peak[KEYS_STEP_MAX][KEYS_CH_MAX];
static uint16_t rt_arm[KEYS_STEP_MAX];

static uint16_t raw[KEYS_STEP_MAX][KEYS_CH_MAX];    /* 누적합(0~12285) — 판정·표시는 전부 이걸 쓴다 */
static uint16_t base[KEYS_STEP_MAX][KEYS_CH_MAX];   /* 무압 기준값 (러닝 최대) */
static uint16_t pressed[KEYS_STEP_MAX];             /* 행별 눌림 비트마스크 */
static bool     is_calibrated = false;
static uint32_t scan_time_us = 0;

/*
 * 스캔 주기가 고른지 본다.
 *
 * 13편의 드리프트 분산과 래피드 트리거는 둘 다 "판정이 일정 주기로 온다"를 깔고
 * 있다. 평균만 보면 고른지 알 수 없어서 최대·초과 횟수를 같이 센다.
 */
#define KEYS_SCAN_OVER_US   60          /* 정상 28us 의 두 배 */
static uint32_t scan_us_max   = 0;
static uint32_t scan_over_cnt = 0;
static uint32_t scan_cnt      = 0;
static bool     is_init      = false;
static uint32_t timeout_cnt  = 0;
static uint32_t cal_time_ms  = 0;
static uint32_t drift_ms     = 0;
static bool     is_cfg_loaded = false;

/*
 * keys 명령이 도는 동안에는 HID 리포트를 막는다.
 *
 * 측정하려고 누른 키가 호스트로 그대로 입력되어 터미널이 엉키거나 CLI 가 끊긴다.
 * 매핑·보정처럼 전 키를 눌러야 하는 작업에서는 치명적이다.
 */
static bool     report_off = false;

/* 보정 중 키별 바닥값 수집용 */
static uint16_t cal_min_tmp[KEYS_MAX];

/*
 * 지정한 두 키코드가 동시에 눌려 있는가.
 *
 * 보정 중에는 리포트를 막아두므로 키 조합을 종료 신호로 쓸 수 있다. 터미널 없이
 * 키보드만 연결한 상태에서도 끝낼 수 있어야 하기 때문이다.
 *
 * 자리를 박아두지 않고 키맵에서 찾는다 — 키맵이 바뀌어도 따라간다.
 */
/*
 * ★ 루프 명령은 키보드만으로도 빠져나올 수 있어야 한다.
 *
 *   report_off 를 켜는 명령(map·learn·layout·show·bar·watch·raw)이 도는 동안에는
 *   키보드가 리포트를 안 보낸다. 그런데 콘솔은 그 키보드로 치는 터미널이다 —
 *   Ctrl-C 를 칠 방법이 없어서 USB 를 뽑는 수밖에 없었다. cal 만 자체 탈출이
 *   있었는데, 나머지도 같아야 한다.
 *
 *   Esc 를 누르면 나간다. 어차피 그동안 Esc 는 호스트로 안 나가므로 충돌이 없다.
 */
static bool keysKcHeld(uint8_t kc)
{
  for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
    {
      if (keysGetPressed(st, c) && keys_keymap[st][c] == kc) return true;
    }
  }
  return false;
}

static bool keysCliKeep(void)
{
  if (cliKeepLoop() == false) return false;

  /* 리포트를 막는 동안에만 — 평소에는 Esc 가 진짜 Esc 여야 한다 */
  if (report_off && keysKcHeld(KEYS_CANCEL_KC)) return false;

  return true;
}

static bool keysComboHeld(uint8_t kc1, uint8_t kc2)
{
  bool a = false;
  bool b = false;

  for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
    {
      uint8_t kc;

      if (keysGetPressed(st, c) == false) continue;

      kc = keys_keymap[st][c];
      if (kc == kc1) a = true;
      if (kc == kc2) b = true;
    }
  }
  return (a && b);
}

static bool keysCalIsDone(uint16_t row, uint16_t col)
{
  uint32_t i = row * KEYS_CH_MAX + col;

  if (i >= KEYS_MAX)                return false;
  if (cal_min_tmp[i] == 0xFFFF)     return false;

  return ((int32_t)base[row][col] - (int32_t)cal_min_tmp[i]) >= KEYS_CAL_STROKE_MIN;
}
static bool     drift_due    = false;

static void keysCalRejectOutlier(void);
static bool keysCfgLoad(void);
static bool keysCfgSave(void);
static ATTR_RAMFUNC void keysTrack(uint32_t step);
static inline void keysFilter(uint32_t step, uint32_t ch, uint32_t packed);

#if CLI_USE(HW_KEYS)
static void cliKeys(cli_args_t *args);
#endif




/*---------------------------------------------------------------------------
 *  완료 대기
 *---------------------------------------------------------------------------*/

/*
 * ★ 인터럽트를 쓰지 않는다.
 *
 *   처음에는 ADC 완료 인터럽트로 플래그를 세우고 스캔 루프가 그걸 스핀으로 기다렸다.
 *   그런데 어차피 기다릴 거면 상태 레지스터를 직접 보면 된다 — 인터럽트가 하는 일이
 *   플래그 하나 세우는 것뿐이었다.
 *
 *   인터럽트 부하가 만만치 않았다. 8스텝 x ADC 2개 x 초당 15,000 스캔이면
 *   초당 24만 번이다. 그 ISR 들이 USB 완료 콜백을 밀어내서 다음 마이크로프레임(125us)
 *   안에 재무장하지 못했고, 리포트가 8000/s 가 아니라 6000/s 로 떨어졌다.
 */
/*
 * ★ SEQ_CMPT 폴링도 걷어냈다.
 *
 *   한동안 "SEQ_CMPT 를 기다린 뒤 DMA 칸이 채워지길 기다린다"로 두 번 기다렸는데,
 *   DMA 기록은 변환이 끝나야 일어난다 — 뒤엣것이 앞엣것을 이미 포함한다.
 *   두 번 기다리는 값이 스캔 1회에 4.5us 였다.
 *
 *   플래그는 이제 아무도 보지 않으므로 지우지도 않는다. `keys adc` 진단만 읽는다.
 */


/*---------------------------------------------------------------------------
 *  초기화
 *---------------------------------------------------------------------------*/
static void keysInitPins(void)
{
  /* 아날로그 입력 — PB00~PB15 */
  for (uint32_t i = 0; i < KEYS_ANALOG_PAD_CNT; i++)
  {
    HPM_IOC->PAD[KEYS_ANALOG_PAD_FIRST + i].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
  }

  /*
   * MUX 주소 — PY00~PY03.
   *
   * ★ PY 패드는 IOC 만으로는 연결되지 않는다. PIOC 도 함께 SOC(3) 으로 넘겨야
   *   SoC 쪽 GPIO 가 패드를 잡는다.
   */
  for (uint32_t i = 0; i < KEYS_MUX_PIN_CNT; i++)
  {
    HPM_IOC->PAD[KEYS_MUX_PAD_FIRST + i].FUNC_CTL  = IOC_PY00_FUNC_CTL_GPIO_Y_00;
    HPM_PIOC->PAD[KEYS_MUX_PAD_FIRST + i].FUNC_CTL = KEYS_PIOC_SOC_CTRL;
  }

  gpio_enable_port_output_with_mask(HPM_GPIO0, KEYS_MUX_GPIO_PORT, KEYS_MUX_PIN_MASK);
  gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[0]);
}

static bool keysInitAdc(ADC16_Type *ptr, const uint8_t *seq_ch, volatile uint32_t *buf)
{
  adc16_config_t         cfg;
  adc16_channel_config_t ch_cfg;
  adc16_seq_config_t     seq_cfg;
  adc16_dma_config_t     dma_cfg;

  adc16_get_default_config(&cfg);
  cfg.res         = adc16_res_12_bits;
  cfg.conv_mode   = adc16_conv_mode_sequence;
  cfg.adc_clk_div = 4;
  cfg.sel_sync_ahb = false;
  cfg.adc_ahb_en  = true;          /* 시퀀스 모드는 AHB 라이터가 필요하다 */

  if (adc16_init(ptr, &cfg) != status_success) return false;

  adc16_get_channel_default_config(&ch_cfg);
  ch_cfg.sample_cycle = 5;
  for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++)
  {
    ch_cfg.ch = seq_ch[i];
    if (adc16_init_channel(ptr, &ch_cfg) != status_success) return false;
  }

  memset(&seq_cfg, 0, sizeof(seq_cfg));
  seq_cfg.seq_len    = KEYS_SEQ_LEN;
  seq_cfg.sw_trig_en = true;       /* 스캔 루프가 직접 트리거한다 */
  seq_cfg.hw_trig_en = false;

  /*
   * ★ 이름에 속으면 안 된다.
   *   CONT_EN    = "트리거 한 번에 큐 끝까지 진행" — 우리가 원하는 것
   *   RESTART_EN = "끝나면 다시 처음부터" (CONT_EN 과 같이 켤 때만 의미)
   *
   *   CONT_EN 을 끄면 트리거 1회에 변환이 딱 1개만 일어나고 멈춘다.
   *   그러면 완료 인터럽트가 영영 오지 않아 스캔이 통째로 타임아웃한다.
   */
  seq_cfg.cont_en    = true;
  seq_cfg.restart_en = false;
  for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++)
  {
    seq_cfg.queue[i].ch = seq_ch[i];

    /*
     * ★ SEQ_INT_EN 은 시퀀스 전체가 아니라 "이 큐 원소가 끝나면 알려라" 다.
     *   전부 false 로 두면 완료 인터럽트가 영영 오지 않는다 (스캔이 통째로 타임아웃).
     *   마지막 원소에만 켜서 시퀀스 끝에 한 번만 받는다.
     */
    seq_cfg.queue[i].seq_int_en = (i == (KEYS_SEQ_LEN - 1));
  }
  if (adc16_set_seq_config(ptr, &seq_cfg) != status_success) return false;

  memset(&dma_cfg, 0, sizeof(dma_cfg));

  /*
   * ADC 는 시스템 버스로 쓰므로 코어 로컬 주소(ILM/DLM)가 아니라 시스템 주소를 줘야 한다.
   * AXI SRAM 이면 변환이 항등이지만, 버퍼를 옮겼을 때 조용히 깨지는 걸 막는다.
   */
  dma_cfg.start_addr          = (uint32_t *)core_local_mem_to_sys_address(0, (uint32_t)buf);
  dma_cfg.buff_len_in_4bytes  = KEYS_SEQ_LEN;
  dma_cfg.stop_pos            = 0;
  dma_cfg.stop_en             = false;
  if (adc16_init_seq_dma(ptr, &dma_cfg) != status_success) return false;

  /* 완료는 폴링으로 본다. 인터럽트는 켜지 않는다. */
  adc16_clear_status_flags(ptr, ADC16_INT_STS_SEQ_CMPT_MASK);

  return true;
}

bool keysInit(void)
{
  bool ret = true;


  clock_add_to_group(clock_adc0, 0);
  clock_add_to_group(clock_adc1, 0);
  clock_set_adc_source(clock_adc0, clk_adc_src_ahb0);
  clock_set_adc_source(clock_adc1, clk_adc_src_ahb0);

  keysInitPins();

  if (keysInitAdc(HPM_ADC0, adc0_seq_ch, adc0_buf) == false) ret = false;
  if (keysInitAdc(HPM_ADC1, adc1_seq_ch, adc1_buf) == false) ret = false;

  is_init = ret;

#if CLI_USE(HW_KEYS)
  cliAdd("keys", cliKeys);
#endif

  /*
   * 저장된 설정을 읽는다. 읽기뿐이라 인터럽트를 막지 않아 부팅 경로에서 안전하다.
   * 없거나 깨졌으면 기본값으로 계속 간다 — 여기서 멈추면 복구가 막힌다.
   */
  is_cfg_loaded = keysCfgLoad();
  keysThrRebuild();          /* 설정을 읽었으니 임계값을 푼다 */

  if (ret)
  {
    keysCalibrate();
  }

  logPrintf("[%s] keysInit()\n", ret ? "OK" : "E_");
  if (ret)
  {
    logPrintf("     %d step x %d ch = %d keys, cal %d\n",
              KEYS_STEP_MAX, KEYS_CH_MAX, KEYS_MAX, is_calibrated);
  }

  return ret;
}




/*---------------------------------------------------------------------------
 *  스캔
 *---------------------------------------------------------------------------*/
static inline void keysSettle(void)
{
  for (uint32_t i = 0; i < KEYS_SETTLE_CYCLES; i++)
  {
    __asm volatile ("nop");
  }
}

/*
 * ★ "변환 끝"과 "버퍼에 앉음"은 다른 사건이다.
 *
 *   SEQ_CMPT 는 마지막 채널의 **변환**이 끝났다는 뜻이다. 그 결과가 버스를 건너
 *   DMA 버퍼에 앉기까지는 시간이 더 걸린다. -O0 일 때는 플래그를 보고 버퍼를 읽기까지
 *   코드가 충분히 느려서 드러나지 않았는데, -O2 로 그 간격이 좁아지자 각 시퀀스의
 *   **마지막 원소**(ch3 · ch7)만 직전 스캔 값을 읽었다.
 *
 *     노이즈 p-p    다른 셀 3~9      ch3 · ch7  12 ~ 231
 *
 *   순서 문제라 핀을 바꿔도 소용없다. 원시값이 한 스텝 밀리므로 **엉뚱한 키가 눌린
 *   것으로 판정된다** — 실제로 그렇게 나타났다.
 *
 *   ★ cycle_bit 은 못 쓴다.
 *     DMA 워드 최상위 비트가 버퍼를 한 바퀴 돌 때마다 뒤집힌다고 되어 있지만,
 *     SW 트리거가 시퀀스를 0번부터 다시 시작시켜서 포인터가 감기지 않는다.
 *     수백만 스캔을 돈 뒤 덤프해도 네 워드 모두 bit31 = 0 이었다.
 *
 *   그래서 **마지막 칸을 비워 두고 채워지는 것을 본다.** DMA 워드는 채널 번호가
 *   실려 있어 절대 0 이 아니다. 칸은 순서대로 채워지므로 마지막이 왔으면 앞은 이미 왔다.
 *   보통은 비교 한 번에 끝난다 — 늦었을 때만 돈다.
 */
static inline void keysDmaArm(void)
{
  adc0_buf[KEYS_SEQ_LEN - 1] = 0;
  adc1_buf[KEYS_SEQ_LEN - 1] = 0;
}

static inline bool keysWaitDma(void)
{
  uint32_t spin = 0;

  while (adc0_buf[KEYS_SEQ_LEN - 1] == 0 || adc1_buf[KEYS_SEQ_LEN - 1] == 0)
  {
    if (++spin > KEYS_WAIT_LIMIT)
    {
      timeout_cnt++;
      return false;
    }
  }
  return true;
}

/*
 * 데드밴드 필터.
 *
 * 밴드보다 크게 움직일 때만 출력이 따라간다. 큰 변화는 같은 샘플에서 즉시 반영되므로
 * 지연이 없다. 밴드 안쪽 잔파도는 출력에 아예 나타나지 않는다.
 */
static inline void keysFilter(uint32_t step, uint32_t ch, uint32_t packed)
{
  uint16_t v12 = (uint16_t)((packed & 0xFFFF) >> KEYS_RAW_SHIFT);
  int32_t  v;
  int32_t  o;

  /* 링을 한 칸 밀어 합을 갱신한다 — 가장 오래된 것을 빼고 새 것을 더한다 */
  v = (int32_t)acc_sum[step][ch] - (int32_t)acc_hist[step][ch][acc_idx] + (int32_t)v12;
  acc_hist[step][ch][acc_idx] = v12;
  acc_sum[step][ch]           = (uint16_t)v;

  /* 데드밴드는 합 눈금에 건다 */
  o = (int32_t)raw[step][ch];

  if      (v > o + KEYS_DEADBAND) o = v - KEYS_DEADBAND;
  else if (v < o - KEYS_DEADBAND) o = v + KEYS_DEADBAND;

  raw[step][ch] = (uint16_t)o;
}

/*
 * 기준값 추적 + 눌림 판정.
 *
 * 안 눌린 상태가 물리적 극단(자석이 가장 멀어 값이 가장 크다)이므로 기준값은
 * 러닝 최대값이다. 이 하나로 세 가지가 같이 해결된다.
 *
 *   - 누른 채 부팅  -> 손을 떼는 순간 제 값을 찾는다
 *   - 온도 드리프트 -> 위로 새면 즉시, 아래로 새면 천천히 따라간다
 *   - 개체 편차     -> 셀마다 제 기준을 갖는다
 */
/*
 * 설정(0.01mm)을 키별 카운트로 풀어 둔다.
 *
 * ★ 이걸 넣기 전까지 판정은 전역 상수(KEYS_PRESS_LEVEL 등)를 썼다.
 *   VIA 입력지점 슬라이더가 EEPROM 만 바꾸고 판정에는 닿지 않았다는 뜻이다.
 *
 * 환산에 키별 스트로크가 필요해 나눗셈이 들어간다. 35kHz x 64셀 루프 안에서는 못
 * 하므로 설정·보정이 바뀔 때만 다시 만든다.
 */
static void keysThrRebuild(void)
{
  for (uint32_t i = 0; i < KEYS_MAX; i++)
  {
    const keys_key_cfg_t *k = &cfg.key[i];
    uint32_t stroke = keysStrokeCnt(i);
    uint32_t travel = keys_switch[keysSwType(i)].travel_um;
    keys_thr_t *t   = &thr[i];

    if (travel == 0) travel = 400;

    /* um -> 카운트. um 400, stroke 2700 이라도 32비트 안이다. */
    #define UM2CNT(um)  ((uint16_t)(((uint32_t)(um) * stroke) / travel))

    t->press      = UM2CNT(k->press_um);
    t->release    = UM2CNT(k->release_um);
    t->rt_press   = UM2CNT(k->rt_press_um);
    t->rt_release = UM2CNT(k->rt_release_um);
    t->dead       = UM2CNT(k->dead_um);
    t->rt_flags   = k->rt_flags;

    /* 바닥 보호는 "바닥에서 이만큼 안쪽" 이므로 깊이 기준으로 뒤집는다 */
    {
      uint32_t b = UM2CNT(k->bottom_um);
      t->bottom_lo = (uint16_t)((stroke > b) ? (stroke - b) : 0);
    }
    #undef UM2CNT

    /*
     * 최소 폭을 지킨다.
     *
     *   반응 행정이 잡음보다 작으면 RT 가 잡음을 방향 반전으로 읽는다. 실측 잡음
     *   p-p 가 40 이므로 그 위로 올린다. 사용자가 0 으로 내려도 여기서 막힌다.
     */
    if (t->rt_press   < KEYS_RT_MIN_CNT) t->rt_press   = KEYS_RT_MIN_CNT;
    if (t->rt_release < KEYS_RT_MIN_CNT) t->rt_release = KEYS_RT_MIN_CNT;

    /* 해제가 입력보다 깊으면 눌린 채로 굳는다 */
    if (t->release >= t->press && t->press > 0) t->release = (uint16_t)(t->press - 1);
  }
}

ATTR_RAMFUNC static void keysTrack(uint32_t step)
{
  bool do_drift = drift_due;

  for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
  {
    /* 스위치가 없는 자리는 판정하지 않는다. 기준값 추적은 그대로 둬도 무해하다. */
    if ((keys_present[step] & (1U << c)) == 0) continue;

    const keys_thr_t *t = &thr[step * KEYS_CH_MAX + c];
    bool     rt_on   = (t->rt_flags & KEYS_RT_ON)     != 0;
    bool     rt_cont = (t->rt_flags & KEYS_RT_CONT)   != 0;
    bool     bot_on  = (t->rt_flags & KEYS_RT_BOTTOM) != 0;
    uint16_t v = raw[step][c];
    uint16_t bit = (uint16_t)(1U << c);
    int32_t  d;

    /*
     * 해제 방향 갱신 — 크기에 따라 갈라야 한다.
     *
     * ★ 무조건 즉시 갱신하면 기준값이 노이즈 꼭대기를 붙잡는다. 그런데 그 기회는
     *   스캔 속도에 비례하는 반면 드리프트 보정은 시간 기준이라, 스캔이 빨라질수록
     *   균형점이 위로 밀려 편차가 커진다.
     *
     *   그래서 큰 변화(진짜 해제)만 즉시 반영하고, 노이즈 수준의 잔파도는 드리프트와
     *   같은 시간 기준으로 올린다. 위아래가 대칭이 되어 스캔 속도와 무관해진다.
     */
    if (v > base[step][c])
    {
      if ((uint32_t)(v - base[step][c]) > KEYS_LATCH_JUMP) base[step][c] = v;
      else if (do_drift)                                   base[step][c] += KEYS_DRIFT_STEP;
    }

    d = (int32_t)base[step][c] - (int32_t)v;    /* 깊이 — 누를수록 커진다 */

    /*
     * 노이즈 범위 안에 머무는 셀만 기준값을 한 걸음 내린다.
     * 눌려 있는 셀은 d 가 밴드를 넘어서 영향받지 않는다.
     */
    if (do_drift && d > KEYS_DRIFT_STEP && d < KEYS_DRIFT_BAND)
      base[step][c] -= KEYS_DRIFT_STEP;

    /*
     * ★ 여기서 음수를 잘라낸다.
     *
     *   기준값은 러닝 최대라 보통 d >= 0 이지만, 드리프트로 기준값이 한 걸음 내려간
     *   직후에 잡음이 겹치면 잠깐 음수가 된다. 아래 판정은 peak 을 uint16 으로 들고
     *   있어서, 음수를 그대로 넘기면 (uint16_t)(-1) = 65535 가 되어 peak 이 튀고
     *   다음 스캔에 즉시 해제가 난다. 부팅 직후처럼 기준값이 자리를 잡는 구간에서
     *   나기 쉽다.
     *
     * 데드존 — 쉬는 위치 근처는 아예 안 본다.
     *
     *   여기 있는 동안은 완전히 뗀 것으로 보고 RT 도 푼다. 그래야 진동으로 값이
     *   흔들려도 RT 가 "방향이 바뀌었다"고 읽지 않는다.
     */
    if (d < (int32_t)t->dead || d < 0)
    {
      d = 0;
      rt_arm[step] &= (uint16_t)~bit;
    }

    if (pressed[step] & bit)
    {
      /* 눌린 동안에는 가장 깊었던 지점을 좇는다 */
      if ((uint16_t)d > peak[step][c]) peak[step][c] = (uint16_t)d;

      /*
       * RT 입력 해제 — 가장 깊었던 지점에서 이만큼 올라오면 뗀 것으로 본다.
       *
       * ★ 바닥 근처에서는 끈다.
       *   바닥까지 눌러 붙잡으면 기준점이 바닥에 고정되는데 손가락은 누르는 동안에도
       *   미세하게 풀린다. 그 이완만으로 해제가 나가 키가 툭툭 끊긴다.
       */
      bool in_bottom  = bot_on && ((uint16_t)d >= t->bottom_lo);
      bool rt_active  = rt_on && (rt_cont || (rt_arm[step] & bit));

      if (rt_active && !in_bottom &&
          (int32_t)peak[step][c] - d >= (int32_t)t->rt_release)
      {
        pressed[step] &= (uint16_t)~bit;
        peak[step][c]  = (uint16_t)d;
      }
      else if (d < (int32_t)t->release)         /* 절대 해제 — 항상 유효하다 */
      {
        pressed[step] &= (uint16_t)~bit;
        peak[step][c]  = (uint16_t)d;
      }
    }
    else
    {
      /* 떼진 동안에는 가장 얕았던 지점을 좇는다 */
      if ((uint16_t)d < peak[step][c]) peak[step][c] = (uint16_t)d;

      /*
       * RT 재입력 — 가장 얕았던 지점에서 이만큼 내려가면 다시 누른 것으로 본다.
       *
       * ★ RT 가 걸려 있는 동안 절대 입력지점은 보지 않는다.
       *
       *   처음에는 둘을 or 로 묶었다가 크게 당했다. RT 가 깊은 곳(예: 2187 카운트)
       *   에서 해제를 내면 키는 떼진 상태인데 깊이는 아직 입력지점(627)보다 훨씬
       *   깊다. 그래서 바로 다음 스캔에 절대 입력이 걸려 즉시 재입력되고,
       *   RT 해제 -> 절대 입력 -> RT 해제 가 손가락이 올라오는 내내 반복됐다.
       *
       *     ffffffffffffffffffffffff
       *
       *   바닥까지 누르면 peak 이 최대라 그 반복 구간이 가장 길어진다.
       *
       *   RT 가 걸린 동안에는 RT 만 판정하고, 절대 입력지점은 RT 를 처음 거는
       *   용도로만 쓴다.
       */
      bool rt_active = rt_on && (rt_cont || (rt_arm[step] & bit));

      if (rt_active)
      {
        if (d - (int32_t)peak[step][c] >= (int32_t)t->rt_press)
        {
          pressed[step] |= bit;
          peak[step][c]  = (uint16_t)d;
        }
      }
      else if (d > (int32_t)t->press)           /* 절대 입력지점 — RT 를 여기서 건다 */
      {
        pressed[step] |= bit;
        peak[step][c]  = (uint16_t)d;
        rt_arm[step]  |= bit;
      }

      /*
       * 입력지점 아래로 완전히 돌아왔으면 RT 를 푼다.
       *
       * 연속 RT 면 풀지 않는다 — 전 구간에서 살아 있는 게 그 기능이다.
       */
      if (!rt_cont && d < (int32_t)t->release) rt_arm[step] &= (uint16_t)~bit;
    }
  }
}

/*
 * ★ 스캔 경로는 ILM 에 둔다 (ATTR_RAMFUNC -> .fast 섹션 -> 리셋 때 복사).
 *
 *   같은 코드가 문맥에 따라 두 배 느렸다. 측정 루프에서 keysUpdate() 만 반복하면
 *   29us 인데, 메인 루프에서는 63us 였다. 사이에 qmkUpdate() 와 usbUpdate() 가
 *   끼면서 XIP 플래시 명령어 캐시에서 스캔 코드가 밀려났기 때문이다 — 매 스캔이
 *   플래시에서 다시 읽혔다.
 *
 *   ILM 은 코어에 붙어 있어 캐시를 타지 않는다. 128KB 중 1KB 도 안 쓰고 있었다.
 */
ATTR_RAMFUNC bool keysUpdate(void)
{
  uint32_t t_begin;
  bool     ret = true;


  if (is_init == false) return false;

  t_begin = micros();

  /* 시간 기준 드리프트 — 스캔 속도와 무관하게 같은 속도로 보정된다 */
  drift_due = false;
  if (millis() - drift_ms >= KEYS_DRIFT_MS)
  {
    drift_ms  = millis();
    drift_due = true;
  }

  /*
   * ★ 처리를 변환 대기 안으로 숨긴다.
   *
   *   변환은 스텝당 2.96us 인데 그동안 CPU 는 회전만 했고, 다 기다린 뒤에야 0.93us 를
   *   처리했다. 스텝 N 을 트리거해 놓고 그 대기 시간에 스텝 N-1 을 처리하면 처리
   *   시간이 통째로 숨는다.
   *
   *     전    [설정][변환 대기 2.96us      ][처리 0.93us]  x 8
   *     후    [설정][변환 2.96us | N-1 처리 ]              x 8
   *
   *   DMA 버퍼는 다음 트리거가 덮어쓰므로 결과를 한 번 떠 놓아야 한다 (8워드).
   *   그 복사 비용이 숨기는 시간보다 훨씬 싸다.
   *
   *   ADC 설정도 잡음도 건드리지 않는다 — 순서만 바꾼다.
   */
  uint32_t snap[KEYS_CH_MAX];
  uint32_t hold     = 0;        /* 처리 대기 중인 스텝 */
  bool     has_hold = false;

  for (uint32_t step = 0; step < KEYS_STEP_MAX; step++)
  {
    /*
     * 주소는 직전 반복 끝에서 이미 걸어 뒀다. 같은 값을 다시 쓰는 셈이지만 80ns 고,
     * 타임아웃으로 루프를 중간에 빠져나갔을 때 다음 스캔이 제자리를 찾는다.
     */
    gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[step]);
    keysSettle();

    /* 마지막 칸을 비워 둔다 — 여기가 채워지면 이번 스텝의 값이 다 온 것이다 */
    keysDmaArm();

    adc16_trigger_seq_by_sw(HPM_ADC0);
    adc16_trigger_seq_by_sw(HPM_ADC1);

    /* ── 여기부터 변환이 끝날 때까지가 공짜 시간이다 ── */
    if (has_hold)
    {
      for (uint32_t i = 0; i < KEYS_CH_MAX; i++) keysFilter(hold, i, snap[i]);
      if (is_calibrated) keysTrack(hold);
    }

    /* DMA 기록이 곧 변환 완료다 — 한 번만 기다린다 */
    if (keysWaitDma() == false)
    {
      ret      = false;
      has_hold = false;      /* 이번 값은 못 믿는다 */
      break;
    }

    /* ★ 다음 주소를 결과 뜨기 전에 — 세틀링이 복사와 겹친다 */
    gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[step + 1]);

    for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++)
    {
      snap[i]               = adc0_buf[i];
      snap[KEYS_SEQ_LEN + i] = adc1_buf[i];
    }
    hold     = step;
    has_hold = true;
  }

  /* 마지막 스텝은 숨길 대기 시간이 없다 */
  if (has_hold)
  {
    for (uint32_t i = 0; i < KEYS_CH_MAX; i++) keysFilter(hold, i, snap[i]);
    if (is_calibrated) keysTrack(hold);
  }

  /* 링 칸은 스캔 단위로 돈다 — 한 스캔 안에서는 64셀이 같은 칸을 덮는다 */
  if (++acc_idx >= KEYS_ACC_CNT) acc_idx = 0;

  scan_time_us = micros() - t_begin;
  scan_cnt++;
  if (scan_time_us > scan_us_max)      scan_us_max = scan_time_us;
  if (scan_time_us > KEYS_SCAN_OVER_US) scan_over_cnt++;

  return ret;
}

/* 데드밴드 이전의 누적합. 필터가 얼마나 덮고 있는지 가르려면 이게 필요하다. */
uint16_t keysGetAcc(uint8_t step, uint8_t ch)
{
  if (step >= KEYS_STEP_MAX || ch >= KEYS_CH_MAX) return 0;
  return acc_sum[step][ch];
}

uint16_t keysGetRaw(uint8_t step, uint8_t ch)
{
  if (step >= KEYS_STEP_MAX || ch >= KEYS_CH_MAX) return 0;
  return raw[step][ch];
}

uint32_t keysGetScanTime(void)
{
  return scan_time_us;
}

/* 리포트를 내보내도 되는가. keys 명령 중에는 false 다. */
bool keysIsReportEnabled(void)
{
  return (report_off == false);
}

/*
 * 무압 기준값을 잡는다.
 *
 * 채널마다 기준값이 다르다 (자석·센서·기구 공차). 절대값이 아니라 "제 기준값에서
 * 얼마나 벗어났는가"로 판정해야 하므로 부팅 때 한 번 재둔다.
 * 여러 번 평균내서 노이즈를 줄인다.
 */
bool keysCalibrate(void)
{
  uint32_t acc[KEYS_STEP_MAX][KEYS_CH_MAX];
  uint32_t t_begin;


  if (is_init == false) return false;

  t_begin = millis();

  memset(acc, 0, sizeof(acc));

  /* 안정화 — 결과는 버린다 */
  for (uint32_t n = 0; n < KEYS_CAL_DISCARD; n++)
  {
    if (keysUpdate() == false) return false;
  }

  for (uint32_t n = 0; n < KEYS_CAL_SAMPLES; n++)
  {
    if (keysUpdate() == false) return false;

    for (uint32_t s = 0; s < KEYS_STEP_MAX; s++)
    {
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
      {
        acc[s][c] += raw[s][c];
      }
    }
  }

  for (uint32_t s = 0; s < KEYS_STEP_MAX; s++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
    {
      base[s][c] = (uint16_t)(acc[s][c] / KEYS_CAL_SAMPLES);
    }
  }

  keysCalRejectOutlier();

  cal_time_ms = millis() - t_begin;

  memset(pressed,   0, sizeof(pressed));
  is_calibrated = true;
  keysThrRebuild();          /* 키별 스트로크가 바뀌었다 */

  return true;
}

/*
 * 부팅 때 눌려 있던 키를 걸러낸다.
 *
 * 누르면 값이 내려가므로, 기준값이 전체 중앙값보다 크게 낮은 셀은 "눌린 채 측정됐다".
 * 그 셀만 중앙값으로 밀어두면 지금은 눌림으로 판정되고(맞다), 손을 떼는 순간
 * 러닝 최대값 추적이 제 값을 찾아준다.
 *
 * 스트로크(약 13400)가 셀 간 정상 편차(약 5800)의 2배가 넘어서 이 판별이 성립한다.
 */
static void keysCalRejectOutlier(void)
{
  uint16_t sorted[KEYS_MAX];
  uint32_t n = 0;
  uint16_t median;


  /* ★ 없는 셀은 중앙값을 왜곡한다. 자석이 없어 늘 높은 값이라 기준을 위로 끌어올린다. */
  for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
    {
      if (keys_present[st] & (1U << c)) sorted[n++] = base[st][c];
    }
  }
  if (n == 0) return;

  /* 64개뿐이라 삽입정렬로 충분하다 */
  for (uint32_t i = 1; i < n; i++)
  {
    uint16_t v = sorted[i];
    uint32_t j = i;
    while (j > 0 && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; j--; }
    sorted[j] = v;
  }
  median = sorted[n / 2];

  for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
    {
      if ((keys_present[st] & (1U << c)) == 0) continue;
      if ((int32_t)median - (int32_t)base[st][c] > KEYS_CAL_OUTLIER)
      {
        logPrintf("[  ] s%d/ch%d 눌린 채 캘리 (%d -> 중앙값 %d)\n",
                  (int)st, (int)c, (int)base[st][c], (int)median);
        base[st][c] = median;
      }
    }
  }
}

/* 기준값 대비 편차. 부호가 어느 쪽으로 움직이는지는 실측으로 정한다. */
int32_t keysGetDelta(uint8_t step, uint8_t ch)
{
  if (step >= KEYS_STEP_MAX || ch >= KEYS_CH_MAX) return 0;
  if (is_calibrated == false)                     return 0;

  return (int32_t)raw[step][ch] - (int32_t)base[step][ch];
}

uint16_t keysGetBase(uint8_t step, uint8_t ch)
{
  if (step >= KEYS_STEP_MAX || ch >= KEYS_CH_MAX) return 0;
  return base[step][ch];
}

bool keysGetPressed(uint16_t row, uint16_t col)
{
  if (row >= KEYS_STEP_MAX || col >= KEYS_CH_MAX) return false;
  return (pressed[row] & (1U << col)) != 0;
}

/*---------------------------------------------------------------------------
 *  설정 저장 — 핑퐁 2섹터
 *---------------------------------------------------------------------------*/

static uint32_t keysCfgCrc(const keys_cfg_t *p)
{
  return crc32((const uint8_t *)p, (uint32_t)(sizeof(keys_cfg_t) - sizeof(uint32_t)));
}

static bool keysCfgValid(const keys_cfg_t *p)
{
  if (p->magic   != KEYS_CFG_MAGIC)     return false;
  if (p->version != KEYS_CFG_VERSION)   return false;
  if (p->length  != sizeof(keys_cfg_t)) return false;

  return (p->crc == keysCfgCrc(p));
}

static void keysCfgDefault(void)
{
  memset(&cfg, 0, sizeof(cfg));

  cfg.magic       = KEYS_CFG_MAGIC;
  cfg.version     = KEYS_CFG_VERSION;
  cfg.length      = sizeof(keys_cfg_t);
  cfg.seq         = 0;

  /* 입문용 기본값 */
  cfg.press_um      = 100;   /* 1.00mm */
  cfg.release_um    = 50;    /* 0.50mm */

  /*
   * 래피드 트리거는 **꺼진 채로** 시작한다.
   *
   *   보통 키보드로 먼저 완성한다는 게 이 펌웨어의 순서다. RT 는 켜는 순간 타이핑
   *   느낌이 크게 달라지므로 사용자가 고르게 둔다. 값만 미리 채워 두어
   *   켜자마자 쓸 만하게 한다.
   */
  cfg.rt_press_um   = 50;    /* 0.50mm */
  cfg.rt_release_um = 50;    /* 0.50mm */
  cfg.bottom_um     = 10;    /* 0.10mm */
  cfg.dead_um       = 0;
  cfg.rt_flags      = KEYS_RT_BOTTOM;   /* 바닥 보호만 켜 둔다 (RT 는 꺼짐) */

  cfg.sw_type_def = 0;

  for (uint32_t i = 0; i < KEYS_MAX; i++)
  {
    cfg.key[i].sw_type = cfg.sw_type_def;
  }
  keysCfgFanout();
}

/*
 * 전역 값을 64키 전부에 뿌린다 — "모두 선택"에 해당한다.
 *
 * 판정은 언제나 키별 값을 보므로, 전역 설정을 바꾼다는 건 곧 전 키를 바꾼다는 뜻이다.
 * 키별로 다르게 두는 것은 VIA 에서 키를 골라 설정할 때 생긴다.
 */
static void keysCfgFanout(void)
{
  for (uint32_t i = 0; i < KEYS_MAX; i++)
  {
    keys_key_cfg_t *k = &cfg.key[i];

    k->press_um      = cfg.press_um;
    k->release_um    = cfg.release_um;
    k->rt_press_um   = cfg.rt_press_um;
    k->rt_release_um = cfg.rt_release_um;
    k->bottom_um     = cfg.bottom_um;
    k->dead_um       = cfg.dead_um;
    k->rt_flags      = cfg.rt_flags;
  }
}

/*
 * 두 슬롯을 읽어 유효하면서 seq 가 큰 쪽을 쓴다.
 * 둘 다 못 쓰면 기본값으로 간다 — 여기서 멈추면 안 된다.
 */
static bool keysCfgLoad(void)
{
  keys_cfg_t tmp;
  bool       found = false;

  keysCfgDefault();

  for (uint32_t i = 0; i < 2; i++)
  {
    uint32_t addr = (i == 0) ? HW_FLASH_CAL_A : HW_FLASH_CAL_B;

    if (flashRead(addr, (uint8_t *)&tmp, sizeof(tmp)) == false) continue;
    if (keysCfgValid(&tmp) == false)                            continue;

    if (found == false || tmp.seq > cfg.seq)
    {
      memcpy(&cfg, &tmp, sizeof(cfg));
      found = true;
    }
  }

  return found;
}

/*
 * 오래된 쪽 슬롯에 쓴다. 쓰다 죽어도 다른 쪽이 남아 있어 이전 설정으로 돌아간다.
 * 소거 1ms + 기록이라 USB 가 느끼지 못한다.
 */
static bool keysCfgSave(void)
{
  keys_cfg_t tmp;
  uint32_t   addr = HW_FLASH_CAL_A;
  uint32_t   seq_a = 0;

  if (flashRead(HW_FLASH_CAL_A, (uint8_t *)&tmp, sizeof(tmp)) && keysCfgValid(&tmp))
  {
    seq_a = tmp.seq;
    addr  = HW_FLASH_CAL_B;      /* A 가 유효하면 B 에 쓴다 */
  }
  if (flashRead(HW_FLASH_CAL_B, (uint8_t *)&tmp, sizeof(tmp)) && keysCfgValid(&tmp))
  {
    if (tmp.seq >= seq_a) addr = HW_FLASH_CAL_A;   /* B 가 더 최신이면 A 에 */
  }

  cfg.seq++;
  cfg.crc = keysCfgCrc(&cfg);

  if (flashErase(addr, HW_FLASH_SECTOR_SIZE) == false)                       return false;
  if (flashWrite(addr, (const uint8_t *)&cfg, sizeof(cfg)) == false)       return false;

  return true;
}

/* 그 키에 배정된 스위치 종류 인덱스 (범위를 벗어나면 0) */
static inline uint8_t keysSwType(uint32_t i)
{
  uint8_t t = cfg.key[i].sw_type;
  return (t < KEYS_SWITCH_CNT) ? t : 0;
}

/*
 * 그 키의 물리 행정 (0.01mm).
 *
 * 보정 여부와 무관하다 — 보정은 "몇 카운트가 그 행정인가"를 정할 뿐, 스위치가
 * 몇 mm 짜리인지를 바꾸지 않는다.
 */
uint16_t keysGetTravelUm(uint16_t row, uint16_t col)
{
  uint32_t i = row * KEYS_CH_MAX + col;

  if (i >= KEYS_MAX) return 0;

  return keys_switch[keysSwType(i)].travel_um;
}

/*
 * 그 키의 전 행정에 해당하는 카운트.
 *
 * 보정했으면 실측(cal_max - cal_min), 아니면 종류표의 공칭값이다. 보정값이 있어도
 * 너무 작으면 (스위치가 덜 눌린 채 저장된 경우) 공칭값으로 돌아간다.
 */
static uint16_t keysStrokeCnt(uint32_t i)
{
  if (cfg.key[i].flags & 0x01)
  {
    int32_t s = (int32_t)cfg.key[i].cal_max - (int32_t)cfg.key[i].cal_min;

    if (s >= KEYS_CAL_STROKE_MIN) return (uint16_t)s;
  }

  return keys_switch[keysSwType(i)].stroke_cnt;
}

/*
 * 지금 눌려 있는 깊이 (0.01mm). 0 = 안 눌림.
 *
 * ★ 영점은 저장된 cal_max 가 아니라 살아 있는 기준값(base)을 쓴다.
 *
 *   base 는 러닝 최대값이라 온도·자세로 생기는 드리프트를 계속 따라간다. 보정
 *   당시의 cal_max 를 영점으로 쓰면 몇 시간 뒤에는 안 눌러도 0 이 아니게 된다.
 *   보정에서 가져오는 것은 "몇 카운트가 전 행정인가"(기울기)뿐이다.
 */
uint16_t keysGetDepthUm(uint16_t row, uint16_t col)
{
  uint32_t i = row * KEYS_CH_MAX + col;
  int32_t  d;
  uint32_t travel;
  uint32_t stroke;

  if (i >= KEYS_MAX)          return 0;
  if (is_calibrated == false) return 0;

  d = (int32_t)base[row][col] - (int32_t)raw[row][col];   /* 누를수록 양수 */
  if (d <= 0) return 0;

  travel = keys_switch[keysSwType(i)].travel_um;
  stroke = keysStrokeCnt(i);
  if (stroke == 0) return 0;

  d = (int32_t)(((uint32_t)d * travel) / stroke);

  return (uint16_t)((d > (int32_t)travel) ? travel : d);
}

/*---------------------------------------------------------------------------
 *  설정 접근자 — VIA 커스텀 메뉴가 쓴다
 *
 *  값만 바꾸고 플래시에는 쓰지 않는다. 저장은 `keys save` 나 보정 완료처럼 사용자가
 *  명시할 때만 한다 — VIA 슬라이더를 움직일 때마다 섹터를 지우면 수명이 남지 않는다.
 *---------------------------------------------------------------------------*/
uint16_t keysGetPressUm(void)     { return cfg.press_um; }
uint16_t keysGetReleaseUm(void)   { return cfg.release_um; }
uint8_t  keysGetSwitchType(void)  { return cfg.sw_type_def; }

void keysSetPressUm(uint16_t um)
{
  uint16_t travel = keys_switch[keysSwType(0)].travel_um;

  if (um == 0)      um = 1;
  if (um > travel)  um = travel;
  cfg.press_um = um;

  /* 해제지점이 입력지점보다 깊으면 키가 눌린 채로 남는다 */
  if (cfg.release_um >= cfg.press_um) cfg.release_um = (uint16_t)(cfg.press_um - 1);

  keysCfgFanout();
  keysThrRebuild();
}

void keysSetReleaseUm(uint16_t um)
{
  if (um == 0) um = 1;
  if (um >= cfg.press_um) um = (uint16_t)(cfg.press_um - 1);
  cfg.release_um = um;

  keysCfgFanout();
  keysThrRebuild();
}

void keysSetSwitchType(uint8_t type)
{
  if (type >= KEYS_SWITCH_CNT) return;

  cfg.sw_type_def = type;
  for (uint32_t i = 0; i < KEYS_MAX; i++) cfg.key[i].sw_type = type;

  keysThrRebuild();
}


/*---------------------------------------------------------------------------
 *  래피드 트리거 설정
 *
 *  전부 0.01mm 단위다. 값을 바꾸면 임계값 표를 즉시 다시 만들어 다음 스캔부터
 *  반영된다 — 저장(keys save)과는 별개다.
 *---------------------------------------------------------------------------*/
uint16_t keysGetRtPressUm(void)   { return cfg.rt_press_um; }
uint16_t keysGetRtReleaseUm(void) { return cfg.rt_release_um; }
uint16_t keysGetBottomUm(void)    { return cfg.bottom_um; }
uint16_t keysGetDeadUm(void)      { return cfg.dead_um; }
uint8_t  keysGetRtFlags(void)     { return cfg.rt_flags; }

static uint16_t keysClampUm(uint16_t um)
{
  uint16_t travel = keys_switch[keysSwType(0)].travel_um;

  return (um > travel) ? travel : um;
}

void keysSetRtPressUm(uint16_t um)   { cfg.rt_press_um   = keysClampUm(um); keysCfgFanout(); keysThrRebuild(); }
void keysSetRtReleaseUm(uint16_t um) { cfg.rt_release_um = keysClampUm(um); keysCfgFanout(); keysThrRebuild(); }
void keysSetBottomUm(uint16_t um)    { cfg.bottom_um     = keysClampUm(um); keysCfgFanout(); keysThrRebuild(); }
void keysSetDeadUm(uint16_t um)      { cfg.dead_um       = keysClampUm(um); keysCfgFanout(); keysThrRebuild(); }

void keysSetRtFlags(uint8_t flags)
{
  cfg.rt_flags = flags & (KEYS_RT_ON | KEYS_RT_BOTTOM | KEYS_RT_CONT);
  keysCfgFanout();
  keysThrRebuild();

  /*
   * RT 를 끄거나 켤 때 상태를 초기화한다. 안 그러면 직전 peak 이 남아
   * 켜자마자 엉뚱한 판정이 한 번 난다.
   */
  for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
  {
    rt_arm[st] = 0;
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++) peak[st][c] = 0;
  }
}

/* 물리 배치 한 항목 {x, y, w, h, row, col} — 1/4 키유닛. 웹 도구가 이걸로 그린다. */
uint32_t keysGetLayoutCount(void)
{
  return KEYS_LAYOUT_KEY_CNT;
}

const uint8_t *keysGetLayoutEntry(uint32_t idx)
{
  if (idx >= KEYS_LAYOUT_KEY_CNT) return NULL;
  return keys_geo[idx];
}


/* 키맵은 keyboards/<모델>/layout.h 에 있다. 표를 밖으로 내보내지 않고 조회만 준다. */
uint8_t keysGetKeycode(uint16_t row, uint16_t col)
{
  if (row >= KEYS_STEP_MAX || col >= KEYS_CH_MAX) return 0;
  return keys_keymap[row][col];
}

bool keysIsPresent(uint16_t row, uint16_t col)
{
  if (row >= KEYS_STEP_MAX || col >= KEYS_CH_MAX) return false;
  return (keys_present[row] & (1U << col)) != 0;
}

/*
 * 행 비트마스크를 그대로 준다. QMK 의 matrix_row_t 와 비트 순서가 같아서
 * 상위 계층은 이 보드가 HE 인지 일반 매트릭스인지 몰라도 된다.
 */
uint16_t keysGetRow(uint16_t row)
{
  if (row >= KEYS_STEP_MAX) return 0;
  return pressed[row];
}




/*---------------------------------------------------------------------------
 *  CLI
 *---------------------------------------------------------------------------*/
#if CLI_USE(HW_KEYS)

/* 레이아웃이 몇 행인가 */
static uint32_t keysLayoutRows(void)
{
  uint32_t rows = 0;

  for (uint32_t i = 0; i < KEYS_LAYOUT_KEY_CNT; i++)
  {
    uint32_t r = keys_geo[i][1] / KEYS_GEO_UNIT + 1;
    if (r > rows) rows = r;
  }
  return rows;
}

/*
 * 실제 배치로 그린다. mark() 가 true 인 키만 채워 표시한다.
 *
 * 8x8 격자로는 매핑이 맞는지, 어느 키가 남았는지 알 수 없다. ESC 를 눌렀을 때
 * ESC 자리가 채워져야 비로소 맞는 것이다.
 */
static void keysDrawLayout(bool (*mark)(uint16_t row, uint16_t col))
{
  uint32_t rows = keysLayoutRows();

  for (uint32_t r = 0; r < rows; r++)
  {
    char line[KEYS_VIEW_W + 1];

    memset(line, ' ', KEYS_VIEW_W);
    line[KEYS_VIEW_W] = 0;

    for (uint32_t i = 0; i < KEYS_LAYOUT_KEY_CNT; i++)
    {
      uint32_t x0, x1, w;
      bool     on;

      if (keys_geo[i][1] / KEYS_GEO_UNIT != r) continue;

      /*
       * ★ 폭을 따로 환산하면 안 된다. x0 과 w 를 각각 내림하면 오차가 두 번 생겨
       *   행마다 오른쪽 끝이 한두 칸씩 어긋난다. 오른쪽 모서리를 직접 구해서 뺀다.
       */
      x0 = (keys_geo[i][0]) * KEYS_VIEW_UNIT / KEYS_GEO_UNIT;
      x1 = (keys_geo[i][0] + keys_geo[i][2]) * KEYS_VIEW_UNIT / KEYS_GEO_UNIT;
      w  = x1 - x0;
      if (w < 3) w = 3;
      if (x0 + w > KEYS_VIEW_W) continue;

      on = mark(keys_geo[i][4], keys_geo[i][5]);

      line[x0]         = '[';
      line[x0 + w - 1] = ']';
      for (uint32_t k = 1; k + 1 < w; k++) line[x0 + k] = on ? '#' : ' ';
    }
    cliPrintf("  %s\n", line);
  }
}

static void keysPrintTable(void)
{
  cliPrintf("      ");
  for (uint32_t ch = 0; ch < KEYS_CH_MAX; ch++)
  {
    cliPrintf(" ch%-2d", (int)ch);
  }
  cliPrintf("\n");

  for (uint32_t step = 0; step < KEYS_STEP_MAX; step++)
  {
    cliPrintf("  s%-2d ", (int)step);
    for (uint32_t ch = 0; ch < KEYS_CH_MAX; ch++)
    {
      cliPrintf(" %4d", (int)raw[step][ch]);
    }
    cliPrintf("\n");
  }
}

void cliKeys(cli_args_t *args)
{
  bool ret = false;


  /*
   * 키를 눌러가며 측정하는 명령만 리포트를 막는다.
   *
   * 측정용으로 누른 키가 호스트로 입력되면 터미널이 엉켜 측정 자체가 안 된다.
   * 반대로 info·cfg 처럼 한 번 찍고 끝나는 명령은 막을 이유가 없다 — 사용자가
   * 키를 누를 일이 없는데 막아두면 그냥 키보드가 멈춘 것으로 보인다.
   */
  static const char *interactive[] =
    { "cal", "map", "learn", "layout", "show", "bar", "watch", "noise", "raw" };

  for (uint32_t i = 0; i < sizeof(interactive) / sizeof(interactive[0]); i++)
  {
    if (args->argc == 1 && args->isStr(0, (char *)interactive[i]))
    {
      report_off = true;
      break;
    }
  }


  /*
   * 래피드 트리거 조작.
   *
   *   keys rt                        상태 보기
   *   keys rt on|off                 RT 켜고 끄기
   *   keys rt cont|bottom on|off     연속 RT / 바닥 보호
   *   keys rt press|release|bottom|dead <0.01mm>
   */
  if (args->argc >= 1 && args->isStr(0, "rt"))
  {
    uint8_t f = keysGetRtFlags();

    if (args->argc == 2 && (args->isStr(1, "on") || args->isStr(1, "off")))
    {
      keysSetRtFlags(args->isStr(1, "on") ? (uint8_t)(f | KEYS_RT_ON)
                                          : (uint8_t)(f & ~KEYS_RT_ON));
    }
    else if (args->argc == 3 && (args->isStr(1, "cont") || args->isStr(1, "bottom"))
                             && (args->isStr(2, "on") || args->isStr(2, "off")))
    {
      uint8_t bit = args->isStr(1, "cont") ? KEYS_RT_CONT : KEYS_RT_BOTTOM;

      keysSetRtFlags(args->isStr(2, "on") ? (uint8_t)(f | bit) : (uint8_t)(f & ~bit));
    }
    else if (args->argc == 3)
    {
      uint16_t um = (uint16_t)args->getData(2);

      if      (args->isStr(1, "press"))   keysSetRtPressUm(um);
      else if (args->isStr(1, "release")) keysSetRtReleaseUm(um);
      else if (args->isStr(1, "bottom"))  keysSetBottomUm(um);
      else if (args->isStr(1, "dead"))    keysSetDeadUm(um);
    }

    f = keysGetRtFlags();
    cliPrintf("RT        : %s   연속 %s   바닥 보호 %s\n",
              (f & KEYS_RT_ON)     ? "켬" : "끔",
              (f & KEYS_RT_CONT)   ? "켬" : "끔",
              (f & KEYS_RT_BOTTOM) ? "켬" : "끔");
    cliPrintf("재입력    : %d.%02d mm  (%d 카운트)\n",
              keysGetRtPressUm() / 100, keysGetRtPressUm() % 100, thr[0].rt_press);
    cliPrintf("입력 해제 : %d.%02d mm  (%d 카운트)\n",
              keysGetRtReleaseUm() / 100, keysGetRtReleaseUm() % 100, thr[0].rt_release);
    cliPrintf("바닥 보호 : %d.%02d mm  (깊이 %d 이상이면 RT 해제 끔)\n",
              keysGetBottomUm() / 100, keysGetBottomUm() % 100, thr[0].bottom_lo);
    cliPrintf("데드존    : %d.%02d mm  (%d 카운트)\n",
              keysGetDeadUm() / 100, keysGetDeadUm() % 100, thr[0].dead);
    cliPrintf("입력지점  : %d 카운트,  해제지점 %d 카운트  (0번 키)\n",
              thr[0].press, thr[0].release);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("keys init   : %d\n", is_init);
    cliPrintf("step x ch   : %d x %d = %d\n", KEYS_STEP_MAX, KEYS_CH_MAX, KEYS_MAX);
    cliPrintf("ADC0 seq ch : ");
    for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) cliPrintf("%d ", adc0_seq_ch[i]);
    cliPrintf("\nADC1 seq ch : ");
    for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) cliPrintf("%d ", adc1_seq_ch[i]);
    cliPrintf("\nmux addr    : ");
    for (uint32_t i = 0; i < KEYS_STEP_MAX; i++) cliPrintf("%d ", mux_addr[i]);
    cliPrintf("\nlayout      : %s  %d 키\n", KEYS_LAYOUT_NAME, KEYS_LAYOUT_KEY_CNT);
    cliPrintf("calibrated  : %d  (%d + %d scan, %d ms)\n",
              is_calibrated, KEYS_CAL_DISCARD, KEYS_CAL_SAMPLES, (int)cal_time_ms);
    cliPrintf("scan        : %d us  (max %d, %dus 초과 %d / %d 회)\n",
              (int)scan_time_us, (int)scan_us_max,
              KEYS_SCAN_OVER_US, (int)scan_over_cnt, (int)scan_cnt);
    cliPrintf("timeout     : %d\n", (int)timeout_cnt);
    ret = true;
  }

  /* 눌린 키를 실시간으로 본다 */
  if (args->argc == 1 && args->isStr(0, "show"))
  {
    while (keysCliKeep())
    {
      /* 헤더와 데이터 모두 열 폭 5, 앞 라벨 폭 6 으로 맞춘다 */
      cliPrintf("      ");
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" ch%d ", (int)c);
      cliPrintf("   row\n");

      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        cliPrintf("  s%-2d ", (int)st);
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          cliPrintf(" %s ", keysGetPressed(st, c) ? "[#]" : " . ");
        }
        cliPrintf("   0x%02X\n", (int)keysGetRow(st));
      }
      keysUpdate();
      cliMoveUp(KEYS_STEP_MAX + 1);
      delay(30);
    }
    cliMoveDown(KEYS_STEP_MAX + 1);
    ret = true;
  }

  /*
   * 실제 키보드 배치로 그린다.
   *
   * 8x8 격자로는 매핑이 맞는지 알 수 없다. ESC 를 눌렀을 때 ESC 자리에 표시돼야
   * 비로소 맞는 것이다. keyboards/<모델>/layout.h 의 물리 좌표를 그대로 쓴다.
   */
  if (args->argc == 1 && args->isStr(0, "layout"))
  {
    cliPrintf("%s  —  %d 키\n", KEYS_LAYOUT_NAME, KEYS_LAYOUT_KEY_CNT);

    while (keysCliKeep())
    {
      keysDrawLayout(keysGetPressed);
      keysUpdate();
      cliMoveUp(keysLayoutRows());
      delay(30);
    }
    cliMoveDown(keysLayoutRows());
    ret = true;
  }

  /*
   * 매핑 측정 — 키를 누를 때마다 한 줄씩 순번을 붙여 기록한다.
   *
   * keys map 은 누르는 내내 찍혀 로그가 길어진다. 물리 배치와 (s, ch) 를 짝지을 때는
   * "몇 번째로 누른 키가 어느 셀인가" 만 있으면 되므로 눌림 에지에서만 한 줄 낸다.
   * 이 목록을 via.json 의 매트릭스 주소로 그대로 옮긴다.
   */
  if (args->argc == 1 && args->isStr(0, "learn"))
  {
    static uint16_t prev[KEYS_STEP_MAX];
    uint32_t n = 0;

    memset(prev, 0, sizeof(prev));
    cliPrintf("키를 순서대로 누르세요. 누를 때마다 한 줄씩 기록합니다.\n\n");

    while (keysCliKeep())
    {
      keysUpdate();
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        uint16_t now  = keysGetRow(st);
        uint16_t rise = now & (uint16_t)~prev[st];

        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          if (rise & (1U << c)) cliPrintf("  #%02d  %d,%d\n", (int)++n, (int)st, (int)c);
        }
        prev[st] = now;
      }
      delay(5);
    }
    cliPrintf("\n%d 개 기록\n", (int)n);
    ret = true;
  }

  /*
   * 보정 — 전 키를 끝까지 눌러 바닥값을 모은다.
   *
   * 기준값(무압)은 러닝 최대값이 늘 추적하지만, 바닥값은 실제로 끝까지 눌러야만
   * 알 수 있다. 이 둘이 있어야 눌린 깊이를 mm 로 환산할 수 있다 (13편 래피드 트리거).
   *
   * 레이아웃 뷰로 진행 상황을 보여준다 — 어느 키가 남았는지 눈으로 보인다.
   */
  if (args->argc == 1 && args->isStr(0, "cal"))
  {
    uint32_t total  = 0;
    uint32_t done   = 0;
    uint32_t rows;
    bool     cancel = false;

    for (uint32_t i = 0; i < KEYS_MAX; i++) cal_min_tmp[i] = 0xFFFF;
    for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
    {
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++) if (keysIsPresent(st, c)) total++;
    }

    cliPrintf("모든 키를 끝까지 한 번씩 눌러주세요.\n");
    cliPrintf("채워진 자리가 끝난 키입니다.\n");
    cliPrintf("키보드에서   [Ctrl + Enter] 여기까지 저장하고 끝\n");
    cliPrintf("             [Ctrl + ESC]   취소 (저장하지 않음)\n\n");
    rows = keysLayoutRows();

    while (keysCliKeep())
    {
      keysUpdate();

      done = 0;
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          uint32_t i = st * KEYS_CH_MAX + c;
          uint16_t v;

          if (keysIsPresent(st, c) == false) continue;

          v = raw[st][c];
          if (v < cal_min_tmp[i]) cal_min_tmp[i] = v;

          if ((int32_t)base[st][c] - (int32_t)cal_min_tmp[i] >= KEYS_CAL_STROKE_MIN) done++;
        }
      }

      keysDrawLayout(keysCalIsDone);
      cliPrintf("  %d / %d 완료    \n", (int)done, (int)total);
      cliMoveUp(rows + 1);

      if (done >= total) break;                                  /* 전부 끝남 -> 저장 */

      if (keysComboHeld(KEYS_MOD_KC, KEYS_CANCEL_KC))
      {
        cancel = true;
        break;
      }
      if (keysComboHeld(KEYS_MOD_KC, KEYS_SAVE_KC)) break;

      delay(30);
    }
    cliMoveDown(rows + 1);

    /*
     * ★ 부분 저장을 허용한다.
     *
     *   레이아웃에는 레이아웃 옵션 소켓(스플릿 백스페이스 등)이 다 들어 있지만
     *   실제로는 그중 하나만 끼운다. 그래서 "전부 끝나야 저장"으로 막으면 영영
     *   저장할 수 없다. 끝난 키만 보정됨으로 표시하고, 나머지는 종류표의 공칭값을
     *   계속 쓰면 된다.
     */
    if (cancel)
    {
      cliPrintf("\n취소 — 저장하지 않는다 (기존 보정 유지)\n");
    }
    else if (done > 0)
    {
      uint32_t n_skip = 0;

      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          uint32_t i = st * KEYS_CH_MAX + c;

          if (keysIsPresent(st, c) == false) continue;

          if (keysCalIsDone(st, c))
          {
            cfg.key[i].cal_max = base[st][c];
            cfg.key[i].cal_min = cal_min_tmp[i];
            cfg.key[i].flags  |= 0x01;
          }
          else
          {
            n_skip++;
          }
        }
      }

      cliPrintf("\n보정 %d / %d 저장", (int)done, (int)total);
      if (n_skip) cliPrintf("  (%d개는 스위치가 없거나 덜 눌림 — 공칭값 유지)", (int)n_skip);
      cliPrintf("\n");

      cliPrintf("save : %s  seq %d\n", keysCfgSave() ? "OK" : "E_", (int)cfg.seq);

      /* 어느 자리가 빠졌는지 알려준다. 예상과 다르면 배선이나 장착을 봐야 한다. */
      if (n_skip)
      {
        cliPrintf("빠진 자리 : ");
        for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
        {
          for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
          {
            if (keysIsPresent(st, c) && keysCalIsDone(st, c) == false)
            {
              cliPrintf("%d,%d ", (int)st, (int)c);
            }
          }
        }
        cliPrintf("\n");
      }
    }
    else
    {
      cliPrintf("\n눌린 키가 없어 저장하지 않는다\n");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "cfg"))
  {
    uint32_t n_cal = 0;

    for (uint32_t i = 0; i < KEYS_MAX; i++) if (cfg.key[i].flags & 1) n_cal++;

    cliPrintf("loaded      : %d  (seq %d)\n", is_cfg_loaded, (int)cfg.seq);
    cliPrintf("press       : %d.%02d mm\n", cfg.press_um / 100, cfg.press_um % 100);
    cliPrintf("release     : %d.%02d mm\n", cfg.release_um / 100, cfg.release_um % 100);
    cliPrintf("rapid       : %s  되눌림 %d.%02d mm / 되뗌 %d.%02d mm\n",
              (cfg.rt_flags & KEYS_RT_ON) ? "켬" : "끔",
              cfg.rt_press_um / 100,   cfg.rt_press_um % 100,
              cfg.rt_release_um / 100, cfg.rt_release_um % 100);
    cliPrintf("바닥 보호   : %s  %d.%02d mm\n",
              (cfg.rt_flags & KEYS_RT_BOTTOM) ? "켬" : "끔",
              cfg.bottom_um / 100, cfg.bottom_um % 100);
    cliPrintf("데드존      : %d.%02d mm\n", cfg.dead_um / 100, cfg.dead_um % 100);
    cliPrintf("switch      : %d (%s, %d.%02d mm)\n", cfg.sw_type_def,
              keys_switch[cfg.sw_type_def].name,
              keys_switch[cfg.sw_type_def].travel_um / 100,
              keys_switch[cfg.sw_type_def].travel_um % 100);
    cliPrintf("calibrated  : %d / %d 키\n", (int)n_cal, KEYS_MAX);
    cliPrintf("record      : %d B\n", (int)sizeof(keys_cfg_t));

    if (n_cal)
    {
      uint32_t lo = 0xFFFF, hi = 0, sum = 0;

      cliPrintf("\n키별 스트로크 (보정된 것만, '-' 은 미보정)\n      ");
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" ch%-3d", (int)c);
      cliPrintf("\n");

      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        cliPrintf("  s%-2d ", (int)st);
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          uint32_t i = st * KEYS_CH_MAX + c;

          if (cfg.key[i].flags & 1)
          {
            uint32_t st_v = cfg.key[i].cal_max - cfg.key[i].cal_min;

            cliPrintf(" %5d", (int)st_v);
            if (st_v < lo) lo = st_v;
            if (st_v > hi) hi = st_v;
            sum += st_v;
          }
          else
          {
            cliPrintf("     -");
          }
        }
        cliPrintf("\n");
      }
      cliPrintf("\n  최소 %d, 최대 %d, 평균 %d  (편차 %d%)\n",
                (int)lo, (int)hi, (int)(sum / n_cal),
                (int)((hi - lo) * 100 / (sum / n_cal)));
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "save"))
  {
    uint32_t t = millis();
    bool     ok = keysCfgSave();

    cliPrintf("save : %s  seq %d  (%d ms)\n", ok ? "OK" : "E_",
              (int)cfg.seq, (int)(millis() - t));
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "load"))
  {
    bool ok = keysCfgLoad();

    cliPrintf("load : %s  seq %d\n", ok ? "OK" : "없음(기본값)", (int)cfg.seq);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "base"))
  {
    cliPrintf("기준값을 다시 잡는다 — 키에서 손을 떼고 있을 것\n");
    delay(300);
    if (keysCalibrate())
    {
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        cliPrintf("  s%-2d ", (int)st);
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" %5d", (int)base[st][c]);
        cliPrintf("\n");
      }
    }
    else
    {
      cliPrintf("[E_] 캘리브레이션 실패\n");
    }
    ret = true;
  }

  /*
   * 키 하나를 누르면 어느 셀이 얼마나 움직이는지 알려준다.
   * 64셀 <-> 실제 키 위치 표를 이걸로 만든다.
   */
  if (args->argc == 1 && args->isStr(0, "map"))
  {
    int32_t  last_d  = 0;
    uint32_t last_st = 0, last_ch = 0;

    cliPrintf("키를 하나씩 눌러본다. 가장 크게 움직인 셀을 표시한다.\n\n");

    while (keysCliKeep())
    {
      int32_t  best   = 0;
      uint32_t best_s = 0, best_c = 0;

      keysUpdate();

      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          int32_t d = keysGetDelta(st, c);
          int32_t a = (d < 0) ? -d : d;

          if (a > ((best < 0) ? -best : best)) { best = d; best_s = st; best_c = c; }
        }
      }

      /* 값이 크게 바뀐 순간에만 한 줄 남긴다 — 스크롤이 넘치지 않게 */
      if (((best < 0) ? -best : best) > KEYS_MAP_REPORT_MIN &&
          (best_s != last_st || best_c != last_ch ||
           ((best - last_d) > 200) || ((last_d - best) > 200)))
      {
        cliPrintf("  s%d / ch%d   delta %+6d   (raw %5d, base %5d)\n",
                  (int)best_s, (int)best_c, (int)best,
                  (int)raw[best_s][best_c], (int)base[best_s][best_c]);
        last_d = best; last_st = best_s; last_ch = best_c;
      }
      delay(20);
    }
    ret = true;
  }

  /*
   * 셀별 노이즈 측정.
   *
   * "값이 흔들린다"와 "값이 치우쳐 있다"는 다른 문제인데 한 장면만 봐서는 구분이 안 된다.
   * 일정 시간 동안 편차의 최소/최대를 모아 진폭(p-p)과 중심을 같이 보여준다.
   *
   *   진폭이 크다        -> 그 셀의 노이즈가 크다
   *   진폭은 작고 치우침 -> 스위치가 덜 복귀했거나 기준값이 아직 안 맞았다
   */
  if (args->argc == 1 && args->isStr(0, "noise"))
  {
    static int16_t  d_min[KEYS_STEP_MAX][KEYS_CH_MAX];
    static int16_t  d_max[KEYS_STEP_MAX][KEYS_CH_MAX];
    static uint16_t a_min[KEYS_STEP_MAX][KEYS_CH_MAX];
    static uint16_t a_max[KEYS_STEP_MAX][KEYS_CH_MAX];
    uint32_t t_begin;
    uint32_t cnt = 0;

    for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
    {
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
      {
        d_min[st][c] = 32767; d_max[st][c] = -32768;
        a_min[st][c] = 0xFFFF; a_max[st][c] = 0;
      }
    }

    cliPrintf("%d ms 동안 측정한다 — 키에서 손을 뗄 것\n", KEYS_NOISE_MS);
    delay(300);

    t_begin = millis();
    while (millis() - t_begin < KEYS_NOISE_MS)
    {
      keysUpdate();
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          int32_t  d = keysGetDelta(st, c);
          uint16_t a = keysGetAcc(st, c);

          if (d < d_min[st][c]) d_min[st][c] = (int16_t)d;
          if (d > d_max[st][c]) d_max[st][c] = (int16_t)d;
          if (a < a_min[st][c]) a_min[st][c] = a;
          if (a > a_max[st][c]) a_max[st][c] = a;
        }
      }
      cnt++;
    }

    cliPrintf("\n진폭 (p-p)\n      ");
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" ch%-3d", (int)c);
    cliPrintf("\n");
    for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
    {
      cliPrintf("  s%-2d ", (int)st);
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        cliPrintf(" %5d", (int)(d_max[st][c] - d_min[st][c]));
      cliPrintf("\n");
    }

    cliPrintf("\n중심 ((max+min)/2)\n      ");
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" ch%-3d", (int)c);
    cliPrintf("\n");
    for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
    {
      cliPrintf("  s%-2d ", (int)st);
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        cliPrintf(" %+5d", (int)((d_max[st][c] + d_min[st][c]) / 2));
      cliPrintf("\n");
    }
    /*
     * ★ 필터 이전 값도 같이 낸다.
     *
     *   위 표는 데드밴드를 통과한 뒤라 밴드 폭을 바꾸면 같이 움직인다. 누적을
     *   넣으면서 밴드도 7 -> 12 로 바꿨으니 그 표만으로는 누적의 효과를 못 가른다.
     *   아래가 센서에서 온 그대로다.
     */
    {
      uint32_t sum = 0, n = 0, mx = 0;

      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          uint32_t pp;

          if ((keys_present[st] & (1U << c)) == 0) continue;
          pp = (uint32_t)(a_max[st][c] - a_min[st][c]);
          sum += pp; n++;
          if (pp > mx) mx = pp;
        }
      }
      n = n ? n : 1;

      cliPrintf("\n데드밴드 이전 (누적 %d개, 눈금 0~%d)\n", KEYS_ACC_CNT, 4095 * KEYS_ACC_CNT);
      cliPrintf("  p-p 평균 %d, 최대 %d  (셀 %d)\n", (int)(sum / n), (int)mx, (int)n);
      cliPrintf("  스트로크 %d 대비 %d.%02d %\n",
                (int)(836 * KEYS_ACC_CNT),
                (int)((sum / n) * 100 / (836 * KEYS_ACC_CNT)),
                (int)((sum / n) * 10000 / (836 * KEYS_ACC_CNT) % 100));
    }

    cliPrintf("\n%d 회 스캔\n", (int)cnt);
    ret = true;
  }

  /*
   * 눌린 깊이를 가로 막대로 본다.
   *
   * keys map 은 숫자만 흘려서 "얼마나 깊이 들어갔나"가 눈에 안 들어온다. 여기서는
   * 최대 6개까지 슬롯을 잡아 왼쪽에 좌표·값, 오른쪽에 막대를 그린다.
   *
   * 슬롯은 한 번 잡히면 값이 0 으로 돌아올 때까지 유지한다 — 떼는 동안 막대가
   * 줄어드는 걸 봐야 하기 때문이다. 그래야 자리가 튀지 않는다.
   */
  if (args->argc == 1 && args->isStr(0, "bar"))
  {
    int8_t   slot_s[KEYS_BAR_SLOTS];
    int8_t   slot_c[KEYS_BAR_SLOTS];
    uint32_t slot_ms[KEYS_BAR_SLOTS];
    /* 표시도 실제 판정값을 쓴다 — 0번 키 기준 (키별로 조금씩 다르다) */
    uint32_t press_x = thr[0].press   * KEYS_BAR_W / KEYS_BAR_FULL;
    uint32_t rel_x   = thr[0].release * KEYS_BAR_W / KEYS_BAR_FULL;

    for (uint32_t i = 0; i < KEYS_BAR_SLOTS; i++)
    {
      slot_s[i] = -1; slot_c[i] = -1; slot_ms[i] = 0;
    }

    cliPrintf("눌린 깊이. ':' 해제 임계 %d, '|' 누름 임계 %d, 전체 %d\n\n",
              thr[0].release, thr[0].press, KEYS_BAR_FULL);

    while (keysCliKeep())
    {
      keysUpdate();

      /* 살아 있는 슬롯의 시각을 갱신한다 — 재활용 순서를 정하는 기준이다 */
      for (uint32_t i = 0; i < KEYS_BAR_SLOTS; i++)
      {
        if (slot_s[i] >= 0 && -keysGetDelta(slot_s[i], slot_c[i]) >= KEYS_BAR_MIN)
        {
          slot_ms[i] = millis();
        }
      }

      /*
       * 새로 움직인 셀을 슬롯에 앉힌다.
       *
       * ★ 빈 슬롯을 앞에서부터 찾으면 순차로 눌렀을 때 0번만 계속 재활용되어
       *   한 줄에만 나온다. 빈 자리를 먼저 쓰고, 없으면 "가장 오래 조용했던"
       *   슬롯을 밀어낸다. 그래야 동시에 눌러도, 하나씩 눌러도 쌓인다.
       */
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          int32_t  d = -keysGetDelta(st, c);      /* 누를수록 양수 */
          bool     have = false;
          uint32_t pick = KEYS_BAR_SLOTS;

          if (d < KEYS_BAR_MIN) continue;

          for (uint32_t i = 0; i < KEYS_BAR_SLOTS; i++)
          {
            if (slot_s[i] == (int8_t)st && slot_c[i] == (int8_t)c) { have = true; break; }
          }
          if (have) continue;

          for (uint32_t i = 0; i < KEYS_BAR_SLOTS; i++)      /* 빈 자리 우선 */
          {
            if (slot_s[i] < 0) { pick = i; break; }
          }
          if (pick == KEYS_BAR_SLOTS)                        /* 없으면 가장 오래 조용했던 슬롯 */
          {
            for (uint32_t i = 0; i < KEYS_BAR_SLOTS; i++)
            {
              if (-keysGetDelta(slot_s[i], slot_c[i]) >= KEYS_BAR_MIN) continue;
              if (pick == KEYS_BAR_SLOTS || slot_ms[i] < slot_ms[pick]) pick = i;
            }
          }
          if (pick < KEYS_BAR_SLOTS)
          {
            slot_s[pick]  = (int8_t)st;
            slot_c[pick]  = (int8_t)c;
            slot_ms[pick] = millis();
          }
        }
      }

      for (uint32_t i = 0; i < KEYS_BAR_SLOTS; i++)
      {
        char bar[KEYS_BAR_W + 1];

        if (slot_s[i] < 0)
        {
          cliPrintf("  --      ---      ---  %*s   \n", KEYS_BAR_W, "");
          continue;
        }

        {
          int32_t  d = -keysGetDelta(slot_s[i], slot_c[i]);
          uint32_t n;

          if (d < 0) d = 0;
          n = (uint32_t)d * KEYS_BAR_W / KEYS_BAR_FULL;
          if (n > KEYS_BAR_W) n = KEYS_BAR_W;

          for (uint32_t k = 0; k < KEYS_BAR_W; k++)
          {
            if (k < n)            bar[k] = '#';
            else if (k == press_x) bar[k] = '|';
            else if (k == rel_x)   bar[k] = ':';
            else                   bar[k] = '.';
          }
          bar[KEYS_BAR_W] = 0;

          cliPrintf("  s%d,ch%d  raw %4d  d %4d  %s %s\n",
                    (int)slot_s[i], (int)slot_c[i],
                    (int)keysGetRaw(slot_s[i], slot_c[i]), (int)d, bar,
                    keysGetPressed(slot_s[i], slot_c[i]) ? "ON" : "  ");
        }
      }

      cliMoveUp(KEYS_BAR_SLOTS);
      delay(30);
    }
    cliMoveDown(KEYS_BAR_SLOTS);
    ret = true;
  }

  /* 편차 표. 눌린 셀이 표에서 바로 보인다. */
  if (args->argc == 1 && args->isStr(0, "watch"))
  {
    while (keysCliKeep())
    {
      keysUpdate();
      cliPrintf("      ");
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" ch%-3d", (int)c);
      cliPrintf("\n");
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        cliPrintf("  s%-2d ", (int)st);
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" %+5d", (int)keysGetDelta(st, c));
        cliPrintf("\n");
      }
      cliMoveUp(KEYS_STEP_MAX + 1);
      delay(50);
    }
    cliMoveDown(KEYS_STEP_MAX + 1);
    ret = true;
  }

  /* 한 번만 찍는다. 스크립트로 캡처할 때 쓴다. */
  if (args->argc == 1 && args->isStr(0, "dump"))
  {
    keysUpdate();
    keysPrintTable();
    cliPrintf("  scan : %d us\n", (int)scan_time_us);
    ret = true;
  }

  /* 살아있는 8x8 표. 키를 눌러 값이 어떻게 움직이는지 본다. */
  if (args->argc == 1 && args->isStr(0, "raw"))
  {
    while (keysCliKeep())
    {
      keysUpdate();
      keysPrintTable();
      cliPrintf("  scan : %d us   \n", (int)scan_time_us);
      cliMoveUp(KEYS_STEP_MAX + 2);
      delay(50);
    }
    cliMoveDown(KEYS_STEP_MAX + 2);
    ret = true;
  }

  /*
   * 스캔 한 바퀴 시간 — 8kHz(125us) 예산과 비교하는 근거.
   *
   * 총량만으로는 손댈 데를 못 고른다. 같은 루프를 네 단계로 쌓아 돌리고 차이로
   * 각 몫을 뽑는다.
   *
   *   mux      MUX 주소 쓰기 + 세틀링
   *   +adc     트리거와 완료 대기 — 여기 늘어난 만큼이 ADC 변환 시간이다
   *   +filter  데드밴드 필터 8채널
   *   +track   기준값 추적과 눌림 판정  == keysUpdate() 와 같다
   *
   * ★ 변환 대기가 크면 후처리를 그 안으로 겹치는 게 답이고 (파이프라인),
   *   후처리가 크면 스텝 루프에서 빼내는 게 답이다. 답이 갈리므로 먼저 잰다.
   */
  if (args->argc == 1 && args->isStr(0, "time"))
  {
    const uint32_t   N       = 2000;
    static const char *name[4] = { "mux+settle", "  +adc 변환", "  +filter ", "  +track  " };
    uint32_t         us[4]   = { 0, };
    uint32_t         cnt     = 0;
    uint32_t         t_begin;

    /* 드리프트가 끼면 회차마다 몫이 달라진다. 흔한 쪽(안 도는 회차)으로 고정한다. */
    drift_due = false;

    for (uint32_t mode = 0; mode < 4; mode++)
    {
      uint32_t t = micros();

      for (uint32_t n = 0; n < N; n++)
      {
        for (uint32_t step = 0; step < KEYS_STEP_MAX; step++)
        {
          gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[step]);
          keysSettle();

          if (mode >= 1)
          {
            keysDmaArm();
            adc16_trigger_seq_by_sw(HPM_ADC0);
            adc16_trigger_seq_by_sw(HPM_ADC1);
            if (keysWaitDma() == false) break;
            gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[step + 1]);
          }

          if (mode >= 2)
          {
            for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++)
            {
              keysFilter(step, i,                adc0_buf[i]);
              keysFilter(step, KEYS_SEQ_LEN + i, adc1_buf[i]);
            }
          }

          if (mode >= 3 && is_calibrated) keysTrack(step);
        }
      }
      us[mode] = micros() - t;
    }

    cliPrintf("스캔 1회 = %d step x %d ch,  %d 회 평균\n\n",
              KEYS_STEP_MAX, KEYS_CH_MAX, (int)N);

    for (uint32_t i = 0; i < 4; i++)
    {
      uint32_t ns   = us[i] * 1000 / N;                       /* 스캔 1회 */
      uint32_t d_ns = ns - (i ? (us[i - 1] * 1000 / N) : 0);  /* 이번 단계 몫 */

      cliPrintf("  %s : %3d.%03d us   (+%2d.%03d)\n",
                name[i], (int)(ns / 1000), (int)(ns % 1000),
                (int)(d_ns / 1000), (int)(d_ns % 1000));
    }

    /* 실제 keysUpdate() 로도 한 번 — 위 분해가 맞는지 대조한다 */
    t_begin = micros();
    while (micros() - t_begin < 500000)
    {
      keysUpdate();
      cnt++;
    }
    cnt = cnt ? cnt : 1;

    cliPrintf("\nkeysUpdate : %d us,  %d 회/초\n", (int)(500000 / cnt), (int)(cnt * 2));
    cliPrintf("8kHz 예산 125us 대비 : %d %\n", (int)((500000 / cnt) * 100 / 125));
    cliPrintf("timeout    : %d\n", (int)timeout_cnt);
    ret = true;
  }

  /* 진단 — 시퀀스가 실제로 도는지, 어떤 인터럽트 비트가 서는지 본다 */
  if (args->argc == 1 && args->isStr(0, "adc"))
  {
    struct { const char *name; ADC16_Type *ptr; volatile uint32_t *buf; }
    tbl[2] = { {"ADC0", HPM_ADC0, adc0_buf}, {"ADC1", HPM_ADC1, adc1_buf} };

    for (uint32_t n = 0; n < 2; n++)
    {
      uint32_t sts = 0;
      uint32_t spin;

      cliPrintf("%s\n", tbl[n].name);
      cliPrintf("  SEQ_CFG0 : 0x%08X\n", (unsigned)tbl[n].ptr->SEQ_CFG0);
      cliPrintf("  INT_EN   : 0x%08X\n", (unsigned)tbl[n].ptr->INT_EN);
      cliPrintf("  INT_STS  : 0x%08X\n", (unsigned)adc16_get_status_flags(tbl[n].ptr));

      adc16_clear_status_flags(tbl[n].ptr, ADC16_INT_STS_SEQ_CMPT_MASK);
      for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) tbl[n].buf[i] = 0xDEADBEEF;

      adc16_trigger_seq_by_sw(tbl[n].ptr);

      for (spin = 0; spin < 200000; spin++)
      {
        sts = adc16_get_status_flags(tbl[n].ptr);
        if (sts) break;
      }

      cliPrintf("  트리거 후 INT_STS : 0x%08X (spin %d)\n", (unsigned)sts, (int)spin);
      cliPrintf("  DMA 버퍼          : ");
      for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) cliPrintf("0x%08X ", (unsigned)tbl[n].buf[i]);
      cliPrintf("\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("keys info\n");
    cliPrintf("keys adc\n");
    cliPrintf("keys show      눌린 키 표시 (8x8 격자)\n");
    cliPrintf("keys layout    눌린 키 표시 (실제 배치)\n");
    cliPrintf("keys learn     매핑 측정 — 누를 때마다 \"s,ch\" 한 줄\n");
    cliPrintf("keys cal       전 키 보정 (끝까지 눌러 바닥값 수집)\n");
    cliPrintf("keys cfg       저장된 설정 보기\n");
    cliPrintf("keys save      설정 저장\n");
    cliPrintf("keys load      설정 다시 읽기\n");
    cliPrintf("keys base\n");
    cliPrintf("keys map\n");
    cliPrintf("keys bar       눌린 깊이를 막대로 (최대 6개)\n");
    cliPrintf("keys watch\n");
    cliPrintf("keys noise\n");
    cliPrintf("keys dump\n");
    cliPrintf("keys raw\n");
    cliPrintf("keys time\n");
    cliPrintf("keys rt        래피드 트리거 (on/off, cont, bottom, press, release, dead)\n");
  }

  /* ★ 반드시 되돌린다. 안 그러면 keys 명령을 한 번 쓴 뒤로 키보드가 죽는다. */
  report_off = false;
}
#endif

#endif
