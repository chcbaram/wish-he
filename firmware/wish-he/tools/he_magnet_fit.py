#!/usr/bin/env python3
"""
Fit a Hall-effect keyboard switch's flux-vs-travel curve from the two
numbers manufacturers actually publish (initial flux, bottom-out flux)
and emit a normalised LUT for firmware.

Usage:
    python he_magnet_fit.py --initial 120 --bottom 700 --travel 3.5
    python he_magnet_fit.py --initial 102 --bottom 905 --travel 4.1 -n 65

Background and derivation: see he-magnet-model.md
"""

import argparse
import numpy as np
from scipy.optimize import brentq


def f(z, R, L):
    """Shape factor of the on-axis field of a cylindrical magnet.

    B(z) = (Br/2) * f(z, R, L)

    z : gap from magnet face to the Hall sensing element (mm)
    R : magnet radius (mm)
    L : magnet length along the magnetisation axis (mm)
    """
    return (z + L) / np.sqrt((z + L) ** 2 + R ** 2) - z / np.sqrt(z ** 2 + R ** 2)


def fit(B_initial, B_bottom, travel, R, L):
    """Solve for effective gap z0 and remanence Br from the two spec points.

    Two equations, two unknowns:
        B(z0)          = B_bottom
        B(z0 + travel) = B_initial
    Taking the ratio eliminates Br and leaves a scalar root-find in z0.
    """
    ratio = B_bottom / B_initial
    g = lambda z: f(z, R, L) / f(z + travel, R, L) - ratio
    lo, hi = 1e-4, 20.0
    if g(lo) * g(hi) > 0:
        raise ValueError(
            f"no solution for R={R}, L={L}: the flux ratio {ratio:.2f} is "
            f"outside what this geometry can produce"
        )
    z0 = brentq(g, lo, hi)
    Br = 2 * B_bottom / f(z0, R, L)      # gauss
    return z0, Br


def search_geometry(B_initial, B_bottom, travel):
    """Scan candidate magnet geometries, keep the physically plausible ones.

    NdFeB remanence tops out near 1.45 T, so any (R, L) demanding more than
    that is impossible and gets discarded. This is what pins the magnet size.
    """
    out = []
    for R in np.arange(0.5, 3.01, 0.25):
        for L in np.arange(1.0, 4.01, 0.25):
            try:
                z0, Br = fit(B_initial, B_bottom, travel, R, L)
            except (ValueError, RuntimeError):
                continue
            Br_T = Br / 10000.0
            if 0.90 <= Br_T <= 1.45 and 0.3 <= z0 <= 5.0:
                out.append((R, L, z0, Br_T))
    return out


def normalised_lut(z0, Br, travel, R, L, n):
    """Forward LUT: u (normalised ADC) sampled at uniform travel steps.

    u = (adc - adc_rest) / (adc_bottom - adc_rest)

    u is invariant under any affine transform of the ADC reading, so
    per-switch magnetisation spread and per-sensor offset both cancel.
    Sampling at uniform travel (rather than uniform u) keeps distance
    resolution even where the curve is steep.
    """
    d = np.linspace(0.0, travel, n)
    B = Br / 2 * f(z0 + (travel - d), R, L)
    u = (B - B[0]) / (B[-1] - B[0])
    return d, u


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--initial", type=float, default=120.0,
                   help="initial (rest) flux in gauss")
    p.add_argument("--bottom", type=float, default=700.0,
                   help="bottom-out flux in gauss")
    p.add_argument("--travel", type=float, default=3.5,
                   help="total travel in mm")
    p.add_argument("-n", type=int, default=33,
                   help="LUT entry count (33 or 65 are convenient)")
    a = p.parse_args()

    cand = search_geometry(a.initial, a.bottom, a.travel)
    if not cand:
        raise SystemExit(
            "No plausible geometry. Check the spec values -- in particular "
            "whether both were measured at the same PCB thickness."
        )

    print(f"spec: {a.initial:.0f} Gs -> {a.bottom:.0f} Gs over {a.travel} mm "
          f"(ratio {a.bottom / a.initial:.2f})\n")
    print("plausible geometries:")
    print(f"  {'dia':>6} {'len':>6} {'gap z0':>8} {'Br':>7}")
    for R, L, z0, Br_T in cand:
        print(f"  {2 * R:6.1f} {L:6.2f} {z0:8.3f} {Br_T:6.3f} T")

    # Any of them gives the same normalised shape -- take the middle one.
    R, L, z0, Br_T = cand[len(cand) // 2]
    Br = Br_T * 10000
    print(f"\nusing dia {2 * R:.1f} mm, len {L:.2f} mm, gap {z0:.3f} mm, "
          f"Br {Br_T:.3f} T")

    d, u = normalised_lut(z0, Br, a.travel, R, L, a.n)

    print(f"\n// travel {a.travel} mm, {a.n} entries, uniform {a.travel / (a.n - 1) * 1000:.1f} um steps")
    print(f"// index i  ->  travel = i * {a.travel / (a.n - 1):.6f} mm")
    print(f"static const uint16_t he_curve_q15[{a.n}] = {{")
    q = [int(round(x * 32767)) for x in u]
    for i in range(0, a.n, 8):
        print("    " + ", ".join(f"{v:5d}" for v in q[i:i + 8]) + ",")
    print("};")

    # Worst-case error of a naive linear mapping, for reference.
    lin = a.travel * u
    print(f"\n// linear mapping would be off by up to "
          f"{np.abs(lin - d).max():.3f} mm")


if __name__ == "__main__":
    main()
