import re
import sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

BIN = r'C:\Users\gpmpo\cs2-internal\re\cs2_212C3300000\cs2_212C3300000.bin'
BASE = 0x212C3300000
d = open(BIN, 'rb').read()
low = d.lower()


def ctx(pat, lim=12, w=70):
    print('==== %s (%d)' % (pat.decode(), low.count(pat.lower())))
    seen = set()
    for m in list(re.finditer(re.escape(pat.lower()), low))[:lim]:
        s = m.start()
        a = s
        while a > 0 and 32 <= d[a - 1] < 127:
            a -= 1
        b = s
        while b < len(d) and 32 <= d[b] < 127:
            b += 1
        t = d[max(a, s - w):b + w].decode('latin1')
        if t[:40] in seen:
            continue
        seen.add(t[:40])
        print('   %08X  %s' % (s, t))


import sys as _s
for p in _s.argv[1:]:
    ctx(p.encode())
