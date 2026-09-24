#pragma once

#include <src/engine/cache/games/games.cuh>

#include <vector>

namespace games
{
namespace frontlines
{

// Place 5938036553. Replaces default — Workspace soldier_model children.
auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void;

}
}
