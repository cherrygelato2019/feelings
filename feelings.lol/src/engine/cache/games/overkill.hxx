#pragma once

#include <src/engine/cache/games/games.cuh>

#include <vector>

namespace games
{
namespace overkill
{

// Place 124842176624983. Replaces default — BFS for HitboxTorso character roots.
auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void;

}
}
