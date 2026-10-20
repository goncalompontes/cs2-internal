// Unit tests for the material system's parameter-name hash and for the chams'
// "is this entity one of ours" decision.
//
// **The hash** is the engine's own (recovered from materialsystem2.dll
// `FUN_180007f20`): MurmurHash2 over the case-folded name, seed
// `0x31415926`, `h = len ^ seed`. The payload looks material parameters up by
// that hash, so a refactor of the hash would silently address the wrong
// parameter. The test pins it against the engine's own constants —
// `0x5D700411` = "RepresentativeTexture" and `0xf655faab` =
// "PlayerVisibilityDataColor" — both of which are the hashes the live
// binaries use.
//
// **The decision** is a pure function of the four facts about a drawn entity
// (team, life state, local, view model). It is the part of the chams that
// decides whether a model is drawn with the flat material, so it is pinned
// here: a wrong answer either chams the whole world or chams nothing.

#include <cstdio>

#include "cs2/game/material.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

}  // namespace

auto main() -> int {
  using cs2::game::ChamsEntity;
  using cs2::game::ChamsPolicy;

  // --- The parameter-name hash ----------------------------------------------
  {
    // The engine's own constant: materialsystem2.dll looks the
    // "RepresentativeTexture" parameter up by this hash.
    expect(cs2::game::material_parameter_hash("RepresentativeTexture") == 0x5D700411U,
           "hash(RepresentativeTexture) == 0x5D700411");
    expect(cs2::game::material_parameter_hash("representativetexture") == 0x5D700411U,
           "the hash is case folded");
    expect(cs2::game::material_parameter_hash("REPRESENTATIVETEXTURE") == 0x5D700411U,
           "the hash is case folded (upper case)");
    // The scene system's named value ("PlayerVisibilityDataColor"), the hash
    // the live client.dll writes into the scene object desc.
    expect(cs2::game::material_parameter_hash("PlayerVisibilityDataColor") == 0xf655faabU,
           "hash(PlayerVisibilityDataColor) == 0xf655faab");
    // The two names the material binds as textures. They are *not* colour
    // parameters — the pin keeps that distinction honest.
    expect(cs2::game::material_parameter_hash("DataColor") == 0x1c6e1da6U,
           "hash(DataColor) == 0x1c6e1da6");
    // A short name exercises the tail branch of the hash.
    expect(cs2::game::material_parameter_hash("a") == 0x1ecf71e1U, "hash(a) == 0x1ecf71e1");
    expect(cs2::game::material_parameter_hash("ab") == 0xe7ff0c8aU, "hash(ab) == 0xe7ff0c8a");
    expect(cs2::game::material_parameter_hash("abc") == 0xd6cf0d16U, "hash(abc) == 0xd6cf0d16");
    expect(cs2::game::material_parameter_hash("abcd") == 0xb42825ecU, "hash(abcd) == 0xb42825ec");
    expect(cs2::game::material_parameter_hash("abcde") == 0x9d750f78U, "hash(abcde) == 0x9d750f78");
    expect(cs2::game::material_parameter_hash("") == 0xb5d89f2fU, "hash(\"\") == 0xb5d89f2f");
    // The hash must be stable: the same name always hashes the same way.
    expect(
        cs2::game::material_parameter_hash("Color") == cs2::game::material_parameter_hash("Color"),
        "the hash is stable");
  }

  // --- The chams decision ----------------------------------------------------
  {
    ChamsPolicy policy;
    policy.enabled = true;
    policy.only_enemies = true;
    policy.include_local = false;
    policy.local_team = 3;  // the local player is on CT

    // An alive enemy is chamsed.
    {
      const ChamsEntity enemy{2 /*T*/, 0, false, false};
      expect(cs2::game::wants_chams(enemy, policy), "an alive enemy is chamsed");
    }
    // A dead player is not.
    {
      const ChamsEntity dead{2, 1, false, false};
      expect(!cs2::game::wants_chams(dead, policy), "a dead player is not chamsed");
    }
    // The view model is never chamsed.
    {
      const ChamsEntity viewmodel{2, 0, false, true};
      expect(!cs2::game::wants_chams(viewmodel, policy), "the view model is never chamsed");
    }
    // A teammate is not, with "only enemies" on.
    {
      const ChamsEntity teammate{3, 0, false, false};
      expect(!cs2::game::wants_chams(teammate, policy), "a teammate is not chamsed");
    }
    // ... and is, with "only enemies" off.
    {
      ChamsPolicy teammates = policy;
      teammates.only_enemies = false;
      const ChamsEntity teammate{3, 0, false, false};
      expect(cs2::game::wants_chams(teammate, teammates),
             "a teammate is chamsed without "
             "'only enemies'");
    }
    // The local player is not, unless asked for.
    {
      const ChamsEntity local{3, 0, true, false};
      expect(!cs2::game::wants_chams(local, policy), "the local player is not chamsed");
    }
    {
      ChamsPolicy with_local = policy;
      with_local.include_local = true;
      const ChamsEntity local{3, 0, true, false};
      expect(cs2::game::wants_chams(local, with_local), "the local player is chamsed on request");
    }
    // A prop (not a player) is not.
    {
      const ChamsEntity prop{0, 0, false, false};
      expect(!cs2::game::wants_chams(prop, policy), "a prop is not chamsed");
    }
    // Nothing is chamsed while the feature is off.
    {
      ChamsPolicy off = policy;
      off.enabled = false;
      const ChamsEntity enemy{2, 0, false, false};
      expect(!cs2::game::wants_chams(enemy, off), "nothing is chamsed while the feature is off");
    }
    // The local player is never chamsed through the view-model path, whatever
    // the policy says.
    {
      ChamsPolicy with_local = policy;
      with_local.include_local = true;
      const ChamsEntity local_viewmodel{3, 0, true, true};
      expect(!cs2::game::wants_chams(local_viewmodel, with_local),
             "the view model is not chamsed even with the local player included");
    }
  }

  if (failures != 0) {
    std::printf("material: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("material: ok");
  return 0;
}
