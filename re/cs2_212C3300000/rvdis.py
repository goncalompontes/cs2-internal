import sys, capstone

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

BIN = r'C:\Users\gpmpo\cs2-internal\re\cs2_212C3300000\cs2_212C3300000.bin'
BASE = 0x212C3300000
d = open(BIN, 'rb').read()

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def cstr(off, maxlen=70):
    e = d.find(b'\x00', off)
    if e < 0:
        e = off + maxlen
    return d[off:e][:maxlen].decode('latin1', 'replace')


def dis(rva, n):
    print('==== RVA', hex(rva), 'VA', hex(BASE + rva))
    code = d[rva:rva + n]
    for i in md.disasm(code, BASE + rva):
        note = ''
        try:
            for op in i.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    t = i.address + i.size + op.mem.disp
                    tv = t - BASE
                    if 0 <= tv < len(d):
                        note = '  ; [%08X] "%s"' % (tv, cstr(tv))
        except Exception:
            pass
        print('%08X  %-9s %s%s' % (i.address - BASE, i.mnemonic, i.op_str, note))


if __name__ == '__main__':
    rva = int(sys.argv[1], 16)
    n = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x100
    dis(rva, n)
