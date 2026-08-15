/*
 * keys_layout.h  —  자동 생성. 직접 고치지 말 것.
 *
 *   생성 : tools/gen_keymap.py
 *   원본 : json/wish60-he-kle.json
 *
 * row = MUX 스텝, col = ADC 채널. 매트릭스가 곧 하드웨어다.
 */
#ifndef KEYS_LAYOUT_H_
#define KEYS_LAYOUT_H_

#define KEYS_LAYOUT_NAME      "WISH60-HE"
#define KEYS_LAYOUT_ROWS      8
#define KEYS_LAYOUT_COLS      8
#define KEYS_LAYOUT_KEY_CNT   60

/* 실재하는 셀만 1. 레이아웃에 없는 자리는 스위치가 없다. */
static const uint16_t keys_present[KEYS_LAYOUT_ROWS] =
{
  0x00FF,   /* s0  ######## */
  0x00FF,   /* s1  ######## */
  0x00FF,   /* s2  ######## */
  0x00FF,   /* s3  ######## */
  0x00FF,   /* s4  ######## */
  0x00FF,   /* s5  ######## */
  0x00FF,   /* s6  ######## */
  0x000F,   /* s7  ####.... */
};

/* 물리 배치 순서(좌상단부터 가로) -> (row, col) */
static const uint8_t keys_pos[KEYS_LAYOUT_KEY_CNT][2] =
{
  {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7},
  {1,0}, {1,1}, {1,2}, {1,3}, {1,4}, {1,5}, {1,6}, {1,7},
  {2,0}, {2,1}, {2,2}, {2,3}, {2,4}, {2,5}, {2,6}, {2,7},
  {3,0}, {3,1}, {3,2}, {3,3}, {3,4}, {3,5}, {3,6}, {3,7},
  {4,0}, {4,1}, {4,2}, {4,3}, {4,4}, {4,5}, {4,6}, {4,7},
  {5,0}, {5,1}, {5,2}, {5,3}, {5,4}, {5,5}, {5,6}, {5,7},
  {6,0}, {6,1}, {6,2}, {6,3}, {6,4}, {6,5}, {6,6}, {6,7},
  {7,0}, {7,1}, {7,2}, {7,3},
};

#endif
