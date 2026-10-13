"""Print entities from a FAKK2 BSP's entity lump that match a filter (read-only)."""
import re
import struct
import sys
import zipfile

pak, mapname, pattern = sys.argv[1], sys.argv[2], sys.argv[3]
data = zipfile.ZipFile(pak).read(f"maps/{mapname}.bsp")
ofs, length = struct.unpack_from("<ii", data, 12 + 14 * 8)
text = data[ofs:ofs + length].split(b"\0")[0].decode("latin-1")
ents = re.findall(r"\{(.*?)\}", text, flags=re.S)
rx = re.compile(pattern, re.I)
print(f"{len(ents)} entities in {mapname}")
for i, body in enumerate(ents):
    if rx.search(body):
        pairs = re.findall(r'"([^"]*)"\s+"([^"]*)"', body)
        print(f"--- entity #{i}: " + "  ".join(f"{k}={v}" for k, v in pairs))
