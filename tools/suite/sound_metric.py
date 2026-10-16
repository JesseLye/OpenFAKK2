"""How far one sound recording is from another, for the test suite's sound checks.

    python sound_metric.py <retail.npy> <ours.npy> tone|rms

tone: the levels of the probes' 1 kHz tone, one per plateau (sound_levels.py's
tone_levels and plateaus), compared plateau by plateau and channel by channel.
rms: for music and game sounds, the spread of each recording's power per channel over
the quarter seconds that have sound in them (50th, 75th and 90th percentiles), compared;
it does not depend on when each game started the sound. Prints "max level difference:
<dB> dB".
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


def active_levels(x):
    """Each quarter second's power per channel in dB, for the quarters with sound in them."""
    win = sound_levels.RATE // 4
    n = len(x) // win
    levels = np.array([10 * np.log10(np.maximum((x[i * win:(i + 1) * win].astype(np.float64) ** 2).mean(axis=0), 1e-12)) for i in range(n)])
    return levels[levels.max(axis=1) > -60]


def rms(a, b):
    # The two games start the sound at slightly different moments, so window against
    # window compares sound with silence (90 dB on music). Instead: the spread of levels
    # while there is sound, its 50th, 75th and 90th percentiles per channel. Not the
    # quieter quarters: they are the decays between the uzi's bursts, which moved 6 dB
    # between two runs of ours.
    la, lb = active_levels(a), active_levels(b)
    if not len(la) or not len(lb):
        return 100.0, 'no sound in %s' % ('either recording' if not len(la) and not len(lb) else 'one recording')
    pa = np.percentile(la, [50, 75, 90], axis=0)
    pb = np.percentile(lb, [50, 75, 90], axis=0)
    note = 'sound for %.1f s retail, %.1f s ours; median L/R %.1f/%.1f retail, %.1f/%.1f ours' % (
        len(la) / 4, len(lb) / 4, pa[0][0], pa[0][1], pb[0][0], pb[0][1])
    return float(np.abs(pa - pb).max()), note


def main():
    a = np.load(sys.argv[1])
    b = np.load(sys.argv[2])
    value, note = (tone if sys.argv[3] == 'tone' else rms)(a, b)
    print(note)
    print('max level difference: %.2f dB' % value)


if __name__ == '__main__':
    main()
