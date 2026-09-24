#pragma once

#include <src/engine/math/math.cuh>

namespace raycast_hook
{
    auto ensure( ) -> bool;
    auto unhook( ) -> void;
    auto aim( const math::vector3_t& world, const math::vector3_t& cam, bool wallbang ) -> void;
    auto passthrough( ) -> void;
    auto armed( ) -> bool;
    auto hooked_count( ) -> int;
    auto hits( ) -> uint64_t;
    auto log_debug( ) -> void;
}

namespace viewport_hook
{
    auto ensure( ) -> bool;
    auto unhook( ) -> void;
    auto redirect( const math::vector3_t& origin, const math::vector3_t& direction, float screen_x, float screen_y ) -> void;
    auto passthrough( ) -> void;
    auto armed( ) -> bool;
    auto hooked_count( ) -> int;
    auto hits( ) -> uint64_t;
    auto rewrites( ) -> uint64_t;
}
