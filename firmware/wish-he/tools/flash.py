#!/usr/bin/env python3
"""wish61-he 벤더 IAP USB 플래셔.

    ./flash.py image.bin

앱 모드면 부트로더로 넘긴 뒤 굽고, 다 굽고 나면 앱으로 되돌린다.

프로토콜 — 벤더 HID 채널 usage_page 0xFFB0, 리포트 ID 0, IN/OUT 각 64 B

    08 01                                   AppToBoot
    08 02  addr(LE32) len(LE16) data(<=56)  Flash
    08 03                                   ValiDate   resp[2]==1 이면 성공
    08 04                                   BootToApp
    01 02                                   장치 정보   t[16]==255 이면 부트 모드

제약 (IAP 핸들러 0x80005094 / 0x80004CE2 에서 확인, 실측 일치)
  - 청크 최대 56 B
  - addr 은 **파일 오프셋 0 부터 순차**여야 한다. IAP 가 다음 기대 주소를 들고 있고
    불일치하면 거부한다. 임의 주소 쓰기는 불가능하다
  - 쓰기 대상은 0x80020000 + addr, 4KB 경계마다 소거된다
  - addr + len <= 0x80000

근거: firmware/hpm5361-fw/docs/wish61-he-flash-dump.md 8.3절
"""
import hid
import struct
import sys
import time

VID, PID, USAGE_PAGE = 0x1CA6, 0x300B, 0xFFB0    # 벤더 앱 / IAP 의 채널

# 우리 펌웨어. HID 를 걷어낸 상태라 0xFFB0 채널이 없다 —
# 부트로더로 넘기려면 CDC 로 `reset boot` 을 보내야 한다.
OUR_VID, OUR_PID = 0x0483, 0x5305
CHUNK = 56
FLASH_FAIL = 0x0FFF


def open_channel():
    for d in hid.enumerate(VID, PID):
        if d.get('usage_page') == USAGE_PAGE:
            h = hid.device()
            h.open_path(d['path'])
            h.set_nonblocking(0)
            return h, d.get('product_string')
    return None, None


def tx(h, payload, timeout=3000):
    """장치가 리셋 중이면 read/write 가 OSError 를 던진다 — 호출부가 재시도한다."""
    try:
        h.write(bytes([0x00]) + payload + b'\x00' * (64 - len(payload)))
        r = h.read(64, timeout_ms=timeout)
    except OSError:
        return None
    return bytes(r) if r else None


def run_mode(h):
    """01 02 장치 정보의 runModeVersion(t[16])을 읽는다. 255 = 부트로더, 0 = 앱.

    ★ resp[2] 로는 판별할 수 없다. 앱에서도 1 이 나온다 — 그걸로 판정했다가
      앱에 대고 1,207개 청크를 쏟아부은 적이 있다. 벤더 웹앱의 JS 도
      `runModeVersion !== 255` 로 AppToBoot 여부를 정한다.

    필드 배치 (벤더 JS 의 getDeviceInfoData)
        t[2] type   t[3] subType   t[4..7] boardId(BE)
        t[8..11] appVersion   t[12..15] pcbVersion
        t[16] runModeVersion   t[17..28] sn   t[29..40] timestamp
    """
    r = tx(h, bytes([0x01, 0x02]))
    return r[16] if r and len(r) > 16 else None


BOOT_MODE = 255


def our_cdc_port():
    """우리 펌웨어의 CDC 포트를 VID/PID 로 정확히 고른다.

    ★ 포트를 이름으로 넘겨짚으면 안 된다. 다른 보드(0483:5304)가 같이 꽂혀 있고,
      거기에 `reset boot` 을 보내면 그 보드가 자기 부트로더로 들어가 버린다.
    """
    try:
        import serial.tools.list_ports as lp
    except ImportError:
        return None
    for p in lp.comports():
        if p.vid == OUR_VID and p.pid == OUR_PID:
            return p.device
    return None


def enter_boot_via_cli():
    """우리 펌웨어가 돌고 있으면 CLI 로 부트로더에 넣는다."""
    port = our_cdc_port()
    if not port:
        return False
    try:
        import serial
    except ImportError:
        print("!! pyserial 이 없다 — CLI 로 `reset boot` 을 직접 쳐라")
        return False
    print(f"우리 펌웨어({OUR_VID:04x}:{OUR_PID:04x} @ {port}) — CLI 로 부트로더에 넣는다")

    """
    ★ "못 열었다" 와 "보내고 나서 끊겼다" 를 갈라야 한다.

      리셋이 걸리면 포트가 사라지므로 그때 나는 OSError 는 정상이다. 그런데 예전에는
      try 전체를 한 덩어리로 삼켜서, **포트를 아예 못 연 경우까지 성공으로 보고**
      True 를 돌려줬다. 그러면 호출자는 보내지도 않은 부트로더를 30초 기다린다 —
      "가끔 업데이트가 실패" 하던 것이 이것이었다.

      못 여는 것은 흔하다. screen 이 물고 있거나, 방금 끝난 도구가 포트를 놓는 데
      macOS 가 한 박자 걸린다. 그래서 몇 번 다시 시도하고, 그래도 안 되면 **왜**
      안 되는지 찍고 실패로 돌려준다.
    """
    sent = False
    for attempt in range(6):
        try:
            with serial.Serial(port, 115200, timeout=1) as s:
                time.sleep(0.2)
                s.write(b'reset boot\r\n')
                s.flush()
                sent = True
                time.sleep(0.5)   # 여기서 끊기는 것은 정상 — 이미 보냈다
        except OSError as e:
            if sent:
                break             # 보낸 뒤 끊긴 것이다
            if attempt == 0:
                print(f"   포트를 못 연다 ({e.__class__.__name__}: {e})")
                print("   screen 이나 다른 도구가 물고 있으면 닫을 것 — 다시 시도한다")
            time.sleep(1)
            continue
        break

    if not sent:
        print("!! `reset boot` 을 보내지 못했다. 한 바이트도 쓰지 않는다.")
        print(f"   {port} 를 쓰는 프로그램을 닫고 다시 실행할 것.")
    return sent


def enter_boot():
    """앱에 08 01 을 보내고 IAP 로 재열거되기를 기다린다.

    ★ 이 명령은 파괴적이다. 앱이 App1 헤더 페이지를 지우고 업데이트 표식을 쓴다.
      벤더 앱은 그 직전에 App1 → App2 백업을 돌리므로 실패해도 IAP 가 복구한다.
    """
    h, name = open_channel()
    if not h:
        # 벤더 채널이 없다 = 우리 펌웨어가 돌고 있거나 보드가 없다
        if not enter_boot_via_cli():
            print("!! 보드를 못 찾았다.")
            print("   벤더 앱이면 0xFFB0 채널이, 우리 펌웨어면 CDC(0483:5305)가 보여야 한다.")
            return False
    else:
        m = run_mode(h)
        if m == BOOT_MODE:
            h.close()
            return True
        print(f"벤더 앱({name!r}, runMode={m}) — 08 01 로 부트로더에 넣는다")
        tx(h, bytes([0x08, 0x01]))
        h.close()

    for _ in range(30):
        time.sleep(1)
        try:
            h, name = open_channel()
        except OSError:
            continue
        if h:
            m = run_mode(h)
            try:
                h.close()
            except OSError:
                pass
            if m == BOOT_MODE:
                print(f"  부트로더 진입 확인 ({name!r}, runMode={m})")
                return True
    print("!! 부트로더로 넘어가지 않았다")
    return False


def flash(h, img):
    t0 = time.time()
    total = len(img)
    retried = 0
    for off in range(0, total, CHUNK):
        blk = img[off:off + CHUNK]
        pkt = (bytes([0x08, 0x02]) + struct.pack('<I', off)
               + struct.pack('<H', len(blk)) + blk)

        """
        ★ 응답 하나를 놓쳤다고 굽기를 통째로 버리면 안 된다.

          청크가 3천 개다. USB 인터럽트 전송이 한 번만 미끄러져도 전체가 실패하고,
          그때 App1 은 **반쯤 쓰인 상태**로 남는다. 다음 부팅에서 CRC 가 어긋나
          IAP 가 App2(벤더)로 복구해 버린다 — "가끔 업데이트가 실패" 하던 것이 이거다.

          IAP 는 주소가 순차인지 검사한다(`*(gp+0x6e4)` 와 비교). 그래서 재시도의
          결과로 두 가지가 가능하다.

            응답이 옴          -> 이번에 들어갔다. 그대로 진행
            주소 거부가 옴     -> **앞 시도가 이미 들어갔고 응답만 잃은 것**이다.
                                  거부를 성공으로 읽고 넘어간다

          이 구분이 있어야 재시도가 안전하다. 무턱대고 다시 보내면 주소가 어긋난다.
        """
        r = tx(h, pkt)
        if r is None:
            r = tx(h, pkt, timeout=8000)          # 한 번 더, 넉넉히
            if r is not None and struct.unpack_from('<H', r, 2)[0] == FLASH_FAIL:
                r = None                          # 거부 = 앞 것이 들어갔다
                retried += 1
            elif r is None:
                print(f"\n!! 0x{off:06X} 두 번 다 무응답")
                print("   App1 이 반쯤 쓰인 상태다. 그대로 두면 다음 부팅에")
                print("   IAP 가 App2(벤더 펌웨어)로 복구한다 — 다시 구울 것.")
                return False
            else:
                retried += 1
        elif struct.unpack_from('<H', r, 2)[0] == FLASH_FAIL:
            print(f"\n!! 0x{off:06X} 거부 (resp={r[:8].hex(' ')})")
            return False
        if off % (CHUNK * 200) == 0 or off + CHUNK >= total:
            done = min(off + len(blk), total)
            print(f"\r  쓰는 중 {done * 100 // total:3}%  0x{off:06X}", end='', flush=True)
    print(f"\n  전송 완료 {time.time() - t0:.1f}s"
          + (f"  (재시도 {retried}회)" if retried else ""))
    return True


def main(argv):
    if len(argv) != 2:
        raise SystemExit(f"사용법: {argv[0]} <image.bin>")
    img = open(argv[1], 'rb').read()

    if not enter_boot():
        return 1
    h, name = open_channel()
    if not h:
        print("!! 0xFFB0 채널 없음")
        return 1
    m = run_mode(h)
    if m != BOOT_MODE:
        print(f"!! 부트 모드가 아니다 (runMode={m}). 한 바이트도 쓰지 않는다.")
        h.close()
        return 1
    print(f"장치: {name!r}   이미지: {len(img):,} B  "
          f"({(len(img) + CHUNK - 1) // CHUNK:,} 청크)")

    if not flash(h, img):
        print("   앱은 아직 안 살아났다. 이미지를 고쳐 다시 굽거나,")
        print("   ROM ISP + JTAG 로 복구한다 (문서 2절)")
        return 1

    r = tx(h, bytes([0x08, 0x03]), timeout=8000)
    print(f"ValiDate → {r[:8].hex(' ') if r else '무응답'}")
    if not r or r[2] != 1:
        print("!! 검증 실패 — BootToApp 을 보내지 않는다")
        return 1
    print("  검증 통과")

    tx(h, bytes([0x08, 0x04]), timeout=3000)
    print("BootToApp 전송 — 앱으로 복귀")
    h.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
