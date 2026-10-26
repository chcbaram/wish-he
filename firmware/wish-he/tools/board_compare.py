#!/usr/bin/env python3
"""같은 모델의 보드를 여러 장 떠서 칸별로 비교한다 — 새로 조립한 보드가 멀쩡한지 본다.

    python3 tools/board_compare.py snap A        지금 붙은 보드를 A 로 뜬다
    python3 tools/board_compare.py snap B        보드를 바꿔 B 로 뜬다
    python3 tools/board_compare.py cmp A B       나란히 비교

뜨는 것 (전부 CLI)

    keys dump     휴지 레벨 — 칸마다. 빈 칸(GND)은 최대값이어야 한다
    keys cfg      키별 스트로크 — 보정한 뒤라야 나온다 (keys cal)
    keys noise    칸별 잡음 p-p, 데드밴드 이전 평균·최대
    keys info     스캔 시간, ADC 타임아웃

★ 무엇을 보나. 한 칸만 튀면 그 센서 · 커패시터 · 납땜이고, 한 열이 통째로 다르면 그 MUX
  나 ADC 입력이다. 그래서 보드끼리의 차이와 **보드 안에서 중앙값과의 차이**를 같이 본다.

★ 한 장씩 꽂는다. 같은 모델은 USB ID 와 시리얼이 같아서 둘을 꽂으면 어느 쪽에 붙을지
  모른다.

baram-term 이 포트를 열고 있으면 그쪽을 거친다 (baram-ctl). 아니면 포트를 직접 연다.
저장은 build/board/<이름>.json — 개체 기록이라 깃에 안 올린다.
"""
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

FW = Path(__file__).resolve().parent.parent
OUT = FW / "build" / "board"
PID = os.environ.get("WISH_PID", "0x5307")
MATCH = "0483:%04x" % int(PID, 0)

FULL = 12270          # 빈 칸(GND)이 뒤집혀 들어온 값 근처 — 3개 합 눈금 최대
REST_DEV = 300        # 보드 안 중앙값에서 이만큼 벗어나면 표시 (3개 합 눈금, 12비트 100)
STROKE_DEV = 0.15     # 보드 안 평균 스트로크에서 15% 넘게 벗어나면 표시
NOISE_MAX = 10        # 데드밴드 뒤 p-p 가 이보다 크면 표시


def run(cmd, timeout=10):
    """baram-term 을 거치거나 포트를 직접 열어 CLI 출력을 돌려준다."""
    if shutil.which("baram-ctl"):
        r = subprocess.run(["baram-ctl", "list"], capture_output=True, text=True)
        if MATCH in r.stdout and "connected" in r.stdout:
            r = subprocess.run(["baram-ctl", "--match", MATCH, "send", cmd,
                                "--until", r"cli# $", "--timeout", str(timeout)],
                               capture_output=True, text=True)
            return r.stdout.replace("\r", "")
    sys.path.insert(0, str(FW / "tools"))
    os.environ["WISH_PID"] = PID
    import dev
    return dev.cli_get([cmd], wait=timeout)[0][1].replace("\r", "")


def grid(text, title):
    """제목 줄 다음의 `sN  v v v` 표를 읽는다. '-' 는 None."""
    lines = text.split("\n")
    i = next((k for k, l in enumerate(lines) if title in l), None)
    if i is None:
        return None
    out = []
    for l in lines[i + 1:]:
        m = re.match(r"\s*s(\d+)\s+(.*)$", l)
        if m:
            out.append([None if v == "-" else int(v) for v in m.group(2).split()])
        elif out:
            break
    return out


def snap(name):
    d = {}
    t = run("keys info")
    d["scan_us"] = int(re.search(r"scan\s*:\s*(\d+)", t).group(1))
    d["timeout"] = int(re.search(r"timeout\s*:\s*(\d+)", t).group(1))
    d["layout"] = re.search(r"layout\s*:\s*(.*)", t).group(1).strip()

    d["rest"] = grid(run("keys dump"), "ch0")

    t = run("keys cfg")
    d["stroke"] = grid(t, "키별 스트로크")

    t = run("keys noise 3000", timeout=15)
    d["noise_pp"] = grid(t, "진폭")
    m = re.search(r"p-p 평균 (\d+), 최대 (\d+)", t)
    d["raw_pp"] = [int(m.group(1)), int(m.group(2))] if m else None

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f"{name}.json").write_text(json.dumps(d, indent=1, ensure_ascii=False))
    print(f"{OUT / name}.json")
    report(name, d)


def flags(d):
    """보드 안에서 튀는 칸을 모은다."""
    out = []
    vals = [v for row in d["rest"] for v in row if v < FULL - 200]
    med = statistics.median(vals)
    for s, row in enumerate(d["rest"]):
        for c, v in enumerate(row):
            present = d["stroke"] is None or d["stroke"][s][c] is not None
            if not present:
                if v < FULL - 200:
                    out.append(f"s{s}/ch{c} 빈 칸인데 {v} — GND 가 아니다")
            elif abs(v - med) > REST_DEV:
                out.append(f"s{s}/ch{c} 휴지 {v} (중앙값 {med:.0f} 에서 {v - med:+.0f})")
    if d["stroke"]:
        st = [v for row in d["stroke"] for v in row if v is not None]
        avg = sum(st) / len(st)
        for s, row in enumerate(d["stroke"]):
            for c, v in enumerate(row):
                if v is not None and abs(v - avg) / avg > STROKE_DEV:
                    out.append(f"s{s}/ch{c} 스트로크 {v} (평균 {avg:.0f} 에서 {(v - avg) / avg:+.0%})")
    for s, row in enumerate(d["noise_pp"] or []):
        for c, v in enumerate(row):
            if v is not None and v > NOISE_MAX:
                out.append(f"s{s}/ch{c} 잡음 p-p {v}")
    return med, out


def report(name, d):
    med, fl = flags(d)
    st = [v for row in (d["stroke"] or []) for v in row if v is not None]
    print(f"[{name}] {d['layout']}  스캔 {d['scan_us']}us  타임아웃 {d['timeout']}  "
          f"휴지 중앙값 {med:.0f}  스트로크 {len(st)}키 "
          + (f"{min(st)}~{max(st)} 평균 {sum(st) / len(st):.0f}" if st else "(보정 전)")
          + (f"  잡음(데드밴드 전) 평균 {d['raw_pp'][0]} 최대 {d['raw_pp'][1]}" if d["raw_pp"] else ""))
    for f in fl:
        print("   · " + f)
    if not fl:
        print("   튀는 칸 없음")


def cmp(a, b):
    da = json.loads((OUT / f"{a}.json").read_text())
    db = json.loads((OUT / f"{b}.json").read_text())
    report(a, da)
    report(b, db)

    for key, title in (("rest", "휴지 레벨 차 (B - A)"), ("stroke", "스트로크 차 (B - A)")):
        if not da.get(key) or not db.get(key):
            continue
        print(f"\n{title}")
        print("      " + "".join(f"  ch{c:<4d}" for c in range(len(da[key][0]))))
        for s, (ra, rb) in enumerate(zip(da[key], db[key])):
            cells = []
            for va, vb in zip(ra, rb):
                if va is None or vb is None or va >= FULL - 200:
                    cells.append(f"{'-':>8}")
                else:
                    cells.append(f"{vb - va:+8d}")
            print(f"  s{s:<3d}" + "".join(cells))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "snap":
        snap(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "cmp":
        cmp(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
