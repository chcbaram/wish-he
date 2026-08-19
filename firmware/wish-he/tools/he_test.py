#!/usr/bin/env python3
"""
단계별 기능 시험 — 고친 것이 다시 깨지지 않는지 본다.

    python3 tools/he_test.py              전부
    python3 tools/he_test.py drift perf   무리만 골라서
    python3 tools/he_test.py -v           장치 출력까지

★ 왜 있나.

  성능 비교(`qmk info` / `keys time`)는 "느려졌나" 만 본다. **오입력과 키씹힘은
  느려지지 않아도 난다.** 실제로 기준값 드리프트가 얕은 설정에서 키를 저절로 떼던
  버그가 성능 지표에는 한 글자도 안 나타났다.

  그리고 손으로 재현하면 그때뿐이다. 고칠 때 한 번 확인하고 잊어버리면, 반년 뒤
  상수 하나를 만졌을 때 조용히 되살아난다. **재현 절차를 코드로 굳혀 둔다.**

★ `keys inject` 가 있어서 가능하다.

  실제 키를 안 누르고 값을 갈아 끼우므로 사람 손이 필요 없다. 한 스캔짜리 글리치,
  정확한 깊이 유지 — 손가락으로는 하나도 못 만드는 조건이다.

★ 상태를 반드시 되돌린다.

  시험이 설정을 바꾸므로 끝나면 원래대로 놓는다. 안 그러면 사용자가 쓰던 입력지점이
  조용히 바뀐 채로 남아, 다음에 칠 때 감이 달라져도 원인을 못 짚는다.
  (`dev.py burst` 가 프로파일을 4번에 남겨 두고 끝나던 것과 같은 잘못이다.)

★ 아직 안 고친 것은 xfail 로 둔다.

  늘 빨간 검사는 아무도 안 본다. 알려진 문제는 "알려진 대로 재현됨" 이 통과이고,
  **재현이 안 되면 그때 알려 준다** — 고쳐졌거나, 조건이 바뀐 것이다.

필요한 것:  pyserial (dev.py 와 같다).  장치가 붙어 있어야 한다.
"""

import re
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dev

VERBOSE = False

CELL_ST, CELL_CH = 0, 0          # 시험에 쓸 셀
DRIFT_TICK_MS    = 512           # keys.c 의 KEYS_DRIFT_MS
DRIFT_STEP       = 3             # KEYS_DRIFT_STEP
DRIFT_BAND       = 150           # KEYS_DRIFT_BAND
LATCH_JUMP       = 93            # KEYS_LATCH_JUMP


# ── 장치 말 걸기 ─────────────────────────────────────────────────────────

def say(*cmds, wait=1.2):
    out = dev.cli_get(list(cmds), wait)
    if VERBOSE:
        for c, o in out:
            print(f"      $ {c}\n      " + o.replace("\n", "\n      "))
    return "\n".join(o for _, o in out)


def depth():
    """주입 중인 셀의 지금 깊이와 눌림 여부."""
    m = re.search(r"s%d/ch%d\s+raw\s+(-?\d+)\s+->\s+깊이\s+(-?\d+)\s+카운트(\s+\[눌림\])?"
                  % (CELL_ST, CELL_CH), say("keys inject"))
    if not m:
        return None, None
    return int(m.group(2)), (m.group(3) is not None)


def counters():
    m = re.search(r"latch (\d+),\s+drift 위 (\d+) / 아래 (\d+)", say("keys info"))
    return tuple(int(x) for x in m.groups()) if m else (None, None, None)


def cfg_um():
    o = say("keys cfg")
    p = re.search(r"press\s+:\s+(\d+)\.(\d+) mm", o)
    r = re.search(r"release\s+:\s+(\d+)\.(\d+) mm", o)
    return (int(p.group(1)) * 100 + int(p.group(2)),
            int(r.group(1)) * 100 + int(r.group(2)))


# ── 시험 틀 ──────────────────────────────────────────────────────────────

TESTS = []


def test(group, name, xfail=False):
    def deco(fn):
        TESTS.append((group, name, fn, xfail))
        return fn
    return deco


# ── 1. 연결 · 기본 상태 ──────────────────────────────────────────────────

@test("link", "장치가 붙고 보정돼 있다")
def t_link():
    o = say("keys info")
    if "keys init   : 1" not in o:
        return "keysInit 이 실패한 상태다"
    if "calibrated  : 1" not in o:
        return "보정이 안 됐다 — 이 상태에서는 판정이 아예 안 돈다"
    return None


@test("link", "ADC 타임아웃이 없다")
def t_timeout():
    m = re.search(r"timeout\s+:\s+(\d+)", say("keys info"))
    n = int(m.group(1))
    # 타임아웃이 나면 그 스텝의 pressed[] 가 직전 값으로 얼어붙는다 — 눌린 채 굳는다
    return None if n == 0 else f"timeout {n} 회 — 눌린 채 굳었을 수 있다"


# ── 2. 주입 자체 ─────────────────────────────────────────────────────────

@test("inject", "주입이 판정까지 간다")
def t_inject():
    say("keys inject %d %d d 150" % (CELL_ST, CELL_CH))
    d, pressed = depth()
    if d is None:
        return "주입 셀이 안 보인다"
    if not pressed:
        return f"깊이 {d} 인데 눌림으로 안 잡힌다"
    say("keys inject off")
    return None


@test("inject", "주입을 끄면 원래대로 돌아온다")
def t_inject_off():
    say("keys inject %d %d d 150" % (CELL_ST, CELL_CH), "keys inject off")
    if "주입 중인 셀 없다" not in say("keys inject"):
        return "해제했는데 셀이 남아 있다"
    return None


# ── 3. 기준값 드리프트 ───────────────────────────────────────────────────

@test("drift", "눌린 키의 기준값은 안 내려간다 (A1)")
def t_drift_hold():
    """
    고치기 전에는 입력지점 0.30mm 에서 깊이가 116 -> 14 로 줄며 18초에 저절로
    해제됐다. 주입값은 한 번도 안 바뀌었으니 손가락은 그대로였던 셈이다.
    """
    say("keys cfg press 30", "keys cfg release 20")
    say("keys inject %d %d d 40" % (CELL_ST, CELL_CH))

    d0, p0 = depth()
    if d0 is None or not p0:
        return f"시작부터 안 눌렸다 (깊이 {d0})"
    if d0 >= DRIFT_BAND:
        return f"깊이 {d0} 가 밴드({DRIFT_BAND}) 밖이라 시험이 안 된다"

    time.sleep(8)                       # 16 틱 = 최대 48 카운트가 움직일 시간
    d1, p1 = depth()

    if not p1:
        return f"8초 만에 저절로 떼졌다 ({d0} -> {d1})"
    if abs(d1 - d0) > DRIFT_STEP:
        return f"눌린 채로 기준값이 움직였다 ({d0} -> {d1})"
    return None


@test("drift", "안 눌린 셀의 드리프트는 계속 돈다")
def t_drift_alive():
    """A1 을 고치면서 드리프트를 통째로 죽이지 않았는지 본다."""
    _, up0, dn0 = counters()
    time.sleep(4)
    _, up1, dn1 = counters()

    if up1 == up0 and dn1 == dn0:
        return "드리프트가 아예 안 돈다 — 온도 추종이 죽었다"
    if dn1 == dn0:
        return "하향 드리프트만 안 돈다"
    return None


# ── 3-b. 문턱 방어 ───────────────────────────────────────────────────────

KEYCFG_OFF = 3          # hid_if.h 의 HID_KEYCFG_OFF
KEYCFG_LEN = 14         # HID_KEYCFG_LEN


def keycfg(idx, vals=None):
    """키별 설정을 웹앱과 같은 길(0xC5)로 읽고 쓴다."""
    h = dev._open()
    try:
        if vals is not None:
            dev._cmd(h, [0xC5, 0x01, idx] + list(vals))
            time.sleep(0.3)
        r, _ = dev._cmd(h, [0xC5, 0x00, idx])
        return list(r[KEYCFG_OFF:KEYCFG_OFF + KEYCFG_LEN])
    finally:
        h.close()


@test("guard", "해제지점 0 을 쏴도 키가 떨어진다 (A3)")
def t_release_zero():
    """
    ★ 웹앱이 쓰는 길에는 방어가 없었다.

      전역 setter 는 0 을 막는데 키별 명령(0xC5)은 안 막았고, press_um 이 0 이면
      순서 보정까지 건너뛰어져 t->release 가 0 으로 남았다. d 는 0 미만으로 안
      내려가므로 `d < 0` 이 영원히 거짓 — 손을 다 떼도 안 떨어졌다.
    """
    idx = CELL_ST * 8 + CELL_CH
    orig = keycfg(idx)

    try:
        bad = list(orig)
        bad[0] = bad[1] = 0            # press_um  = 0
        bad[2] = bad[3] = 0            # release_um = 0
        keycfg(idx, bad)

        say("keys inject %d %d d 150" % (CELL_ST, CELL_CH))
        d, pressed = depth()
        if not pressed:
            return f"누름부터 안 잡힌다 (깊이 {d})"

        say("keys inject %d %d d 0" % (CELL_ST, CELL_CH))
        time.sleep(0.5)
        d, pressed = depth()
        if pressed:
            return f"손을 뗐는데(깊이 {d}) 안 떨어진다 — 해제지점이 0 이다"
        return None
    finally:
        # ★ 순서가 중요하다 — 설정을 먼저 되돌리고 주입을 나중에 끈다.
        #
        #   반대로 하면 그 사이 몇 초 동안 **해제지점이 0 인 채로 리포트가 살아난다.**
        #   주입 중에는 keysIsReportEnabled() 가 막아 주지만 끄는 순간 풀린다.
        #   실제로 이 순서 때문에 스턱 키가 호스트로 새어 나갔다.
        keycfg(idx, orig)
        say("keys inject off")


# ── 4. 기준값 래치 ───────────────────────────────────────────────────────

@test("latch", "상향 글리치가 기준값을 영구히 끌어올린다 (A2)", xfail=True)
def t_latch_stuck():
    """
    ★ 아직 안 고쳤다. **재현되는 것이 지금은 통과**다.

      +100/표본 글리치 뒤 정상값으로 되돌려도 깊이가 안 줄어든다. d 가 밴드 밖이라
      하향 보정이 아예 안 돌기 때문이다. 회복은 재보정뿐이다.
    """
    try:
        say("keys inject %d %d d 0" % (CELL_ST, CELL_CH))
        base_raw = int(re.search(r"raw\s+(\d+)", say("keys inject")).group(1))

        say("keys inject %d %d %d" % (CELL_ST, CELL_CH, base_raw + 100))
        time.sleep(2)
        say("keys inject %d %d %d" % (CELL_ST, CELL_CH, base_raw))

        d0, _ = depth()
        time.sleep(6)
        d1, _ = depth()

        if d0 is None or d0 < DRIFT_BAND:
            return f"글리치가 밴드를 못 넘겼다 (깊이 {d0}) — 시험 조건이 안 됐다"
        if abs(d1 - d0) > DRIFT_STEP:
            return f"기준값이 회복됐다 ({d0} -> {d1}) — 고쳐졌나?"
        return None
    finally:
        # ★ 이 시험은 **일부러 기준값을 갇히게 만든다.** 되돌리지 않으면 그 키가
        #   실물에서도 눌린 것으로 남는다 — 실제로 X 키가 0.9mm 눌린 채로 남았다.
        #   회복 수단이 재보정뿐인 것이 이 버그의 성질이므로 여기서 재보정한다.
        say("keys inject off")
        say("keys base", wait=3.0)


# ── 4-b. 뒷정리 확인 ─────────────────────────────────────────────────────

@test("clean", "시험이 끝난 뒤 눌린 키가 없다")
def t_no_stuck():
    """
    ★ 시험 자체가 스턱 키를 만들 수 있다.

      주입 중에는 리포트가 막히지만 끄는 순간 풀린다. 그때 문턱이 이상한 상태로
      남아 있으면 그 키가 눌린 채 호스트로 나간다 — 터미널에 글자가 쏟아진다.
      실제로 한 번 냈다. 마지막에 반드시 확인한다.
    """
    say("keys inject off")
    a = re.search(r"EXK\s+ready \d+\s+sent (\d+)", say("usb stat"))
    time.sleep(2.5)
    b = re.search(r"EXK\s+ready \d+\s+sent (\d+)", say("usb stat"))
    if a and b and int(b.group(1)) != int(a.group(1)):
        return "아무도 안 치는데 리포트가 나간다 — 키가 눌린 채로 남았다"
    return None


@test("clean", "기준값이 실제 값에 붙어 있다")
def t_base_sane():
    """
    ★ "리포트가 안 나간다" 로는 못 잡는다.

      기준값이 295 카운트 갇혀 있어도 입력지점(334)을 안 넘으면 리포트는 조용하다.
      그런데 화면에는 **0.9mm 눌린 것으로** 보인다. 실제로 그렇게 남긴 적이 있다.

      기준값(keys key)과 실제 값(keys dump)을 직접 견준다. 아무도 안 누르고 있으면
      둘이 잡음 폭 안에서 붙어 있어야 한다.
    """
    m = re.search(r"기준값 (\d+)", say("keys key %d %d" % (CELL_ST, CELL_CH)))
    if not m:
        return "기준값을 못 읽었다"
    base = int(m.group(1))

    o = say("keys dump", wait=3.0)
    row = re.search(r"^\s+s%d\s+(.+)$" % CELL_ST, o, re.M)
    if not row:
        return "keys dump 를 못 읽었다"
    raw = int(row.group(1).split()[CELL_CH])

    d = base - raw
    # 실측 잡음 p-p 가 누적 눈금 44 다. 그 두 배까지는 정상으로 본다
    if abs(d) > 90:
        return f"기준값이 {d} 카운트 떠 있다 (기준 {base}, 실제 {raw}) — 갇혔다"
    return None


# ── 5. 성능 ──────────────────────────────────────────────────────────────

@test("perf", "스캔이 예산 안이다")
def t_perf():
    m = re.search(r"keysUpdate : (\d+) us", say("keys time", wait=2.0))
    us = int(m.group(1))
    # 8kHz 예산 125us. 실측 26us 라 두 배까지는 여유로 본다
    return None if us <= 50 else f"keysUpdate {us}us — 예산 대비 너무 크다"


@test("perf", "태스크 평균이 예산 안이다")
def t_task():
    """
    ★ 125us 초과 **횟수**로는 판정하지 않는다.

      플래시 쓰기(설정 저장·프로파일 전환·굽기)는 XIP 라 인터럽트를 막아야 하고,
      그동안 태스크가 통째로 멎는다. 시험 자체가 설정을 쓰므로 이 시험이 그 초과를
      만들어 낸다 — 그걸로 판정하면 늘 빨간불이다.

      평균과 최대를 본다. 평균이 늘면 진짜 회귀다.
    """
    o = say("qmk info")
    m = re.search(r"keyboard_task : last \d+ us, avg (\d+) us, max (\d+) us", o)
    if not m:
        return "qmk info 를 못 읽었다"
    avg, mx = int(m.group(1)), int(m.group(2))
    if avg > 10:
        return f"태스크 평균 {avg}us — 실측 2us 대비 너무 크다"
    if mx > 2000:
        return f"태스크 최대 {mx}us — 플래시 쓰기(약 1.7ms)보다 크다"
    return None


# ── 표로 찍기 ────────────────────────────────────────────────────────────

def w(t):
    """
    화면에서 차지하는 칸 수. 한글은 두 칸이다.

    ★ len() 으로 맞추면 표가 어긋난다. 한글 항목 이름을 쓰는 한 피할 수 없다.
    """
    n = 0
    for ch in t:
        o = ord(ch)
        n += 2 if (0x1100 <= o <= 0x115F or 0x2E80 <= o <= 0xA4CF
                   or 0xAC00 <= o <= 0xD7A3 or 0xF900 <= o <= 0xFAFF
                   or 0xFE30 <= o <= 0xFE6F or 0xFF00 <= o <= 0xFF60
                   or 0xFFE0 <= o <= 0xFFE6) else 1
    return n


def pad(t, n):
    return t + " " * max(0, n - w(t))


COL_G, COL_N, COL_R = 8, 46, 14

MARK = {
    "ok":    "OK",
    "bad":   "[E_]",
    "xfail": "알려진 문제",
    "xpass": "[E_] 안 남",
}


def row(g, name, mark, secs):
    print(f"  {pad(g, COL_G)}{pad(name, COL_N)}{pad(MARK[mark], COL_R)}{secs:5.1f}s")


def rule():
    print("  " + "─" * (COL_G + COL_N + COL_R + 6))


# ── 돌리기 ───────────────────────────────────────────────────────────────

def main():
    global VERBOSE
    args = [a for a in sys.argv[1:] if a != "-v"]
    VERBOSE = "-v" in sys.argv[1:]
    groups = set(args)

    print("시험 전 설정을 적어 둔다 — 끝나면 되돌린다")
    try:
        press0, release0 = cfg_um()
    except Exception as e:
        sys.exit(f"[E_] 장치에 못 붙었다 — {e}")
    print(f"  입력지점 {press0/100:.2f} mm,  해제지점 {release0/100:.2f} mm\n")

    print(f"  {pad('단계', COL_G)}{pad('항목', COL_N)}{pad('결과', COL_R)}   시간")
    rule()

    n_ok = n_bad = n_x = 0
    bad  = []
    t_all = time.time()

    try:
        for group, name, fn, xfail in TESTS:
            if groups and group not in groups:
                continue

            t0 = time.time()
            try:
                why = fn()
            except Exception as e:
                why = f"시험이 터졌다 — {e}"
            dt = time.time() - t0

            if xfail:
                if why is None:
                    mark, ok = "xfail", True
                    n_x += 1
                else:
                    mark, ok = "xpass", False
                    why = "알려진 문제가 재현이 안 된다 — " + why
                    n_bad += 1
            else:
                mark, ok = ("ok", True) if why is None else ("bad", False)
                n_ok += ok
                n_bad += (not ok)

            row(group, name, mark, dt)
            if not ok:
                bad.append((group, name, why))

        rule()
        print(f"  {pad('', COL_G)}"
              f"{pad(f'통과 {n_ok} · 실패 {n_bad} · 알려진 문제 {n_x}', COL_N + COL_R)}"
              f"{time.time() - t_all:5.1f}s")
    finally:
        # 순서 — 설정 먼저, 주입 나중 (그 사이 리포트가 살아나면 안 된다)
        say("keys cfg press %d" % press0,
            "keys cfg release %d" % release0)
        say("keys inject off")
        say("keys base", wait=3.0)      # 갇힌 기준값이 남지 않게

    if bad:
        print("\n파고들 것")
        for g, name, why in bad:
            print(f"  · [{g}] {name}\n      {why}")
    else:
        print("\n문제 없다" + (f"  (알려진 문제 {n_x} 건은 그대로 재현됨)" if n_x else ""))

    print("\n설정을 되돌리고 기준값을 다시 잡았다")
    return 1 if n_bad else 0


if __name__ == "__main__":
    sys.exit(main())
