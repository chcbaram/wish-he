#!/usr/bin/env python3
# WISH60-HE 문서용 SVG 다이어그램 생성기
# (hpm5361-fw/docs/images/gen_svg.py 의 Svg 헬퍼를 그대로 가져왔다)
import os, html

OUT = "/Users/hancheol/hdd/git/hpm5300evk/firmware/wish60-he/docs/images"
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


print("생성:")
ws2812_dma()
