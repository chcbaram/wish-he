#!/usr/bin/env python3
"""
라이브 트래킹 뷰어 — 눌린 깊이를 실제 배치대로 그린다.

장치가 raw HID 로 밀어내는 스냅샷을 받아 터미널에 표시한다. 요청-응답이 아니라
장치가 켜진 동안 계속 내보내는 구조라, 왕복 없이 폴링 주기가 곧 프레임 주기다.

    64키 x 4B(원시값 + 깊이) = 256B,  프레임당 7키  ->  스냅샷 10프레임
    bInterval 1 (125us)  ->  스냅샷 1.25ms = 초당 800장

배치는 장치에서 읽는다. JSON 파일이 없어도 되고, 펌웨어의 레이아웃이 바뀌면
따라간다.

의존성 없음 — brew 의 libhidapi 를 ctypes 로 직접 부른다.

사용법
    python3 he_track.py                깊이 바를 배치대로
    python3 he_track.py --raw          원시값도 같이
    python3 he_track.py --rate         스냅샷/초만 재고 끝
"""

import argparse
import ctypes
import struct
import sys
import time

from iap_update import load_hidapi, enumerate_devices, APP_USAGE_PAGE, APP_VID, APP_PID


REPORT_LEN = 32
HDR        = 4

# ★ 채널이 둘이다.
#
#   0xFF60  설정 채널 — 명령을 보내고 응답을 받는다 (VIA 와 같이 쓴다)
#   0xFF61  스트리밍  — 장치가 트래킹 프레임만 밀어낸다. IN 뿐이다
#
# 처음에는 하나로 썼는데, 트래킹 프레임이 VIA 의 요청-응답 짝에 끼어들었다.
TRK_USAGE_PAGE = 0xFF61

CMD_LAYOUT = 0xC2
CMD_TRACK  = 0xC3
EVT_TRACK  = 0xC4

PRESSED_BIT = 0x8000


class Device:
    def __init__(self, lib, path):
        self.lib = lib
        self.h = lib.hid_open_path(path)
        if not self.h:
            sys.exit(f"장치를 열지 못했다: {path!r}")

    def close(self):
        if self.h:
            self.lib.hid_close(self.h)
            self.h = None

    def write(self, buf):
        wire = b"\x00" + bytes(buf) + b"\x00" * (REPORT_LEN - len(buf))
        if self.lib.hid_write(self.h, wire, REPORT_LEN + 1) < 0:
            raise IOError(f"hid_write 실패: {self.lib.hid_error(self.h)}")

    def read(self, timeout_ms=1000):
        buf = ctypes.create_string_buffer(REPORT_LEN)
        n = self.lib.hid_read_timeout(self.h, buf, REPORT_LEN, timeout_ms)
        if n <= 0:
            return None
        return buf.raw[:n]

    def command(self, cmd, arg=0, timeout_ms=1000):
        # 응답은 [명령, 인자, ...] 로 시작한다 (VIA 규약과 같다)
        """명령을 보내고 그 명령의 응답을 받는다."""
        self.write(bytes([cmd, arg]))
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            rsp = self.read(int(timeout_ms))
            if rsp and rsp[0] == cmd:
                return rsp
        raise IOError(f"cmd 0x{cmd:02X} 응답 없음")


def read_layout(dev):
    """
    배치를 장치에서 읽는다.  {x, y, w, h, row, col} — 1/4 키유닛.

    개수를 따로 묻지 않는다. 끝을 넘겨 물으면 0개가 오므로 그때까지 인덱스를 늘린다.
    """
    out = []
    idx = 0
    while True:
        rsp = dev.command(CMD_LAYOUT, idx)
        n = rsp[2]
        if n == 0:
            break
        for k in range(n):
            o = 3 + k * 6
            x, y, w, h, row, col = rsp[o:o + 6]
            out.append({"x": x, "y": y, "w": w, "h": h, "row": row, "col": col})
        idx += n
        if idx > 255:
            break
    return out


def track(dev, on):
    """켜고/끄고, 전체 키 수 · 프레임당 키 수 · 전 행정(0.01mm)을 받는다."""
    rsp = dev.command(CMD_TRACK, 1 if on else 0)
    travel = struct.unpack_from("<H", rsp, 4)[0]
    return rsp[2], rsp[3], (travel or 400)


def collect(trk, key_cnt, state, timeout_ms=500):
    """
    스냅샷 한 장이 완성될 때까지 프레임을 모은다.

    프레임은 첫 키 인덱스를 달고 오므로 순서가 어긋나도 자리에 넣을 수 있다.
    인덱스가 0 으로 돌아오면 한 바퀴 돈 것이다.
    """
    seen_wrap = False
    while True:
        rsp = trk.read(timeout_ms)
        if rsp is None:
            return False
        if rsp[0] != EVT_TRACK:
            continue

        start, n = rsp[1], rsp[2]
        for k in range(n):
            o = HDR + k * 4
            raw, um = struct.unpack_from("<HH", rsp, o)
            i = start + k
            if i < key_cnt:
                state[i] = (raw, um & ~PRESSED_BIT, bool(um & PRESSED_BIT))

        if start == 0:
            if seen_wrap:
                return True
            seen_wrap = True
        if start + n >= key_cnt:
            return True


# ── 그리기 ──────────────────────────────────────────────────────────────────
CELL_W = 7          # 1 키유닛이 차지하는 칸 수

REV   = "\x1b[7m"   # 반전 — 막대의 채워진 부분
GREEN = "\x1b[32m"  # 눌림 판정
DIM   = "\x1b[2m"
OFF   = "\x1b[0m"


def cell_depth(um, pressed, travel, w):
    """
    막대와 숫자를 한 칸에 겹쳐 그린다.

    숫자를 오른쪽 정렬로 깔고 왼쪽부터 깊이만큼을 반전시킨다. 상용 디버깅 화면이
    막대와 mm 값을 같이 보여주는 것과 같은 모양인데, 칸을 더 쓰지 않는다.
    """
    if um <= 0:
        return DIM + "·".ljust(w) + OFF

    txt  = f"{um / 100:.2f}".rjust(w)[:w]
    fill = min(w, max(1, round(w * um / travel)))
    col  = GREEN if pressed else ""

    return col + REV + txt[:fill] + OFF + col + txt[fill:] + OFF


def render(layout, state, key_cnt, travel, show_raw):
    """
    배치대로 그린다. keys layout 과 같은 방식으로 오른쪽 끝을 맞춘다 —
    x 와 w 를 따로 내림하면 행마다 끝이 어긋난다.
    """
    rows = {}
    for e in layout:
        rows.setdefault(e["y"], []).append(e)

    out = []
    for y in sorted(rows):
        line = ""
        for e in sorted(rows[y], key=lambda a: a["x"]):
            left  = e["x"] * CELL_W // 4
            right = (e["x"] + e["w"]) * CELL_W // 4
            w     = max(4, right - left - 1)

            i = e["row"] * 8 + e["col"]
            if i >= key_cnt or state[i] is None:
                line += " " * (right - left)
                continue

            raw, um, pressed = state[i]
            if show_raw:
                cell = f"{raw:>{w}}"
                if pressed:
                    cell = GREEN + cell + OFF
            else:
                cell = cell_depth(um, pressed, travel, w)
            line += cell + " "
        out.append(line.rstrip())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw",  action="store_true", help="깊이 대신 원시값")
    ap.add_argument("--rate", action="store_true", help="스냅샷/초만 재고 끝")
    ap.add_argument("--vid",  type=lambda s: int(s, 0), default=APP_VID)
    ap.add_argument("--pid",  type=lambda s: int(s, 0), default=APP_PID)
    args = ap.parse_args()

    lib = load_hidapi()
    devs = [d for d in enumerate_devices(lib)
            if d["usage_page"] == APP_USAGE_PAGE
            and d["vid"] == args.vid and d["pid"] == args.pid]
    if not devs:
        sys.exit(f"설정 채널을 찾지 못했다 "
                 f"(usage page 0x{APP_USAGE_PAGE:04X}, "
                 f"VID {args.vid:04X} PID {args.pid:04X})")

    strm = [d for d in enumerate_devices(lib)
            if d["usage_page"] == TRK_USAGE_PAGE
            and d["vid"] == args.vid and d["pid"] == args.pid]
    if not strm:
        sys.exit(f"스트리밍 채널을 찾지 못했다 (usage page 0x{TRK_USAGE_PAGE:04X})")

    dev  = Device(lib, devs[0]["path"])     # 설정 — 명령
    trk  = Device(lib, strm[0]["path"])     # 스트리밍 — 프레임만
    try:
        layout = read_layout(dev)
        key_cnt, per_frame, travel = track(dev, True)
        print(f"키 {key_cnt}개, 프레임당 {per_frame}키, "
              f"배치 {len(layout)}자리, 전 행정 {travel/100:.2f}mm")

        state = [None] * key_cnt

        if args.rate:
            t0 = time.time()
            n = 0
            while time.time() - t0 < 3.0:
                if collect(trk, key_cnt, state):
                    n += 1
            dt = time.time() - t0
            print(f"스냅샷 {n}장 / {dt:.1f}s = {n/dt:.0f}/s")
            return

        print("\x1b[2J", end="")
        while True:
            if not collect(trk, key_cnt, state):
                continue
            lines = render(layout, state, key_cnt, travel, args.raw)
            sys.stdout.write("\x1b[H")
            for l in lines:
                sys.stdout.write("\x1b[K" + l + "\n")
            sys.stdout.flush()
            time.sleep(1 / 60)          # 화면은 60Hz 면 충분하다
    except KeyboardInterrupt:
        pass
    finally:
        try:
            track(dev, False)
        except Exception:
            pass
        dev.close()
        trk.close()
        print()


if __name__ == "__main__":
    main()
