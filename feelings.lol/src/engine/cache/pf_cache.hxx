#pragma once

#include <src/engine/cache/cache.cuh>
#include <src/engine/math/math.cuh>

#include <cstdint>
#include <string>
#include <vector>

namespace pf_cache
{

auto cache_players(
    std::vector<cache::player>& out,
    const math::vector3_t& local_pos,
    uintptr_t local_prim,
    const std::string& local_name ) -> void;

auto resolve_local_prim( uintptr_t local_char ) -> uintptr_t;

auto clear_pins( ) -> void;

}
