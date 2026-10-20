"""Unit tests for the session layer (`scripts/scenario/session.py`).

The session is the part that touches the machine: it finds the tools the
build produces, launches the game through the scheduled task, injects the
payload and hands out the typed façade. None of that needs a game to be
*tested* — the tool paths and the manifest parsing are pure, and the launch /
inject / teardown steps are driven against a fake ``_run``.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from typing import Any, cast
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import session  # noqa: E402
from scenario.protocol import (  # noqa: E402
    EventsResult,
    ExecResult,
    InputStatus,
    LookResult,
    PingResult,
    PressResult,
    StateSnapshotDict,
)
from scenario.control import Control  # noqa: E402
from scenario.screens import Can, Machine, Screen  # noqa: E402
from scenario.session import Game, SessionError  # noqa: E402
from scenario.tests.test_screens import snapshot  # noqa: E402


class FakeControl:
    """A control channel that answers from a script (no pipe, no game)."""

    def __init__(self, raw: StateSnapshotDict | None = None) -> None:
        self.raw: StateSnapshotDict = raw if raw is not None else snapshot()
        self.execs: list[str] = []
        self.calls: list[str] = []
        self.closed = False
        self.quit_called = False

    def state(self) -> StateSnapshotDict:
        self.calls.append("state")
        return self.raw

    def ping(self) -> PingResult:
        self.calls.append("ping")
        return {"pong": True, "pid": 1, "uptime_ms": 60_000, "exec_ready": True,
                "events_ready": True, "events_subscribed": 0,
                "events_dispatch_calls": 0, "input_ready": True,
                "input_hooked": True, "input_backend": "console",
                "state_ready": True}

    def exec(self, line: str) -> ExecResult:
        self.execs.append(line)
        return {"queued": True}

    def events(self, limit: int = 64,
               subscribe: str | list[str] | None = None) -> EventsResult:
        return {"count": 0, "subscribe": {}, "events": []}

    def press(self, key: str, hold: float = 0.0,
              via: str | None = None) -> PressResult:
        self.calls.append(f"press:{key}")
        return {"button": key, "held": hold <= 0}

    def release(self, key: str, via: str | None = None) -> ExecResult:
        self.calls.append(f"release:{key}")
        return {"queued": True}

    def input(self, via: str | None = None) -> InputStatus:
        return {"backend": via or "console", "ready": True, "hooked": True}

    def buttons(self, *, press: Any = None, release: Any = None,
                reset: bool = False) -> InputStatus:
        return {"backend": "usercmd", "ready": True, "hooked": True}

    def bhop(self, on: bool = True) -> InputStatus:
        return {"backend": "usercmd", "bhop": on}

    def strafe(self, on: bool = True, *, wishspeed: float | None = None,
               subtick: int | None = None) -> InputStatus:
        return {"backend": "usercmd", "strafe": on}

    def look(self, **kwargs: Any) -> LookResult:
        self.calls.append("look")
        return {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}

    def move(self, **kwargs: Any) -> InputStatus:
        self.calls.append("move")
        return {"backend": "usercmd"}

    def noclip(self, enabled: bool = True) -> dict[str, bool]:
        return {"noclip": enabled}

    def teleport(self, x: float, y: float, z: float | None = None) -> ExecResult:
        self.calls.append("teleport")
        return {"queued": True}

    def quit(self) -> None:
        self.quit_called = True
        self.close()

    def close(self) -> None:
        self.closed = True

    def connect(self, timeout: float = 30.0, interval: float = 0.25) -> "FakeControl":
        return self


def completed(argv: list[str], returncode: int = 0,
              stdout: str = "", stderr: str = "") -> subprocess.CompletedProcess[str]:
    """A finished process, as ``_run`` returns one."""
    return subprocess.CompletedProcess(argv, returncode, stdout, stderr)


class TestToolPaths(unittest.TestCase):
    """Where the session looks for the tools the build produces."""

    def test_the_repository_root_is_the_root(self) -> None:
        """The paths are relative to the repository, not to the caller."""
        self.assertTrue((session.REPO_ROOT / "meson.build").is_file())

    def test_the_payload_is_looked_for_in_the_payload_build(self) -> None:
        self.assertEqual(session.payload_dll(),
                         session.REPO_ROOT / "build-payload" / "payload.dll")

    def test_the_injector_is_looked_for_in_the_injector_build(self) -> None:
        self.assertEqual(session.injector_exe(),
                         session.REPO_ROOT / "build-injector" / "injector.exe")

    def test_the_dumper_is_looked_for_in_the_dumper_checkout(self) -> None:
        path = session.dumper_exe()
        self.assertEqual(path.parent,
                         session.REPO_ROOT / "external" / "dumper" / "target" / "release")
        self.assertTrue(path.name.startswith("cs2-dumper"))


class TestManifest(unittest.TestCase):
    """The dumper's manifest — the process discovery."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        # The session checks that the tool *exists* before it runs it, so the
        # fake dumper has to be a real (empty) file.
        self.dumper = pathlib.Path(self.directory.name) / "cs2-dumper.exe"
        self.dumper.write_bytes(b"")

    def test_the_manifest_is_parsed(self) -> None:
        """The dumper answers with JSON; the session hands it on."""
        manifest = {"process": "cs2.exe", "pid": 4242,
                    "modules": [{"name": "client.dll"}]}
        with mock.patch.object(session, "dumper_exe",
                               return_value=self.dumper), \
                mock.patch.object(session, "_run",
                                  return_value=completed(["dumper"],
                                                         stdout=json.dumps(manifest))):
            self.assertEqual(session.process_manifest(), manifest)

    def test_a_process_that_is_not_running_has_no_manifest(self) -> None:
        with mock.patch.object(session, "dumper_exe",
                               return_value=self.dumper), \
                mock.patch.object(session, "_run",
                                  return_value=completed(["dumper"], returncode=1)):
            self.assertIsNone(session.process_manifest())

    def test_a_broken_manifest_is_an_error(self) -> None:
        """Garbage on stdout is a broken dumper, not "the game is not running"."""
        with mock.patch.object(session, "dumper_exe",
                               return_value=self.dumper), \
                mock.patch.object(session, "_run",
                                  return_value=completed(["dumper"], stdout="not json")):
            with self.assertRaises(SessionError) as raised:
                session.process_manifest()
            self.assertIn("bad dumper manifest", str(raised.exception))

    def test_a_missing_dumper_is_an_error(self) -> None:
        with mock.patch.object(session, "dumper_exe",
                               return_value=pathlib.Path("no-such-dumper.exe")):
            with self.assertRaises(SessionError) as raised:
                session.process_manifest()
            self.assertIn("dumper not built", str(raised.exception))

    def test_waiting_for_the_process_gives_up(self) -> None:
        with mock.patch.object(session, "process_manifest", return_value=None):
            with self.assertRaises(SessionError) as raised:
                session.wait_for_process(timeout=0.0)
            self.assertIn("did not start", str(raised.exception))

    def test_waiting_for_the_process_returns_the_manifest(self) -> None:
        manifest = {"process": "cs2.exe", "pid": 1, "modules": []}
        with mock.patch.object(session, "process_manifest", return_value=manifest):
            self.assertEqual(session.wait_for_process(timeout=1.0), manifest)

    def test_waiting_for_the_module_needs_the_client_dll(self) -> None:
        """``client.dll`` is the point where the control channel works."""
        without = {"process": "cs2.exe", "pid": 1,
                   "modules": [{"name": "engine.dll"}]}
        with mock.patch.object(session, "process_manifest", return_value=without):
            with self.assertRaises(SessionError) as raised:
                session.wait_for_module(timeout=0.0)
            self.assertIn("client.dll not loaded", str(raised.exception))

    def test_waiting_for_the_module_returns_the_manifest(self) -> None:
        manifest = {"process": "cs2.exe", "pid": 1,
                    "modules": [{"name": "CLIENT.DLL"}]}
        with mock.patch.object(session, "process_manifest", return_value=manifest):
            self.assertEqual(session.wait_for_module(timeout=1.0), manifest)


class TestActions(unittest.TestCase):
    """The launch / inject / kill steps."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        # The session checks that the tools exist before it runs them.
        self.injector = pathlib.Path(self.directory.name) / "injector.exe"
        self.injector.write_bytes(b"")
        self.payload = pathlib.Path(self.directory.name) / "payload.dll"
        self.payload.write_bytes(b"")

    def test_the_game_is_launched_through_the_scheduled_task(self) -> None:
        """The agent runs in session 0 — the game has to start in session 1."""
        recorded: list[list[str]] = []

        def run(argv: list[str]) -> subprocess.CompletedProcess[str]:
            recorded.append(argv)
            return completed(argv)

        with mock.patch.object(session, "_run", side_effect=run):
            session.launch_cs2()
        self.assertEqual(recorded[0][:2], ["schtasks", "/run"])
        self.assertIn(session.CS2_TASK, recorded[0])

    def test_a_failing_launch_is_reported(self) -> None:
        with mock.patch.object(session, "_run",
                               return_value=completed(["schtasks"], returncode=1,
                                                      stderr="access denied")):
            with self.assertRaises(SessionError) as raised:
                session.launch_cs2()
            self.assertIn(session.CS2_TASK, str(raised.exception))

    def test_the_stale_process_is_killed(self) -> None:
        recorded: list[list[str]] = []

        def run(argv: list[str]) -> subprocess.CompletedProcess[str]:
            recorded.append(argv)
            return completed(argv)

        with mock.patch.object(session, "_run", side_effect=run), \
                mock.patch.object(session, "process_manifest", return_value=None):
            session.kill_cs2()
        self.assertEqual(recorded[0][:2], ["taskkill", "/IM"])
        self.assertIn(session.PLAYER_PROCESS, recorded[0])

    def test_a_process_that_will_not_die_is_reported(self) -> None:
        with mock.patch.object(session, "_run",
                               return_value=completed(["taskkill"])), \
                mock.patch.object(session, "process_manifest",
                                  return_value={"pid": 1}):
            with self.assertRaises(SessionError) as raised:
                session.kill_cs2(timeout=0.0)
            self.assertIn("did not exit", str(raised.exception))

    def test_the_injection_names_the_target_and_the_image(self) -> None:
        recorded: list[list[str]] = []

        def run(argv: list[str]) -> subprocess.CompletedProcess[str]:
            recorded.append(argv)
            return completed(argv)

        with mock.patch.object(session, "injector_exe",
                               return_value=self.injector), \
                mock.patch.object(session, "payload_dll",
                                  return_value=self.payload), \
                mock.patch.object(session, "_run", side_effect=run):
            session.inject(pid=4242)
        self.assertEqual(recorded[0][1:3], ["4242", str(self.payload)])
        self.assertIn("--load-missing", recorded[0])

    def test_a_missing_payload_is_reported(self) -> None:
        with mock.patch.object(session, "injector_exe",
                               return_value=self.injector), \
                mock.patch.object(session, "payload_dll",
                                  return_value=pathlib.Path("no-such-payload.dll")):
            with self.assertRaises(SessionError) as raised:
                session.inject()
            self.assertIn("payload not built", str(raised.exception))

    def test_a_failed_injection_is_reported(self) -> None:
        with mock.patch.object(session, "injector_exe",
                               return_value=self.injector), \
                mock.patch.object(session, "payload_dll",
                                  return_value=self.payload), \
                mock.patch.object(session, "_run",
                                  return_value=completed(["injector"], returncode=1,
                                                         stderr="access denied")):
            with self.assertRaises(SessionError) as raised:
                session.inject()
            self.assertIn("injection failed", str(raised.exception))


class TestFacade(unittest.TestCase):
    """The typed façade the scenarios program against."""

    def facade(self) -> tuple[Game, FakeControl]:
        control = FakeControl()
        return Game(cast(Control, control)), control

    def test_the_state_is_the_validated_snapshot(self) -> None:
        game, control = self.facade()
        self.assertIs(game.state(), control.raw)

    def test_the_players_are_the_snapshots_players(self) -> None:
        game, _ = self.facade()
        self.assertEqual(len(game.players()), 2)

    def test_the_local_player_is_the_snapshots_local(self) -> None:
        game, _ = self.facade()
        self.assertIsNotNone(game.local())

    def test_the_console_lines_are_forwarded(self) -> None:
        game, control = self.facade()
        game.exec("sv_cheats 1")
        self.assertEqual(control.execs, ["sv_cheats 1"])

    def test_the_screen_is_the_deduction(self) -> None:
        game, _ = self.facade()
        self.assertIs(game.screen(), Screen.LIVE)

    def test_the_game_state_is_the_observation(self) -> None:
        game, _ = self.facade()
        self.assertIs(game.game_state().screen, Screen.LIVE)

    def test_a_capability_can_be_required(self) -> None:
        game, _ = self.facade()
        self.assertTrue(game.require(Can.MOVE).can(Can.MOVE))

    def test_a_missing_capability_is_refused_with_the_reason(self) -> None:
        game, _ = self.facade()
        with self.assertRaises(SessionError):
            game.require(Can.RECONNECT)

    def test_the_state_machine_is_bound_to_the_session(self) -> None:
        game, _ = self.facade()
        machine = game.state_machine()
        self.assertIsInstance(machine, Machine)
        self.assertIs(machine.game, game)

    def test_the_screen_can_be_driven(self) -> None:
        game, _ = self.facade()
        self.assertIs(game.ensure_screen(Screen.LIVE).screen, Screen.LIVE)

    def test_the_input_helpers_are_forwarded(self) -> None:
        game, control = self.facade()
        game.press("w", hold=0.1)
        game.release("w")
        self.assertIn("press:w", control.calls)
        self.assertIn("release:w", control.calls)


class TestSession(unittest.TestCase):
    """The context manager that sets the whole thing up."""

    def test_the_session_sets_up_and_tears_down(self) -> None:
        """The order matters: launch, wait, inject, connect, ping, quit."""
        order: list[str] = []
        control = FakeControl()

        class FakeSessionControl(FakeControl):
            def connect(self, timeout: float = 30.0,
                        interval: float = 0.25) -> "FakeSessionControl":
                order.append("connect")
                return self

        control = FakeSessionControl()

        def fake_kill(wait: bool = True, timeout: float = 20.0) -> None:
            order.append("kill")

        def fake_launch() -> None:
            order.append("launch")

        with mock.patch.object(session, "kill_cs2", side_effect=fake_kill), \
                mock.patch.object(session, "launch_cs2", side_effect=fake_launch), \
                mock.patch.object(session, "wait_for_process",
                                  return_value={"pid": 1, "modules": []}), \
                mock.patch.object(session, "wait_for_module",
                                  return_value={"pid": 1, "modules": []}), \
                mock.patch.object(session, "inject", side_effect=lambda **_: None), \
                mock.patch.object(session, "Control", return_value=control):
            with session.cs2_session() as game:
                self.assertIsInstance(game, Game)
        self.assertEqual(order[:2], ["kill", "launch"])
        self.assertIn("connect", order)
        self.assertTrue(control.quit_called)

    def test_a_game_that_does_not_answer_the_ping_is_refused(self) -> None:
        """A payload that does not answer is not a session."""

        class SilentControl(FakeControl):
            def ping(self) -> PingResult:
                answer = super().ping()
                answer["pong"] = False
                return answer

        control = SilentControl()
        with mock.patch.object(session, "kill_cs2"), \
                mock.patch.object(session, "launch_cs2"), \
                mock.patch.object(session, "wait_for_process",
                                  return_value={"pid": 1, "modules": []}), \
                mock.patch.object(session, "wait_for_module",
                                  return_value={"pid": 1, "modules": []}), \
                mock.patch.object(session, "inject"), \
                mock.patch.object(session, "Control", return_value=control):
            with self.assertRaises(SessionError):
                with session.cs2_session():
                    pass


if __name__ == "__main__":
    unittest.main(verbosity=2)
