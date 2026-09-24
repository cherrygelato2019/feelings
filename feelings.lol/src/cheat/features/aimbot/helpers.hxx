#pragma once

#include <src/cheat/features/esp/helpers.hxx>
#include <src/engine/mem/usermode.cuh>
#include <src/engine/offsets/offsets.cuh>

#include <addons/imgui_addons.h>

#include <windows.h>

#include <cmath>
#include <cstring>

namespace aimbot_helpers
{

struct key_gate_t
{
    bool active = false;
    bool just_activated = false;
};

inline auto key_gate( int key, int mode, bool& toggled, bool& was_down ) -> key_gate_t
{
    if ( mode == ImAdd::KeybindMode_Always )
    {
        const bool just = !was_down;
        was_down = true;
        return { true, just };
    }

    if ( key <= 0 )
    {
        was_down = false;
        return { false, false };
    }

    const bool down = ( GetAsyncKeyState( key ) & 0x8000 ) != 0;

    if ( mode == ImAdd::KeybindMode_Toggle )
    {
        bool just = false;
        if ( down && !was_down )
        {
            toggled = !toggled;
            just = toggled;
        }
        was_down = down;
        return { toggled, just };
    }

    const bool just = down && !was_down;
    was_down = down;
    return { down, just };
}

inline auto roblox_hwnd( ) -> HWND
{
    static HWND cached = nullptr;
    if ( cached && IsWindow( cached ) )
        return cached;

    struct finder_t
    {
        DWORD pid;
        HWND result;
    };

    finder_t finder{ usermode->pid( ), nullptr };

    EnumWindows( []( HWND hwnd, LPARAM param ) -> BOOL
    {
        auto* self = reinterpret_cast<finder_t*>( param );

        DWORD window_pid = 0;
        GetWindowThreadProcessId( hwnd, &window_pid );
        if ( window_pid != self->pid || !IsWindowVisible( hwnd ) )
            return TRUE;

        char class_name[256]{};
        GetClassNameA( hwnd, class_name, sizeof( class_name ) );
        if ( std::strcmp( class_name, "WINDOWSCLIENT" ) != 0 )
            return TRUE;

        self->result = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>( &finder ) );

    cached = finder.result;
    return cached;
}

inline auto cursor_client( ImVec2& out ) -> bool
{
    POINT point{};
    const HWND hwnd = roblox_hwnd( );
    if ( !hwnd || !GetCursorPos( &point ) || !ScreenToClient( hwnd, &point ) )
        return false;

    out = { static_cast<float>( point.x ), static_cast<float>( point.y ) };
    return true;
}

inline auto move_mouse( float dx, float dy ) -> void
{
    static float accum_x = 0.f;
    static float accum_y = 0.f;

    accum_x += dx;
    accum_y += dy;

    const LONG ix = static_cast<LONG>( accum_x );
    const LONG iy = static_cast<LONG>( accum_y );
    if ( ix == 0 && iy == 0 )
        return;

    accum_x -= static_cast<float>( ix );
    accum_y -= static_cast<float>( iy );

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = ix;
    input.mi.dy = iy;
    SendInput( 1, &input, sizeof( INPUT ) );
}

inline auto mouse_click( bool down ) -> void
{
    INPUT i{};
    i.type = INPUT_MOUSE;
    i.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput( 1, &i, sizeof( i ) );
}

inline auto bone_aliases( int aim_part, const char* out[4] ) -> int
{
    switch ( aim_part )
    {
    case 0:
        out[0] = "Head";
        return 1;
    case 1:
        out[0] = "UpperTorso";
        out[1] = "Torso";
        return 2;
    case 2:
        out[0] = "LowerTorso";
        out[1] = "Torso";
        return 2;
    case 3:
        out[0] = "HumanoidRootPart";
        out[1] = "UpperTorso";
        out[2] = "Torso";
        return 3;
    default:
        out[0] = "Head";
        return 1;
    }
}

inline auto find_named_bone( const cache::player& player, const char* name ) -> const cache::player::bone*
{
    for ( const auto& b : player.bones )
    {
        if ( b.name == name )
            return &b;
    }
    return nullptr;
}

inline auto resolve_hitbox( const cache::player& player, int aim_part ) -> const cache::player::bone*
{
    if ( aim_part == 0 )
    {
        if ( const auto* head = find_named_bone( player, "Head" ) )
            return head;
        if ( const auto* head = find_named_bone( player, "head" ) )
            return head;
        if ( const auto* torso = find_named_bone( player, "UpperTorso" ) )
            return torso;
        if ( const auto* torso = find_named_bone( player, "Torso" ) )
            return torso;
        if ( const auto* root = find_named_bone( player, "HumanoidRootPart" ) )
            return root;
        return nullptr;
    }

    const char* names[4]{};
    const int n = bone_aliases( aim_part, names );
    for ( int i = 0; i < n; ++i )
    {
        if ( const auto* b = find_named_bone( player, names[i] ) )
            return b;
    }
    return nullptr;
}

inline auto head_aim_world( const cache::player& player ) -> math::vector3_t
{
    const cache::player::bone* bone = find_named_bone( player, "Head" );
    if ( !bone )
        bone = find_named_bone( player, "head" );

    if ( bone )
    {
        const cache::bone_pose_t pose = cache::read_bone_pose( *bone );
        if ( pose.valid )
            return pose.position;
        if ( bone->position.is_valid( ) )
            return bone->position;
    }

    return math::vector3_t::nan( );
}

inline auto nearest_bone_to_cursor( const cache::player& player, const math::vector2_t& dimensions,
    const math::matrix4_t& viewmatrix, const math::vector2_t& offset, const ImVec2& cursor,
    math::vector3_t& out_world, ImVec2& out_screen ) -> bool
{
    float best = FLT_MAX;
    bool found = false;

    for ( const auto& b : player.bones )
    {
        if ( b.name.rfind( "Acc:", 0 ) == 0 || b.name == "HumanoidRootPart" )
            continue;

        math::vector3_t world = b.position;
        if ( b.part )
        {
            const cache::bone_pose_t pose = cache::read_bone_pose( b );
            if ( pose.valid )
                world = pose.position;
        }
        if ( !world.is_valid( ) )
            continue;

        ImVec2 screen{};
        if ( !project_world( world, dimensions, viewmatrix, offset, screen ) )
            continue;

        const float dx = screen.x - cursor.x;
        const float dy = screen.y - cursor.y;
        const float dsq = dx * dx + dy * dy;
        if ( dsq < best )
        {
            best = dsq;
            out_world = world;
            out_screen = screen;
            found = true;
        }
    }
    return found;
}

} 
