"""Unit tests for the scenario registry and the tooling around it.

Two things live here:

* the **registry** (``scenario.scenarios.SCENARIOS``) — the names the runner
  accepts. A scenario module that is not in the registry is dead code, and a
  registered name that is not a scenario is a name the runner cannot run; both
  are silent failures today, so they are pinned here.
* the **tooling configuration** — ``pyproject.toml`` (the type checker's
  configuration) and ``requirements-dev.txt``. They are the gate's own
  inputs, so they are parsed and asserted like any other contract.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import importlib
import inspect
import pathlib
import subprocess
import sys
import tomllib
import unittest
from typing import Any, Callable, cast

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from scenario import scenarios  # noqa: E402
from scenario.scenarios import SCENARIOS  # noqa: E402

#: The repository root (the tooling configuration lives there).
REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

#: The runner that consumes the registry.
RUNNER = REPO_ROOT / "scripts" / "run-scenario.py"


class TestRegistry(unittest.TestCase):
    """The registry the runner looks the scenarios up in."""

    def test_the_registry_is_not_empty(self) -> None:
        self.assertGreaterEqual(len(SCENARIOS), 19)

    def test_every_name_maps_to_a_callable(self) -> None:
        for name, run in SCENARIOS.items():
            with self.subTest(scenario=name):
                self.assertTrue(callable(run))

    def test_every_scenario_takes_the_game_and_the_keywords(self) -> None:
        """The runner calls ``run(game, **kwargs)`` — the shape is a contract."""
        for name, run in SCENARIOS.items():
            with self.subTest(scenario=name):
                self.assertTrue(callable(run))
                signature = inspect.signature(cast(Callable[..., Any], run))
                parameters = list(signature.parameters.values())
                self.assertTrue(parameters, f"{name} takes no arguments")
                first = parameters[0]
                self.assertEqual(first.name, "game")
                self.assertIs(first.kind, inspect.Parameter.POSITIONAL_OR_KEYWORD)
                for parameter in parameters[1:]:
                    self.assertIs(parameter.kind, inspect.Parameter.KEYWORD_ONLY,
                                  f"{name}: {parameter.name} is not keyword-only")

    def test_every_scenario_module_is_registered(self) -> None:
        """A scenario file that is not in the registry is dead code."""
        directory = pathlib.Path(scenarios.__file__).parent
        modules = sorted(path.stem for path in directory.glob("*.py")
                         if path.stem != "__init__")
        self.assertGreaterEqual(len(modules), 19)
        registered = {f"{run.__module__}.{run.__qualname__}"
                      for run in SCENARIOS.values()}
        for name in modules:
            with self.subTest(module=name):
                module = importlib.import_module(f"scenario.scenarios.{name}")
                self.assertTrue(hasattr(module, "run"),
                                f"{name} has no run()")
                self.assertIn(f"scenario.scenarios.{name}.run", registered)

    def test_the_names_are_kebab_case(self) -> None:
        """The names are the command line's words."""
        for name in SCENARIOS:
            with self.subTest(scenario=name):
                self.assertEqual(name, name.lower())
                self.assertNotIn("_", name)
                self.assertNotIn(" ", name)

    def test_the_mcp_tool_lists_exactly_the_registry(self) -> None:
        """The ``cs2_run_scenario`` enum and the registry must not drift apart."""
        from scenario.mcp import tools

        tool = next(entry for entry in tools.TOOLS
                    if entry.name == "cs2_run_scenario")
        schema = tool.input_schema()
        self.assertEqual(set(schema["properties"]["name"]["enum"]), set(SCENARIOS))


class TestTooling(unittest.TestCase):
    """The gate's own configuration."""

    def test_the_pyproject_is_a_valid_file(self) -> None:
        """The configuration is TOML and it parses."""
        with open(REPO_ROOT / "pyproject.toml", "rb") as handle:
            config = tomllib.load(handle)
        self.assertIn("project", config)
        self.assertIn("tool", config)

    def test_the_package_targets_the_supported_interpreter(self) -> None:
        with open(REPO_ROOT / "pyproject.toml", "rb") as handle:
            config = tomllib.load(handle)
        self.assertEqual(config["project"]["requires-python"], ">=3.11")

    def test_the_package_has_no_runtime_dependencies(self) -> None:
        """The package is standard library only; mypy is a dev dependency."""
        with open(REPO_ROOT / "pyproject.toml", "rb") as handle:
            config = tomllib.load(handle)
        self.assertEqual(config["project"]["dependencies"], [])

    def test_the_type_checker_is_configured(self) -> None:
        with open(REPO_ROOT / "pyproject.toml", "rb") as handle:
            config = tomllib.load(handle)
        mypy = config["tool"]["mypy"]
        self.assertTrue(mypy["strict"])
        self.assertEqual(mypy["files"], ["scripts/scenario"])
        self.assertEqual(mypy["python_version"], "3.11")

    def test_the_requirements_pin_the_checker(self) -> None:
        """The gate needs a pinned checker: one that changes is not a gate."""
        text = (REPO_ROOT / "requirements-dev.txt").read_text(encoding="utf-8")
        self.assertIn("mypy==", text)

    def test_the_gate_script_exists_and_is_runnable(self) -> None:
        script = REPO_ROOT / "scripts" / "typecheck.py"
        self.assertTrue(script.is_file())
        finished = subprocess.run([sys.executable, str(script), "--help"],
                                  capture_output=True, text=True, timeout=120)
        self.assertEqual(finished.returncode, 0, finished.stderr)
        self.assertIn("typecheck", finished.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
