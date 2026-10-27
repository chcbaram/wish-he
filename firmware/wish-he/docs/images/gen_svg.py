#!/usr/bin/env python3
# WISH60-HE 문서용 SVG 다이어그램 생성기
# Svg 헬퍼 + 다이어그램 생성
import os, html

# 이 스크립트가 있는 폴더에 쓴다 (예전에는 옛 작업장 경로를 박아 두어 여기서 돌리면
# 엉뚱한 곳에 생겼다)
OUT = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)

MONO = "ui-monospace,'SF Mono',Menlo,Consolas,'Liberation Mono',monospace"
SANS = "-apple-system,BlinkMacSystemFont,'Apple SD Gothic Neo','Malgun Gothic','Noto Sans KR',sans-serif"

BG      = "#ffffff"
INK     = "#1f2328"
MUTED   = "#6a737d"
LINE    = "#8c959f"

# 팔레트
ROM_F, ROM_S     = "#dbeafe", "#3b82f6"   # BootROM / 부팅 단계
FLASH_F, FLASH_S = "#fef3c7", "#d97706"   # 플래시
RAM_F, RAM_S     = "#dcfce7", "#16a34a"   # RAM
PER_F, PER_S     = "#f3f4f6", "#9ca3af"   # 주변장치 / 기타
WARN_F, WARN_S   = "#fee2e2", "#dc2626"   # 예약 / 주의
GAP_F, GAP_S     = "#fafbfc", "#d0d7de"   # 빈 영역
APP_F, APP_S     = "#ede9fe", "#7c3aed"   # 애플리케이션


class Svg:
    def __init__(self, w, h, title=""):
        self.w, self.h, self.title = w, h, title
        self.p = []

    def rect(self, x, y, w, h, fill=BG, stroke=LINE, rx=5, sw=1.2, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.p.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
                      f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def text(self, x, y, s, size=13, fill=INK, anchor="start", mono=False,
             weight="400", family=None, opacity=None):
        fam = family or (MONO if mono else SANS)
        op = f' opacity="{opacity}"' if opacity else ""
        self.p.append(f'<text xml:space="preserve" x="{x}" y="{y}" font-family="{fam}" '
                      f'font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
                      f'font-weight="{weight}"{op}>{html.escape(s)}</text>')

    def line(self, x1, y1, x2, y2, stroke=LINE, sw=1.2, dash=None, marker=False):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        m = ' marker-end="url(#a)"' if marker else ""
        self.p.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" '
                      f'stroke-width="{sw}"{d}{m}/>')

    def path(self, d, stroke=LINE, sw=1.2, fill="none", dash=None, marker=False):
        ds = f' stroke-dasharray="{dash}"' if dash else ""
        m = ' marker-end="url(#a)"' if marker else ""
        self.p.append(f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{ds}{m}/>')

    def diamond(self, cx, cy, rw, rh, fill=PER_F, stroke=PER_S, sw=1.2):
        self.p.append(f'<polygon points="{cx},{cy-rh} {cx+rw},{cy} {cx},{cy+rh} {cx-rw},{cy}" '
                      f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def save(self, name):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" role="img" aria-label="{html.escape(self.title)}">\n'
                f'<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
                f'markerHeight="7" orient="auto-start-reverse">'
                f'<path d="M0,0 L10,5 L0,10 z" fill="{LINE}"/></marker></defs>\n'
                f'<rect width="{self.w}" height="{self.h}" fill="{BG}"/>\n')
        with open(os.path.join(OUT, name), "w") as f:
            f.write(head + "\n".join(self.p) + "\n</svg>\n")
        print("  ", name)


def boxlines(s, x, y, w, lines, dy=19, pad=14, size=12.5, mono=True, fill=INK):
    for i, ln in enumerate(lines):
        s.text(x + pad, y + pad + 12 + i * dy, ln, size=size, mono=mono, fill=fill)



# ────────────────────────────────────────── WS2812 데이터 경로 (SPI1 + HDMA)
def ws2812_dma():
    W, H = 1120, 560
    s = Svg(W, H, "WS2812 데이터 경로")

    s.text(W/2, 34, "WS2812 는 어떻게 켜지나", size=17, anchor="middle", weight="700")
    s.text(W/2, 56, "CPU 는 색만 정하고 빠진다. 나머지는 DMA 와 SPI 가 알아서 한다.",
           size=12.5, anchor="middle", fill=MUTED)

    # ── 1. 가로 흐름 ─────────────────────────────────────────────────────
    BY, BH, BW, STEP = 86, 84, 170, 212
    xs = [50 + i*STEP for i in range(5)]
    boxes = [
        ("CPU",          "색을 정한다",      "ws2812SetColor()",  APP_F,   APP_S),
        ("frame_buf",    "2052 바이트",      "메모리에 쌓아둔다",  RAM_F,   RAM_S),
        ("HDMA ch2",     "혼자 퍼나른다",    "CPU 개입 없음",     ROM_F,   ROM_S),
        ("SPI1",         "8 MHz",            "비트를 실어 보낸다", FLASH_F, FLASH_S),
        ("WS2812 x83",   "PA29 로 들어간다", "",                  WARN_F,  WARN_S),
    ]
    for x, (t, l1, l2, f, st) in zip(xs, boxes):
        s.rect(x, BY, BW, BH, f, st, rx=8, sw=1.6)
        s.text(x + BW/2, BY + 28, t, size=14, anchor="middle", weight="700")
        s.text(x + BW/2, BY + 50, l1, size=11.5, anchor="middle")
        if l2:
            s.text(x + BW/2, BY + 68, l2, size=10.5, anchor="middle", fill=MUTED)

    for i in range(4):
        ax = xs[i] + BW
        s.line(ax + 8, BY + BH/2, ax + STEP - BW - 8, BY + BH/2, LINE, 2.2, marker=True)

    # LED 알갱이
    for i in range(9):
        s.p.append(f'<circle cx="{xs[4]+22+i*16}" cy="{BY+BH+20}" r="6" '
                   f'fill="{WARN_F}" stroke="{WARN_S}" stroke-width="1.4"/>')
    s.text(xs[4]+BW-6, BY+BH+24, "...", size=12, anchor="end", fill=MUTED)

    # ── 2. 비트 인코딩 (실제 파형) ────────────────────────────────────────
    s.rect(40, 210, W-80, 190, GAP_F, GAP_S, rx=8)
    s.text(64, 238, "SPI 1 바이트 = WS2812 1 비트", size=14, weight="700")
    s.text(64, 258, "8비트 x 125ns = 1.0us. 앞쪽을 몇 칸 높게 두느냐로 0 과 1 을 만든다.",
           size=11.5, fill=MUTED)

    def wave(x0, y_lo, byte_s, high_n, color, meaning, us):
        CW, HI = 44, 34                     # 셀 폭, 진폭
        y_hi = y_lo - HI
        for i in range(8):                  # 셀 격자
            s.rect(x0 + i*CW, y_hi, CW, HI, BG, "#e6e8eb", rx=0, sw=1)
        xh = x0 + high_n*CW
        s.path(f"M {x0},{y_lo} L {x0},{y_hi} L {xh},{y_hi} L {xh},{y_lo} L {x0+8*CW},{y_lo}",
               color, 2.6)
        s.text(x0 - 12, y_hi + 6, byte_s, size=12.5, anchor="end", mono=True,
               fill=color, weight="700")
        s.text(x0 + high_n*CW/2, y_hi - 8, f"{high_n*125}ns", size=10.5,
               anchor="middle", fill=color, weight="600")
        s.text(x0 + 8*CW + 14, y_lo - 12, meaning, size=12.5, weight="700")
        s.text(x0 + 8*CW + 14, y_lo + 4, us, size=10.5, fill=MUTED)

    wave(150, 316, "0xE0", 3, ROM_S,   "= 0",  "규격 T0H 0.4us")
    wave(620, 316, "0xFC", 6, FLASH_S, "= 1",  "규격 T1H 0.8us")
    def dim(x0, y, label):
        x1 = x0 + 8*44
        s.line(x0, y, x1, y, MUTED, 1.0)
        for xx in (x0, x1):
            s.line(xx, y - 4, xx, y + 4, MUTED, 1.0)
        s.rect(x0 + 8*44/2 - 26, y - 8, 52, 16, BG, BG, rx=0, sw=0)
        s.text(x0 + 8*44/2, y + 4, label, size=10.5, anchor="middle", fill=MUTED)

    dim(150, 344, "1.0us")
    dim(620, 344, "1.0us")

    # ── 3. 타임라인 ──────────────────────────────────────────────────────
    s.rect(40, 418, W-80, 118, "#f0fdf4", RAM_S, rx=8)
    s.text(64, 446, "CPU 는 기다리지 않는다", size=14, weight="700")

    tx, tw, ty = 150, 830, 462
    s.rect(tx, ty, 16, 26, APP_F, APP_S, rx=3, sw=1.6)
    s.rect(tx + 16, ty, tw - 16, 26, ROM_F, ROM_S, rx=3, sw=1.6)
    s.text(tx + 16 + (tw-16)/2, ty + 18, "DMA 가 전송   2052 x 1.0us = 2.05 ms",
           size=12, anchor="middle", weight="600")
    s.text(tx - 12, ty + 18, "호출", size=11, anchor="end", fill=APP_S, weight="700")
    s.text(tx, ty + 48, "ws2812Refresh() 는 수 us 만에 반환한다. 이 2ms 동안 CPU 는 키 스캔을 돈다.",
           size=11.5, fill=MUTED)

    s.save("ws2812-dma.svg")



# ────────────────────────────────────────── 키 신호 처리 경로 (ADC -> 판정)
def keys_pipeline():
    W, H = 1180, 636
    s = Svg(W, H, "키 신호 처리 경로")

    s.text(40, 38, "키 하나가 눌리기까지 — 신호가 거치는 단계", size=17, weight="700")
    s.text(40, 60, "숫자는 전부 실측값. 괄호 안은 12비트로 내리기 전 원시 16비트.",
           size=12, fill=MUTED)

    # ── 파이프라인 5단
    bx, by, bw, bh, gap = 40, 88, 200, 132, 32
    stages = [
        ("① ADC 시퀀스",      PER_F,  PER_S, [
            "ADC0 4채널", "ADC1 4채널", "동시 변환", "", "16비트 원시값"]),
        ("② 12비트로",        ROM_F,  ROM_S, [
            "raw >> 4", "", "하위 4비트는", "노이즈뿐이라", "버려도 손실 없음"]),
        ("③ 데드밴드 ±7",     RAM_F,  RAM_S, [
            "밴드 밖 → 즉시", "밴드 안 → 무시", "", "지연 0", "노이즈 p-p 12→5"]),
        ("④ 기준값 추적",     FLASH_F, FLASH_S, [
            "무압 = 물리적 극단", "", "큰 변화 → 즉시", "잔파도 → 512ms당 1", "누른 채 부팅 복구"]),
        ("⑤ 판정",            APP_F,  APP_S, [
            "d = base - raw", "", "d > 250 → 눌림", "d < 156 → 해제", "히스테리시스"]),
    ]
    for i, (title, f, st, lines) in enumerate(stages):
        x = bx + i * (bw + gap)
        s.rect(x, by, bw, bh, f, st, rx=6, sw=1.6)
        s.text(x + bw/2, by + 24, title, size=13.5, anchor="middle", weight="700")
        s.line(x + 12, by + 34, x + bw - 12, by + 34, stroke=st, sw=1)
        for j, ln in enumerate(lines):
            if ln:
                s.text(x + 14, by + 54 + j*17, ln, size=11.5, mono=True)
        if i < len(stages) - 1:
            s.line(x + bw + 5, by + bh/2, x + bw + gap - 5, by + bh/2, sw=1.8, marker=True)

    # 단계별 값의 크기
    vy = by + bh + 34
    s.text(40, vy, "각 단계에서 값이 얼마나 되나", size=13, weight="700")
    cols = ["", "무압 기준값", "풀 스트로크", "노이즈 p-p", "누름 임계"]
    rows = [
        ("16비트 (①)",  "40,000 ~ 46,000", "13,400", "200", "4,000"),
        ("12비트 (② ~)", "2,490 ~ 2,880",  "838",    "12",  "250"),
    ]
    cw = [130, 200, 150, 130, 130]
    tx0, ty0 = 40, vy + 14
    cx = tx0
    for i, c in enumerate(cols):
        s.text(cx + 8, ty0 + 16, c, size=11.5, fill=MUTED, weight="700")
        cx += cw[i]
    for r, row in enumerate(rows):
        y = ty0 + 24 + r * 26
        s.rect(tx0, y, sum(cw), 24, "#fafbfc" if r % 2 == 0 else BG, "#e5e7eb", rx=3, sw=0.8)
        cx = tx0
        for i, v in enumerate(row):
            s.text(cx + 8, y + 17, v, size=11.5, mono=(i > 0),
                   weight="700" if i == 0 else "400")
            cx += cw[i]

    # ── 실제 파형
    gx, gy, gw, gh = 40, 402, 700, 196
    s.text(gx, gy - 14, "키 하나를 눌렀다 뗄 때 (실측, 12비트)", size=13, weight="700")
    s.rect(gx, gy, gw, gh, BG, "#e5e7eb", rx=4, sw=1)

    base_y  = gy + 22
    floor_y = gy + gh - 22

    def lvl(d):                       # 편차 d(0~838) -> y
        return base_y + (floor_y - base_y) * d / 838.0

    press_y, rel_y = lvl(250), lvl(156)

    s.line(gx, base_y, gx + gw, base_y, stroke=FLASH_S, sw=1.6)
    s.text(gx + gw - 6, base_y - 7, "기준값 2524  (무압)", size=11, anchor="end",
           fill=FLASH_S, weight="700", mono=True)
    s.text(gx + 10, base_y - 7, "손 뗀 상태", size=11, fill=FLASH_S, mono=True)

    s.line(gx, press_y, gx + gw, press_y, stroke=APP_S, sw=1.3, dash="5 4")
    s.text(gx + 10, press_y - 6, "누름 250  여기서 눌림 판정", size=11,
           fill=APP_S, weight="700", mono=True)
    s.line(gx, rel_y, gx + gw, rel_y, stroke="#0ea5e9", sw=1.3, dash="5 4")
    s.text(gx + gw - 6, rel_y + 15, "해제 156  (히스테리시스)", size=11, anchor="end",
           fill="#0ea5e9", weight="700", mono=True)

    # 실측 샘플 (keys map 출력)
    seq = [0, 86, 429, 832, 247, 0]
    n   = len(seq)
    pts = [(gx + 40 + i * (gw - 120) / (n - 1), lvl(d)) for i, d in enumerate(seq)]
    s.path("M " + " L ".join(f"{x:.1f},{y:.1f}" for x, y in pts), stroke=RAM_S, sw=2.4)
    for (x, y), d in zip(pts, seq):
        s.p.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{RAM_S}"/>')
        if d:
            s.text(x, y + 17, f"-{d}", size=11, anchor="middle", fill=RAM_S,
                   weight="700", mono=True)

    s.text(gx + 10, floor_y + 14, "바닥 = 스트로크 838", size=10.5, fill=MUTED, mono=True)

    # 오른쪽 설명
    nx = gx + gw + 26
    s.rect(nx, gy, W - nx - 40, gh, "#fafbfc", "#e5e7eb", rx=5, sw=1)
    boxlines(s, nx, gy, W - nx - 40, [
        "샘플 간격이 넓은 건",
        "필터 지연이 없기 때문이다.",
        "",
        "IIR 을 쓰던 때는 같은 속도로",
        "눌러도 -652 까지밖에 못 갔다.",
        "필터가 못 따라온 것이다.",
        "",
        "데드밴드는 밴드보다 큰 변화를",
        "같은 샘플에서 반영한다.",
    ], dy=16, size=11.5, mono=False)

    s.save("keys-pipeline.svg")


# ────────────────────────────────────────── wish40-he 플래시 배치
def wish40_flash_map():
    W, H = 1120, 640
    s = Svg(W, H, "wish40-he 플래시 배치")
    s.text(W/2, 34, "wish40-he 플래시 1MB — 누가 어디를 쓰나", size=17, anchor="middle", weight="700")
    s.text(W/2, 56, "계약은 wish60-he 부트로더와 같다. 앱은 이 배치를 이미 안다. (칸 높이는 비율이 아니다)",
           size=12.5, anchor="middle", fill=MUTED)

    X, BW = 150, 480
    rows = [
        # (키, 주소, 높이, 이름, 설명, 채움, 테두리, 점선)
        ("gap0",  "0x80000000", 30, "비움", "", GAP_F, GAP_S, "4 3"),
        ("norcfg","0x80000400", 30, "nor_cfg_option  16 B", "ROM 이 XPI 를 이걸로 잡는다", ROM_F, ROM_S, None),
        ("hdr",   "0x80001000", 30, "부트 헤더", "ROM 이 찾는 이미지 표지", ROM_F, ROM_S, None),
        ("boot",  "0x80003000", 92, "부트로더", "34 KB 사용 / 116 KB 한도 — 넘으면 링커가 멈춘다", FLASH_F, FLASH_S, None),
        ("flag",  "0x8001D000", 34, "업데이트 플래그", "4 KB 섹터 — 0xFFFFFFFF 가 아니면 남는다", WARN_F, WARN_S, None),
        ("app",   "0x80020000", 120, "앱  384 KB", "첫 4 바이트 \"HPM\\n\", 진입점 0x80020004", APP_F, APP_S, None),
        ("gap1",  "0x80080000", 56, "안 쓴다", "wish60 의 데이터 자리 — 규칙으로 비워 둔다", GAP_F, GAP_S, "4 3"),
        ("data",  "0x800C0000", 100, "앱 데이터", "보정 · QMK EEPROM · 설정 · 스위치 정의", RAM_F, RAM_S, None),
    ]
    y = 86
    ys = {}
    for key, addr, h, name, desc, f, st, dash in rows:
        s.rect(X, y, BW, h, f, st, rx=4, sw=1.4, dash=dash)
        s.text(X - 12, y + 15, addr, size=11.5, anchor="end", mono=True, fill=MUTED)
        if h >= 50:
            s.text(X + 14, y + h/2 - 4, name, size=13, weight="700")
            s.text(X + 14, y + h/2 + 14, desc, size=11.5, fill=MUTED)
        else:
            s.text(X + 14, y + h/2 + 4, name, size=13, weight="700")
            if desc:
                s.text(X + BW - 14, y + h/2 + 4, desc, size=11.5, anchor="end", fill=MUTED)
        ys[key] = (y, y + h)
        y += h + 4
    s.text(X - 12, y + 4, "0x80100000", size=11.5, anchor="end", mono=True, fill=MUTED)

    def bracket(lane, y0, y1, col):
        """칸 오른쪽에 범위 괄호를 긋는다. 가운데 y 를 돌려준다."""
        s.path(f"M{lane - 8} {y0 + 2} L{lane} {y0 + 2} L{lane} {y1 - 2} L{lane - 8} {y1 - 2}",
               stroke=col, sw=2.2)
        return (y0 + y1) / 2

    RX, RW = 770, 310

    def writer(cy, title, l1, l2, col):
        by = cy - 41
        s.rect(RX, by, RW, 82, "#ffffff", col, rx=8, sw=1.6)
        s.text(RX + 14, by + 24, title, size=13, weight="700")
        s.text(RX + 14, by + 46, l1, size=11.5)
        s.text(RX + 14, by + 66, l2, size=11.5, fill=MUTED)

    L1, L2, L3 = X + BW + 22, X + BW + 46, X + BW + 70

    # JTAG — 0x00000 ~ 0x20000 (부트로더 + 플래그까지)
    cy = bracket(L1, ys["gap0"][0], ys["flag"][1], FLASH_S)
    s.line(L1, cy, RX, cy, stroke=FLASH_S, sw=1.3, marker=True)
    writer(cy, "JTAG — boot-wish40-flash 태스크", "0x00000 ~ 0x20000 만 지우고 쓴다",
           "새 보드에 한 번. 앱과 보정값은 남는다", FLASH_S)

    # 부트로더 — 앱 자리, 끝나면 플래그 지움
    cy = bracket(L2, ys["app"][0], ys["app"][1], APP_S)
    fy = bracket(L2, ys["flag"][0], ys["flag"][1], APP_S)
    s.line(L2, cy, RX, cy, stroke=APP_S, sw=1.3, marker=True)
    s.line(L2, fy, L2 + 10, fy, stroke=APP_S, sw=1.3)
    s.path(f"M{L2 + 10} {fy} L{RX - 40} {fy} L{RX - 40} {cy - 30} L{RX} {cy - 30}",
           stroke=APP_S, sw=1.1, dash="4 3", marker=True)
    writer(cy, "부트로더 — USB 굽기", "0x20000 ~ 0x80000 만 쓴다 (못 박음)",
           "끝나면 플래그 섹터를 지운다", APP_S)

    # 앱 — 플래그에 쓰고, 자기 데이터는 0xC0000 위
    cy = bracket(L3, ys["data"][0], ys["data"][1], RAM_S)
    fy = bracket(L3, ys["flag"][0], ys["flag"][1], RAM_S)
    s.line(L3, cy, RX, cy, stroke=RAM_S, sw=1.3, marker=True)
    s.path(f"M{L3} {fy} L{RX - 20} {fy} L{RX - 20} {cy - 30} L{RX} {cy - 30}",
           stroke=RAM_S, sw=1.1, dash="4 3", marker=True)
    writer(cy, "앱", "플래그에 0x0000FFFF 를 쓰고 리셋 → 부트로더",
           "자기 데이터는 0xC0000 위에만 쓴다", RAM_S)

    s.rect(RX, 86, RW, 66, "#fafbfc", "#e5e7eb", rx=6, sw=1)
    boxlines(s, RX, 84, RW, [
        "부트로더는 0x20000 앞을 지울 길이 없다.",
        "굽다 망가져도 부트로더는 산다. 그 대신",
        "USB 로는 부트로더 자신을 못 고친다.",
    ], dy=17, size=11.5, mono=False)

    s.save("wish40-flash-map.svg")


# ────────────────────────────────────────── wish40-he 부팅 판정
def wish40_boot_flow():
    W, H = 1120, 600
    s = Svg(W, H, "wish40-he 부팅 판정")
    s.text(W/2, 34, "부팅마다 고른다 — 앱으로 가나, 남나", size=17, anchor="middle", weight="700")
    s.text(W/2, 56, "판정은 ROM 이 넘겨 준 24 MHz 그대로 한다. PLL 과 USB 는 남을 때만 켠다.",
           size=12.5, anchor="middle", fill=MUTED)

    def box(x, y, w, h, t, l1, l2, f, st):
        s.rect(x, y, w, h, f, st, rx=8, sw=1.6)
        s.text(x + w/2, y + 26, t, size=13.5, anchor="middle", weight="700")
        if l1: s.text(x + w/2, y + 47, l1, size=11.5, anchor="middle")
        if l2: s.text(x + w/2, y + 66, l2, size=11.5, anchor="middle", fill=MUTED)

    box(40, 96, 180, 78, "전원 · 리셋", "ROM 이 부트 헤더를 읽고", "0x80003000 으로", ROM_F, ROM_S)
    s.line(220, 135, 268, 135, marker=True)
    box(270, 96, 220, 78, "부트로더 판정", "GPIO 와 ADC 원샷만 쓴다", "", FLASH_F, FLASH_S)

    # 조건 목록
    s.rect(270, 196, 220, 158, "#fafbfc", "#e5e7eb", rx=6, sw=1)
    boxlines(s, 270, 196, 220, [
        "① 0x80020000 = \"HPM\\n\"",
        "② 플래그 = 0xFFFFFFFF",
        "③ BOOT 버튼 안 눌림",
        "④ Esc 안 눌림",
        "",
        "어긋난 것이 boot_why 비트",
        "(1 · 2 · 4 · 8) 로 남는다",
    ], dy=18, size=11.5, mono=False)
    s.line(380, 174, 380, 196, stroke="#d0d7de")

    s.line(490, 135, 556, 135, marker=True)
    s.diamond(620, 135, 62, 44, PER_F, PER_S)
    s.text(620, 131, "넷 다", size=12.5, anchor="middle", weight="700")
    s.text(620, 148, "참?", size=12.5, anchor="middle", weight="700")

    # 예 → 앱
    s.line(682, 135, 760, 135, marker=True)
    s.text(720, 125, "예", size=12, anchor="middle", fill=MUTED)
    box(762, 92, 318, 86, "앱으로 점프  0x80020004", "PLL · USB 를 안 건드리고 넘긴다",
        "앱이 24 MHz 에서 제 클럭을 처음부터 잡는다", APP_F, APP_S)

    # 아니오 → 업데이트 모드
    s.line(620, 179, 620, 236, marker=True)
    s.text(632, 212, "아니오", size=12, fill=MUTED)
    box(500, 238, 240, 96, "업데이트 모드", "400 MHz · USB 0483:5308", "LED 100 ms 마다 반전", WARN_F, WARN_S)

    s.line(740, 286, 800, 286, marker=True)
    box(802, 238, 278, 96, "굽기  0x80 ~ 0x83", "wish60 부트로더와 같은 프로토콜",
        "iap_update.py · 웹앱 그대로", WARN_F, WARN_S)

    s.line(941, 334, 941, 380, marker=True)
    box(802, 382, 278, 96, "종료 0x83", "매직 페이지를 쓰고 플래그를 지운다",
        "50 ms 뒤 리셋 — 점프하지 않는다", WARN_F, WARN_S)

    # 리셋 → 처음으로
    s.path("M802 430 L 130 430 L 130 176", stroke=LINE, sw=1.3, dash="6 4", marker=True)
    s.text(470, 448, "다음 부팅이 판정부터 다시 한다 — 앱은 늘 깨끗한 상태에서 출발한다",
           size=12, anchor="middle", fill=MUTED)

    s.rect(40, 488, 1040, 90, "#fafbfc", "#e5e7eb", rx=6, sw=1)
    boxlines(s, 40, 488, 1040, [
        "★ Esc 는 극성을 가정하지 않는다 — |값 − VCC/2| > 8192 일 때 눌림. 스위치를 바꾸면 방향이 뒤집힌다.",
        "★ 센서가 레일에 붙은 값(0.2 V 밑, VCC − 0.2 V 위)은 눌림으로 치지 않는다 — Esc 센서 하나가 죽으면 영영 앱으로 못 간다.",
        "★ 조립하면 BOOT 버튼에 손이 안 닿는다. 앱이 깨졌을 때 남는 길이 Esc 다.",
    ], dy=22, size=12, mono=False)

    s.save("wish40-boot-flow.svg")


# ────────────────────────────────────────── wish40-he 굽기 순서
def wish40_update_seq():
    W, H = 1120, 560
    s = Svg(W, H, "wish40-he 굽기 순서")
    s.text(W/2, 34, "매직 페이지를 맨 마지막에 쓴다", size=17, anchor="middle", weight="700")
    s.text(W/2, 56, "어디서 끊겨도 0x80020000 에 \"HPM\\n\" 이 없으니 다음 부팅은 부트로더에 남는다",
           size=12.5, anchor="middle", fill=MUTED)

    SX, SW, SH, GAPX = 200, 92, 46, 8
    labels = ["0x20000", "0x21000", "0x22000", "0x23000", "…", "끝"]

    def strip(y, states, title, note1, note2):
        s.text(40, y + 20, title, size=13.5, weight="700")
        for i, st in enumerate(states):
            x = SX + i * (SW + GAPX)
            if st == "old":
                s.rect(x, y, SW, SH, PER_F, PER_S, rx=4)
                s.text(x + SW/2, y + 28, "옛 앱", size=11.5, anchor="middle", fill=MUTED)
            elif st == "erased":
                s.rect(x, y, SW, SH, GAP_F, GAP_S, rx=4, dash="4 3")
                s.text(x + SW/2, y + 28, "지움", size=11.5, anchor="middle", fill=MUTED)
            elif st == "ram":
                s.rect(x, y, SW, SH, RAM_F, RAM_S, rx=4, dash="5 3")
                s.text(x + SW/2, y + 22, "RAM 에", size=11.5, anchor="middle")
                s.text(x + SW/2, y + 37, "들고 있다", size=11.5, anchor="middle")
            elif st == "new":
                s.rect(x, y, SW, SH, APP_F, APP_S, rx=4)
                s.text(x + SW/2, y + 28, "새 앱", size=11.5, anchor="middle")
            elif st == "magic":
                s.rect(x, y, SW, SH, APP_F, APP_S, rx=4, sw=2.4)
                s.text(x + SW/2, y + 22, "새 앱", size=11.5, anchor="middle")
                s.text(x + SW/2, y + 37, "+ 매직", size=11.5, anchor="middle", weight="700")
        nx = SX + 6 * (SW + GAPX) + 10
        s.text(nx, y + 18, note1, size=12)
        s.text(nx, y + 37, note2, size=11.5, fill=MUTED)

    for i, l in enumerate(labels):
        s.text(SX + i * (SW + GAPX) + SW/2, 92, l, size=11, anchor="middle", mono=True, fill=MUTED)

    strip(102, ["old"] * 6, "굽기 전", "앱이 돈다", "매직이 있다 → 넘어간다")
    strip(178, ["erased", "old", "old", "old", "old", "old"], "0x81 시작",
          "첫 섹터부터 지운다", "여기서 끊겨도 매직이 없다")
    strip(254, ["ram", "new", "new", "erased", "old", "old"], "0x80 · 0x82",
          "받는 대로 지우고 · 쓰고 · 되읽는다", "첫 페이지만 RAM 에 둔다")
    strip(330, ["ram", "new", "new", "new", "new", "new"], "마지막 0x82",
          "나머지가 다 들어갔다", "아직 매직이 없다")
    strip(406, ["magic", "new", "new", "new", "new", "new"], "0x83 종료",
          "첫 페이지를 쓴다 → 매직이 생긴다", "플래그를 지우고 50 ms 뒤 리셋")

    s.rect(40, 476, 1040, 66, "#fafbfc", "#e5e7eb", rx=6, sw=1)
    boxlines(s, 40, 476, 1040, [
        "★ 시험: 세 페이지만 쓰고 끊은 뒤 플래그까지 지워 \"앱으로 가라\" 쪽 조건을 최대한 만들었다 — 매직이 없어 부트로더에 남았고, 다시 구워 복구했다.",
        "★ 쓰는 범위는 0x20000 ~ 0x80000 으로 못 박았다. 호스트가 주소를 줄 수 없어 부트로더 자신도 앱 데이터(0xC0000~)도 덮을 길이 없다.",
    ], dy=22, size=12, mono=False)

    s.save("wish40-update-seq.svg")


print("생성:")
ws2812_dma()
keys_pipeline()
wish40_flash_map()
wish40_boot_flow()
wish40_update_seq()
