"""Built-in scenarios for ``scripts/run-scenario.py``.

Each scenario is a ``run(game, **kwargs) -> dict`` callable. The runner imports
this package and looks up the function by name.
"""

from . import join_bot_match, move_and_aim

SCENARIOS = {
    "join-bot-match": join_bot_match.run,
    "move-and-aim": move_and_aim.run,
}

__all__ = ["SCENARIOS"]
