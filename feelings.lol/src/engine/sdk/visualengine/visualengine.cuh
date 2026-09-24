#pragma once

#include <src/engine/math/math.cuh>
#include <windows.h>

namespace sdk
{

class u_visualengine
{
public:
    static auto view_matrix( ) -> math::matrix4_t;

    // draw-space size (game client rect when available)
    static auto dimensions( ) -> math::vector2_t;

    // raw VisualEngine dimensions (projection viewport)
    static auto ve_dimensions( ) -> math::vector2_t;

    // screen-space origin of the Roblox client (0 if overlay is synced onto it)
    static auto client_offset( ) -> math::vector2_t;

    static auto window( ) -> HWND;
    static auto sync_target( ) -> HWND;
    static auto clear( ) -> void;

    // when overlay is glued to the game client, draw in local client coords (offset = 0)
    static auto set_overlay_synced( bool synced ) -> void;
    static auto overlay_synced( ) -> bool;
};

} // namespace sdk
