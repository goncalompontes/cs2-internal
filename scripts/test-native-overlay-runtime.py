"""Load the actual payload in an agent-created hidden WARP host; never use CS2."""
from pathlib import Path
import os
import subprocess
import time

project = Path(__file__).resolve().parent.parent
state = project / "build-tools/native-runtime-smoke"
state.mkdir(exist_ok=True)
env = os.environ.copy()
env["CS2_PAYLOAD_DIR"] = str(state)
env["CS2_PAYLOAD_NO_DIAG"] = "1"
env["CS2_PAYLOAD_NO_CONTROL"] = "1"
log = state / "cs2-payload.log"
log.write_text("")
host = subprocess.Popen([str(project / "build-payload/features/native_overlay_test.exe"), "--host"],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env,
                        creationflags=subprocess.CREATE_NO_WINDOW)
try:
    assert host.stdout.readline().strip() == "READY", "software graphics host did not start"
    result = subprocess.run([str(project / "build-injector/injector.exe"), str(host.pid),
                             str(project / "build-payload/payload.dll"), "--load-library"],
                            capture_output=True, text=True, timeout=15)
    print(result.stdout.strip())
    assert result.returncode == 0, result.stderr
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        content = log.read_text(errors="replace") if log.exists() else ""
        if "[native-dx11] overlay frame rendered" in content:
            break
        time.sleep(0.1)
    else:
        raise AssertionError("Payload did not render: " + content[-2500:])
    print("PASS: actual payload rendered through its own native DXGI hook in the hidden WARP host.")
finally:
    if host.poll() is None:
        host.terminate()
    host.wait(timeout=5)
