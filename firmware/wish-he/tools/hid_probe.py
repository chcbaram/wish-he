#!/usr/bin/env python3
"""키보드 HID 인터페이스를 호스트에서 열 수 있나 — 권한 확인용.

    python3 tools/hid_probe.py          그냥
    sudo python3 tools/hid_probe.py     root 로

macOS 는 키보드 usage(0x0001/0x06) 를 TCC 로 막는다. 입력 모니터링 권한을 줘도
`0xE00002C1 privilege violation` 이 났다는 기록이 docs/07-keyboard.md 에 있다.
root 로 열리는지가 갈림길이다 — 열리면 리포트를 직접 세어 유실을 잴 수 있고,
안 열리면 장치가 스스로 쳐 넣고 받은 글자를 비교하는 길로 가야 한다.
"""
import sys, hid

VID, PID = 0x0483, 0x5304

for d in sorted(hid.enumerate(VID, PID), key=lambda x: x.get('interface_number', 0)):
    up, us, itf = d.get('usage_page', 0), d.get('usage', 0), d.get('interface_number')
    kind = {0x0006: "키보드", 0x0002: "마우스", 0x0080: "시스템",
            0x0001: "포인터"}.get(us, "") if up == 1 else f"벤더 0x{up:04X}"
    h = hid.device()
    try:
        h.open_path(d['path'])
        h.set_nonblocking(1)
        r = h.read(64, timeout_ms=300)
        print(f"if {itf}  up 0x{up:04X} us 0x{us:02X}  {kind:8s} : 열림 "
              f"{'— ' + str(len(r)) + 'B 수신' if r else '(조용함)'}")
        h.close()
    except Exception as e:
        print(f"if {itf}  up 0x{up:04X} us 0x{us:02X}  {kind:8s} : 실패 — {e}")
