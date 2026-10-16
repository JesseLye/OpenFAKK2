"""Screenshot tools for retail-vs-OpenFAKK2 captures. Standard library only (no PIL here).

  shots.py png   IN.tga OUT.png [--crop X0 Y0 X1 Y1] [--zoom N]
      Convert a game screenshot to PNG (the Read tool shows PNGs), optionally cropped and
      enlarged with nearest-neighbour pixels.

  shots.py pair  A.tga B.tga OUT.png [--crop ...] [--zoom N] [--mask X0 Y0 X1 Y1]... [--mask-notify]
      A | B | |A-B|x4 side by side, and the difference figures: mean absolute difference per
      channel, the share of pixels whose largest channel differs by more than --threshold
      (default 24), and the box around those pixels. Masked rectangles count as equal.

  shots.py middle WIDE.tga NARROW.tga OUT.png [--top Y] [--zoom N]
      A wide window's shot against a 4:3 one of the same height: the wide one's middle
      columns, as many as the narrow one has, from row Y down (below the HUD, which keeps
      to the window's corners), with the figures as pair gives them.

  shots.py delta OFF.tga ON.tga [--grid COLSxROWS] [--crop ...] [--mask ...] [--mask-notify]
      What an effect adds: the sum and mean of ON - OFF per channel, over the image and per
      grid cell. Take OFF and ON in one probe (the effect toggled between two screenshots)
      on each game, run delta on both pairs and compare the figures.

Coordinates are pixels from the top left of the full screenshot; X1 and Y1 are exclusive.
--mask-notify hides the "Wrote screenshots/shotNNNN.tga" line a screenshot taken within
three seconds of another shows: retail prints it centred (rows 228-250 at 640x480), ours
at the top left (rows 0-18, columns 0-300). Both are scaled to the image's height.
"""
import argparse
import struct
import sys
import zlib


def read_tga(path):
    d = open(path, 'rb').read()
    idlen, cmtype, itype = d[0], d[1], d[2]
    w, h, bpp, desc = struct.unpack_from('<HHBB', d, 12)
    if itype != 2 or cmtype != 0 or bpp not in (24, 32):
        sys.exit('%s: only uncompressed 24/32-bit TGA screenshots are supported' % path)
    bp = bpp // 8
    px = memoryview(d)[18 + idlen:]
    rows = []
    for y in range(h):
        r = px[y * w * bp:(y + 1) * w * bp]
        out = bytearray(w * 3)
        out[0::3] = r[2::bp]
        out[1::3] = r[1::bp]
        out[2::3] = r[0::bp]
        rows.append(out)
    if not (desc & 0x20):  # bottom-up unless the origin bit says top-down
        rows.reverse()
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + bytes(r) for r in rows)

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def crop(img, box):
    w, h, rows = img
    if not box:
        return img
    x0, y0, x1, y1 = box
    x0, y0, x1, y1 = max(0, x0), max(0, y0), min(w, x1), min(h, y1)
    return x1 - x0, y1 - y0, [rows[y][x0 * 3:x1 * 3] for y in range(y0, y1)]


def zoom(img, z):
    w, h, rows = img
    if z <= 1:
        return img
    out = []
    for r in rows:
        zr = bytearray(w * 3 * z)
        for i in range(w):
            px = r[i * 3:i * 3 + 3]
            zr[i * 3 * z:(i + 1) * 3 * z] = px * z
        out.extend(bytearray(zr) for _ in range(z))
    return w * z, h * z, out


def notify_masks(w, h):
    s = h / 480.0
    return [(0, int(228 * s), w, int(251 * s) + 1), (0, 0, int(300 * s), int(19 * s) + 1)]


def mask_grid(w, h, masks, box):
    """Per row, a bytearray of 1 where the pixel is masked (in cropped coordinates)."""
    ox, oy = (box[0], box[1]) if box else (0, 0)
    grid = [bytearray(w) for _ in range(h)]
    for (x0, y0, x1, y1) in masks:
        for y in range(max(0, y0 - oy), min(h, y1 - oy)):
            a, b = max(0, x0 - ox), min(w, x1 - ox)
            if a < b:
                grid[y][a:b] = b'\1' * (b - a)
    return grid


def load(path, args):
    img = read_tga(path)
    full_w, full_h = img[0], img[1]
    masks = list(args.mask or [])
    if args.mask_notify:
        masks += notify_masks(full_w, full_h)
    img = crop(img, args.crop)
    return img, mask_grid(img[0], img[1], masks, args.crop)


def cmd_png(args):
    img = crop(read_tga(args.input), args.crop)
    img = zoom(img, args.zoom)
    write_png(args.output, *img)
    print('%s: %dx%d' % (args.output, img[0], img[1]))


def cmd_pair(args):
    (wa, ha, ra), ma = load(args.a, args)
    (wb, hb, rb), _ = load(args.b, args)
    if (wa, ha) != (wb, hb):
        sys.exit('the images differ in size: %dx%d and %dx%d' % (wa, ha, wb, hb))
    ox, oy = (args.crop[0], args.crop[1]) if args.crop else (0, 0)
    compare((wa, ha, ra), ma, (wb, hb, rb), args, ox, oy)


def cmd_middle(args):
    """The middle of a wide shot against a 4:3 one of the same height, from --top down."""
    wide, narrow = read_tga(args.wide), read_tga(args.narrow)
    if wide[1] != narrow[1] or wide[0] < narrow[0]:
        sys.exit('not a wide shot and a narrower one of the same height: %dx%d and %dx%d'
                 % (wide[0], wide[1], narrow[0], narrow[1]))
    w, h = narrow[0], narrow[1]
    x0 = (wide[0] - w) // 2
    a = crop(wide, (x0, args.top, x0 + w, h))
    b = crop(narrow, (0, args.top, w, h))
    compare(a, mask_grid(a[0], a[1], [], None), b, args, 0, args.top)


def compare(img_a, ma, img_b, args, ox, oy):
    """Writes A | B | |A-B|x4 and prints the difference figures; ox, oy place the images
    in the full screenshot for the box."""
    (wa, ha, ra), (wb, hb, rb) = img_a, img_b
    sums = [0, 0, 0]
    n = over = 0
    bx0, by0, bx1, by1 = wa, ha, -1, -1
    diff_rows = []
    for y in range(ha):
        a, b, m = ra[y], rb[y], ma[y]
        dr = bytearray(wa * 3)
        for x in range(wa):
            if m[x]:
                continue
            i = x * 3
            d0, d1, d2 = abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2])
            sums[0] += d0
            sums[1] += d1
            sums[2] += d2
            n += 1
            dm = max(d0, d1, d2)
            if dm > args.threshold:
                over += 1
                bx0, by0, bx1, by1 = min(bx0, x), min(by0, y), max(bx1, x), max(by1, y)
            v = min(255, dm * 4)
            dr[i] = dr[i + 1] = dr[i + 2] = v
        diff_rows.append(dr)
    gap = 4
    strip = []
    for y in range(ha):
        strip.append(ra[y] + bytearray(gap * 3) + rb[y] + bytearray(gap * 3) + diff_rows[y])
    img = zoom((wa * 3 + gap * 2, ha, strip), args.zoom)
    write_png(args.output, *img)
    print('%s: A | B | |A-B|x4' % args.output)
    if not n:
        print('every pixel is masked')
        return
    print('mean abs diff  R %.2f  G %.2f  B %.2f   (0-255, over %d unmasked pixels)'
          % (sums[0] / n, sums[1] / n, sums[2] / n, n))
    print('pixels differing by more than %d: %d (%.2f%%)' % (args.threshold, over, 100.0 * over / n))
    if over:
        print('their box: x %d-%d, y %d-%d (full-image pixels)' % (bx0 + ox, bx1 + ox, by0 + oy, by1 + oy))


def cmd_delta(args):
    (w, h, roff), m = load(args.off, args)
    (w2, h2, ron), _ = load(args.on, args)
    if (w, h) != (w2, h2):
        sys.exit('the images differ in size')
    cols, rows_ = args.grid
    cells = [[[0, 0, 0, 0] for _ in range(cols)] for _ in range(rows_)]
    tot = [0, 0, 0, 0]
    for y in range(h):
        a, b, mk = roff[y], ron[y], m[y]
        cy = min(rows_ - 1, y * rows_ // h)
        for x in range(w):
            if mk[x]:
                continue
            i = x * 3
            d = (b[i] - a[i], b[i + 1] - a[i + 1], b[i + 2] - a[i + 2])
            c = cells[cy][min(cols - 1, x * cols // w)]
            for k in range(3):
                c[k] += d[k]
                tot[k] += d[k]
            c[3] += 1
            tot[3] += 1
    def fmt(c):
        n = max(1, c[3])
        return 'sum %9d %9d %9d   mean %6.2f %6.2f %6.2f' % (c[0], c[1], c[2], c[0] / n, c[1] / n, c[2] / n)
    print('ON - OFF, R G B')
    print('whole       ' + fmt(tot))
    if cols * rows_ > 1:
        for r in range(rows_):
            for c in range(cols):
                print('cell %d,%d    %s' % (c, r, fmt(cells[r][c])))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)

    def common(sp):
        sp.add_argument('--crop', type=int, nargs=4, metavar=('X0', 'Y0', 'X1', 'Y1'))
        sp.add_argument('--zoom', type=int, default=1)

    def masking(sp):
        sp.add_argument('--mask', type=int, nargs=4, action='append', metavar=('X0', 'Y0', 'X1', 'Y1'))
        sp.add_argument('--mask-notify', action='store_true')

    sp = sub.add_parser('png')
    sp.add_argument('input')
    sp.add_argument('output')
    common(sp)
    sp.set_defaults(fn=cmd_png)

    sp = sub.add_parser('pair')
    sp.add_argument('a')
    sp.add_argument('b')
    sp.add_argument('output')
    common(sp)
    masking(sp)
    sp.add_argument('--threshold', type=int, default=24)
    sp.set_defaults(fn=cmd_pair)

    sp = sub.add_parser('middle')
    sp.add_argument('wide')
    sp.add_argument('narrow')
    sp.add_argument('output')
    sp.add_argument('--top', type=int, default=0)
    sp.add_argument('--zoom', type=int, default=1)
    sp.add_argument('--threshold', type=int, default=24)
    sp.set_defaults(fn=cmd_middle)

    sp = sub.add_parser('delta')
    sp.add_argument('off')
    sp.add_argument('on')
    sp.add_argument('--crop', type=int, nargs=4, metavar=('X0', 'Y0', 'X1', 'Y1'))
    sp.add_argument('--grid', default='1x1', type=lambda s: tuple(int(v) for v in s.lower().split('x')))
    masking(sp)
    sp.set_defaults(fn=cmd_delta)

    args = p.parse_args()
    args.fn(args)


if __name__ == '__main__':
    main()
