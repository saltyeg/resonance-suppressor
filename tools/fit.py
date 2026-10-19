"""Fit the DSP Tuning constants so our gain curves match soothe3's reference renders.

    python tools/fit.py core        # widths / thresholds / depth curve (soft mode)
    python tools/fit.py ballistics  # attack / release
    python tools/fit.py hard        # hard mode
    python tools/fit.py eval        # just report the current loss per case

Renders run through rsdsp (DSP only) at idle CPU priority. The best tuning so far is kept in
tools/tuning.json and passed to every render, so stages build on each other.
"""
import json
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.optimize import minimize

sys.path.insert(0, str(Path(__file__).parent))
from compare import gain_curve, source_for  # noqa: E402
from render_matrix import CASES  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
RSDSP = ROOT / "build/Release/rsdsp_artefacts/Release/rsdsp.exe"
REF = ROOT / "renders/soothe3"
TUNING_FILE = Path(__file__).parent / "tuning.json"
IDLE = 0x00000040  # IDLE_PRIORITY_CLASS
WORKERS = 8

VOCALS = ["VOC_Respect_dec", "VOC_So_Easy_dec", "VOC_WhatsThisKnob_dec"]
CORE_SIG = ["pink_peaks", "pink_flat", "pink_moving_peak", "saw_chord"] + VOCALS

STAGES = {
    "core": {
        "cases": [(s, c) for c in ["core_d5", "core_d10", "detail_1", "detail_9"] for s in CORE_SIG]
        + [("sweep", "core_d5"), ("pink_peaks", "core_d2"), ("saw_chord", "core_d2")],
        "params": {
            # name: (initial, lo, hi)
            "fineOctBase": (0.05, 0.0, 1.0), "fineOctSlope": (2.5, 0.5, 20.0), "fineBinsBase": (1.0, 0.0, 6.0),
            "refOctBase": (1.2, 0.1, 6.0), "refOctSlope": (6.0, 0.5, 40.0),
            "cutOctBase": (0.8, 0.01, 3.0), "cutOctSlope": (4.0, 0.5, 40.0), "cutPower": (3.0, 1.0, 8.0),
            "thrBase": (2.0, -10.0, 20.0), "thrDetail": (0.6, -2.0, 3.0), "thrDepth": (0.9, 0.0, 5.0),
            "ratio": (0.8, 0.05, 5.0), "limitScale": (40.0, 3.0, 80.0), "limitExp": (1.5, 0.2, 5.0),
            "floorRange": (70.0, 20.0, 120.0), "cliffTolerance": (9.0, 1.0, 40.0),
            "attackBase": (0.0005, 1e-5, 0.05), "releaseBase": (0.005, 1e-4, 0.5), "freqExp": (0.5, 0.0, 1.5),
        },
    },
    "ballistics": {
        "cases": [(s, c) for c in ["attack_0", "attack_10", "release_0", "release_10", "core_d10"]
                  for s in ["pink_moving_peak", "saw_chord"] + VOCALS],
        "params": {
            "attackBase": (0.0005, 1e-5, 0.05), "attackSlope": (0.9, 0.1, 2.0),
            "releaseBase": (0.005, 1e-4, 0.5), "releaseSlope": (0.75, 0.1, 2.0),
            "freqExp": (0.5, 0.0, 1.5),
        },
    },
    "hard": {
        "cases": [(s, c) for c in ["hard_d5"] for s in CORE_SIG]
        + [("pink_peaks", "hard_d10"), ("pink_peaks_ramp", "hard_d10"), ("pink_peaks_ramp", "hard_d5")],
        "params": {"hardGain": (0.5, 0.0, 6.0), "hardRef": (-40.0, -120.0, 0.0)},
    },
}


def load_tuning():
    return json.loads(TUNING_FILE.read_text()) if TUNING_FILE.exists() else {}


CROP = (1.0, 4.0)  # seconds of each signal used for fitting (after the attack transient)


def crop(src, dst):
    from scipy.io import wavfile

    sr, x = wavfile.read(src)
    a, b = int(CROP[0] * sr), int(CROP[1] * sr)
    if len(x) < b:  # short clips (the vocals) are used whole
        a, b = 0, len(x)
    wavfile.write(dst, sr, x[a:b])
    return dst


class Evaluator:
    def __init__(self, cases):
        self.cases = [(s, c) for s, c in cases if (REF / f"{s}__{c}.wav").exists()]
        self.tmp = Path(tempfile.mkdtemp(prefix="rsfit_"))
        self.src, self.ref = {}, {}
        for s, c in self.cases:
            name = f"{s}__{c}"
            if s not in self.src:
                self.src[s] = crop(source_for(name), self.tmp / f"src_{s}.wav")
            ref = crop(REF / f"{name}.wav", self.tmp / f"ref_{name}.wav")
            f, g = gain_curve(self.src[s], ref)
            self.band = (f > 40) & (f < 16000)
            self.ref[(s, c)] = g

    def render_one(self, case, tuning):
        s, c = case
        out = self.tmp / f"{s}__{c}.wav"
        settings = CASES[c][0]
        cmd = [str(RSDSP), str(self.src[s]), str(out)] + settings + [f"tune:{k}={v}" for k, v in tuning.items()]
        subprocess.run(cmd, capture_output=True, creationflags=IDLE)
        _, g = gain_curve(self.src[s], out)
        return float(np.sqrt(np.mean((g[self.band] - self.ref[case][self.band]) ** 2)))

    def losses(self, tuning):
        with ThreadPoolExecutor(WORKERS) as pool:
            return list(pool.map(lambda case: self.render_one(case, tuning), self.cases))


def run_stage(name):
    stage = STAGES[name]
    base = load_tuning()
    ev = Evaluator(stage["cases"])
    keys = list(stage["params"])
    lo = np.array([stage["params"][k][1] for k in keys])
    hi = np.array([stage["params"][k][2] for k in keys])
    x0 = np.array([base.get(k, stage["params"][k][0]) for k in keys])

    # Optimise in a normalised 0..1 space so Powell's steps are comparable across parameters.
    to_tuning = lambda u: {**base, **{k: float(v) for k, v in zip(keys, lo + np.clip(u, 0, 1) * (hi - lo))}}
    best = {"loss": np.inf, "tuning": to_tuning((x0 - lo) / (hi - lo))}
    count = [0]

    def objective(u):
        t = to_tuning(u)
        loss = float(np.mean(ev.losses(t)))
        count[0] += 1
        if loss < best["loss"]:
            best.update(loss=loss, tuning=t)
            TUNING_FILE.write_text(json.dumps(t, indent=1))
            print(f"[{count[0]:4d}] loss {loss:.3f}  " + " ".join(f"{k}={t[k]:.4g}" for k in keys), flush=True)
        return loss

    u0 = (x0 - lo) / (hi - lo)
    print(f"stage {name}: {len(ev.cases)} cases, start loss {np.mean(ev.losses(best['tuning'])):.3f}", flush=True)
    minimize(objective, u0, method="Powell", bounds=[(0, 1)] * len(keys), options={"maxfev": 1500, "xtol": 1e-3, "ftol": 1e-3})
    print(f"done: best loss {best['loss']:.3f}")


def plot(filters):
    import matplotlib.pyplot as plt

    t = load_tuning()
    cases = sorted({c for st in STAGES.values() for c in st["cases"]})
    cases = [c for c in cases if not filters or any(f in f"{c[0]}__{c[1]}" for f in filters)]
    ev = Evaluator(cases)
    losses = ev.losses(t)
    cols = 4
    rows = (len(ev.cases) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(5 * cols, 2.8 * rows), squeeze=False)
    for ax, case, loss in zip(axes.flat, ev.cases, losses):
        name = f"{case[0]}__{case[1]}"
        f, ours = gain_curve(ev.src[case[0]], ev.tmp / f"{name}.wav")
        ax.semilogx(f[1:], ev.ref[case][1:], label="soothe3", lw=1.2)
        ax.semilogx(f[1:], ours[1:], label="ours", lw=1.2)
        ax.set_xlim(30, 20000)
        ax.set_ylim(-20, 3)
        ax.grid(True, which="both", alpha=0.3)
        ax.set_title(f"{name} ({loss:.1f})", fontsize=8)
    for ax in list(axes.flat)[len(ev.cases):]:
        ax.axis("off")
    axes.flat[0].legend(fontsize=7)
    fig.tight_layout()
    out = ROOT / "renders/fit_plot.png"
    fig.savefig(out, dpi=60)
    print(f"mean loss {np.mean(losses):.3f} -> {out}")


def bake():
    """Write tools/tuning.json into the Tuning defaults in src/dsp/Suppressor.h."""
    import re

    def literal(v):
        s = f"{v:.6g}"
        if "." not in s and "e" not in s:
            s += ".0"  # "40f" is not a valid float literal
        return s + "f"

    header = ROOT / "src/dsp/Suppressor.h"
    text = header.read_text()
    for key, value in load_tuning().items():
        pattern = re.compile(rf"(\b{key} = )-?[0-9.]+(?:e-?[0-9]+)?f")
        text, n = pattern.subn(lambda m: m.group(1) + literal(value), text)
        if n != 1:
            print(f"warning: {key} matched {n} times")
    header.write_text(text)
    print(f"baked {len(load_tuning())} constants into {header}")


def evaluate():
    t = load_tuning()
    cases = sorted({c for st in STAGES.values() for c in st["cases"]})
    ev = Evaluator(cases)
    for case, loss in zip(ev.cases, ev.losses(t)):
        print(f"{case[0]:24s} {case[1]:12s} {loss:6.2f} dB")
    print("tuning:", json.dumps(t))


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "eval"
    if arg == "eval":
        evaluate()
    elif arg == "bake":
        bake()
    elif arg == "plot":
        plot(sys.argv[2:])
    else:
        run_stage(arg)
