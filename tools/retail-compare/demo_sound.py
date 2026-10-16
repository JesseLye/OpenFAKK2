"""Compares how the retail game and OpenFAKK2 play the sounds of one demo: two loopback
recordings of the same .dm3 played at its own speed (sound-record.ps1 -Demo), and the
sounds each game logged as it started them (retail's s_show_sounds, ours' s_show).

    python demo_sound.py RETAIL.npy OURS.npy [--retail-log LOG] [--ours-log LOG] [--window S]

The recordings start whenever each game's window came up, so they are aligned first: the
offset that best matches their loudness envelopes (10 ms steps). Then, per window of
--window seconds (default 1) of the demo, each game's level in dBFS per channel and the
difference, marking windows where they differ by 3 dB or more, or where one is silent.
The logs are compared as sequences of sound names: sounds only one game started, and
volumes or minimum distances that differ for the same sound. Needs numpy.
"""
import argparse
import difflib
import re

import numpy as np

RATE = 48000
STEP = RATE // 100           # 10 ms
SILENT = -70.0               # dBFS: below this a window counts as silence
START = re.compile(r'^\s*(-?\d+) - (\S+) \(vol (-?[\d.]+), mindist (-?[\d.]+)\)')
VARIANT = re.compile(r'\d+(?=\.\w+$)')    # the number before the extension: site1.wav, site2.wav


def envelope(x):
    n = len(x) // STEP
    p = (x[:n * STEP].astype(np.float64) ** 2).reshape(n, STEP, -1).mean(axis=(1, 2))
    return 10 * np.log10(np.maximum(p, 1e-12))


def align(a, b):
    """Samples to drop from the start of a and of b so the two line up."""
    ea, eb = envelope(a), envelope(b)
    # only where there is sound: silence matches anything
    za, zb = np.maximum(ea, SILENT) - SILENT, np.maximum(eb, SILENT) - SILENT
    if not za.any() or not zb.any():
        return 0, 0
    corr = np.correlate(za - za.mean(), zb - zb.mean(), mode='full')
    lag = int(corr.argmax()) - (len(zb) - 1)     # a[i + lag] matches b[i]
    return (lag * STEP, 0) if lag >= 0 else (0, -lag * STEP)


def levels(x):
    p = (x.astype(np.float64) ** 2).mean(axis=0)
    return 10 * np.log10(np.maximum(p, 1e-15))


def read_starts(path):
    out = []
    for line in open(path, encoding='latin-1', errors='replace'):
        m = START.match(line)
        if m:
            out.append((m.group(2).lower().replace('\\', '/'), float(m.group(3)), float(m.group(4))))
    return out


def compare_logs(retail, ours):
    a, b = read_starts(retail), read_starts(ours)
    print('\nsounds started: retail %d, ours %d' % (len(a), len(b)))
    if not a or not b:
        print('  (a log has no sound lines: ours prints them with the software mixer only, -Engine dma)')
        return
    sm = difflib.SequenceMatcher(None, [s[0] for s in a], [s[0] for s in b], autojunk=False)
    ops = sm.get_opcodes()
    # retail prints a volume already scaled by s_volume, ours unscaled: the ratio both games
    # keep for most sounds is that scale, and only a sound off it differs
    ratios = sorted(a[i1 + k][1] / b[j1 + k][1] for tag, i1, i2, j1, j2 in ops if tag == 'equal'
                    for k in range(i2 - i1) if b[j1 + k][1] > 0)
    scale = ratios[len(ratios) // 2] if ratios else 1.0
    if abs(scale - 1) > 0.01:
        print("  retail's printed volumes are ours' x %.3f throughout (retail's print includes s_volume); compared at that scale" % scale)
    shown = variants = 0
    for tag, i1, i2, j1, j2 in ops:
        if tag == 'equal':
            for k in range(i2 - i1):
                ra, ob = a[i1 + k], b[j1 + k]
                if (abs(ra[1] - ob[1] * scale) > 0.01 or abs(ra[2] - ob[2]) > 0.5) and shown < 40:
                    print('  %-48s vol %.2f / %.2f  mindist %.0f / %.0f  (retail / ours x scale)' % (ra[0][-48:], ra[1], ob[1] * scale, ra[2], ob[2]))
                    shown += 1
            continue
        # an alias with several sounds (site1, site2) picks one at random on each game: the
        # same place in the sequence with the same name but its number is a variant
        ra, ob = list(a[i1:i2]), list(b[j1:j2])
        if tag == 'replace':
            for k in range(min(len(ra), len(ob))):
                if ra[k] and ob[k] and VARIANT.sub('', ra[k][0]) == VARIANT.sub('', ob[k][0]):
                    variants += 1
                    ra[k] = ob[k] = None
        for s in ra:
            if s and shown < 40:
                print('  only retail: %s' % s[0]); shown += 1
        for s in ob:
            if s and shown < 40:
                print('  only ours:   %s' % s[0]); shown += 1
    if variants:
        print('  %d chose a different variant of the same sound (drawn at random on each game)' % variants)
    if not shown and not variants:
        print('  the same sounds in the same order, with the same volumes and minimum distances')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('retail')
    p.add_argument('ours')
    p.add_argument('--retail-log')
    p.add_argument('--ours-log')
    p.add_argument('--window', type=float, default=1.0)
    args = p.parse_args()

    a, b = np.load(args.retail), np.load(args.ours)
    da, db = align(a, b)
    a, b = a[da:], b[db:]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    print('aligned: retail from %.2f s, ours from %.2f s of their recordings; %.1f s compared'
          % (da / RATE, db / RATE, n / RATE))
    la, lb = levels(a), levels(b)
    print('whole:  retail L %6.2f R %6.2f   ours L %6.2f R %6.2f   ours - retail L %+5.2f R %+5.2f dB'
          % (la[0], la[1], lb[0], lb[1], lb[0] - la[0], lb[1] - la[1]))
    w = int(args.window * RATE)
    print('\n  time    retail L/R      ours L/R      diff L/R')
    flagged = 0
    for k in range(n // w):
        xa, xb = levels(a[k * w:(k + 1) * w]), levels(b[k * w:(k + 1) * w])
        if xa.max() < SILENT and xb.max() < SILENT:
            continue
        d = xb - xa
        silent = ['%s silent on %s' % ('LR'[c], 'ours' if xb[c] < SILENT else 'retail')
                  for c in range(2) if (xa[c] < SILENT) != (xb[c] < SILENT)]
        mark = '  <- ' + ', '.join(silent) if silent else ('  <- %+.1f dB' % d[np.abs(d).argmax()] if np.abs(d).max() >= 3 else '')
        d = np.where((xa < SILENT) | (xb < SILENT), 0, d)
        flagged += bool(mark)
        print('%6.1f s  %6.1f/%6.1f  %6.1f/%6.1f  %+5.1f/%+5.1f%s' % (k * args.window, xa[0], xa[1], xb[0], xb[1], d[0], d[1], mark))
    print('%d window(s) differ by 3 dB or more, or are silent on one side' % flagged)
    if args.retail_log and args.ours_log:
        compare_logs(args.retail_log, args.ours_log)


if __name__ == '__main__':
    main()
