"""Unit tests for the MCP server (``scripts/scenario/mcp/``).

The server is the scenario API as tools, so what has to hold is:

* the **schema bridge** projects the typed models onto JSON Schema (and the
  validator keeps a bad argument from reaching the game);
* the **server** speaks MCP over stdio: ``initialize``, ``tools/list``,
  ``tools/call`` and the notifications, and it answers — never crashes — on a
  malformed request, an unknown method, an unknown tool or a tool that raises;
* the **session** is lazy, cached, self-healing and never kills the game;
* the **server is standard library only**.

The wire tests speak the protocol to a real subprocess over a pipe; the
conformance test drives it with the real ``mcp`` client (and is skipped when
that package is not installed — it is a test-time convenience, not a runtime
dependency).

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import ast
import contextlib
import io
import json
import os
import pathlib
import queue
import subprocess
import sys
import threading
import time
import unittest
from typing import Any, Literal, Mapping, NotRequired, Required, TypedDict, cast, get_args

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import protocol  # noqa: E402
from scenario.control import Control, ControlError  # noqa: E402
from scenario.mcp import schema as schema_bridge  # noqa: E402
from scenario.mcp import server as mcp_server  # noqa: E402
from scenario.mcp import session as mcp_session  # noqa: E402
from scenario.mcp import tools as mcp_tools  # noqa: E402
from scenario.screens import Screen  # noqa: E402

#: The server script (the process the OpenCode config starts).
SERVER = pathlib.Path(__file__).resolve().parents[1] / "mcp" / "server.py"

#: A pipe that certainly does not exist: the "the game is not running" path.
MISSING_PIPE = r"\\.\pipe\cs2-internal-control-test-missing"

#: The tools the catalogue must expose (one per API area).
EXPECTED_TOOLS = (
    "cs2_ping", "cs2_state", "cs2_screen", "cs2_ensure", "cs2_exec",
    "cs2_look", "cs2_move", "cs2_press", "cs2_release", "cs2_aim",
    "cs2_rage", "cs2_events", "cs2_wait_for", "cs2_settings_get",
    "cs2_settings_set", "cs2_list_scenarios", "cs2_run_scenario",
    "cs2_launch", "cs2_inject",
)


# --- the sample models for the bridge --------------------------------------
class Sample(TypedDict):
    """A closed shape: every key is required."""

    name: str
    count: int
    ratio: float
    flag: bool


class OptionalSample(TypedDict, total=False):
    """A wide shape: no key is required."""

    name: str
    count: int


class Nested(TypedDict, total=False):
    """Every kind the bridge has to know."""

    inner: Sample
    items: list[str]
    mapping: dict[str, int]
    maybe: str | None
    choice: Literal["a", "b"]
    marked: Required[int]
    unmarked: NotRequired[str]


# --- the schema bridge -----------------------------------------------------
class SchemaBridgeTest(unittest.TestCase):
    """The TypedDict -> JSON Schema projection."""

    def test_a_typed_dict_becomes_an_object_schema(self) -> None:
        schema = schema_bridge.schema_for(Sample)
        self.assertEqual(schema["type"], "object")
        self.assertEqual(schema["properties"], {
            "name": {"type": "string"},
            "count": {"type": "integer"},
            "ratio": {"type": "number"},
            "flag": {"type": "boolean"},
        })

    def test_the_required_keys_are_the_total_ones(self) -> None:
        self.assertEqual(schema_bridge.schema_for(Sample)["required"],
                         ["count", "flag", "name", "ratio"])

    def test_an_optional_key_is_not_required(self) -> None:
        schema = schema_bridge.schema_for(OptionalSample)
        self.assertEqual(schema["required"], [])

    def test_additional_properties_is_false(self) -> None:
        self.assertIs(schema_bridge.schema_for(Sample)["additionalProperties"], False)
        self.assertIs(schema_bridge.schema_for(OptionalSample)["additionalProperties"],
                      False)

    def test_a_literal_becomes_an_enum(self) -> None:
        schema = schema_bridge.schema_for(Nested)
        self.assertEqual(schema["properties"]["choice"], {"enum": ["a", "b"]})

    def test_a_nested_typed_dict_becomes_a_nested_object(self) -> None:
        schema = schema_bridge.schema_for(Nested)
        inner = schema["properties"]["inner"]
        self.assertEqual(inner["type"], "object")
        self.assertEqual(inner["properties"]["name"], {"type": "string"})
        self.assertEqual(inner["required"], ["count", "flag", "name", "ratio"])
        self.assertIs(inner["additionalProperties"], False)

    def test_a_list_becomes_an_array(self) -> None:
        schema = schema_bridge.schema_for(Nested)
        self.assertEqual(schema["properties"]["items"],
                         {"type": "array", "items": {"type": "string"}})

    def test_a_dict_becomes_an_object_with_additional_properties(self) -> None:
        schema = schema_bridge.schema_for(Nested)
        self.assertEqual(schema["properties"]["mapping"],
                         {"type": "object", "additionalProperties": {"type": "integer"}})

    def test_a_union_with_none_becomes_any_of(self) -> None:
        schema = schema_bridge.schema_for(Nested)
        self.assertEqual(schema["properties"]["maybe"],
                         {"anyOf": [{"type": "string"}, {"type": "null"}]})

    def test_the_required_marker_is_honoured(self) -> None:
        schema = schema_bridge.schema_for(Nested)
        self.assertIn("marked", schema["required"])
        self.assertNotIn("unmarked", schema["required"])

    def test_a_not_required_key_is_not_required(self) -> None:
        # The payload's answer may carry the field or not: `PingResult.protocol`
        # is `NotRequired`, and under `from __future__ import annotations` the
        # class attribute still claims it — the resolved hint has to win.
        self.assertIn("protocol", protocol.PingResult.__required_keys__)
        schema = schema_bridge.schema_for(protocol.PingResult)
        self.assertNotIn("protocol", schema["required"])
        self.assertEqual(schema["properties"]["protocol"], {"type": "integer"})

    def test_an_enum_becomes_an_enum_of_its_values(self) -> None:
        class WithScreen(TypedDict, total=False):
            target: Screen

        schema = schema_bridge.schema_for(WithScreen)
        values = schema["properties"]["target"]["enum"]
        self.assertEqual(values, [member.value for member in Screen])
        self.assertIn("not-ready", values)  # the kebab-case names
        self.assertEqual(schema["properties"]["target"]["type"], "string")

    def test_an_inexpressible_hint_falls_back_to_any_and_is_reported(self) -> None:
        class Weird(TypedDict, total=False):
            value: complex

        problems: list[str] = []
        schema = schema_bridge.schema_for(Weird, problems=problems)
        self.assertEqual(schema["properties"]["value"], {})
        self.assertTrue(problems)
        self.assertIn("complex", problems[0])

    def test_a_recursive_shape_is_expanded_to_a_bounded_depth(self) -> None:
        problems: list[str] = []
        schema = schema_bridge.schema_for(protocol.WaitExpr, problems=problems)
        self.assertEqual(schema["properties"]["in_map"], {"type": "boolean"})
        # The nesting is bounded: at the deepest level the shape is "an object".
        deepest = schema
        for _ in range(schema_bridge.MAX_EXPANSION_DEPTH):
            deepest = deepest["properties"]["not"]
        self.assertEqual(deepest["type"], "object")
        self.assertTrue(any("recursive" in problem for problem in problems))

    def test_the_predicate_keys_are_checked_four_levels_deep(self) -> None:
        """A bogus key is refused at the boundary, not only by the payload."""
        schema = schema_bridge.schema_for(protocol.WaitExpr)
        nested = schema
        for _ in range(3):
            nested = nested["properties"]["not"]
        errors = schema_bridge.validate_arguments(nested, {"nonsense": True})
        self.assertTrue(errors)
        self.assertIn("unknown argument", errors[0])


# --- the validator ---------------------------------------------------------
class ValidatorTest(unittest.TestCase):
    """The schema is also the contract the server enforces."""

    def setUp(self) -> None:
        self.schema = schema_bridge.schema_for(Sample)

    def test_a_valid_object_has_no_complaints(self) -> None:
        errors = schema_bridge.validate_arguments(
            self.schema, {"name": "x", "count": 1, "ratio": 0.5, "flag": True})
        self.assertEqual(errors, [])

    def test_a_missing_required_key_is_named(self) -> None:
        errors = schema_bridge.validate_arguments(
            self.schema, {"name": "x", "count": 1, "ratio": 0.5})
        self.assertTrue(errors)
        self.assertIn("flag", errors[0])

    def test_an_unknown_key_is_refused(self) -> None:
        errors = schema_bridge.validate_arguments(
            self.schema, {"name": "x", "count": 1, "ratio": 0.5, "flag": True, "x": 1})
        self.assertTrue(errors)
        self.assertIn("unknown argument", errors[0])

    def test_a_wrong_type_is_refused(self) -> None:
        errors = schema_bridge.validate_arguments(
            self.schema, {"name": "x", "count": "one", "ratio": 0.5, "flag": True})
        self.assertTrue(errors)
        self.assertIn("integer", errors[0])

    def test_a_bool_is_not_an_integer(self) -> None:
        errors = schema_bridge.validate_arguments(
            self.schema, {"name": "x", "count": True, "ratio": 0.5, "flag": True})
        self.assertTrue(errors)

    def test_an_enum_violation_is_refused(self) -> None:
        schema = schema_bridge.schema_for(Nested)
        errors = schema_bridge.validate_arguments(schema, {"choice": "c", "marked": 1})
        self.assertTrue(errors)
        self.assertIn("not one of", errors[0])

    def test_the_screen_enum_is_enforced(self) -> None:
        schema = schema_bridge.schema_for(mcp_tools.EnsureArgs)
        self.assertEqual(schema_bridge.validate_arguments(schema, {"target": "live"}), [])
        errors = schema_bridge.validate_arguments(schema, {"target": "LIVE"})
        self.assertTrue(errors)
        self.assertIn("not one of", errors[0])


# --- the catalogue ---------------------------------------------------------
class ToolCatalogueTest(unittest.TestCase):
    """The tools the server advertises."""

    def test_the_catalogue_holds_the_expected_tools(self) -> None:
        self.assertEqual(tuple(tool.name for tool in mcp_tools.TOOLS), EXPECTED_TOOLS)

    def test_every_tool_has_a_description(self) -> None:
        for tool in mcp_tools.TOOLS:
            self.assertTrue(tool.description.strip(), tool.name)

    def test_every_schema_is_an_object_schema(self) -> None:
        for tool in mcp_tools.TOOLS:
            schema = tool.input_schema()
            self.assertEqual(schema["type"], "object", tool.name)
            self.assertIs(schema["additionalProperties"], False, tool.name)
            self.assertIsInstance(schema["properties"], dict)

    def test_the_schemas_are_json_serialisable(self) -> None:
        for tool in mcp_tools.TOOLS:
            json.dumps(tool.input_schema())

    def test_the_ensure_target_is_the_screen_enum(self) -> None:
        tool = next(t for t in mcp_tools.TOOLS if t.name == "cs2_ensure")
        target = tool.input_schema()["properties"]["target"]
        self.assertEqual(target["enum"], [member.value for member in Screen])

    def test_the_scenario_name_enum_is_the_registry(self) -> None:
        from scenario.scenarios import SCENARIOS

        tool = next(t for t in mcp_tools.TOOLS if t.name == "cs2_run_scenario")
        self.assertEqual(tool.input_schema()["properties"]["name"]["enum"],
                         sorted(SCENARIOS))

    def test_the_button_key_is_a_closed_enum(self) -> None:
        """The advertised button names must actually *constrain* the argument.

        The key used to be ``ButtonName | str``: the union collapsed into a
        free-text branch, so the schema accepted **any** string and a typo
        (``"spcae"``) reached the game as the console command ``+spcae`` — a
        command the engine answers with an error nobody sees.
        """
        from scenario.control import BUTTONS
        from scenario.protocol import ButtonName, UsercmdButtonName

        for name in ("cs2_press", "cs2_release"):
            tool = next(t for t in mcp_tools.TOOLS if t.name == name)
            key = tool.input_schema()["properties"]["key"]
            with self.subTest(tool=name):
                self.assertEqual(set(key), {"enum"})
                allowed = set(key["enum"])
                self.assertEqual(allowed, set(get_args(ButtonName))
                                 | set(get_args(UsercmdButtonName)))
                # Every alias the runner documents is in the enum.
                for alias in BUTTONS:
                    self.assertIn(alias, allowed)

    def test_a_bogus_button_name_is_refused_at_the_boundary(self) -> None:
        tool = next(t for t in mcp_tools.TOOLS if t.name == "cs2_press")
        schema = tool.input_schema()
        errors = schema_bridge.validate_arguments(schema, {"key": "spcae"})
        self.assertTrue(errors)
        self.assertIn("not one of", errors[0])
        self.assertEqual(schema_bridge.validate_arguments(schema, {"key": "space"}), [])

    def test_a_settings_value_may_be_a_bool(self) -> None:
        """The payload's ``write_field`` accepts ``true``/``false`` for a flag."""
        tool = next(t for t in mcp_tools.TOOLS if t.name == "cs2_settings_set")
        schema = tool.input_schema()
        self.assertEqual(
            schema_bridge.validate_arguments(schema, {"values": {"esp_box": True}}), [])
        self.assertEqual(
            schema_bridge.validate_arguments(schema, {"values": {"esp_box": 1}}), [])
        self.assertEqual(
            schema_bridge.validate_arguments(schema,
                                            {"values": {"esp_box": [1, 2, 3, 4]}}), [])
        self.assertTrue(schema_bridge.validate_arguments(
            schema, {"values": {"esp_box": {"nope": 1}}}))

    def test_the_destructive_tools_need_a_confirmation(self) -> None:
        for name in ("cs2_launch", "cs2_inject"):
            tool = next(t for t in mcp_tools.TOOLS if t.name == name)
            self.assertIn("confirm", tool.input_schema()["required"], name)
            self.assertIn("DESTRUCTIVE", tool.description, name)

    def test_the_release_tool_has_no_hold(self) -> None:
        # Releasing a button takes no hold: the button is already down.
        tool = next(t for t in mcp_tools.TOOLS if t.name == "cs2_release")
        self.assertNotIn("hold", tool.input_schema()["properties"])

    def test_the_schemas_are_structurally_valid_json_schema(self) -> None:
        # A quick structural check: the keywords the bridge emits have to be
        # the ones JSON Schema knows, and the root has to be an object schema.
        known = {"type", "properties", "required", "additionalProperties", "items",
                 "enum", "anyOf"}
        for tool in mcp_tools.TOOLS:
            schema = tool.input_schema()
            self.assertEqual(schema["type"], "object", tool.name)
            self.assertIsInstance(schema["properties"], dict, tool.name)
            for key in schema["required"]:
                self.assertIn(key, schema["properties"], tool.name)
            for name, prop in schema["properties"].items():
                self._check(prop, known, f"{tool.name}.{name}")

    def _check(self, schema: Any, known: set[str], path: str) -> None:
        self.assertIsInstance(schema, dict, path)
        for key in schema:
            self.assertIn(key, known, f"{path}: {key}")
        if "anyOf" in schema:
            for branch in schema["anyOf"]:
                self._check(branch, known, path)
        if "items" in schema:
            self._check(schema["items"], known, f"{path}[]")
        if "properties" in schema:
            for name, prop in schema["properties"].items():
                self._check(prop, known, f"{path}.{name}")
        if isinstance(schema.get("additionalProperties"), dict):
            self._check(schema["additionalProperties"], known, f"{path}{{}}")


# --- the session -----------------------------------------------------------
class _FakeControl:
    """A channel that records what the session did to it."""

    def __init__(self) -> None:
        self.calls: list[str] = []

    def close(self) -> None:
        self.calls.append("close")

    def quit(self) -> None:  # pragma: no cover - must never be called
        self.calls.append("quit")

    def ping(self) -> dict[str, Any]:
        self.calls.append("ping")
        return {"pong": True}


class _FlakyControl(_FakeControl):
    """A channel whose first call finds the pipe closed."""

    def __init__(self) -> None:
        super().__init__()
        self.dropped = False

    def ping(self) -> dict[str, Any]:
        self.calls.append("ping")
        if not self.dropped:
            self.dropped = True
            raise ControlError("control channel closed")
        return {"pong": True}


class _FakeSession(mcp_session.Session):
    """A session whose channel (and façade) is handed out instead of opened."""

    def __init__(self, controls: list[Any] | None = None,
                 games: list[Any] | None = None) -> None:
        super().__init__(pipe_name="fake-pipe")
        self.controls = list(controls or [])
        self.games = list(games or [])
        self.connected = 0

    def _connect(self) -> Any:
        self.connected += 1
        return self.controls.pop(0)

    def game(self) -> Any:
        if self.games:
            return self.games.pop(0)
        return super().game()


class _FakeGame:
    """A façade whose first call finds the pipe closed (unless told not to)."""

    def __init__(self, drop: bool = True) -> None:
        self.calls: list[str] = []
        self.drop = drop

    def ping(self) -> dict[str, Any]:
        self.calls.append("ping")
        if self.drop and len(self.calls) == 1:
            raise ControlError("control channel closed")
        return {"pong": True}


class SessionTest(unittest.TestCase):
    """The lazy, cached, self-healing connection."""

    def test_the_message_says_what_to_do(self) -> None:
        message = mcp_session.not_running_message(MISSING_PIPE)
        self.assertIn("the game is not running", message)
        self.assertIn("schtasks /run /tn CS2Launch", message)
        self.assertIn("payload.dll", message)
        self.assertNotIn("Traceback", message)

    def test_a_dropped_pipe_is_reconnected(self) -> None:
        first, second = _FlakyControl(), _FakeControl()
        session = _FakeSession([first, second])
        result = session.call(lambda control: control.ping())
        self.assertTrue(result["pong"])
        self.assertEqual(session.connected, 2)
        self.assertIn("close", first.calls)

    def test_a_rejected_request_does_not_reconnect(self) -> None:
        class Rejecting(_FakeControl):
            def ping(self) -> dict[str, Any]:
                self.calls.append("ping")
                raise ControlError('settings: unknown key "nope"')

        control = Rejecting()
        session = _FakeSession([control])
        with self.assertRaises(ControlError):
            session.call(lambda channel: channel.ping())
        self.assertEqual(session.connected, 1)

    def test_the_facade_is_healed_too(self) -> None:
        # The state machine and the scenarios work on the façade, so the
        # reconnect has to work there as well.
        first, second = _FakeGame(), _FakeGame(drop=False)
        session = _FakeSession(games=[first, second])
        result = session.run(lambda game: game.ping())
        self.assertTrue(result["pong"])
        self.assertEqual(len(first.calls), 1)
        self.assertEqual(len(second.calls), 1)

    def test_the_game_is_never_killed(self) -> None:
        control = _FakeControl()
        session = mcp_session.Session(pipe_name="fake-pipe")
        session._control = cast(Control, control)
        session.close()
        self.assertNotIn("quit", control.calls)
        self.assertIn("close", control.calls)

    def test_the_missing_game_raises_the_actionable_error(self) -> None:
        session = mcp_session.Session(pipe_name=MISSING_PIPE, connect_timeout=0.2)
        with self.assertRaises(mcp_session.GameNotRunning) as caught:
            session.control()
        self.assertIn("the game is not running", str(caught.exception))


# --- the server, in process ------------------------------------------------
def _answer(response: dict[str, Any] | None) -> dict[str, Any]:
    """The server's answer to a request.

    ``Server.handle`` answers ``None`` for a *notification* — a message that
    must not be answered — so a request path has to say "there is an answer"
    before it reads one.
    """
    if response is None:
        raise AssertionError("the server answered a request with silence")
    return response


class ServerDispatchTest(unittest.TestCase):
    """The dispatch: every bad input is answered, never fatal."""

    def setUp(self) -> None:
        self.server = mcp_server.Server(
            session=mcp_session.Session(pipe_name=MISSING_PIPE, connect_timeout=0.2))

    def test_initialize_answers_with_the_version_and_the_capabilities(self) -> None:
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": "2025-06-18"},
        })
        result = _answer(response)["result"]
        self.assertEqual(result["protocolVersion"], "2025-06-18")
        self.assertIn("tools", result["capabilities"])
        self.assertEqual(result["serverInfo"]["name"], mcp_server.SERVER_NAME)

    def test_an_unknown_protocol_version_is_answered_with_a_known_one(self) -> None:
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": "1999-01-01"},
        })
        self.assertIn(_answer(response)["result"]["protocolVersion"],
                      mcp_server.PROTOCOL_VERSIONS)

    def test_an_unknown_method_is_an_error(self) -> None:
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "nope"})
        self.assertEqual(_answer(response)["error"]["code"], mcp_server.METHOD_NOT_FOUND)

    def test_a_malformed_request_is_an_error(self) -> None:
        response = self.server.handle(["not", "an", "object"])
        self.assertEqual(_answer(response)["error"]["code"], mcp_server.INVALID_REQUEST)

    def test_a_request_without_the_version_is_refused(self) -> None:
        """The envelope's version is part of the contract."""
        response = self.server.handle({"id": 1, "method": "initialize"})
        self.assertEqual(_answer(response)["error"]["code"], mcp_server.INVALID_REQUEST)

    def test_a_request_with_the_wrong_version_is_refused(self) -> None:
        response = self.server.handle({"jsonrpc": "1.0", "id": 1, "method": "initialize"})
        self.assertEqual(_answer(response)["error"]["code"], mcp_server.INVALID_REQUEST)

    def test_a_null_id_is_a_request_and_not_a_notification(self) -> None:
        """``"id": null`` is still an id: the message has to be answered."""
        response = self.server.handle({"jsonrpc": "2.0", "id": None, "method": "ping"})
        self.assertIsNotNone(response)
        self.assertEqual(_answer(response)["id"], None)

    def test_a_notification_is_never_answered(self) -> None:
        self.assertIsNone(self.server.handle(
            {"jsonrpc": "2.0", "method": "notifications/initialized"}))

    def test_a_notification_without_a_version_is_not_answered(self) -> None:
        """A notification is never answered — not even with an error."""
        self.assertIsNone(self.server.handle({"method": "notifications/initialized"}))

    def test_a_tool_call_before_the_handshake_is_refused(self) -> None:
        """The client must say hello before it drives the game."""
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "cs2_ping", "arguments": {}},
        })
        error = _answer(response)["error"]
        self.assertEqual(error["code"], mcp_server.INVALID_REQUEST)
        self.assertIn("initialize", error["message"])

    def test_the_tool_list_carries_the_schemas(self) -> None:
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        tools = _answer(response)["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], list(EXPECTED_TOOLS))
        for tool in tools:
            self.assertEqual(tool["inputSchema"]["type"], "object")

    def test_an_unknown_tool_is_a_protocol_error(self) -> None:
        self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "cs2_nope", "arguments": {}},
        })
        self.assertEqual(_answer(response)["error"]["code"], mcp_server.INVALID_PARAMS)

    def test_wrong_arguments_are_a_tool_error(self) -> None:
        self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "cs2_exec", "arguments": {"line": 5}},
        })
        result = _answer(response)["result"]
        self.assertTrue(result["isError"])
        self.assertIn("invalid arguments", result["content"][0]["text"])

    def test_a_tool_that_raises_is_a_tool_error(self) -> None:
        # The game is not running: that is an expected failure, so the agent
        # gets the message and no traceback.
        self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
        with contextlib.redirect_stderr(io.StringIO()) as diagnostics:
            response = self.server.handle({
                "jsonrpc": "2.0", "id": 1, "method": "tools/call",
                "params": {"name": "cs2_ping", "arguments": {}},
            })
        result = _answer(response)["result"]
        self.assertTrue(result["isError"])
        self.assertIn("the game is not running", result["content"][0]["text"])
        self.assertNotIn("Traceback", result["content"][0]["text"])
        self.assertNotIn("Traceback", diagnostics.getvalue())

    def test_the_exec_tool_refuses_a_command_that_ends_the_game(self) -> None:
        """``quit`` is a console command: the server never kills the game."""
        self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
        for line in ("quit", "quit; sv_cheats 1", "exit"):
            with self.subTest(line=line):
                response = self.server.handle({
                    "jsonrpc": "2.0", "id": 1, "method": "tools/call",
                    "params": {"name": "cs2_exec", "arguments": {"line": line}},
                })
                result = _answer(response)["result"]
                self.assertTrue(result["isError"])
                self.assertIn("never kills the game", result["content"][0]["text"])

    def test_a_client_that_vanished_does_not_crash_the_server(self) -> None:
        """The pipe to the client is gone: the server stops, it does not die.

        The write of the answer fails with ``BrokenPipeError``; without the
        catch the server would exit with a traceback — and the traceback
        would be on the *protocol* channel's stderr, next to the frames.
        """
        class BrokenStream(io.StringIO):
            def write(self, text: str) -> int:
                raise BrokenPipeError(32, "The pipe has been ended")

        source = io.StringIO(
            '{"jsonrpc": "2.0", "id": 1, "method": "initialize"}\n')
        with contextlib.redirect_stderr(io.StringIO()) as diagnostics:
            self.assertEqual(mcp_server.serve(BrokenStream(), source), 0)
        self.assertNotIn("Traceback", diagnostics.getvalue())


# --- the server, over a pipe ----------------------------------------------
class ServerProcess:
    """The server as a subprocess, spoken to over its pipes."""

    def __init__(self, cwd: str | None = None,
                 environment: Mapping[str, str] | None = None) -> None:
        self.process: subprocess.Popen[bytes] | None = None
        self.messages: queue.Queue[dict[str, Any]] = queue.Queue()
        #: The lines of stdout that were **not** protocol messages.
        self.bad_lines: list[str] = []
        self.stderr_lines: list[str] = []
        self.cwd = cwd
        #: The extra environment (the pipe, the deadlines).
        self.environment = dict(environment or {})

    def __enter__(self) -> "ServerProcess":
        environment = dict(os.environ)
        # A pipe that does not exist: the "the game is not running" path, and
        # a short timeout so the test does not wait for the default.
        environment["CS2_CONTROL_PIPE"] = MISSING_PIPE
        environment["CS2_CONTROL_CONNECT_TIMEOUT"] = "0.5"
        environment.update(self.environment)
        self.process = subprocess.Popen(
            [sys.executable, str(SERVER)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=environment, cwd=self.cwd,
        )
        for target, is_stderr in ((self.process.stdout, False),
                                  (self.process.stderr, True)):
            thread = threading.Thread(target=self._drain, args=(target, is_stderr),
                                      daemon=True)
            thread.start()
        return self

    def __exit__(self, *_exc: object) -> None:
        if self.process is None:
            return
        try:
            if self.process.stdin is not None:
                self.process.stdin.close()
        except OSError:  # pragma: no cover
            pass
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:  # pragma: no cover
            self.process.kill()
        for stream in (self.process.stdout, self.process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except OSError:  # pragma: no cover
                    pass

    def _drain(self, source: Any, is_stderr: bool) -> None:
        for raw in source:
            text = raw.decode("utf-8", "replace")
            if is_stderr:
                self.stderr_lines.append(text)
                continue
            try:
                message = json.loads(text)
            except json.JSONDecodeError:
                # Anything on stdout that is not a protocol message is a bug.
                self.bad_lines.append(text)
                continue
            self.messages.put(message)

    def write_raw(self, text: str) -> None:
        """Write a raw line (a malformed request, for the crash test)."""
        assert self.process is not None and self.process.stdin is not None
        self.process.stdin.write((text + "\n").encode("utf-8"))
        self.process.stdin.flush()

    def request(self, method: str, params: Mapping[str, Any] | None = None,
                request_id: int = 1) -> dict[str, Any]:
        """Send a request and read its answer."""
        message: dict[str, Any] = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = dict(params)
        self.write_raw(json.dumps(message))
        return self.read()

    def read(self, timeout: float = 20.0) -> dict[str, Any]:
        """Read the next protocol message (and fail if it does not come)."""
        try:
            return self.messages.get(timeout=timeout)
        except queue.Empty:
            raise AssertionError(f"the server did not answer within {timeout}s")

    def notify(self, method: str, params: Mapping[str, Any] | None = None) -> None:
        """Send a notification (which is never answered)."""
        message: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = dict(params)
        self.write_raw(json.dumps(message))


class ServerOverPipeTest(unittest.TestCase):
    """The server as a real process, spoken to over a pipe."""

    def test_initialize_answers_with_the_protocol_version(self) -> None:
        with ServerProcess() as server:
            response = server.request("initialize", {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "test", "version": "1"},
            })
        result = response["result"]
        self.assertEqual(result["protocolVersion"], "2025-11-25")
        self.assertIn("tools", result["capabilities"])
        self.assertEqual(result["serverInfo"]["name"], "cs2-scenario")

    def test_the_tools_are_listed_with_a_valid_schema(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            response = server.request("tools/list", request_id=2)
        tools = response["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], list(EXPECTED_TOOLS))
        for tool in tools:
            self.assertIsInstance(tool["description"], str)
            schema = tool["inputSchema"]
            self.assertEqual(schema["type"], "object")
            self.assertIs(schema["additionalProperties"], False)
            json.dumps(schema)

    def test_wrong_arguments_are_an_error_and_not_a_crash(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            response = server.request("tools/call", {
                "name": "cs2_exec", "arguments": {"line": 5},
            }, request_id=2)
            # The server is still alive: the next call is answered too.
            following = server.request("tools/list", request_id=3)
        self.assertTrue(response["result"]["isError"])
        self.assertIn("invalid arguments", response["result"]["content"][0]["text"])
        self.assertIn("tools", following["result"])

    def test_a_tool_that_needs_the_game_says_the_game_is_not_running(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            response = server.request("tools/call", {
                "name": "cs2_ping", "arguments": {},
            }, request_id=2)
        result = response["result"]
        self.assertTrue(result["isError"])
        text = result["content"][0]["text"]
        self.assertIn("the game is not running", text)
        self.assertIn("schtasks /run /tn CS2Launch", text)
        self.assertNotIn("Traceback", text)

    def test_an_unknown_tool_is_an_error(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            response = server.request("tools/call", {
                "name": "cs2_nope", "arguments": {},
            }, request_id=2)
        self.assertIn("error", response)
        self.assertEqual(response["error"]["code"], mcp_server.INVALID_PARAMS)

    def test_an_unknown_method_is_an_error(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            response = server.request("nope/nope", request_id=2)
        self.assertEqual(response["error"]["code"], mcp_server.METHOD_NOT_FOUND)

    def test_a_malformed_line_does_not_kill_the_server(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            server.write_raw("this is not json")
            response = server.read()
            self.assertEqual(response["error"]["code"], mcp_server.PARSE_ERROR)
            # The server keeps serving.
            following = server.request("tools/list", request_id=3)
        self.assertIn("tools", following["result"])

    def test_a_notification_is_not_answered(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            server.notify("notifications/initialized")
            response = server.request("tools/list", request_id=2)
        self.assertIn("tools", response["result"])

    def test_stdout_carries_nothing_but_the_protocol(self) -> None:
        with ServerProcess() as server:
            server.request("initialize", {"protocolVersion": "2025-11-25"})
            # The tool fails (the game is not running) and the server answers.
            server.request("tools/call", {"name": "cs2_ping", "arguments": {}},
                           request_id=2)
        self.assertEqual(server.bad_lines, [])

    def test_a_stray_print_goes_to_stderr_and_not_to_the_protocol(self) -> None:
        # stdout is the protocol channel: the server diverts it to stderr, so
        # the library's own prints cannot corrupt a frame.
        root = str(pathlib.Path(SERVER).resolve().parents[2])
        script = (
            "import sys; sys.path.insert(0, %r);"
            "from scenario.mcp.server import _protocol_stream;"
            "stream, diverted = _protocol_stream();"
            "print('a stray print');"
            "stream.write('PROTOCOL\\n'); stream.flush()"
            % root
        )
        finished = subprocess.run([sys.executable, "-c", script],
                                  capture_output=True, text=True, timeout=60)
        self.assertEqual(finished.stdout, "PROTOCOL\n")
        self.assertIn("a stray print", finished.stderr)

    def test_the_server_runs_from_any_working_directory(self) -> None:
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            with ServerProcess(cwd=directory) as server:
                response = server.request("initialize", {"protocolVersion": "2025-11-25"})
        self.assertIn("result", response)


# --- a payload that never answers -----------------------------------------
# The failure the deadline exists for: the pipe is there, the request
# arrives, and the answer never comes.  The fake payload is a **real**
# Win32 named pipe (the client's whole surface is the pipe), so the test
# covers the blocking read and the release of the channel — not a mock of it.
PIPE_ACCESS_DUPLEX = 0x00000003
PIPE_TYPE_BYTE = 0x00000000
PIPE_READMODE_BYTE = 0x00000000
PIPE_WAIT = 0x00000000
PIPE_UNLIMITED_INSTANCES = 255
ERROR_PIPE_CONNECTED = 535
INVALID_HANDLE_VALUE = -1


class SilentPayload:
    """A named pipe that accepts a client and never answers it.

    Every client gets a fresh instance (the pipe is created again after a
    client hangs up), so a client that gave the channel up can reconnect —
    which is exactly what the deadline has to make possible.
    """

    def __init__(self) -> None:
        self.name = (rf"\\.\pipe\cs2-internal-control-silent-{os.getpid()}"
                     f"-{threading.get_ident()}")
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, name="silent-payload",
                                        daemon=True)

    def __enter__(self) -> "SilentPayload":
        self._thread.start()
        return self

    def __exit__(self, *_exc: object) -> None:
        self._stop.set()

    def _serve(self) -> None:
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateNamedPipeW.argtypes = [
            wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
            wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
        kernel32.CreateNamedPipeW.restype = wintypes.HANDLE
        kernel32.ConnectNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
        kernel32.ConnectNamedPipe.restype = wintypes.BOOL
        kernel32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID,
                                      wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                                      wintypes.LPVOID]
        kernel32.ReadFile.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.DisconnectNamedPipe.argtypes = [wintypes.HANDLE]

        while not self._stop.is_set():
            handle = kernel32.CreateNamedPipeW(
                self.name, PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, None)
            if handle == INVALID_HANDLE_VALUE:
                return
            # The **same** instance serves every client, exactly like the
            # payload's channel (`Channel::accept` / `Channel::disconnect`):
            # a client that hangs up is disconnected and the instance waits
            # for the next one.
            while not self._stop.is_set():
                connected = kernel32.ConnectNamedPipe(handle, None)
                if (not connected
                        and ctypes.get_last_error() != ERROR_PIPE_CONNECTED):
                    kernel32.CloseHandle(handle)
                    return
                # Read the request (so the client's write completes) and
                # **never answer**: this is the payload that wedges its client.
                while not self._stop.is_set():
                    buffer = ctypes.create_string_buffer(4096)
                    read = wintypes.DWORD(0)
                    ok = kernel32.ReadFile(handle, buffer, 4096,
                                           ctypes.byref(read), None)
                    if not ok or read.value == 0:
                        break  # the client hung up
                kernel32.DisconnectNamedPipe(handle)


@unittest.skipUnless(os.name == "nt", "the named pipe is a Windows thing")
class SilentPayloadTest(unittest.TestCase):
    """A payload that never answers must not wedge the server.

    The server reads one request at a time, so a tool call that never
    returns blocks every later request — and the payload answers a command
    *inline* on its one service thread, so this is a real possibility, not a
    theoretical one.
    """

    def test_the_call_times_out_and_the_server_keeps_serving(self) -> None:
        with SilentPayload() as payload:
            environment = {
                "CS2_CONTROL_PIPE": payload.name,
                "CS2_CONTROL_CONNECT_TIMEOUT": "5",
                "CS2_CONTROL_REQUEST_TIMEOUT": "2",
            }
            with ServerProcess(environment=environment) as server:
                server.request("initialize", {"protocolVersion": "2025-11-25"})
                started = time.monotonic()
                response = server.request("tools/call", {
                    "name": "cs2_ping", "arguments": {},
                }, request_id=2)
                elapsed = time.monotonic() - started
                # The call must **fail** (with the timeout), not hang.
                self.assertLess(elapsed, 30.0)
                result = response["result"]
                self.assertTrue(result["isError"])
                self.assertIn("did not answer", result["content"][0]["text"])
                # The server is still alive: the next request is answered.
                following = server.request("tools/list", request_id=3)
                self.assertIn("tools", following["result"])

    def test_the_next_call_reconnects_after_the_timeout(self) -> None:
        """The channel was released, so the next call may connect again."""
        with SilentPayload() as payload:
            environment = {
                "CS2_CONTROL_PIPE": payload.name,
                "CS2_CONTROL_CONNECT_TIMEOUT": "5",
                "CS2_CONTROL_REQUEST_TIMEOUT": "2",
            }
            with ServerProcess(environment=environment) as server:
                server.request("initialize", {"protocolVersion": "2025-11-25"})
                server.request("tools/call", {"name": "cs2_ping", "arguments": {}},
                               request_id=2)
                # The second call connects again and times out again — it is
                # *not* "the game is not running" (the pipe is there).
                response = server.request("tools/call", {
                    "name": "cs2_ping", "arguments": {},
                }, request_id=3)
                text = response["result"]["content"][0]["text"]
                self.assertIn("did not answer", text)
                self.assertNotIn("the game is not running", text)


# --- the real MCP client ---------------------------------------------------
try:  # pragma: no cover - the package is a test-time convenience only
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client

    MCP_AVAILABLE = True
except ImportError:  # pragma: no cover
    MCP_AVAILABLE = False


@unittest.skipUnless(MCP_AVAILABLE, "the mcp package is not installed")
class McpClientTest(unittest.IsolatedAsyncioTestCase):
    """The server is protocol-correct, proven with the reference client."""

    async def test_the_reference_client_can_drive_the_server(self) -> None:
        environment = dict(os.environ)
        environment["CS2_CONTROL_PIPE"] = MISSING_PIPE
        environment["CS2_CONTROL_CONNECT_TIMEOUT"] = "0.5"
        parameters = StdioServerParameters(
            command=sys.executable, args=[str(SERVER)], env=environment)
        async with stdio_client(parameters) as (read, write):
            async with ClientSession(read, write) as session:
                result = await session.initialize()
                self.assertEqual(result.server_info.name, "cs2-scenario")
                self.assertIn(result.protocol_version, mcp_server.PROTOCOL_VERSIONS)
                tools = await session.list_tools()
                names = {tool.name for tool in tools.tools}
                self.assertEqual(names, set(EXPECTED_TOOLS))
                outcome = await session.call_tool("cs2_ping", {})
                self.assertTrue(outcome.is_error)
                # The content block is a union of the MCP content kinds; the
                # tool error is a text block.
                text = getattr(outcome.content[0], "text", "")
                self.assertIn("the game is not running", text)


# --- the runtime dependency rule -------------------------------------------
class RuntimeDependencyTest(unittest.TestCase):
    """The server is standard library only."""

    def test_the_mcp_package_imports_nothing_outside_the_standard_library(self) -> None:
        allowed = set(sys.stdlib_module_names) | {"scenario"}
        for name in ("schema.py", "session.py", "tools.py", "server.py"):
            path = pathlib.Path(mcp_tools.__file__).parent / name
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    for alias in node.names:
                        self.assertIn(alias.name.split(".")[0], allowed,
                                      f"{name}: {alias.name}")
                elif isinstance(node, ast.ImportFrom):
                    if node.level:
                        continue  # a relative import: the package itself
                    self.assertIn((node.module or "").split(".")[0], allowed,
                                  f"{name}: {node.module}")


if __name__ == "__main__":
    unittest.main()
