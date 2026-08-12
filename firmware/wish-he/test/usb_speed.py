#!/usr/bin/env python3
# USB CDC 송/수신 처리량 측정 스크립트 (pyserial 필요: pip install pyserial)
#
#   [테스트 흐름]
#     1) UART(SWD) CLI 로 접속해서 아래 명령 중 하나를 실행
#          usb tx   -> 보드가 USB 로 계속 송신 (PC 가 수신/측정)   => 이 스크립트 mode: tx
#          usb rx   -> 보드가 USB 를 계속 수신 (PC 가 송신/측정)   => 이 스크립트 mode: rx
#     2) PC 에서 이 스크립트 실행 (USB CDC 포트 지정)
#     3) 측정이 끝나면 UART CLI 에서 아무 키나 누르면 usb tx/rx 명령이 종료됨
#
#   [수동 실행]
#     python3 usb_speed.py /dev/cu.usbmodemXXXX tx           # 보드 송신 -> PC 수신
#     python3 usb_speed.py /dev/cu.usbmodemXXXX rx           # PC 송신 -> 보드 수신
#
#   [자동 실행] --cli 로 UART CLI 포트를 주면 usb 명령 실행/종료까지 자동 처리
#     python3 usb_speed.py /dev/cu.usbmodemXXXX tx --cli /dev/cu.usbserial-YYYY --secs 5
#
#   USB CDC 포트 찾기:  ls /dev/cu.usbmodem*   (mac)  /  ls /dev/ttyACM*  (linux)
#   UART CLI 포트     :  온보드 FT2232 의 VCP (mac: /dev/cu.usbserial-*)
#
#   주의: --baud 를 115200 으로 주면 보드의 cli_mgr 이 "터미널이 붙었다"고 판단해
#         CLI/로그 채널을 USB 로 옮긴다. 순수 처리량 측정에는 다른 값을 쓴다(기본 921600).
#   종료: Ctrl-C
import sys
import time
import argparse
import threading

try:
    import serial
except ImportError:
    print("pyserial 이 필요합니다:  pip install pyserial")
    sys.exit(1)


def human(bps):
    # bytes/sec -> 보기 좋은 단위 (비트레이트는 항상 Mbps)
    mbps = bps * 8 / 1e6
    if bps >= 1024 * 1024:
        return f"{bps / (1024*1024):6.2f} MB/s ({mbps:6.2f} Mbps)"
    return f"{bps / 1024:6.1f} KB/s ({mbps:6.2f} Mbps)"


# 무결성 검증용 증가 시퀀스(0,1,2,...,255,0,...). 미리 타일링해 슬라이스로 빠르게 생성.
_TILE = bytes(range(256)) * 512   # 128KB


def seq_bytes(start, length):
    s = start & 0xFF
    return _TILE[s:s + length]


def count_discont(data, exp_first):
    # data 가 exp_first 부터 증가 시퀀스인지 검사, 불연속(유실/손상) 지점 수를 반환
    errors = 0
    expv = exp_first
    for b in data:
        if b != expv:
            errors += 1
        expv = (b + 1) & 0xFF
    return errors


def cli_echo(cli_ser, stop_evt):
    # 보드가 CLI(UART) 로 출력하는 "tx : N KB/s" 등을 그대로 보여준다.
    buf = b""
    while not stop_evt.is_set():
        try:
            data = cli_ser.read(cli_ser.in_waiting or 1)
        except Exception:
            break
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            txt = line.decode(errors="replace").rstrip("\r")
            if txt:
                print(f"    [board] {txt}")


def measure_rx(ser, secs):
    # 보드 송신 -> PC 수신 + 무결성 검증 (usb tx)
    print(f"[tx] 보드 송신 수신+무결성 검증 {secs}s ...")
    total = 0
    errors = 0
    exp = None
    t0 = time.time()
    mark = t0
    mark_bytes = 0
    while time.time() - t0 < secs:
        data = ser.read(65536)   # timeout 내 도착분 반환
        if data:
            if exp is None:
                exp = data[0]
            if data != seq_bytes(exp, len(data)):   # 빠른 C 비교, 불일치 시에만 정밀 카운트
                errors += count_discont(data, exp)
            exp = (data[-1] + 1) & 0xFF
            total += len(data)
            mark_bytes += len(data)
        now = time.time()
        if now - mark >= 1.0:
            print(f"    {human(mark_bytes / (now - mark))}")
            mark = now
            mark_bytes = 0
    dt = time.time() - t0
    verdict = "유실 없음 OK" if errors == 0 else f"불연속(유실/손상) {errors} 건 !!"
    print(f"[tx] 평균: {human(total / dt)}   총 {total/1024:.1f} KB / {dt:.1f}s   무결성: {verdict}")


def measure_tx(ser, secs):
    # PC 송신(증가 시퀀스) -> 보드 수신/검증 (usb rx) : 보드측 err 카운트로 유실 확인
    print(f"[rx] PC 송신(증가 시퀀스) 측정 {secs}s ...")
    total = 0
    seq = 0
    t0 = time.time()
    mark = t0
    mark_bytes = 0
    while time.time() - t0 < secs:
        try:
            n = ser.write(seq_bytes(seq, 4096))
        except serial.SerialTimeoutException:
            n = 0                            # 보드 스로틀(NAK) 중 - 다음 루프에서 시간 체크 후 재시도
        if n:
            total += n
            mark_bytes += n
            seq = (seq + n) & 0xFF            # 실제 보낸 만큼 진행(연속성 유지)
        now = time.time()
        if now - mark >= 1.0:
            print(f"    {human(mark_bytes / (now - mark))}")
            mark = now
            mark_bytes = 0
    ser.flush()
    dt = time.time() - t0
    print(f"[rx] 평균: {human(total / dt)}   총 {total/1024:.1f} KB / {dt:.1f}s   (보드 'err' 카운트=유실)")


def _reader(ser, stop, ctr):
    while not stop.is_set():
        try:
            data = ser.read(65536)
        except Exception:
            break                      # 포트 닫힘/에러 시 조용히 종료
        if data:
            ctr["rx"] += len(data)


def _writer(ser, stop, ctr):
    chunk = bytes((i & 0xFF) for i in range(4096))
    while not stop.is_set():
        try:
            n = ser.write(chunk)
        except Exception:
            break                      # 포트 닫힘/에러 시 조용히 종료
        if n:
            ctr["tx"] += n


def measure_both(ser, secs):
    # 송신/수신 동시 (usb duplex) - 리더/라이터 스레드 병렬
    print(f"[both] 동시 송/수신 측정 {secs}s ...")
    ctr = {"tx": 0, "rx": 0}
    stop = threading.Event()
    rt = threading.Thread(target=_reader, args=(ser, stop, ctr), daemon=True)
    wt = threading.Thread(target=_writer, args=(ser, stop, ctr), daemon=True)

    t0 = time.time()
    rt.start()
    wt.start()
    last, last_tx, last_rx = t0, 0, 0
    while time.time() - t0 < secs:
        time.sleep(1.0)
        now = time.time()
        dt = now - last
        print(f"    tx {human((ctr['tx']-last_tx)/dt)}   rx {human((ctr['rx']-last_rx)/dt)}")
        last, last_tx, last_rx = now, ctr["tx"], ctr["rx"]

    stop.set()
    rt.join(timeout=3)
    wt.join(timeout=3)
    dt = time.time() - t0
    print(f"[both] 평균  tx {human(ctr['tx']/dt)}   rx {human(ctr['rx']/dt)}")


def main():
    ap = argparse.ArgumentParser(description="USB CDC 송/수신 처리량 측정")
    ap.add_argument("port", help="USB CDC 포트 (예: /dev/cu.usbmodem00011)")
    ap.add_argument("mode", choices=["tx", "rx", "both"],
                    help="tx=보드송신/PC수신(usb tx), rx=PC송신/보드수신(usb rx), both=동시(usb duplex)")
    ap.add_argument("--secs", type=float, default=5.0, help="측정 시간(초), 기본 5")
    ap.add_argument("--baud", type=int, default=921600,
                    help="CDC baud (기본 921600). 115200 을 주면 보드 CLI 가 USB 로 전환되므로 측정에는 부적합")
    ap.add_argument("--cli", default=None,
                    help="UART CLI 포트. 지정 시 usb 명령 실행/종료 자동 처리")
    args = ap.parse_args()

    cli_ser = None
    stop_evt = threading.Event()
    echo_th = None

    # --cli 지정: UART CLI 로 usb tx/rx 명령을 먼저 실행
    if args.cli:
        cli_ser = serial.Serial(args.cli, 115200, timeout=0.2)
        time.sleep(0.2)
        cli_ser.reset_input_buffer()
        cli_ser.write(b"\r")                      # 프롬프트 정리
        time.sleep(0.1)
        cmd = {"tx": "usb tx", "rx": "usb rx", "both": "usb duplex"}[args.mode]
        cli_ser.write(f"{cmd}\r".encode())        # 보드측 시험 명령 실행
        time.sleep(0.3)
        echo_th = threading.Thread(target=cli_echo, args=(cli_ser, stop_evt), daemon=True)
        echo_th.start()
    else:
        print(f"[준비] UART CLI 에서 'usb {args.mode}' 를 먼저 실행하세요. 3초 후 측정 시작...")
        time.sleep(3)

    # write_timeout 을 둬야 rx(PC 송신) 시 보드가 NAK 흐름제어로 스로틀할 때
    # ser.write 가 영원히 블로킹되어 측정 루프가 시간 체크를 못 하는 것을 방지한다.
    ser = serial.Serial(args.port, args.baud, timeout=1, write_timeout=1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    try:
        if args.mode == "tx":
            measure_rx(ser, args.secs)     # 보드 tx -> PC rx
        elif args.mode == "rx":
            measure_tx(ser, args.secs)     # PC tx -> 보드 rx
        else:
            measure_both(ser, args.secs)   # 동시
    except KeyboardInterrupt:
        print("\n중단됨")
    finally:
        ser.close()
        # usb tx/rx 명령 종료: CLI 채널에 아무 바이트나 보내면 cliKeepLoop 이 빠져나온다.
        if cli_ser is not None:
            cli_ser.write(b"\r")
            time.sleep(0.2)
            stop_evt.set()
            if echo_th:
                echo_th.join(timeout=1)
            cli_ser.close()


if __name__ == "__main__":
    main()
