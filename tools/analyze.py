"""Compare a processed WAV against its input.

    python tools/analyze.py <in.wav> <out.wav> [png] [--title T]

Writes a PNG with:
  * long-term average spectra (1/12-octave smoothed) of input and output
  * mean and max gain change per frequency
  * a time x frequency map of the gain change (output / input, dB)
and prints a short numeric summary.
"""
import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy import signal
from scipy.io import wavfile

NFFT = 4096


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.dtype.kind == "i":
        x /= np.iinfo(x.dtype).max
    if x.ndim == 1:
        x = x[:, None]
    return sr, x


def stft_mag(x, sr):
    f, t, z = signal.stft(x.mean(axis=1), sr, nperseg=NFFT, noverlap=NFFT * 3 // 4)
    return f, t, np.abs(z) ** 2


def octave_smooth(f, p, frac=12):
    out = np.empty_like(p)
    for i, fc in enumerate(f):
        lo, hi = fc * 2 ** (-0.5 / frac), fc * 2 ** (0.5 / frac)
        m = (f >= lo) & (f <= hi)
        out[i] = p[m].mean() if m.any() else p[i]
    return out


def gain_map(pi, po, floor_db=-80):
    # Gain only where the input carries energy (avoid dividing noise by noise).
    ref = pi.max()
    gi = 10 * np.log10(po + 1e-20) - 10 * np.log10(pi + 1e-20)
    gi[10 * np.log10(pi / ref + 1e-20) < floor_db] = np.nan
    return gi


def analyze(in_path, out_path, png=None, title=None):
    sr, xi = load(in_path)
    sr2, xo = load(out_path)
    assert sr == sr2, "sample rates differ"
    n = min(len(xi), len(xo))
    xi, xo = xi[:n], xo[:n]

    f, t, pi = stft_mag(xi, sr)
    _, _, po = stft_mag(xo, sr)
    g = gain_map(pi, po)

    avg_i = octave_smooth(f, pi.mean(axis=1))
    avg_o = octave_smooth(f, po.mean(axis=1))
    mean_gain = 10 * np.log10(avg_o + 1e-20) - 10 * np.log10(avg_i + 1e-20)
    with np.errstate(all="ignore"):
        min_gain = np.nanmin(g, axis=1)

    band = (f > 30) & (f < 18000)
    worst = np.nanargmin(np.where(band, mean_gain, np.nan))
    rms = lambda a: 20 * np.log10(np.sqrt(np.mean(a ** 2)) + 1e-20)
    summary = {
        "in_rms_db": rms(xi),
        "out_rms_db": rms(xo),
        "delta_rms_db": rms(xo - xi),
        "deepest_avg_cut_db": mean_gain[worst],
        "deepest_avg_cut_hz": f[worst],
        "deepest_instant_cut_db": np.nanmin(np.where(band[:, None], g, np.nan)),
    }

    if png:
        fig, ax = plt.subplots(3, 1, figsize=(12, 11), gridspec_kw={"height_ratios": [1, 1, 1.4]})
        fig.suptitle(title or f"{Path(in_path).name} -> {Path(out_path).name}")
        ax[0].semilogx(f[1:], 10 * np.log10(avg_i[1:] + 1e-20), label="input", lw=1)
        ax[0].semilogx(f[1:], 10 * np.log10(avg_o[1:] + 1e-20), label="output", lw=1)
        ax[0].set_ylabel("dB")
        ax[0].legend()
        ax[1].semilogx(f[1:], mean_gain[1:], label="avg gain", lw=1)
        ax[1].semilogx(f[1:], octave_smooth(f, np.nan_to_num(min_gain))[1:], label="min gain over time", lw=1, alpha=0.7)
        ax[1].set_ylabel("gain dB")
        ax[1].set_ylim(-30, 3)
        ax[1].legend()
        for a in ax[:2]:
            a.set_xlim(20, 20000)
            a.grid(True, which="both", alpha=0.3)
        mesh = ax[2].pcolormesh(t, f[1:], g[1:], shading="auto", cmap="magma", vmin=-24, vmax=0)
        ax[2].set_yscale("log")
        ax[2].set_ylim(20, 20000)
        ax[2].set_xlabel("s")
        ax[2].set_ylabel("Hz")
        fig.colorbar(mesh, ax=ax[2], label="gain dB")
        fig.tight_layout()
        fig.savefig(png, dpi=80)
        plt.close(fig)

    return summary


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("inp")
    p.add_argument("out")
    p.add_argument("png", nargs="?")
    p.add_argument("--title")
    a = p.parse_args()
    s = analyze(a.inp, a.out, a.png, a.title)
    for k, v in s.items():
        print(f"{k:24s} {v:8.2f}")
