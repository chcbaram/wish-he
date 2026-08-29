#!/usr/bin/env python3
"""
빌드 산출물을 배포용으로 옮기고 목록(manifest.json)을 갱신한다.

    python3 tools/make_release.py                          wish60-he-7u (기본)
    python3 tools/make_release.py wish61-he -n "노트"
    python3 tools/make_release.py wish61-he --version V260829R1

만들어지는 것 — **웹앱(via-he) 안에 바로 쓴다.**

    ../../../via-he/public/firmware/<보드>/manifest.json
    ../../../via-he/public/firmware/<보드>/<버전>/<보드>-<버전>.bin

★ 보드마다 이미지 형식이 다르다 (부트로더 계약이 다르다).

    wish60-he-7u   fw_tag.py 가 매직 "HPM\n" 과 검사값을 여기서 박는다
    wish61-he      mkimage.py 가 빌드 후처리에서 이미 헤더(0xBEAF5AA5)를 박았다.
                   여기서는 -tag.bin 을 그대로 옮기고 확인만 한다

웹 도구(VIA HE)가 이 manifest 를 읽어 버전 목록과 릴리즈 노트를 보여주고, 고른
펌웨어를 내려받아 WebHID 로 굽는다.

★ 펌웨어 저장소에는 사본을 안 둔다.

  예전에는 `release/` 에 만들고 "rsync 로 옮겨라" 를 사람에게 시켰는데, 그 한 단계가
  빠지면서 실제로 갈라졌다 (펌웨어 R38 / 웹앱 R1). 읽는 것은 웹앱뿐이므로 한 벌만 둔다.

★ 이미지에는 태그가 박힌다 (tools/fw_tag.py). 굽다 만 앱이 그대로 도는 것을 막는
  장치다 — 자세한 것은 그 파일과 src/hw/hw.c 의 hwVerifyFirm.

★ 버전은 손으로 적지 않는다.

  `src/hw/hw_def.h` 의 `_DEF_FIRMWATRE_VERSION` 이 장치가 스스로 보고하는 값이고,
  그것이 곧 이 릴리스의 버전이어야 한다. 두 군데에 적으면 반드시 갈라진다 —
  장치는 A 라 하는데 목록에는 B 로 올라가면 사용자가 뭘 깔았는지 알 수 없다.
"""

import os
import argparse
import binascii
import json
import re
import struct
import sys
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fw_tag import fill_tag           # noqa: E402  (태그 규칙은 그쪽 한 곳에만 둔다)

ROOT = Path(__file__).resolve().parent.parent
HW_DEF = ROOT / "src/hw/hw_def.h"

#
# 보드 표 — tools/build.sh 와 같은 축이다.
#
# ★ **보드 이름을 hw_def.h 에서 읽지 않는다.**
#
#   거기서는 `#if defined(HW_BOARD_WISH61_HE)` 로 갈리는데, 정규식은 파일 전체에서
#   **첫 번째**를 잡는다. wish61 이 위에 있으므로 wish60 배포를 내도 이름이
#   "WISH61-HE" 로 찍힌다. 조건부 매크로를 밖에서 읽으려 한 것이 잘못이다 —
#   어느 보드를 내는지는 여기서 이미 알고 있다.
#
# dir  : 웹앱 public/firmware/ 아래 이 보드가 쓰는 칸
# bin  : build/<kb>/ 안에서 가져올 파일
# tag  : "fw_tag" 면 여기서 태그를 박고, "none" 이면 이미 박혀 있다
# magic: 그 이미지의 선두 4바이트
#
BOARDS = {
    "wish60-he-7u": {
        "dir": "wish60-he", "name": "WISH60-HE",
        "bin": "{kb}.bin", "tag": "fw_tag", "magic": b"HPM\n",
    },
    "wish61-he": {
        "dir": "wish61-he", "name": "WISH61-HE",
        "bin": "{kb}-tag.bin", "tag": "none",
        "magic": bytes([0xA5, 0x5A, 0xAF, 0xBE]),   # 0xBEAF5AA5 (LE)
    },
}

#
# ★ 배포본은 **웹앱에 바로 쓴다.** 펌웨어 저장소에는 사본을 안 둔다.
#
#   예전에는 release/ 에 만들고 "rsync 로 옮겨라" 를 사람에게 시켰다. 그 한 단계가
#   빠지면서 실제로 갈라졌다 — 펌웨어 쪽은 V260816R38, 웹앱 쪽은 V260817R1 이었다.
#   읽는 것은 웹앱뿐이므로 한 벌만 둔다.
#
WEB = ROOT.parents[2] / "via-he/public/firmware"


def read_define(name: str) -> str:
    """조건부로 갈리지 않는 매크로만 여기서 읽는다 (버전). 보드 이름은 BOARDS 가 안다."""
    m = re.search(rf'#define\s+{name}\s+"([^"]+)"', HW_DEF.read_text())
    if not m:
        sys.exit(f"[E_] {HW_DEF.name} 에서 {name} 을 못 찾았다")
    return m.group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kb", nargs="?", default=os.environ.get("WISH_KB", "wish60-he-7u"),
                    choices=sorted(BOARDS), help="키보드 (기본: wish60-he-7u)")
    ap.add_argument("--version", help="버전 (기본: hw_def.h 의 값)")
    ap.add_argument("-n", "--note", action="append", default=[],
                    help="릴리즈 노트 한 줄. 여러 번 줄 수 있다")
    ap.add_argument("--bin", type=Path, help="이미지를 직접 지정")
    args = ap.parse_args()

    spec = BOARDS[args.kb]
    src = args.bin or (ROOT / "build" / args.kb / spec["bin"].format(kb=args.kb))

    if not src.exists():
        sys.exit(f"[E_] 빌드 산출물이 없다: {src}\n"
                 f"     먼저 tools/build.sh {args.kb}")

    image = src.read_bytes()

    # ★ 매직을 확인한다. 부트로더가 인식 못 하는 것을 배포하면 사용자가 굽고 나서야
    #   안다 — 그때는 이미 업데이트 모드에 갇혀 있다.
    if image[:4] != spec["magic"]:
        sys.exit(f"[E_] 선두 4바이트가 {spec['magic']!r} 이 아니다 — "
                 f"{args.kb} 의 이미지가 아니다 ({src.name})")

    if spec["tag"] == "fw_tag":
        image, tag_off, fw_size, fw_crc = fill_tag(image)
        print(f"  태그   : 오프셋 0x{tag_off:X}, 검사 {fw_size:,} B, crc 0x{fw_crc:08X}")
    else:
        # 빌드 후처리(mkimage.py)가 이미 헤더를 박았다. 여기서 다시 손대면 CRC 가 깨진다.
        magic, devid, off, size, crc = struct.unpack_from("<5I", image)
        print(f"  헤더   : magic=0x{magic:08X} devid=0x{devid:08X} "
              f"offset=0x{off:X} size={size:,} crc32=0x{crc:08X}")

    board = spec["name"]
    version = args.version or read_define("_DEF_FIRMWATRE_VERSION")

    release = WEB / spec["dir"]
    manifest = release / "manifest.json"

    out_dir = release / version
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
    if manifest.exists():
        man = json.loads(manifest.read_text())

    # 같은 버전이면 갈아 끼운다. 최신이 맨 앞.
    man["board"] = board
    man["firmwares"] = [f for f in man.get("firmwares", []) if f["version"] != version]
    man["firmwares"].insert(0, entry)

    manifest.write_text(json.dumps(man, indent=2, ensure_ascii=False) + "\n")

    print(f"  {version}  {len(image):,} B  crc {entry['crc']}")
    for n in args.note:
        print(f"    - {n}")
    # ★ ROOT 기준 상대경로로 찍지 않는다. 배포 자리는 **다른 저장소** 안이라
    #   relative_to(ROOT) 가 터진다. 실제로 그렇게 터졌다.
    print(f"  생성: {out_dir / name}")
    print(f"  갱신: {manifest}  (버전 {len(man['firmwares'])}개)")
    print()
    print("  ↳ 웹앱(via-he)에 바로 썼다. 거기서 커밋하면 된다.")
    print(f"     보드 목록은 public/firmware/manifest.json 이고 손으로 관리한다 —")
    print(f"     새 보드를 더할 때만 거기에 한 줄 넣는다.")


if __name__ == "__main__":
    main()
