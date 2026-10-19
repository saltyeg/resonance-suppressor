"""Generate synthetic test signals into test-audio/synth/ (48 kHz, stereo, 32-bit float WAV)."""
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
OUT = Path(__file__).resolve().parent.parent / "test-audio" / "synth"
rng = np.random.default_rng(1234)


def pink(n):
    # Voss-McCartney approximation via 1/f filtering of white noise in the frequency domain.
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = f[1]
    spec /= np.sqrt(f)
    x = np.fft.irfft(spec, n)
    return x / np.max(np.abs(x))


def peaking(x, f0, gain_db, q):
    # RBJ peaking EQ.
    a = 10 ** (gain_db / 40)
    w = 2 * np.pi * f0 / SR
    alpha = np.sin(w) / (2 * q)
    b = [1 + alpha * a, -2 * np.cos(w), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w), 1 - alpha / a]
    return signal.lfilter(b, den, x)


def normalize(x, peak_db=-6.0):
    return x / np.max(np.abs(x)) * 10 ** (peak_db / 20)


def write(name, x):
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    OUT.mkdir(parents=True, exist_ok=True)
    wavfile.write(OUT / f"{name}.wav", SR, x.astype(np.float32))
    print("wrote", name)


PEAKS = [(900, 12, 8), (2700, 12, 10), (5500, 12, 12)]


def planted(x):
    for f0, g, q in PEAKS:
        x = peaking(x, f0, g, q)
    return x


n = SR * 8

write("pink_flat", normalize(pink(n)))
write("pink_peaks", normalize(planted(pink(n))))

# Same planted peaks with a level ramp from -40 dB to 0 dB: soft mode should be ~level independent.
ramp = 10 ** (np.linspace(-40, 0, n) / 20)
write("pink_peaks_ramp", normalize(planted(pink(n))) * ramp)

# A single resonance sweeping 500 Hz -> 8 kHz, block-wise.
x = pink(n)
y = np.zeros(n)
blk = 1024
zi = None
for i in range(0, n, blk):
    f0 = 500 * (16 ** (i / n))
    a = 10 ** (15 / 40)
    w = 2 * np.pi * f0 / SR
    alpha = np.sin(w) / (2 * 10)
    b = [1 + alpha * a, -2 * np.cos(w), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w), 1 - alpha / a]
    if zi is None:
        zi = signal.lfilter_zi(b, den) * 0
    y[i:i + blk], zi = signal.lfilter(b, den, x[i:i + blk], zi=zi)
write("pink_moving_peak", normalize(y))

# Exponential sine sweep 20 Hz - 20 kHz.
t = np.arange(n) / SR
write("sweep", normalize(signal.chirp(t, 20, t[-1], 20000, method="logarithmic")))

# Harmonic-rich sawtooth chord with slow vibrato (resonance-like partials that move).
saw = np.zeros(n)
for f0 in (110, 138.6, 164.8):
    vib = 1 + 0.01 * np.sin(2 * np.pi * 0.3 * t)
    phase = 2 * np.pi * np.cumsum(f0 * vib) / SR
    saw += signal.sawtooth(phase)
write("saw_chord", normalize(signal.lfilter(*signal.butter(2, 8000 / (SR / 2)), saw)))

# Clicks every 250 ms, for pre-ringing / phase checks.
clicks = np.zeros(n)
clicks[SR // 8::SR // 4] = 1.0
write("clicks", clicks * 0.5)

# Stereo: different planted peak on each side (900 Hz left, 3.3 kHz right) for L/R vs M/S tests.
left = peaking(pink(n), 900, 12, 8)
right = peaking(pink(n), 3300, 12, 8)
st = np.stack([normalize(left), normalize(right)], axis=1)
write("stereo_split_peaks", st)

# Sidechain material: pure pink noise with a 2 kHz peak.
write("sc_peak_2k", normalize(peaking(pink(n), 2000, 15, 8)))
