/*
 * rgb_matrix_kb.inc  —  이 보드만의 RGB 효과.
 *
 * ★ 여기 있는 것들은 **HE 라서 만들 수 있는 효과**다.
 *
 *   멤브레인이나 기계식은 키가 눌렸는지 아닌지 두 값밖에 없어서, 반응형 효과가
 *   전부 "눌린 순간부터 시간에 따라 사라지는" 모양이 된다. HE 는 깊이가 연속값으로
 *   들어오므로 **지금 얼마나 눌려 있는지**에 직접 반응할 수 있다. 손가락을 멈추면
 *   빛도 멈춘다.
 *
 *   깊이는 keysGetDepthUm() 이 0.01mm 단위로 주고, 키마다 전 행정이 다르므로
 *   keysGetTravelUm() 으로 나눠 0~255 로 맞춘다 (스트로크 편차가 실측 13% 다).
 *
 * ★ 이 파일은 **두 번** 포함된다.
 *
 *   한 번은 rgb_matrix.h 가 효과 이름 enum 을 만들 때, 한 번은 rgb_matrix.c 가
 *   구현을 넣을 때다. 앞의 것은 **enum 본문 안**이라 거기에 뭘 쓰면 문법 오류가
 *   된다 — `#include` 도 마찬가지다. 그래서 선언 말고는 전부
 *   RGB_MATRIX_CUSTOM_EFFECT_IMPLS 안에 둔다.
 */


/* ── 공통 ──────────────────────────────────────────────────────────────── */
#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#include "keys.h"
#include "layout.h"

/*
 * LED 하나가 얼마나 눌려 있는가 (0~255).
 *
 * 언더글로우는 키에 안 붙어 있으므로 **판 전체에서 가장 깊은 값**을 따라간다 —
 * 아무 키나 누르면 아래쪽이 같이 반응해서 하나의 물건처럼 보인다.
 */
static uint8_t heDepth8(uint8_t row, uint8_t col)
{
    uint16_t travel = keysGetTravelUm(row, col);
    uint32_t depth  = keysGetDepthUm(row, col);

    if (travel == 0) return 0;
    if (depth >= travel) return 255;
    return (uint8_t)((depth * 255U) / travel);
}

/* 판에서 가장 깊이 눌린 키. 64셀을 훑으므로 프레임당 한 번만 센다. */
static uint8_t heDepthMax(void)
{
    static uint8_t  cached   = 0;
    static uint16_t cached_t = 0xFFFF;
    uint16_t        now      = (uint16_t)g_rgb_timer;

    if (now != cached_t) {
        uint8_t mx = 0;
        cached_t = now;
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                uint8_t d = heDepth8(r, c);
                if (d > mx) mx = d;
            }
        }
        cached = mx;
    }
    return cached;
}

/* 키 밑 LED 는 그 키의 깊이, 언더글로우는 판 전체의 최대. */
static uint8_t heDepthOfLed(uint8_t i)
{
    if (i < KEYS_LAYOUT_LED_CNT) {
        return heDepth8(keys_led[i][0], keys_led[i][1]);
    }
    return heDepthMax();
}

#endif


/*
 * 진단용 — 효과가 실제로 보는 값을 그대로 내보낸다.
 *
 * 같은 함수를 다른 번역 단위에서 부르면 다른 값이 나올 수 있다(헤더·매크로가
 * 다르게 펼쳐지면). 그래서 **효과가 있는 이 자리에서** 재야 한다.
 */
#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS
void heDbgLed(uint8_t led, uint8_t *p_row, uint8_t *p_col,
              uint16_t *p_travel, uint16_t *p_depth, uint8_t *p_d8)
{
    uint8_t r = (led < KEYS_LAYOUT_LED_CNT) ? keys_led[led][0] : 0xFF;
    uint8_t c = (led < KEYS_LAYOUT_LED_CNT) ? keys_led[led][1] : 0xFF;

    *p_row = r; *p_col = c;
    *p_travel = (r == 0xFF) ? 0 : keysGetTravelUm(r, c);
    *p_depth  = (r == 0xFF) ? 0 : keysGetDepthUm(r, c);
    *p_d8     = heDepthOfLed(led);
}
#endif


/* ── 깊이 밝기 ─────────────────────────────────────────────────────────── */
/*
 * 누른 깊이만큼 밝아진다. 반쯤 누르면 반쯤 밝다.
 *
 * 입력지점을 눈으로 익히는 데 쓸 만하다 — 어느 밝기에서 글자가 나가는지 보인다.
 */
#ifdef ENABLE_RGB_MATRIX_HE_DEPTH
RGB_MATRIX_EFFECT(HE_DEPTH)
#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

bool HE_DEPTH(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    HSV hsv = rgb_matrix_config.hsv;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        HSV h = hsv;
        h.v   = scale8(heDepthOfLed(i), hsv.v);
        RGB rgb = rgb_matrix_hsv_to_rgb(h);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

#    endif
#endif


/* ── 깊이 색상 ─────────────────────────────────────────────────────────── */
/*
 * 밝기는 그대로 두고 **색이** 깊이를 따라 돈다. 얕으면 설정한 색, 깊이 누를수록
 * 색상환을 따라 이동한다. 밝기가 안 변하므로 전류 합이 거의 일정하다 —
 * 리미터가 걸릴 일이 없다.
 *
 * ★ 키마다가 아니라 **판 전체가 같이** 돈다.
 *
 *   처음에는 눌린 키의 LED 만 돌게 했다. 값은 정상이었는데(깊이 254 까지 들어온다)
 *   **정작 그 키는 손가락이 덮고 있어** 보이지 않았다. 밝기 모드는 꺼짐 -> 켜짐이라
 *   캡 가장자리로도 보이지만, 색만 바뀌는 것은 가려지면 끝이다.
 *
 *   그래서 판에서 가장 깊이 눌린 값 하나로 전체를 물들인다. 손가락 밖에서 보이고,
 *   64셀 훑기가 프레임당 한 번뿐이라 오히려 싸다.
 */
#ifdef ENABLE_RGB_MATRIX_HE_DEPTH_HUE
RGB_MATRIX_EFFECT(HE_DEPTH_HUE)
#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

bool HE_DEPTH_HUE(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    HSV     hsv   = rgb_matrix_config.hsv;
    uint8_t depth = heDepthMax();          /* 판 전체가 같은 값을 쓴다 */

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        HSV h = hsv;
        h.h   = (uint8_t)(hsv.h + (depth >> 1));   /* 최대 반 바퀴 */
        RGB rgb = rgb_matrix_hsv_to_rgb(h);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

#    endif
#endif


/* ── 깊이 파문 ─────────────────────────────────────────────────────────── */
/*
 * 누른 키를 중심으로 빛이 퍼진다. **깊이 누를수록 넓어진다.**
 *
 * 시간이 아니라 깊이가 반지름이라 손가락을 멈추면 파문도 멈춘다. 떼면 오므라든다.
 * 여러 키를 누르면 각 파문 중 가장 밝은 것을 취한다.
 */
#ifdef ENABLE_RGB_MATRIX_HE_DEPTH_RIPPLE
RGB_MATRIX_EFFECT(HE_DEPTH_RIPPLE)
#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#define HE_RIPPLE_REACH  90   /* 가장 깊이 눌렀을 때 반지름 (QMK 좌표, 판 폭이 224) */

bool HE_DEPTH_RIPPLE(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    /*
     * 눌린 키를 먼저 모은다. LED 마다 매트릭스를 훑으면 64 x 83 이 되지만,
     * 한 번 모아 두면 슬라이스당 (눌린 키 수)번만 돈다.
     */
    struct { uint8_t x, y, d; } src[8];
    uint8_t n = 0;

    for (uint8_t r = 0; r < MATRIX_ROWS && n < 8; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS && n < 8; c++) {
            uint8_t d   = heDepth8(r, c);
            uint8_t led = g_led_config.matrix_co[r][c];

            if (d < 16 || led == NO_LED) continue;      /* 잡음·미배치는 건너뛴다 */
            src[n].x = g_led_config.point[led].x;
            src[n].y = g_led_config.point[led].y;
            src[n].d = d;
            n++;
        }
    }

    HSV hsv = rgb_matrix_config.hsv;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();

        uint8_t best = 0;
        for (uint8_t k = 0; k < n; k++) {
            uint8_t  dx  = abs8(g_led_config.point[i].x - src[k].x);
            uint8_t  dy  = abs8(g_led_config.point[i].y - src[k].y);
            /* 거리 근사 — 큰 쪽 + 작은 쪽 절반. sqrt 없이 충분히 둥글다 */
            uint16_t dist = (dx > dy) ? (dx + (dy >> 1)) : (dy + (dx >> 1));
            uint16_t reach = ((uint16_t)src[k].d * HE_RIPPLE_REACH) >> 8;
            uint8_t  v;

            if (dist >= reach) continue;
            v = (uint8_t)(255 - (dist * 255U) / (reach ? reach : 1));
            if (v > best) best = v;
        }

        HSV h = hsv;
        h.v   = scale8(best, hsv.v);
        RGB rgb = rgb_matrix_hsv_to_rgb(h);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

#    endif
#endif
