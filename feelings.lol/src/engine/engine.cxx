#include <include/global.cuh>

#include <src/engine/cache/games/games.cuh>
#include <src/cheat/features/silentaim/raycast_hook.cuh>
#include <src/cheat/features/silentaim/mouse_hit.cuh>
#include <src/cheat/features/lua/lua_gc.cuh>
#include <src/engine/raycast/engine_ray.hxx>

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

auto engine_c::game( ) -> void
{
    static auto last_fail_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now( );
    const bool log_fail = last_fail_log.time_since_epoch( ).count( ) == 0
        || ( now - last_fail_log ) >= std::chrono::seconds( 2 );

    sdk::u_datamodel dm = sdk::u_datamodel::resolve( );
    if ( !dm )
    {
        if ( log_fail )
        {
            g->logger.print( "game: datamodel resolve failed (base=0x%llx)",
                ( unsigned long long )usermode->base( ) );
            last_fail_log = now;
        }
        return;
    }

    datamodel = dm.address( );

    sdk::u_workspace ws = dm.workspace( );
    if ( !ws )
    {
        if ( log_fail )
        {
            g->logger.print( "game: workspace missing (dm=0x%llx place=%lld)",
                ( unsigned long long )dm.address( ), ( long long )dm.place_id( ) );
            last_fail_log = now;
        }
        return;
    }
    workspace = ws.address( );

    sdk::u_camera cam = ws.current_camera( );
    if ( cam )
        camera = cam.address( );

    sdk::u_players ps = dm.players( );
    if ( !ps )
    {
        if ( log_fail )
        {
            const auto kids = dm.get_children( );
            int named = 0;
            for ( const auto& c : kids )
                if ( !c.name( ).empty( ) )
                    ++named;
            g->logger.print( "game: Players missing (ws=0x%llx kids=%zu named=%d)",
                ( unsigned long long )ws.address( ), kids.size( ), named );
            size_t show = kids.size( ) < 8 ? kids.size( ) : 8;
            for ( size_t i = 0; i < show; ++i )
            {
                g->logger.print( "  child[%zu] class='%s' name='%s'",
                    i, kids[ i ].class_name( ).c_str( ), kids[ i ].name( ).c_str( ) );
            }
            last_fail_log = now;
        }
        return;
    }
    players = ps.address( );

    sdk::u_player lp = ps.local_player( );
    if ( !lp )
    {
        if ( log_fail )
        {
            g->logger.print( "game: localplayer not ready yet (place=%lld)",
                ( long long )dm.place_id( ) );
            last_fail_log = now;
        }
        return;
    }

    client = lp.address( );
    client_team = lp.team( ).address( );
    client_model = lp.character( ).address( );
    client_user_id = lp.user_id( );

    if ( !cam )
    {
        if ( log_fail )
        {
            g->logger.print( "game: camera missing (continuing)" );
            last_fail_log = now;
        }
    }

    g->logger.print( "datamodel -> 0x%llx", ( unsigned long long )datamodel.load( ) );
    g->logger.print( "workspace -> 0x%llx", ( unsigned long long )workspace.load( ) );
    g->logger.print( "camera -> 0x%llx", ( unsigned long long )camera.load( ) );
    g->logger.print( "players -> 0x%llx", ( unsigned long long )players.load( ) );
    g->logger.print( "client -> 0x%llx", ( unsigned long long )client.load( ) );
    g->logger.print( "place -> %lld", ( long long )dm.place_id( ) );
}

auto engine_c::refresh( ) -> void
{
    const uintptr_t dm_addr = datamodel.load( );
    if ( !dm_addr )
    {
        this->game( );
        return;
    }

    sdk::u_datamodel dm( dm_addr );
    sdk::u_workspace ws = dm.workspace( );
    if ( !ws )
        return;

    workspace = ws.address( );

    sdk::u_camera cam = ws.current_camera( );
    if ( cam )
        camera = cam.address( );

    sdk::u_players ps = dm.players( );
    if ( !ps )
        return;

    players = ps.address( );

    sdk::u_player lp = ps.local_player( );
    if ( !lp )
        return;

    client = lp.address( );
    client_team = lp.team( ).address( );
    client_model = lp.character( ).address( );
    client_user_id = lp.user_id( );
}

auto engine_c::clear_bindings( ) -> void
{
    // drop raycast origin stubs before addresses go stale
    raycast_hook::unhook( );
    viewport_hook::unhook( );
    engine_ray::invalidate( );
    lua_gc::reset( );
    // mouse.hit hooks ClassDescriptor getters — they survive datamodel hops.
    // only tear down when the process is gone (ensure() probes the cave).

    datamodel = 0;
    workspace = 0;
    camera = 0;
    players = 0;
    client = 0;
    client_team = 0;
    client_model = 0;
    client_user_id = 0;
    g->handler.m_last_datamodel = 0;
}

auto engine_c::rescan( ) -> bool
{
    std::unique_lock<std::mutex> lock( rescan_mutex, std::try_to_lock );
    if ( !lock.owns_lock( ) )
    {
        g->logger.print( "rescan already in progress" );
        return false;
    }

    g->logger.print( "rescanning roblox..." );

    // Drop bindings before detach so other threads stop using stale addresses
    this->clear_bindings( );
    sdk::u_visualengine::clear( );
    {
        auto empty = std::make_shared<std::vector<cache::player>>( );
        std::lock_guard<std::mutex> clock( g->cache.swap_mutex );
        g->cache.players = empty;
    }

    // Brief pause so in-flight RPM bursts finish against the closed-handle path
    std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );

    if ( !usermode->attach( ) )
    {
        g->logger.print( "rescan failed — roblox not found" );
        return false;
    }

    g->logger.print( "attached ( pid %lu, base 0x%llx, size 0x%zx, %s )",
        usermode->pid( ),
        ( unsigned long long )usermode->base( ),
        usermode->image_size( ),
        usermode->version( ).empty( ) ? "version-unknown" : usermode->version( ).c_str( ) );

    sdk::u_instance::clear_class_cache( );
    sdk::u_visualengine::clear( );

    this->game( );

    const uintptr_t dm = datamodel.load( );
    if ( !dm )
    {
        g->logger.print( "rescan attached — waiting for datamodel" );
        return false;
    }

    g->handler.m_last_datamodel = dm;
    const int64_t place = sdk::u_datamodel( dm ).place_id( );
    const games::game_id gid = games::detect( place, workspace.load( ) );
    games::set_current( gid, place );
    g->logger.print( "rescan ok — game=%s place=%lld", games::name( gid ), ( long long )place );
    return true;
}

auto engine_c::init( ) -> void
{
    g->logger.print( "resolving datamodel..." );

    for ( int i = 0; i < 80; ++i )
    {
        this->game( );
        if ( datamodel.load( ) && workspace.load( ) && players.load( ) && client.load( ) )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
    }

    if ( !datamodel.load( ) )
        g->logger.print( "datamodel not ready yet — handler will keep retrying" );
    else if ( !client.load( ) )
        g->logger.print( "datamodel ok, waiting for localplayer..." );
}

auto engine_c::start_runtime( ) -> void
{
    if ( runtime_started.exchange( true ) )
        return;

    std::thread( &cache_c::tick, &g->cache ).detach( );
    std::thread( &cache_c::transform, &g->cache ).detach( );
    std::thread( &handler_c::run, &g->handler ).detach( );
    g->logger.print( "runtime started (cache + handler) after auth" );
}
