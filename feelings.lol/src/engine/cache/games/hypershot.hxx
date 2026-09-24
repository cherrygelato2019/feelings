#pragma once

#include <src/engine/cache/games/games.cuh>

#include <vector>

namespace games
{
namespace hypershot
{

// No place_id in njx — active when Workspace.Mobs exists. Appends mob models as NPCs.
auto is_active( uintptr_t workspace ) -> bool;
auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void;

}
}
