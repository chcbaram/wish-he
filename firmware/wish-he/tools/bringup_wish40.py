#!/usr/bin/env python3
"""wish40-he 브링업 시험 이미지(bringup/wish40-he)의 결과를 JTAG 으로 읽는다.

    python3 tools/bringup_wish40.py            지금 값 (주소 x 채널, ADC0 · ADC1)
    python3 tools/bringup_wish40.py range      min/max 폭 — 키를 눌러 본 뒤에
    python3 tools/bringup_wish40.py clear      min/max 를 다시 잡는다
    python3 tools/bringup_wish40.py settle N   주소를 쓴 뒤 nop 수

★ 채널 <-> MAG 대응은 **잰다.** 회로도가 빈 MUX 입력을 GND 에 묶어 두어서 MAG 마다
  0 이 되는 주소가 다르다 — 키를 안 눌러도 갈린다 (MAG2~4 만 무늬가 같다).

★ 코어가 버스에 묶여 정지를 안 받으면 `init` 이 실패한다. 그때는 DMI 로 리셋과
  정지 요청을 같이 걸어 ROM 에서 붙잡는다 (--rescue). 시험 이미지의 첫 판이
  원샷 ADC 를 켜지 않아 실제로 그랬다.
"""
import os
import struct
import subprocess
import sys
import tempfile

FW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OCD = os.path.expanduser('~/hdd/tools/hpmicro/openocd/bin/openocd')
ELF = os.path.join(FW, 'build/bringup-wish40-he/wish40-he-bringup.elf')
NM = os.path.expanduser('~/hdd/tools/xpack-riscv-none-elf-gcc-13.4.0-1/bin/riscv-none-elf-nm')

ADC, ADDR, CH = 2, 8, 16
HDR = 8 * 4
SIZE = HDR + 3 * ADC * ADDR * CH * 2
MAGIC = 0x42303457

# 회로도: 빈 입력(GND) 이 있는 주소. MAG1 은 8칸이 다 찼다.
GND_ADDR = {
    'MAG0': {5, 7},
    'MAG1': set(),
    'MAG2/3/4': {6},
    'MAG5': {3},
}
ZERO_MAX = 100          # 16비트 눈금. GND 칸은 24 로 읽힌다
SENSOR_LO, SENSOR_HI = 16000, 50000     # 자석 없는 센서는 VCC/2 ≈ 32500


def probe_addr():
    out = subprocess.run([NM, ELF], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == 'probe':
            return int(p[0], 16)
    sys.exit('[E_] ELF 에 probe 심볼이 없다 — 시험 이미지를 빌드했나')


def dmi_write(v):
    return ['-c', 'irscan hpm5361.cpu 0x11',
            '-c', f'drscan hpm5361.cpu 2 0x2 32 {v:#x} 7 0x10', '-c', 'runtest 100']


def ocd(cmds, rescue=False):
    pre = ['-c', 'adapter speed 4000']
    if rescue:
        pre = (['-c', 'hpm5361.cpu0 configure -defer-examine', '-c', 'adapter speed 1000', '-c', 'init']
               + dmi_write(1) + dmi_write(0x80000003) + ['-c', 'sleep 100']
               + dmi_write(0x80000001) + ['-c', 'sleep 100', '-c', 'hpm5361.cpu0 arp_examine'])
    else:
        pre += ['-c', 'init']
    args = [OCD, '-s', os.path.join(FW, 'tools/openocd'), '-f', 'hpm5361-fw.cfg'] + pre
    for c in cmds:
        args += ['-c', c]
    args += ['-c', 'exit']
    r = subprocess.run(args, capture_output=True, text=True)
    if 'Error' in r.stderr and 'Unable to halt' in r.stderr:
        sys.exit('[E_] 코어가 정지를 안 받는다. --rescue 로 다시')
    return r.stderr


def read_probe(base):
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, 'probe.bin')
        ocd(['halt', f'dump_image {f} {base:#x} {SIZE}', 'resume'])
        if not os.path.exists(f):
            sys.exit('[E_] 덤프 실패')
        data = open(f, 'rb').read()
    hdr = struct.unpack('<8I', data[:HDR])
    arr = struct.unpack(f'<{3 * ADC * ADDR * CH}H', data[HDR:SIZE])
    n = ADC * ADDR * CH

    def cube(off):
        return [[[arr[off + (a * ADDR + s) * CH + c] for c in range(CH)]
                 for s in range(ADDR)] for a in range(ADC)]
    return hdr, cube(0), cube(n), cube(2 * n)


def table(title, cube):
    print(f'\n{title}')
    print('  addr ' + ''.join(f'{c:>6}' for c in range(CH)))
    for s in range(ADDR):
        print(f'    {s}  ' + ''.join(f'{cube[s][c]:>6}' for c in range(CH)))


def fingerprint(cur):
    """채널마다 0 이 되는 주소 집합으로 MAG 를 맞춘다."""
    print('\n채널 -> MAG (GND 무늬, ADC0 기준)')
    for c in range(CH):
        zeros = {s for s in range(ADDR) if cur[0][s][c] < ZERO_MAX}
        vals = [cur[0][s][c] for s in range(ADDR) if s not in zeros]
        hit = [m for m, z in GND_ADDR.items() if z == zeros]
        # 떠 있는 패드는 샘플 커패시터가 직전 채널 값을 물고 와서 주소마다 흘러내린다.
        # "0 이 없다" 만으로는 MAG1 과 못 가르므로 센서 출력 대역(VCC/2 근처)을 같이 본다.
        if len(zeros) == ADDR:
            what = '전부 0 — 연결 없음/GND'
        elif vals and (min(vals) < SENSOR_LO or max(vals) > SENSOR_HI):
            what = f'연결 없음 (떠 있다)  {min(vals)}~{max(vals)}'
        elif hit and vals:
            what = f'{hit[0]:9}  나머지 {min(vals)}~{max(vals)}'
        else:
            what = f'?  0 인 주소 {sorted(zeros)}  값 {min(vals) if vals else "-"}~{max(vals) if vals else "-"}'
        print(f'  ch{c:<3} {what}')


def main():
    args = [a for a in sys.argv[1:] if a != '--rescue']
    if '--rescue' in sys.argv:
        print(ocd(['reset halt', 'reset run'], rescue=True)[-300:])
    base = probe_addr()
    cmd = args[0] if args else 'show'

    if cmd == 'clear':
        ocd(['halt', f'mww {base + 20:#x} 1', 'resume'])
        print('min/max 를 다시 잡는다')
        return
    if cmd == 'settle':
        ocd(['halt', f'mww {base + 16:#x} {int(args[1])}', 'resume'])
        print(f'settle = {args[1]} nop')
        return

    hdr, cur, mn, mx = read_probe(base)
    magic, loop, cpu, adc, settle, _, err, tmo = hdr
    if magic != MAGIC:
        sys.exit(f'[E_] magic {magic:#x} — 시험 이미지가 안 돌고 있다')
    print(f'loop {loop}  cpu {cpu / 1e6:.0f}MHz  adc {adc / 1e6:.1f}MHz  '
          f'settle {settle}  adc_err {err:#x}  timeout {tmo}')

    if cmd == 'range':
        for a in range(ADC):
            table(f'ADC{a}  max - min', [[mx[a][s][c] - mn[a][s][c] for c in range(CH)]
                                        for s in range(ADDR)])
        return

    for a in range(ADC):
        table(f'ADC{a}  지금 값', cur[a])
    fingerprint(cur)


if __name__ == '__main__':
    main()
