#!/usr/bin/env python3
"""
빌드 산출물을 배포용으로 옮기고 목록(manifest.json)을 갱신한다.

    python3 tools/make_release.py                    빌드 버전 그대로
    python3 tools/make_release.py -n "노트" -n "노트2"
    python3 tools/make_release.py --version V260816R1

만들어지는 것:

    release/manifest.json
    release/<버전>/wish60-he-<버전>.bin

웹 도구(VIA HE)가 이 manifest 를 읽어 버전 목록과 릴리즈 노트를 보여주고, 고른
펌웨어를 내려받아 WebHID 로 굽는다.

★ 버전은 손으로 적지 않는다.

  `src/hw/hw_def.h` 의 `_DEF_FIRMWATRE_VERSION` 이 장치가 스스로 보고하는 값이고,
  그것이 곧 이 릴리스의 버전이어야 한다. 두 군데에 적으면 반드시 갈라진다 —
  장치는 A 라 하는데 목록에는 B 로 올라가면 사용자가 뭘 깔았는지 알 수 없다.
"""

import argparse
import binascii
import json
import re
import shutil
import struct
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HW_DEF = ROOT / "src/hw/hw_def.h"
BUILD = ROOT / "build/wish60-he.bin"
RELEASE = ROOT / "release"
MANIFEST = RELEASE / "manifest.json"

# 웹앱(via-he)의 public/firmware/ 아래 이 보드가 쓰는 칸 이름.
# 거기 manifest.json 은 **보드 목록**이고, 이 파일이 만드는 목록은 그 아래로 들어간다.
BOARD_DIR = "wish60-he"

APP_MAGIC = b"HPM\n"


def read_define(name: str) -> str:
    m = re.search(rf'#define\s+{name}\s+"([^"]+)"', HW_DEF.read_text())
    if not m:
        sys.exit(f"[E_] {HW_DEF.name} 에서 {name} 을 못 찾았다")
    return m.group(1)


TAG_MAGIC = 0x54414720          # "TAG " — src/common/def.h
TAG_SIZE = 20                   # firm_tag_t : magic, fw_addr, fw_size, fw_crc, tag_crc


def fill_tag(image: bytes) -> bytes:
    """이미지 안의 firm_tag 블롭을 채운다.

    ★ 왜 필요한가.

      보드의 IAP 는 선두 매직 "HPM\\n" 넉 자만 보고 앱으로 뛴다 — 길이도 CRC 도 안 본다.
      그런데 그 넉 자는 **맨 먼저** 써지므로, 굽다 만 이미지도 부트로더에게는 멀쩡해
      보인다. 그래서 앱이 스스로 검사하도록 여기서 태그를 심는다
      (`src/hw/hw.c` 의 hwVerifyFirm).

    ★ 자리는 매직으로 찾는다.

      링커에 오프셋을 박아 두고 여기서 그 숫자를 또 적으면 반드시 갈라진다.
      매직이 정확히 하나여야 하고, 아니면 멈춘다.

    ★ 검사 범위는 **태그 뒤 전부**다. 한 덩어리라 장치가 crc32() 한 번으로 본다.
    """
    magic = struct.pack("<I", TAG_MAGIC)
    hits = [i for i in range(0, len(image) - TAG_SIZE, 4)
            if image[i:i + 4] == magic]

    if len(hits) == 0:
        sys.exit("[E_] 이미지에서 TAG 블롭을 못 찾았다 — hw.c 의 firm_tag 가 빠졌나?")
    if len(hits) > 1:
        sys.exit(f"[E_] TAG 매직이 {len(hits)} 개다 (오프셋 {[hex(h) for h in hits]}) — "
                 "하나여야 한다")

    off = hits[0]
    fw_addr = off + TAG_SIZE            # 검사 시작 = 태그 바로 뒤
    fw_size = len(image) - fw_addr
    fw_crc = binascii.crc32(image[fw_addr:]) & 0xFFFFFFFF

    tag = struct.pack("<IIII", TAG_MAGIC, fw_addr, fw_size, fw_crc)
    tag += struct.pack("<I", binascii.crc32(tag) & 0xFFFFFFFF)

    print(f"  태그   : 오프셋 0x{off:X}, 검사 {fw_size:,} B, crc 0x{fw_crc:08X}")
    return image[:off] + tag + image[off + TAG_SIZE:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", help="버전 (기본: hw_def.h 의 값)")
    ap.add_argument("-n", "--note", action="append", default=[],
                    help="릴리즈 노트 한 줄. 여러 번 줄 수 있다")
    ap.add_argument("--bin", type=Path, default=BUILD)
    args = ap.parse_args()

    if not args.bin.exists():
        sys.exit(f"[E_] 빌드 산출물이 없다: {args.bin}\n     먼저 cmake --build build")

    image = args.bin.read_bytes()

    # ★ 매직을 확인한다. 부트로더가 인식 못 하는 것을 배포하면 사용자가 굽고 나서야
    #   안다 — 그때는 이미 업데이트 모드에 갇혀 있다.
    if image[:4] != APP_MAGIC:
        sys.exit(f"[E_] 선두 4바이트가 {APP_MAGIC!r} 이 아니다 — 이 보드 펌웨어가 아니다")

    image = fill_tag(image)

    board = read_define("_DEF_BOARD_NAME")
    version = args.version or read_define("_DEF_FIRMWATRE_VERSION")

    out_dir = RELEASE / version
    out_dir.mkdir(parents=True, exist_ok=True)
    name = f"{board.lower()}-{version}.bin"
    (out_dir / name).write_bytes(image)

    entry = {
        "version": version,
        "board": board,
        "date": date.today().isoformat(),
        "bin": f"{version}/{name}",
        "size": len(image),
        "crc": f"0x{binascii.crc32(image) & 0xFFFFFFFF:08X}",
        "notes": args.note,
    }

    man = {"board": board, "firmwares": []}
    if MANIFEST.exists():
        man = json.loads(MANIFEST.read_text())

    # 같은 버전이면 갈아 끼운다. 최신이 맨 앞.
    man["board"] = board
    man["firmwares"] = [f for f in man.get("firmwares", []) if f["version"] != version]
    man["firmwares"].insert(0, entry)

    MANIFEST.write_text(json.dumps(man, indent=2, ensure_ascii=False) + "\n")

    print(f"  {version}  {len(image):,} B  crc {entry['crc']}")
    for n in args.note:
        print(f"    - {n}")
    print(f"  생성: {(out_dir / name).relative_to(ROOT)}")
    print(f"  갱신: {MANIFEST.relative_to(ROOT)}  (버전 {len(man['firmwares'])}개)")
    print()
    print("  ↳ 웹앱에도 옮겨야 한다:")
    print(f"       rsync -a {RELEASE.relative_to(ROOT)}/ <via-he>/public/firmware/{BOARD_DIR}/")
    print("     (두 저장소가 따로 움직여 자동으로 잇지 않는다)")
    print(f"     ★ 보드마다 칸이 나뉜다. 웹앱의 public/firmware/manifest.json 은")
    print(f"       보드 목록이고, 여기 것은 그 아래 {BOARD_DIR}/manifest.json 으로 들어간다.")


if __name__ == "__main__":
    main()
