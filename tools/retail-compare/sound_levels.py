"""Levels in sound_capture.py recordings.

    python sound_levels.py tone [labels] <a.npy> [b.npy ...]
    python sound_levels.py rms <from_s> <to_s> <a.npy> [b.npy ...]

tone: the steady levels of the probes' 1 kHz tone (985 to 1015 Hz, as the RMS of a sine,
in dBFS per channel), one row per plateau (quarter seconds within 0.3 dB of each other
for at least 1.5 s) and one left/right column per recording. labels, comma separated,
name the rows. rms: each recording's mean power per channel between two times, for
music or a whole scene.
"""
import sys

import numpy as np

RATE = 48000
WIN = RATE // 4


def tone_levels(data):
    freqs = np.fft.rfftfreq(WIN, 1 / RATE)
    band = (freqs > 985) & (freqs < 1015)
    window = np.hanning(WIN)[:, None]
    out = []
    for i in range(len(data) // WIN):
        spec = np.fft.rfft(data[i * WIN:(i + 1) * WIN] * window, axis=0)
        power = (np.abs(spec[band]) ** 2).sum(axis=0) * 2 / (WIN * WIN * 0.375)  # Hann's power gain
        out.append(10 * np.log10(np.maximum(power, 1e-15)))
    return np.array(out)


def plateaus(levels):
    found, start = [], 0
    for i in range(1, len(levels) + 1):
        if i == len(levels) or np.abs(levels[i] - levels[i - 1]).max() > 0.3:
            if i - start >= 6 and levels[start:i].max() > -120:
                found.append(np.median(levels[start + 1:i - 1], axis=0))
            start = i
    return found


def name(path):
    return path.replace("\\", "/").split("/")[-1].removesuffix(".npy")


# run as a script; sound_metric.py imports the functions above
if __name__ == "__main__":
    if sys.argv[1] == "tone":
        args = sys.argv[2:]
        labels = args.pop(0).split(",") if not args[0].endswith(".npy") else []
        cols = [plateaus(tone_levels(np.load(f).astype(np.float64))) for f in args]
        print(" " * 10 + "".join(f"{name(f)[:19]:>20}" for f in args))
        for k in range(max(len(c) for c in cols)):
            row = f"{labels[k] if k < len(labels) else k:<10}"
            for c in cols:
                row += f"{'%6.1f/%6.1f' % tuple(c[k]) if k < len(c) else '':>20}"
            print(row)
    else:
        a, b = float(sys.argv[2]), float(sys.argv[3])
        for f in sys.argv[4:]:
            x = np.load(f)[int(a * RATE):int(b * RATE)].astype(np.float64)
            p = (x ** 2).mean(axis=0)
            db = 10 * np.log10(np.maximum(np.append(p, p.mean()), 1e-15))
            print(f"{name(f):<40} L {db[0]:6.2f}  R {db[1]:6.2f}  L+R {db[2]:6.2f}")
