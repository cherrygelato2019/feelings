#include <include/global.cuh>

#include <src/cheat/features/player/player_mods.cuh>
#include <src/cheat/features/aimbot/helpers.hxx>
#include <src/cheat/features/combat/combat_state.cuh>
#include <src/cheat/features/esp/helpers.hxx>

#include <addons/imgui_addons.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace player_mods
{
namespace
{

constexpr int k_burst = 2;

std::atomic<bool> g_alive{ false };
std::thread g_fly_thread;
std::thread g_speed_thread;
std::thread g_misc_thread;
std::thread g_strafe_thread;
std::thread g_hvh_thread;
std::thread g_infjump_thread;
std::thread g_hitbox_thread;

struct hitbox_vis_t
{
    math::vector3_t pos{};
    math::matrix3_t rot{};
    math::vector3_t size{};
};
std::mutex g_hitbox_vis_mtx;
std::vector<hitbox_vis_t> g_hitbox_vis;
std::mutex g_hitbox_orig_mtx;
std::unordered_map<uintptr_t, math::vector3_t> g_hitbox_orig; // prim -> original size
bool g_hitbox_was_on = false;

struct hvh_vis_t
{
    math::vector3_t pos{};
    math::vector3_t look{};
    math::vector3_t ghost{};
    bool aa = false;
    bool ghost_ok = false;
};
std::mutex g_hvh_vis_mtx;
hvh_vis_t g_hvh_vis{};

auto local_player( ) -> sdk::u_player
{
    const uintptr_t client = g->engine.client.load( );
    if ( client )
        return sdk::u_player( client );

    if ( !g->engine.players )
        return {};
    sdk::u_players service( g->engine.players.load( ) );
    return service.local_player( );
}

auto local_character( ) -> sdk::u_model
{
    const uintptr_t cached = g->engine.client_model.load( );
    if ( cached )
        return sdk::u_model( cached );

    sdk::u_player lp = local_player( );
    if ( !lp )
        return {};
    return lp.character( );
}

auto local_humanoid( ) -> sdk::u_humanoid
{
    sdk::u_model character = local_character( );
    if ( !character )
        return {};
    return sdk::as<sdk::u_humanoid>( character.find_first_child_of_class( "Humanoid" ) );
}

auto local_hrp( ) -> sdk::u_base_part
{
    auto part_ok = []( const sdk::u_base_part& bp ) -> bool
    {
        if ( !bp )
            return false;
        const uintptr_t prim = bp.primitive( );
        return prim && prim >= 0x10000 && usermode->is_valid( prim );
    };

    sdk::u_model character = local_character( );
    if ( character )
    {
        sdk::u_base_part hrp = sdk::get_child_as<sdk::u_base_part>( character, "HumanoidRootPart" );
        if ( part_ok( hrp ) )
            return hrp;
        hrp = sdk::get_child_as<sdk::u_base_part>( character, "Torso" );
        if ( part_ok( hrp ) )
            return hrp;
        hrp = sdk::get_child_as<sdk::u_base_part>( character, "UpperTorso" );
        if ( part_ok( hrp ) )
            return hrp;
    }

    sdk::u_humanoid hum = local_humanoid( );
    if ( hum )
    {
        sdk::u_base_part root = hum.root_part( );
        if ( part_ok( root ) )
            return root;
    }
    return {};
}

auto key_active( int key, int mode, bool& was, bool& toggle ) -> bool
{
    if ( key <= 0 || mode == ImAdd::KeybindMode_Always )
        return true;
    if ( mode == ImAdd::KeybindMode_Toggle )
    {
        const bool down = ( GetAsyncKeyState( key ) & 0x8000 ) != 0;
        if ( down && !was )
            toggle = !toggle;
        was = down;
        return toggle;
    }
    const bool down = ( GetAsyncKeyState( key ) & 0x8000 ) != 0;
    was = down;
    return down;
}

auto cam_look( const math::matrix3_t& r ) -> math::vector3_t
{
    // LookVector = -ZVector (docs). Z column is (R02,R12,R22) = data[2],data[5],data[8].
    return { -r.data[2], -r.data[5], -r.data[8] };
}

auto cam_right( const math::matrix3_t& r ) -> math::vector3_t
{
    // RightVector = XVector = (R00,R10,R20)
    return { r.data[0], r.data[3], r.data[6] };
}

auto move_dir_from_cam( const math::matrix3_t& rot, bool allow_y ) -> math::vector3_t
{
    const math::vector3_t look = cam_look( rot );
    const math::vector3_t right = cam_right( rot );
    math::vector3_t dir{};

    if ( GetAsyncKeyState( 'W' ) & 0x8000 ) dir = dir + look;
    if ( GetAsyncKeyState( 'S' ) & 0x8000 ) dir = dir - look;
    if ( GetAsyncKeyState( 'A' ) & 0x8000 ) dir = dir - right;
    if ( GetAsyncKeyState( 'D' ) & 0x8000 ) dir = dir + right;
    if ( allow_y )
    {
        if ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) dir.y += 1.f;
        if ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) dir.y -= 1.f;
    }
    else
        dir.y = 0.f;

    const float len = dir.length( );
    return len > 1e-4f ? dir * ( 1.f / len ) : math::vector3_t{};
}

auto flatten_yaw( const math::vector3_t& v ) -> math::vector3_t
{
    math::vector3_t f{ v.x, 0.f, v.z };
    const float len = f.length( );
    return len > 1e-4f ? f * ( 1.f / len ) : math::vector3_t{};
}

auto rand11( ) -> float
{
    return ( ( float )( rand( ) % 1000 ) / 1000.f ) * 2.f - 1.f;
}

auto pick_hvh_target( ) -> math::vector3_t
{
    const uintptr_t prefer = combat::aim_sticky.load( )
        ? combat::aim_sticky.load( )
        : ( combat::silent_sticky.load( ) ? combat::silent_sticky.load( ) : combat::last_target.load( ) );

    auto players = g->cache.snapshot_players( );
    if ( !players )
        return math::vector3_t::nan( );

    const uintptr_t client = g->engine.client.load( );
    const uintptr_t team = g->engine.client_team.load( );
    math::vector3_t best{};
    float best_d = 1e12f;
    bool found = false;

    for ( const auto& p : *players )
    {
        if ( !p.address || p.address == client )
            continue;
        if ( prefer && p.address != prefer )
            continue;
        if ( config.aimbot.team_check && p.same_team( team ) )
            continue;
        if ( p.max_health > 0.f && p.health <= 0.f )
            continue;
        for ( const auto& b : p.bones )
        {
            if ( ( b.name == "HumanoidRootPart" || b.name == "UpperTorso" || b.name == "Torso" || b.name == "Head" )
                && b.position.is_valid( ) )
            {
                if ( !found || p.distance < best_d )
                {
                    best = b.position;
                    best_d = p.distance;
                    found = true;
                }
                break;
            }
        }
        if ( prefer && found )
            break;
    }

    if ( !found && !prefer )
    {
        for ( const auto& p : *players )
        {
            if ( !p.address || p.address == client )
                continue;
            if ( config.aimbot.team_check && p.same_team( team ) )
                continue;
            if ( p.max_health > 0.f && p.health <= 0.f )
                continue;
            for ( const auto& b : p.bones )
            {
                if ( ( b.name == "HumanoidRootPart" || b.name == "Head" ) && b.position.is_valid( ) )
                {
                    if ( !found || p.distance < best_d )
                    {
                        best = b.position;
                        best_d = p.distance;
                        found = true;
                    }
                    break;
                }
            }
        }
    }

    return found ? best : math::vector3_t::nan( );
}

auto aa_look( const math::vector3_t& pos, const math::vector3_t& cam_fwd ) -> math::vector3_t
{
    math::vector3_t flat = flatten_yaw( cam_fwd );
    if ( flat.length( ) < 1e-4f )
        flat = { 0.f, 0.f, -1.f };

    float yaw = std::atan2( flat.x, -flat.z );
    if ( config.player.antiaim_at_targets )
    {
        const math::vector3_t tgt = pick_hvh_target( );
        if ( tgt.is_valid( ) )
        {
            const math::vector3_t away = flatten_yaw( pos - tgt );
            if ( away.length( ) > 1e-4f )
                yaw = std::atan2( away.x, -away.z );
        }
    }

    const int ymode = config.player.antiaim_yaw;
    static float spin = 0.f;
    if ( ymode == 1 )
        yaw += 3.14159265f;
    else if ( ymode == 2 )
    {
        spin += config.player.antiaim_spin_speed * 0.045f;
        if ( spin > 6.2831853f )
            spin -= 6.2831853f;
        yaw = spin;
    }
    else if ( ymode == 3 )
        yaw += rand11( ) * 1.35f;
    else if ( ymode == 4 )
        yaw -= 1.5707963f;
    else if ( ymode == 5 )
        yaw += 1.5707963f;

    float pitch = 0.f;
    const int pmode = config.player.antiaim_pitch;
    if ( pmode == 1 )
        pitch = 1.15f;
    else if ( pmode == 2 )
        pitch = -1.15f;
    else if ( pmode == 3 )
        pitch = rand11( ) * 1.05f;

    const float cy = std::cos( yaw ), sy = std::sin( yaw );
    const float cp = std::cos( pitch ), sp = std::sin( pitch );
    return { sy * cp, -sp, -cy * cp };
}

auto moving_keys( ) -> bool
{
    return ( GetAsyncKeyState( 'W' ) & 0x8000 )
        || ( GetAsyncKeyState( 'A' ) & 0x8000 )
        || ( GetAsyncKeyState( 'S' ) & 0x8000 )
        || ( GetAsyncKeyState( 'D' ) & 0x8000 );
}

auto publish_hvh_vis( const hvh_vis_t& v ) -> void
{
    std::lock_guard<std::mutex> lk( g_hvh_vis_mtx );
    g_hvh_vis = v;
}

// WASD stays level with camera yaw. Space up, ctrl/shift down. Looking down no longer dives.
auto fly_move_dir( const math::matrix3_t& rot ) -> math::vector3_t
{
    const math::vector3_t look = cam_look( rot );
    const math::vector3_t right = cam_right( rot );
    math::vector3_t fwd = flatten_yaw( look );
    math::vector3_t rgt = flatten_yaw( right );
    if ( fwd.length( ) < 1e-4f )
        fwd = flatten_yaw( { right.z, 0.f, -right.x } );
    if ( rgt.length( ) < 1e-4f && fwd.length( ) > 1e-4f )
        rgt = { -fwd.z, 0.f, fwd.x };

    math::vector3_t dir{};
    if ( GetAsyncKeyState( 'W' ) & 0x8000 ) dir = dir + fwd;
    if ( GetAsyncKeyState( 'S' ) & 0x8000 ) dir = dir - fwd;
    if ( GetAsyncKeyState( 'A' ) & 0x8000 ) dir = dir - rgt;
    if ( GetAsyncKeyState( 'D' ) & 0x8000 ) dir = dir + rgt;
    if ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) dir.y += 1.f;
    if ( ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) || ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) )
        dir.y -= 1.f;

    const float len = dir.length( );
    return len > 1e-4f ? dir * ( 1.f / len ) : math::vector3_t{};
}

auto pin_fly_vel( uintptr_t prim, const math::vector3_t& vel ) -> void
{
    if ( !prim || !usermode->can_rpm( ) || !usermode->is_valid( prim ) )
        return;
    if ( !std::isfinite( vel.x ) || !std::isfinite( vel.y ) || !std::isfinite( vel.z ) )
        return;
    usermode->write<math::vector3_t>( prim + offsets::primitive::linear_velocity, vel );
    usermode->write<math::vector3_t>( prim + offsets::primitive::angular_velocity, {} );
}

auto slam_prim_pos( uintptr_t prim, const math::vector3_t& pos, int n = 1 ) -> void
{
    if ( !prim || !pos.is_valid( ) || !usermode->can_rpm( ) || !usermode->is_valid( prim ) )
        return;
    if ( !std::isfinite( pos.x ) || !std::isfinite( pos.y ) || !std::isfinite( pos.z ) )
        return;
    // 0xd4 is position. 0xe0/0xec are linear/angular velocity — never write pos there.
    const int reps = ( n < 1 ) ? 1 : ( n > 2 ? 2 : n );
    for ( int r = 0; r < reps; ++r )
        usermode->write<math::vector3_t>( prim + offsets::primitive::position, pos );
}

// Primitive CFrame is contiguous: rotation 3x3 @ 0xb0 then position @ 0xd4 (12 floats).
// Writing the assembly root CFrame moves welded parts with it (BasePart.CFrame docs).
auto write_prim_cframe( uintptr_t prim, const math::matrix3_t& rot, const math::vector3_t& pos, int repeats = 1 ) -> void
{
    if ( !prim || !pos.is_valid( ) || !usermode->can_rpm( ) || !usermode->is_valid( prim ) )
        return;
    for ( int i = 0; i < 9; ++i )
    {
        if ( !std::isfinite( rot.data[i] ) )
            return;
    }

    float block[12]{};
    for ( int i = 0; i < 9; ++i )
        block[i] = rot.data[i];
    block[9] = pos.x;
    block[10] = pos.y;
    block[11] = pos.z;

    const int reps = ( repeats < 1 ) ? 1 : ( repeats > 2 ? 2 : repeats );
    for ( int i = 0; i < reps; ++i )
    {
        if ( !usermode->can_rpm( ) )
            return;
        usermode->write_buf( prim + offsets::primitive::rotation, block, sizeof( block ) );
    }
}

auto write_prim_pos_keep_rot( uintptr_t prim, const math::vector3_t& pos, int repeats = 1 ) -> void
{
    if ( !prim || !pos.is_valid( ) )
        return;
    const math::matrix3_t rot = usermode->read<math::matrix3_t>( prim + offsets::primitive::rotation );
    write_prim_cframe( prim, rot, pos, repeats );
}

auto write_assembly_vel( uintptr_t prim, const math::vector3_t& vel, bool burst ) -> void
{
    if ( !prim || !usermode->can_rpm( ) )
        return;
    const int n = burst ? k_burst : 1;
    for ( int i = 0; i < n; ++i )
    {
        if ( !usermode->can_rpm( ) )
            return;
        // AssemblyLinearVelocity — same for every part in the assembly
        usermode->write<math::vector3_t>( prim + offsets::primitive::linear_velocity, vel );
    }
}

auto write_cframe_pos( uintptr_t prim, const math::vector3_t& pos, int repeats = 1 ) -> void
{
    write_prim_pos_keep_rot( prim, pos, repeats );
}

auto sticky_velocity( uintptr_t prim, const math::vector3_t& vel, int n = 8 ) -> void
{
    if ( !prim || !usermode->can_rpm( ) )
        return;
    for ( int i = 0; i < n; ++i )
    {
        if ( !usermode->can_rpm( ) )
            return;
        usermode->write<math::vector3_t>( prim + offsets::primitive::linear_velocity, vel );
    }
    usermode->write<math::vector3_t>( prim + offsets::primitive::angular_velocity, {} );
}

auto set_prim_anchored( uintptr_t prim, bool on ) -> void
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

auto live_local_hrp( ) -> sdk::u_base_part
{
    if ( !g->engine.players )
        return {};
    sdk::u_players service( g->engine.players.load( ) );
    sdk::u_player lp = service.local_player( );
    if ( !lp )
        lp = local_player( );
    if ( !lp )
        return {};

    sdk::u_model character = lp.character( );
    if ( !character )
        return {};

    auto try_name = [&]( const char* name ) -> sdk::u_base_part
    {
        sdk::u_base_part bp = sdk::get_child_as<sdk::u_base_part>( character, name );
        if ( !bp )
            return {};
        const uintptr_t prim = bp.primitive( );
        if ( !prim || prim < 0x10000 || !usermode->is_valid( prim ) )
            return {};
        return bp;
    };

    if ( auto p = try_name( "HumanoidRootPart" ) ) return p;
    if ( auto p = try_name( "Torso" ) ) return p;
    if ( auto p = try_name( "UpperTorso" ) ) return p;
    return local_hrp( );
}

auto live_local_humanoid( ) -> sdk::u_humanoid
{
    if ( !g->engine.players )
        return {};
    sdk::u_players service( g->engine.players.load( ) );
    sdk::u_player lp = service.local_player( );
    if ( !lp )
        lp = local_player( );
    if ( !lp )
        return {};
    sdk::u_model character = lp.character( );
    if ( !character )
        return {};
    return sdk::as<sdk::u_humanoid>( character.find_first_child_of_class( "Humanoid" ) );
}

auto resolve_camera( ) -> sdk::u_camera
{
    sdk::u_camera cam( g->engine.camera.load( ) );
    if ( cam )
        return cam;

    const uintptr_t ws = g->engine.workspace.load( );
    if ( !ws )
        return {};

    sdk::u_instance workspace( ws );
    sdk::u_instance found = workspace.find_first_child_of_class( "Camera" );
    if ( !found )
        found = workspace.find_first_child( "Camera" );
    if ( found )
    {
        g->engine.camera.store( found.address( ), std::memory_order_relaxed );
        return sdk::u_camera( found.address( ) );
    }
    return {};
}

auto fly_thread( ) -> void
{
    bool was = false;
    bool toggle = false;
    bool applied_ps = false;
    bool saved_ps = false;
    bool anchored = false;
    bool flying = false;
    auto last_log = std::chrono::steady_clock::now( );
    auto tick = std::chrono::steady_clock::now( );

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const auto now = std::chrono::steady_clock::now( );
        float dt = std::chrono::duration<float>( now - tick ).count( );
        tick = now;
        if ( dt < 0.0005f )
            dt = 0.0005f;
        if ( dt > 0.05f )
            dt = 0.016f;

        const bool want = config.player.fly
            && key_active( config.player.fly_key, config.player.fly_key_mode, was, toggle );

        if ( !want )
        {
            if ( flying || applied_ps || anchored )
            {
                sdk::u_base_part hrp = live_local_hrp( );
                if ( !hrp )
                    hrp = local_hrp( );
                const uintptr_t prim = hrp ? hrp.primitive( ) : 0;
                if ( prim && usermode->is_valid( prim ) )
                {
                    pin_fly_vel( prim, {} );
                    if ( anchored )
                    {
                        set_prim_anchored( prim, false );
                        anchored = false;
                    }
                }
                if ( applied_ps )
                {
                    auto restore_hum = [&]( sdk::u_humanoid hum )
                    {
                        hum.set_platform_stand( saved_ps );
                        const int st = hum.state( );
                        if ( st == offsets::humanoid_state::physics
                            || st == offsets::humanoid_state::flying
                            || st == offsets::humanoid_state::platform_standing )
                            hum.set_state( offsets::humanoid_state::getting_up );
                    };
                    if ( sdk::u_humanoid hum = live_local_humanoid( ) )
                        restore_hum( hum );
                    else if ( sdk::u_humanoid hum = local_humanoid( ) )
                        restore_hum( hum );
                    applied_ps = false;
                }
                flying = false;
            }
            toggle = config.player.fly ? toggle : false;
            std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 16 : 8 ) );
            continue;
        }

        if ( !usermode->can_rpm( ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            continue;
        }

        sdk::u_base_part hrp = live_local_hrp( );
        if ( !hrp )
            hrp = local_hrp( );
        if ( !hrp )
        {
            if ( std::chrono::duration_cast<std::chrono::seconds>( now - last_log ).count( ) >= 2 )
            {
                last_log = now;
                g->logger.print( "[fly] no HRP (client=0x%llx model=0x%llx)",
                    ( unsigned long long )g->engine.client.load( ),
                    ( unsigned long long )g->engine.client_model.load( ) );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            continue;
        }

        const uintptr_t prim = hrp.primitive( );
        if ( !prim || !usermode->is_valid( prim ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            continue;
        }

        flying = true;

        sdk::u_humanoid hum = live_local_humanoid( );
        if ( !hum )
            hum = local_humanoid( );
        if ( hum )
        {
            hum.set_sit( false );
            if ( !applied_ps )
            {
                saved_ps = hum.platform_stand( );
                applied_ps = true;
            }
            hum.set_platform_stand( true );
            hum.set_state( offsets::humanoid_state::physics );
            hum.set_move_direction( {} );
        }

        math::matrix3_t cam_rot{};
        sdk::u_camera cam = resolve_camera( );
        if ( cam )
            cam_rot = cam.cframe( ).rotation;
        else
            cam_rot = usermode->read<math::matrix3_t>( prim + offsets::primitive::rotation );

        const int method = config.player.fly_method;
        const math::vector3_t dir = ( method == 5 )
            ? move_dir_from_cam( cam_rot, true )
            : fly_move_dir( cam_rot );
        const float speed = ( std::max )( 1.f, config.player.fly_speed );
        const bool moving = dir.length( ) > 1e-4f;
        const math::vector3_t vel = moving ? ( dir * speed ) : math::vector3_t{};

        const math::vector3_t cur = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
        const math::vector3_t next = ( cur.is_valid( ) && moving ) ? ( cur + dir * ( speed * dt ) ) : cur;

        auto clear_anchor = [&]( )
        {
            if ( anchored )
            {
                set_prim_anchored( prim, false );
                anchored = false;
            }
        };

        if ( method == 2 || method == 4 )
        {
            set_prim_anchored( prim, true );
            anchored = true;
            if ( method == 4 )
            {
                uint8_t flags = usermode->read<uint8_t>( prim + offsets::primitive::flags );
                flags = ( uint8_t )( flags & ~offsets::primitive_flags::can_collide );
                usermode->write<uint8_t>( prim + offsets::primitive::flags, flags );
            }
            if ( next.is_valid( ) )
                write_prim_pos_keep_rot( prim, next, 1 );
            pin_fly_vel( prim, {} );
        }
        else if ( method == 3 )
        {
            clear_anchor( );
            if ( !moving && cur.is_valid( ) )
            {
                write_prim_pos_keep_rot( prim, cur, 1 );
                pin_fly_vel( prim, {} );
            }
            else
            {
                pin_fly_vel( prim, vel );
                if ( next.is_valid( ) && moving )
                    write_prim_pos_keep_rot( prim, next, 1 );
            }
        }
        else if ( method == 6 )
        {
            clear_anchor( );
            if ( cur.is_valid( ) && next.is_valid( ) && moving )
            {
                const math::vector3_t lerped = cur + ( next - cur ) * std::clamp( 8.f * dt, 0.2f, 0.85f );
                write_prim_pos_keep_rot( prim, lerped, 1 );
            }
            pin_fly_vel( prim, vel );
        }
        else if ( method == 7 )
        {
            clear_anchor( );
            pin_fly_vel( prim, vel );
            if ( next.is_valid( ) && moving )
                write_prim_pos_keep_rot( prim, next, 1 );
        }
        else
        {
            // 0 velocity, 1 velocity+cframe, 5 look fly — pos write so humanoid cannot eat vel.
            clear_anchor( );
            pin_fly_vel( prim, vel );
            if ( next.is_valid( ) )
                write_prim_pos_keep_rot( prim, next, 1 );
        }

        if ( config.settings.low_power )
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        else
            std::this_thread::yield( );
    }
}

auto infinite_jump_thread( ) -> void
{
    bool was = false;
    bool toggle = false;

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const bool want = ( config.player.infinite_jump || config.player.no_jump_cooldown )
            && key_active(
                config.player.infinite_jump ? config.player.infinite_jump_key : 0,
                config.player.infinite_jump ? config.player.infinite_jump_key_mode : ImAdd::KeybindMode_Always,
                was, toggle );

        if ( !want )
        {
            toggle = ( config.player.infinite_jump || config.player.no_jump_cooldown ) ? toggle : false;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            continue;
        }

        if ( !usermode->can_rpm( ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            continue;
        }

        if ( !( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            continue;
        }

        sdk::u_base_part hrp = live_local_hrp( );
        if ( !hrp )
            hrp = local_hrp( );
        const uintptr_t prim = hrp ? hrp.primitive( ) : 0;
        if ( !prim || !usermode->is_valid( prim ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            continue;
        }

        float power = config.player.infinite_jump_power;
        if ( power < 1.f )
            power = 50.f;
        if ( config.player.jump )
            power = ( std::max )( power, config.player.jump_power );

        if ( sdk::u_humanoid hum = live_local_humanoid( ) )
        {
            usermode->write<uint8_t>( hum.address( ) + offsets::humanoid::jump_bool, 1 );
            hum.set_state( offsets::humanoid_state::jumping );
            if ( config.player.no_jump_cooldown )
            {
                hum.set_state( offsets::humanoid_state::freefall );
                usermode->write<uint8_t>( hum.address( ) + offsets::humanoid::jump_bool, 1 );
                hum.set_state( offsets::humanoid_state::jumping );
            }
        }

        math::vector3_t vel = usermode->read<math::vector3_t>( prim + offsets::primitive::linear_velocity );
        if ( !vel.is_valid( ) )
            vel = {};
        vel.y = power;
        sticky_velocity( prim, vel, 2 );

        std::this_thread::sleep_for( std::chrono::milliseconds( config.player.no_jump_cooldown ? 1 : 2 ) );
    }
}

auto expand_part_size( uintptr_t part, const math::vector3_t& size, hitbox_vis_t* out_vis ) -> bool
{
    if ( !part || !usermode->is_valid( part ) )
        return false;
    const uintptr_t prim = usermode->read<uintptr_t>( part + offsets::basepart::primitive );
    if ( !prim || !usermode->is_valid( prim ) )
        return false;

    {
        std::lock_guard<std::mutex> lk( g_hitbox_orig_mtx );
        if ( g_hitbox_orig.find( prim ) == g_hitbox_orig.end( ) )
        {
            const math::vector3_t cur = usermode->read<math::vector3_t>( prim + offsets::primitive::size );
            if ( cur.is_valid( ) && cur.x > 0.05f && cur.y > 0.05f && cur.z > 0.05f
                && cur.x < 80.f && cur.y < 80.f && cur.z < 80.f )
                g_hitbox_orig[prim] = cur;
            else
                g_hitbox_orig[prim] = { 2.f, 2.f, 1.f };
        }
    }

    usermode->write<math::vector3_t>( prim + offsets::primitive::size, size );

    if ( out_vis )
    {
        out_vis->pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
        out_vis->rot = usermode->read<math::matrix3_t>( prim + offsets::primitive::rotation );
        out_vis->size = size;
    }
    return true;
}

auto restore_hitboxes( ) -> void
{
    std::unordered_map<uintptr_t, math::vector3_t> orig;
    {
        std::lock_guard<std::mutex> lk( g_hitbox_orig_mtx );
        orig.swap( g_hitbox_orig );
    }

    if ( !usermode->can_rpm( ) )
        return;

    for ( const auto& [prim, size] : orig )
    {
        if ( !prim || !usermode->is_valid( prim ) )
            continue;
        if ( !size.is_valid( ) )
            continue;
        for ( int i = 0; i < 4; ++i )
            usermode->write<math::vector3_t>( prim + offsets::primitive::size, size );
    }
}

auto hitbox_thread( ) -> void
{
    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        if ( !config.player.hitbox_expander )
        {
            if ( g_hitbox_was_on )
            {
                restore_hitboxes( );
                g_hitbox_was_on = false;
            }
            {
                std::lock_guard<std::mutex> lk( g_hitbox_vis_mtx );
                g_hitbox_vis.clear( );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            continue;
        }

        g_hitbox_was_on = true;

        if ( !usermode->can_rpm( ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            continue;
        }

        auto players = g->cache.snapshot_players( );
        std::vector<hitbox_vis_t> vis;
        if ( players )
        {
            const uintptr_t client = g->engine.client.load( );
            const uintptr_t client_team = g->engine.client_team.load( );
            const math::vector3_t size{
                ( std::max )( 0.5f, config.player.hitbox_size_x ),
                ( std::max )( 0.5f, config.player.hitbox_size_y ),
                ( std::max )( 0.5f, config.player.hitbox_size_z ),
            };

            for ( const auto& pl : *players )
            {
                if ( !pl.address || pl.address == client )
                    continue;
                if ( config.player.hitbox_team_check && pl.same_team( client_team ) )
                    continue;
                if ( config.player.hitbox_npc_check && pl.is_npc )
                    continue;
                if ( std::isfinite( pl.health ) && pl.health <= 0.f && pl.max_health > 5.f )
                    continue;

                if ( config.player.hitbox_root_only )
                {
                    uintptr_t part = pl.root_part;
                    if ( !part )
                    {
                        for ( const auto& b : pl.bones )
                        {
                            if ( b.name == "HumanoidRootPart" && b.part )
                            {
                                part = b.part;
                                break;
                            }
                        }
                    }
                    hitbox_vis_t v{};
                    if ( expand_part_size( part, size, config.player.hitbox_visualizer ? &v : nullptr ) )
                    {
                        if ( config.player.hitbox_visualizer && v.pos.is_valid( ) )
                            vis.push_back( v );
                    }
                }
                else
                {
                    for ( const auto& b : pl.bones )
                    {
                        if ( !b.part )
                            continue;
                        if ( b.name.rfind( "Acc:", 0 ) == 0 )
                            continue;
                        hitbox_vis_t v{};
                        if ( expand_part_size( b.part, size, config.player.hitbox_visualizer ? &v : nullptr ) )
                        {
                            if ( config.player.hitbox_visualizer && v.pos.is_valid( ) )
                                vis.push_back( v );
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk( g_hitbox_vis_mtx );
            g_hitbox_vis.swap( vis );
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 8 ) );
    }

    restore_hitboxes( );
}

auto force_walk_speed( const sdk::u_humanoid& hum, float target ) -> void
{
    if ( !hum )
        return;
    // Universal: Da Hood / many games Heartbeat-reset WalkSpeed to 16. Slam both fields hard.
    const uintptr_t base = hum.address( );
    // 0x1ec / walk_timer (0x3c8) are not WalkSpeed — writing them crashes or desyncs Humanoid.
    for ( int i = 0; i < 4; ++i )
    {
        usermode->write<float>( base + offsets::humanoid::walk_speed, target );
        usermode->write<float>( base + offsets::humanoid::walk_speed_check, target );
    }
}

// Games (Da Hood etc.) often mirror speed in a NumberValue/IntValue named WalkSpeed
// on the character or humanoid — scripts set Humanoid.WalkSpeed from that every Heartbeat.
auto pin_character_speed_values( float target ) -> void
{
    auto pin_on = [&]( const sdk::u_instance& root )
    {
        if ( !root )
            return;
        for ( const auto& child : root.get_children( ) )
        {
            const std::string name = child.name( );
            if ( name != "WalkSpeed" && name != "walkspeed" && name != "Speed" && name != "speed"
                && name != "WS" && name != "ws" )
                continue;
            const std::string cn = child.class_name( );
            if ( cn != "NumberValue" && cn != "IntValue" )
                continue;
            usermode->write<float>( child.address( ) + offsets::misc::value, target );
            if ( cn == "IntValue" )
                usermode->write<int>( child.address( ) + offsets::misc::value, ( int )target );
        }
    };

    sdk::u_model character = local_character( );
    if ( !character )
        return;
    pin_on( character );
    pin_on( character.find_first_child_of_class( "Humanoid" ) );
}

auto speed_thread( ) -> void
{
    bool was = false;
    bool toggle = false;
    bool had_speed = false;
    float saved_walk = 16.f;
    uintptr_t last_hum = 0;

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const bool want = config.player.speed
            && key_active( config.player.speed_key, config.player.speed_key_mode, was, toggle );

        // Live resolve — cached humanoid goes stale on respawn / character swap.
        sdk::u_humanoid hum = live_local_humanoid( );
        if ( !hum )
            hum = local_humanoid( );

        const uintptr_t hum_addr = hum ? hum.address( ) : 0;
        if ( hum_addr != last_hum )
        {
            had_speed = false;
            last_hum = hum_addr;
        }

        if ( !want )
        {
            if ( had_speed && hum )
                force_walk_speed( hum, saved_walk );
            had_speed = false;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            continue;
        }

        sdk::u_base_part hrp = live_local_hrp( );
        if ( !hrp )
            hrp = local_hrp( );

        if ( config.player.speed_method == 0 )
        {
            if ( hum )
            {
                if ( !had_speed )
                {
                    const float cur = hum.walk_speed( );
                    // Don't treat the game's forced 16 as "original" if we already wanted custom speed.
                    if ( cur > 0.1f && cur < 500.f && std::fabs( cur - 16.f ) > 0.05f )
                        saved_walk = cur;
                    else if ( cur > 0.1f && cur < 500.f && saved_walk <= 0.1f )
                        saved_walk = 16.f;
                    had_speed = true;
                }

                const float target = config.player.walk_speed;
                // Tight re-apply loop — outrun Heartbeat scripts that force WalkSpeed = 16.
                for ( int n = 0; n < 8; ++n )
                {
                    force_walk_speed( hum, target );
                    pin_character_speed_values( target );
                    const float cur = hum.walk_speed( );
                    if ( std::fabs( cur - target ) > 0.01f )
                        force_walk_speed( hum, target );
                }
            }
            // Yield briefly so we still beat Heartbeat without pegging a core at 100%.
            std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 2 : 0 ) );
            if ( !config.settings.low_power )
                std::this_thread::yield( );
            continue;
        }

        // CFrame / velocity methods — still pin WalkSpeed so games that clamp to 16 don't fight movement.
        if ( hum )
        {
            if ( !had_speed )
            {
                const float cur = hum.walk_speed( );
                if ( cur > 0.1f && cur < 500.f )
                    saved_walk = cur;
                had_speed = true;
            }
            force_walk_speed( hum, config.player.walk_speed );
            pin_character_speed_values( config.player.walk_speed );
        }
        else
        {
            had_speed = true;
        }

        if ( hrp )
        {
            const uintptr_t prim = hrp.primitive( );
            if ( prim )
            {
                sdk::u_camera cam = resolve_camera( );
                if ( cam )
                {
                    math::vector3_t dir = move_dir_from_cam( cam.orientation( ), false );
                    if ( dir.length( ) > 1e-4f )
                    {
                        if ( config.player.speed_method == 1 )
                        {
                            math::vector3_t cur = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                            if ( cur.is_valid( ) )
                            {
                                const float step = config.player.walk_speed / 10.f;
                                write_cframe_pos( prim, cur + dir * step, 1 );
                            }
                        }
                        else
                        {
                            math::vector3_t vel = dir * config.player.walk_speed;
                            vel.y = usermode->read<math::vector3_t>( prim + offsets::primitive::linear_velocity ).y;
                            write_assembly_vel( prim, vel, config.player.speed_method == 3 );
                            if ( hum )
                                hum.set_move_direction( dir );
                        }
                    }
                }
            }
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 6 : 3 ) );
    }
}

auto misc_thread( ) -> void
{
    bool hip_was = false, hip_tog = false, hip_on = false;
    float saved_hip = 2.f;

    bool jump_was = false, jump_tog = false, jump_on = false;
    float saved_jump = 50.f;

    bool grav_was = false, grav_tog = false, grav_on = false;
    float saved_grav = 196.2f;

    bool noclip_was = false, noclip_tog = false;
    bool sit_was = false, sit_tog = false;
    bool spin_was = false, spin_tog = false;
    bool nf_was = false, nf_tog = false;
    bool aa_was = false, aa_tog = false;
    bool ds_was = false, ds_tog = false;
    bool flg_was = false, flg_tog = false;
    bool afk_was = false, afk_tog = false;

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const bool any = config.player.jump || config.player.hip_height || config.player.gravity
            || config.player.noclip || config.player.sit
            || config.player.spin
            || config.player.no_fall || config.player.antiaim || config.player.desync || config.player.rage_tp
            || config.player.fling || config.player.antiafk || config.player.state_force
            || config.player.fake_lag;
        if ( !any )
        {
            if ( hip_on || jump_on || grav_on )
            {
                if ( sdk::u_humanoid hum = local_humanoid( ) )
                {
                    if ( hip_on )
                        hum.set_hip_height( saved_hip );
                    if ( jump_on )
                        hum.set_jump_power( saved_jump );
                }
                if ( grav_on )
                {
                    sdk::u_workspace ws( g->engine.workspace.load( ) );
                    if ( ws )
                        ws.set_gravity( saved_grav );
                }
                hip_on = jump_on = grav_on = false;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            continue;
        }

        sdk::u_humanoid hum = local_humanoid( );
        sdk::u_base_part hrp = local_hrp( );

        {
            const bool want = config.player.hip_height
                && key_active( config.player.hip_height_key, config.player.hip_height_key_mode, hip_was, hip_tog );
            if ( want && hum )
            {
                if ( !hip_on )
                {
                    const float cur = hum.hip_height( );
                    if ( std::isfinite( cur ) )
                        saved_hip = cur;
                    hip_on = true;
                }
                hum.set_hip_height( config.player.hip_height_value );
            }
            else if ( hip_on && hum )
            {
                hum.set_hip_height( saved_hip );
                hip_on = false;
            }
        }

        {
            const bool want = config.player.jump
                && key_active( config.player.jump_key, config.player.jump_key_mode, jump_was, jump_tog );
            if ( want && hum )
            {
                if ( !jump_on )
                {
                    const float cur = hum.jump_power( );
                    if ( cur > 0.1f && cur < 1000.f )
                        saved_jump = cur;
                    jump_on = true;
                }
                for ( int i = 0; i < 4; ++i )
                    hum.set_jump_power( config.player.jump_power );
            }
            else if ( jump_on && hum )
            {
                hum.set_jump_power( saved_jump );
                jump_on = false;
            }
        }

        if ( config.player.sit
            && key_active( config.player.sit_key, config.player.sit_key_mode, sit_was, sit_tog )
            && hum )
        {
            hum.set_sit( true );
            hum.set_state( offsets::humanoid_state::seated );
        }

        if ( config.player.state_force && hum )
            hum.set_state( config.player.state_force_id );

        {
            const bool want = config.player.gravity
                && key_active( config.player.gravity_key, config.player.gravity_key_mode, grav_was, grav_tog );
            sdk::u_workspace ws( g->engine.workspace.load( ) );
            if ( want && ws )
            {
                if ( !grav_on )
                {
                    const float cur = ws.gravity( );
                    if ( cur == cur && cur > 0.f )
                        saved_grav = cur;
                    grav_on = true;
                }
                ws.set_gravity( config.player.gravity_value );
            }
            else if ( grav_on && ws )
            {
                ws.set_gravity( saved_grav );
                grav_on = false;
            }
        }

        if ( config.player.noclip
            && key_active( config.player.noclip_key, config.player.noclip_key_mode, noclip_was, noclip_tog ) )
        {
            sdk::u_model character = local_character( );
            if ( character )
            {
                for ( const auto& child : character.get_children( ) )
                {
                    const std::string cn = child.class_name( );
                    if ( cn != "Part" && cn != "MeshPart" )
                        continue;
                    sdk::u_base_part bp( child.address( ) );
                    const uintptr_t prim = bp.primitive( );
                    if ( !prim )
                        continue;
                    uint8_t flags = usermode->read<uint8_t>( prim + offsets::primitive::flags );
                    if ( flags & offsets::primitive_flags::can_collide )
                    {
                        flags = ( uint8_t )( flags & ~offsets::primitive_flags::can_collide );
                        usermode->write<uint8_t>( prim + offsets::primitive::flags, flags );
                    }
                }
            }
        }

        if ( config.player.spin
            && key_active( config.player.spin_key, config.player.spin_key_mode, spin_was, spin_tog )
            && hrp )
        {
            static float spin = 0.f;
            spin += config.player.spin_speed * 0.05f;
            if ( spin > 6.2831853f )
                spin -= 6.2831853f;
            const uintptr_t prim = hrp.primitive( );
            if ( prim )
            {
                const float c = std::cos( spin );
                const float s = std::sin( spin );
                math::matrix3_t rot{};
                rot.data[0] = c;   rot.data[1] = 0.f; rot.data[2] = -s;
                rot.data[3] = 0.f; rot.data[4] = 1.f; rot.data[5] = 0.f;
                rot.data[6] = s;   rot.data[7] = 0.f; rot.data[8] = c;
                const math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                usermode->write<math::matrix3_t>( prim + offsets::primitive::rotation, rot );
                if ( pos.is_valid( ) )
                    usermode->write<math::vector3_t>( prim + offsets::primitive::position, pos );
                usermode->write<math::vector3_t>( prim + offsets::primitive::angular_velocity,
                    math::vector3_t{ 0.f, config.player.spin_speed, 0.f } );
            }
        }

        if ( config.player.no_fall
            && key_active( config.player.no_fall_key, config.player.no_fall_key_mode, nf_was, nf_tog )
            && hrp )
        {
            const uintptr_t prim = hrp.primitive( );
            if ( prim )
            {
                math::vector3_t vel = usermode->read<math::vector3_t>( prim + offsets::primitive::linear_velocity );
                if ( vel.y < -2.f )
                {
                    vel.y = 0.f;
                    write_assembly_vel( prim, vel, false );
                }
            }
        }

        hvh_vis_t vis{};
        if ( hrp )
        {
            const uintptr_t prim = hrp.primitive( );
            if ( prim )
            {
                const math::vector3_t live = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                if ( live.is_valid( ) )
                    vis.pos = live;
            }
        }

        if ( config.player.antiaim
            && key_active( config.player.antiaim_key, config.player.antiaim_key_mode, aa_was, aa_tog )
            && hrp )
        {
            const uintptr_t prim = hrp.primitive( );
            if ( prim )
            {
                math::vector3_t old = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                if ( old.is_valid( ) )
                {
                    math::vector3_t cam_fwd{ 0.f, 0.f, -1.f };
                    sdk::u_camera cam( g->engine.camera.load( ) );
                    if ( cam )
                        cam_fwd = cam_look( cam.orientation( ) );

                    const math::vector3_t look = aa_look( old, cam_fwd );
                    const math::cframe_t cf = math::cframe_look_at( old, old + look );

                    math::vector3_t write_pos = old;
                    const float j = config.player.antiaim_jitter;
                    if ( j > 0.05f )
                    {
                        write_pos.x += rand11( ) * j;
                        write_pos.y += rand11( ) * j * 0.25f;
                        write_pos.z += rand11( ) * j;
                    }

                    write_prim_cframe( prim, cf.rotation, write_pos, 1 );
                    vis.pos = write_pos;
                    vis.look = look;
                    vis.aa = true;
                }
            }
        }

        if ( config.player.desync
            && key_active( config.player.desync_key, config.player.desync_key_mode, ds_was, ds_tog )
            && hrp
            && ( !config.player.desync_on_peek || moving_keys( ) ) )
        {
            const uintptr_t prim = hrp.primitive( );
            if ( prim )
            {
                math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                math::matrix3_t rot = usermode->read<math::matrix3_t>( prim + offsets::primitive::rotation );
                if ( pos.is_valid( ) )
                {
                    const int method = config.player.desync_method;
                    float amt = config.player.desync_amount;
                    if ( config.player.desync_random )
                        amt *= 0.45f + ( ( float )( rand( ) % 1000 ) / 1000.f ) * 0.70f;

                    math::vector3_t look{ 0.f, 0.f, -1.f };
                    sdk::u_camera cam( g->engine.camera.load( ) );
                    if ( cam )
                    {
                        look = cam_look( cam.orientation( ) );
                        const float llen = look.length( );
                        if ( llen > 1e-4f )
                            look = look * ( 1.f / llen );
                    }

                    if ( method == 1 )
                    {
                        write_prim_cframe( prim, rot, pos, 1 );
                        write_assembly_vel( prim, {}, false );
                        vis.ghost = pos;
                        vis.ghost_ok = true;
                    }
                    else if ( method == 2 )
                    {
                        const math::vector3_t back = flatten_yaw( look * -1.f );
                        const math::cframe_t flip = math::cframe_look_at( pos, pos + ( back.length( ) > 1e-4f ? back : math::vector3_t{ 0.f, 0.f, 1.f } ) );
                        write_prim_cframe( prim, flip.rotation, pos, 1 );
                        vis.ghost = pos;
                        vis.ghost_ok = true;
                    }
                    else
                    {
                        const math::vector3_t ghost{
                            pos.x - look.x * amt,
                            pos.y + amt * 0.12f,
                            pos.z - look.z * amt
                        };
                        write_prim_cframe( prim, rot, ghost, 1 );
                        write_assembly_vel( prim, {}, false );
                        vis.ghost = ghost;
                        vis.ghost_ok = true;
                        std::this_thread::sleep_for( std::chrono::milliseconds( 6 ) );
                        write_prim_cframe( prim, rot, pos, 1 );
                    }
                }
            }
        }

        if ( config.player.fake_lag
            && key_active( config.player.fake_lag_key, config.player.fake_lag_key_mode, flg_was, flg_tog )
            && hrp )
        {
            const uintptr_t prim = hrp.primitive( );
            if ( prim )
            {
                static math::vector3_t frozen{};
                static auto hold_at = std::chrono::steady_clock::now( );
                static bool holding = false;

                math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                if ( pos.is_valid( ) )
                {
                    const auto now = std::chrono::steady_clock::now( );
                    const float hold = config.player.fake_lag_hold_ms;
                    if ( !holding )
                    {
                        frozen = pos;
                        holding = true;
                        hold_at = now;
                    }

                    const float elapsed = ( float )std::chrono::duration_cast<std::chrono::milliseconds>( now - hold_at ).count( );
                    if ( elapsed <= hold )
                    {
                        math::vector3_t look{ 0.f, 0.f, -1.f };
                        sdk::u_camera cam( g->engine.camera.load( ) );
                        if ( cam )
                            look = flatten_yaw( cam_look( cam.orientation( ) ) );
                        const math::vector3_t ghost{
                            frozen.x - look.x * config.player.fake_lag_amount,
                            frozen.y,
                            frozen.z - look.z * config.player.fake_lag_amount
                        };
                        const math::matrix3_t rot = usermode->read<math::matrix3_t>( prim + offsets::primitive::rotation );
                        write_prim_cframe( prim, rot, ghost, 1 );
                        write_assembly_vel( prim, {}, false );
                        vis.ghost = ghost;
                        vis.ghost_ok = true;
                    }
                    else if ( elapsed > hold + 45.f )
                    {
                        holding = false;
                    }
                }
            }
        }

        if ( vis.pos.is_valid( ) && ( vis.aa || vis.ghost_ok ) )
            publish_hvh_vis( vis );
        else if ( !config.player.antiaim && !config.player.desync && !config.player.fake_lag )
            publish_hvh_vis( {} );

        if ( config.player.rage_tp && hrp )
        {
            static bool rtp_was = false;
            static bool rtp_toggle = false;
            static bool underground = false;
            static auto last_cycle = std::chrono::steady_clock::now( );

            if ( key_active( config.player.rage_tp_key, config.player.rage_tp_key_mode, rtp_was, rtp_toggle ) )
            {
                const uintptr_t prefer = combat::aim_sticky.load( )
                    ? combat::aim_sticky.load( )
                    : ( combat::silent_sticky.load( ) ? combat::silent_sticky.load( ) : combat::last_target.load( ) );

                math::vector3_t target{};
                bool found = false;
                auto players = g->cache.snapshot_players( );
                if ( players && prefer )
                {
                    for ( const auto& p : *players )
                    {
                        if ( p.address != prefer )
                            continue;
                        for ( const auto& b : p.bones )
                        {
                            if ( ( b.name == "Head" || b.name == "HumanoidRootPart" || b.name == "UpperTorso" )
                                && b.position.is_valid( ) )
                            {
                                target = b.position;
                                found = true;
                                break;
                            }
                        }
                        break;
                    }
                }

                if ( found )
                {
                    const auto now = std::chrono::steady_clock::now( );
                    if ( std::chrono::duration_cast<std::chrono::milliseconds>( now - last_cycle ).count( ) >= 1000 )
                    {
                        underground = !underground;
                        last_cycle = now;
                    }

                    const float dy = underground ? -config.player.rage_tp_under : -config.player.rage_tp_depth;
                    const math::vector3_t tp{ target.x, target.y + dy, target.z };
                    const uintptr_t prim = hrp.primitive( );
                    if ( prim )
                    {
                        write_cframe_pos( prim, tp, 1 );
                        write_assembly_vel( prim, {}, false );
                    }

                    static auto last_click = std::chrono::steady_clock::now( );
                    if ( std::chrono::duration_cast<std::chrono::milliseconds>( now - last_click ).count( ) >= 180 )
                    {
                        last_click = now;
                        INPUT in{};
                        in.type = INPUT_MOUSE;
                        in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
                        SendInput( 1, &in, sizeof( in ) );
                    }
                }
            }
        }

        if ( config.player.fling && hrp )
        {
            static bool fl_was = false;
            static bool fl_toggle = false;
            if ( key_active( config.player.fling_key, config.player.fling_key_mode, fl_was, fl_toggle ) )
            {
                const uintptr_t prim = hrp.primitive( );
                if ( prim )
                {
                    const float pwr = std::clamp( config.player.fling_power, 1.f, 400.f );
                    const math::vector3_t vel{
                        ( ( float )( rand( ) % 2000 ) - 1000.f ) / 1000.f * pwr,
                        pwr * 0.35f,
                        ( ( float )( rand( ) % 2000 ) - 1000.f ) / 1000.f * pwr
                    };
                    write_assembly_vel( prim, vel, false );
                    usermode->write<math::vector3_t>( prim + offsets::primitive::angular_velocity,
                        math::vector3_t{ 0.f, pwr * 0.15f, 0.f } );
                }
            }
        }

        if ( config.player.antiafk
            && key_active( config.player.antiafk_key, config.player.antiafk_key_mode, afk_was, afk_tog ) )
        {
            static auto last_afk = std::chrono::steady_clock::now( );
            const auto now = std::chrono::steady_clock::now( );
            if ( std::chrono::duration_cast<std::chrono::seconds>( now - last_afk ).count( ) >= 30 )
            {
                last_afk = now;
                INPUT in{};
                in.type = INPUT_MOUSE;
                in.mi.dx = 1;
                in.mi.dy = 0;
                in.mi.dwFlags = MOUSEEVENTF_MOVE;
                SendInput( 1, &in, sizeof( in ) );
                in.mi.dx = -1;
                SendInput( 1, &in, sizeof( in ) );
            }
        }

        std::this_thread::sleep_for( std::chrono::milliseconds(
            ( config.player.rage_tp || config.player.antiaim
                || config.player.desync || config.player.fake_lag )
                ? ( config.settings.low_power ? 5 : 3 )
                : ( config.settings.low_power ? 20 : 10 ) ) );
    }
}

auto freecam_thread( ) -> void
{
    bool was = false;
    bool toggle = false;
    bool active = false;
    int saved_cam_type = 0;
    math::vector3_t cam_pos{};
    math::matrix3_t cam_rot{};
    float pitch = 0.f;
    float yaw = 0.f;

    auto make_rot = []( float p, float y ) -> math::matrix3_t
    {
        const float cp = std::cos( p ), sp = std::sin( p );
        const float cy = std::cos( y ), sy = std::sin( y );
        math::matrix3_t r{};
        r.data[0] = cy;           r.data[1] = sy * sp;  r.data[2] = sy * cp;
        r.data[3] = 0.f;          r.data[4] = cp;       r.data[5] = -sp;
        r.data[6] = -sy;          r.data[7] = cy * sp;  r.data[8] = cy * cp;
        return r;
    };

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const bool want = config.player.freecam
            && key_active( config.player.freecam_key, config.player.freecam_key_mode, was, toggle )
            && !config.menu_open;

        sdk::u_camera cam( g->engine.camera.load( ) );
        sdk::u_base_part hrp = local_hrp( );

        if ( want && cam )
        {
            if ( !active )
            {
                saved_cam_type = cam.camera_type( );
                cam.set_camera_type( offsets::camera_type::scriptable );
                cam_pos = cam.position( );
                cam_rot = cam.orientation( );
                if ( hrp )
                {
                    const uintptr_t prim = hrp.primitive( );
                    if ( prim )
                    {
                        uint8_t flags = usermode->read<uint8_t>( prim + offsets::primitive::flags );
                        flags = ( uint8_t )( flags | offsets::primitive_flags::anchored );
                        usermode->write<uint8_t>( prim + offsets::primitive::flags, flags );
                    }
                }
                active = true;
            }

            static POINT last_m{};
            POINT m{};
            GetCursorPos( &m );
            if ( last_m.x || last_m.y )
            {
                yaw -= ( m.x - last_m.x ) * 0.002f;
                pitch -= ( m.y - last_m.y ) * 0.002f;
                pitch = std::clamp( pitch, -1.5f, 1.5f );
            }
            last_m = m;
            cam_rot = make_rot( pitch, yaw );

            math::vector3_t dir = move_dir_from_cam( cam_rot, true );
            if ( GetAsyncKeyState( VK_LSHIFT ) & 0x8000 )
                dir.y -= 1.f;
            if ( dir.length( ) > 1e-4f )
                dir = dir * ( 1.f / dir.length( ) );
            const float mul = ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 ) ? 2.f : 1.f;
            cam_pos = cam_pos + dir * ( config.player.freecam_speed * mul / 100.f );

            for ( int i = 0; i < 4; ++i )
            {
                cam.set_position( cam_pos );
                cam.set_orientation( cam_rot );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 6 : 3 ) );
        }
        else
        {
            if ( active && cam )
            {
                cam.set_camera_type( saved_cam_type );
                if ( hrp )
                {
                    const uintptr_t prim = hrp.primitive( );
                    if ( prim )
                    {
                        uint8_t flags = usermode->read<uint8_t>( prim + offsets::primitive::flags );
                        flags = ( uint8_t )( flags & ~offsets::primitive_flags::anchored );
                        usermode->write<uint8_t>( prim + offsets::primitive::flags, flags );
                    }
                }
                active = false;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
    }
}

}

auto target_strafe_thread( ) -> void
{
    bool was = false;
    bool toggle = false;
    uintptr_t locked = 0;
    auto orbit_t0 = std::chrono::steady_clock::now( );
    bool running = false;

    auto part_pos = []( uintptr_t part ) -> math::vector3_t
    {
        if ( !part || !usermode->is_valid( part ) )
            return math::vector3_t::nan( );
        const uintptr_t prim = usermode->read<uintptr_t>( part + offsets::basepart::primitive );
        if ( !prim || !usermode->is_valid( prim ) )
            return math::vector3_t::nan( );
        const math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
        if ( pos.is_valid( ) && std::isfinite( pos.x ) && std::isfinite( pos.y ) && std::isfinite( pos.z ) )
            return pos;
        return math::vector3_t::nan( );
    };

    auto matches = []( const cache::player& p, uintptr_t id ) -> bool
    {
        return id && ( p.address == id || p.character == id || p.root_part == id );
    };

    auto usable = []( const cache::player& p ) -> bool
    {
        const uintptr_t client = g->engine.client.load( );
        if ( ( !p.address && !p.character ) || p.address == client || p.character == client )
            return false;
        if ( p.health_nv && p.max_health > 1.f && p.health <= 0.f )
            return false;
        return true;
    };

    auto center_of = [&]( const cache::player& p ) -> math::vector3_t
    {
        math::vector3_t pos = part_pos( p.root_part );
        if ( pos.is_valid( ) )
            return pos;
        for ( const auto& b : p.bones )
        {
            pos = part_pos( b.part );
            if ( pos.is_valid( ) )
                return pos;
            if ( b.position.is_valid( ) )
                return b.position;
        }
        return math::vector3_t::nan( );
    };

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const bool want = config.player.target_strafe
            && key_active( config.player.target_strafe_key, config.player.target_strafe_key_mode, was, toggle );

        if ( !want || !usermode->can_rpm( ) )
        {
            running = false;
            locked = 0;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            continue;
        }

        sdk::u_base_part hrp = live_local_hrp( );
        if ( !hrp )
            hrp = local_hrp( );
        const uintptr_t prim = hrp ? hrp.primitive( ) : 0;
        if ( !prim || !usermode->is_valid( prim ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            continue;
        }

        const uintptr_t prefer = config.player.target_strafe_from_aim
            ? ( combat::aim_sticky.load( ) ? combat::aim_sticky.load( )
                : ( combat::silent_sticky.load( ) ? combat::silent_sticky.load( ) : combat::last_target.load( ) ) )
            : 0;

        math::vector3_t center = math::vector3_t::nan( );
        if ( auto players = g->cache.snapshot_players( ) )
        {
            float best_d = 1e12f;
            uintptr_t picked_id = 0;

            auto try_pick = [&]( const cache::player& p ) -> bool
            {
                if ( !usable( p ) )
                    return false;
                const math::vector3_t c = center_of( p );
                if ( !c.is_valid( ) )
                    return false;
                center = c;
                picked_id = p.address ? p.address : p.character;
                return true;
            };

            if ( prefer )
            {
                for ( const auto& p : *players )
                {
                    if ( matches( p, prefer ) && try_pick( p ) )
                        break;
                }
            }
            if ( !center.is_valid( ) && locked )
            {
                for ( const auto& p : *players )
                {
                    if ( matches( p, locked ) && try_pick( p ) )
                        break;
                }
            }
            if ( !center.is_valid( ) )
            {
                for ( const auto& p : *players )
                {
                    if ( !usable( p ) )
                        continue;
                    const math::vector3_t c = center_of( p );
                    if ( !c.is_valid( ) )
                        continue;
                    if ( p.distance < best_d )
                    {
                        best_d = p.distance;
                        center = c;
                        picked_id = p.address ? p.address : p.character;
                    }
                }
            }
            if ( picked_id )
                locked = picked_id;
        }

        if ( !center.is_valid( ) )
        {
            running = false;
            locked = 0;
            std::this_thread::sleep_for( std::chrono::milliseconds( 8 ) );
            continue;
        }

        if ( !running )
        {
            orbit_t0 = std::chrono::steady_clock::now( );
            running = true;
        }

        const float t = std::chrono::duration<float>( std::chrono::steady_clock::now( ) - orbit_t0 ).count( );
        const float r = ( std::max )( 1.f, config.player.target_strafe_radius );
        const float angle = config.player.target_strafe_speed * t;
        const math::vector3_t next{
            center.x + std::cos( angle ) * r,
            center.y + config.player.target_strafe_height,
            center.z + std::sin( angle ) * r
        };
        if ( !next.is_valid( )
            || !std::isfinite( next.x ) || !std::isfinite( next.y ) || !std::isfinite( next.z )
            || !usermode->is_valid( prim ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 4 ) );
            continue;
        }

        slam_prim_pos( prim, next, 1 );
        std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 4 : 2 ) );
    }
}

auto start( ) -> void
{
    if ( g_alive.exchange( true ) )
        return;
    g_fly_thread = std::thread( fly_thread );
    g_speed_thread = std::thread( speed_thread );
    g_misc_thread = std::thread( misc_thread );
    g_strafe_thread = std::thread( target_strafe_thread );
    g_hvh_thread = std::thread( freecam_thread );
    g_infjump_thread = std::thread( infinite_jump_thread );
    g_hitbox_thread = std::thread( hitbox_thread );
}

auto stop( ) -> void
{
    if ( !g_alive.exchange( false ) )
        return;
    if ( g_fly_thread.joinable( ) )
        g_fly_thread.join( );
    if ( g_speed_thread.joinable( ) )
        g_speed_thread.join( );
    if ( g_misc_thread.joinable( ) )
        g_misc_thread.join( );
    if ( g_strafe_thread.joinable( ) )
        g_strafe_thread.join( );
    if ( g_hvh_thread.joinable( ) )
        g_hvh_thread.join( );
    if ( g_infjump_thread.joinable( ) )
        g_infjump_thread.join( );
    if ( g_hitbox_thread.joinable( ) )
        g_hitbox_thread.join( );
}

auto run( ) -> void
{
}

auto draw( ) -> void
{
    const math::vector2_t dims = sdk::u_visualengine::dimensions( );
    const math::matrix4_t vm = sdk::u_visualengine::view_matrix( );
    const math::vector2_t off = sdk::u_visualengine::client_offset( );
    ImDrawList* dl = ImGui::GetForegroundDrawList( );

    if ( config.player.antiaim_arrows || config.player.desync || config.player.fake_lag )
    {
        hvh_vis_t vis{};
        {
            std::lock_guard<std::mutex> lk( g_hvh_vis_mtx );
            vis = g_hvh_vis;
        }

        auto project = [&]( const math::vector3_t& world, ImVec2& out ) -> bool
        {
            return project_world( world, dims, vm, off, out );
        };

        if ( vis.aa && config.player.antiaim_arrows && vis.pos.is_valid( ) && vis.look.is_valid( ) )
        {
            ImVec2 a{}, b{};
            const math::vector3_t tip = vis.pos + vis.look * 5.f;
            if ( project( vis.pos, a ) && project( tip, b ) )
            {
                dl->AddLine( a, b, IM_COL32( 255, 70, 90, 230 ), 2.2f );
                const ImVec2 d{ b.x - a.x, b.y - a.y };
                const float len = std::sqrt( d.x * d.x + d.y * d.y );
                if ( len > 4.f )
                {
                    const ImVec2 n{ d.x / len, d.y / len };
                    const ImVec2 p{ -n.y, n.x };
                    dl->AddTriangleFilled(
                        b,
                        ImVec2( b.x - n.x * 8.f + p.x * 4.f, b.y - n.y * 8.f + p.y * 4.f ),
                        ImVec2( b.x - n.x * 8.f - p.x * 4.f, b.y - n.y * 8.f - p.y * 4.f ),
                        IM_COL32( 255, 70, 90, 230 ) );
                }
            }
        }

        if ( vis.ghost_ok && vis.ghost.is_valid( ) )
        {
            ImVec2 ghost_pt{};
            if ( project( vis.ghost, ghost_pt ) )
            {
                dl->AddCircleFilled( ghost_pt, 4.f, IM_COL32( 90, 200, 255, 200 ) );
                dl->AddCircle( ghost_pt, 7.f, IM_COL32( 90, 200, 255, 160 ), 12, 1.4f );
                if ( vis.pos.is_valid( ) )
                {
                    ImVec2 a{};
                    if ( project( vis.pos, a ) )
                        dl->AddLine( a, ghost_pt, IM_COL32( 90, 200, 255, 90 ), 1.2f );
                }
            }
        }
    }

    if ( config.player.hitbox_expander && config.player.hitbox_visualizer )
    {
        std::vector<hitbox_vis_t> boxes;
        {
            std::lock_guard<std::mutex> lk( g_hitbox_vis_mtx );
            boxes = g_hitbox_vis;
        }

        if ( !boxes.empty( ) )
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList( );
            const math::vector2_t dims = sdk::u_visualengine::dimensions( );
            const math::matrix4_t vm = sdk::u_visualengine::view_matrix( );
            const math::vector2_t off = sdk::u_visualengine::client_offset( );
            const ImU32 col = IM_COL32(
                ( int )( config.player.hitbox_color[0] * 255.f ),
                ( int )( config.player.hitbox_color[1] * 255.f ),
                ( int )( config.player.hitbox_color[2] * 255.f ),
                ( int )( config.player.hitbox_color[3] * 255.f ) );

            auto project = [&]( const math::vector3_t& world, ImVec2& out ) -> bool
            {
                return project_world( world, dims, vm, off, out );
            };

            for ( const auto& box : boxes )
            {
                if ( !box.pos.is_valid( ) )
                    continue;
                const math::vector3_t half{ box.size.x * 0.5f, box.size.y * 0.5f, box.size.z * 0.5f };
                const math::vector3_t local[8] = {
                    { -half.x, -half.y, -half.z }, { -half.x, -half.y,  half.z },
                    { -half.x,  half.y, -half.z }, { -half.x,  half.y,  half.z },
                    {  half.x, -half.y, -half.z }, {  half.x, -half.y,  half.z },
                    {  half.x,  half.y, -half.z }, {  half.x,  half.y,  half.z },
                };
                ImVec2 pts[8]{};
                bool ok[8]{};
                for ( int i = 0; i < 8; ++i )
                {
                    const math::vector3_t r = math::mat3_mul_vec3( box.rot, local[i] );
                    const math::vector3_t w{ box.pos.x + r.x, box.pos.y + r.y, box.pos.z + r.z };
                    ok[i] = project( w, pts[i] );
                }
                static constexpr int edges[12][2] = {
                    {0,1},{0,2},{0,4},{1,3},{1,5},{2,3},{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}
                };
                for ( const auto& e : edges )
                {
                    if ( ok[e[0]] && ok[e[1]] )
                        dl->AddLine( pts[e[0]], pts[e[1]], col, 1.5f );
                }
            }
        }
    }
}

}
