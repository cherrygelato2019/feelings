#pragma once

#include <src/engine/cache/games/games.cuh>

#include <vector>

namespace games
{
namespace badbusiness
{

// Place 3233893879 / 1168263273. Replaces default — Workspace.Characters + Body bones.
auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void;

}
}
