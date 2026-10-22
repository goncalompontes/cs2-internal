"""Smoke-test the registered overlay with a disposable Vulkan instance, without CS2."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys


def child():
    loader = ctypes.WinDLL("vulkan-1.dll")

    class InstanceInfo(ctypes.Structure):
        _fields_ = [
            ("s_type", ctypes.c_uint32), ("next", ctypes.c_void_p),
            ("flags", ctypes.c_uint32), ("application", ctypes.c_void_p),
            ("layer_count", ctypes.c_uint32), ("layers", ctypes.c_void_p),
            ("extension_count", ctypes.c_uint32), ("extensions", ctypes.c_void_p),
        ]

    info = InstanceInfo()
    info.s_type = 1  # VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    instance = ctypes.c_void_p()
    loader.vkCreateInstance.argtypes = [ctypes.POINTER(InstanceInfo), ctypes.c_void_p,
                                        ctypes.POINTER(ctypes.c_void_p)]
    loader.vkCreateInstance.restype = ctypes.c_int32
    result = loader.vkCreateInstance(ctypes.byref(info), None, ctypes.byref(instance))
    print("vkCreateInstance:", result)
    if result != 0:
        return 1
    loader.vkDestroyInstance.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    loader.vkDestroyInstance(instance, None)
    return 0


def main():
    if "--child" in sys.argv:
        return child()
    env = os.environ.copy()
    env.pop("CS2_OVERLAY", None)
    env.pop("DISABLE_CS2_OVERLAY", None)
    env["VK_LOADER_DEBUG"] = "all"
    if "--isolated" in sys.argv:
        # Loader diagnostic filters apply to this disposable child only.
        env["VK_LOADER_LAYERS_DISABLE"] = "~implicit~"
        env["VK_LOADER_LAYERS_ENABLE"] = "VK_LAYER_CS2_overlay"
    result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--child"],
                            env=env, capture_output=True, text=True, timeout=30)
    log = Path(__file__).resolve().parent.parent / "build-tools" / (
        "overlay-loader-isolated.log" if "--isolated" in sys.argv else "overlay-loader-smoke.log")
    log.write_text(result.stdout + "\n" + result.stderr)
    print(result.stdout.strip())
    if result.returncode or 'Insert instance layer "VK_LAYER_CS2_overlay"' not in result.stderr:
        print("FAIL: see", log)
        return 1
    print("PASS: actual Vulkan loader inserted the registered overlay without CS2_OVERLAY;")
    print("instance creation and destruction succeeded in a non-game process.")
    print("Log:", log)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
