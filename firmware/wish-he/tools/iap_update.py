#!/usr/bin/env python3
"""
IAP USB 업데이터 — JTAG 없이 USB 만으로 펌웨어를 교체한다.

상용 보드의 IAP 부트로더가 노출하는 HID 채널(64바이트 리포트)로 통신한다.
프로토콜은 덤프 분석으로 복원했다 — ../hpm5361-fw/docs/board-iap.md 3절.

    cmd 0x81  시작    기록 주소를 0x80020000 으로 고정, [4..7] = 총 길이(LE32)
    cmd 0x80  데이터  [1] = 길이, [4..] = 데이터. 4KB 페이지 버퍼에 누적
    cmd 0x82  기록    0xFF 로 패딩해 4KB 소거+기록, 주소 진행. 이어서 [4..] 누적
    cmd 0x83  종료    앱으로 점프

    응답은 항상 64바이트, [0]=0x85, [1]=1(성공)/0(실패)

의존성 없음 — brew 의 libhidapi 를 ctypes 로 직접 부른다.

사용법
    python3 iap_update.py --list                 연결된 HID 장치 나열
    python3 iap_update.py --info                 IAP 장치 찾기만
    python3 iap_update.py fw.bin                 펌웨어 기록
"""

import argparse
import ctypes
import ctypes.util
import struct
import sys
import time
from pathlib import Path


# ── IAP 프로토콜 ────────────────────────────────────────────────────────────
CMD_DATA  = 0x80
CMD_START = 0x81
CMD_FLUSH = 0x82
CMD_END   = 0x83

RSP_TAG   = 0x85
REPORT_LEN = 64
PAYLOAD_OFF = 4                       # [0]=cmd [1]=len [2..3]=미사용 [4..]=데이터
PAYLOAD_MAX = REPORT_LEN - PAYLOAD_OFF
PAGE_SIZE   = 4096                    # IAP 내부 페이지 버퍼 크기

APP_ADDR  = 0x80020000                # IAP 가 하드코딩한 기록 주소
APP_MAGIC = b"HPM\n"


# ── libhidapi (ctypes) ──────────────────────────────────────────────────────
class HidDeviceInfo(ctypes.Structure):
    pass


HidDeviceInfo._fields_ = [
    ("path", ctypes.c_char_p),
    ("vendor_id", ctypes.c_ushort),
    ("product_id", ctypes.c_ushort),
    ("serial_number", ctypes.c_wchar_p),
    ("release_number", ctypes.c_ushort),
    ("manufacturer_string", ctypes.c_wchar_p),
    ("product_string", ctypes.c_wchar_p),
    ("usage_page", ctypes.c_ushort),
    ("usage", ctypes.c_ushort),
    ("interface_number", ctypes.c_int),
    ("next", ctypes.POINTER(HidDeviceInfo)),
    # 신버전에 bus_type 이 붙지만 우리가 읽는 필드보다 뒤라 무시해도 된다
]


def load_hidapi():
    names = [
        "/opt/homebrew/lib/libhidapi.dylib",
        "/usr/local/lib/libhidapi.dylib",
        ctypes.util.find_library("hidapi"),
       ]
    for n in [x for x in names if x]:
        try:
            lib = ctypes.CDLL(n)
            break
        except OSError:
            continue
    else:
        sys.exit("libhidapi 를 찾지 못했다.  brew install hidapi")

    lib.hid_init.restype = ctypes.c_int
    lib.hid_enumerate.restype = ctypes.POINTER(HidDeviceInfo)
    lib.hid_enumerate.argtypes = [ctypes.c_ushort, ctypes.c_ushort]
    lib.hid_free_enumeration.argtypes = [ctypes.POINTER(HidDeviceInfo)]
    lib.hid_open_path.restype = ctypes.c_void_p
    lib.hid_open_path.argtypes = [ctypes.c_char_p]
    lib.hid_close.argtypes = [ctypes.c_void_p]
    lib.hid_write.restype = ctypes.c_int
    lib.hid_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.hid_read_timeout.restype = ctypes.c_int
    lib.hid_read_timeout.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                     ctypes.c_size_t, ctypes.c_int]
    lib.hid_error.restype = ctypes.c_wchar_p
    lib.hid_error.argtypes = [ctypes.c_void_p]
    lib.hid_init()
    return lib


def enumerate_devices(lib, vid=0, pid=0):
    out = []
    head = lib.hid_enumerate(vid, pid)
    cur = head
    while cur:
        d = cur.contents
        out.append({
            "path": d.path,
            "vid": d.vendor_id,
            "pid": d.product_id,
            "manufacturer": d.manufacturer_string or "",
            "product": d.product_string or "",
            "serial": d.serial_number or "",
            "usage_page": d.usage_page,
            "usage": d.usage,
            "interface": d.interface_number,
        })
        cur = d.next
    if head:
        lib.hid_free_enumeration(head)
    return out


# ── IAP 세션 ────────────────────────────────────────────────────────────────
class Iap:
    def __init__(self, lib, path, verbose=False):
        self.lib = lib
        self.verbose = verbose
        self.h = lib.hid_open_path(path)
        if not self.h:
            sys.exit(f"장치를 열지 못했다: {path!r}")

    def close(self):
        if self.h:
            self.lib.hid_close(self.h)
            self.h = None

    def _xfer(self, cmd, payload=b"", length=None, check=True):
        """64바이트 리포트를 보내고 응답을 받는다."""
        buf = bytearray(REPORT_LEN)
        buf[0] = cmd
        buf[1] = len(payload) if length is None else length
        buf[PAYLOAD_OFF:PAYLOAD_OFF + len(payload)] = payload

        # 리포트 ID 0 을 앞에 붙인다 (hidapi 규약)
        wire = b"\x00" + bytes(buf)
        n = self.lib.hid_write(self.h, wire, len(wire))
        if n < 0:
            raise IOError(f"hid_write 실패: {self.lib.hid_error(self.h)}")

        rbuf = ctypes.create_string_buffer(REPORT_LEN)
        n = self.lib.hid_read_timeout(self.h, rbuf, REPORT_LEN, 1000)
        if n <= 0:
            raise IOError(f"응답 없음 (cmd 0x{cmd:02X})")

        rsp = rbuf.raw[:n]
        if self.verbose:
            print(f"    cmd 0x{cmd:02X} -> tag 0x{rsp[0]:02X} status {rsp[1]}")
        if rsp[0] != RSP_TAG:
            raise IOError(f"응답 태그가 0x{rsp[0]:02X} (기대 0x{RSP_TAG:02X})")
        if check and rsp[1] != 1:
            raise IOError(f"cmd 0x{cmd:02X} 실패 (status {rsp[1]})")
        return rsp

    def start(self, total_len):
        self._xfer(CMD_START, struct.pack("<I", total_len), length=4)

    def data(self, chunk):
        self._xfer(CMD_DATA, chunk)

    def flush(self, chunk=b""):
        self._xfer(CMD_FLUSH, chunk)

    def end(self):
        """
        종료 — 앱으로 점프한다.

        IAP 의 0x83 분기는 status 를 1 로 세우지 않는다(코드상 nop 뿐이다).
        즉 status 0 이 정상이므로 검사하지 않는다.
        """
        self._xfer(CMD_END, check=False)


def program(iap, image, progress=True):
    """
    페이지를 정확히 4096 바이트로 채운 뒤 flush 한다.

    cmd 0x82 는 "현재 버퍼를 기록하고 나서 이 리포트의 데이터를 붙인다" 이므로,
    페이지를 채우는 마지막 청크를 0x82 에 실으면 안 된다 — 그러면 덜 찬 페이지가
    0xFF 로 패딩돼 기록되고 데이터가 밀린다.
    """
    total = len(image)
    iap.start(total)

    sent = 0
    in_page = 0
    while sent < total:
        n = min(PAYLOAD_MAX, total - sent, PAGE_SIZE - in_page)
        iap.data(image[sent:sent + n])          # 언제나 0x80 으로 누적
        sent += n
        in_page += n

        if in_page == PAGE_SIZE:                # 정확히 찼을 때만 기록
            iap.flush()                         # 0x82, 페이로드 없음
            in_page = 0

        if progress and (sent % (8 * 1024) == 0 or sent == total):
            pct = sent * 100 // total
            print(f"\r  {sent:7d} / {total} B  ({pct:3d}%)", end="", flush=True)

    if in_page:
        iap.flush()                             # 마지막 자투리 페이지
    if progress:
        print()
    iap.end()


# ── main ────────────────────────────────────────────────────────────────────
IAP_USAGE_PAGE = 0xFF53          # IAP 의 64바이트 업데이트 채널 (IF3)


def find_iap(devs):
    """IAP 업데이트 채널을 고른다. 0xFF53 을 우선하고, 없으면 벤더 페이지 전부."""
    exact = [d for d in devs if d["usage_page"] == IAP_USAGE_PAGE]
    if exact:
        return exact
    return [d for d in devs if d["usage_page"] >= 0xFF00]


def main():
    ap = argparse.ArgumentParser(description="IAP USB 업데이터")
    ap.add_argument("image", nargs="?", help="기록할 .bin (0x80020000 기준)")
    ap.add_argument("--list", action="store_true", help="HID 장치 전부 나열")
    ap.add_argument("--info", action="store_true", help="IAP 후보만 표시")
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--path", help="hidapi 경로를 직접 지정")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    lib = load_hidapi()
    devs = enumerate_devices(lib, args.vid, args.pid)

    if args.list or args.info:
        target = devs if args.list else find_iap(devs)
        if not target:
            print("해당하는 장치가 없다.")
            return
        for d in target:
            print(f"{d['vid']:04x}:{d['pid']:04x}  if={d['interface']:<2} "
                  f"usage={d['usage_page']:#06x}/{d['usage']:#04x}  "
                  f"{d['manufacturer']} {d['product']}")
            print(f"    path = {d['path'].decode(errors='replace')}")
        return

    if not args.image:
        ap.error("이미지 파일을 지정하거나 --list / --info 를 쓴다")

    image = Path(args.image).read_bytes()
    if image[:4] != APP_MAGIC:
        sys.exit(f"이미지 선두가 {APP_MAGIC!r} 가 아니다 — IAP 가 인식하지 못한다")

    if args.path:
        path = args.path.encode()
    else:
        cands = find_iap(devs)
        if not cands:
            sys.exit("IAP 장치를 찾지 못했다. 보드가 업데이트 모드인지 확인할 것 "
                     "(CLI 에서 'reset boot')")
        if len(cands) > 1:
            print("후보가 여러 개다. --path 로 지정할 것:")
            for d in cands:
                print("   ", d["path"].decode(errors="replace"))
            sys.exit(1)
        path = cands[0]["path"]

    print(f"이미지 : {args.image}  ({len(image)} B)")
    print(f"대상   : 0x{APP_ADDR:08X}")

    iap = Iap(lib, path, verbose=args.verbose)
    t0 = time.time()
    try:
        program(iap, image)
    finally:
        iap.close()
    print(f"완료 — {time.time() - t0:.1f}초")


if __name__ == "__main__":
    main()
