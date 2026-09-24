#pragma once

#include <src/engine/cache/games/games.cuh>

#include <vector>

namespace games
{
namespace arsenal
{

// Place 863266079. Appends Workspace.Characters + Workspace.Zombies on top of default cache.
auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void;

}
}
