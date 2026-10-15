"""Expand a short probe template into a harness probe script.

    python make_probe.py <template> [<output.scr>]
    python make_probe.py - < template.txt > tools/retail-compare/probe-x.scr

The template is a FAKK2 level script with a few directives, one per line:

    @cmd <console command>        -> stuffcmd "<console command>"
    @hold <key> <seconds>         -> stuffcmd "+key" / wait <seconds> / stuffcmd "-key"
    @sweep [from] [to]            -> whatis from..to, ten per line group with "wait 0.1"
                                     between groups (default 0 299)
    @end                          -> wait 0.5 / levelvars / pendingevents / end

Everything else is copied through. The header "probe:" and "waitforplayer" are added if
the template does not start with them, and the probe is closed with @end if it is not
closed already, because the runners stop when they see pendingevents.

Example template (fire the sling looking down, then read everything):

    wait 2
    @cmd give weapon_sling.tik
    wait 0.5
    @cmd use Sling dual
    wait 2
    @hold lookdown 0.6
    wait 0.3
    @cmd whatis 0
    @hold attackright 1.5
    wait 0.3
    @sweep
"""
import sys


def expand(lines):
    out = []
    closed = False
    for raw in lines:
        line = raw.rstrip("\r\n")
        s = line.strip()
        if s.startswith("@cmd "):
            out.append('stuffcmd "%s"' % s[5:].strip())
        elif s.startswith("@hold "):
            parts = s.split()
            key = parts[1].lstrip("+")
            secs = parts[2] if len(parts) > 2 else "1"
            out += ['stuffcmd "+%s"' % key, "wait %s" % secs, 'stuffcmd "-%s"' % key]
        elif s.startswith("@sweep"):
            parts = s.split()
            lo = int(parts[1]) if len(parts) > 1 else 0
            hi = int(parts[2]) if len(parts) > 2 else 299
            for start in range(lo, hi + 1, 10):
                out += ['stuffcmd "whatis %d"' % n for n in range(start, min(start + 10, hi + 1))]
                out.append("wait 0.1")
        elif s == "@end":
            out += ["wait 0.5", 'stuffcmd "levelvars"', 'stuffcmd "pendingevents"', "end"]
            closed = True
        else:
            out.append(line)
            if s == "end":
                closed = True
    head = []
    if not out or out[0].strip() != "probe:":
        head.append("probe:")
    if "waitforplayer" not in [l.strip() for l in out[:3]]:
        head.append("waitforplayer")
    out = head + out
    if not closed:
        out += ["wait 0.5", 'stuffcmd "levelvars"', 'stuffcmd "pendingevents"', "end"]
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    src = sys.stdin if argv[1] == "-" else open(argv[1], encoding="latin-1")
    text = expand(src.readlines())
    if len(argv) > 2:
        with open(argv[2], "w", encoding="latin-1", newline="\n") as f:
            f.write(text)
        print("wrote %s (%d lines)" % (argv[2], text.count("\n")))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
