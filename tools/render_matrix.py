"""Render the test signals through a plugin with a matrix of settings, then analyze each render.

    python tools/render_matrix.py soothe3            # reference renders (needs the soothe3 demo)
    python tools/render_matrix.py ours               # our plugin from ~/VST3-dev
    python tools/render_matrix.py soothe3 --only core_d5,hard_d5

Output: renders/<target>/<signal>__<case>.wav/.png and renders/<target>/summary.json
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from analyze import analyze  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "build/Release/rshost_artefacts/Release/rshost.exe"
TARGETS = {
    "soothe3": Path(r"C:\Program Files\Common Files\VST3\soothe3.vst3"),
    "ours": Path(os.environ["USERPROFILE"]) / "VST3-dev/resonance-suppressor.vst3",
}

SYNTH = ROOT / "test-audio/synth"
REAL = sorted((ROOT / "test-audio/real").glob("*.wav"))

# Our plugin mirrors soothe3's parameter names where a feature exists in both, so one matrix serves both.
NO_BANDS = ["Band 1 enabled=off", "Band 2 enabled=off", "Band 3 enabled=off"]

CORE_SIGNALS = ["pink_peaks", "pink_peaks_ramp", "pink_moving_peak", "pink_flat", "saw_chord"]
REAL_SIGNALS = [p.stem for p in REAL]

CASES = {
    # name: (settings, signals)
    "core_d5": (NO_BANDS + ["Depth=5"], CORE_SIGNALS + REAL_SIGNALS + ["sweep", "clicks"]),
    "core_d10": (NO_BANDS + ["Depth=10"], CORE_SIGNALS + REAL_SIGNALS),
    "core_d2": (NO_BANDS + ["Depth=2"], CORE_SIGNALS),
    "detail_1": (NO_BANDS + ["Depth=8", "Detail=1"], CORE_SIGNALS + REAL_SIGNALS),
    "detail_9": (NO_BANDS + ["Depth=8", "Detail=9"], CORE_SIGNALS + REAL_SIGNALS),
    "attack_0": (NO_BANDS + ["Depth=8", "Attack=0"], ["pink_moving_peak", "saw_chord", "clicks"] + REAL_SIGNALS),
    "attack_10": (NO_BANDS + ["Depth=8", "Attack=10"], ["pink_moving_peak", "saw_chord", "clicks"] + REAL_SIGNALS),
    "release_0": (NO_BANDS + ["Depth=8", "Release=0"], ["pink_moving_peak", "saw_chord"] + REAL_SIGNALS),
    "release_10": (NO_BANDS + ["Depth=8", "Release=10"], ["pink_moving_peak", "saw_chord"] + REAL_SIGNALS),
    "tilt_detail_hi": (NO_BANDS + ["Depth=8", "Detail tilt high=norm:1"], ["pink_peaks", "saw_chord"]),
    "tilt_detail_lo": (NO_BANDS + ["Depth=8", "Detail tilt low=norm:1"], ["pink_peaks", "saw_chord"]),
    "tilt_attack_hi": (NO_BANDS + ["Depth=8", "Attack tilt high=norm:1"], ["pink_moving_peak"]),
    "tilt_release_lo": (NO_BANDS + ["Depth=8", "Release tilt low=norm:1"], ["pink_moving_peak"]),
    "hard_d5": (NO_BANDS + ["Mode=hard", "Depth=5"], CORE_SIGNALS + REAL_SIGNALS),
    "hard_d10": (NO_BANDS + ["Mode=hard", "Depth=10"], ["pink_peaks", "pink_peaks_ramp"]),
    "delta_d8": (NO_BANDS + ["Depth=8", "Delta=on"], ["pink_peaks"] + REAL_SIGNALS),
    "maxcut_6": (NO_BANDS + ["Depth=10", "Max cut=6"], ["pink_peaks"] + REAL_SIGNALS),
    "mix_50": (NO_BANDS + ["Depth=10", "Mix=50"], ["pink_peaks"]),
    "quality_high": (NO_BANDS + ["Depth=8", "Quality=high"], ["pink_moving_peak", "clicks"]),
    "quality_ultra": (NO_BANDS + ["Depth=8", "Quality=ultra"], ["pink_moving_peak", "clicks"]),
    "linear_phase": (NO_BANDS + ["Depth=8", "Linear phase=on"], ["pink_peaks", "clicks", "sweep"]),
    "low_latency": (NO_BANDS + ["Depth=8", "Low latency=on"], ["pink_peaks", "pink_moving_peak", "clicks"] + REAL_SIGNALS),
    "stereo_lr_link100": (NO_BANDS + ["Depth=8"], ["stereo_split_peaks"]),
    "stereo_lr_link0": (NO_BANDS + ["Depth=8", "Stereo link=0"], ["stereo_split_peaks"]),
    "stereo_ms": (NO_BANDS + ["Depth=8", "Stereo mode=M/S", "Stereo link=0"], ["stereo_split_peaks"]),
    "factory_default": ([], CORE_SIGNALS + REAL_SIGNALS),
    "band_bell_3k_boost": (NO_BANDS + ["Depth=5", "Band 4 used=on", "Band 4 enabled=on", "Band 4 shape=Bell",
                                       "Band 4 frequency=3100", "Band 4 depth=norm:1"], ["pink_peaks", "pink_flat"]),
    "band_bell_3k_cut": (NO_BANDS + ["Depth=5", "Band 4 used=on", "Band 4 enabled=on", "Band 4 shape=Bell",
                                     "Band 4 frequency=3100", "Band 4 depth=norm:0"], ["pink_peaks", "pink_flat"]),
    "band_lowcut_1k": (["Band 2 enabled=off", "Band 3 enabled=off", "Depth=8", "Band 1 frequency=1000"], ["pink_peaks", "pink_flat"]),
    "sidechain_2k": (NO_BANDS + ["Depth=8", "Sidechain=on"], ["pink_flat"]),
}
SIDECHAIN = {"sidechain_2k": SYNTH / "sc_peak_2k.wav"}


def signal_path(name):
    p = SYNTH / f"{name}.wav"
    return p if p.exists() else ROOT / "test-audio/real" / f"{name}.wav"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target", choices=TARGETS)
    ap.add_argument("--only")
    ap.add_argument("--no-png", action="store_true")
    args = ap.parse_args()

    plugin = TARGETS[args.target]
    out_dir = ROOT / "renders" / args.target
    out_dir.mkdir(parents=True, exist_ok=True)
    summary_file = out_dir / "summary.json"
    summary = json.loads(summary_file.read_text()) if summary_file.exists() else {}

    only = set(args.only.split(",")) if args.only else None
    for case, (settings, signals) in CASES.items():
        if only and case not in only:
            continue
        for sig in signals:
            src = signal_path(sig)
            dst = out_dir / f"{sig}__{case}.wav"
            cmd = [str(HOST), "render", str(plugin), str(src), str(dst)]
            if args.target == "ours":
                cmd.append("--lenient")  # features we haven't built yet are skipped
            for s in settings:
                cmd += ["--set", s]
            if case in SIDECHAIN:
                cmd += ["--sidechain", str(SIDECHAIN[case])]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                print(f"FAIL {sig}__{case}: {r.stderr.strip() or r.stdout.strip()}")
                continue
            latency = next((int(l.split("latency ")[1].split()[0]) for l in r.stdout.splitlines() if l.startswith("rate ")), None)
            png = None if args.no_png else dst.with_suffix(".png")
            s = analyze(src, dst, png, f"{args.target}: {sig} / {case}")
            s["latency"] = latency
            summary[f"{sig}__{case}"] = {k: (round(float(v), 2) if v is not None else None) for k, v in s.items()}
            print(f"{sig:22s} {case:20s} lat={latency:<6} cut={s['deepest_avg_cut_db']:6.1f} dB @ {s['deepest_avg_cut_hz']:7.0f} Hz  out-in={s['out_rms_db'] - s['in_rms_db']:5.1f} dB")

    summary_file.write_text(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
