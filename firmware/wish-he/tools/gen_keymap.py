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


def set_board(name):
    """
    모델별 폴더 하나에 편집 원본과 생성물을 모은다.

        keyboards/<모델>/layout-kle.json   <- 손으로 편집하는 건 이것 하나
                        layout-via.json    VIA 앱용   (생성물)
                        layout.h           펌웨어용   (생성물)

    CMake 가 keyboards/${HW_KEYBOARD} 를 인클루드 경로에 넣으므로 펌웨어는
    #include "layout.h" 한 줄이면 된다.
    """
    global KLE_PATH, VIA_PATH, HDR_PATH
    d = BOARDS / name
    if not d.is_dir():
        have = sorted(p.name for p in BOARDS.iterdir() if p.is_dir()) if BOARDS.is_dir() else []
        sys.exit(f"[E_] 그런 보드가 없다: {name}\n     있는 것: {', '.join(have) or '없음'}")
    KLE_PATH = d / "layout-kle.json"
    VIA_PATH = d / "layout-via.json"
    HDR_PATH = d / "layout.h"

ADDR_RE = re.compile(r"^(\d+),(\d+)$")
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

    # ── VIA JSON
    via = {
        "name": name_of(kle),
        "vendorId": VID,
        "productId": PID,
        "matrix": {"rows": ROWS, "cols": COLS},
        "layouts": {"keymap": rows_of(kle)},
    }
    VIA_PATH.write_text(json.dumps(via, indent=2, ensure_ascii=False) + "\n")
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
    L += ["};", "", "#endif", ""]

    HDR_PATH.parent.mkdir(parents=True, exist_ok=True)
    HDR_PATH.write_text("\n".join(L))
    print(f"생성: {HDR_PATH.relative_to(ROOT)}  ({len(keys)} 키)")
    dump_grid(kle)


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
