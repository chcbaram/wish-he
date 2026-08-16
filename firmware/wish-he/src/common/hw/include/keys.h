#ifndef KEYS_H_
#define KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_KEYS


#define KEYS_STEP_MAX     HW_KEYS_STEP_MAX    /* MUX 스텝 = 논리 행 */
#define KEYS_CH_MAX       HW_KEYS_CH_MAX      /* ADC 채널 = 논리 열 */
#define KEYS_MAX          (KEYS_STEP_MAX * KEYS_CH_MAX)


bool     keysInit(void);

/* 한 바퀴(8스텝) 스캔. 원시값을 갱신한다. */
bool     keysUpdate(void);

/* 직전 스캔의 원시값. step = MUX 스텝, ch = ADC 채널 */
uint16_t keysGetRaw(uint8_t step, uint8_t ch);

/* 직전 스캔 한 바퀴에 걸린 시간 (us). 12편 예산 계산의 근거가 된다. */
/* 진단용 통계 — 화면이 늘 함께 보므로 한 번에 준다 */
typedef struct
{
  uint32_t scan_us;        /* 마지막 스캔 한 바퀴 */
  uint32_t scan_us_max;    /* 그중 최악 */
  uint32_t scan_over;      /* 기준(60us)을 넘긴 횟수 */
  uint32_t scan_cnt;       /* 전체 스캔 횟수 */
  uint32_t timeout;        /* ADC 타임아웃 */
  uint32_t cal_ms;         /* 부팅 보정에 걸린 시간 */
  uint32_t calibrated;
} keys_stat_t;

void     keysGetStat(keys_stat_t *p_stat);

uint32_t keysGetScanTime(void);

/*
 * 리포트를 내보내도 되는가.
 *
 * keys 명령(매핑·보정·관측)이 도는 동안은 false 다. 측정하려고 누른 키가 호스트로
 * 입력되면 터미널이 엉켜 측정을 못 한다.
 */
bool     keysIsReportEnabled(void);

/*
 * 무압 기준값을 다시 잡는다. 부팅 때 자동으로 한 번 한다.
 *
 * 채널마다 기준값이 다르므로(자석·센서·기구 공차) 절대값이 아니라 기준값 대비
 * 편차로 판정한다.
 */
bool     keysCalibrate(void);

/* 프로파일 — 설정 한 벌을 통째로 갈아 끼운다 (보정값은 공유한다) */
uint8_t  keysProfGet(void);
uint8_t  keysProfCount(void);
/*
 * 설정이 바뀌었다고 표시한다. 실제 저장은 메인 루프가 조용해진 뒤에 한 번 한다.
 * 값을 바꾸는 길이 여럿이라(VIA 채널·키별 명령·CLI) 저장을 그 자리마다 붙이면
 * 반드시 하나를 빠뜨린다.
 */
void     keysCfgTouch(void);
void     keysCfgUpdate(void);           /* 메인 루프에서 — ISR 금지 (플래시) */

bool     keysProfSelect(uint8_t idx);   /* 메모리에서 갈아 끼우기만 — 싸다 */
bool     keysProfSave(void);            /* 플래시에 남기기 — ISR 밖에서 */
void     keysProfTouch(void);           /* 나중에 남겨라 (키로 바꿀 때) */
void     keysProfChanged_kb(uint8_t idx);  /* 0xFF = 바뀌기 직전 — ISR 일 수 있다 */
void     keysProfUpdate_kb(void);          /* 미뤄 둔 일 — 메인 루프 */
bool     keysProfSet(uint8_t idx);      /* 둘 다 (CLI 용) */
bool     keysProfCopy(uint8_t dst);     /* 지금 것을 dst 에 붓는다 */

/*
 * 보정 — 바닥값 모으기. CLI 와 HID 가 같은 핵을 쓴다.
 *
 * 무압 기준값은 러닝 최대값이 늘 추적하므로 여기서 할 일이 없다. 바닥값만 모은다 —
 * 그건 실제로 끝까지 눌러야만 알 수 있다.
 */
void     keysCalStart(void);
void     keysCalCancel(void);
void     keysCalCollect(void);        /* keysUpdate 안에서 불린다 */
bool     keysCalIsActive(void);
uint32_t keysCalTotal(void);
uint32_t keysCalDone(void);
uint32_t keysCalBitmap(uint8_t *p_buf, uint32_t len);   /* 비트 i = 키 i 완료 */
uint32_t keysCalStrokes(uint32_t start, uint16_t *p_out, uint32_t max);  /* 진행 중인 행정 */
bool     keysCalSave(uint32_t *p_done, uint32_t *p_skip);

/* 눌림 판정. row = MUX 스텝, col = ADC 채널 */
bool     keysGetPressed(uint16_t row, uint16_t col);

/* 레이아웃에 실재하는 셀인가 (keyboards/<모델>/layout.h) */
bool     keysIsPresent(uint16_t row, uint16_t col);

/* 그 자리의 HID Usage ID. 0 이면 배정 안 됨 */
uint8_t  keysGetKeycode(uint16_t row, uint16_t col);

/* 행 비트마스크 (col c -> bit c). QMK matrix_row_t 와 비트 순서가 같다. */
uint16_t keysGetRow(uint16_t row);

uint16_t keysGetBase(uint8_t step, uint8_t ch);
int32_t  keysGetDelta(uint8_t step, uint8_t ch);

/*
 * mm 환산 — 라이브 트래킹과 13편(래피드 트리거)이 쓴다. 단위는 0.01mm.
 *
 * 깊이의 영점은 살아 있는 기준값이고, 기울기만 보정에서 온다. 보정하지 않은 키도
 * 스위치 종류표의 공칭값으로 환산되므로 항상 값이 나온다.
 */
uint16_t keysGetTravelUm(uint16_t row, uint16_t col);   /* 그 키의 전 행정 */
uint16_t keysGetDepthUm(uint16_t row, uint16_t col);    /* 지금 눌린 깊이 */

/*
 * 설정 — VIA 커스텀 메뉴가 읽고 쓴다. 단위는 0.01mm.
 *
 * 값만 바꾼다. 플래시 저장은 `keys save` 처럼 사용자가 명시할 때만 한다.
 */
uint16_t keysGetPressUm(void);
uint16_t keysGetReleaseUm(void);
uint8_t  keysGetSwitchType(void);
void     keysSetPressUm(uint16_t um);
void     keysSetReleaseUm(uint16_t um);
void     keysSetSwitchType(uint8_t type);

/* 설정을 플래시에 남긴다. 바꾸는 것과 저장은 따로다. */
bool     keysSave(void);

/*
 * 키별 설정을 바이트로. 배치는 keys.c 주석 참고.
 * keysSetKeyCfg 는 idx 가 범위를 벗어나면 전 키에 적용한다.
 */
uint32_t keysGetKeyCfg(uint32_t idx, uint8_t *p_buf, uint32_t len);
bool     keysSetKeyCfg(uint32_t idx, const uint8_t *p_buf, uint32_t len);

/* 스위치 종류표. 앞 GenericCount 개가 일반형이고 나머지가 제원을 아는 제품이다. */
uint32_t    keysGetSwitchCount(void);
uint32_t    keysGetSwitchGenericCount(void);
const char *keysGetSwitchName(uint32_t i);
uint16_t    keysGetSwitchTravelUm(uint32_t i);

/* 래피드 트리거 — 전부 0.01mm. 플래그는 keys.c 의 KEYS_RT_* */
uint16_t keysGetRtPressUm(void);
uint16_t keysGetRtReleaseUm(void);
uint16_t keysGetBottomUm(void);
uint16_t keysGetDeadUm(void);
uint8_t  keysGetRtFlags(void);
void     keysSetRtPressUm(uint16_t um);
void     keysSetRtReleaseUm(uint16_t um);
void     keysSetBottomUm(uint16_t um);
void     keysSetDeadUm(uint16_t um);
void     keysSetRtFlags(uint8_t flags);

/* 물리 배치 — {x, y, w, h, row, col} 6바이트, 1/4 키유닛. 웹 도구가 JSON 없이 그린다. */
uint32_t       keysGetLayoutCount(void);
const uint8_t *keysGetLayoutEntry(uint32_t idx);


#endif


#ifdef __cplusplus
}
#endif

#endif
