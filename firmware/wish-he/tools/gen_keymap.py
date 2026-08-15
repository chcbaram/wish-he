#!/usr/bin/env python3
"""
KLE 레이아웃 하나를 단일 진실 원본으로 삼아 VIA JSON 과 펌웨어 헤더를 만든다.

VIA 앱과 펌웨어가 같은 정보를 필요로 한다 — 어떤 (row, col) 이 실재하는 키이고
물리적으로 어디에 있는가. 두 벌로 관리하면 반드시 어긋나므로 한 곳에서 뽑아 쓴다.

    json/wish60-he-kle.json          <- 손으로 편집하는 건 이것 하나
       │   (keyboard-layout-editor.com 에 그대로 붙여넣어 편집)
       ├──▶  json/wish60-he-via.json        VIA 앱용
       └──▶  src/hw/driver/keys_layout.h    펌웨어용 present/pos 표

이 보드는 row = MUX 스텝(s), col = ADC 채널(ch) 이다. 매트릭스가 곧 하드웨어라
변환 계층이 없다.

사용법
    python3 tools/gen_keymap.py --apply learn.txt   # 실측값을 KLE 범례에 채운다
    python3 tools/gen_keymap.py                     # KLE -> VIA JSON + 헤더
    python3 tools/gen_keymap.py --show              # 현재 매핑을 표로만 본다

의존성 없음.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KLE_PATH = ROOT / "json" / "wish60-he-kle.json"
VIA_PATH = ROOT / "json" / "wish60-he-via.json"
HDR_PATH = ROOT / "src" / "hw" / "driver" / "keys_layout.h"

NAME = "WISH60-HE"
VID, PID = "0x0483", "0x5304"
ROWS, COLS = 8, 8

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


def iter_keys(kle):
    """(행 참조, 원소 인덱스, 범례) 을 KLE 읽기 순서로 돌려준다."""
    for row in rows_of(kle):
        for i, item in enumerate(row):
            if isinstance(item, str) and ADDR_RE.match(item.split("\n")[0]):
                yield row, i, item


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
        "name": NAME,
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
        f" *   원본 : json/{KLE_PATH.name}",
        " *",
        " * row = MUX 스텝, col = ADC 채널. 매트릭스가 곧 하드웨어다.",
        " */",
        "#ifndef KEYS_LAYOUT_H_",
        "#define KEYS_LAYOUT_H_",
        "",
        f'#define KEYS_LAYOUT_NAME      "{NAME}"',
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
    args = ap.parse_args()

    if args.apply:
        cmd_apply(args.apply)
    elif args.show:
        dump_grid(load_kle())
    else:
        cmd_gen()


if __name__ == "__main__":
    main()
