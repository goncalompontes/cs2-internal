import sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

BIN = r'C:\Users\gpmpo\cs2-internal\re\cs2_212C3300000\cs2_212C3300000.bin'
BASE = 0x212C3300000
d = open(BIN, 'rb').read()


def hexdump(off, n):
    for i in range(0, n, 16):
        c = d[off + i:off + i + 16]
        if not c:
            break
        asc = ''.join(chr(b) if 32 <= b < 127 else '.' for b in c)
        print('%08X  %-47s  %s' % (off + i, ' '.join('%02X' % b for b in c), asc))


if __name__ == '__main__':
    off = int(sys.argv[1], 16)
    n = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x100
    hexdump(off, n)
