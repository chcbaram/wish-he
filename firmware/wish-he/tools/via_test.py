#!/usr/bin/env python3
"""
VIA 프로토콜 점검 — 웹앱을 붙이기 전에 장치가 제대로 답하는지 본다.

QMK 의 quantum/via.c 가 응답한다. 우리 raw HID 채널(0xFF60)에서 우리가 모르는
명령을 넘겨받는 구조라, 여기서 답이 오면 배선이 맞은 것이다.

    python3 via_test.py            읽기만 (안전)
    python3 via_test.py --write    키코드 하나를 바꿨다 되돌린다
"""

import argparse
import struct
import sys
import time

from iap_update import (load_hidapi, enumerate_devices,
                        APP_USAGE_PAGE, APP_VID, APP_PID)


REPORT_LEN = 32

# quantum/via.h
ID_GET_PROTOCOL_VERSION            = 0x01
ID_GET_KEYBOARD_VALUE              = 0x02
ID_DYNAMIC_KEYMAP_GET_KEYCODE      = 0x04
ID_DYNAMIC_KEYMAP_SET_KEYCODE      = 0x05
ID_DYNAMIC_KEYMAP_MACRO_GET_COUNT  = 0x0C
ID_DYNAMIC_KEYMAP_MACRO_GET_BUFSZ  = 0x0D
ID_DYNAMIC_KEYMAP_GET_LAYER_COUNT  = 0x11
ID_UNHANDLED                       = 0xFF

# id_get_keyboard_value 의 하위
ID_UPTIME          = 0x01
ID_LAYOUT_OPTIONS  = 0x02
ID_SWITCH_MATRIX_STATE = 0x03
ID_FIRMWARE_VERSION    = 0x04


class Via:
    def __init__(self, lib, path):
        self.lib = lib
        self.h = lib.hid_open_path(path)
        if not self.h:
            sys.exit(f"장치를 열지 못했다: {path!r}")

    def close(self):
        if self.h:
            self.lib.hid_close(self.h)
            self.h = None

    def xfer(self, payload, timeout=1000):
        buf = bytes(payload) + b"\x00" * (REPORT_LEN - len(payload))
        if self.lib.hid_write(self.h, b"\x00" + buf, REPORT_LEN + 1) < 0:
            raise IOError("hid_write 실패")

        import ctypes
        rbuf = ctypes.create_string_buffer(REPORT_LEN)
        deadline = time.time() + timeout / 1000.0
        while time.time() < deadline:
            n = self.lib.hid_read_timeout(self.h, rbuf, REPORT_LEN, 200)
            if n <= 0:
                continue
            rsp = rbuf.raw[:n]
            # 트래킹 프레임(0xC4) 같은 자발 전송은 건너뛴다
            if rsp[0] == payload[0]:
                return rsp
        raise IOError(f"cmd 0x{payload[0]:02X} 응답 없음")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="키코드 쓰기까지 시험")
    args = ap.parse_args()

    lib = load_hidapi()
    devs = [d for d in enumerate_devices(lib)
            if d["usage_page"] == APP_USAGE_PAGE
            and d["vid"] == APP_VID and d["pid"] == APP_PID]
    if not devs:
        sys.exit("설정 채널을 찾지 못했다")

    v = Via(lib, devs[0]["path"])
    try:
        r = v.xfer([ID_GET_PROTOCOL_VERSION])
        proto = struct.unpack(">H", r[1:3])[0]
        print(f"프로토콜 버전 : {proto}")
        if r[0] == ID_UNHANDLED:
            sys.exit("[E_] via.c 가 응답하지 않는다 — raw HID 배선을 본다")

        r = v.xfer([ID_GET_KEYBOARD_VALUE, ID_FIRMWARE_VERSION])
        print(f"펌웨어 버전   : {struct.unpack('>I', r[2:6])[0]}")

        r = v.xfer([ID_GET_KEYBOARD_VALUE, ID_UPTIME])
        print(f"가동 시간     : {struct.unpack('>I', r[2:6])[0] / 1000:.1f} s")

        r = v.xfer([ID_DYNAMIC_KEYMAP_GET_LAYER_COUNT])
        layers = r[1]
        print(f"레이어 수     : {layers}")

        r = v.xfer([ID_DYNAMIC_KEYMAP_MACRO_GET_COUNT])
        print(f"매크로 수     : {r[1]}")

        r = v.xfer([ID_DYNAMIC_KEYMAP_MACRO_GET_BUFSZ])
        print(f"매크로 버퍼   : {struct.unpack('>H', r[1:3])[0]} B")

        # 스위치 매트릭스 상태 — 지금 눌린 키가 보인다
        r = v.xfer([ID_GET_KEYBOARD_VALUE, ID_SWITCH_MATRIX_STATE])
        rows = [r[2 + i] for i in range(8)]
        print(f"매트릭스      : {' '.join('%02X' % x for x in rows)}"
              f"   ({sum(bin(x).count('1') for x in rows)} 키 눌림)")

        # 레이어 0 의 첫 행 키코드
        print("\n레이어 0, s0 행 키코드")
        for col in range(8):
            r = v.xfer([ID_DYNAMIC_KEYMAP_GET_KEYCODE, 0, 0, col])
            kc = struct.unpack(">H", r[4:6])[0]
            print(f"  s0/ch{col} = 0x{kc:04X}")

        if args.write:
            print("\n쓰기 시험 — s0/ch0 을 KC_F13(0x68) 로 바꿨다 되돌린다")
            r = v.xfer([ID_DYNAMIC_KEYMAP_GET_KEYCODE, 0, 0, 0])
            orig = struct.unpack(">H", r[4:6])[0]

            v.xfer([ID_DYNAMIC_KEYMAP_SET_KEYCODE, 0, 0, 0, 0x00, 0x68])
            r = v.xfer([ID_DYNAMIC_KEYMAP_GET_KEYCODE, 0, 0, 0])
            now = struct.unpack(">H", r[4:6])[0]
            print(f"  0x{orig:04X} -> 0x{now:04X}   {'OK' if now == 0x68 else '[E_] 안 바뀜'}")

            v.xfer([ID_DYNAMIC_KEYMAP_SET_KEYCODE, 0, 0, 0,
                    (orig >> 8) & 0xFF, orig & 0xFF])
            r = v.xfer([ID_DYNAMIC_KEYMAP_GET_KEYCODE, 0, 0, 0])
            back = struct.unpack(">H", r[4:6])[0]
            print(f"  되돌림 0x{back:04X}   {'OK' if back == orig else '[E_] 복구 실패'}")
    finally:
        v.close()


if __name__ == "__main__":
    main()
