"""Read-only movement capture. Does not send input, configure features, or inject."""
import argparse
import ctypes as C
import hashlib
import json
import math
import struct
import sys
import time
from pathlib import Path


def image_data(path):
    raw = Path(path).read_bytes()
    pe = struct.unpack_from("<I", raw, 0x3c)[0]
    optional = pe + 24
    if raw[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", raw, optional)[0] != 0x20b:
        raise RuntimeError("Expected x64 PE image")
    count = struct.unpack_from("<H", raw, pe + 6)[0]
    opt_size = struct.unpack_from("<H", raw, pe + 20)[0]
    preferred = struct.unpack_from("<Q", raw, optional + 24)[0]
    size, headers = struct.unpack_from("<II", raw, optional + 56)
    mapped = bytearray(size)
    mapped[:headers] = raw[:headers]
    for i in range(count):
        section = optional + opt_size + i * 40
        virtual, disk_size, disk_at = struct.unpack_from("<III", raw, section + 12)
        mapped[virtual:virtual+disk_size] = raw[disk_at:disk_at+disk_size]
    return bytes(mapped), preferred, hashlib.sha256(raw).hexdigest()


def schema_fields(image, preferred, name):
    output = {}
    token = name.encode() + b"\0"
    at = image.find(token)
    while at >= 0:
        pointer = struct.pack("<Q", preferred + at)
        ref = image.find(pointer)
        while ref >= 0:
            if ref % 8 == 0 and ref + 48 <= len(image):
                count = struct.unpack_from("<H", image, ref + 28)[0]
                fields = struct.unpack_from("<Q", image, ref + 40)[0] - preferred
                if 0 < count <= 8192 and 0 <= fields <= len(image) - count * 32:
                    for i in range(count):
                        entry = fields + i * 32
                        text = struct.unpack_from("<Q", image, entry)[0] - preferred
                        offset = struct.unpack_from("<I", image, entry + 16)[0]
                        if 0 <= text < len(image) and offset < 0x100000:
                            end = image.find(b"\0", text, text + 256)
                            if end > text:
                                field = image[text:end].decode("ascii", errors="replace")
                                if field in output and output[field] != offset:
                                    raise RuntimeError("Ambiguous schema field " + name + "::" + field)
                                output[field] = offset
            ref = image.find(pointer, ref + 1)
        at = image.find(token, at + 1)
    if not output:
        raise RuntimeError("No static descriptors for " + name)
    return output


class ReadProcess:
    def __init__(self, pid):
        self.api = C.WinDLL("kernel32", use_last_error=True)
        self.api.OpenProcess.argtypes = [C.c_ulong, C.c_int, C.c_ulong]
        self.api.OpenProcess.restype = C.c_void_p
        self.api.ReadProcessMemory.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        self.api.CloseHandle.argtypes = [C.c_void_p]
        self.handle = self.api.OpenProcess(0x1010, False, pid)
        if not self.handle:
            raise OSError(C.get_last_error(), "OpenProcess(read-only)")

    def read(self, address, fmt):
        size = struct.calcsize(fmt)
        buffer = C.create_string_buffer(size)
        used = C.c_size_t()
        if not address or not self.api.ReadProcessMemory(self.handle, address, buffer, size, C.byref(used)) or used.value != size:
            raise OSError(C.get_last_error(), "ReadProcessMemory")
        value = struct.unpack(fmt, buffer.raw)
        return value[0] if len(value) == 1 else value

    def close(self):
        self.api.CloseHandle(self.handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", default=r"F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--seconds", type=float, default=12)
    parser.add_argument("--output", default="build-tools/movement-readonly-capture.json")
    args = parser.parse_args()
    image, preferred, digest = image_data(args.client)
    types = ["C_BaseEntity", "C_BasePlayerPawn", "CPlayer_MovementServices",
             "CPlayer_MovementServices_Humanoid", "CCSPlayer_MovementServices", "CCSPlayerModernJump"]
    fields = {name: schema_fields(image, preferred, name) for name in types}
    report = {"client_sha256": digest, "schema": fields, "mode": "read-only; non-atomic samples", "samples": []}
    if args.pid:
        from scenario.control import Control
        import msvcrt
        process = ReadProcess(args.pid)
        control = Control().connect(timeout=2)
        peek = process.api.PeekNamedPipe
        peek.argtypes = [C.c_void_p, C.c_void_p, C.c_ulong, C.c_void_p, C.POINTER(C.c_ulong), C.c_void_p]
        pipe = msvcrt.get_osfhandle(control._pipe.fileno())
        def status():
            # Only the existing read-only input-status request is permitted.
            control._write_all(b'{"id":1,"cmd":"input","args":{}}\n')
            data = bytearray()
            deadline = time.monotonic() + 3
            while b"\n" not in data:
                available = C.c_ulong()
                if not peek(pipe, None, 0, None, C.byref(available), None):
                    raise OSError(C.get_last_error(), "PeekNamedPipe")
                if available.value:
                    data += control._pipe.read(available.value)
                elif time.monotonic() >= deadline:
                    raise TimeoutError("input-status response timed out")
                else:
                    time.sleep(.005)
            response = json.loads(data.split(b"\n", 1)[0])
            if not response.get("ok"):
                raise RuntimeError(response)
            return response["result"]
        def field(obj, typename, name, fmt):
            offset = fields[typename].get(name)
            return process.read(obj + offset, fmt) if offset is not None else None
        end = time.monotonic() + min(max(args.seconds, .1), 60)
        try:
            while time.monotonic() < end:
                sample = {"time": time.monotonic()}
                try:
                    info = status()
                    pawn = info.get("input_local_pawn", 0)
                    services = field(pawn, "C_BasePlayerPawn", "m_pMovementServices", "<Q")
                    sample["input"] = {k:v for k,v in info.items() if k.startswith(("strafe", "bhop", "autostop", "command_crc", "movement"))}
                    sample["pawn"] = pawn
                    sample["abs_velocity"] = field(pawn, "C_BaseEntity", "m_vecAbsVelocity", "<fff")
                    sample["velocity"] = field(pawn, "C_BaseEntity", "m_vecVelocity", "<fff")
                    sample["flags"] = field(pawn, "C_BaseEntity", "m_fFlags", "<I")
                    sample["services"] = {name:field(services, "CPlayer_MovementServices", name, fmt) for name,fmt in
                        [("m_flCmdForwardMove", "<f"), ("m_flCmdLeftMove", "<f"), ("m_flMaxspeed", "<f"), ("m_nLastCommandNumberProcessed", "<I")]}
                    sample["stamina"] = field(services, "CCSPlayer_MovementServices", "m_flStamina", "<f")
                    sample["duck"] = field(services, "CCSPlayer_MovementServices", "m_flDuckAmount", "<f")
                    modern = services + fields["CCSPlayer_MovementServices"]["m_ModernJump"]
                    sample["jump"] = {name:process.read(modern + offset, "<f" if name.startswith("m_fl") else "<i")
                        for name,offset in fields["CCSPlayerModernJump"].items() if name.startswith(("m_n", "m_fl"))}
                    cmd = int(info["last_cmd"], 16)
                    number = process.read(cmd + 8, "<I")
                    base = process.read(cmd + 0x40, "<Q")
                    count = process.read(base + 0x20, "<i")
                    rep = process.read(base + 0x28, "<Q")
                    angles = process.read(base + 0x40, "<Q")
                    sample["command"] = {"number":number, "client_tick":process.read(base + 0x54, "<i"),
                        "buttons":process.read(cmd + 0x60, "<QQQ"), "axes":process.read(base + 0x58, "<ff"),
                        "angles":process.read(angles + 0x18, "<fff"), "count":count, "steps":[]}
                    if 0 <= count <= 32:
                        for i in range(count):
                            step = process.read(rep + 8 + i * 8, "<Q")
                            sample["command"]["steps"].append({"bits":process.read(step + 0x10,"<I"),
                                "button":process.read(step + 0x18,"<Q"), "pressed":process.read(step + 0x20,"<B"),
                                "when":process.read(step + 0x24,"<f"), "analog":process.read(step + 0x28,"<ff"),
                                "yaw":process.read(step + 0x34,"<f")})
                    sample["same_sequence_after_read"] = process.read(cmd + 8, "<I") == number
                except (OSError, KeyError, ValueError) as error:
                    sample["error"] = str(error)
                report["samples"].append(sample)
                time.sleep(.05)
        finally:
            control.close()
            process.close()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2))
    print("Saved", output, "samples", len(report["samples"]))
    print("ModernJump fields:", fields["CCSPlayerModernJump"])


if __name__ == "__main__":
    main()
