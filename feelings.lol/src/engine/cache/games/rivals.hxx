#pragma once

#include <src/engine/cache/games/games.cuh>

#include <cstdint>
#include <vector>

namespace games
{
namespace rivals
{

// Place 5995470825. Cache is a stub in njx — uses default Players; teammate via HRP.TeammateLabel.
auto cache_players( std::vector<cache::player>& /*out*/, const cache_ctx& /*ctx*/ ) -> void;
auto is_teammate( uintptr_t character ) -> bool;

}
}
