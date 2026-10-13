"""Qualify member-function entries in Ritual response tables.

The SDK was written for MSVC 6, which accepted a bare member name where a
pointer-to-member is required:

    CLASS_DECLARATION( Entity, SplinePath, "info_splinepath" )
        {
          { &EV_SplinePath_Create, CreatePath },

Standard C++ requires `&SplinePath::CreatePath`. This rewrites each row using
the class named by the enclosing CLASS_DECLARATION. It is idempotent.
"""

import re
import sys
from pathlib import Path

DECL = re.compile(r"^\s*CLASS_DECLARATION\(\s*\w+\s*,\s*(\w+)\s*,")
ROW = re.compile(r"^(\s*\{\s*&\w+\s*,\s*)(\w+(?:::\w+)?)(\s*\},?.*)$")


def fix(path: Path) -> int:
    data = path.read_bytes()
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    lines = data.decode("latin-1").split(newline.decode())
    current = None
    changed = 0
    for i, line in enumerate(lines):
        decl = DECL.match(line)
        if decl:
            current = decl.group(1)
            continue
        row = ROW.match(line)
        if not row or current is None:
            continue
        name = row.group(2)
        if name == "NULL":
            continue
        qualified = name if "::" in name else f"{current}::{name}"
        lines[i] = f"{row.group(1)}&{qualified}{row.group(3)}"
        changed += 1
    if changed:
        path.write_bytes(newline.decode().join(lines).encode("latin-1"))
    return changed


def main() -> None:
    total = 0
    for root in sys.argv[1:]:
        for path in sorted(Path(root).glob("*.cpp")):
            total += fix(path)
    print(f"rewrote {total} response rows")


if __name__ == "__main__":
    main()
