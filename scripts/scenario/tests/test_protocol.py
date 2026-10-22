"""Unit tests for the typed wire contract (`scripts/scenario/protocol.py`).

The types are the client's half of the contract with the payload; the validator
is the other half (the payload is a separately built binary, so a version skew
must fail loudly at the boundary). Both are testable without CS2 running, so
they are tested here with synthetic answers:

    python -m unittest discover -s scripts/scenario/tests
    # or
    python scripts/scenario/tests/test_protocol.py
"""

from __future__ import annotations

import pathlib
import re
import sys
import unittest
from typing import Any, NotRequired, get_args, get_origin, get_type_hints

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import protocol  # noqa: E402
from scenario.control import BUTTONS, Control, console_button  # noqa: E402
from scenario.protocol import (  # noqa: E402
    ButtonName,
    CONSOLE_BUTTONS,
    ControlError,
    ControlTransport,
    ProtocolError,
    SettingsValue,
    UsercmdButtonName,
    WaitExpr,
    validate_ping,
    validate_snapshot,
)
from scenario.session import Game  # noqa: E402

#: The repository's root — the payload's own source lives under it.
REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

#: The payload's control service: the source of the answers the client's
#: types describe (``Service::do_aim`` and ``Service::do_rage``).
SERVICE = REPO_ROOT / "payload" / "control" / "src" / "service.cpp"


def player(**overrides: Any) -> dict[str, Any]:
    """A player snapshot as the payload writes it."""
    raw: dict[str, Any] = {
        "index": 3,
        "name": "bot",
        "weapon": "weapon_ak47",
        "team": 3,
        "connected": 0,
        "health": 100,
        "max_health": 100,
        "armor": 0,
        "life_state": 0,
        "on_ground": True,
        "flags": 1,
        "clip": 30,
        "reserve": 90,
        "ping": 12,
        "money": 800,
        "flash": 0.0,
        "alive": True,
        "is_local": False,
        "helmet": False,
        "defuser": False,
        "scoped": False,
        "defusing": False,
        "in_buy_zone": True,
        "has_bomb": False,
        "origin": {"x": 1.0, "y": 2.0, "z": 3.0},
        "eye_origin": {"x": 1.0, "y": 2.5, "z": 3.0},
        "velocity": {"x": 0.0, "y": 0.0, "z": 0.0},
        "eye_angles": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0},
        "distance": 12.5,
        "simulation_time": 30.25,
        "tick_base": 1234,
    }
    raw.update(overrides)
    return raw


def snapshot(**overrides: Any) -> dict[str, Any]:
    """A ``state`` snapshot as the payload writes it."""
    raw: dict[str, Any] = {
        "valid": True,
        "in_map": True,
        "local_pawn": 0x1234,
        "level": {
            "up": True,
            "proxy": "0x7ff6a1b2c3d4",
            "rules_global": True,
            "identities": 726,
            "local_connected": 0,
        },
        "rules": {
            "loaded": True,
            "match_started": True,
            "freeze": False,
            "warmup": False,
            "game_restart": False,
            "bomb_planted": False,
            "bomb_dropped": False,
            "t_cant_buy": False,
            "ct_cant_buy": False,
            "game_phase": 2,
            "round_time": 115,
            "freeze_time": 0,
            "round_win_status": 0,
            "round_win_reason": 0,
            "total_rounds_played": 3,
            "match_start_time": 30.0,
            "round_start_time": 12.5,
            "restart_round_time": 0.0,
            "game_start_time": 30.0,
            "warmup_period_start": 0.0,
            "time_until_next_phase": 0.0,
        },
        "local": player(is_local=True),
        "players": [player()],
    }
    raw.update(overrides)
    return raw


def ping(**overrides: Any) -> dict[str, Any]:
    """A ``ping`` answer as the payload writes it."""
    raw: dict[str, Any] = {
        "pong": True,
        "pid": 4242,
        "uptime_ms": 5000,
        "exec_ready": True,
        "events_ready": True,
        "events_subscribed": 6,
        "events_dispatch_calls": 128,
        "input_ready": True,
        "input_hooked": True,
        "input_backend": "console",
        "state_ready": True,
    }
    raw.update(overrides)
    return raw


class _FixedControl(Control):
    """A control whose wire answer is fixed (no pipe, no game)."""

    def __init__(self, answer: Any) -> None:
        super().__init__()
        self.answer = answer
        self.calls: list[tuple[str, Any]] = []

    def request(self, cmd: str, args: Any = None, *,
                timeout: float | None = None) -> Any:
        self.calls.append((cmd, args))
        return self.answer


class TestSnapshotValidator(unittest.TestCase):
    def test_accepts_a_well_formed_snapshot(self) -> None:
        """The payload's own shape passes."""
        raw = snapshot()
        self.assertIs(validate_snapshot(raw), raw)

    def test_accepts_a_null_local_player(self) -> None:
        """No local player is a JSON ``null``, not a missing key."""
        raw = snapshot(local=None)
        self.assertIs(validate_snapshot(raw), raw)

    def test_accepts_whole_numbers_for_float_fields(self) -> None:
        """The payload prints a whole double as an integer (``json.cpp``)."""
        raw = snapshot(local=player(origin={"x": 0, "y": 0, "z": 0}))
        self.assertIs(validate_snapshot(raw), raw)

    def test_rejects_a_missing_required_key(self) -> None:
        """An older payload (one that omits a field) must fail loudly."""
        raw = snapshot()
        del raw["local"]["health"]
        with self.assertRaises(ProtocolError) as raised:
            validate_snapshot(raw)
        self.assertIn("health", str(raised.exception))

    def test_rejects_a_missing_top_level_key(self) -> None:
        raw = snapshot()
        del raw["rules"]
        with self.assertRaises(ProtocolError) as raised:
            validate_snapshot(raw)
        self.assertIn("rules", str(raised.exception))

    def test_rejects_a_mistyped_value(self) -> None:
        """``health`` as a string is a skew, not a number."""
        raw = snapshot()
        raw["players"][0]["health"] = "100"
        with self.assertRaises(ProtocolError) as raised:
            validate_snapshot(raw)
        message = str(raised.exception)
        self.assertIn("health", message)
        self.assertIn("string", message)

    def test_rejects_a_mistyped_string(self) -> None:
        """A name is a string; a number there is a skew."""
        raw = snapshot()
        raw["players"][0]["name"] = 5
        with self.assertRaises(ProtocolError) as raised:
            validate_snapshot(raw)
        message = str(raised.exception)
        self.assertIn("name", message)
        self.assertIn("string", message)

    def test_rejects_a_bool_where_an_int_is_expected(self) -> None:
        """A bool is an int in Python, but not on the wire."""
        raw = snapshot()
        raw["players"][0]["tick_base"] = True
        with self.assertRaises(ProtocolError) as raised:
            validate_snapshot(raw)
        self.assertIn("tick_base", str(raised.exception))

    def test_accepts_extra_keys(self) -> None:
        """A newer payload may answer with fields this client does not know."""
        raw = snapshot()
        raw["future_block"] = {"anything": 1}
        raw["local"]["future_field"] = 7
        self.assertIs(validate_snapshot(raw), raw)

    def test_rejects_a_non_object(self) -> None:
        with self.assertRaises(ProtocolError):
            validate_snapshot(None)  # type: ignore[arg-type]


class TestPingValidator(unittest.TestCase):
    def test_accepts_a_well_formed_ping(self) -> None:
        raw = ping()
        self.assertIs(validate_ping(raw), raw)

    def test_accepts_the_command_surface_version(self) -> None:
        """The version is optional (an older payload does not know it)."""
        raw = ping(protocol=3)
        self.assertIs(validate_ping(raw), raw)

    def test_rejects_a_missing_required_key(self) -> None:
        raw = ping()
        del raw["uptime_ms"]
        with self.assertRaises(ProtocolError) as raised:
            validate_ping(raw)
        self.assertIn("uptime_ms", str(raised.exception))

    def test_rejects_a_mistyped_value(self) -> None:
        raw = ping()
        raw["pong"] = "yes"
        with self.assertRaises(ProtocolError) as raised:
            validate_ping(raw)
        message = str(raised.exception)
        self.assertIn("pong", message)
        self.assertIn("bool", message)

    def test_accepts_extra_keys(self) -> None:
        raw = ping(future_field=1)
        self.assertIs(validate_ping(raw), raw)


class TestValidatorWiring(unittest.TestCase):
    """The validator has to run where the payload's answer is read."""

    def test_state_validates_the_answer(self) -> None:
        control = _FixedControl(snapshot(local=player(health="100")))
        with self.assertRaises(ProtocolError) as raised:
            control.state()
        self.assertIn("health", str(raised.exception))
        self.assertEqual(control.calls, [("state", None)])

    def test_state_returns_the_answer_when_it_is_well_formed(self) -> None:
        raw = snapshot()
        control = _FixedControl(raw)
        self.assertIs(control.state(), raw)

    def test_ping_validates_the_answer(self) -> None:
        control = _FixedControl(ping(exec_ready="yes"))
        with self.assertRaises(ProtocolError) as raised:
            control.ping()
        self.assertIn("exec_ready", str(raised.exception))


class TestTransport(unittest.TestCase):
    def test_control_satisfies_the_transport_protocol(self) -> None:
        """``Control`` is a ``ControlTransport`` (the runtime half of the check)."""
        control: ControlTransport = Control()
        self.assertIsInstance(control, ControlTransport)

    def test_the_game_facade_satisfies_the_transport_protocol(self) -> None:
        """``Game`` is a ``ControlTransport`` too — and this is the *static* check.

        A runtime ``isinstance`` against a ``runtime_checkable`` protocol only
        proves that the *attributes* are there, never that the signatures
        match.  The assignment below is what mypy checks: it compares the two
        signatures member by member, so a ``Game.look`` without the
        ``timeout`` parameter (or a narrowed ``event_args``) is a type error
        here rather than a surprise at runtime.
        """
        game: ControlTransport = Game(Control())
        self.assertIsInstance(game, ControlTransport)

    def test_the_protocol_covers_the_command_surface(self) -> None:
        """Every member of the protocol is a method of ``Control``."""
        control = Control()
        for member in ("request", "state", "events", "wait_for", "exec", "press",
                       "release", "move", "look"):
            with self.subTest(member=member):
                self.assertTrue(callable(getattr(control, member, None)))


class TestButtonNames(unittest.TestCase):
    def test_the_buttons_table_only_holds_known_names(self) -> None:
        """A typo in the table is a type error — and this catches it at runtime."""
        known = set(get_args(ButtonName))
        for alias, button in BUTTONS.items():
            with self.subTest(alias=alias):
                self.assertIn(button, known)

    def test_the_known_names_cover_the_table(self) -> None:
        """The table's aliases are the ones the runner documents."""
        self.assertEqual(BUTTONS["w"], "forward")
        self.assertEqual(BUTTONS["space"], "jump")
        self.assertEqual(BUTTONS["mouse1"], "attack")

    def test_every_alias_is_a_known_name(self) -> None:
        """The runner's own aliases are part of the advertised set.

        The tool's schema is built from these names, so an alias that is not
        in the set would be *refused* at the boundary — ``cs2_press(key="up")``
        is legal and must stay legal.
        """
        known = set(get_args(ButtonName))
        for alias in BUTTONS:
            with self.subTest(alias=alias):
                self.assertIn(alias, known)

    def test_the_typed_names_agree_with_the_console_table(self) -> None:
        """Every console name is an engine action; the usercmd-only ones are not.

        The typed set used to advertise ``moveleft`` / ``moveright`` / ``speed``
        / ``zoom``, which CS2's console does not know: the strings do not exist
        in ``client.dll``, so the console path issued an unknown command.
        """
        known = set(get_args(ButtonName))
        for alias, button in BUTTONS.items():
            with self.subTest(alias=alias):
                self.assertIn(button, known)
                self.assertIn(button, CONSOLE_BUTTONS)
        for name in get_args(UsercmdButtonName):
            with self.subTest(name=name):
                self.assertNotIn(name, CONSOLE_BUTTONS)
                self.assertNotIn(name, known)

    def test_the_console_path_rejects_a_usercmd_only_name(self) -> None:
        """A name the engine has no action for is an error, not a bad command."""
        self.assertEqual(console_button("w"), "forward")
        self.assertEqual(console_button("up"), "turnup")
        for name in get_args(UsercmdButtonName):
            with self.subTest(name=name):
                with self.assertRaises(ControlError):
                    console_button(name)

    def test_the_console_path_rejects_a_typo(self) -> None:
        """A typo is refused — it must not become a ``+<garbage>`` command.

        The engine answers an unknown command with an error nobody sees, so
        the command would be a silent no-op: the button would simply not be
        pressed.
        """
        for typo in ("spcae", "totally-not-a-button", "moveleft"):
            with self.subTest(typo=typo):
                with self.assertRaises(ControlError):
                    console_button(typo)


class TestSettingsValue(unittest.TestCase):
    def test_the_value_kinds_are_the_payloads_ones(self) -> None:
        """A flag takes a **bool** (the payload's ``write_field`` reads it)."""
        kinds = set(get_args(SettingsValue))
        self.assertIn(bool, kinds)
        self.assertIn(int, kinds)
        self.assertIn(float, kinds)
        self.assertIn(str, kinds)
        self.assertIn(list[int], kinds)


class TestPredicateGrammar(unittest.TestCase):
    def test_the_expression_has_the_servers_keys(self) -> None:
        """The server's grammar is 11 kinds; an unknown key is an error there."""
        self.assertEqual(
            set(WaitExpr.__annotations__),
            {"in_map", "local_alive", "on_ground", "playable", "players_gte",
             "alive_gte", "local_hp_gte", "team", "all", "any", "not"},
        )

    def test_the_expression_is_recursive(self) -> None:
        """``all``/``any``/``not`` nest, so the type has to be recursive."""
        hints = get_type_hints(WaitExpr)
        self.assertIn("WaitExpr", str(hints["all"]))
        self.assertIs(hints["not"], WaitExpr)


# --- the payload's published keys -------------------------------------------
def _function_body(text: str, signature: str) -> str:
    """The body of a C++ function, found by counting the braces."""
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError(f"the body of {signature!r} is never closed")


def _block_after(text: str, marker: str) -> str:
    """The ``{ ... }`` block that follows a marker, found by brace counting."""
    start = text.index(marker) + len(marker)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError(f"the block after {marker!r} is never closed")


def _published(body: str, variable: str) -> set[str]:
    """The keys a JSON object is given, by the ``set("name", ...)`` calls.

    The payload writes its answers with ``Json::set`` — ``payload.set("key",
    ...)`` for the answer itself, ``perf.set(...)`` for the instrumentation
    block inside it — so the keys it publishes are **in the source**.
    """
    pattern = re.compile(rf'(?<![A-Za-z0-9_]){variable}\.set\(\s*"([^"]+)"')
    return {match.group(1) for match in pattern.finditer(body)}


def _optional_keys(shape: type) -> set[str]:
    """The keys a ``TypedDict`` declares optional (``NotRequired``).

    Under ``from __future__ import annotations`` the class attribute
    ``__required_keys__`` still claims an optional key, so the **resolved**
    annotations are consulted — the same rule the module's own validator
    follows (``protocol._required_keys``).
    """
    hints = get_type_hints(shape, include_extras=True)
    return {key for key, hint in hints.items() if get_origin(hint) is NotRequired}


@unittest.skipUnless(SERVICE.is_file(), "the payload's source is not in this tree")
class TestThePublishedKeys(unittest.TestCase):
    """The types must describe the answers the payload actually sends.

    A ``TypedDict`` that promises a key the payload does not send is a lie,
    and the type gate cannot catch it: the reader would look for a key that
    is not there.  A ``TypedDict`` that is missing a key the payload *does*
    send is the mirror image — the reader would not know the key exists.

    So the two answers that carry the Monte-Carlo objective are read from the
    payload's **own source** and compared with the client's types, key by
    key: a key added on one side and not the other fails this suite.
    """

    #: The two answer bodies, read once for the whole class.
    aim: str
    rage: str

    @classmethod
    def setUpClass(cls) -> None:
        text = SERVICE.read_text(encoding="utf-8")
        cls.aim = _function_body(text, "void Service::do_aim(")
        cls.rage = _function_body(text, "void Service::do_rage(")

    def test_the_rage_answer_carries_exactly_the_payloads_keys(self) -> None:
        """``RageResult`` is the ``rage`` command's answer, key for key."""
        published = _published(self.rage, "payload")
        self.assertEqual(set(protocol.RageResult.__annotations__), published)

    def test_the_instrumentation_block_carries_exactly_the_payloads_keys(self) -> None:
        """The ``perf`` sub-object is its own shape."""
        published = _published(self.rage, "perf")
        self.assertEqual(set(protocol.RagePerfDict.__annotations__), published)

    def test_the_aim_answer_carries_exactly_the_payloads_keys(self) -> None:
        """``AimResult`` is the ``aim`` command's answer, key for key."""
        published = _published(self.aim, "payload")
        self.assertEqual(set(protocol.AimResult.__annotations__), published)

    def test_the_keys_the_payload_may_omit_are_optional(self) -> None:
        """The Monte-Carlo's numbers are sent only when it produced a verdict.

        The payload publishes them inside
        ``if (result.monte_carlo.has_value())`` — under the other four
        objectives, and when the solver refused, they are **absent**.  So
        they are the optional keys of the type, and the rest are the
        required ones: a type that promised a key the payload does not send
        would be a lie the type gate cannot catch.

        ``mc_samples`` is published **both** inside the block and outside it
        (the block overwrites it with the verdict's own count), so it is
        always there — and it stays required.
        """
        conditional_block = _block_after(
            self.aim, "if (result.monte_carlo.has_value())")
        conditional = _published(conditional_block, "payload")
        # The keys published **outside** the block — ``mc_samples`` is in both
        # (the block overwrites it with the verdict's own count), so the
        # block is removed before the rest is read.
        always = _published(self.aim.replace(conditional_block, "", 1), "payload")
        optional = _optional_keys(protocol.AimResult)
        self.assertEqual(optional, conditional - always)
        self.assertEqual(set(protocol.AimResult.__annotations__) - optional, always)

    def test_the_rage_answer_may_omit_nothing(self) -> None:
        """The ``rage`` command publishes every key it declares, always.

        ``RageResult`` is ``total=False`` (the block grows), so the keys are
        optional by construction — but the payload sends **all** of them on
        every call, and a key the payload stopped sending would be a silent
        loss the type would not report.
        """
        published = _published(self.rage, "payload")
        self.assertEqual(set(protocol.RageResult.__annotations__) - published, set())


if __name__ == "__main__":
    unittest.main(verbosity=2)
