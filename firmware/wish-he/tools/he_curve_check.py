#!/usr/bin/env python3
"""
곡선 계산이 모델과 맞는지 호스트에서 대조한다 — 굽지 않고, 몇 초 만에.

    python3 tools/he_curve_check.py                          내장 스위치 전부
    python3 tools/he_curve_check.py --rest 120 --bottom 700 --travel 350
    python3 tools/he_curve_check.py -v                       33칸 전부 보기

★ 왜 있나.

  같은 계산을 세 군데가 한다 — 펌웨어의 keysCurveBuild(정수), 웹앱의 heMakeCurve,
  그리고 tools/he_magnet_fit.py(부동소수). 셋이 어긋나도 **에러가 나지 않는다.**
  화면에는 1.20mm 라고 뜨는데 키보드는 1.35mm 에서 입력을 낸다. 감이 이상할 뿐
  원인을 짚을 방법이 없다.

  keys.c 주석에 "float 판과 대조해 보니 최대 21(0.06%) 차이" 라고 적혀 있는데,
  그 대조가 일회성이었다. 곡선이나 자석 치수를 건드리면 다시 확인할 길이 없었다.

★ 펌웨어 코드를 베끼지 않는다.

  keys.c 에서 함수 본문을 **그 자리에서 잘라 와** 호스트로 컴파일한다. 베껴 두면
  반드시 갈라지고, 갈라진 사본을 시험해 봐야 아무 뜻이 없다. 함수 이름이 바뀌면
  조용히 통과하는 대신 여기서 멈춘다.

필요한 것:  numpy, scipy (he_magnet_fit 과 같다), 그리고 호스트 C 컴파일러
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
from scipy.optimize import brentq

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from he_magnet_fit import f as shape_f, fit   # 모델은 한 곳에서만 정의한다

HERE     = os.path.dirname(os.path.abspath(__file__))
KEYS_C   = os.path.join(HERE, "..", "src", "hw", "driver", "keys.c")

# keys.c 에서 잘라 올 것들
NEED_DEFINES = ["KEYS_MAG_R_UM", "KEYS_MAG_L_UM", "KEYS_SHAPE_Q", "KEYS_SQRT_F",
                "KEYS_CURVE_N", "KEYS_CURVE_SEG", "KEYS_CURVE_ONE"]
NEED_FUNCS   = ["keysISqrt64", "keysAxisQ20", "keysShapeQ20", "keysCurveBuild"]

# 통과 기준 — ① 의 실측이 21 카운트 / 1.2 µm 라 여유를 크게 준 값이다.
LIMIT_Q15 = 64
LIMIT_UM  = 20

#
# ★ ②·③ 은 통과/실패를 가르지 않는다. **알고 있는 상태**다.
#
#   구워 둔 표는 he_magnet_fit.py 가 스위치마다 고른 기하로 만들었고, 런타임
#   keysCurveBuild 는 keys.c 가 박아 둔 기하를 쓴다. 그래서 둘이 어긋난다.
#
#   맞추지 않고 두기로 했다 — 차이가 잡음 바닥(0.10mm)의 3분의 1이라 느낄 수 없고,
#   지금 표를 다시 구우면 사용자 입력지점만 최대 0.035mm 움직인다. 실측 표(kind=1)가
#   들어오면 모델 자체가 필요 없어지므로 그때 한꺼번에 정리한다.
#
#   ★ 그러니 매번 [E_] 를 뱉게 두면 안 된다. 늘 빨간 검사는 아무도 안 본다.
#     **종료 코드를 가르는 것은 ① 뿐이다** — 그게 진짜 회귀 감지기다.

# ── keys.c 에서 잘라 오기 ────────────────────────────────────────────────

def slice_define(src, name):
    m = re.search(r'^#define\s+' + name + r'\s+(.+?)\s*(?:/\*.*)?$', src, re.M)
    if not m:
        sys.exit(f"[E_] keys.c 에서 #define {name} 을 못 찾았다")
    return f"#define {name}   {m.group(1).strip()}"


def slice_func(src, name):
    """이름이 같은 첫 정의를 중괄호 짝으로 잘라 온다 (선언은 건너뛴다)."""
    lines = src.split("\n")
    for i, l in enumerate(lines):
        if re.search(r'\b' + name + r'\s*\(', l) and not l.rstrip().endswith(";") \
           and re.match(r'^(static|[A-Za-z_])', l):
            depth = 0
            seen  = False
            for j in range(i, len(lines)):
                depth += lines[j].count("{") - lines[j].count("}")
                if "{" in lines[j]:
                    seen = True
                if seen and depth == 0:
                    return "\n".join(lines[i:j + 1])
    sys.exit(f"[E_] keys.c 에서 {name}() 정의를 못 찾았다 — 이름이 바뀌었나?")


def parse_switches(src):
    """{ 이름, 행정(0.01mm), 스트로크, 초기자속, 바닥자속, 곡선 } 를 읽는다."""
    m = re.search(r'static const keys_switch_t keys_switch\[\]\s*=\s*\{(.*?)\n\};',
                  src, re.S)
    if not m:
        sys.exit("[E_] keys_switch[] 표를 못 찾았다")
    out = []
    for e in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(\d+)\s*,[^,]+,\s*(\d+)\s*,\s*(\d+)\s*,'
                         r'\s*(\w+)\s*\}', m.group(1)):
        name, travel, rest, bot, curve = e.groups()
        out.append(dict(name=name, travel=int(travel), rest=int(rest),
                        bottom=int(bot), curve=curve))
    return out


def parse_curve_table(src, sym):
    """구워 둔 곡선표 하나를 읽는다."""
    m = re.search(r'static const uint16_t\s+' + sym + r'\[[^\]]*\]\s*=\s*\{(.*?)\};',
                  src, re.S)
    if not m:
        return None
    return [int(x) for x in re.findall(r'\d+', m.group(1))]


# ── 호스트에서 펌웨어 코드를 돌린다 ──────────────────────────────────────

HARNESS_MAIN = r'''
int main(int argc, char **argv)
{
  uint16_t out[KEYS_CURVE_N];
  uint16_t rest   = (uint16_t)atoi(argv[1]);
  uint16_t bottom = (uint16_t)atoi(argv[2]);
  uint16_t travel = (uint16_t)atoi(argv[3]);

  if (keysCurveBuild(rest, bottom, travel, out) == false)
  {
    printf("FAIL\n");
    return 1;
  }
  for (uint32_t i = 0; i < KEYS_CURVE_N; i++) printf("%u\n", out[i]);
  return 0;
}
'''


def build_harness(src, workdir):
    parts = ["#include <stdint.h>", "#include <stdbool.h>",
             "#include <stdio.h>", "#include <stdlib.h>", ""]
    parts += [slice_define(src, d) for d in NEED_DEFINES]
    parts.append("")
    parts += [slice_func(src, fn) for fn in NEED_FUNCS]
    parts.append(HARNESS_MAIN)

    c_path   = os.path.join(workdir, "curve_harness.c")
    bin_path = os.path.join(workdir, "curve_harness")
    with open(c_path, "w", encoding="utf-8") as fp:
        fp.write("\n".join(parts))

    cc = os.environ.get("CC", "cc")
    r = subprocess.run([cc, "-O2", "-std=c11", "-Wall", "-o", bin_path, c_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit(f"[E_] 호스트 컴파일 실패 — {c_path} 를 보라")
    if r.stderr.strip():
        print("컴파일 경고:\n" + r.stderr.strip() + "\n")
    return bin_path


def run_harness(bin_path, rest, bottom, travel):
    r = subprocess.run([bin_path, str(rest), str(bottom), str(travel)],
                       capture_output=True, text=True)
    if r.returncode != 0 or r.stdout.startswith("FAIL"):
        return None
    return [int(x) for x in r.stdout.split()]


# ── 모델 (부동소수) ──────────────────────────────────────────────────────

def model_curve(rest_gs, bottom_gs, travel_um, n, R_mm, L_mm):
    """he_magnet_fit 과 같은 식을, 펌웨어가 박아 둔 자석 치수로 푼다."""
    travel_mm = travel_um / 100.0            # keys.c 의 "um" 은 0.01mm 다
    ratio = bottom_gs / rest_gs
    g = lambda z: shape_f(z, R_mm, L_mm) / shape_f(z + travel_mm, R_mm, L_mm) - ratio
    lo, hi = 1e-4, 20.0
    if g(lo) * g(hi) > 0:
        return None, None
    z0 = brentq(g, lo, hi)

    d = np.linspace(0.0, travel_mm, n)
    B = shape_f(z0 + (travel_mm - d), R_mm, L_mm)
    u = (B - B[0]) / (B[-1] - B[0])
    return z0, u


def depth_error_um(u_fw, travel_um, z0, R_mm, L_mm, n):
    """
    u 차이를 **거리 오차**로 옮긴다.

    모델 곡선을 촘촘히 깔아 두고 "펌웨어가 낸 u 를 모델은 몇 mm 로 읽나" 를 되물어
    그 칸의 진짜 깊이와 견준다. 카운트 차이만 보면 곡선이 가파른 구간에서 얼마나
    아픈지가 안 보인다.
    """
    travel_mm = travel_um / 100.0
    dd = np.linspace(0.0, travel_mm, 20001)
    B  = shape_f(z0 + (travel_mm - dd), R_mm, L_mm)
    uu = (B - B[0]) / (B[-1] - B[0])

    d_true = np.linspace(0.0, travel_mm, n)
    d_fw   = np.interp(np.asarray(u_fw) / 32767.0, uu, dd)
    return np.abs(d_fw - d_true) * 1000.0        # µm


# ── 대조 한 건 ───────────────────────────────────────────────────────────

def compare(label, fw, rest, bottom, travel, n, R_mm, L_mm, verbose):
    z0, u_ref = model_curve(rest, bottom, travel, n, R_mm, L_mm)
    if z0 is None:
        print(f"  {label:26s}  모델이 근을 못 찾는다 (비 {bottom / rest:.2f})")
        return False

    q_ref = np.round(np.asarray(u_ref) * 32767).astype(int)
    q_fw  = np.asarray(fw, dtype=int)
    dq    = np.abs(q_fw - q_ref)
    dum   = depth_error_um(q_fw, travel, z0, R_mm, L_mm, n)

    ok = (dq.max() <= LIMIT_Q15) and (dum.max() <= LIMIT_UM)
    print(f"  {label:26s}  최대 {dq.max():4d} 카운트 ({dq.max() / 327.67:.2f}%)"
          f"   거리 {dum.max():5.1f} µm   {'OK' if ok else '[E_]'}")

    if verbose:
        print(f"      z0 {z0:.3f} mm")
        print(f"      {'i':>3} {'펌웨어':>7} {'모델':>7} {'차':>5} {'µm':>7}")
        for i in range(n):
            print(f"      {i:3d} {q_fw[i]:7d} {q_ref[i]:7d} "
                  f"{q_fw[i] - q_ref[i]:5d} {dum[i]:7.1f}")
    return ok


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--rest",   type=int, help="초기(무압) 자속 Gs")
    p.add_argument("--bottom", type=int, help="바닥 자속 Gs")
    p.add_argument("--travel", type=int, help="전 행정 (0.01mm — keys.c 단위)")
    p.add_argument("-v", "--verbose", action="store_true", help="33칸 전부 찍는다")
    a = p.parse_args()

    src = open(KEYS_C, encoding="utf-8").read()

    n    = int(re.search(r'#define\s+KEYS_CURVE_N\s+(\d+)', src).group(1))
    R_mm = int(re.search(r'#define\s+KEYS_MAG_R_UM\s+(\d+)', src).group(1)) / 1000.0
    L_mm = int(re.search(r'#define\s+KEYS_MAG_L_UM\s+(\d+)', src).group(1)) / 1000.0
    print(f"keys.c — 자석 R {R_mm} mm, L {L_mm} mm, 곡선 {n} 칸\n")

    with tempfile.TemporaryDirectory() as work:
        harness = build_harness(src, work)
        all_ok  = True

        if a.rest and a.bottom and a.travel:
            fw = run_harness(harness, a.rest, a.bottom, a.travel)
            label = f"{a.rest}->{a.bottom} Gs / {a.travel / 100:.2f}mm"
            if fw is None:
                print(f"  {label:26s}  keysCurveBuild 가 false — 직선으로 읽는다")
                all_ok = False
            else:
                all_ok = compare(label, fw, a.rest, a.bottom, a.travel,
                                 n, R_mm, L_mm, a.verbose)
        else:
            sw = parse_switches(src)

            print("① keysCurveBuild (정수)  vs  모델 (부동소수)")
            for s in sw:
                fw = run_harness(harness, s["rest"], s["bottom"], s["travel"])
                if fw is None:
                    print(f"  {s['name']:26s}  keysCurveBuild 가 false")
                    all_ok = False
                    continue
                all_ok &= compare(s["name"], fw, s["rest"], s["bottom"],
                                  s["travel"], n, R_mm, L_mm, a.verbose)

            print("\n② 구워 둔 곡선표  vs  모델 — 표가 낡지 않았나")
            seen = set()
            tbl_ok = True
            for s in sw:
                if s["curve"] in seen:
                    continue
                seen.add(s["curve"])
                tbl = parse_curve_table(src, s["curve"])
                if tbl is None:
                    print(f"  {s['curve']:26s}  표를 못 찾았다")
                    tbl_ok = False
                    continue
                tbl_ok &= compare(s["curve"], tbl, s["rest"], s["bottom"],
                                  s["travel"], n, R_mm, L_mm, a.verbose)
            if not tbl_ok:
                print("      ↑ 표는 he_magnet_fit.py 가 구웠는데, 그 도구는 스위치마다"
                      " 기하를 따로 고른다.\n"
                      "        여기 모델은 keys.c 가 박아 둔 R/L 을 쓴다 — 그 차이다.\n"
                      "        맞추지 않기로 했다. 위 주석과 docs/README.md 를 볼 것.")

            print("\n③ 박아 둔 자석 기하가 그 자속을 낼 수 있나")
            for s in sw:
                try:
                    z0, Br = fit(s["rest"], s["bottom"], s["travel"] / 100.0, R_mm, L_mm)
                    Br_T = Br / 10000.0
                    bad  = Br_T > 1.45           # NdFeB 잔류자속 상한
                    print(f"  {s['name']:26s}  z0 {z0:5.2f} mm   Br {Br_T:5.2f} T"
                          f"   {'← 1.45 T 초과, 이 기하로는 불가능' if bad else 'OK'}")
                except (ValueError, RuntimeError) as e:
                    print(f"  {s['name']:26s}  근이 없다 — {e}")

    print(f"\n① 의 기준 : {LIMIT_Q15} 카운트 / {LIMIT_UM} µm 이내  (②·③ 은 참고용)")
    print("① 통과 — 정수 계산이 모델과 맞는다" if all_ok
          else "[E_] ① 이 기준을 넘었다 — 정수 계산이 어긋났다")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
