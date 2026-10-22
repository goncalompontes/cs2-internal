"""Unit tests for the control transport (`scripts/scenario/control.py`).

The transport is the only part of the harness that talks to the game, and it
had **no** tests at all: a framing bug (a response split over two reads, a
stale ``id``, a stray non-JSON line) would show up as a scenario that hangs,
which is the hardest kind of failure to diagnose.

So the pipe is faked here — a small object with the same ``read``/``write``/
``close`` surface the client uses — and the wire is asserted from both sides:
the *request* the client writes and the *answer* it accepts.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import threading
import time
import unittest
from typing import Any, Sequence

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario.control import BUTTONS, Control, ControlError, ControlTimeout  # noqa: E402
from scenario.protocol import ProtocolError  # noqa: E402


class FakePipe:
    """A pipe with the surface the control client uses.

    The read side is a **script**: every ``read`` hands out the next chunk
    (the bytes the payload "sent"), and an empty script is the end of the
    stream — the payload closed the channel. The write side records what the
    client sent, so a test can assert on the request as well as on the answer.
    """

    def __init__(self, chunks: Sequence[bytes] = ()) -> None:
        self.chunks: list[bytes] = list(chunks)
        self.written = bytearray()
        self.closed = False
        #: How many bytes one ``write`` accepts at a time (0 = "all of them").
        self.write_limit = 0
        #: Whether the pipe accepts writes at all (a dead pipe accepts none).
        self.accepts_writes = True

    def read(self, size: int = -1) -> bytes:
        if not self.chunks:
            return b""  # end of the stream: the channel is gone
        return self.chunks.pop(0)

    def write(self, data: bytes | memoryview) -> int:
        if not self.accepts_writes:
            return 0
        if self.write_limit:
            chunk = bytes(memoryview(data)[: self.write_limit])
            self.written.extend(chunk)
            return len(chunk)
        self.written.extend(bytes(memoryview(data)))
        return len(memoryview(data))

    def close(self) -> None:
        self.closed = True


class SilentPipe:
    """A pipe that accepts the request and never answers.

    This is the shape of the failure the deadline exists for: the payload
    took the request and never wrote an answer back.  The read blocks until
    the pipe is closed (that is what releases the reader).
    """

    def __init__(self) -> None:
        self.written = bytearray()
        self.closed = False
        self._released = threading.Event()

    def read(self, size: int = -1) -> bytes:
        self._released.wait()
        return b""

    def write(self, data: bytes | memoryview) -> int:
        self.written.extend(bytes(memoryview(data)))
        return len(memoryview(data))

    def close(self) -> None:
        self.closed = True
        self._released.set()


def frame(message: dict[str, Any]) -> bytes:
    """One ``Content-Length`` frame, as the payload writes it."""
    body = json.dumps(message).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def answer(request_id: int = 1, result: Any = None, *, ok: bool = True,
           error: str | None = None) -> bytes:
    """One JSON-RPC answer frame."""
    if ok:
        return frame({"jsonrpc": "2.0", "id": request_id, "result": result})
    message = error if error is not None else "failed"
    return frame({"jsonrpc": "2.0", "id": request_id,
                  "error": {"code": -32000, "message": message}})


def connected(chunks: Sequence[bytes] = ()) -> tuple[Control, "FakePipe"]:
    """A control whose socket is the fake (the transport's only seam)."""
    control = Control("fake-pipe")
    pipe = FakePipe(chunks)
    control._pipe = pipe
    return control, pipe


def _frames(pipe: "FakePipe") -> list[dict[str, Any]]:
    """Parse every complete request frame the client wrote."""
    data = bytes(pipe.written)
    messages: list[dict[str, Any]] = []
    while True:
        header_end = data.find(b"\r\n\r\n")
        if header_end == -1:
            break
        header = data[:header_end].decode("ascii", "replace")
        length = 0
        for line in header.split("\r\n"):
            name, _, value = line.partition(":")
            if name.strip().lower() == "content-length":
                length = int(value.strip())
        body_start = header_end + 4
        if len(data) < body_start + length:
            break
        messages.append(json.loads(data[body_start:body_start + length]))
        data = data[body_start + length:]
    return messages


def sent(pipe: "FakePipe") -> list[dict[str, Any]]:
    """The requests in the legacy shorthand (``id``/``cmd``/``args``)."""
    return [{"id": message.get("id"), "cmd": message.get("method"),
             "args": message.get("params", {})} for message in _frames(pipe)]


class TestRequestEnvelope(unittest.TestCase):
    """What the client puts on the wire."""

    def test_the_envelope_is_jsonrpc_2(self) -> None:
        control, pipe = connected([answer(1, {"pong": True})])
        control.request("ping")
        message = _frames(pipe)[0]
        self.assertEqual(message["jsonrpc"], "2.0")
        self.assertEqual(message["id"], 1)
        self.assertEqual(message["method"], "ping")

    def test_the_arguments_are_sent_as_params(self) -> None:
        control, pipe = connected([answer(1, {"queued": True})])
        control.request("exec", {"line": "sv_cheats 1"})
        self.assertEqual(_frames(pipe)[0]["params"], {"line": "sv_cheats 1"})

    def test_the_ids_increase(self) -> None:
        control, pipe = connected([answer(1, {}), answer(2, {})])
        control.request("ping")
        control.request("ping")
        self.assertEqual([request["id"] for request in sent(pipe)], [1, 2])

    def test_the_request_is_one_content_length_frame(self) -> None:
        control, pipe = connected([answer(1, {})])
        control.request("ping")
        raw = bytes(pipe.written)
        self.assertTrue(raw.startswith(b"Content-Length: "))
        self.assertIn(b"\r\n\r\n", raw)
        self.assertFalse(raw.endswith(b"\n"))

    def test_the_result_is_returned(self) -> None:
        control, _ = connected([answer(1, {"pong": True, "pid": 7})])
        self.assertEqual(control.request("ping"), {"pong": True, "pid": 7})

    def test_a_stale_id_is_skipped(self) -> None:
        control, _ = connected([answer(99, {"stale": True}), answer(1, {"mine": True})])
        self.assertEqual(control.request("ping"), {"mine": True})

    def test_invalid_json_is_an_error(self) -> None:
        control, _ = connected([b"Content-Length: 4\r\n\r\nnope"])
        with self.assertRaises(ControlError):
            control.request("ping")

    def test_an_error_answer_raises_with_the_servers_message(self) -> None:
        control, _ = connected([answer(1, ok=False, error="unknown method: nope")])
        with self.assertRaises(ControlError) as raised:
            control.request("nope")
        self.assertIn("unknown method", str(raised.exception))

    def test_an_error_answer_without_a_message_still_raises(self) -> None:
        control, _ = connected([answer(1, ok=False)])
        with self.assertRaises(ControlError):
            control.request("ping")

    def test_an_answer_without_a_result_is_none(self) -> None:
        control, _ = connected([frame({"jsonrpc": "2.0", "id": 1})])
        self.assertIsNone(control.request("ping"))


class TestFraming(unittest.TestCase):
    """The ``Content-Length`` framing - the part a stream can get wrong."""

    def test_a_frame_split_over_two_reads_is_one_answer(self) -> None:
        message = answer(1, {"mine": True})
        cut = len(message) // 2
        control, pipe = connected([message[:cut], message[cut:]])
        self.assertEqual(control.request("ping"), {"mine": True})

    def test_two_frames_in_one_read_are_separated(self) -> None:
        control, _ = connected([answer(1, {"first": True}) + answer(2, {"second": True})])
        self.assertEqual(control.request("ping"), {"first": True})
        self.assertEqual(control.request("ping"), {"second": True})

    def test_a_partial_write_is_completed(self) -> None:
        control, pipe = connected([answer(1, {"mine": True})])
        pipe.write_limit = 7
        self.assertEqual(control.request("ping"), {"mine": True})
        self.assertEqual(len(_frames(pipe)), 1)


class TestFailures(unittest.TestCase):
    """The transport's failure modes."""

    def test_a_request_without_a_connection_is_refused(self) -> None:
        control = Control("fake-pipe")
        with self.assertRaises(ControlError):
            control.request("ping")

    def test_the_channel_closing_mid_request_is_an_error(self) -> None:
        control, _ = connected([])
        with self.assertRaises(ControlError):
            control.request("ping")

    def test_a_pipe_that_cannot_be_written_to_is_an_error(self) -> None:
        control, pipe = connected([answer(1, {})])
        pipe.accepts_writes = False
        with self.assertRaises(ControlError):
            control.request("ping")

    def test_a_payload_that_never_answers_times_out(self) -> None:
        control = Control("fake-pipe", request_timeout=0.2)
        pipe = SilentPipe()
        control._pipe = pipe
        with self.assertRaises(ControlTimeout):
            control.request("ping")

    def test_the_timeout_names_the_budget(self) -> None:
        control = Control("fake-pipe", request_timeout=0.2)
        control._pipe = SilentPipe()
        with self.assertRaises(ControlTimeout) as raised:
            control.request("ping")
        self.assertIn("0.2s", str(raised.exception))


class TestConnection(unittest.TestCase):
    """The connection lifecycle."""

    def test_connect_reports_a_socket_that_never_appears(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            missing = pathlib.Path(directory) / "no-such-socket"
            control = Control(str(missing))
            with self.assertRaises(ControlError) as raised:
                control.connect(timeout=0.0)
            self.assertIn("cannot connect", str(raised.exception))

    def test_the_context_manager_closes_the_pipe(self) -> None:
        control, pipe = connected()
        with control as opened:
            self.assertIs(opened, control)
        self.assertTrue(pipe.closed)

    def test_close_is_idempotent(self) -> None:
        control, pipe = connected()
        control.close()
        control.close()
        self.assertTrue(pipe.closed)

class TestWaitForBuilder(unittest.TestCase):
    """The client's side of the ``wait_for`` predicate DSL.

    The *server*'s grammar is tested in C++ (``control-core``); this is the
    half that builds the request: the predicates travel as an ``expr`` object,
    the timeout as milliseconds, and the event as its own field.
    """

    def test_the_predicates_travel_as_an_expression(self) -> None:
        control, pipe = connected([answer(1, {"matched": True, "snapshot": None})])
        control.wait_for(local_alive=True, players_gte=3)
        request = sent(pipe)[0]
        self.assertEqual(request["cmd"], "wait_for")
        self.assertEqual(request["args"]["expr"],
                         {"local_alive": True, "players_gte": 3})

    def test_the_timeout_is_milliseconds(self) -> None:
        control, pipe = connected([answer(1, {"matched": True, "snapshot": None})])
        control.wait_for(timeout=1.5)
        self.assertEqual(sent(pipe)[0]["args"]["timeout_ms"], 1500)

    def test_no_predicate_waits_for_the_local_player(self) -> None:
        """With nothing to match, the wait is "a local player exists"."""
        control, pipe = connected([answer(1, {"matched": True, "snapshot": None})])
        control.wait_for()
        self.assertEqual(sent(pipe)[0]["args"]["expr"], {"in_map": True})

    def test_an_event_is_its_own_field(self) -> None:
        control, pipe = connected([answer(1, {"matched": True, "snapshot": None})])
        control.wait_for(event="player_death")
        request = sent(pipe)[0]
        self.assertEqual(request["args"]["event"], "player_death")
        self.assertNotIn("expr", request["args"])

    def test_the_event_arguments_travel_with_it(self) -> None:
        control, pipe = connected([answer(1, {"matched": True, "snapshot": None})])
        control.wait_for(event="player_death", event_args={"headshot": True})
        self.assertEqual(sent(pipe)[0]["args"]["event_args"], {"headshot": True})

    def test_the_expression_form_takes_the_object(self) -> None:
        """``not`` is a Python keyword — the object form is the way to spell it."""
        control, pipe = connected([answer(1, {"matched": True, "snapshot": None})])
        control.wait_for_expr({"not": {"local_alive": True}}, timeout=2.0)
        request = sent(pipe)[0]
        self.assertEqual(request["args"]["expr"], {"not": {"local_alive": True}})
        self.assertEqual(request["args"]["timeout_ms"], 2000)

    def test_the_nested_expression_is_sent_unchanged(self) -> None:
        control, pipe = connected([answer(1, {"matched": True, "snapshot": None})])
        control.wait_for_expr({"all": [{"in_map": True}, {"players_gte": 2}]})
        self.assertEqual(sent(pipe)[0]["args"]["expr"],
                         {"all": [{"in_map": True}, {"players_gte": 2}]})


class TestButtonTable(unittest.TestCase):
    """The friendly button names the client maps onto the engine's own."""

    def test_the_table_maps_the_friendly_names(self) -> None:
        self.assertEqual(BUTTONS["w"], "forward")
        self.assertEqual(BUTTONS["space"], "jump")

    def test_an_unknown_name_is_passed_through(self) -> None:
        """The engine's own button names are accepted as they are."""
        self.assertNotIn("+use_hack", BUTTONS)

    def test_a_typo_is_refused_and_never_reaches_the_console(self) -> None:
        """The console path must not issue an unknown ``+<garbage>`` command.

        The engine answers an unknown command with an error the caller never
        sees, so the press would be a silent no-op.
        """
        control, pipe = connected([answer(1, {"queued": True})])
        with self.assertRaises(ControlError):
            control.press("spcae")
        self.assertEqual(bytes(pipe.written), b"")

    def test_a_known_alias_reaches_the_console(self) -> None:
        control, pipe = connected([answer(1, {"queued": True})])
        result = control.press("space")
        self.assertEqual(result["button"], "jump")
        self.assertIn(b'"line": "+jump"', bytes(pipe.written))

    def test_a_usercmd_only_name_needs_the_usercmd_path(self) -> None:
        """``moveleft`` has no console command: the mask path is the only one."""
        control, pipe = connected([answer(1, {"buttons_set": 1, "buttons_clear": 0})])
        with self.assertRaises(ControlError):
            control.press("moveleft")
        self.assertEqual(bytes(pipe.written), b"")


class TestSettings(unittest.TestCase):
    """The feature settings block (the ``settings`` command)."""

    def test_the_read_asks_for_the_named_fields(self) -> None:
        control, pipe = connected([answer(1, {"values": {"esp_box": 1}})])
        control.settings_get(["esp_box"])
        request = sent(pipe)[0]
        self.assertEqual(request["cmd"], "settings")
        self.assertEqual(request["args"]["get"], ["esp_box"])

    def test_the_read_without_fields_asks_for_everything(self) -> None:
        control, pipe = connected([answer(1, {"values": {"esp_box": 1}})])
        control.settings_get()
        self.assertNotIn("get", sent(pipe)[0]["args"])

    def test_a_flag_may_be_written_as_a_bool(self) -> None:
        """The payload's ``write_field`` reads a flag's ``true``/``false``."""
        control, pipe = connected([answer(1, {"changed": ["esp_box"]})])
        control.settings_set({"esp_box": True})
        request = sent(pipe)[0]
        self.assertEqual(request["cmd"], "settings")
        self.assertEqual(request["args"]["set"], {"esp_box": True})


class TestValidation(unittest.TestCase):
    """The validated answers — the boundary against a skewed payload."""

    def test_a_mistyped_snapshot_is_refused(self) -> None:
        """A payload that answers with a wrong type must fail at the boundary."""
        broken = {"valid": True, "in_map": True, "local_pawn": 0,
                  "level": {"up": True, "proxy": "0x1", "rules_global": True,
                            "identities": 1, "local_connected": 0},
                  "rules": {}, "local": None, "players": []}
        control, _ = connected([answer(1, broken)])
        with self.assertRaises(ProtocolError):
            control.state()


if __name__ == "__main__":
    unittest.main(verbosity=2)
