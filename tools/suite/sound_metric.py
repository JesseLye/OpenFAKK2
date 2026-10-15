"""How far one sound recording is from another, for the test suite's sound checks.

    python sound_metric.py <retail.npy> <ours.npy> tone|rms

tone: the levels of the probes' 1 kHz tone, one per plateau (sound_levels.py's
tone_levels and plateaus), compared plateau by plateau and channel by channel.
rms: each recording's power per channel in one-second windows, compared window by
window over the length of the shorter. Prints "max level difference: <dB> dB".
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'retail-compare'))
import sound_levels  # noqa: E402


def tone(a, b):
    pa = sound_levels.plateaus(sound_levels.tone_levels(a))
    pb = sound_levels.plateaus(sound_levels.tone_levels(b))
    n = min(len(pa), len(pb))
    if not n:
        return 100.0, 'no tone plateaus in %s' % ('either recording' if not pa and not pb else 'one recording')
    diffs = [np.abs(np.asarray(pa[i]) - np.asarray(pb[i])).max() for i in range(n)]
    note = '%d plateaus retail, %d ours' % (len(pa), len(pb))
    return float(max(diffs)) + (10.0 * abs(len(pa) - len(pb))), note


def rms(a, b):
    rate = sound_levels.RATE
    n = min(len(a), len(b)) // rate
    out = []
    for i in range(n):
        wa = a[i * rate:(i + 1) * rate].astype(np.float64)
        wb = b[i * rate:(i + 1) * rate].astype(np.float64)
        la = 10 * np.log10(np.maximum((wa ** 2).mean(axis=0), 1e-12))
        lb = 10 * np.log10(np.maximum((wb ** 2).mean(axis=0), 1e-12))
        if max(la.max(), lb.max()) > -60:  # ignore silence on both
            out.append(np.abs(la - lb).max())
    if not out:
        return 100.0, 'no sound in either recording'
    return float(max(out)), '%d one-second windows compared' % len(out)


def main():
    a = np.load(sys.argv[1])
    b = np.load(sys.argv[2])
    value, note = (tone if sys.argv[3] == 'tone' else rms)(a, b)
    print(note)
    print('max level difference: %.2f dB' % value)


if __name__ == '__main__':
    main()
