#!/usr/bin/env python3
"""
USB 전류계로 LED 전류 모델을 실측한다.

펌웨어의 전류 모델은 이런 모양이다 (src/hw/driver/ws2812.c).

    보드 전류 = WS2812_IDLE_MA + Σ(무리별 합 x 무리별 채널 전류 / 255)

밝기를 바꿔 가며 USB 전류를 재면 직선이 나오고, **절편이 고정분, 기울기가 채널당
전류**다. USB 쪽에서 재므로 레귤레이터 손실과 MCU·홀센서까지 전부 절편에 들어간다.
그게 데이터시트 값보다 낫다 — 실제로 500mA 예산을 먹는 것이 그 값이다.

★ **무리마다 따로 재야 한다.** 이 보드는 위쪽과 언더글로우가 서로 다른 LED 라
  채널 전류가 2.5배 차이 난다. 전체 측정에서 한쪽을 빼서 구하려 하면 안 된다 —
  큰 수의 차라 오차가 증폭돼 값이 흩어진다 (실제로 3.70~5.06 으로 흩어졌다).

  $ python3 tools/led_current.py

각 단계에서 전류계 값을 읽어 입력하면 된다. 그냥 엔터를 치면 그 점을 건너뛴다.

★ 리미터를 잠시 풀고 재므로 예상 전류가 SAFE_MA 를 넘는 단계는 만들지 않는다.
"""
import glob
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial 이 필요하다 — pip3 install pyserial")


LED_CNT   = 83
KEY_CNT   = 65          # ws2812.c 의 WS2812_KEY_CNT 와 같아야 한다 (실측 확정)
SAFE_MA   = 450         # 이 예상치를 넘는 단계는 아예 만들지 않는다
LIMIT_OFF = 60000       # 리미터를 사실상 끄는 값

# 예상 전류 계산에만 쓰는 현재 값 (uA/채널). 재고 나면 여기도 갱신한다.
UA_KEY    = 11510
UA_UNDER  = 4660


def open_cli():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("USB CDC 를 못 찾았다. /dev/tty.* 가 아니라 /dev/cu.* 여야 한다")
    p = serial.Serial(ports[0], 115200, timeout=0.3)
    p.dtr = True
    p.rts = True
    time.sleep(0.3)
    p.reset_input_buffer()
    return p


def cmd(p, s, wait=0.4):
    p.write((s + "\r\n").encode())
    time.sleep(wait)
    return p.read(16384).decode(errors="replace")


def ask_ma(label):
    while True:
        s = input(f"  {label:28s} mA = ").strip()
        if s == "":
            return None
        try:
            return float(s)
        except ValueError:
            print("    숫자를 넣거나 그냥 엔터")


def fit(points):
    """최소자승 직선. points = [(sum_units, mA), ...] -> (절편, 기울기)"""
    n = len(points)
    sx = sum(x for x, _ in points)
    sy = sum(y for _, y in points)
    sxx = sum(x * x for x, _ in points)
    sxy = sum(x * y for x, y in points)
    den = n * sxx - sx * sx
    if den == 0:
        return None, None
    slope = (n * sxy - sx * sy) / den
    inter = (sy - slope * sx) / n
    return inter, slope


def sweep(p, name, lo, hi, ua_guess, floor_ma, levels):
    """한 무리를 밝기별로 켜며 읽는다. -> [(합, mA), ...]"""
    cnt = hi - lo
    pts = []

    print(f"[{name}] {lo}~{hi - 1} ({cnt}개)")
    for lvl in levels:
        units = cnt * 3 * lvl
        est = floor_ma + units * ua_guess / 255 / 1000
        if est > SAFE_MA:
            print(f"  v={lvl:3d} 는 예상 {est:.0f} mA 라 건너뛴다")
            continue
        cmd(p, "ws2812 off")
        cmd(p, f"ws2812 range {lo} {hi - 1} {lvl} {lvl} {lvl}", wait=0.8)
        v = ask_ma(f"v={lvl} (합 {units})")
        if v is not None:
            pts.append((units, v))
    print()
    return pts


def report(name, pts, base):
    """절편을 base 로 고정하고 기울기만 뽑는다 (원점을 지나는 직선)."""
    use = [(x, y) for x, y in pts if x > 0]
    if not use:
        return None
    # Σ(x·Δy) / Σ(x²) — 최소자승, 절편 고정
    num = sum(x * (y - base) for x, y in use)
    den = sum(x * x for x, _ in use)
    slope = num / den
    ua = slope * 255 * 1000

    print(f"  {name}")
    print(f"    채널 풀스케일  {ua / 1000:.2f} mA   ({round(ua)} uA)")
    for x, y in use:
        print(f"      합 {x:6d}   실측 {y:6.1f}   모델 {base + slope * x:6.1f}"
              f"   차 {y - (base + slope * x):+5.1f}")
    return ua


def main():
    p = open_cli()

    print(__doc__.split("  $")[0].strip())
    print()
    print("전류계를 보드와 호스트 사이에 넣고, 값이 안정되면 읽는다.")
    print("CLI 도 같은 USB 라 통신 중에는 조금 흔들린다 — 명령 뒤 1초쯤 기다린다.")
    print()

    cmd(p, "ws2812 off")
    cmd(p, f"ws2812 limit {LIMIT_OFF}")     # 리미터를 풀어야 원본 그대로 나간다
    cmd(p, "ws2812 prio shared")

    print("[바탕] LED 전부 꺼짐")
    base = ask_ma("off")
    if base is None:
        sys.exit("바탕은 반드시 재야 한다 — 나머지가 전부 이 값 위에 얹힌다")
    print()

    key = sweep(p, "위쪽", 0, KEY_CNT, UA_KEY, base, (8, 12, 16, 20))
    und = sweep(p, "언더글로우", KEY_CNT, LED_CNT, UA_UNDER, base,
                (40, 80, 120, 160))

    # 섞인 경우 — 두 기울기가 맞으면 이것도 맞아야 한다
    print("[확인] 전체 (두 무리가 섞인 경우)")
    cmd(p, "ws2812 off")
    cmd(p, "ws2812 range 0 82 12 12 12", wait=0.8)
    mix = ask_ma("전체 v=12")
    print()

    cmd(p, "ws2812 off")
    cmd(p, "ws2812 limit 450")

    print("=" * 60)
    print(f"  고정분  {base:.1f} mA   (MCU·홀센서·USB + LED 칩 자체)")
    print()
    ua_k = report("위쪽", key, base)
    print()
    ua_u = report("언더글로우", und, base)
    print()

    if ua_k is None or ua_u is None:
        sys.exit("두 무리를 다 재야 값이 나온다")

    print("  ws2812.c 에 넣을 값")
    print(f"    #define WS2812_CH_FULL_UA_KEY    {round(ua_k):5d}")
    print(f"    #define WS2812_CH_FULL_UA_UNDER  {round(ua_u):5d}")
    print(f"    #define WS2812_IDLE_MA           {round(base):5d}")
    print()

    if mix is not None:
        pred = base + (KEY_CNT * 3 * 12 * ua_k + (LED_CNT - KEY_CNT) * 3 * 12 * ua_u) / 255 / 1000
        print(f"  섞인 경우 확인   실측 {mix:.0f} mA   모델 {pred:.0f} mA"
              f"   차 {mix - pred:+.0f}")
        print("    이게 크게 어긋나면 무리 경계(KEY_CNT)가 틀린 것이다")
        print()

    print("  ★ 고정분에 MCU 몫이 통째로 들어 있으므로 상한은 **보드 전체 전류**의")
    print("    상한이 된다. USB 선언 500mA 에서 여유를 두면 450 근처다.")
    print()
    for budget in (400, 450, 480):
        avail = (budget - base) * 1000
        print(f"    상한 {budget} mA 에서 흰색 채널당")
        for nm, cnt, ua in (("전체 83개", LED_CNT, None),
                            ("위쪽만", KEY_CNT, ua_k),
                            ("언더글로우만", LED_CNT - KEY_CNT, ua_u)):
            if ua is None:
                per = (KEY_CNT * 3 * ua_k + (LED_CNT - KEY_CNT) * 3 * ua_u) / 255
            else:
                per = cnt * 3 * ua / 255
            print(f"      {nm:14s} {int(avail / per) if per else 0}")


if __name__ == "__main__":
    main()
