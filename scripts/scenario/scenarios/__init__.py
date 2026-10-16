"""Built-in scenarios for ``scripts/run-scenario.py``.

Each scenario is a ``run(game, **kwargs) -> dict`` callable. The runner imports
this package and looks up the function by name.
"""

from . import (autostrafe, bhop, game_events, join_bot_match, move_and_aim, move_to,
               movement_probe, screens, status, strafe_probe, subtick_aim, subtick_move,
               subtick_shot, usercmd_move, aim_probe, aim_shot, rage)

SCENARIOS = {
    "join-bot-match": join_bot_match.run,
    "move-and-aim": move_and_aim.run,
    "game-events": game_events.run,
    "usercmd-move": usercmd_move.run,
    "bhop": bhop.run,
    "subtick-aim": subtick_aim.run,
    "subtick-shot": subtick_shot.run,
    "subtick-move": subtick_move.run,
    "movement-probe": movement_probe.run,
    "strafe-probe": strafe_probe.run,
    "autostrafe": autostrafe.run,
    "move-to": move_to.run,
    "screens": screens.run,
    "status": status.run,
    "aim-probe": aim_probe.run,
    "aim-shot": aim_shot.run,
    "rage": rage.run,
}

__all__ = ["SCENARIOS"]
