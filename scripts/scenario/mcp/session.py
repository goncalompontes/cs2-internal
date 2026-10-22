"""The lazy pipe session: the MCP server's handle on the running game.

The MCP server is a **long-lived process**; the game is not.  The server may
sit idle for minutes between two tool calls, the game may be restarted under
it, and the very first call may happen before anybody launched CS2.  So the
connection is:

* **lazy** — nothing is opened until a tool actually needs the game;
* **cached** — the channel is opened once and reused (the payload's pipe
  accepts one client at a time);
* **bounded** — a connect attempt gives up after a few seconds instead of
  hanging the agent, and a **call** gives up after its own deadline;
* **self-healing** — a dropped pipe is detected and the next call reconnects
  (the :class:`~scenario.control.Control` class has no reconnect of its own,
  so the retry lives here).

The failure mode matters as much as the happy path: when the game is not
running the tool must answer with an **actionable message**, not a hang and
not a traceback.  :func:`not_running_message` is that message.

The **deadline** is what keeps a stuck payload from wedging the server.  The
server reads one request at a time, so a tool call that never returns blocks
every later request — and the payload answers a command *inline* on its one
service thread, so a wedged payload (or a slow ``do_heatmap``) is a real
possibility, not a theoretical one.  Two deadlines bound it:

* the **wire deadline** (inside the control client: the payload did not
  answer this command within N seconds) — the inner bound, and the one that
  fires for a payload that never answers;
* the **call deadline** (here: the tool call did not finish within N
  seconds) — the outer bound, for a call that blocks somewhere else.

Both release the channel, so the next call reconnects instead of waiting on
a handle nobody will ever answer.

The server never kills the game: the pipe is closed on exit, the payload keeps
running and the next client (the next scenario, the next MCP session) can
reconnect.
"""

from __future__ import annotations

import os
import threading
from typing import Any, Callable, TypeVar, cast

from ..control import DEFAULT_SOCKET, Control, ControlError, ControlTimeout
from ..session import Game

__all__ = [
    "CALL_TIMEOUT_ENV",
    "DEFAULT_CALL_TIMEOUT",
    "DEFAULT_CONNECT_TIMEOUT",
    "GameNotRunning",
    "Session",
    "not_running_message",
]

T = TypeVar("T")

#: How long a connect attempt may take before the game is declared absent.
#: The pipe either exists or it does not: the wait is only there for the
#: window between "the payload was injected" and "the pipe is up".
DEFAULT_CONNECT_TIMEOUT = 5.0

#: How long one tool call may take before the server gives up on it.
#:
#: The wire deadline (the control client's) is the *inner* bound: a command
#: that is never answered fails there.  This is the *outer* bound, for a call
#: that blocks somewhere else — the server's read loop is single-threaded, so
#: a call that never returns would block every later request.
#:
#: It is generous on purpose: a tool may legitimately wait for the game (the
#: ``ensure`` tool drives the game into a screen and takes minutes, and a
#: scenario may set a bot match up).
DEFAULT_CALL_TIMEOUT = 600.0

#: The environment variable that overrides the connect timeout.  It exists so
#: a test (or an impatient operator) does not have to wait the full default
#: for the "the game is not running" answer.
CONNECT_TIMEOUT_ENV = "CS2_CONTROL_CONNECT_TIMEOUT"

#: The environment variable that overrides the call timeout.
CALL_TIMEOUT_ENV = "CS2_CONTROL_CALL_TIMEOUT"

#: The pipe the server talks to.  ``CS2_CONTROL_PIPE`` overrides it (the
#: control channel honours the same variable), which is what lets a test point
#: the server at a pipe that does not exist.
DEFAULT_PIPE_NAME = DEFAULT_SOCKET

#: The error texts that mean "the pipe dropped" rather than "the payload
#: rejected the request".  A rejected request must **not** trigger a
#: reconnect: the channel is alive, the answer is simply an error.
_DROP_MARKERS = ("control channel closed", "not connected")


class GameNotRunning(ControlError):
    """The game is not running (or the payload is not injected into it)."""


def not_running_message(pipe_name: str = DEFAULT_PIPE_NAME) -> str:
    """The message a tool answers with when the game is absent.

    It has to be *actionable*: the agent reading it must know the two things
    that are missing (the running game and the injected payload) and how to
    get them.
    """
    return (
        "the game is not running: the control channel is not connected "
        f"({pipe_name}). Is CS2 running? Launch it with "
        "`schtasks /run /tn CS2Launch` (the agent runs in session 0 without a "
        "GPU, so the game has to be started in the interactive session) and "
        "inject the payload (`meson compile -C build-payload`, then "
        "`build-injector/injector.exe cs2.exe build-payload/payload.dll "
        "--load-missing`). The game is never killed by this server."
    )


def _is_dropped(error: ControlError) -> bool:
    """Whether the error means "the pipe dropped" (and a reconnect may help)."""
    if isinstance(error, ControlTimeout):
        # The channel was released: the next call has to reconnect.  There is
        # no retry here — the payload is the thing that is late, and a retry
        # would only wait for it a second time.
        return False
    text = str(error)
    return any(marker in text for marker in _DROP_MARKERS)


def _timeout_from_env(name: str, default: float) -> float:
    """A positive float from the environment (the default when unset)."""
    raw = os.environ.get(name)
    if not raw:
        return default
    try:
        value = float(raw)
    except ValueError:
        return default
    return value if value > 0 else default


class Session:
    """The lazy, cached connection to the game's control channel."""

    def __init__(self, pipe_name: str | None = None, *,
                 connect_timeout: float | None = None,
                 call_timeout: float | None = None) -> None:
        self.pipe_name = (pipe_name
                          or os.environ.get("CS2_CONTROL_SOCKET")
                          or os.environ.get("CS2_CONTROL_PIPE")
                          or "")
        self.connect_timeout = (DEFAULT_CONNECT_TIMEOUT if connect_timeout is None
                                else connect_timeout)
        if connect_timeout is None:
            self.connect_timeout = _timeout_from_env(CONNECT_TIMEOUT_ENV,
                                                     DEFAULT_CONNECT_TIMEOUT)
        #: The deadline of one tool call (see :data:`DEFAULT_CALL_TIMEOUT`).
        self.call_timeout = (DEFAULT_CALL_TIMEOUT if call_timeout is None
                             else call_timeout)
        if call_timeout is None:
            self.call_timeout = _timeout_from_env(CALL_TIMEOUT_ENV,
                                                  DEFAULT_CALL_TIMEOUT)
        self._control: Control | None = None
        self._game: Game | None = None

    # --- the connection ----------------------------------------------------
    def control(self) -> Control:
        """The connected channel (opening it on the first call)."""
        if self._control is None:
            self._control = self._connect()
        return self._control

    def game(self) -> Game:
        """The session façade over the channel (the state machine's peer)."""
        if self._game is None:
            self._game = Game(self.control())
        return self._game

    def _connect(self) -> Control:
        """Open the pipe, or explain that the game is not there."""
        control = Control(self.pipe_name or None)
        try:
            control.connect(timeout=self.connect_timeout)
        except ControlError as error:
            raise GameNotRunning(not_running_message(self.pipe_name)) from error
        return control

    def reset(self) -> None:
        """Drop the cached connection; the next call reconnects.

        Used after a dropped pipe and after an injection (the payload opens
        the pipe when it starts, so the old handle is worthless).
        """
        control, self._control, self._game = self._control, None, None
        if control is not None:
            try:
                control.close()
            except OSError:  # pragma: no cover - a broken pipe on close
                pass

    def call(self, action: Callable[[Control], T], *,
             timeout: float | None = None) -> T:
        """Run ``action`` on the channel, reconnecting once if the pipe dropped.

        A *rejected* request (the payload answered with an error) is passed
        straight through: the channel is alive and the agent deserves the
        payload's own message.

        The call runs under the **call deadline** (:attr:`call_timeout`, or
        the ``timeout`` this call passes): the server's read loop is
        single-threaded, so a call that never returns would block every later
        request.  A call that runs out of time releases the channel (so the
        next call reconnects) and reports the timeout.
        """
        control = self.control()
        try:
            return self._bounded(action, control, timeout)
        except ControlTimeout:
            # The channel was released: drop the cached handle so the next
            # call reconnects.  There is no retry — the payload is the thing
            # that is late, and a retry would only wait for it again.
            self.reset()
            raise
        except ControlError as error:
            if not _is_dropped(error):
                raise
            self.reset()
            return self._bounded(action, self.control(), timeout)

    def run(self, action: Callable[[Game], T], *,
            timeout: float | None = None) -> T:
        """Run ``action`` on the session façade, reconnecting once if it dropped.

        The state machine and the scenarios work on the :class:`~scenario.session.Game`
        façade (not on the raw channel), so they get the same self-healing
        and the same deadline.
        """
        game = self.game()
        try:
            return self._bounded(action, game, timeout)
        except ControlTimeout:
            self.reset()
            raise
        except ControlError as error:
            if not _is_dropped(error):
                raise
            self.reset()
            return self._bounded(action, self.game(), timeout)

    def _bounded(self, action: Callable[[Any], T], target: Any,
                 timeout: float | None) -> T:
        """Run ``action`` under the call deadline.

        The action runs in a worker thread and the caller waits for it with a
        deadline: a tool call that blocks (a wedged payload, a scenario that
        sleeps) must not wedge the server's read loop.  On a timeout the
        channel is released, which unblocks a call that is stuck in the pipe's
        read.
        """
        budget = self.call_timeout if timeout is None else timeout
        outcome: dict[str, Any] = {}
        finished = threading.Event()

        def work() -> None:
            try:
                outcome["value"] = action(target)
            except BaseException as error:  # noqa: BLE001 - handed to the caller
                outcome["error"] = error
            finally:
                finished.set()

        worker = threading.Thread(target=work, name="cs2-mcp-call", daemon=True)
        worker.start()
        if not finished.wait(budget):
            # The call did not finish: release the channel (a call that is
            # stuck in the pipe's read unwinds) and report the timeout.
            self.reset()
            raise ControlError(
                f"the tool call did not finish within {budget:.1f}s "
                "(the control channel was released)")
        error = outcome.get("error")
        if error is not None:
            raise error
        return cast(T, outcome.get("value"))

    def close(self) -> None:
        """Close the channel — **without** stopping the payload or the game."""
        self.reset()
