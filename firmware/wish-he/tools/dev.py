#!/usr/bin/env python3
"""
장치에 직접 붙어 확인하는 도구 — CLI(CDC) 와 설정 채널(HID) 양쪽.

    python3 tools/dev.py cli "keys info" "keys prof"     CLI 명령을 던진다
    python3 tools/dev.py stat                            진단 통계를 읽는다
    python3 tools/dev.py hw                              하드웨어·펌웨어 제원
    python3 tools/dev.py ver                             지금 올라간 펌웨어 버전
    python3 tools/dev.py burst                           대량 왕복 — 응답 소실 시험

★ 이 도구가 왜 있나.

  "굳는다", "값이 0 이다" 같은 증상은 원인을 안 알려준다. 웹 도구를 통해서만 보면
  펌웨어가 잘못한 것인지 앱이 잘못한 것인지 가를 수가 없어, 짐작으로 고치다 두 번
  빗나갔다.

  직접 붙으면 한 번에 갈린다. 실제로 이렇게 잡았다 —

    · 프로파일 전환 뒤 응답이 사라지던 것
      전환 없이 400 왕복은 멀쩡, 저장이 도는 전환 뒤에는 7번째에서 소실.
      원인은 플래시 쓰기(6ms, 인터럽트 꺼짐) 중 놓친 전송 완료였다

    · 통계가 0 으로 나오던 것
      page 0 과 1 이 같은 데이터를 돌려주는 것을 보고 즉시 알았다.
      한 프레임 32바이트에 값 14개를 담으려다 버퍼를 넘겨 쓰고 있었다

  짐작을 얹기 전에 이걸 먼저 돌릴 것.

필요한 것:  pip3 install pyserial hidapi
"""

import sys
import time

VID, PID = 0x0483, 0x5304
USAGE_CFG = 0xFF60          # VIA 설정 채널
PORT = "/dev/cu.usbmodem00015"


# ── CLI (CDC) ────────────────────────────────────────────────────────────

def cli(cmds):
    import serial

    s = serial.Serial(PORT, 115200, timeout=0.4)
    time.sleep(0.3)
    s.reset_input_buffer()

    for c in cmds:
        s.write((c + "\r\n").encode())
        time.sleep(1.2)
        print(f"$ {c}")
        print(s.read(20000).decode("utf-8", "replace").strip())
        print("-" * 60)
    s.close()


# ── 설정 채널 (HID) ──────────────────────────────────────────────────────

def _open():
    import hid

    paths = [d["path"] for d in hid.enumerate(VID, PID)
             if d.get("usage_page") == USAGE_CFG]
    if not paths:
        sys.exit("[E_] 0xFF60 인터페이스를 못 찾았다 — 장치가 붙어 있나?")

    h = hid.device()
    h.open_path(paths[0])
    h.set_nonblocking(0)
    return h


def _cmd(h, data, timeout=1500):
    """한 번 왕복. 응답이 없으면 None — 그 자체가 정보다."""
    h.write([0x00] + data + [0] * (32 - len(data)))
    t0 = time.time()
    r = h.read(32, timeout_ms=timeout)
    return r, (time.time() - t0) * 1000


def _s(r, a, b):
    return bytes(x for x in r[a:b] if x).decode("ascii", "replace")


def _u32(r, off, i):
    o = off + i * 4
    return r[o] | r[o + 1] << 8 | r[o + 2] << 16 | r[o + 3] << 24


def ver():
    h = _open()
    r, _ = _cmd(h, [0xC0])
    print(f"보드 {_s(r, 1, 16)}   버전 {_s(r, 16, 32)}")
    h.close()


def stat():
    """
    진단 통계. 한 프레임이 32바이트라 페이지로 나눠 온다 — 페이지당 LE32 일곱 개.
    """
    names = ["scan_us", "scan_max", "scan_over", "scan_cnt", "timeout",
             "cal_ms", "calibrated", "task_us", "task_max", "task_avg",
             "task_over", "task_cnt", "rgb_max", "rgb_avg"]
    h = _open()
    v = []
    for page in (0, 1):
        r, _ = _cmd(h, [0xC9, 0, page])
        v += [_u32(r, 4, i) for i in range(7)]
    for n, x in zip(names, v):
        print(f"  {n:12s} {x:,}")
    h.close()


def hw():
    names = ["clock", "fw_size", "app_begin", "app_size", "eeprom",
             "cal_addr", "set_addr", "keys", "rows", "cols", "layers", "leds"]
    h = _open()
    v = []
    for page in (0, 1):
        r, _ = _cmd(h, [0xCA, 0, page])
        v += [_u32(r, 4, i) for i in range(7)]
    for n, x in zip(names, v):
        hexed = f"  (0x{x:X})" if ("addr" in n or "begin" in n) else ""
        print(f"  {n:10s} {x:,}{hexed}")
    print("  mcu       ", _s(_cmd(h, [0xCA, 0, 2])[0], 4, 32))
    print("  author    ", _s(_cmd(h, [0xCA, 0, 3])[0], 4, 32))
    h.close()


def burst():
    """
    프로파일을 옮겨 가며 대량으로 왕복한다 — 응답이 사라지는지 보는 시험.

    ★ 전환 직후가 위험하다. 전환은 플래시에 "지금 몇 번" 을 남기고, 그 6ms 동안
      인터럽트가 꺼져 있다. 그때 전송 완료를 놓치면 응답 하나가 조용히 사라진다.
    """
    h = _open()
    fails = 0

    for p in range(4):
        r, dt = _cmd(h, [0xC8, 1, p])
        print(f"\n프로파일 {p + 1} 전환 : {'OK' if r else 'TIMEOUT'} {dt:.0f} ms")

        n = 0
        for i in range(64):                       # 키 설정
            r, _ = _cmd(h, [0xC5, 0x00, i])
            if not r:
                fails += 1
                print(f"   !! 키 {i} 응답 없음")
                break
            n += 1
        for off in range(0, 1024, 28):            # 키맵 8레이어
            r, _ = _cmd(h, [0x12, (off >> 8) & 0xFF, off & 0xFF, 28])
            if not r:
                fails += 1
                print(f"   !! 키맵 off {off} 응답 없음")
                break
            n += 1
        print(f"   왕복 {n} 회 완료")

    print(f"\n실패 {fails}건" + ("  ← 정상" if fails == 0 else "  ← 파고들 것"))
    h.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    what = sys.argv[1]
    if what == "cli":
        cli(sys.argv[2:] or ["keys info"])
    elif what == "stat":
        stat()
    elif what == "hw":
        hw()
    elif what == "ver":
        ver()
    elif what == "burst":
        burst()
    else:
        sys.exit(__doc__)
