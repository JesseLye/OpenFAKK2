"""Names the functions in a crash report.

When the engine crashes on Windows it prints the fault and the stack into the
console log as module-relative addresses ("at openfakk2.exe+0x87350", "called
from cgamex86.dll+0x1234"; sys_win32.c, Sys_CrashReport) and writes crash.dmp
beside the log. This looks each address up in the linker maps the build writes
beside the binaries (/MAP), so it needs the build that crashed, unchanged.

    python tools/crash-symbols.py <console log> [build dir]

The build dir defaults to build/x86-Release/Release; the game modules' maps are
looked for in its fakk folder too. A module without a map (the retail game
modules, Windows' own) is printed as it is.
"""
import bisect
import os
import re
import sys

LINE = re.compile(r'^\s*(at|called from) (\S+)\+0x([0-9a-fA-F]+)')
SYMBOL = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+(?:f\s+)?(?:i\s+)?(\S+)?')


def read_map(path):
    base = 0x400000
    symbols = []
    with open(path, encoding='latin-1') as f:
        for line in f:
            m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
            if m:
                base = int(m.group(1), 16)
                continue
            m = SYMBOL.match(line)
            if m and int(m.group(2), 16) >= base:
                symbols.append((int(m.group(2), 16) - base, m.group(1), m.group(3) or ''))
    symbols.sort()
    return symbols


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    log = sys.argv[1]
    build = sys.argv[2] if len(sys.argv) > 2 else os.path.join('build', 'x86-Release', 'Release')
    maps = {}
    with open(log, encoding='latin-1') as f:
        lines = f.read().splitlines()
    for i, line in enumerate(lines):
        if not line.startswith('Fatal exception'):
            continue
        print(line)
        for line in lines[i + 1:]:
            m = LINE.match(line)
            if not m:
                if line.strip().startswith(('reading', 'writing', 'minidump')):
                    print(line)
                    continue
                break
            label, module, offset = m.group(1), m.group(2), int(m.group(3), 16)
            if module not in maps:
                maps[module] = None
                stem = os.path.splitext(module)[0]
                for folder in (build, os.path.join(build, 'fakk')):
                    path = os.path.join(folder, stem + '.map')
                    if os.path.exists(path):
                        maps[module] = read_map(path)
                        break
            symbols = maps[module]
            where = ''
            if symbols:
                k = bisect.bisect_right(symbols, (offset, '￿')) - 1
                if k >= 0:
                    rva, name, obj = symbols[k]
                    where = '  %s+0x%x (%s)' % (name, offset - rva, obj)
            print('  %s %s+0x%x%s' % (label, module, offset, where))


if __name__ == '__main__':
    main()
