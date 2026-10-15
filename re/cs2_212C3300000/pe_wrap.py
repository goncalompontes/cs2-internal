import struct, sys, shutil

SRC = r'C:\Users\gpmpo\cs2-internal\re\cs2_212C3300000\cs2_212C3300000.bin'
DST = r'C:\Users\gpmpo\cs2-internal\re\cs2_212C3300000\cs2_212C3300000.pe.bin'

IMAGE_BASE = 0x212C3300000
SIZE_OF_IMAGE = 0x5001000
ENTRY = 0xA00
SECTION_VA = 0x1000
SECTION_SIZE = 0x5000000          # covers RVA 0x1000 .. 0x5001000
PDATA_RVA = 0x3F454F0
PDATA_SIZE = 0x8406C

d = bytearray(open(SRC, 'rb').read())
assert len(d) == 0x5001000, hex(len(d))

# keep the original stub at 0xa00 intact; we only write the header area 0x0..0x200
assert d[0xa00:0xa04] == b'\x48\x83\xec\x28'

hdr = bytearray(0x200)

def put(fmt, off, *a):
    struct.pack_into(fmt, hdr, off, *a)

# ---- DOS header ----
put('<H', 0x00, 0x5A4D)          # MZ
put('<H', 0x02, 0x90)            # e_cblp
put('<H', 0x04, 0x03)
put('<H', 0x06, 0x00)
put('<H', 0x08, 0x04)
put('<H', 0x0A, 0x00)
put('<H', 0x0C, 0xFFFF)
put('<H', 0x0E, 0x00)
put('<H', 0x10, 0xB8)
put('<H', 0x12, 0x00)
put('<H', 0x14, 0x00)
put('<H', 0x16, 0x00)
put('<H', 0x18, 0x40)
put('<I', 0x3C, 0x80)            # e_lfanew
put('<I', 0x40, 0)               # 'PE\0\0' written below as dword 0x00004550

# ---- PE signature ----
put('<I', 0x80, 0x00004550)      # 'PE\0\0'

# ---- COFF header ----
put('<H', 0x84, 0x8664)          # Machine AMD64
put('<H', 0x86, 1)               # NumberOfSections
put('<I', 0x88, 0)               # TimeDateStamp
put('<I', 0x8C, 0)
put('<I', 0x90, 0)
put('<H', 0x94, 0xF0)            # SizeOfOptionalHeader
put('<H', 0x96, 0x2022)          # Characteristics: EXECUTABLE_IMAGE|LARGE_ADDRESS_AWARE|DLL

# ---- Optional header (PE32+) ----
O = 0x98
put('<H', O + 0x00, 0x20B)
put('<B', O + 0x02, 14)
put('<B', O + 0x03, 0)
put('<I', O + 0x04, SECTION_SIZE)     # SizeOfCode
put('<I', O + 0x08, 0)
put('<I', O + 0x0C, 0)
put('<I', O + 0x10, ENTRY)            # AddressOfEntryPoint
put('<I', O + 0x14, SECTION_VA)       # BaseOfCode
put('<Q', O + 0x18, IMAGE_BASE)
put('<I', O + 0x20, 0x1000)           # SectionAlignment
put('<I', O + 0x24, 0x1000)           # FileAlignment
put('<H', O + 0x28, 6)                # MajorOperatingSystemVersion
put('<H', O + 0x2A, 0)
put('<H', O + 0x2C, 0)
put('<H', O + 0x2E, 0)
put('<H', O + 0x30, 6)                # MajorSubsystemVersion
put('<H', O + 0x32, 0)
put('<I', O + 0x34, 0)
put('<I', O + 0x38, SIZE_OF_IMAGE)
put('<I', O + 0x3C, 0x1000)           # SizeOfHeaders
put('<I', O + 0x40, 0)
put('<H', O + 0x44, 2)                # Subsystem = GUI
put('<H', O + 0x46, 0x160)            # DllCharacteristics
put('<Q', O + 0x48, 0x100000)
put('<Q', O + 0x50, 0x1000)
put('<Q', O + 0x58, 0x100000)
put('<Q', O + 0x60, 0x1000)
put('<I', O + 0x68, 0)
put('<I', O + 0x6C, 16)               # NumberOfRvaAndSizes

DD = O + 0x70
put('<I', DD + 3 * 8, PDATA_RVA)      # Exception directory (.pdata)
put('<I', DD + 3 * 8 + 4, PDATA_SIZE)
put('<I', DD + 2 * 8, 0)              # Resource = 0

# ---- Section header ----
S = O + 0xF0
hdr[S:S + 8] = b'.text\x00\x00\x00'
put('<I', S + 0x08, SECTION_SIZE)     # VirtualSize
put('<I', S + 0x0C, SECTION_VA)       # VirtualAddress
put('<I', S + 0x10, SECTION_SIZE)     # SizeOfRawData
put('<I', S + 0x14, SECTION_VA)       # PointerToRawData
put('<I', S + 0x18, 0)
put('<I', S + 0x1C, 0)
put('<H', S + 0x20, 0)
put('<H', S + 0x22, 0)
put('<I', S + 0x24, 0x60000020)       # CODE|EXECUTE|READ

d[0:0x200] = hdr
open(DST, 'wb').write(d)
print('wrote', DST, len(d), 'entry', hex(ENTRY), 'pdata', hex(PDATA_RVA), hex(PDATA_SIZE))
