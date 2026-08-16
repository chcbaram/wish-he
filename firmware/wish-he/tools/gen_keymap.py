#!/usr/bin/env python3
"""
KLE 레이아웃 하나를 단일 진실 원본으로 삼아 VIA JSON 과 펌웨어 헤더를 만든다.

VIA 앱과 펌웨어가 같은 정보를 필요로 한다 — 어떤 (row, col) 이 실재하는 키이고
물리적으로 어디에 있는가. 두 벌로 관리하면 반드시 어긋나므로 한 곳에서 뽑아 쓴다.

    keyboards/<모델>/layout-kle.json   <- 손으로 편집하는 건 이것 하나
       │   (keyboard-layout-editor.com 의 Raw data 에 그대로 붙여넣어 편집)
       ├──▶  keyboards/<모델>/layout-via.json   VIA 앱용
       └──▶  keyboards/<모델>/layout.h          펌웨어용 present/pos/geo 표

이 보드는 row = MUX 스텝(s), col = ADC 채널(ch) 이다. 매트릭스가 곧 하드웨어라
변환 계층이 없다.

사용법
    python3 tools/gen_keymap.py --apply learn.txt   # 실측값을 KLE 범례에 채운다
    python3 tools/gen_keymap.py                     # KLE -> VIA JSON + 헤더
    python3 tools/gen_keymap.py --show              # 현재 매핑을 표로만 본다
    python3 tools/gen_keymap.py --board <모델>      # 다른 모델을 대상으로

의존성 없음.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOARDS = ROOT / "keyboards"
DEFAULT_BOARD = "wish60-he-7u"

VID, PID = "0x0483", "0x5304"
ROWS, COLS = 8, 8

# --board 로 정해지는 경로들
KLE_PATH = VIA_PATH = HDR_PATH = None


BOARD_DIR = None


def set_board(name):
    """
    모델별 폴더 하나에 편집 원본과 생성물을 모은다.

        keyboards/<모델>/layout-kle.json   <- 손으로 편집하는 건 이것 하나
                        layout-via.json    VIA 앱용   (생성물)
                        layout.h           펌웨어용   (생성물)

    CMake 가 keyboards/${HW_KEYBOARD} 를 인클루드 경로에 넣으므로 펌웨어는
    #include "layout.h" 한 줄이면 된다.
    """
    global KLE_PATH, VIA_PATH, HDR_PATH, KEYMAP_PATH, LAYOUT_PATH, LABELS_PATH, MENUS_PATH
    global BOARD_DIR
    d = BOARDS / name
    if not d.is_dir():
        have = sorted(p.name for p in BOARDS.iterdir() if p.is_dir()) if BOARDS.is_dir() else []
        sys.exit(f"[E_] 그런 보드가 없다: {name}\n     있는 것: {', '.join(have) or '없음'}")
    KLE_PATH    = d / "layout-kle.json"
    VIA_PATH    = d / "layout-via.json"
    HDR_PATH    = d / "layout.h"
    KEYMAP_PATH = d / "keymap.c"        # QMK 키맵   (생성물)
    LAYOUT_PATH = d / "layout_qmk.h"    # QMK LAYOUT 매크로 (생성물)
    LABELS_PATH = d / "labels.json"     # 레이아웃 옵션 이름 (손으로 쓴다, 선택)
    MENUS_PATH  = d / "menus.json"      # VIA 커스텀 메뉴   (손으로 쓴다, 선택)
    BOARD_DIR   = d

ADDR_RE = re.compile(r"^(\d+),(\d+)$")

# ── 기본 키맵 (HID Usage ID, Keyboard/Keypad page)
#
# 물리 배치 읽기 순서대로 늘어놓는다. 레이아웃 옵션 키와 여분 소켓은 0(KC_NO)으로
# 두고 사용자가 채운다 — 무엇을 넣을지는 실제로 어떤 키캡을 끼우느냐에 달렸다.
KC = {
    "NO": 0x00, "A": 0x04, "B": 0x05, "C": 0x06, "D": 0x07, "E": 0x08, "F": 0x09,
    "G": 0x0A, "H": 0x0B, "I": 0x0C, "J": 0x0D, "K": 0x0E, "L": 0x0F, "M": 0x10,
    "N": 0x11, "O": 0x12, "P": 0x13, "Q": 0x14, "R": 0x15, "S": 0x16, "T": 0x17,
    "U": 0x18, "V": 0x19, "W": 0x1A, "X": 0x1B, "Y": 0x1C, "Z": 0x1D,
    "1": 0x1E, "2": 0x1F, "3": 0x20, "4": 0x21, "5": 0x22, "6": 0x23, "7": 0x24,
    "8": 0x25, "9": 0x26, "0": 0x27,
    "ENT": 0x28, "ESC": 0x29, "BSPC": 0x2A, "TAB": 0x2B, "SPC": 0x2C,
    "MINS": 0x2D, "EQL": 0x2E, "LBRC": 0x2F, "RBRC": 0x30, "BSLS": 0x31,
    "SCLN": 0x33, "QUOT": 0x34, "GRV": 0x35, "COMM": 0x36, "DOT": 0x37,
    "SLSH": 0x38, "CAPS": 0x39,
    "LCTL": 0xE0, "LSFT": 0xE1, "LALT": 0xE2, "LGUI": 0xE3,
    "RCTL": 0xE4, "RSFT": 0xE5, "RALT": 0xE6, "RGUI": 0xE7,

    # ── 펌웨어 내부 키코드 (HID Usage 가 아니다)
    #
    # HID 키보드 usage 는 0x00~0xE7 까지만 쓴다. 0xF0 부터는 비어 있으므로 레이어
    # 같은 펌웨어 내부 동작에 쓴다. 리포트에는 실리지 않는다.
    "FN": 0xF0,
}

# 행별 기본 배치. 행의 실제 키 수가 더 많으면 나머지는 NO 로 채운다.
DEFAULT_ROWS = [
    ["ESC", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "MINS", "EQL", "BSPC"],
    ["TAB", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "LBRC", "RBRC", "BSLS"],
    ["CAPS", "A", "S", "D", "F", "G", "H", "J", "K", "L", "SCLN", "QUOT", "ENT"],
    ["LSFT", "Z", "X", "C", "V", "B", "N", "M", "COMM", "DOT", "SLSH", "RSFT", "FN"],
    ["LCTL", "LGUI", "LALT", "SPC", "RALT", "RGUI", "RCTL"],
]
LEARN_RE = re.compile(r"#(\d+)\s+(\d+),(\d+)")


def load_kle():
    if not KLE_PATH.exists():
        sys.exit(f"[E_] 없다: {KLE_PATH}")
    kle = json.loads(KLE_PATH.read_text())
    if not isinstance(kle, list):
        sys.exit("[E_] KLE 는 최상위가 배열이어야 한다 (keyboard-layout-editor 원본 형식)")
    return kle


def rows_of(kle):
    """메타데이터 객체를 건너뛰고 행 배열만."""
    return [r for r in kle if isinstance(r, list)]


def name_of(kle):
    for it in kle:
        if isinstance(it, dict) and it.get("name"):
            return it["name"]
    return "WISH60-HE"


def options_of(kle):
    """
    VIA 레이아웃 옵션을 모은다.

    범례가 "addr\n\n\n<옵션>,<선택>" 이면 그 키는 해당 선택일 때만 쓰인다.
    소켓 자체는 셋 다 PCB 에 있으므로 present 마스크에는 전부 넣는다 —
    스위치가 안 꽂힌 자리는 자석이 없어 임계값을 넘지 못하므로 유령 입력이 없다.
    """
    out = {}
    for _, _, legend in iter_keys(kle):
        if "\n\n\n" in legend:
            addr, sel = legend.split("\n\n\n")
            opt, choice = sel.split(",")
            out.setdefault(int(opt), {}).setdefault(int(choice), []).append(addr)
    return out


def iter_keys(kle):
    """(행 참조, 원소 인덱스, 범례) 을 KLE 읽기 순서로 돌려준다."""
    for row in rows_of(kle):
        for i, item in enumerate(row):
            if isinstance(item, str) and ADDR_RE.match(item.split("\n")[0]):
                yield row, i, item


def parse_geometry(kle):
    """
    KLE 좌표 규칙대로 훑어 (x, y, w, h, row, col) 을 키 단위로 뽑는다.

    x/y 는 "다음 키 앞에 더할 상대 오프셋"이고, w/h 는 다음 키 하나에만 적용된 뒤
    1 로 돌아간다. 키를 놓을 때마다 x 는 그 키의 폭만큼 나아간다.
    """
    out = []
    y = 0.0
    for row in rows_of(kle):
        x, w, h = 0.0, 1.0, 1.0
        for item in row:
            if isinstance(item, dict):
                x += item.get("x", 0)
                y += item.get("y", 0)
                w = item.get("w", w)
                h = item.get("h", h)
                continue
            first = item.split("\n")[0]
            if ADDR_RE.match(first):
                s_, c_ = map(int, first.split(","))
                out.append((x, y, w, h, s_, c_))
            x += w
            w = h = 1.0
        y += 1.0
    return out


# ── LED 체인 ────────────────────────────────────────────────────────────────
#
# ESC 에서 오른쪽으로 시작해 **행마다 방향이 바뀌는 지그재그**다. 실측으로 확인했다
# (LED 0 = ESC, 16 = 백슬래시 = 2행 오른쪽 끝, 30 = CapsLock = 3행 왼쪽 끝,
#  64 = 오른쪽 Ctrl = 5행 오른쪽 끝).
#
# ★ "한 키에 LED 하나" 가 아니다. 이 보드에서 그 전제가 두 군데서 깨진다.
#
#   - 넓은 키는 하나로 고르게 못 비춰 스위치 좌우로 하나씩 더 있다 (7u 스페이스바)
#   - 스플릿 백스페이스처럼 **스위치가 안 꽂힌 자리에도 LED 는 있다.** 그 자리는
#     배치에 매트릭스 좌표로 이미 들어 있으므로 따로 처리할 것이 없다
#
LED_WIDE_U   = 6.0   # 이 폭 이상이면 좌·중·우 3개 (7u 스페이스바에서 실측)
LED_WIDE_CNT = 3

# 언더글로우 — 상단 뒤에 이어 붙는다. **오른쪽 아래 Ctrl 근처에서 시작해 왼쪽으로**
# 가며 판 외곽을 한 바퀴 돈다 (아래 → 왼쪽 → 위 → 오른쪽).
#
# 변마다 몇 개씩인지도 눈으로 셌다 — 아래 6, 좌 3, 위 6, 우 3.
#
# ★ 변별 개수까지가 실측이고 **한 변 안에서의 간격은 아니다.** 각 변을 균등하게
#   나눠 놓았다. 효과가 어색하면 `ws2812 walk 65` 로 하나씩 짚어 고친다.
#
#   둘레를 통째로 균등 분할하면 안 된다 — 짧은 변(5u)과 긴 변(15u)에 같은 간격을
#   주면 아래가 7개, 옆이 2개가 되어 실제(6/3)와 어긋난다.
LED_UNDER_SIDES = (6, 3, 6, 3)   # 아래, 왼쪽, 위, 오른쪽 (합 = 언더글로우 개수)


def led_under(geo):
    """언더글로우 (x, y) 근사 좌표. 키 단위, 판 왼쪽 위가 원점."""
    xs = [x for x, y, w, h, _, _ in geo if x < 15.5]     # 판 밖 대체 자리는 뺀다
    ws = [(x, w) for x, y, w, h, _, _ in geo if x < 15.5]
    x1 = max(x + w for x, w in ws)
    y1 = max(y + h for _, y, _, h, _, _ in geo)

    # 둘레를 한 바퀴 — 오른쪽 아래에서 왼쪽으로 출발한다
    legs = [((x1, y1), (0.0, y1)),      # 아래 : 오른쪽 -> 왼쪽
            ((0.0, y1), (0.0, 0.0)),    # 왼쪽 : 아래 -> 위
            ((0.0, 0.0), (x1, 0.0)),    # 위   : 왼쪽 -> 오른쪽
            ((x1, 0.0), (x1, y1))]      # 오른쪽 : 위 -> 아래

    out = []
    for ((ax, ay), (bx, by)), n in zip(legs, LED_UNDER_SIDES):
        for i in range(n):
            t = (i + 0.5) / n           # 변 안에서 균등
            out.append((ax + (bx - ax) * t, ay + (by - ay) * t))
    return out


def led_chain(geo):
    """geo -> LED 순서대로의 (row, col) 목록."""
    rows = {}
    for x, y, w, h, s_, c_ in geo:
        rows.setdefault(round(y * 4), []).append((x, w, s_, c_))

    out = []
    for i, ry in enumerate(sorted(rows)):
        ks = sorted(rows[ry])              # 왼쪽 -> 오른쪽
        if i % 2:
            ks = list(reversed(ks))        # 홀수 행은 되돌아온다
        for x, w, s_, c_ in ks:
            for _ in range(LED_WIDE_CNT if w >= LED_WIDE_U else 1):
                out.append((s_, c_))
    return out


# 드롭다운에 쓸 이름.
#
# ★ 영어로 쓴다. 웹앱이 이 문자열을 그대로 i18n 키로 쓰기 때문이다
#   (custom-control.tsx 의 `label: t(label)`). 여기 한글을 박으면 어느 언어로
#   보든 한글만 나온다 — 실제로 그렇게 넣었다가 되돌렸다.
#   번역은 via-he 의 src/locales/*.json 에 둔다.
RGB_LABEL = {
    "SOLID_COLOR":            "Solid Color",
    "GRADIENT_UP_DOWN":       "Gradient Up/Down",
    "BREATHING":              "Breathing",
    "CYCLE_ALL":              "Cycle All",
    "CYCLE_LEFT_RIGHT":       "Cycle Left/Right",
    "RAINBOW_MOVING_CHEVRON": "Rainbow Moving Chevron",
    "PIXEL_FLOW":             "Pixel Flow",
    "HE_DEPTH":               "HE Depth Brightness",
    "HE_DEPTH_HUE":           "HE Depth Hue",
    "HE_DEPTH_RIPPLE":        "HE Depth Ripple",
}


def rgb_effect_list():
    """
    VIA 드롭다운에 넣을 효과 목록을 **펌웨어 enum 순서 그대로** 만든다.

    ★ 손으로 적으면 안 된다. 순서는 rgb_matrix_effects.inc 의 나열 순서에
      config.h 에서 켠 것만 남긴 결과이고, 커스텀 효과가 그 뒤에 붙는다.
      효과를 하나 켜고 끄면 그 뒤의 번호가 전부 밀리는데, VIA 는 번호로만
      말하므로 목록이 어긋나면 **엉뚱한 효과가 걸린다.**
    """
    qmk = ROOT / "src/ap/modules/qmk"
    cfg = (BOARD_DIR / "config.h").read_text()

    def enabled(name):
        return re.search(rf"^\s*#\s*define\s+ENABLE_RGB_MATRIX_{name}\b",
                         cfg, re.M) is not None

    out = ["All Off"]                # RGB_MATRIX_NONE = 0

    # 기본 효과 — .inc 가 include 하는 순서가 곧 enum 순서다
    inc = (qmk / "quantum/rgb_matrix/animations/rgb_matrix_effects.inc").read_text()
    for m in re.finditer(r'#include\s+"([a-z0-9_]+)_anim\.h"', inc):
        anim = qmk / "quantum/rgb_matrix/animations" / f"{m.group(1)}_anim.h"
        t = anim.read_text()
        for e in re.finditer(r"RGB_MATRIX_EFFECT\(([A-Z0-9_]+)\)", t):
            nm = e.group(1)
            if nm == "SOLID_COLOR" or enabled(nm):
                out.append(RGB_LABEL.get(nm, nm.replace("_", " ").title()))

    # 커스텀 효과 — 기본 효과 뒤에 붙는다
    kb = BOARD_DIR / "rgb_matrix_kb.inc"
    if kb.exists():
        t = kb.read_text()
        for e in re.finditer(r"RGB_MATRIX_EFFECT\(([A-Z0-9_]+)\)", t):
            nm = e.group(1)
            if enabled(nm):
                out.append(RGB_LABEL.get(nm, nm.replace("_", " ").title()))
    return out


def gen_rgb(kle, geo, leds, und):
    """
    QMK rgb_matrix 의 g_led_config 를 배치에서 만든다.

    QMK 좌표계는 x 0~224, y 0~64 고정이다. 우리 배치는 1/4 키유닛이라 판 크기로
    normalize 한다.

    ★ matrix_co 는 (row, col) 하나에 LED 하나만 담는다. 스페이스바는 LED 가 셋인데
      대표로 가운데를 넣는다 — 반응형 효과가 눌린 키를 찾는 용도라 그것으로 충분하고,
      나머지 둘은 좌표만으로 효과에 참여한다.
    """
    xs = [(x, w) for x, y, w, h, _, _ in geo if x < 15.5]
    bx = max(x + w for x, w in xs)
    by = max(y + h for _, y, _, h, _, _ in geo)

    def qx(v): return min(224, max(0, round(v * 224 / bx)))
    def qy(v): return min(64,  max(0, round(v * 64 / by)))

    # LED 인덱스별 물리 좌표 — 넓은 키는 폭을 나눠 퍼뜨린다
    pos, i = [], 0
    rows = {}
    for x, y, w, h, s_, c_ in geo: rows.setdefault(round(y * 4), []).append((x, w, h, s_, c_))
    for ri, ry in enumerate(sorted(rows)):
        ks = sorted(rows[ry])
        if ri % 2: ks = list(reversed(ks))
        for x, w, h, s_, c_ in ks:
            n = LED_WIDE_CNT if w >= LED_WIDE_U else 1
            for k in range(n):
                pos.append((x + w * (k + 0.5) / n, ry / 4 + h / 2))

    co = [[255] * COLS for _ in range(ROWS)]
    for i, (s_, c_) in enumerate(leds):
        mid = LED_WIDE_CNT // 2
        # 넓은 키는 가운데 것만 대표로 — 같은 (row,col) 이 연속으로 나오는 구간이다
        if i and leds[i - 1] == (s_, c_):
            if i + 1 < len(leds) and leds[i + 1] == (s_, c_):
                co[s_][c_] = i          # 가운데
            continue
        if co[s_][c_] == 255:
            co[s_][c_] = i

    L = [
        "/*",
        " * rgb_config.c  —  자동 생성. 직접 고치지 말 것.",
        " *   tools/gen_keymap.py 가 layout-kle.json 에서 만든다.",
        " *",
        " * g_led_config — QMK rgb_matrix 가 보는 배치.",
        " *   matrix_co  (row, col) -> LED 인덱스, 없으면 NO_LED",
        " *   point      LED 물리 좌표. x 0~224, y 0~64 로 normalize 한 값",
        " *   flags      LED_FLAG_KEYLIGHT(4) = 키 밑, LED_FLAG_UNDERGLOW(2) = 언더글로우",
        " */",
        '#include "quantum.h"',
        "",
        "#ifdef RGB_MATRIX_ENABLE",
        "",
        "led_config_t g_led_config = {",
        "  {   /* matrix_co */",
    ]
    for r in range(ROWS):
        L.append("    { " + ", ".join("NO_LED" if v == 255 else f"{v:6d}" for v in co[r]) + " },")
    L += ["  },", "  {   /* point */"]
    for i in range(0, len(pos) + len(und), 6):
        chunk = (pos + und)[i:i + 6]
        L.append("    " + " ".join(f"{{{qx(x):3d},{qy(y):3d}}}," for x, y in chunk))
    L += ["  },", "  {   /* flags */"]
    fl = ["LED_FLAG_KEYLIGHT"] * len(pos) + ["LED_FLAG_UNDERGLOW"] * len(und)
    for i in range(0, len(fl), 4):
        L.append("    " + " ".join(f"{v}," for v in fl[i:i + 4]))
    L += ["  },", "};", "", "#endif", ""]
    return "\n".join(L)


def addr_of(legend):
    s, c = legend.split("\n")[0].split(",")
    return int(s), int(c)


def dump_grid(kle):
    """8x8 격자에 어느 셀이 쓰이는지 찍어 눈으로 검산한다."""
    used = {}
    for n, (_, _, legend) in enumerate(iter_keys(kle)):
        used[addr_of(legend)] = n

    print(f"\n{ROWS}x{COLS} 셀 사용 현황 (숫자 = 물리 배치 순번, . = 안 쓰임)")
    print("      " + "".join(f" ch{c} " for c in range(COLS)))
    for s in range(ROWS):
        line = f"  s{s:<2d} "
        for c in range(COLS):
            line += f" {used[(s, c)]:>2d}  " if (s, c) in used else "  .  "
        print(line)
    print(f"\n  쓰는 셀 {len(used)} / {ROWS * COLS}   (여유 {ROWS * COLS - len(used)})")
    free = sorted({(s, c) for s in range(ROWS) for c in range(COLS)} - set(used))
    if free:
        print("  안 쓰는 셀 : " + ", ".join(f"{s},{c}" for s, c in free))

    opts = options_of(kle)
    for opt in sorted(opts):
        parts = "   ".join(f"{ch} = [{' '.join(a)}]" for ch, a in sorted(opts[opt].items()))
        print(f"  레이아웃 옵션 {opt} : {parts}")


def cmd_apply(learn_file):
    """
    keys learn 출력을 KLE 범례에 순서대로 채운다.

    측정은 KLE 읽기 순서(좌상단부터 가로)로 누른다는 전제다. 순서가 어긋나면 결과도
    그대로 어긋나므로 개수·중복을 먼저 확인하고, 반영 뒤 격자를 찍어 눈으로 검산한다.
    """
    measured = [(int(m.group(2)), int(m.group(3)))
                for m in LEARN_RE.finditer(Path(learn_file).read_text())]
    if not measured:
        sys.exit("[E_] 측정값을 못 읽었다. 'keys learn' 출력을 그대로 저장했는지 확인할 것")

    kle = load_kle()
    slots = list(iter_keys(kle))

    print(f"레이아웃 {len(slots)} 키 / 측정 {len(measured)} 개")

    if len(measured) != len(slots):
        diff = len(slots) - len(measured)
        print(f"[E_] 개수가 다르다 ({'모자람' if diff > 0 else '많음'} {abs(diff)} 개).")
        print("     누락·중복이 있으니 다시 측정하는 편이 빠르다.")
        sys.exit(1)

    dup = sorted({a for a in measured if measured.count(a) > 1})
    if dup:
        sys.exit(f"[E_] 같은 셀이 여러 번 나왔다: {dup}")

    for (row, i, _), (s, c) in zip(slots, measured):
        if s >= ROWS or c >= COLS:
            sys.exit(f"[E_] 매트릭스 범위를 벗어난 주소: {s},{c}")
        row[i] = f"{s},{c}"

    KLE_PATH.write_text(json.dumps(kle, indent=2, ensure_ascii=False) + "\n")
    print(f"반영: {KLE_PATH.relative_to(ROOT)}")
    dump_grid(kle)


def cmd_gen():
    kle = load_kle()
    keys = [addr_of(legend) for _, _, legend in iter_keys(kle)]
    if not keys:
        sys.exit("[E_] 주소 범례를 하나도 못 찾았다")

    for s, c in keys:
        if s >= ROWS or c >= COLS:
            sys.exit(f"[E_] 매트릭스 범위를 벗어난 주소: {s},{c}")

    dup = sorted({a for a in keys if keys.count(a) > 1})
    if dup:
        sys.exit(f"[E_] 같은 셀이 여러 키에 배정됐다: {dup}")

    # ── VIA JSON (v3 정의)
    #
    # 레이아웃 옵션이 있으면 labels 가 있어야 VIA 가 선택지를 그린다. 이름은 사람이
    # 정하는 것이라 보드 폴더의 labels.json 에서 읽고, 없으면 자리표시자를 낸다.
    layouts = {"keymap": rows_of(kle)}
    opts = options_of(kle)
    if opts:
        labels = None
        if LABELS_PATH.exists():
            labels = json.loads(LABELS_PATH.read_text()).get("labels")
        if labels is None:
            labels = [[f"옵션 {o}"] + [f"선택 {c}" for c in sorted(opts[o])]
                      for o in sorted(opts)]
            print(f"[  ] {LABELS_PATH.relative_to(ROOT)} 가 없어 자리표시자를 넣었다")
        layouts["labels"] = labels

    via = {
        "name": name_of(kle),
        "vendorId": VID,
        "productId": PID,
        "matrix": {"rows": ROWS, "cols": COLS},
        "layouts": layouts,
    }

    # 커스텀 메뉴 — 있으면 그대로 싣는다. VIA 가 이걸 보고 설정 UI 를 그린다.
    if MENUS_PATH.exists():
        menus = json.loads(MENUS_PATH.read_text()).get("menus")
        if menus is None:
            menus = []

        # 조명 메뉴는 **생성한다.** 효과 번호가 펌웨어 enum 을 그대로 따라야 한다.
        eff = rgb_effect_list()
        if len(eff) > 1:
            menus = [{
                "label": "Lighting",
                "content": [{
                    "label": "RGB Matrix",
                    "content": [
                        {"label": "Brightness", "type": "range", "options": [0, 255],
                         "content": ["id_qmk_rgb_matrix_brightness", 3, 1]},
                        {"label": "Effect", "type": "dropdown", "options": eff,
                         "content": ["id_qmk_rgb_matrix_effect", 3, 2]},
                        {"showIf": "{id_qmk_rgb_matrix_effect} != 0",
                         "label": "Effect Speed", "type": "range", "options": [0, 255],
                         "content": ["id_qmk_rgb_matrix_effect_speed", 3, 3]},
                        {"showIf": "{id_qmk_rgb_matrix_effect} != 0",
                         "label": "Color", "type": "color",
                         "content": ["id_qmk_rgb_matrix_color", 3, 4]},
                    ],
                }],
            }] + [m for m in menus if m != "qmk_rgb_matrix"]

        if menus:
            via["menus"] = menus
            # 항목은 dict(직접 쓴 메뉴) 이거나 str(VIA 내장 메뉴 참조) 이다
            print(f"메뉴 {len(menus)}개 : "
                  + ", ".join(m if isinstance(m, str) else m.get("label", "?")
                              for m in menus))
    """
    프로파일 전환 키코드.

    ★ 키보드만으로 바꿀 수 있어야 한다.

      게임 중에 프로파일을 바꾸려고 브라우저를 띄울 수는 없다. VIA 의 커스텀
      키코드로 내면 사용자가 키 선택기에서 원하는 자리에 붙일 수 있다.

      NEXT 를 같이 둔다 — 네 개를 다 배치할 자리가 없는 60% 에서는 한 키로 도는
      쪽이 실제로 쓰인다.
    """
    via["customKeycodes"] = [
        {"name": f"Profile {i + 1}",
         "title": f"Switch to profile {i + 1}",
         "shortName": f"PF{i + 1}"}
        for i in range(4)
    ] + [{"name": "Profile Next",
          "title": "Switch to the next profile, wrapping around",
          "shortName": "PF>"}]

    VIA_PATH.write_text(json.dumps(via, indent=2, ensure_ascii=False) + "\n")

    """
    ★ 이 파일은 웹앱까지 손으로 옮겨야 한다.

      포크(via-he)는 local-kbs/ 에 **사본**을 두고 빌드 때 정의로 굽는다. 여기만
      고치면 앱은 옛 정의를 그대로 본다 — 효과 목록을 늘려 놓고 앱에서 안 보여
      한참 헤맸다. 자동으로 잇지 않는 것은 두 저장소가 따로 움직여서다.
    """
    print(f"     ↳ 웹앱에도 복사해야 한다:")
    print(f"       cp {VIA_PATH.relative_to(ROOT)} <via-he>/local-kbs/{VIA_PATH.name}")
    print(f"       (그다음 via-he 에서  bun scripts/add-local-kbs.ts)")
    print(f"생성: {VIA_PATH.relative_to(ROOT)}")

    # ── 펌웨어 헤더
    present = [0] * ROWS
    for s, c in keys:
        present[s] |= 1 << c

    L = [
        "/*",
        " * keys_layout.h  —  자동 생성. 직접 고치지 말 것.",
        " *",
        " *   생성 : tools/gen_keymap.py",
        f" *   원본 : keyboards/{KLE_PATH.parent.name}/{KLE_PATH.name}",
        " *",
        " * row = MUX 스텝, col = ADC 채널. 매트릭스가 곧 하드웨어다.",
        " */",
        "#ifndef KEYS_LAYOUT_H_",
        "#define KEYS_LAYOUT_H_",
        "",
        f'#define KEYS_LAYOUT_NAME      "{name_of(kle)}"',
        f"#define KEYS_LAYOUT_ROWS      {ROWS}",
        f"#define KEYS_LAYOUT_COLS      {COLS}",
        f"#define KEYS_LAYOUT_KEY_CNT   {len(keys)}",
        "",
        "/* 실재하는 셀만 1. 레이아웃에 없는 자리는 스위치가 없다. */",
        "static const uint16_t keys_present[KEYS_LAYOUT_ROWS] =",
        "{",
    ]
    for s in range(ROWS):
        bits = "".join("#" if present[s] >> c & 1 else "." for c in range(COLS))
        L.append(f"  0x{present[s]:04X},   /* s{s}  {bits} */")
    L += [
        "};",
        "",
        "/* 물리 배치 순서(좌상단부터 가로) -> (row, col) */",
        "static const uint8_t keys_pos[KEYS_LAYOUT_KEY_CNT][2] =",
        "{",
    ]
    for n in range(0, len(keys), 8):
        L.append("  " + " ".join(f"{{{s},{c}}}," for s, c in keys[n:n + 8]))
    L += ["};", ""]

    # ── 기본 키맵
    kmap = [[0] * COLS for _ in range(ROWS)]
    names = [[None] * COLS for _ in range(ROWS)]
    n = 0
    for r, row in enumerate(rows_of(kle)):
        base = DEFAULT_ROWS[r] if r < len(DEFAULT_ROWS) else []
        k = 0
        for item in row:
            if not (isinstance(item, str) and ADDR_RE.match(item.split("\n")[0])):
                continue
            s_, c_ = addr_of(item)
            nm = base[k] if k < len(base) else "NO"
            kmap[s_][c_] = KC[nm]
            names[s_][c_] = nm
            k += 1
            n += 1

    L += [
        "/*",
        " * 기본 키맵 — HID Usage ID (Keyboard/Keypad page).",
        " *",
        " * 0xE0~0xE7 은 모디파이어라 리포트의 키 배열이 아니라 [0] 바이트의 비트로 들어간다.",
        " * 0x00 (KC_NO) 은 레이아웃 옵션 키나 여분 소켓 — 무엇을 넣을지는 어떤 키캡을",
        " * 끼우느냐에 달렸으므로 비워 둔다.",
        " */",
        "static const uint8_t keys_keymap[KEYS_LAYOUT_ROWS][KEYS_LAYOUT_COLS] =",
        "{",
    ]
    for s_ in range(ROWS):
        cells = ", ".join(f"0x{kmap[s_][c]:02X}" for c in range(COLS))
        tags = " ".join((names[s_][c] or "-")[:4].ljust(4) for c in range(COLS))
        L.append(f"  {{ {cells} }},   /* s{s_}  {tags} */")
    L += ["};", ""]

    # 물리 좌표 — CLI 가 실제 배치로 그려서 매핑을 눈으로 검증할 수 있게 한다.
    geo = parse_geometry(kle)
    L += [
        "/*",
        " * 물리 좌표. 단위는 1/4 키유닛 (1 키 = 4).",
        " *   { x, y, w, h, row, col }",
        " */",
        "static const uint8_t keys_geo[KEYS_LAYOUT_KEY_CNT][6] =",
        "{",
    ]
    for x, y, w, h, s_, c_ in geo:
        L.append(f"  {{{round(x*4):3d},{round(y*4):3d},{round(w*4):3d},"
                 f"{round(h*4):3d}, {s_},{c_} }},")
    leds = led_chain(geo)
    L += [
        "};",
        "",
        f"#define KEYS_LAYOUT_LED_CNT   {len(leds)}",
        "",
        "/*",
        " * LED 체인 순서 -> 매트릭스 (row, col).",
        " *",
        " * ESC 에서 오른쪽으로, 행마다 방향이 바뀌는 지그재그다. 넓은 키는 여러 개가",
        " * 같은 (row, col) 을 가리킨다 — 7u 스페이스바가 좌·중·우 3개다.",
        " * 이 표 뒤(KEYS_LAYOUT_LED_CNT 이후)는 키에 안 붙은 언더글로우다.",
        " */",
        "static const uint8_t keys_led[KEYS_LAYOUT_LED_CNT][2] =",
        "{",
    ]
    for i in range(0, len(leds), 8):
        L.append("  " + " ".join(f"{{{s_},{c_}}}," for s_, c_ in leds[i:i + 8]))
    L += ["};"]

    und = led_under(geo)
    L += [
        "",
        f"#define KEYS_LAYOUT_LED_UNDER_CNT   {len(und)}",
        "",
        "/*",
        " * 언더글로우 위치 (상단 뒤에 이어 붙는다). 단위는 1/4 키유닛.",
        " *",
        " * 오른쪽 아래에서 시작해 왼쪽으로 가며 판 외곽을 한 바퀴 돈다.",
        " * 변별 개수는 아래 6, 왼쪽 3, 위 6, 오른쪽 3 이다 (눈으로 셈).",
        " *",
        " * ★ 변별 개수까지가 실측이고 **한 변 안에서의 간격은 아니다** — 균등하게",
        " *   나눠 놓았다. 어색하면 `ws2812 walk 65` 로 짚어 고친다.",
        " */",
        "static const uint8_t keys_led_under[KEYS_LAYOUT_LED_UNDER_CNT][2] =",
        "{",
    ]
    for i in range(0, len(und), 6):
        L.append("  " + " ".join(f"{{{round(x*4):3d},{round(y*4):3d}}},"
                                 for x, y in und[i:i + 6]))
    L += ["};"]
    L += ["", "#endif", ""]

    HDR_PATH.parent.mkdir(parents=True, exist_ok=True)
    HDR_PATH.write_text("\n".join(L))
    print(f"생성: {HDR_PATH.relative_to(ROOT)}  ({len(keys)} 키)")

    rgb_path = HDR_PATH.parent / "rgb_config.c"
    rgb_path.write_text(gen_rgb(kle, geo, leds, und))
    print(f"생성: {rgb_path.relative_to(ROOT)}  (LED {len(leds) + len(und)})")

    gen_qmk(kle, keys, names)
    dump_grid(kle)


# ── QMK 쪽 생성물 ───────────────────────────────────────────────────────────
#
# QMK 의 기본 키코드(0x0000~0x00FF)는 HID Usage ID 와 값이 같다. 그래서 위에서 만든
# 표를 그대로 쓸 수 있다. 예외는 우리가 내부용으로 잡은 0xF0(FN) 하나뿐인데,
# QMK 에서는 레이어 전환이 따로 있으므로 MO(1) 로 바꾼다.
QMK_SPECIAL = {"FN": "MO(1)"}


def gen_qmk(kle, keys, names):
    """
    QMK 는 물리 배치 순서로 키를 적고(LAYOUT 매크로), 매크로가 그걸 매트릭스 자리로
    흩뿌린다. 우리는 이미 물리 순서 -> (row, col) 표를 갖고 있으므로 그대로 만든다.
    """
    board = KLE_PATH.parent.name
    guard = "LAYOUT_QMK_H_"
    n     = len(keys)

    # ── LAYOUT 매크로
    #
    # 인자는 물리 순서 k00, k01, ... 이고, 본문은 매트릭스 순서로 늘어놓는다.
    # 자리가 없는 셀은 KC_NO 다 — 매트릭스가 8x8 인데 실재 키는 63개뿐이다.
    cell = [["KC_NO"] * COLS for _ in range(ROWS)]
    for i, (s, c) in enumerate(keys):
        cell[s][c] = f"k{i:02d}"

    L = [
        "/*",
        f" * layout_qmk.h  —  자동 생성. 직접 고치지 말 것.",
        " *",
        " *   생성 : tools/gen_keymap.py",
        f" *   원본 : keyboards/{board}/{KLE_PATH.name}",
        " *",
        " * LAYOUT 은 물리 배치 순서로 받아 매트릭스 자리에 흩뿌린다.",
        " * 스위치가 없는 셀은 KC_NO 로 채운다.",
        " */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#define LAYOUT( \\",
    ]
    for i in range(0, n, 8):
        tail = " \\" if i + 8 < n else " \\"
        L.append("  " + ", ".join(f"k{j:02d}" for j in range(i, min(i + 8, n))) + "," + tail)
    L[-1] = L[-1].replace(", \\", " \\").rstrip("\\ ").rstrip(",") + " \\"
    L += ["  ) { \\"]
    for s in range(ROWS):
        L.append("    { " + ", ".join(cell[s][c] for c in range(COLS)) + " }, \\")
    L += ["  }", "", f"#endif", ""]
    LAYOUT_PATH.write_text("\n".join(L))
    print(f"생성: {LAYOUT_PATH.relative_to(ROOT)}")

    # ── keymap.c
    #
    # 레이어 0 은 위에서 만든 기본 배치 그대로. 레이어 1 은 FN 을 눌렀을 때의
    # 자리인데, 지금은 F키·방향키만 얹고 나머지는 투명(KC_TRNS)으로 둔다.
    order = []
    for i, (s, c) in enumerate(keys):
        nm = names[s][c] or "NO"
        order.append(QMK_SPECIAL.get(nm, f"KC_{nm}"))

    fn = fn_layer(kle, keys, names)

    K = [
        "/*",
        " * keymap.c  —  자동 생성. 직접 고치지 말 것.",
        " *",
        " *   생성 : tools/gen_keymap.py",
        f" *   원본 : keyboards/{board}/{KLE_PATH.name}",
        " *",
        " * 여기 값은 '공장 기본값'일 뿐이다. VIA 로 바꾼 키맵은 EEPROM 에 들어가고,",
        " * dynamic_keymap 이 그쪽을 먼저 본다.",
        " */",
        '#include QMK_KEYBOARD_H',
        "",
        "const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {",
    ]
    for idx, layer in enumerate((order, fn)):
        K.append(f"  [{idx}] = LAYOUT(")
        for i in range(0, n, 8):
            K.append("    " + ", ".join(layer[i:i + 8]) + ("," if i + 8 < n else ""))
        K.append("  )," if idx == 0 else "  ),")
    K += ["};", ""]
    KEYMAP_PATH.write_text("\n".join(K))
    print(f"생성: {KEYMAP_PATH.relative_to(ROOT)}  (레이어 2)")


# FN 레이어에 얹을 것 — 숫자열은 F키, WASD 쪽은 방향키.
FN_MAP = {
    "ESC": "KC_GRV",
    "1": "KC_F1", "2": "KC_F2", "3": "KC_F3", "4": "KC_F4", "5": "KC_F5", "6": "KC_F6",
    "7": "KC_F7", "8": "KC_F8", "9": "KC_F9", "0": "KC_F10", "MINS": "KC_F11",
    "EQL": "KC_F12", "BSPC": "KC_DEL",
    "I": "KC_UP", "J": "KC_LEFT", "K": "KC_DOWN", "L": "KC_RGHT",
    "H": "KC_HOME", "N": "KC_END", "Y": "KC_PGUP", "B": "KC_PGDN",
}


def fn_layer(kle, keys, names):
    """FN 을 누른 동안의 자리. 정하지 않은 키는 투명 — 아래 레이어가 그대로 보인다."""
    out = []
    for s, c in keys:
        nm = names[s][c] or "NO"
        out.append(FN_MAP.get(nm, "KC_TRNS"))
    return out


def main():
    ap = argparse.ArgumentParser(description="KLE -> VIA JSON + 펌웨어 레이아웃 헤더")
    ap.add_argument("--apply", metavar="FILE",
                    help="keys learn 출력을 KLE 범례에 순서대로 반영한다")
    ap.add_argument("--show", action="store_true", help="현재 매핑만 표로 본다")
    ap.add_argument("--board", default=DEFAULT_BOARD,
                    help=f"keyboards/ 아래 모델 이름 (기본 {DEFAULT_BOARD})")
    args = ap.parse_args()

    set_board(args.board)

    if args.apply:
        cmd_apply(args.apply)
    elif args.show:
        dump_grid(load_kle())
    else:
        cmd_gen()


if __name__ == "__main__":
    main()
