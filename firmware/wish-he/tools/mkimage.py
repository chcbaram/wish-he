#!/usr/bin/env python3
"""wish61-he 슬롯 이미지를 만든다.

링커가 뽑은 순수 바이너리(0x80021000 에 놓일 것) 앞에 4KB 헤더 페이지를 붙여
벤더 IAP 가 받아들이는 슬롯 이미지를 만든다.

    ./mkimage.py app.bin image.bin

슬롯 배치
    +0x0000  이미지 헤더 20 B
    +0x0014  0xFF 패딩
    +0x1000  본문 (진입점 = 슬롯 + 0x1000 = 0x80021000)

헤더 (리틀엔디안 u32 x 5)
    magic   0xBEAF5AA5
    devid   0x0030000B    ★ 0x8001F000(devinfo)+4 와 같아야 IAP 가 받는다
    offset  0x00001000    슬롯 기준. IAP 는 `슬롯 + offset` 으로 점프한다
    size    본문 길이
    crc32   표준 CRC-32(zlib) — 대상은 본문뿐, 헤더는 제외

근거: firmware/hpm5361-fw/docs/wish61-he-flash-dump.md 7.2절
"""
import struct
import sys
import zlib

MAGIC = 0xBEAF5AA5
DEVID = 0x0030000B          # devinfo(0x8001F000)+4. 바꾸면 IAP 가 거부한다
OFFSET = 0x1000             # 헤더 페이지 크기 = 본문 시작 오프셋
SLOT_SIZE = 0x40000         # App1 슬롯 256KB
MAX_BODY = SLOT_SIZE - OFFSET   # 252KB


def build(body: bytes) -> bytes:
    if len(body) > MAX_BODY:
        raise SystemExit(
            f"본문이 슬롯을 넘는다: {len(body):,} B > {MAX_BODY:,} B (252KB)\n"
            f"  App2 백업을 포기하면 508KB 까지 늘릴 수 있다 (문서 7.1절 B안)")
    crc = zlib.crc32(body) & 0xFFFFFFFF
    header = struct.pack('<5I', MAGIC, DEVID, OFFSET, len(body), crc)
    # 패딩은 0x00 이다 — 원본 이미지를 실측해서 맞췄다 (4,076 B 전부 0x00).
    # 플래시 관점에서는 0xFF 가 무의미 기록이라 낫지만, 벤더 포맷과 바이트 단위로
    # 같게 두는 편이 나중에 원본과 대조할 때 편하다.
    return header + b'\x00' * (OFFSET - len(header)) + body


def main(argv):
    if len(argv) != 3:
        raise SystemExit(f"사용법: {argv[0]} <app.bin> <image.bin>")
    body = open(argv[1], 'rb').read()
    img = build(body)
    open(argv[2], 'wb').write(img)

    magic, devid, offset, size, crc = struct.unpack_from('<5I', img)
    print(f"{argv[1]} → {argv[2]}")
    print(f"  본문   {size:,} B  ({size / 1024:.1f} KB)   여유 {MAX_BODY - size:,} B")
    print(f"  헤더   magic=0x{magic:08X} devid=0x{devid:08X} "
          f"offset=0x{offset:X} crc32=0x{crc:08X}")
    print(f"  이미지 {len(img):,} B   ({(len(img) + 55) // 56:,} 청크)")


if __name__ == '__main__':
    main(sys.argv)
