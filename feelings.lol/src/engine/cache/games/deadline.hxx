#pragma once

#include <src/engine/cache/games/games.cuh>

#include <vector>

namespace games
{
namespace deadline
{

// Place 119426593006199. Appends Workspace.characters (lowercase) with custom bone names.
auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void;

}
}
