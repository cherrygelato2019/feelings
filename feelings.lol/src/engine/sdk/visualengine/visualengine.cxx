#include <include/global.cuh>

namespace sdk
{

namespace
{
    HWND g_hwnd = nullptr;
    HWND g_sync_hwnd = nullptr;
    uintptr_t g_ve_base = 0;
    uintptr_t g_ve = 0;
    bool g_overlay_synced = false;

    auto resolve_ve( ) -> uintptr_t
    {
        const uintptr_t base = usermode->base( );
        // Always re-read the VisualEngine pointer — caching it across teleports /
        // place loads makes W2S die randomly and boxes stick to the camera.
        g_ve_base = base;
        g_ve = base ? usermode->read<uintptr_t>( base + offsets::visual_engine::pointer ) : 0;
        return g_ve;
    }

    struct enum_ctx_t
    {
        DWORD pid;
        HWND hwnd;
    };

    auto CALLBACK enum_game_window( HWND hwnd, LPARAM lparam ) -> BOOL
    {
        auto* ctx = reinterpret_cast<enum_ctx_t*>( lparam );

        DWORD pid = 0;
        GetWindowThreadProcessId( hwnd, &pid );
        if ( pid != ctx->pid || !IsWindowVisible( hwnd ) )
            return TRUE;

        char cls[64]{};
        GetClassNameA( hwnd, cls, sizeof( cls ) );
        if ( strcmp( cls, "WINDOWSCLIENT" ) != 0 )
            return TRUE;

        ctx->hwnd = hwnd;
        return FALSE;
    }

    auto client_size( HWND hwnd ) -> math::vector2_t
    {
        if ( !hwnd )
            return { };

        RECT rect{};
        if ( !GetClientRect( hwnd, &rect ) )
            return { };

        const float w = ( float )( rect.right - rect.left );
        const float h = ( float )( rect.bottom - rect.top );
        if ( w < 2.f || h < 2.f )
            return { };

        return { w, h };
    }
}

auto u_visualengine::set_overlay_synced( bool synced ) -> void
{
    g_overlay_synced = synced;
}

auto u_visualengine::overlay_synced( ) -> bool
{
    return g_overlay_synced;
}

auto u_visualengine::view_matrix( ) -> math::matrix4_t
{
    uintptr_t ve = resolve_ve( );
    if ( !ve )
        return { };
    return usermode->read<math::matrix4_t>( ve + offsets::visual_engine::view_matrix );
}

auto u_visualengine::window( ) -> HWND
{
    if ( g_hwnd && IsWindow( g_hwnd ) )
        return g_hwnd;

    g_hwnd = FindWindowA( "WINDOWSCLIENT", "Roblox" );
    if ( g_hwnd )
        return g_hwnd;

    const DWORD pid = usermode->pid( );
    if ( pid )
    {
        enum_ctx_t ctx{ pid, nullptr };
        EnumWindows( enum_game_window, reinterpret_cast<LPARAM>( &ctx ) );
        g_hwnd = ctx.hwnd;
    }

    if ( !g_hwnd )
        g_hwnd = FindWindowW( nullptr, L"Roblox" );

    return g_hwnd;
}

auto u_visualengine::sync_target( ) -> HWND
{
    HWND game = window( );
    g_sync_hwnd = game;
    return game;
}

auto u_visualengine::ve_dimensions( ) -> math::vector2_t
{
    uintptr_t ve = resolve_ve( );
    if ( ve )
    {
        math::vector2_t dim = usermode->read<math::vector2_t>( ve + offsets::visual_engine::dimensions );
        if ( dim.x > 1.f && dim.y > 1.f )
            return dim;
    }
    return client_size( window( ) );
}

auto u_visualengine::dimensions( ) -> math::vector2_t
{
    // Draw / overlay space = game client rect (matches synced ImGui DisplaySize).
    // W2S still uses ve_dimensions(); project_to_draw scales VE → this.
    const math::vector2_t client = client_size( window( ) );
    if ( client.x > 1.f && client.y > 1.f )
        return client;

    return ve_dimensions( );
}

auto u_visualengine::client_offset( ) -> math::vector2_t
{
    // Overlay glued to the client → draw in local client space.
    if ( g_overlay_synced )
        return { 0.f, 0.f };

    HWND hwnd = window( );
    if ( !hwnd )
        return { };

    POINT pt{ 0, 0 };
    ClientToScreen( hwnd, &pt );
    return { ( float )pt.x, ( float )pt.y };
}

auto u_visualengine::clear( ) -> void
{
    g_hwnd = nullptr;
    g_sync_hwnd = nullptr;
    g_ve_base = 0;
    g_ve = 0;
    g_overlay_synced = false;
}

} // namespace sdk
