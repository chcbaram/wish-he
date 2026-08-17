#!/usr/bin/env python3
"""이미지에 태그를 심는다 — 굽다 만 앱이 그대로 도는 것을 막기 위한 것.

    python3 tools/fw_tag.py build/wish-he.bin build/wish-he-tag.bin

★ 왜 필요한가.

  보드의 IAP 는 선두 매직 "HPM\\n" 넉 자만 보고 앱으로 뛴다 — 길이도 CRC 도 안 본다
  (docs/board-iap.md 2절). 그런데 그 넉 자는 **맨 먼저** 써지므로, 굽다 만 이미지도
  부트로더에게는 멀쩡해 보인다. 부트로더는 바꿀 수 없으니 앱이 스스로 본다
  (`src/hw/hw.c` 의 hwVerifyFirm).

★ 왜 별도 파일로 만드나 — 원본을 덮지 않는다.

  `build/wish-he.bin` 은 태그가 0 이라 부팅 검사를 건너뛴다. 개발 중 JTAG 나
  iap_update.py 로 바로 굽는 흐름이 그대로 살아 있어야 하기 때문이다.
  태그가 붙은 쪽(`-tag.bin`)은 웹 도구의 "파일에서 굽기" 로 올릴 때 쓴다 — 그때는
  검사가 돌아 반쯤 써진 이미지를 잡아낸다.

★ 자리는 매직으로 찾는다.

  링커에 오프셋을 박아 두고 도구에 그 숫자를 또 적으면 반드시 갈라진다. 매직이
  정확히 하나여야 하고, 아니면 멈춘다.

★ 검사 범위는 **태그 뒤 전부**다. 한 덩어리라 장치가 crc32() 한 번으로 본다.
  그래서 태그는 이미지 앞쪽(.start 안, 오프셋 0x50)에 있어야 한다.
"""

import binascii
import struct
import sys
from pathlib import Path

TAG_MAGIC = 0x54414720          # "TAG " — src/common/def.h 의 TAG_MAGIC_NUMBER
TAG_SIZE = 20                   # firm_tag_t : magic, fw_addr, fw_size, fw_crc, tag_crc
APP_MAGIC = b"HPM\n"


def fill_tag(image: bytes) -> bytes:
    """태그를 채운 이미지를 돌려준다. 길이는 그대로다."""
    if image[:4] != APP_MAGIC:
        sys.exit(f"[E_] 선두 4바이트가 {APP_MAGIC!r} 이 아니다 — 이 보드 펌웨어가 아니다")

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

    return image[:off] + tag + image[off + TAG_SIZE:], off, fw_size, fw_crc


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    if not src.exists():
        sys.exit(f"[E_] 입력이 없다: {src}")

    out, off, fw_size, fw_crc = fill_tag(src.read_bytes())
    dst.write_bytes(out)

    print(f"  태그 : {dst.name}  오프셋 0x{off:X}, "
          f"검사 {fw_size:,} B ({fw_size * 100 / len(out):.2f}%), crc 0x{fw_crc:08X}")


if __name__ == "__main__":
    main()
