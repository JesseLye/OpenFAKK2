"""Compares two demos of the same play, one recorded on the retail game and one on
OpenFAKK2 (the same probe run on each, recording; retail-probe and retail-demo skills), as
a timeline of what the two servers sent: the player, each entity, the sounds started, the
prints and the server commands. Where whatis readings show a moment, this shows when the
two games start to differ.

    python demo_diff.py RETAIL.dm3 OURS.dm3 [--tolerance UNITS] [--offset MS] [--entities N]
                        [--sound-window MS]

Snapshots are paired by time since each demo's first snapshot (--offset MS shifts ours),
nearest first; at the retail engine's 20 server frames a second they are 50 ms apart.
Entity numbers can differ between the games, so entities are paired by model (the model
configstring their modelindex names, or their type for none) and, among the same model,
nearest first; a pairing holds until one side's entity goes. A pair is reported from when
its distance exceeds --tolerance (default 16 units) or its animation differs. A sound is the
same event on both when it is the same sound within --sound-window ms (default 1000).

Only what the server sent is here: the sounds a weapon or an animation plays are started by
the client game module from the model's TIKI file and are not in a demo (play the demo with
the retail-demo skill's sound mode for those).

What makes two runs comparable: the game draws random numbers seeded from the clock, so
actors' choices and spreads part ways; scripted moves, cameras, doors and projectiles stay
comparable. Held keys are timed by rendered frames, so the player drifts a little.
"""
import argparse
import bisect
import difflib
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm3  # noqa: E402

CS_MODELS, CS_SOUNDS = 32, 32 + 384      # bg_public.h: CS_SOUNDS is CS_MODELS + MAX_MODELS
ORIGIN = ('netorigin[0]', 'netorigin[1]', 'netorigin[2]')
PS_ORIGIN = ('origin[0]', 'origin[1]', 'origin[2]')
PS_ANGLES = ('viewangles[0]', 'viewangles[1]')


class Side:
    def __init__(self, path):
        self.path = path
        self.demo = dm3.read_demo(path)
        self.snaps = self.demo.snapshots
        if not self.snaps:
            sys.exit('%s: no snapshots' % path)
        self.t0 = self.snaps[0]['server_time']
        self.times = [s['server_time'] - self.t0 for s in self.snaps]
        cs = self.demo.configstrings
        m = re.search(r'\\mapname\\([^\\]+)', cs.get(0, ''))
        self.map = m.group(1) if m else '?'

    def model(self, e):
        if e['modelindex']:
            return self.demo.configstrings.get(CS_MODELS + e['modelindex'], '#%d' % e['modelindex']) or '#%d' % e['modelindex']
        return 'type %d' % e['eType']

    def sound(self, index):
        return self.demo.configstrings.get(CS_SOUNDS + index, '#%d' % index) or '#%d' % index

    def at(self, t):
        """The snapshot nearest t (ms since the first)."""
        i = bisect.bisect_left(self.times, t)
        if i == len(self.times) or (i and t - self.times[i - 1] < self.times[i] - t):
            i -= 1
        return self.snaps[i]


def dist(a, b, keys):
    return math.sqrt(sum((a[k] - b[k]) ** 2 for k in keys))


def short(name):
    return name.replace('models/', '').replace('.tik', '')


def secs(ms):
    return '%6.2f s' % (ms / 1000.0)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('retail')
    p.add_argument('ours')
    p.add_argument('--tolerance', type=float, default=16.0)
    p.add_argument('--offset', type=int, default=0)
    p.add_argument('--entities', type=int, default=25)
    p.add_argument('--sound-window', type=int, default=1000)
    args = p.parse_args()
    r, o = Side(args.retail), Side(args.ours)

    print('retail: %s, %d snapshots over %s' % (r.map, len(r.snaps), secs(r.times[-1])))
    print('ours:   %s, %d snapshots over %s' % (o.map, len(o.snaps), secs(o.times[-1])))
    if r.map != o.map:
        print('the demos are of different maps')

    # --- the player -------------------------------------------------------------------
    first_pos = first_ang = None
    worst_pos = (0, 0)
    stat_first = {}
    for rs in r.snaps:
        t = rs['server_time'] - r.t0
        os_ = o.at(t + args.offset)
        rp, op = rs['ps'], os_['ps']
        d = dist(rp, op, PS_ORIGIN)
        if d > worst_pos[0]:
            worst_pos = (d, t)
        if d > args.tolerance and first_pos is None:
            first_pos = (t, d)
        da = max(abs((rp[k] - op[k] + 180) % 360 - 180) for k in PS_ANGLES)
        if da > 5 and first_ang is None:
            first_ang = (t, da)
        for i, (x, y) in enumerate(zip(rp['stats'], op['stats'])):
            if x != y and i not in stat_first:
                stat_first[i] = (t, x, y)
    print('\nplayer:')
    print('  position: %s' % ('within %.0f units throughout (worst %.1f at %s)' % (args.tolerance, worst_pos[0], secs(worst_pos[1]))
                              if first_pos is None else 'apart from %s (%.1f units), worst %.1f at %s'
                              % (secs(first_pos[0]), first_pos[1], worst_pos[0], secs(worst_pos[1]))))
    print('  view: %s' % ('within 5 degrees throughout' if first_ang is None else 'apart from %s (%.1f degrees)' % (secs(first_ang[0]), first_ang[1])))
    for i, (t, x, y) in sorted(stat_first.items(), key=lambda kv: kv[1][0])[:8]:
        print('  stats[%d] first differs at %s: retail %d, ours %d' % (i, secs(t), x, y))

    # --- entities ---------------------------------------------------------------------
    pairs = {}           # retail number -> ours number
    report = {}          # retail number -> record
    only_r, only_o = {}, {}
    for rs in r.snaps:
        t = rs['server_time'] - r.t0
        os_ = o.at(t + args.offset)
        re_, oe = rs['entities'], os_['entities']
        # drop pairings whose entity went on either side
        for a in list(pairs):
            if a not in re_ or pairs[a] not in oe:
                del pairs[a]
        # new pairings: per model, the closest two first (walking one side in number
        # order swapped two creepers that stand close together)
        taken = set(pairs.values())
        new_r, new_o = {}, {}
        for n, e in re_.items():
            if n != 0 and n not in pairs:
                new_r.setdefault(r.model(e), []).append(n)
        for n, e in oe.items():
            if n != 0 and n not in taken:
                new_o.setdefault(o.model(e), []).append(n)
        for m in set(new_r) | set(new_o):
            ra, ob = new_r.get(m, []), new_o.get(m, [])
            for d, a, b in sorted((dist(re_[a], oe[b], ORIGIN), a, b) for a in ra for b in ob):
                if a in ra and b in ob:
                    pairs[a] = b
                    ra.remove(a)
                    ob.remove(b)
            for a in ra:
                only_r.setdefault((m, a), t)
            for b in ob:
                only_o.setdefault((m, b), t)
        for a, b in pairs.items():
            ea, eb = re_[a], oe[b]
            rec = report.setdefault(a, {'model': r.model(ea), 'ours': b, 'since': t, 'apart': None, 'worst': 0.0, 'anim': None})
            d = dist(ea, eb, ORIGIN)
            rec['worst'] = max(rec['worst'], d)
            if d > args.tolerance and rec['apart'] is None:
                rec['apart'] = (t, d)
            if (ea['anim'], ea['torso_anim']) != (eb['anim'], eb['torso_anim']) and rec['anim'] is None:
                rec['anim'] = (t, ea['anim'], eb['anim'], ea['torso_anim'], eb['torso_anim'])
    # pairings made late (an entity that was only on one side for a moment) are not "only"
    paired_r = set(report)
    paired_o = {rec['ours'] for rec in report.values()}
    only_r = {k: v for k, v in only_r.items() if k[1] not in paired_r}
    only_o = {k: v for k, v in only_o.items() if k[1] not in paired_o}

    differing = [(a, rec) for a, rec in report.items() if rec['apart'] or rec['anim']]
    differing.sort(key=lambda ar: min(x[0] for x in (ar[1]['apart'], ar[1]['anim']) if x))
    print('\nentities: %d paired, %d differ, %d only on retail, %d only on ours'
          % (len(report), len(differing), len(only_r), len(only_o)))
    for a, rec in differing[:args.entities]:
        bits = []
        if rec['apart']:
            bits.append('apart from %s (%.0f units, worst %.0f)' % (secs(rec['apart'][0]), rec['apart'][1], rec['worst']))
        if rec['anim']:
            t, ra, oa, rt, ot = rec['anim']
            bits.append('animation differs from %s (legs %d/%d, torso %d/%d)' % (secs(t), ra, oa, rt, ot))
        print('  %-34s retail #%-4d ours #%-4d %s' % (short(rec['model'])[-34:], a, rec['ours'], '; '.join(bits)))
    for label, only in (('only on retail', only_r), ('only on ours', only_o)):
        for (m, n), t in sorted(only.items(), key=lambda kv: kv[1])[:10]:
            print('  %-34s %-15s #%-4d from %s' % (short(m)[-34:], label, n, secs(t)))

    # --- sounds -----------------------------------------------------------------------
    def starts(side):
        out = []
        for s in side.snaps:
            t = s['server_time'] - side.t0
            for snd in s['sounds']:
                if not snd['stop']:
                    ent = s['entities'].get(snd['entity'])
                    out.append((t, side.sound(snd['index']), short(side.model(ent)) if ent else '#%d' % snd['entity']))
        return out
    # the same sound on both within --sound-window ms is one event, nearest first
    sr, so = starts(r), starts(o)
    print('\nsounds started: retail %d, ours %d' % (len(sr), len(so)))
    cands = sorted((abs(b[0] - args.offset - a[0]), i, j) for i, a in enumerate(sr) for j, b in enumerate(so)
                   if a[1] == b[1] and abs(b[0] - args.offset - a[0]) <= args.sound_window)
    used_r, used_o, lags = set(), set(), []
    for _, i, j in cands:
        if i not in used_r and j not in used_o:
            used_r.add(i); used_o.add(j)
            lags.append(so[j][0] - args.offset - sr[i][0])
    unmatched = [('only retail', x) for i, x in enumerate(sr) if i not in used_r] + \
                [('only ours  ', x) for j, x in enumerate(so) if j not in used_o]
    for label, x in sorted(unmatched, key=lambda u: u[1][0])[:30]:
        print('  %s %s  %-44s on %s' % (label, secs(x[0]), x[1][-44:], x[2]))
    if lags:
        late = sorted(lags, key=abs)[-1]
        print('  %d in both (within %d ms); ours starts them %+d ms on average, worst %+d ms'
              % (len(lags), args.sound_window, sum(lags) / len(lags), late))

    # --- prints and server commands ---------------------------------------------------
    for label, a, b in (('prints', r.demo.prints, o.demo.prints),
                        ('server commands', [c[1] for c in r.demo.commands], [c[1] for c in o.demo.commands])):
        a, b = [str(x) for x in a], [str(x) for x in b]
        diff = [l for l in difflib.unified_diff(a, b, lineterm='', n=0) if l[:1] in '+-' and l[:3] not in ('+++', '---')]
        print('\n%s: retail %d, ours %d%s' % (label, len(a), len(b), '' if diff else ', the same'))
        for l in diff[:15]:
            print('  %s %s' % ('ours  ' if l[0] == '+' else 'retail', l[1:][:110]))


if __name__ == '__main__':
    main()
