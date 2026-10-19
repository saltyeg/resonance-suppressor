# resonance-suppressor

A dynamic resonance suppressor VST3 in the spirit of oeksound soothe3, built with JUCE 8. Personal use.

## How it works

Every hop (256 samples at 48 kHz in normal quality) the look-ahead input is analysed with a 2048-point FFT.
Each bin's level is compared with a reference built from its neighbourhood (tilt-neutral, with a fallback at
steep spectral cliffs). Bins sticking out above a threshold get a cut, which is widened across frequency,
limited by depth and smoothed with frequency-dependent attack/release. The cut curve becomes a minimum-phase
FIR (cepstral method) — or linear-phase, or a direct zero-latency FIR — applied to the delayed audio.

Band nodes shape *where* it listens (detection sensitivity), like soothe's bands. The internal constants that map
the unitless knobs to DSP quantities are fitted against soothe3 renders (`tools/fit.py`).

| Mode | Latency @ 48 kHz |
| --- | --- |
| normal / high / ultra | 1152 samples |
| linear phase | 1664 samples |
| zero latency | 0 |

## Build

```powershell
.\build.ps1                     # Release; VS 2026 Build Tools, fetches JUCE on first run
.\build.ps1 -Target rsui        # one target
```

The plugin is copied to `%USERPROFILE%\VST3-dev\resonance-suppressor.vst3`.
In FL Studio add that folder once under *Options → Manage plugins → Plugin search paths*, then rescan.
FL locks the `.vst3` while it is loaded, so remove the plugin (or close the project) before rebuilding.

## Using it

- **Analyzer**: grey = input spectrum, dark line = output, orange = reduction (scale on the right).
- **Band nodes** (blue): drag to move, mouse wheel for Q (slope on cuts), double-click empty space to add,
  double-click a node to remove, right-click for shape / stereo focus / enable.
- Hovering a knob shows its real-unit value (dB, ms at 1 kHz, notch width).
- **more** opens tilts, stereo (L/R, M/S, link, focus), sidechain and advanced Sharpness / Selectivity.
- Presets: 11 factory presets, user presets in `%APPDATA%\resonance-suppressor\Presets`. A/B slots and undo/redo
  are in the header.

## Tools

Python tools use `.venv` (`python -m venv .venv` then `pip install numpy scipy matplotlib soundfile`).

| Tool | What it does |
| --- | --- |
| `rshost.exe params\|render` | Headless VST3 host: list parameters, render offline with `--set "Name=value"` (latency compensated). Hosts soothe3 for reference renders. |
| `rsdsp.exe in.wav out.wav "Depth=8" "tune:key=v"` | Runs the DSP core directly (fast, used for fitting). |
| `rsui.exe in.wav out.png` | Renders the editor offscreen while audio plays through it. |
| `tools/gen_signals.py` | Synthetic test signals into `test-audio/synth/`. |
| `tools/render_matrix.py soothe3\|ours` | Renders every test case through a plugin into `renders/<target>/`. |
| `tools/compare.py` | Overlays soothe3 vs our gain curves. |
| `tools/fit.py core\|ballistics\|hard\|plot\|eval\|bake` | Fits the tuning constants to the soothe3 renders; `bake` writes them into `Suppressor.h`. |

`test-audio/` and `renders/` are not tracked.
