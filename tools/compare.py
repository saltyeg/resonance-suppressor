"""Overlay soothe3 vs our plugin for matching renders.

    python tools/compare.py [case-or-signal filter ...] [--out renders/compare.png]

For each render present in both renders/soothe3 and renders/ours, plots the average gain curve
(output / input, 1/12-octave smoothed) of both and prints a table of key numbers.
"""
import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from analyze import load, octave_smooth, stft_mag

ROOT = Path(__file__).resolve().parent.parent
REF, OURS = ROOT / "renders/soothe3", ROOT / "renders/ours"


def gain_curve(src, rendered):
    sr, xi = load(src)
    _, xo = load(rendered)
    n = min(len(xi), len(xo))
    f, _, pi = stft_mag(xi[:n], sr)
    _, _, po = stft_mag(xo[:n], sr)
    gi = octave_smooth(f, pi.mean(axis=1))
    go = octave_smooth(f, po.mean(axis=1))
    return f, 10 * np.log10(go + 1e-20) - 10 * np.log10(gi + 1e-20)


def source_for(name):
    sig = name.split("__")[0]
    p = ROOT / "test-audio/synth" / f"{sig}.wav"
    return p if p.exists() else ROOT / "test-audio/real" / f"{sig}.wav"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filters", nargs="*")
    ap.add_argument("--out", default=str(ROOT / "renders/compare.png"))
    a = ap.parse_args()

    names = sorted(p.stem for p in OURS.glob("*.wav") if (REF / p.name).exists())
    if a.filters:
        names = [n for n in names if any(flt in n for flt in a.filters)]
    if not names:
        print("nothing to compare")
        return

    ref_sum = json.loads((REF / "summary.json").read_text())
    our_sum = json.loads((OURS / "summary.json").read_text())

    cols = 3
    rows = (len(names) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(6 * cols, 3.2 * rows), squeeze=False)
    err = []
    print(f"{'render':44s} {'ref cut':>8s} {'our cut':>8s} {'ref out-in':>10s} {'our out-in':>10s} {'curve rmse':>10s}")
    for ax, name in zip(axes.flat, names):
        src = source_for(name)
        f, gr = gain_curve(src, REF / f"{name}.wav")
        _, go = gain_curve(src, OURS / f"{name}.wav")
        band = (f > 40) & (f < 16000)
        rmse = float(np.sqrt(np.mean((gr[band] - go[band]) ** 2)))
        err.append(rmse)
        ax.semilogx(f[1:], gr[1:], label="soothe3", lw=1.2)
        ax.semilogx(f[1:], go[1:], label="ours", lw=1.2)
        ax.set_xlim(30, 20000)
        ax.set_ylim(-20, 3)
        ax.grid(True, which="both", alpha=0.3)
        ax.set_title(f"{name}  (rmse {rmse:.1f} dB)", fontsize=9)
        r, o = ref_sum.get(name, {}), our_sum.get(name, {})
        print(f"{name:44s} {r.get('deepest_avg_cut_db', 0):8.1f} {o.get('deepest_avg_cut_db', 0):8.1f} "
              f"{r.get('out_rms_db', 0) - r.get('in_rms_db', 0):10.1f} {o.get('out_rms_db', 0) - o.get('in_rms_db', 0):10.1f} {rmse:10.2f}")
    for ax in list(axes.flat)[len(names):]:
        ax.axis("off")
    axes.flat[0].legend()
    fig.tight_layout()
    fig.savefig(a.out, dpi=70)
    print(f"mean curve rmse {np.mean(err):.2f} dB over {len(names)} renders -> {a.out}")


if __name__ == "__main__":
    main()
