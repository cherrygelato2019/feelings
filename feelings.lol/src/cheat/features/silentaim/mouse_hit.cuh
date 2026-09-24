#pragma once

#include <src/engine/math/math.cuh>

namespace silentaim::mouse_hit
{
auto ensure( ) -> bool;
auto unhook( ) -> void;
auto hooked( ) -> bool;
auto armed( ) -> bool;
auto apply(
    const math::vector3_t& hit_pos,
    const math::vector3_t& cam_pos,
    uintptr_t target_part,
    float screen_x,
    float screen_y ) -> bool;
auto passthrough( ) -> void;
}
