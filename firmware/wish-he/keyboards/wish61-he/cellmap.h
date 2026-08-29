/*
 * cellmap.h — wish61-he 의 (스캔 스텝, ADC 채널) <-> 키 번호 표
 *
 * ★ 좌표는 **keys.c 의 스캔 루프 인덱스**다. MUX 주소가 아니다.
 *   keys.c 는 크로스토크를 줄이려고 그레이 코드로 주소를 쓴다 —
 *   mux_addr[] = {6,7,5,4,0,1,3,2}, 즉 루프 인덱스 i 가 보는 것은 주소 mux_addr[i] 다.
 *
 * ★ 실측 (2026-08-28). 60키는 `keys learn`, Fn 은 원시값으로 따로 잡았다.
 *   이 파일은 기록일 뿐 코드가 읽지 않는다 — 드라이버는 layout.h 를 쓴다.
 *
 * 미연결 3칸 : (2,0) (6,0) (7,0)   = MUX 주소 5, 3, 2
 *
 * ★★ **미연결 칸은 조용하지 않다. 이웃 칸의 값을 지수적으로 끌고 온다.**
 *
 *    Fn 을 누르면 세 칸이 같이 움직였다 —
 *
 *        i5(주소1) -2787      ← 진짜 Fn
 *        i6(주소3) -1625      = 58%
 *        i7(주소2)  -931      = 33%   (앞 칸의 57%)
 *
 *    스캔 순서가 ... 4, 0, 1, 3, 2 라 주소 1 다음이 3, 그 다음이 2 다. 한 스텝마다
 *    0.58배씩 주는 **ADC 샘플홀드 잔류 전하**다. floating 입력은 그 전하를 밀어낼
 *    소스가 없어서 **세틀링을 3000 nop 까지 올려도 값이 그대로였다** (40ns 와 동일).
 *
 *    keys.c 는 keys_present 로 미연결 칸을 판정에서 건너뛰므로(keys.c:2498) 표만
 *    맞으면 문제가 없다. **틀리면 유령 입력이 난다** — 실제로 그렇게 당했다.
 *
 * ★ 겪은 것 : 최초 표는 이 유령 때문에 ch0 를 5개로 세면서 Fn 을 가렸고, 좌표계까지
 *   (주소 vs 루프 인덱스) 어긋나 레이아웃이 통째로 틀렸다. **개수가 딱 맞아떨어지는
 *   것은 검증이 아니다.**
 *
 * 8x8 격자 (step = 스캔 루프 인덱스, ch = ADC 채널)
 *
 *        ch0     ch1     ch2     ch3     ch4     ch5     ch6     ch7     
 * step0  RAlt   B      Z      A      W      3      0      O      
 * step1  RSft   Space  LSft   Caps   Q      2      9      L      
 * step2  ·      J      LCtl   `      Tab    1      I      K      
 * step3  RWin   N      X      S      E      4      -      P      
 * step4  RCtl   .      C      G      Y      6      BSpc   [      
 * step5  Fn     ,      LWin   F      T      7      \      ;      
 * step6  ·      /      V      H      U      5      =      ]      
 * step7  ·      M      LAlt   D      R      8      Enter  '      
 *
 * 키 번호는 배열 순서다 (표준 ANSI 60%, 61키).
 *
 *   0~13   `  1  2  3  4  5  6  7  8  9  0  -  =  Backspace
 *  14~27   Tab  Q  W  E  R  T  Y  U  I  O  P  [  ]  \
 *  28~40   Caps  A  S  D  F  G  H  J  K  L  ;  '  Enter
 *  41~52   LShift  Z  X  C  V  B  N  M  ,  .  /  RShift
 *  53~60   LCtrl  LWin  LAlt  Space  RAlt  RWin  Fn  RCtrl
 */

#ifndef CELLMAP_H_
#define CELLMAP_H_

#define WISH61_KEY_MAX      61
#define WISH61_CELL_NONE    0xFF

/* [스캔 스텝][ADC 채널] -> 키 번호. 0xFF 는 미연결. */
#define WISH61_CELL_MAP  {                                                     \
  {   57,   46,   42,   29,   16,    3,   10,   23 },   \
  {   52,   56,   41,   28,   15,    2,    9,   37 },   \
  { 0xFF,   35,   53,    0,   14,    1,   22,   36 },   \
  {   58,   47,   43,   30,   17,    4,   11,   24 },   \
  {   60,   50,   44,   33,   20,    6,   13,   25 },   \
  {   59,   49,   54,   32,   19,    7,   27,   38 },   \
  { 0xFF,   51,   45,   34,   21,    5,   12,   26 },   \
  { 0xFF,   48,   55,   31,   18,    8,   40,   39 },   \
}

#endif
