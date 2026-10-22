#pragma once

// A point-in-time view of the target's player state.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cs2/game/math.hpp"

namespace cs2::game {

// One player (local or enemy), captured from its controller + pawn.
struct PlayerSnapshot {
  int index = -1;          // entity index
  std::uintptr_t pawn = 0;
  std::uintptr_t controller = 0;
  std::string name;
  std::string weapon;      // active weapon designer name (e.g. "weapon_ak47")
  std::uintptr_t weapon_entity = 0;  // the active weapon entity (`C_CSWeaponBase`)
  std::uintptr_t weapon_vdata = 0;   // its `CCSWeaponBaseVData` (`m_pWeaponVData`)
  int team = 0;            // 2 = T, 3 = CT
  int health = 0;
  int max_health = 0;
  int armor = 0;
  int life_state = 0;      // 0 = alive
  int clip = 0;            // rounds in the active weapon
  int reserve = 0;         // reserve rounds
  int ping = 0;            // ms
  int money = 0;           // account balance
  float flash = 0.0F;      // remaining flash duration (seconds)
  bool alive = false;
  bool is_local = false;
  // `CBasePlayerController::m_iConnected` (PlayerConnectedState). Measured live
  // 2026-10-15: **0 in a game as well** on this build, so it is a reported fact,
  // not the main-screen rule (see `docs/re/client-dll.md` §"Am I in a game?").
  int connected = 0;
  bool helmet = false;
  bool defuser = false;
  bool scoped = false;
  bool defusing = false;
  bool in_buy_zone = false;
  bool has_bomb = false;
  bool on_ground = false;  // C_BaseEntity::m_fFlags & FL_ONGROUND
  std::uint32_t flags = 0; // raw C_BaseEntity::m_fFlags
  Vec3 origin{};           // world origin (feet)
  Vec3 eye_origin{};       // origin + view offset
  Vec3 mins{-16.0F, -16.0F, 0.0F};  // local collision bounds
  Vec3 maxs{16.0F, 16.0F, 72.0F};
  Vec3 velocity{};
  Angle eye_angles{};
  float distance = 0.0F;   // distance from the local player (0 for local)
};

// Authoritative round/game state, read from the level's `C_CSGameRules`
// (`game_rules` global, `docs/re/client-dll.md` §"`game_rules`"). Note that the
// global is readable at the **main menu** too (a default, unstarted object with
// `warmup = freeze = true`), so `loaded` means "the rules are readable", *not*
// "a level is up" — that answer is `Snapshot::level`.
struct RulesSnapshot {
  bool loaded = false;              // the `game_rules` global was readable
  bool has_match_started = false;   // m_bHasMatchStarted
  bool freeze_period = false;       // m_bFreezePeriod — round started, no input
  bool warmup_period = false;       // m_bWarmupPeriod
  bool game_restart = false;        // m_bGameRestart
  bool bomb_planted = false;        // m_bBombPlanted
  bool bomb_dropped = false;        // m_bBombDropped
  bool t_cant_buy = false;          // m_bTCantBuy  — T buy time is over
  bool ct_cant_buy = false;         // m_bCTCantBuy — CT buy time is over
  int game_phase = 0;               // m_gamePhase (CSGamePhase)
  int round_time = 0;               // m_iRoundTime (seconds)
  int freeze_time = 0;              // m_iFreezeTime (seconds)
  int round_win_status = 0;         // m_iRoundWinStatus — != 0 ⇒ a winner is set
  int round_win_reason = 0;         // m_eRoundWinReason
  int total_rounds_played = 0;      // m_totalRoundsPlayed
  float match_start_time = 0.0F;    // m_fMatchStartTime (game time)
  float round_start_time = 0.0F;    // m_fRoundStartTime (game time)
  float restart_round_time = 0.0F;  // m_flRestartRoundTime (game time)
  float game_start_time = 0.0F;     // m_flGameStartTime (game time)
  float warmup_period_start = 0.0F; // m_fWarmupPeriodStart (game time)
  float time_until_next_phase = 0.0F;  // m_timeUntilNextPhaseStarts
};

// Authoritative "a level is up" probe — the main-screen rule.
//
// **The `game_rules` global alone is not it**: it is non-null at the main menu
// too (a default, unstarted `C_CSGameRules`, `warmup=freeze=true`,
// `match_started=false`; measured live 2026-10-15). What exists *only* in a level
// is the level's **`C_CSGameRulesProxy` entity**, whose `m_pGameRules`
// (`+0x600`) points at the same `CGameRules*` as the global
// (`docs/re/client-dll.md` §"`game_rules`"). So the rule is: an entity in the
// identity list whose `+0x600` equals `*(game_rules)`.
struct LevelSnapshot {
  bool up = false;            // the rules-proxy entity exists (true at the main
                              // menu too — the menu has a level world, measured
                              // 2026-10-15 — so this is *not* the main-screen rule)
  std::uintptr_t proxy = 0;   // its instance address
  std::uintptr_t rules = 0;   // `*(game_rules)`, the object the proxy points at
  int identities = 0;         // non-null identities walked
  int local_spawned = 0;      // the local controller's connected state (see
                              // `PlayerSnapshot::connected`)
};

struct Snapshot {
  bool valid = false;                 // target state was readable
  std::uintptr_t local_pawn = 0;
  std::optional<PlayerSnapshot> local;
  std::vector<PlayerSnapshot> players;  // every player, local included
  RulesSnapshot rules;                  // authoritative round/game phase
  LevelSnapshot level;                  // is a level up (not the main screen)?
};

}  // namespace cs2::game
