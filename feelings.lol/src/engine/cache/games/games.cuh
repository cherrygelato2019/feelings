#pragma once

#include <src/engine/cache/cache.cuh>
#include <src/engine/math/math.cuh>

#include <cstdint>
#include <string>
#include <vector>

namespace games
{

enum class game_id
{
    none = 0,
    phantom_forces,
    arsenal,
    bad_business,
    bloxstrike,
    deadline,
    frontlines,
    havoc,
    hypershot,
    overkill,
    rivals,
    multiversal,
};

struct cache_ctx
{
    math::vector3_t local_pos{};
    math::vector3_t cam_pos{};
    uintptr_t local_addr = 0;
    uintptr_t local_char = 0;
    uintptr_t local_prim = 0;
    std::string local_name;
};

// Place IDs from njx headers; Hypershot has no place_id — detect via Workspace.Mobs.
auto detect( int64_t place_id, uintptr_t workspace ) -> game_id;
auto name( game_id id ) -> const char*;
auto replaces_default( game_id id ) -> bool;

// Live detected game (updated by handler / cache on place change)
auto current( ) -> game_id;
auto current_place( ) -> int64_t;
auto set_current( game_id id, int64_t place_id ) -> bool; // true if changed

// Returns true if game-specific cache fully filled `out` (caller should skip default Players cache).
// Additive games (arsenal/deadline/havoc/hypershot) append and return false.
auto cache_players( game_id id, std::vector<cache::player>& out, const cache_ctx& ctx ) -> bool;

auto is_teammate( game_id id, const cache::player& p, uintptr_t local_char ) -> bool;
auto apply_teammates( game_id id, std::vector<cache::player>& players, uintptr_t local_char ) -> void;

// Clear per-game pins/state on place change (PF pins handled separately via pf_cache::clear_pins).
auto clear_state( ) -> void;

}
