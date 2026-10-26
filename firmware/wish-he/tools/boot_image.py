#!/usr/bin/env python3
"""부트로더 ELF 를 0x80000000 부터 굽는 완성 이미지로 만든다.

    boot_image.py <boot.elf> <out.bin>

★ objcopy -O binary 를 쓰지 않는다. 두 번 어긋났다.

  - 가장 낮은 섹션(0x80000400 의 nor_cfg)부터 시작해서 0x80000000 에 쓰면 전부 0x400 씩
    밀린다
  - 섹션 사이 빈틈을 0x00 으로 채운다. `--gap-fill 0xff` 를 주면 이번에는 LMA 가 0 근처인
    섹션까지 이어 채워 **2GB 짜리 파일**을 만들었다

  그래서 ELF 의 LOAD 세그먼트에서 플래시에 놓이는 바이트(물리 주소 기준)만 골라
  0xFF(소거 상태) 바탕에 놓는다.

★ 0x400 앞은 비워 둔다. 링커가 ELF 헤더를 첫 세그먼트(0x80000000~)에 넣어서,
  ELF 를 OpenOCD 로 그대로 구우면 거기에 ELF 헤더가 쓰인다. ROM 은 0x400(nor_cfg)과
  0x1000(부트 헤더)만 보므로 무해하지만 이미지에 남길 까닭은 없다.

★ 검사하는 것 — 어긋나면 ROM 이 부팅을 못 하는 이미지다.
  0x400   nor_cfg_option 태그 0xFCF9xxxx (ROM 이 이걸로 XPI 를 설정한다)
  0x1000  부트 헤더가 비어 있지 않다
  크기     0x1D000(업데이트 플래그) 앞에서 끝난다
"""
import struct
import sys

FLASH_BASE = 0x80000000
NOR_CFG_OFF = 0x400
BOOT_HDR_OFF = 0x1000
FLAG_OFF = 0x1D000
PT_LOAD = 1

elf = open(sys.argv[1], 'rb').read()
if elf[:4] != b'\x7fELF' or elf[4] != 1:
    sys.exit('[E_] 32비트 ELF 가 아니다')

phoff, = struct.unpack_from('<I', elf, 0x1C)
phentsize, phnum = struct.unpack_from('<HH', elf, 0x2A)

img = bytearray(b'\xff' * FLAG_OFF)
end = 0
for i in range(phnum):
    p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from(
        '<IIIII', elf, phoff + i * phentsize)
    if p_type != PT_LOAD or p_filesz == 0:
        continue
    if not FLASH_BASE <= p_paddr < FLASH_BASE + 0x10000000:
        continue                                    # RAM 에만 있는 세그먼트
    off = p_paddr - FLASH_BASE
    data = elf[p_offset:p_offset + p_filesz]
    if off + len(data) > FLAG_OFF:
        sys.exit(f'[E_] {off + len(data):#x} 까지 — 업데이트 플래그(0x1D000)를 덮는다')
    if off < NOR_CFG_OFF:                           # ELF 헤더 자리는 버린다
        data = data[NOR_CFG_OFF - off:]
        off = NOR_CFG_OFF
    img[off:off + len(data)] = data
    end = max(end, off + len(data))

img = bytes(img[:end])
tag, = struct.unpack_from('<I', img, NOR_CFG_OFF)
if tag >> 16 != 0xFCF9:
    sys.exit(f'[E_] 0x400 이 nor_cfg 태그가 아니다 ({tag:#010x}) — 링커 배치가 바뀌었나')
if img[BOOT_HDR_OFF:BOOT_HDR_OFF + 16] == b'\xff' * 16:
    sys.exit('[E_] 0x1000 에 부트 헤더가 없다')

open(sys.argv[2], 'wb').write(img)
print(f'{sys.argv[2]}  {len(img)} B  ({FLASH_BASE:#x} ~ {FLASH_BASE + len(img):#x})')
