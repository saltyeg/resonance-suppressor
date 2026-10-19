# resonance-suppressor

A dynamic resonance suppressor VST3 (in the spirit of oeksound soothe3), built with JUCE 8.

## Build

```powershell
.\build.ps1          # Release; uses the VS 2026 Build Tools, fetches JUCE on first run
```

The plugin is copied to `%USERPROFILE%\VST3-dev\resonance-suppressor.vst3`.
In FL Studio add that folder once under *Options → Manage plugins → Plugin search paths*, then rescan.
FL locks the `.vst3` while it is loaded, so remove the plugin (or close the project) before rebuilding.

## Tools

Python tools live in `.venv` (`python -m venv .venv` then `pip install numpy scipy matplotlib soundfile`).

| Tool | What it does |
| --- | --- |
| `build/Release/rshost_artefacts/Release/rshost.exe` | Headless VST3 host. `params <plugin>` lists parameters; `render <plugin> in.wav out.wav --set "Depth=5" ...` renders offline (latency compensated). |
| `tools/gen_signals.py` | Generates synthetic test signals into `test-audio/synth/`. |
| `tools/analyze.py` | Before/after spectra, gain-change map and numeric summary for one render. |
| `tools/render_matrix.py soothe3\|ours` | Renders every test case through the reference or our plugin into `renders/<target>/`. |

`test-audio/` and `renders/` are not tracked.

## Milestones

1. Scaffold, headless host, reference renders, pass-through plugin
2. Core suppression DSP (detection, per-bin gains, minimum-phase filter)
3. Analyzer GUI with reduction curve
4. Band nodes / sensitivity curve
5. Stereo (L/R, M/S, link), sidechain, quality / linear-phase / low-latency modes
6. Presets, A/B, undo, resizable UI, tuning against the reference
