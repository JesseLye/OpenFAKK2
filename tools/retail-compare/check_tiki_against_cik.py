"""Prototype TIKI loader, checked against the retail engine's .cik caches.

Read-only. Establishes the parsing and derivation rules before they are
written in C.
"""
import collections
import math
import re
import struct
import sys
import zipfile

files = {}
for pak in sys.argv[1:]:
    with zipfile.ZipFile(pak) as z:
        for info in z.infolist():
            files[info.filename.lower()] = (z.filename, info.filename)
_zips = {}


def read(name):
    key = name.lower().replace("\\", "/")
    if key not in files:
        return None
    pak, real = files[key]
    if pak not in _zips:
        _zips[pak] = zipfile.ZipFile(pak)
    return _zips[pak].read(real)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def tokenize(line):
    return re.findall(r'"[^"]*"|\S+', line)


class Tiki:
    def __init__(self, name):
        self.name = name
        self.scale = 1.0
        self.lod_scale = None
        self.lod_bias = None
        self.path = ""
        self.origin = (0.0, 0.0, 0.0)
        self.radius = None
        self.skelmodel = None
        self.surface_cmds = []
        self.anims = []          # (alias, file, flags)
        self.server_init = []
        self.client_init = []
        self.errors = []


def parse(name, tiki=None, macros=None, depth=0):
    data = read(name)
    if data is None:
        if tiki:
            tiki.errors.append(f"missing include {name}")
        return None
    if tiki is None:
        tiki = Tiki(name)
    if macros is None:
        macros = {}
    lines = strip_comments(data.decode("latin-1")).splitlines()

    def expand(line):
        def sub(m):
            return macros.get(m.group(1).lower(), m.group(0))
        return re.sub(r"\$(\w+)\$", sub, line)

    section = None
    sub = None
    level = 0
    anim = None
    for raw in lines:
        line = expand(raw).strip()
        if not line:
            continue
        toks = tokenize(line)
        head = toks[0].lower()
        if head == "$define":
            if len(toks) >= 3:
                macros[toks[1].lower()] = toks[2]
            continue
        if head == "$include":
            parse(toks[1], tiki, macros, depth + 1)
            continue
        if head == "{":
            level += 1
            continue
        if head == "}":
            level -= 1
            if level == 0:
                section = None
            if level == 1:
                sub = None
            continue
        if level == 0:
            section = head
            continue
        if section == "setup":
            if head == "scale":
                tiki.scale = float(toks[1])
            elif head == "lod_scale":
                tiki.lod_scale = float(toks[1])
            elif head == "lod_bias":
                tiki.lod_bias = float(toks[1])
            elif head == "path":
                tiki.path = toks[1].rstrip("/")
            elif head == "origin":
                tiki.origin = tuple(float(x) for x in toks[1:4])
            elif head == "radius":
                tiki.radius = float(toks[1])
            elif head == "skelmodel":
                tiki.skelmodel = toks[1]
            elif head == "surface":
                tiki.surface_cmds.append(toks[1:])
        elif section == "init":
            if level == 1:
                sub = head
            elif level == 2:
                (tiki.server_init if sub == "server" else tiki.client_init).append(toks)
        elif section == "animations":
            if level == 1:
                if len(toks) >= 2:
                    anim = {"alias": toks[0], "file": toks[1], "extra": toks[2:], "server": [], "client": []}
                    tiki.anims.append(anim)
            elif level == 2:
                sub = head
            elif level == 3 and anim is not None:
                anim[sub if sub in ("server", "client") else "client"].append(toks)
    return tiki


def load_tan(data):
    ident, version = struct.unpack_from("<4si", data, 0)
    name = data[8:72]
    num_frames, num_tags, num_surfaces, totaltime = struct.unpack_from("<iiif", data, 72)
    totaldelta = struct.unpack_from("<3f", data, 88)
    ofs_frames, ofs_surfaces = struct.unpack_from("<ii", data, 100)
    frames = []
    for i in range(num_frames):
        f = struct.unpack_from("<17f", data, ofs_frames + i * 68)
        frames.append({"mins": f[0:3], "maxs": f[3:6], "scale": f[6:9], "offset": f[9:12], "delta": f[12:15], "radius": f[15], "frametime": f[16]})
    return {"ident": ident, "version": version, "frames": frames, "num_tags": num_tags, "num_surfaces": num_surfaces,
            "totaltime": totaltime, "totaldelta": totaldelta}


def load_ska(data):
    ident, version = struct.unpack_from("<4si", data, 0)
    typ, num_frames, numbones, totaltime, frametime = struct.unpack_from("<iiiff", data, 72)
    totaldelta = struct.unpack_from("<3f", data, 92)
    (ofs_frames,) = struct.unpack_from("<i", data, 104)
    frame_size = 40 + numbones * 16
    frames = []
    for i in range(num_frames):
        f = struct.unpack_from("<10f", data, ofs_frames + i * frame_size)
        frames.append({"mins": f[0:3], "maxs": f[3:6], "radius": f[6], "delta": f[7:10]})
    return {"ident": ident, "version": version, "frames": frames, "numbones": numbones, "totaltime": totaltime,
            "frametime": frametime, "totaldelta": totaldelta, "type": typ}


def load_cik(data):
    base = 4
    name = data[base:base + 128].split(b"\0")[0].decode("latin-1")
    num_surfaces, num_tags, num_anims = struct.unpack_from("<iii", data, base + 128)
    ncli, ocli, nsrv, osrv, osurf = struct.unpack_from("<5i", data, base + 144)
    load_scale, lod_scale, lod_bias = struct.unpack_from("<3f", data, base + 164)
    light_offset = struct.unpack_from("<3f", data, base + 176)
    load_origin = struct.unpack_from("<3f", data, base + 188)
    mins = struct.unpack_from("<3f", data, base + 200)
    maxs = struct.unpack_from("<3f", data, base + 212)
    (radius,) = struct.unpack_from("<f", data, base + 224)
    (skel_index,) = struct.unpack_from("<i", data, base + 228)
    skel_name = data[base + 232:base + 360].split(b"\0")[0].decode("latin-1")
    return {"head": data[:4], "name": name, "num_surfaces": num_surfaces, "num_tags": num_tags, "num_anims": num_anims,
            "ncli": ncli, "nsrv": nsrv, "load_scale": load_scale, "lod_scale": lod_scale, "lod_bias": lod_bias,
            "light_offset": light_offset, "load_origin": load_origin, "mins": mins, "maxs": maxs, "radius": radius,
            "skel_name": skel_name, "ofs": (ocli, osrv, osurf)}


def close(a, b, tol=1e-3):
    return all(abs(x - y) <= tol * max(1.0, abs(x), abs(y)) for x, y in zip(a, b))


stats = collections.Counter()
examples = collections.defaultdict(list)
_anim_cache = {}


def anim_data(path):
    key = path.lower()
    if key not in _anim_cache:
        data = read(path)
        if data is None:
            _anim_cache[key] = None
        else:
            _anim_cache[key] = load_tan(data) if key.endswith(".tan") else load_ska(data)
    return _anim_cache[key]


for key in sorted(files):
    if not key.endswith(".cik"):
        continue
    cik = load_cik(read(key))
    tiki = parse(key[:-4] + ".tik")
    if tiki is None or not tiki.anims:
        continue
    s, o = tiki.scale, tiki.origin
    mins = [1e30] * 3
    maxs = [-1e30] * 3
    maxrad = 0.0
    missing = 0
    for a in tiki.anims:
        ad = anim_data((tiki.path + "/" if tiki.path else "") + a["file"])
        if ad is None:
            missing += 1
            continue
        for f in ad["frames"]:
            for i in range(3):
                mins[i] = min(mins[i], f["mins"][i])
                maxs[i] = max(maxs[i], f["maxs"][i])
            maxrad = max(maxrad, f["radius"])
    if missing:
        stats["models with a missing anim file"] += 1
    empty = any(mins[i] > maxs[i] for i in range(3))
    if empty:
        bmins, bmaxs = [-4.0] * 3, [4.0] * 3
    else:
        bmins = [v + o[i] for i, v in enumerate(mins)]
        bmaxs = [v + o[i] for i, v in enumerate(maxs)]
    ok = close(bmins, cik["mins"], 2e-3) and close(bmaxs, cik["maxs"], 2e-3)
    stats[f"bounds rule (empty={empty}): {'ok' if ok else 'MISMATCH'}"] += 1
    if not ok and len(examples["bounds"]) < 8:
        examples["bounds"].append(f"{key} s={s} o={o} cik {tuple(round(v,2) for v in cik['mins'])} {tuple(round(v,2) for v in cik['maxs'])} ours {tuple(round(v,2) for v in bmins)} {tuple(round(v,2) for v in bmaxs)} missing={missing} anims={len(tiki.anims)}")

    def rfb(mn, mx):
        return math.sqrt(sum(max(abs(mn[i]), abs(mx[i])) ** 2 for i in range(3)))

    rh = {
        "tik radius": tiki.radius if tiki.radius is not None else -1e9,
        "rfb(bounds)*scale": rfb(bmins, bmaxs) * s,
        "rfb(union raw)*scale": rfb(mins, maxs) * s if not empty else -1e9,
        "rfb(bounds)": rfb(bmins, bmaxs),
    }
    rm = [h for h, v in rh.items() if abs(v - cik["radius"]) <= 2e-3 * max(1.0, abs(v))]
    label = "tik radius" if tiki.radius is not None else "derived"
    stats[f"radius [{label}, empty={empty}] -> " + ("|".join(rm) if rm else "NONE")] += 1
    if not rm and len(examples["radius"]) < 8:
        examples["radius"].append(f"{key} cik {cik['radius']:.3f} rfb*s {rfb(bmins, bmaxs) * s:.3f} s={s} o={o} tikradius={tiki.radius}")

for k in sorted(stats):
    print(f"{stats[k]:5}  {k}")
for k, v in examples.items():
    for e in v:
        print(f"  [{k}] {e}")
