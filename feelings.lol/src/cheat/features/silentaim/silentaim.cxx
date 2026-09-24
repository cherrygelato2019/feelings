#include <include/global.cuh>

#include <src/cheat/features/aimbot/helpers.hxx>
#include <src/cheat/features/combat/combat_state.cuh>
#include <src/cheat/features/silentaim/silentaim.cuh>
#include <src/cheat/features/silentaim/raycast_hook.cuh>
#include <src/cheat/features/silentaim/mouse_hit.cuh>
#include <src/cheat/features/stealth/stealth.hxx>
#include <src/engine/raycast/engine_ray.hxx>
#include <src/engine/cache/games/games.cuh>

#include <chrono>
#include <cfloat>
#include <cmath>
#include <limits>
#include <algorithm>
#include <random>

namespace
{

struct lock_state_t
{
    uintptr_t sticky = 0;
    bool locked = false;
    bool camera_overridden = false;
    int camera_type_saved = 0;
    uintptr_t camera_addr = 0;
    uintptr_t subject_saved = 0;
    bool spectating = false;
    std::chrono::steady_clock::time_point release_at{};
    bool pending_restore = false;
};

struct draw_state_t
{
    ImVec2 aim{};
    ImVec2 cursor{};
    bool ok = false;
};

draw_state_t g_draw{};

auto clear_sticky( lock_state_t& st ) -> void
{
    st.sticky = 0;
    st.locked = false;
    combat::silent_sticky.store( 0, std::memory_order_relaxed );
}

auto restore_spectate( lock_state_t& st, sdk::u_camera cam ) -> void
{
    if ( !st.spectating || !cam )
        return;
    if ( st.subject_saved )
        cam.set_camera_subject( st.subject_saved );
    st.spectating = false;
    st.subject_saved = 0;
}

auto restore_camera( lock_state_t& st ) -> void
{
    if ( st.camera_overridden && st.camera_addr )
    {
        sdk::u_camera( st.camera_addr ).set_camera_type( st.camera_type_saved );
        st.camera_overridden = false;
        st.camera_addr = 0;
    }
    st.pending_restore = false;
}

auto passes_filters( const cache::player& player, uintptr_t client, uintptr_t client_team, int64_t local_uid, bool skip_npc = false ) -> bool
{
    if ( player.address == 0 || player.address == client )
        return false;
    if ( !player.character && player.bones.empty( ) )
        return false;
    if ( config.silentaim.team_check && ( player.teammate || player.same_team( client_team ) ) )
        return false;
    if ( config.silentaim.friend_check && local_uid > 0 && player.user_id > 0 && sdk::is_friend( local_uid, player.user_id ) )
        return false;
    if ( !skip_npc && config.silentaim.npc_check && player.is_npc )
        return false;
    if ( player.max_health >= 20.f && player.max_health <= 10000.f
        && player.health >= 0.f && player.health < 0.5f )
        return false;
    if ( config.silentaim.max_distance > 0.f && player.distance > config.silentaim.max_distance
        && std::isfinite( player.distance ) && player.distance > 1.f )
        return false;
    return true;
}

auto score_target( float dsq, float distance, float health, float max_health ) -> float
{
    switch ( config.silentaim.target_mode )
    {
    case 1: return distance;
    case 2: return health;
    case 3: return max_health > 0.f ? ( max_health - health ) : 0.f;
    case 0:
    default: return dsq;
    }
}

auto silent_range_px( float camera_fov_deg ) -> float
{
    float range = config.silentaim.range;
    float fov = camera_fov_deg;
    if ( fov > 0.01f && fov < 3.5f )
        fov *= 57.2957795f;
    constexpr float k_ref_fov = 70.f;
    if ( fov > 1.f && fov < k_ref_fov )
        range *= std::clamp( k_ref_fov / fov, 1.f, 10.f );
    return range;
}

float& comp_ref( math::vector3_t& v, int i )
{
    return i == 0 ? v.x : ( i == 1 ? v.y : v.z );
}

float comp_val( const math::vector3_t& v, int i )
{
    return i == 0 ? v.x : ( i == 1 ? v.y : v.z );
}

auto closest_point_on_bone(
    const cache::player::bone& bone,
    const math::vector3_t& world_pos,
    const math::matrix3_t& rot,
    const math::vector3_t& size,
    const math::vector2_t& dimensions,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    const ImVec2& cursor,
    float scale ) -> math::vector3_t
{
    math::vector3_t best = world_pos;
    float best_dsq = FLT_MAX;

    const float shrink = 1.f - scale;
    if ( shrink < 0.001f || !size.is_valid( ) )
        return best;

    const math::vector3_t sz{ size.x * shrink, size.y * shrink, size.z * shrink };
    const math::vector3_t half{ sz.x * 0.5f, sz.y * 0.5f, sz.z * 0.5f };

    struct face_t { int n; float sign; int t1; int t2; };
    static constexpr face_t faces[6] = {
        { 0,  1.f, 1, 2 }, { 0, -1.f, 1, 2 },
        { 1,  1.f, 0, 2 }, { 1, -1.f, 0, 2 },
        { 2,  1.f, 0, 1 }, { 2, -1.f, 0, 1 },
    };

    constexpr int n = 10;
    const float step = 1.f / ( float )( n - 1 );

    for ( const auto& face : faces )
    {
        for ( int i = 0; i < n; ++i )
        {
            for ( int j = 0; j < n; ++j )
            {
                const float u = i * step;
                const float v = j * step;

                math::vector3_t local{};
                comp_ref( local, face.n ) = face.sign * comp_val( half, face.n );
                comp_ref( local, face.t1 ) = ( u * comp_val( sz, face.t1 ) ) - comp_val( half, face.t1 );
                comp_ref( local, face.t2 ) = ( v * comp_val( sz, face.t2 ) ) - comp_val( half, face.t2 );

                const math::vector3_t rotated = math::mat3_mul_vec3( rot, local );
                const math::vector3_t world{
                    world_pos.x + rotated.x,
                    world_pos.y + rotated.y,
                    world_pos.z + rotated.z,
                };

                ImVec2 sp{};
                if ( !project_world( world, dimensions, viewmatrix, offset, sp ) )
                    continue;

                const float dx = cursor.x - sp.x;
                const float dy = cursor.y - sp.y;
                const float dsq = dx * dx + dy * dy;
                if ( dsq < best_dsq )
                {
                    best_dsq = dsq;
                    best = world;
                }
            }
        }
    }

    ( void )bone;
    return best;
}

struct aim_sample_t
{
    math::vector3_t world{};
    ImVec2 screen{};
    math::vector3_t vel{};
    bool valid = false;
};

auto world_of( const cache::player& player ) -> math::vector3_t
{
    math::vector3_t w = aimbot_helpers::head_aim_world( player );
    if ( w.is_valid( ) )
        return w;

    const cache::player::bone* bone = aimbot_helpers::resolve_hitbox( player, 0 );
    if ( !bone )
        bone = aimbot_helpers::resolve_hitbox( player, 3 );
    if ( bone )
    {
        const cache::bone_pose_t pose = cache::read_bone_pose( *bone );
        if ( pose.valid && pose.position.is_valid( ) )
            return pose.position;
        if ( bone->position.is_valid( ) )
            return bone->position;
    }

    for ( const auto& b : player.bones )
    {
        if ( b.name.rfind( "Acc:", 0 ) == 0 )
            continue;
        if ( b.position.is_valid( ) )
            return b.position;
    }
    return math::vector3_t::nan( );
}

auto sample_player_aim(
    const cache::player& player,
    const math::vector2_t& dimensions,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    const ImVec2& cursor,
    const math::vector3_t& cam_pos,
    const std::shared_ptr<std::vector<cache::world_part>>& world,
    bool ignore_range,
    float range_px ) -> aim_sample_t
{
    aim_sample_t out{};
    const cache::player::bone* bone = nullptr;
    math::vector3_t size{};
    math::matrix3_t rot{};

    if ( config.silentaim.aim_part == 4 )
    {
        if ( !aimbot_helpers::nearest_bone_to_cursor( player, dimensions, viewmatrix, offset, cursor, out.world, out.screen ) )
        {
            out.world = world_of( player );
            out.screen = { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };
            if ( !out.world.is_valid( ) )
                return out;
        }

        float best = FLT_MAX;
        for ( const auto& b : player.bones )
        {
            if ( b.name.rfind( "Acc:", 0 ) == 0 || b.name == "HumanoidRootPart" )
                continue;
            math::vector3_t world_pos = b.position;
            if ( b.part )
            {
                const cache::bone_pose_t pose = cache::read_bone_pose( b );
                if ( pose.valid )
                    world_pos = pose.position;
            }
            if ( !world_pos.is_valid( ) )
                continue;
            ImVec2 sp{};
            if ( !project_world( world_pos, dimensions, viewmatrix, offset, sp ) )
                continue;
            const float dx = sp.x - cursor.x;
            const float dy = sp.y - cursor.y;
            const float dsq = dx * dx + dy * dy;
            if ( dsq < best )
            {
                best = dsq;
                bone = &b;
                size = b.size;
                rot = b.rotation;
                out.vel = b.velocity;
            }
        }
    }
    else if ( config.silentaim.aim_part == 0 )
    {
        out.world = aimbot_helpers::head_aim_world( player );
        if ( !out.world.is_valid( ) )
        {
            bone = aimbot_helpers::resolve_hitbox( player, 0 );
            if ( bone )
            {
                const cache::bone_pose_t pose = cache::read_bone_pose( *bone );
                out.world = pose.valid ? pose.position : bone->position;
            }
            if ( !out.world.is_valid( ) )
                out.world = world_of( player );
            if ( !out.world.is_valid( ) )
                return out;
        }
        else
        {
            bone = aimbot_helpers::resolve_hitbox( player, 0 );
        }
        if ( bone )
        {
            size = bone->size;
            rot = bone->rotation;
            out.vel = bone->velocity;
        }
        if ( !project_world( out.world, dimensions, viewmatrix, offset, out.screen ) )
            out.screen = { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };
    }
    else
    {
        bone = aimbot_helpers::resolve_hitbox( player, config.silentaim.aim_part );
        if ( bone )
        {
            const cache::bone_pose_t pose = cache::read_bone_pose( *bone );
            out.world = pose.valid ? pose.position : bone->position;
            size = bone->size;
            rot = bone->rotation;
            out.vel = bone->velocity;
        }
        if ( !out.world.is_valid( ) )
            out.world = world_of( player );
        if ( !out.world.is_valid( ) )
            return out;
        if ( !project_world( out.world, dimensions, viewmatrix, offset, out.screen ) )
            out.screen = { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };
    }

    if ( bone )
    {
        const cache::bone_pose_t pose = cache::read_bone_pose( *bone );
        const math::vector3_t hp_pos = pose.valid ? pose.position : out.world;
        const math::matrix3_t hp_rot = pose.valid ? pose.rotation : rot;
        const math::vector3_t hp_size = pose.valid && pose.size.is_valid( ) ? pose.size : size;
        if ( hp_pos.is_valid( ) && hp_size.is_valid( )
            && hp_size.x > 0.05f && hp_size.y > 0.05f && hp_size.z > 0.05f )
        {
            const float scale = config.silentaim.closest_point
                ? config.silentaim.closest_point_scale : 0.72f;
            const math::vector3_t snapped = closest_point_on_bone(
                *bone, hp_pos, hp_rot, hp_size, dimensions, viewmatrix, offset, cursor, scale );
            ImVec2 sp{};
            if ( snapped.is_valid( ) && project_world( snapped, dimensions, viewmatrix, offset, sp ) )
            {
                if ( !config.silentaim.wall_check || config.silentaim.wallbang || config.silentaim.magic_bullet
                    || !world || !cam_pos.is_valid( )
                    || engine_ray::visible( cam_pos, snapped, *world ) )
                {
                    out.world = snapped;
                    out.screen = sp;
                }
            }
        }
    }

    if ( config.silentaim.prediction && cam_pos.is_valid( ) )
    {
        if ( !bone )
            bone = aimbot_helpers::resolve_hitbox( player, 0 );
        math::vector3_t vel = out.vel;
        if ( bone && ( !vel.is_valid( ) || vel.length( ) < 1e-4f ) )
            vel = bone->velocity;
        const math::vector3_t led = math::lead_target( cam_pos, out.world, vel, 1.f, 1.01f, 0.f );
        ImVec2 sp{};
        if ( led.is_valid( ) )
        {
            out.world = led;
            if ( project_world( led, dimensions, viewmatrix, offset, sp ) )
                out.screen = sp;
        }
    }

    if ( config.silentaim.wall_check && !config.silentaim.wallbang && !config.silentaim.magic_bullet
        && world && cam_pos.is_valid( ) )
    {
        if ( !engine_ray::visible( cam_pos, out.world, *world ) )
            return out;
    }

    const bool on_screen = std::isfinite( out.screen.x ) && std::isfinite( out.screen.y );
    if ( !ignore_range )
    {
        if ( !on_screen )
            return out;
        const float dx = out.screen.x - cursor.x;
        const float dy = out.screen.y - cursor.y;
        if ( dx * dx + dy * dy >= range_px * range_px )
            return out;
    }

    if ( !out.world.is_valid( ) )
        return out;

    out.valid = true;
    return out;
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
    if ( found )
        return sdk::u_camera( found.address( ) );
    return {};
}

auto write_raycast( const math::vector3_t& aim, const math::vector3_t& cam_pos ) -> void
{
    if ( !aim.is_valid( ) )
        return;

    math::vector3_t target = aim;
    if ( !config.silentaim.no_spread )
    {
        static std::mt19937 rng{ std::random_device{}( ) };
        static std::uniform_real_distribution<float> dist( -0.08f, 0.08f );
        target.x += dist( rng );
        target.y += dist( rng );
        target.z += dist( rng );
    }

    if ( raycast_hook::ensure( ) )
        raycast_hook::aim( target, cam_pos, config.silentaim.wallbang || config.silentaim.magic_bullet );
}

auto write_viewport( const math::vector3_t&, const math::vector3_t&, float screen_x, float screen_y ) -> void
{
    if ( viewport_hook::ensure( ) )
        viewport_hook::redirect( {}, {}, screen_x, screen_y );
}

auto write_mouse_hit( const math::vector3_t& aim, const math::vector3_t& cam_pos, uintptr_t part, float screen_x, float screen_y ) -> void
{
    if ( silentaim::mouse_hit::ensure( ) )
        silentaim::mouse_hit::apply( aim, cam_pos, part, screen_x, screen_y );
}

auto silent_method( ) -> int
{
    int method = config.silentaim.method;
    if ( method > 2 )
        method = 2;
    return method;
}

auto pf_custom_cast( ) -> bool
{
    return games::current( ) == games::game_id::phantom_forces;
}

// PF never calls WorldRoot:Raycast. Their caster starts from
// ViewportPointToRay / Mouse.Hit, then does its own hit test.
auto pf_raycast_mode( ) -> bool
{
    return pf_custom_cast( ) && silent_method( ) == 0;
}

auto silent_passthrough( ) -> void
{
    raycast_hook::passthrough( );
    viewport_hook::passthrough( );
    silentaim::mouse_hit::passthrough( );
}

auto method_name( int method ) -> const char*
{
    if ( pf_raycast_mode( ) ) return "pf-cast";
    if ( method == 1 ) return "viewport";
    if ( method == 2 ) return "mouse.hit";
    return "raycast";
}

auto method_hooked( int method ) -> int
{
    if ( pf_raycast_mode( ) )
        return viewport_hook::hooked_count( )
            + ( int )silentaim::mouse_hit::hooked( )
            + raycast_hook::hooked_count( );
    if ( method == 1 ) return viewport_hook::hooked_count( );
    if ( method == 2 ) return ( int )silentaim::mouse_hit::hooked( );
    return raycast_hook::hooked_count( );
}

auto method_armed( int method ) -> int
{
    if ( pf_raycast_mode( ) )
        return ( int )viewport_hook::armed( )
            + ( int )silentaim::mouse_hit::armed( )
            + ( int )raycast_hook::armed( );
    if ( method == 1 ) return ( int )viewport_hook::armed( );
    if ( method == 2 ) return ( int )silentaim::mouse_hit::armed( );
    return ( int )raycast_hook::armed( );
}

auto apply_silent_method( const math::vector3_t& aim, const math::vector3_t& cam_pos, float screen_x, float screen_y, uintptr_t part ) -> void
{
    if ( pf_raycast_mode( ) )
    {
        static bool logged = false;
        if ( !logged )
        {
            logged = true;
            g->logger.print( "[silent] Phantom Forces custom cast — viewport + mouse.hit" );
        }
        write_viewport( aim, cam_pos, screen_x, screen_y );
        write_mouse_hit( aim, cam_pos, part, screen_x, screen_y );
        write_raycast( aim, cam_pos );
        return;
    }

    const int method = silent_method( );
    if ( method <= 0 )
    {
        write_raycast( aim, cam_pos );
        viewport_hook::passthrough( );
        silentaim::mouse_hit::passthrough( );
        return;
    }
    if ( method == 1 )
    {
        raycast_hook::passthrough( );
        write_viewport( aim, cam_pos, screen_x, screen_y );
        silentaim::mouse_hit::passthrough( );
        return;
    }

    raycast_hook::passthrough( );
    viewport_hook::passthrough( );
    write_mouse_hit( aim, cam_pos, part, screen_x, screen_y );
}

}

namespace silentaim
{

auto run( ) -> void
{
        static lock_state_t st{};
    static bool gate_toggled = false;
    static bool gate_was_down = false;

    if ( !usermode->can_rpm( ) )
        return;

    {
        if ( config.silentaim.method > 2 )
            config.silentaim.method = 2;
        static int s_live = -1;
        // Never allocate/patch in Roblox unless silent is actually on.
        // ensure() uses NtAllocateVirtualMemory and Hyperion kills the client.
        if ( !config.silentaim.enabled )
        {
            s_live = -1;
        }
        else
        {
            const int want = pf_raycast_mode( ) ? 10 : silent_method( );
            if ( want != s_live )
            {
                if ( want != 0 && want != 10 )
                    raycast_hook::unhook( );
                if ( want != 1 && want != 10 )
                    viewport_hook::unhook( );
                if ( want != 2 && want != 10 )
                    silentaim::mouse_hit::unhook( );
                s_live = want;
            }
            if ( want == 0 )
                raycast_hook::ensure( );
            else if ( want == 1 )
                viewport_hook::ensure( );
            else if ( want == 2 )
                silentaim::mouse_hit::ensure( );
            else
            {
                viewport_hook::ensure( );
                silentaim::mouse_hit::ensure( );
                raycast_hook::ensure( );
            }
        }
    }

    static auto last_tick = std::chrono::steady_clock::time_point{};
    auto tick_log = [&]( const char* why, size_t players, bool locked )
    {
        const auto now = std::chrono::steady_clock::now( );
        if ( last_tick.time_since_epoch( ).count( )
            && now - last_tick < std::chrono::seconds( 2 ) )
            return;
        last_tick = now;
        const int method = silent_method( );
        const char* name = method_name( method );
        const int hooked = method_hooked( method );
        const int armed = method_armed( method );
        const uint64_t hits = method == 0 ? raycast_hook::hits( )
            : ( method == 1 ? viewport_hook::hits( ) : 0 );
        const uint64_t rw = method == 1 ? viewport_hook::rewrites( ) : 0;
        g->logger.print(
            "[overlay] silent %s method=%s hooked=%d armed=%d hits=%llu rw=%llu players=%zu locked=%d",
            why, name, hooked, armed, ( unsigned long long )hits, ( unsigned long long )rw, players, ( int )locked );
        if ( method == 0 )
            raycast_hook::log_debug( );
    };

    const auto gate = aimbot_helpers::key_gate( config.silentaim.key, config.silentaim.key_mode, gate_toggled, gate_was_down );
    if ( !config.silentaim.enabled || !gate.active )
    {
        silent_passthrough( );
        g_draw.ok = false;
        tick_log( "gate", 0, false );

        sdk::u_camera cam = resolve_camera( );
        restore_spectate( st, cam );

        if ( st.locked || st.sticky )
        {
            clear_sticky( st );
            if ( st.camera_overridden )
            {
                const float delay = config.silentaim.restore_delay_ms;
                if ( delay <= 0.f )
                    restore_camera( st );
                else if ( !st.pending_restore )
                {
                    st.pending_restore = true;
                    st.release_at = std::chrono::steady_clock::now( )
                        + std::chrono::milliseconds( static_cast<int>( delay ) );
                }
            }
        }

        if ( st.pending_restore && st.camera_overridden )
        {
            if ( std::chrono::steady_clock::now( ) >= st.release_at )
                restore_camera( st );
        }
        return;
    }

    st.pending_restore = false;

    const math::vector2_t dimensions = sdk::u_visualengine::dimensions( );
    const math::matrix4_t viewmatrix = sdk::u_visualengine::view_matrix( );
    const math::vector2_t offset = sdk::u_visualengine::client_offset( );
    if ( dimensions.x < 1.f || dimensions.y < 1.f )
    {
        clear_sticky( st );
        g_draw.ok = false;
        return;
    }

    sdk::u_camera cam = resolve_camera( );
    const math::vector3_t cam_pos = cam ? cam.position( ) : math::vector3_t::nan( );
    const float range_px = silent_range_px( cam ? cam.field_of_view( ) : 70.f );

    ImVec2 cursor{};
    const ImVec2 crosshair{ dimensions.x * 0.5f, dimensions.y * 0.5f };
    if ( !aimbot_helpers::cursor_client( cursor ) )
        cursor = crosshair;
    else if ( cursor.x < -32.f || cursor.y < -32.f
        || cursor.x > dimensions.x + 32.f
        || cursor.y > dimensions.y + 32.f
        || config.menu_open )
        cursor = crosshair;

    const ImVec2 cursor_draw{ cursor.x + offset.x, cursor.y + offset.y };

    auto players = g->cache.snapshot_players( );
    if ( !players )
    {
        tick_log( "no-cache", 0, false );
        clear_sticky( st );
        g_draw.ok = false;
        return;
    }

    auto world = g->cache.snapshot_world( );
    const uintptr_t client = g->engine.client.load( );
    const uintptr_t client_team = g->engine.client_team.load( );
    const int64_t local_uid = g->engine.client_user_id.load( );

    const cache::player* best = nullptr;
    math::vector3_t best_world{};
    ImVec2 best_screen{};
    float best_score = FLT_MAX;
    bool sticky_held = false;

    auto apply_sample = [&]( const cache::player& player, const aim_sample_t& sample, float score )
    {
        best = &player;
        best_world = sample.world;
        best_screen = sample.screen;
        best_score = score;
    };

    if ( config.silentaim.sticky && st.sticky != 0 )
    {
        const cache::player* sticky_player = nullptr;
        for ( const auto& player : *players )
        {
            if ( player.address == st.sticky )
            {
                sticky_player = &player;
                break;
            }
        }

        if ( sticky_player && passes_filters( *sticky_player, client, client_team, local_uid ) )
        {
            const aim_sample_t sample = sample_player_aim(
                *sticky_player, dimensions, viewmatrix, offset, cursor_draw, cam_pos, world, true, range_px );
            if ( sample.valid )
            {
                sticky_held = true;
                apply_sample( *sticky_player, sample, 0.f );
            }
        }
        else
        {
            st.sticky = 0;
            combat::silent_sticky.store( 0, std::memory_order_relaxed );
        }
    }

    if ( !best && !sticky_held )
    {
        for ( const auto& player : *players )
        {
            if ( !passes_filters( player, client, client_team, local_uid ) )
                continue;

            const aim_sample_t sample = sample_player_aim(
                player, dimensions, viewmatrix, offset, cursor_draw, cam_pos, world, false, range_px );
            if ( !sample.valid )
                continue;

            const float dx = sample.screen.x - cursor_draw.x;
            const float dy = sample.screen.y - cursor_draw.y;
            const float score = score_target( dx * dx + dy * dy, player.distance, player.health, player.max_health );
            if ( score < best_score )
                apply_sample( player, sample, score );
        }
    }

    if ( !best && !sticky_held )
    {
        for ( const auto& player : *players )
        {
            if ( !passes_filters( player, client, client_team, local_uid ) )
                continue;

            const aim_sample_t sample = sample_player_aim(
                player, dimensions, viewmatrix, offset, cursor_draw, cam_pos, world, true, range_px );
            if ( !sample.valid )
                continue;

            float score = player.distance;
            if ( std::isfinite( sample.screen.x ) && std::isfinite( sample.screen.y ) )
            {
                const float dx = sample.screen.x - cursor_draw.x;
                const float dy = sample.screen.y - cursor_draw.y;
                score = dx * dx + dy * dy;
            }
            else if ( cam_pos.is_valid( ) && sample.world.is_valid( ) )
            {
                const float dx = sample.world.x - cam_pos.x;
                const float dy = sample.world.y - cam_pos.y;
                const float dz = sample.world.z - cam_pos.z;
                score = dx * dx + dy * dy + dz * dz;
            }
            if ( score < best_score )
                apply_sample( player, sample, score );
        }
    }

    if ( !best && !sticky_held )
    {
        for ( const auto& player : *players )
        {
            if ( !passes_filters( player, client, client_team, local_uid, true ) )
                continue;

            const math::vector3_t w = world_of( player );
            if ( !w.is_valid( ) )
                continue;

            aim_sample_t sample{};
            sample.world = w;
            sample.valid = true;
            project_world( w, dimensions, viewmatrix, offset, sample.screen );

            float score = player.distance;
            if ( cam_pos.is_valid( ) )
            {
                const float dx = w.x - cam_pos.x;
                const float dy = w.y - cam_pos.y;
                const float dz = w.z - cam_pos.z;
                score = dx * dx + dy * dy + dz * dz;
            }
            if ( score < best_score )
                apply_sample( player, sample, score );
        }
    }

    if ( !best )
    {
        tick_log( "no-target", players->size( ), false );
        silent_passthrough( );
        restore_spectate( st, cam );
        g_draw.ok = false;
        if ( sticky_held )
            return;
        clear_sticky( st );
        return;
    }

    st.locked = true;
    if ( config.silentaim.sticky )
    {
        st.sticky = best->address;
        combat::silent_sticky.store( best->address, std::memory_order_relaxed );
    }

    combat::last_target.store( best->address, std::memory_order_relaxed );
    combat::track_damage( best->address, best->name.c_str( ), best->health );

    if ( st.camera_overridden )
        restore_camera( st );

    const float target_x = best_screen.x - offset.x;
    const float target_y = best_screen.y - offset.y;
    const uintptr_t part = best->head ? best->head : best->root_part;
    if ( std::isfinite( target_x ) && std::isfinite( target_y ) )
        apply_silent_method( best_world, cam_pos, target_x, target_y, part );
    else
        silent_passthrough( );
    tick_log( "set", players->size( ), true );

    if ( config.silentaim.spectate && cam && best->humanoid )
    {
        if ( !st.spectating )
        {
            st.subject_saved = cam.camera_subject( ).address( );
            st.spectating = true;
        }
        cam.set_camera_subject( best->humanoid );
    }
    else
    {
        restore_spectate( st, cam );
    }

    g_draw.aim = best_screen;
    g_draw.cursor = cursor_draw;
    g_draw.ok = true;
}

auto draw( ) -> void
{
    if ( !config.silentaim.enabled && !config.silentaim.fov_visualizer )
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList( );
    if ( !dl )
        return;

    ImVec2 cursor = g_draw.cursor;
    if ( cursor.x == 0.f && cursor.y == 0.f )
    {
        const math::vector2_t dimensions = sdk::u_visualengine::dimensions( );
        const math::vector2_t offset = sdk::u_visualengine::client_offset( );
        ImVec2 c{};
        if ( aimbot_helpers::cursor_client( c ) )
            cursor = { c.x + offset.x, c.y + offset.y };
        else
            cursor = { dimensions.x * 0.5f + offset.x, dimensions.y * 0.5f + offset.y };
    }

    if ( config.silentaim.enabled && config.silentaim.tracer && g_draw.ok )
    {
        const ImDrawListFlags old = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedLines;
        dl->AddLine( cursor, g_draw.aim, IM_COL32( 0, 0, 0, 200 ), 3.f );
        dl->AddLine( cursor, g_draw.aim, IM_COL32( 255, 255, 255, 220 ), 1.f );
        dl->Flags = old;
    }

    if ( config.silentaim.enabled && config.silentaim.target_dot && g_draw.ok )
    {
        const ImU32 col = IM_COL32(
            ( int )( config.silentaim.target_dot_color[0] * 255.f ),
            ( int )( config.silentaim.target_dot_color[1] * 255.f ),
            ( int )( config.silentaim.target_dot_color[2] * 255.f ),
            ( int )( config.silentaim.target_dot_color[3] * 255.f ) );
        const ImDrawListFlags old = dl->Flags;
        dl->Flags &= ~( ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines );
        dl->AddCircleFilled( g_draw.aim, config.silentaim.target_dot_radius, col );
        dl->Flags = old;
    }

    if ( config.silentaim.fov_visualizer )
    {
        const float rad = silent_range_px( 70.f );
        const int rr = ( int )( config.silentaim.fov_color[0] * 255.f );
        const int gg = ( int )( config.silentaim.fov_color[1] * 255.f );
        const int bb = ( int )( config.silentaim.fov_color[2] * 255.f );
        const int aa = ( int )( config.silentaim.fov_color[3] * 255.f );
        const ImU32 ring = IM_COL32( rr, gg, bb, aa );
        const ImU32 shadow = IM_COL32( 0, 0, 0, aa );

        const ImDrawListFlags old = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedLines;

        if ( config.silentaim.fov_filled )
            dl->AddCircleFilled( cursor, rad, IM_COL32( rr, gg, bb, aa / 4 ), 64 );
        if ( config.silentaim.fov_outline )
            dl->AddCircle( cursor, rad, shadow, 64, config.silentaim.fov_thickness + 2.f );
        dl->AddCircle( cursor, rad, ring, 64, config.silentaim.fov_thickness );
        dl->Flags = old;
    }
}

}
