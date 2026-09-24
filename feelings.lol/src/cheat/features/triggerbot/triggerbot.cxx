#include <include/global.cuh>

#include <src/cheat/features/aimbot/helpers.hxx>
#include <src/cheat/features/combat/combat_state.cuh>
#include <src/cheat/features/triggerbot/triggerbot.cuh>
#include <src/engine/raycast/engine_ray.hxx>
#include <src/engine/sdk/camera/camera.cuh>

#include <chrono>
#include <cmath>
#include <algorithm>
#include <random>

namespace
{

auto passes_filters( const cache::player& player, uintptr_t client, uintptr_t client_team, int64_t local_uid ) -> bool
{
    if ( player.address == 0 || player.address == client )
        return false;
    if ( config.triggerbot.team_check && player.same_team( client_team ) )
        return false;
    if ( config.triggerbot.friend_check && local_uid > 0 && player.user_id > 0 && sdk::is_friend( local_uid, player.user_id ) )
        return false;
    if ( config.triggerbot.npc_check && player.is_npc )
        return false;
    if ( player.max_health > 0.f && player.health <= 0.f )
        return false;
    if ( config.triggerbot.max_distance > 0.f && player.distance > config.triggerbot.max_distance )
        return false;
    return true;
}

} // namespace

namespace triggerbot
{

auto run( ) -> void
{
    static bool gate_toggled = false;
    static bool gate_was_down = false;
    static bool mouse_down = false;
    static std::chrono::steady_clock::time_point next_click_at{};
    static std::chrono::steady_clock::time_point release_at{};

    const auto now = std::chrono::steady_clock::now( );

    if ( mouse_down )
    {
        if ( now >= release_at )
        {
            aimbot_helpers::mouse_click( false );
            mouse_down = false;
            next_click_at = now + std::chrono::milliseconds(
                static_cast<int>( ( std::max )( config.triggerbot.delay_ms, 0.f ) ) );
        }
        return;
    }

    const auto gate = aimbot_helpers::key_gate( config.triggerbot.key, config.triggerbot.key_mode, gate_toggled, gate_was_down );
    if ( !config.triggerbot.enabled || !gate.active )
        return;

    if ( now < next_click_at )
        return;

    const math::vector2_t dimensions = sdk::u_visualengine::dimensions( );
    if ( dimensions.x < 1.f || dimensions.y < 1.f )
        return;

    ImVec2 cursor{};
    if ( !aimbot_helpers::cursor_client( cursor ) )
        return;

    auto players = g->cache.snapshot_players( );
    if ( !players )
        return;

    auto world = g->cache.snapshot_world( );
    sdk::u_camera cam( g->engine.camera.load( ) );
    if ( !cam )
        return;

    const math::cframe_t cf = cam.cframe( );
    if ( !cf.position.is_valid( ) )
        return;

    const uintptr_t client = g->engine.client.load( );
    const uintptr_t client_team = g->engine.client_team.load( );
    const int64_t local_uid = g->engine.client_user_id.load( );
    const uintptr_t local_char = g->engine.client_model.load( );

    engine_ray::query_t q = engine_ray::viewport_ray(
        cf, cam.field_of_view( ), dimensions.x, dimensions.y, cursor.x, cursor.y,
        config.triggerbot.max_distance > 0.f ? config.triggerbot.max_distance : 2000.f );

    std::vector<cache::player> filtered;
    filtered.reserve( players->size( ) );
    for ( const auto& player : *players )
    {
        if ( passes_filters( player, client, client_team, local_uid ) )
            filtered.push_back( player );
    }

    const engine_ray::hit_t body = engine_ray::cast_players(
        q, filtered, local_char, config.triggerbot.aim_part );
    if ( !body.ok )
        return;

    if ( config.triggerbot.wall_check && !config.triggerbot.wallbang && world )
    {
        const math::vector3_t hit_pos{
            q.origin.x + q.dir.x * body.t,
            q.origin.y + q.dir.y * body.t,
            q.origin.z + q.dir.z * body.t,
        };
        if ( !engine_ray::visible( q.origin, hit_pos, *world ) )
            return;
    }

    const cache::player* hovered = nullptr;
    for ( const auto& player : filtered )
    {
        if ( player.character && player.character == body.character )
        {
            hovered = &player;
            break;
        }
    }
    if ( !hovered )
        return;

    combat::track_damage( hovered->address, hovered->name.c_str( ), hovered->health );
    combat::last_target.store( hovered->address, std::memory_order_relaxed );

    static std::mt19937 rng{ std::random_device{}( ) };
    std::uniform_real_distribution<float> roll( 0.f, 100.f );
    if ( roll( rng ) > config.triggerbot.hitchance )
    {
        next_click_at = now + std::chrono::milliseconds(
            static_cast<int>( ( std::max )( config.triggerbot.delay_ms, 1.f ) ) );
        return;
    }

    aimbot_helpers::mouse_click( true );
    mouse_down = true;
    const float click_ms = ( std::max )( config.triggerbot.click_ms, 1.f );
    release_at = now + std::chrono::milliseconds( static_cast<int>( click_ms ) );
}

} // namespace triggerbot
