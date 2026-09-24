#pragma once

#include <src/engine/cache/games/games.cuh>

#include <cstdint>
#include <vector>

namespace games
{
namespace bloxstrike
{

// Place 114234929420007. Cache is a stub in njx — uses default Players; teammate via Characters folders.
auto cache_players( std::vector<cache::player>& /*out*/, const cache_ctx& /*ctx*/ ) -> void;
auto is_teammate( uintptr_t character, uintptr_t local_char ) -> bool;

}
}
