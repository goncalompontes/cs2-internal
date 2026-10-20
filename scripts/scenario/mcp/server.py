"""The MCP server: the scenario API over stdio (JSON-RPC 2.0).

The transport is the MCP stdio one: **one JSON object per line** on stdin and
stdout.  The methods are the ones an MCP client needs — ``initialize``,
``notifications/initialized`` (a notification, so it is never answered),
``tools/list``, ``tools/call`` and ``ping``.

Three things this module is careful about:

* **stdout is the protocol channel.**  Nothing else may ever be written to
  it.  The library below prints (the state machine's debug log, a scenario's
  progress lines), so the server *duplicates* the real stdout for the
  protocol and points ``sys.stdout`` at stderr: a stray ``print`` becomes a
  diagnostic instead of a corrupt frame.
* **A bad request is answered, not fatal.**  A malformed line, an unknown
  method, an unknown tool, a tool that raises — every one of them produces an
  error response (or a tool error) and the loop keeps going.
* **The game is never killed.**  The pipe is closed on the way out; the
  payload and the game keep running, and the next client can reconnect.

The implementation uses the **standard library only**: the ``mcp`` package may
happen to be installed on the machine, but the server must run on a bare
Python 3.11.
"""

from __future__ import annotations

import json
import os
import sys
import traceback
from pathlib import Path
from typing import Any, Mapping, TextIO

# The package root (`scripts/`), so `import scenario...` works from any
# working directory: the OpenCode config starts the server by absolute path.
_ROOT = Path(__file__).resolve().parents[2]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from scenario.control import ControlError  # noqa: E402
from scenario.mcp.schema import validate_arguments  # noqa: E402
from scenario.mcp.session import Session  # noqa: E402
from scenario.mcp.tools import TOOLS, tool_definitions  # noqa: E402
from scenario.session import SessionError  # noqa: E402

__all__ = [
    "PROTOCOL_VERSIONS",
    "SERVER_NAME",
    "Server",
    "main",
    "serve",
]

#: The MCP protocol versions this server speaks.  The client proposes one in
#: ``initialize``; a version that is not in this list is answered with the
#: newest one that is (the negotiation the reference implementation does).
PROTOCOL_VERSIONS: tuple[str, ...] = ("2024-11-05", "2025-03-26", "2025-06-18",
                                       "2025-11-25")

#: The name the client shows for this server.
SERVER_NAME = "cs2-scenario"

#: The server's own version (the MCP ``serverInfo`` block).
SERVER_VERSION = "1.0.0"

#: The JSON-RPC error codes (the ones the spec names).
PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
INTERNAL_ERROR = -32603

#: The text the client sees when it asks the server what it is for.
INSTRUCTIONS = (
    "The CS2 scenario/control API of the cs2-internal payload. cs2_ping, "
    "cs2_state, cs2_screen and cs2_events read the game; cs2_exec, cs2_look, "
    "cs2_move, cs2_press, cs2_release, cs2_aim, cs2_rage and cs2_wait_for "
    "drive it; cs2_ensure drives the game into a screen; cs2_settings_get / "
    "cs2_settings_set read and write the feature settings; cs2_run_scenario "
    "runs a built-in scenario. Every tool needs the game to be running: if it "
    "is not, the tool says so instead of failing silently. cs2_launch and "
    "cs2_inject are destructive and need confirm: true."
)


class RpcError(Exception):
    """An error that belongs in a JSON-RPC error response (not a tool error)."""

    def __init__(self, code: int, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


def _result(request_id: Any, result: Mapping[str, Any]) -> dict[str, Any]:
    """A JSON-RPC success response."""
    return {"jsonrpc": "2.0", "id": request_id, "result": dict(result)}


def _error(request_id: Any, code: int, message: str) -> dict[str, Any]:
    """A JSON-RPC error response."""
    return {"jsonrpc": "2.0", "id": request_id,
            "error": {"code": code, "message": message}}


def _tool_error(message: str) -> dict[str, Any]:
    """A tool result that reports a failure (``isError``).

    A tool failure is **not** a protocol failure: the agent has to see the
    message and correct itself, which is what the content block is for.
    """
    return {"content": [{"type": "text", "text": message}], "isError": True}


def _tool_result(value: Any) -> dict[str, Any]:
    """A tool result that reports success: the JSON of the value."""
    text = json.dumps(value, indent=2, sort_keys=True, default=str)
    return {"content": [{"type": "text", "text": text}], "isError": False}


def _message(error: BaseException) -> str:
    """The message of an exception (never a traceback)."""
    text = str(error).strip()
    return text if text else error.__class__.__name__


class Server:
    """The JSON-RPC dispatcher: the MCP methods over a pair of streams."""

    def __init__(self, session: Session | None = None) -> None:
        self.session = session if session is not None else Session()
        self.tools = {tool.name: tool for tool in TOOLS}
        #: The schema fallbacks the bridge reported (for the diagnostics).
        self.problems: list[str] = []
        #: Whether the client finished the handshake.
        self.initialized = False

    # --- the methods ------------------------------------------------------
    def initialize(self, params: Mapping[str, Any]) -> dict[str, Any]:
        """The handshake: the protocol version, the capabilities, who we are."""
        requested = params.get("protocolVersion")
        version = (requested if requested in PROTOCOL_VERSIONS
                   else PROTOCOL_VERSIONS[-1])
        return {
            "protocolVersion": version,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "instructions": INSTRUCTIONS,
        }

    def list_tools(self) -> dict[str, Any]:
        """Every tool with its generated schema."""
        self.problems = []
        return {"tools": tool_definitions(self.problems)}

    def call_tool(self, params: Mapping[str, Any]) -> dict[str, Any]:
        """Run one tool and answer with its result (or its error)."""
        name = params.get("name")
        if not isinstance(name, str):
            raise RpcError(INVALID_PARAMS, "the tool name must be a string")
        tool = self.tools.get(name)
        if tool is None:
            raise RpcError(INVALID_PARAMS,
                           f"unknown tool {name!r} (known: {sorted(self.tools)})")
        arguments = params.get("arguments")
        if arguments is None:
            arguments = {}
        if not isinstance(arguments, Mapping):
            raise RpcError(INVALID_PARAMS, "the tool arguments must be an object")

        # The schema is the contract: a bad argument is rejected here, before
        # the request reaches the game.
        complaints = validate_arguments(tool.input_schema(self.problems), arguments)
        if complaints:
            return _tool_error(f"invalid arguments for {name}: " + "; ".join(complaints))

        try:
            value = tool.handler(arguments, self.session)
        except (ControlError, SessionError) as error:
            # An expected operational failure (the game is not running, the
            # screen was not reached, the payload refused the request): the
            # message *is* the answer, and it is what the agent needs.
            return _tool_error(_message(error))
        except Exception as error:  # noqa: BLE001 - a tool must never crash the server
            # A bug: the traceback goes to the diagnostics, the agent gets the
            # message (never a traceback).
            traceback.print_exc(file=sys.stderr)
            return _tool_error(f"the tool failed: {_message(error)}")
        return _tool_result(value)

    # --- the dispatch -----------------------------------------------------
    def handle(self, message: Any) -> dict[str, Any] | None:
        """Handle one incoming message; ``None`` means "answer with silence"."""
        if not isinstance(message, Mapping):
            return _error(None, INVALID_REQUEST, "a request must be a JSON object")
        method = message.get("method")
        if not isinstance(method, str):
            return _error(message.get("id"), INVALID_REQUEST,
                          "a request must carry a 'method'")
        params = message.get("params")
        if params is None:
            params = {}
        if not isinstance(params, Mapping):
            return _error(message.get("id"), INVALID_PARAMS,
                          "'params' must be an object")

        # A notification is never answered — not even with an error.
        if "id" not in message or message.get("id") is None:
            if method == "notifications/initialized":
                self.initialized = True
            return None

        request_id = message.get("id")
        try:
            if method == "initialize":
                return _result(request_id, self.initialize(params))
            if method == "ping":
                return _result(request_id, {})
            if method == "tools/list":
                return _result(request_id, self.list_tools())
            if method == "tools/call":
                return _result(request_id, self.call_tool(params))
            if method == "resources/list":
                return _result(request_id, {"resources": []})
            if method == "prompts/list":
                return _result(request_id, {"prompts": []})
        except RpcError as error:
            return _error(request_id, error.code, error.message)
        except Exception as error:  # noqa: BLE001 - the server must not die
            traceback.print_exc(file=sys.stderr)
            return _error(request_id, INTERNAL_ERROR, _message(error))
        return _error(request_id, METHOD_NOT_FOUND, f"unknown method: {method!r}")


def serve(stream: TextIO, source: TextIO, server: Server | None = None) -> int:
    """The read loop: one JSON object per line in, one per line out."""
    server = server if server is not None else Server()
    while True:
        line = source.readline()
        if not line:
            break  # the client closed the pipe: the server is done
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except json.JSONDecodeError as error:
            _write(stream, _error(None, PARSE_ERROR, f"parse error: {error}"))
            continue
        response = server.handle(message)
        if response is not None:
            _write(stream, response)
    return 0


def _write(stream: TextIO, message: Mapping[str, Any]) -> None:
    """Write one protocol message (a single line) and flush it."""
    stream.write(json.dumps(message, separators=(",", ":")) + "\n")
    stream.flush()


def _protocol_stream() -> tuple[TextIO, bool]:
    """The stream the protocol is written to, and whether stdout was diverted.

    stdout is the protocol channel, so the *real* stdout is duplicated for
    the protocol and ``sys.stdout`` is pointed at stderr: every stray
    ``print`` in the library becomes a diagnostic instead of a corrupt frame.
    """
    try:
        sys.stdout.flush()
        handle = os.dup(sys.stdout.fileno())
    except (AttributeError, OSError, ValueError):  # pragma: no cover
        return sys.stdout, False
    stream = os.fdopen(handle, "w", encoding="utf-8", newline="\n")
    sys.stdout = sys.stderr
    return stream, True


def _reconfigure(stream: TextIO) -> None:
    """Read the protocol as UTF-8 (the console's own code page is not UTF-8)."""
    try:
        stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):  # pragma: no cover
        pass


def main(argv: list[str] | None = None) -> int:
    """The entry point: serve the protocol on stdin/stdout."""
    args = list(sys.argv[1:] if argv is None else argv)
    if args and args[0] in ("--list-tools", "-l"):
        # A convenience for a human (and for the tests): the catalogue with
        # every generated schema.
        print(json.dumps(tool_definitions(), indent=2, sort_keys=True))
        return 0
    if args and args[0] in ("--help", "-h"):
        print(__doc__)
        return 0

    stream, diverted = _protocol_stream()
    _reconfigure(sys.stdin)
    server = Server()
    try:
        return serve(stream, sys.stdin, server)
    except KeyboardInterrupt:  # pragma: no cover - the operator stops it
        return 0
    finally:
        # The pipe is closed; the game and the payload keep running.
        server.session.close()
        if diverted:
            try:
                stream.close()
            except OSError:  # pragma: no cover
                pass


if __name__ == "__main__":
    sys.exit(main())
