#include <include/global.cuh>

#include <src/cheat/features/player/teleport.cuh>
#include <src/cheat/features/combat/combat_state.cuh>

#include <addons/imgui_addons.h>
#include <windows.h>

#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace teleport
{
namespace
{

std::atomic<bool> g_alive{ false };
std::thread g_thread;

std::mutex g_wp_mu;
std::vector<waypoint_t> g_waypoints;
int g_selected = -1;
bool g_loaded = false;

auto key_active( int key, int mode, bool& was, bool& toggle ) -> bool
{
    if ( mode == ImAdd::KeybindMode_Always )
        return true;
    if ( key <= 0 )
        return false;

    const bool down = ( GetAsyncKeyState( key ) & 0x8000 ) != 0;
    if ( mode == ImAdd::KeybindMode_Toggle )
    {
        if ( down && !was )
            toggle = !toggle;
        was = down;
        return toggle;
    }

    was = down;
    return down;
}

auto rising( bool active, bool& prev ) -> bool
{
    const bool edge = active && !prev;
    prev = active;
    return edge;
}

// Write full assembly-root CFrame (rotation @ 0xb0 + position @ 0xd4 = 12 floats).
// Docs: setting BasePart.CFrame on the root moves joined parts with the weld; do not delta every part.
auto write_pos( uintptr_t prim, const math::vector3_t& pos, int repeats ) -> void
{
    if ( !prim || !pos.is_valid( ) || !usermode->can_rpm( ) )
        return;

    const math::matrix3_t rot = usermode->read<math::matrix3_t>( prim + offsets::primitive::rotation );
    float block[12]{};
    for ( int i = 0; i < 9; ++i )
        block[i] = rot.data[i];
    block[9] = pos.x;
    block[10] = pos.y;
    block[11] = pos.z;

    for ( int i = 0; i < repeats; ++i )
    {
        if ( !usermode->can_rpm( ) )
            return;
        usermode->write_buf( prim + offsets::primitive::rotation, block, sizeof( block ) );
        usermode->write<math::vector3_t>( prim + offsets::primitive::position, pos );
    }
}

auto write_vel( uintptr_t prim, const math::vector3_t& vel, bool burst ) -> void
{
    if ( !prim || !usermode->can_rpm( ) )
        return;
    const int n = burst ? 2 : 1;
    for ( int i = 0; i < n; ++i )
    {
        if ( !usermode->can_rpm( ) )
            return;
        // AssemblyLinearVelocity — same for every part in the assembly
        usermode->write<math::vector3_t>( prim + offsets::primitive::linear_velocity, vel );
    }
    usermode->write<math::vector3_t>( prim + offsets::primitive::angular_velocity, {} );
}

auto set_anchored( uintptr_t prim, bool on ) -> void
{
    if ( !prim )
        return;
    uint8_t flags = usermode->read<uint8_t>( prim + offsets::primitive::flags );
    if ( on )
        flags = ( uint8_t )( flags | offsets::primitive_flags::anchored );
    else
        flags = ( uint8_t )( flags & ~offsets::primitive_flags::anchored );
    usermode->write<uint8_t>( prim + offsets::primitive::flags, flags );
}

auto live_character( ) -> sdk::u_model
{
    if ( !g->engine.players )
        return {};
    sdk::u_players service( g->engine.players.load( ) );
    sdk::u_player lp = service.local_player( );
    if ( !lp )
        return {};
    return lp.character( );
}

auto live_humanoid( ) -> sdk::u_humanoid
{
    sdk::u_model character = live_character( );
    if ( !character )
        return {};
    return sdk::as<sdk::u_humanoid>( character.find_first_child_of_class( "Humanoid" ) );
}

auto live_hrp( ) -> sdk::u_base_part
{
    if ( !g->engine.players )
        return {};

    sdk::u_players service( g->engine.players.load( ) );
    sdk::u_player lp = service.local_player( );
    if ( !lp )
        return {};

    sdk::u_model character = lp.character( );
    if ( !character )
        return {};

    sdk::u_base_part hrp = sdk::get_child_as<sdk::u_base_part>( character, "HumanoidRootPart" );
    if ( !hrp )
        hrp = sdk::get_child_as<sdk::u_base_part>( character, "Torso" );
    if ( !hrp )
        hrp = sdk::get_child_as<sdk::u_base_part>( character, "UpperTorso" );
    return hrp;
}

auto bone_pos( const cache::player& p ) -> math::vector3_t
{
    for ( const char* want : { "HumanoidRootPart", "UpperTorso", "Torso", "Head" } )
    {
        for ( const auto& b : p.bones )
        {
            if ( b.name == want && b.position.is_valid( ) )
                return b.position;
        }
    }
    return {};
}

auto cam_look_dest( float dist ) -> math::vector3_t
{
    sdk::u_camera cam( g->engine.camera.load( ) );
    if ( !cam )
        return {};

    const math::vector3_t from = cam.position( );
    if ( !from.is_valid( ) )
        return {};

    const math::matrix3_t rot = cam.orientation( );
    const math::vector3_t look{
        -rot.data[2],
        -rot.data[5],
        -rot.data[8],
    };
    const float len = math::vec3_length( look );
    if ( len < 1e-4f )
        return {};

    const float inv = 1.f / len;
    return {
        from.x + look.x * inv * dist,
        from.y + look.y * inv * dist,
        from.z + look.z * inv * dist,
    };
}

auto closest_player_pos( ) -> math::vector3_t
{
    auto players = g->cache.snapshot_players( );
    if ( !players )
        return {};

    math::vector3_t best{};
    float best_d = FLT_MAX;
    for ( const auto& p : *players )
    {
        if ( p.distance <= 0.f || p.distance >= best_d )
            continue;
        const math::vector3_t at = bone_pos( p );
        if ( !at.is_valid( ) )
            continue;
        best_d = p.distance;
        best = at;
    }
    return best;
}

auto combat_target_pos( ) -> math::vector3_t
{
    const uintptr_t prefer = combat::aim_sticky.load( )
        ? combat::aim_sticky.load( )
        : ( combat::silent_sticky.load( ) ? combat::silent_sticky.load( ) : combat::last_target.load( ) );
    if ( !prefer )
        return closest_player_pos( );

    auto players = g->cache.snapshot_players( );
    if ( !players )
        return {};

    for ( const auto& p : *players )
    {
        if ( p.address != prefer )
            continue;
        return bone_pos( p );
    }
    return {};
}

auto selected_pos_unlocked( ) -> math::vector3_t
{
    if ( g_selected < 0 || g_selected >= ( int )g_waypoints.size( ) )
        return {};
    return g_waypoints[( size_t )g_selected].pos;
}

auto resolve_dest( ) -> math::vector3_t
{
    switch ( config.player.teleport_method )
    {
    case 1:
        return closest_player_pos( );
    case 2:
        return combat_target_pos( );
    case 3:
    {
        std::lock_guard<std::mutex> lock( g_wp_mu );
        return selected_pos_unlocked( );
    }
    default:
        return cam_look_dest( config.player.teleport_distance );
    }
}

auto fire( ) -> void
{
    math::vector3_t dest = resolve_dest( );
    if ( !dest.is_valid( ) )
        return;

    const float pad = ( config.player.teleport_method == 0 ) ? 0.f : config.player.teleport_height;
    to_world( dest, pad );
}

auto waypoints_path( ) -> std::string
{
    char mod[MAX_PATH]{};
    if ( !GetModuleFileNameA( nullptr, mod, MAX_PATH ) )
        return "feelings_waypoints.txt";
    std::string path( mod );
    const size_t slash = path.find_last_of( "\\/" );
    if ( slash != std::string::npos )
        path.resize( slash + 1 );
    path += "feelings_waypoints.txt";
    return path;
}

auto persist_unlocked( ) -> void
{
    std::ofstream out( waypoints_path( ), std::ios::trunc );
    if ( !out )
        return;
    for ( const auto& wp : g_waypoints )
    {
        if ( wp.name.empty( ) || !wp.pos.is_valid( ) )
            continue;
        out << wp.name << '|' << wp.pos.x << '|' << wp.pos.y << '|' << wp.pos.z << '\n';
    }
}

auto thread_main( ) -> void
{
    bool tp_was = false, tp_tog = false, tp_prev = false;
    bool sv_was = false;

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        if ( !g_loaded )
        {
            load_waypoints( );
            g_loaded = true;
        }

        if ( !config.player.teleport || !usermode->can_rpm( ) )
        {
            tp_prev = false;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            continue;
        }

        const bool armed = key_active( config.player.teleport_key, config.player.teleport_key_mode, tp_was, tp_tog );
        if ( rising( armed, tp_prev ) )
            fire( );

        if ( config.player.teleport_save_key > 0 )
        {
            const bool down = ( GetAsyncKeyState( config.player.teleport_save_key ) & 0x8000 ) != 0;
            if ( down && !sv_was )
                save_current_waypoint( config.player.waypoint_name );
            sv_was = down;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 8 : 4 ) );
    }
}

}

auto to_world( const math::vector3_t& world, float height_pad ) -> bool
{
    if ( !world.is_valid( ) || !usermode->can_rpm( ) )
        return false;

    // Only the assembly root (HRP) — weld graph carries the rest when CFrame is set.
    sdk::u_base_part hrp = live_hrp( );
    if ( !hrp )
        return false;

    const uintptr_t prim = hrp.primitive( );
    if ( !prim || !usermode->is_valid( prim ) )
        return false;

    math::vector3_t at = world;
    at.y += height_pad;

    set_anchored( prim, true );
    write_pos( prim, at, 2 );
    write_vel( prim, {}, false );

    if ( sdk::u_humanoid hum = live_humanoid( ) )
    {
        hum.set_sit( false );
        hum.set_platform_stand( false );
        hum.move_to( at, 1e7 );
    }

    for ( int i = 0; i < 4; ++i )
    {
        write_pos( prim, at, 1 );
        write_vel( prim, {}, false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }

    set_anchored( prim, false );
    write_vel( prim, {}, false );
    g->logger.print( "[teleport] -> (%.1f, %.1f, %.1f)", at.x, at.y, at.z );
    return true;
}

auto waypoints( ) -> std::vector<waypoint_t>
{
    std::lock_guard<std::mutex> lock( g_wp_mu );
    return g_waypoints;
}

auto selected_waypoint( ) -> int
{
    std::lock_guard<std::mutex> lock( g_wp_mu );
    return g_selected;
}

auto set_selected_waypoint( int index ) -> void
{
    std::lock_guard<std::mutex> lock( g_wp_mu );
    if ( index < 0 || index >= ( int )g_waypoints.size( ) )
        g_selected = -1;
    else
        g_selected = index;
}

auto add_waypoint( const char* name, const math::vector3_t& pos ) -> bool
{
    if ( !pos.is_valid( ) )
        return false;

    std::string n = ( name && name[0] ) ? name : "waypoint";
    {
        std::lock_guard<std::mutex> lock( g_wp_mu );
        g_waypoints.push_back( { std::move( n ), pos } );
        g_selected = ( int )g_waypoints.size( ) - 1;
        persist_unlocked( );
    }
    return true;
}

auto save_current_waypoint( const char* name ) -> bool
{
    sdk::u_base_part hrp = live_hrp( );
    if ( !hrp )
        return false;
    const uintptr_t prim = hrp.primitive( );
    if ( !prim || !usermode->is_valid( prim ) )
        return false;

    const math::vector3_t at = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
    if ( !at.is_valid( ) )
        return false;

    const bool ok = add_waypoint( name, at );
    if ( ok )
        g->logger.print( "[teleport] waypoint '%s' (%.1f, %.1f, %.1f)",
            ( name && name[0] ) ? name : "waypoint", at.x, at.y, at.z );
    return ok;
}

auto remove_waypoint( int index ) -> void
{
    std::lock_guard<std::mutex> lock( g_wp_mu );
    if ( index < 0 || index >= ( int )g_waypoints.size( ) )
        return;
    g_waypoints.erase( g_waypoints.begin( ) + index );
    if ( g_selected >= ( int )g_waypoints.size( ) )
        g_selected = ( int )g_waypoints.size( ) - 1;
    persist_unlocked( );
}

auto clear_waypoints( ) -> void
{
    std::lock_guard<std::mutex> lock( g_wp_mu );
    g_waypoints.clear( );
    g_selected = -1;
    persist_unlocked( );
}

auto teleport_to_waypoint( int index ) -> bool
{
    math::vector3_t pos{};
    {
        std::lock_guard<std::mutex> lock( g_wp_mu );
        if ( index < 0 || index >= ( int )g_waypoints.size( ) )
            return false;
        pos = g_waypoints[( size_t )index].pos;
        g_selected = index;
    }
    return to_world( pos, config.player.teleport_height );
}

auto load_waypoints( ) -> void
{
    std::lock_guard<std::mutex> lock( g_wp_mu );
    g_waypoints.clear( );
    g_selected = -1;

    std::ifstream in( waypoints_path( ) );
    if ( !in )
        return;

    std::string line;
    while ( std::getline( in, line ) )
    {
        if ( line.empty( ) )
            continue;
        const size_t a = line.find( '|' );
        const size_t b = ( a == std::string::npos ) ? std::string::npos : line.find( '|', a + 1 );
        const size_t c = ( b == std::string::npos ) ? std::string::npos : line.find( '|', b + 1 );
        if ( a == std::string::npos || b == std::string::npos || c == std::string::npos )
            continue;

        waypoint_t wp{};
        wp.name = line.substr( 0, a );
        try
        {
            wp.pos.x = std::stof( line.substr( a + 1, b - a - 1 ) );
            wp.pos.y = std::stof( line.substr( b + 1, c - b - 1 ) );
            wp.pos.z = std::stof( line.substr( c + 1 ) );
        }
        catch ( ... )
        {
            continue;
        }
        if ( wp.name.empty( ) || !wp.pos.is_valid( ) )
            continue;
        g_waypoints.push_back( std::move( wp ) );
    }
    if ( !g_waypoints.empty( ) )
        g_selected = 0;
}

auto save_waypoints( ) -> void
{
    std::lock_guard<std::mutex> lock( g_wp_mu );
    persist_unlocked( );
}

auto start( ) -> void
{
    if ( g_alive.exchange( true ) )
        return;
    g_thread = std::thread( thread_main );
}

auto stop( ) -> void
{
    if ( !g_alive.exchange( false ) )
        return;
    if ( g_thread.joinable( ) )
        g_thread.join( );
    save_waypoints( );
}

}
