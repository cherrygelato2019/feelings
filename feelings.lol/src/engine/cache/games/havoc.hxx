#pragma once

#include <src/engine/cache/games/games.cuh>

#include <vector>

namespace games
{
namespace havoc
{

// Place 16530963934. Appends bot models from the match folder (ActiveScripts / WeldObjectsLink).
auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void;

}
}
