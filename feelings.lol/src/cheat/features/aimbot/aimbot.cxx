#include <include/global.cuh>

#include <src/cheat/features/aimbot/helpers.hxx>
#include <src/cheat/features/combat/combat_state.cuh>
#include <src/cheat/features/stealth/stealth.hxx>

#include <chrono>
#include <cfloat>
#include <cmath>
#include <random>

namespace
{

struct lock_state_t
{
    uintptr_t sticky = 0;
    float spring_vx = 0.f;
    float spring_vy = 0.f;
    bool locked = false;
    bool camera_overridden = false;
    int camera_type_saved = 0;
    uintptr_t camera_addr = 0;
    std::chrono::steady_clock::time_point lock_started{};
    std::chrono::steady_clock::time_point last_tick{};
};

auto clear_lock( lock_state_t& st ) -> void
{
    if ( st.camera_overridden && st.camera_addr && config.aimbot.cam_restore_type )
    {
        sdk::u_camera( st.camera_addr ).set_camera_type( st.camera_type_saved );
        st.camera_overridden = false;
        st.camera_addr = 0;
    }

    st.sticky = 0;
    combat::aim_sticky.store( 0, std::memory_order_relaxed );
    st.spring_vx = 0.f;
    st.spring_vy = 0.f;
    st.locked = false;
    st.last_tick = {};
    st.lock_started = {};
}

auto passes_filters( const cache::player& player, uintptr_t client, uintptr_t client_team, int64_t local_uid ) -> bool
{
    if ( player.address == 0 || player.address == client )
        return false;
    if ( !player.character && player.bones.empty( ) )
        return false;
    if ( config.aimbot.team_check && ( player.teammate || player.same_team( client_team ) ) )
        return false;
    if ( config.aimbot.friend_check && local_uid > 0 && player.user_id > 0 && sdk::is_friend( local_uid, player.user_id ) )
        return false;
    if ( config.aimbot.npc_check && player.is_npc )
        return false;
    // Only skip clearly-dead targets; bad HP reads must not kill aim assist.
    if ( player.max_health >= 20.f && player.max_health <= 10000.f
        && player.health >= 0.f && player.health < 0.5f )
        return false;
    if ( config.aimbot.max_distance > 0.f && player.distance > config.aimbot.max_distance )
        return false;
    return true;
}

auto score_target( float dsq, float distance, float health, float max_health ) -> float
{
    switch ( config.aimbot.target_mode )
    {
    case 1: return distance;
    case 2: return health;
    case 3: return max_health > 0.f ? ( max_health - health ) : 0.f;
    case 0:
    default: return dsq;
    }
}

struct aim_sample_t
{
    math::vector3_t world{};
    ImVec2 screen{};
    bool valid = false;
};

auto aim_range_px( float camera_fov_deg ) -> float
{
    float range = ( std::max )( config.aimbot.range, 20.f );
    float fov = camera_fov_deg;
    if ( fov > 0.01f && fov < 3.5f )
        fov *= 57.2957795f;
    constexpr float k_ref_fov = 70.f;
    if ( fov > 5.f && fov < k_ref_fov )
        range *= std::clamp( k_ref_fov / fov, 1.f, 6.f );
    return range;
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

// genesis camera aimbot — LookAtToMatrix + element-wise matrix lerp + WriteCameraRotations
auto lerp_matrix3( const math::matrix3_t& a, const math::matrix3_t& b, float t ) -> math::matrix3_t
{
    math::matrix3_t out{};
    for ( int i = 0; i < 9; ++i )
        out.data[i] = a.data[i] + ( b.data[i] - a.data[i] ) * t;
    return out;
}

auto look_at_to_matrix_genesis( const math::vector3_t& camera_pos, const math::vector3_t& target ) -> math::matrix3_t
{
    const math::vector3_t forward = math::vec3_normalize( {
        target.x - camera_pos.x,
        target.y - camera_pos.y,
        target.z - camera_pos.z,
    } );
    const math::vector3_t right = math::vec3_normalize( math::vec3_cross( { 0.f, 1.f, 0.f }, forward ) );
    const math::vector3_t up = math::vec3_cross( forward, right );

    math::matrix3_t m{};
    m.data[0] = -right.x;  m.data[1] = up.x;  m.data[2] = -forward.x;
    m.data[3] = right.y;   m.data[4] = up.y;  m.data[5] = -forward.y;
    m.data[6] = -right.z;  m.data[7] = up.z;  m.data[8] = -forward.z;
    return m;
}

auto write_camera_rotations( sdk::u_camera& cam, const math::matrix3_t& rot ) -> void
{
    if ( !cam )
        return;
    // genesis WriteCameraRotations — orientation slot only (CameraRotation)
    cam.set_orientation( rot );
}

auto apply_genesis_camera_aim( sdk::u_camera& cam, const math::vector3_t& target, float smooth ) -> void
{
    if ( !cam || !target.is_valid( ) )
        return;

    const math::vector3_t camera_pos = cam.position( );
    if ( !camera_pos.is_valid( ) )
        return;

    // genesis: smoothness = (101 - smoth) / 100; Feelings smooth 1..20 → genesis ~5..100
    const float g_smoth = std::clamp( ( std::max )( smooth, 1.f ) * 5.f, 1.f, 100.f );
    const float alpha = std::clamp( ( 101.f - g_smoth ) / 100.f, 0.01f, 1.f );

    const math::matrix3_t camera_rotation = cam.orientation( );
    const math::matrix3_t hit_matrix = look_at_to_matrix_genesis( camera_pos, target );
    write_camera_rotations( cam, lerp_matrix3( camera_rotation, hit_matrix, alpha ) );
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

    auto bone_world = []( const cache::player::bone& b ) -> math::vector3_t
    {
        const cache::bone_pose_t pose = cache::read_bone_pose( b );
        if ( pose.valid )
            return pose.position;
        return b.position;
    };

    auto pick_fallback_bone = [&]( ) -> const cache::player::bone*
    {
        static const char* names[] = {
            "Head", "head", "UpperTorso", "Torso", "HumanoidRootPart", "LowerTorso"
        };
        for ( const char* n : names )
        {
            if ( const auto* b = aimbot_helpers::find_named_bone( player, n ) )
                return b;
        }
        if ( !player.bones.empty( ) )
            return &player.bones.front( );
        return nullptr;
    };

    if ( config.aimbot.aim_part == 4 )
    {
        if ( !aimbot_helpers::nearest_bone_to_cursor( player, dimensions, viewmatrix, offset, cursor, out.world, out.screen ) )
            return out;
    }
    else if ( config.aimbot.aim_part == 0 )
    {
        out.world = aimbot_helpers::head_aim_world( player );
        bone = aimbot_helpers::resolve_hitbox( player, 0 );
        if ( !out.world.is_valid( ) )
        {
            if ( !bone )
                bone = pick_fallback_bone( );
            if ( !bone )
                return out;
            out.world = bone_world( *bone );
            if ( !out.world.is_valid( ) )
                return out;
        }
        if ( !project_world( out.world, dimensions, viewmatrix, offset, out.screen ) )
            return out;
    }
    else
    {
        bone = aimbot_helpers::resolve_hitbox( player, config.aimbot.aim_part );
        if ( !bone )
            bone = pick_fallback_bone( );
        if ( !bone )
            return out;
        out.world = bone_world( *bone );
        if ( !out.world.is_valid( ) || !project_world( out.world, dimensions, viewmatrix, offset, out.screen ) )
            return out;
    }

    if ( config.aimbot.prediction && cam_pos.is_valid( ) )
    {
        math::vector3_t vel{};
        if ( !bone )
            bone = aimbot_helpers::resolve_hitbox( player, config.aimbot.aim_part == 0 ? 0 : config.aimbot.aim_part );
        if ( !bone )
            bone = pick_fallback_bone( );
        if ( bone )
            vel = bone->velocity;
        out.world = math::lead_target( cam_pos, out.world, vel,
            config.aimbot.prediction_xz, config.aimbot.prediction_y, 0.f );
        if ( !project_world( out.world, dimensions, viewmatrix, offset, out.screen ) )
            return out;
    }

    if ( config.aimbot.wall_check && !config.aimbot.wallbang && world && cam_pos.is_valid( ) )
    {
        if ( !wall_visible( cam_pos, out.world, *world ) )
            return out;
    }

    if ( !ignore_range )
    {
        const float dx = out.screen.x - cursor.x;
        const float dy = out.screen.y - cursor.y;
        if ( dx * dx + dy * dy >= range_px * range_px )
            return out;
    }

    out.valid = true;
    return out;
}

}

auto aimbot_c::run( ) -> void
{
        static lock_state_t st{};
    static bool gate_toggled = false;
    static bool gate_was_down = false;

    const auto gate = aimbot_helpers::key_gate( config.aimbot.key, config.aimbot.key_mode, gate_toggled, gate_was_down );
    if ( !config.aimbot.enabled || !gate.active )
    {
        if ( st.locked || st.sticky )
            clear_lock( st );
        return;
    }

    const math::matrix4_t viewmatrix = sdk::u_visualengine::view_matrix( );
    const proj_frame_t& pf = proj_frame( );
    const math::vector2_t dimensions = ( pf.ready && pf.draw.x > 1.f )
        ? pf.draw
        : sdk::u_visualengine::dimensions( );
    if ( dimensions.x < 1.f || dimensions.y < 1.f )
    {
        clear_lock( st );
        return;
    }

    sdk::u_camera cam = resolve_camera( );
    const math::vector3_t cam_pos = cam ? cam.position( ) : math::vector3_t::nan( );
    const float range_px = aim_range_px( cam ? cam.field_of_view( ) : 70.f );

    // Cursor in the same space as project_to_draw (client/draw px). Do NOT add
    // client_offset on top of ScreenToClient — that used to zero the FOV check.
    ImVec2 cursor{};
    const ImVec2 crosshair{ dimensions.x * 0.5f, dimensions.y * 0.5f };
    if ( !aimbot_helpers::cursor_client( cursor )
        || !std::isfinite( cursor.x ) || !std::isfinite( cursor.y )
        || cursor.x < -64.f || cursor.y < -64.f
        || cursor.x > dimensions.x + 64.f || cursor.y > dimensions.y + 64.f )
    {
        cursor = crosshair;
    }

    const math::vector2_t draw_offset = pf.ready ? pf.offset : sdk::u_visualengine::client_offset( );
    cursor.x += draw_offset.x;
    cursor.y += draw_offset.y;

    auto players = g->cache.snapshot_players();
    if ( !players || players->empty( ) )
    {
        clear_lock( st );
        return;
    }

    auto world = g->cache.snapshot_world();
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

    auto same_sticky = []( const cache::player& p, uintptr_t sticky ) -> bool
    {
        return sticky && ( p.address == sticky || p.character == sticky );
    };

    if ( config.aimbot.sticky && st.sticky != 0 )
    {
        const cache::player* sticky_player = nullptr;
        for ( const auto& player : *players )
        {
            if ( same_sticky( player, st.sticky ) )
            {
                sticky_player = &player;
                break;
            }
        }

        if ( sticky_player && passes_filters( *sticky_player, client, client_team, local_uid ) )
        {
            const aim_sample_t sample = sample_player_aim(
                *sticky_player, dimensions, viewmatrix, draw_offset, cursor, cam_pos, world, true, range_px );
            if ( sample.valid )
            {
                sticky_held = true;
                apply_sample( *sticky_player, sample, 0.f );
            }
        }
        else
        {
            st.sticky = 0;
            combat::aim_sticky.store( 0, std::memory_order_relaxed );
        }
    }

    if ( !best && !sticky_held )
    {
        for ( const auto& player : *players )
        {
            if ( !passes_filters( player, client, client_team, local_uid ) )
                continue;

            const aim_sample_t sample = sample_player_aim(
                player, dimensions, viewmatrix, draw_offset, cursor, cam_pos, world, false, range_px );
            if ( !sample.valid )
                continue;

            const float dx = sample.screen.x - cursor.x;
            const float dy = sample.screen.y - cursor.y;
            const float score = score_target( dx * dx + dy * dy, player.distance, player.health, player.max_health );
            if ( score < best_score )
                apply_sample( player, sample, score );
        }
    }

    if ( !best )
    {

        if ( sticky_held )
            return;
        clear_lock( st );
        return;
    }

    const auto now = std::chrono::steady_clock::now( );
    if ( !st.locked )
    {
        st.locked = true;
        st.lock_started = now;
        st.spring_vx = 0.f;
        st.spring_vy = 0.f;
    }

    if ( config.aimbot.sticky )
    {
        st.sticky = best->address;
        combat::aim_sticky.store( best->address, std::memory_order_relaxed );
    }

    combat::track_damage( best->address, best->name.c_str( ), best->health );

    float dt = st.last_tick.time_since_epoch( ).count( )
        ? std::chrono::duration<float>( now - st.last_tick ).count( )
        : ( 1.f / 240.f );
    st.last_tick = now;
    if ( dt <= 0.f || dt > 0.1f )
        dt = 1.f / 240.f;
    const float rate = std::clamp( dt * 240.f, 0.25f, 2.5f );

    const float hold_ms = std::chrono::duration<float, std::milli>( now - st.lock_started ).count( );
    const float smooth = ( std::max )( config.aimbot.smoothing, 1.f );

    if ( config.aimbot.method == 1 && cam )
    {
        if ( config.aimbot.cam_force_scriptable )
            stealth::ensure_camera_scriptable( cam, st.camera_overridden, st.camera_addr, st.camera_type_saved );
        apply_genesis_camera_aim( cam, best_world, ( std::max )( smooth / rate, 1.f ) );
    }
    else if ( config.aimbot.method == 2 && cam )
    {
        stealth::ensure_camera_scriptable( cam, st.camera_overridden, st.camera_addr, st.camera_type_saved );
        const float cam_alpha = std::clamp( ( 1.f / smooth ) * rate, 0.05f, 1.f );
        cam.aim_cframe( best_world, cam_alpha );
    }
    else
    {
        if ( st.camera_overridden && st.camera_addr && config.aimbot.cam_restore_type )
        {
            sdk::u_camera( st.camera_addr ).set_camera_type( st.camera_type_saved );
            st.camera_overridden = false;
            st.camera_addr = 0;
        }

        const float err_x = best_screen.x - cursor.x;
        const float err_y = best_screen.y - cursor.y;
        float dx = ( err_x / smooth ) * rate;
        float dy = ( err_y / smooth ) * rate;

        if ( static_cast<math::aim_style_t>( config.aimbot.style ) == math::aim_style_t::spring )
        {
            const float stiffness = std::clamp( 400.f / smooth, 20.f, 400.f );
            const float damping = std::clamp( stiffness * 0.45f, 4.f, 80.f );
            dx = 0.f;
            dy = 0.f;
            math::spring_update( dx, st.spring_vx, err_x, stiffness, damping, dt );
            math::spring_update( dy, st.spring_vy, err_y, stiffness, damping, dt );
        }

        if ( config.aimbot.humanize )
        {
            static std::mt19937 rng{ std::random_device{}( ) };
            std::uniform_real_distribution<float> noise( -0.25f, 0.25f );
            const float ramp = std::clamp( hold_ms / 50.f, 0.5f, 1.f );
            dx = dx * ramp + noise( rng );
            dy = dy * ramp + noise( rng );
        }

        dx = std::clamp( dx, -80.f, 80.f );
        dy = std::clamp( dy, -80.f, 80.f );
        aimbot_helpers::move_mouse( dx, dy );
    }
}
