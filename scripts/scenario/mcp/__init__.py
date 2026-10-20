"""The MCP server: the scenario/control API as tools an agent can call.

The package has four parts:

* :mod:`scenario.mcp.schema` — the ``TypedDict`` -> JSON Schema bridge (and the
  validator that keeps a bad argument from reaching the game);
* :mod:`scenario.mcp.session` — the lazy, cached pipe connection to the running
  game, with the actionable "the game is not running" error;
* :mod:`scenario.mcp.tools` — the tool catalogue (one tool per API area);
* :mod:`scenario.mcp.server` — the JSON-RPC/stdio server (the process the
  OpenCode config starts).

The server is **standard library only** and it is registered in the global
OpenCode config, so the tools appear in the Code Mode runtime as
``tools.cs2.*`` — the same way the REA tools do (``tools.rea.*``).
"""

from __future__ import annotations

from .schema import MAX_EXPANSION_DEPTH, schema_for, schema_for_hint, validate_arguments
from .session import GameNotRunning, Session, not_running_message
from .tools import TOOLS, Tool, tool_definitions

__all__ = [
    "MAX_EXPANSION_DEPTH",
    "TOOLS",
    "GameNotRunning",
    "Session",
    "Tool",
    "not_running_message",
    "schema_for",
    "schema_for_hint",
    "tool_definitions",
    "validate_arguments",
]
