#include <include/global.cuh>

#include <src/engine/cache/games/games.cuh>
#include <src/engine/cache/pf_cache.hxx>
#include <src/cheat/features/notifications/notifications.cuh>

#include <chrono>
#include <cstdio>
#include <thread>

namespace
{

auto process_alive( ) -> bool
{

    HANDLE h = usermode->handle( );
    if ( !h )
        return false;

    DWORD code = 0;
    if ( !GetExitCodeProcess( h, &code ) )
        return false;
    return code == STILL_ACTIVE;
}

auto on_game_changed( games::game_id gid, int64_t place ) -> void
{
    char buf[128]{};
    std::snprintf( buf, sizeof( buf ), "game → %s  (place %lld)",
        games::name( gid ), ( long long )place );
    g->logger.print( "[rescan] %s", buf );
    notifications::push( buf );
}

auto rebind_place( int64_t place ) -> void
{
    sdk::u_instance::clear_class_cache( );
    pf_cache::clear_pins( );
    games::clear_state( );

    {
        auto empty = std::make_shared<std::vector<cache::player>>( );
        std::lock_guard<std::mutex> lock( g->cache.swap_mutex );
        g->cache.players = empty;
    }

    if ( !usermode->attached( ) )
        return;

    g->engine.refresh( );

    const uintptr_t ws = g->engine.workspace.load( );
    const games::game_id gid = games::detect( place, ws );
    if ( games::set_current( gid, place ) )
        on_game_changed( gid, place );
}

}

auto handler_c::run( ) -> void
{
    int64_t last_place = 0;
    int resolve_fails = 0;
    auto last_attach_try = std::chrono::steady_clock::now( );

    while ( true )
    {
        const bool auto_scan = config.settings.auto_rescan;
        const auto now = std::chrono::steady_clock::now( );

        if ( auto_scan && !process_alive( ) )
        {
            if ( std::chrono::duration_cast<std::chrono::seconds>( now - last_attach_try ).count( ) >= 2 )
            {
                last_attach_try = now;
                g->logger.print( "[rescan] roblox missing — auto attach" );
                if ( g->engine.rescan( ) )
                {
                    last_place = 0;
                    resolve_fails = 0;
                    const uintptr_t dm = g->engine.datamodel.load( );
                    if ( dm )
                    {
                        const int64_t place = sdk::u_datamodel( dm ).place_id( );
                        rebind_place( place );
                        last_place = place;
                    }
                }
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            continue;
        }

        do
        {
            if ( !usermode->attached( ) )
                break;

            sdk::u_datamodel current = sdk::u_datamodel::resolve( );
            if ( !current )
            {
                if ( g->engine.datamodel.load( ) )
                {
                    g->engine.clear_bindings( );
                    last_place = 0;
                    games::set_current( games::game_id::none, 0 );
                }

                if ( auto_scan && ++resolve_fails >= 8 )
                {
                    resolve_fails = 0;
                    g->logger.print( "[rescan] datamodel lost — auto rescan" );
                    g->engine.rescan( );
                    last_place = 0;
                }
                break;
            }

            resolve_fails = 0;

            const uintptr_t cur = current.address( );
            const uintptr_t have_dm = g->engine.datamodel.load( );
            const uintptr_t have_client = g->engine.client.load( );

            if ( !have_dm || !have_client || !m_last_datamodel )
            {
                g->engine.game( );
                if ( g->engine.datamodel.load( ) )
                {
                    m_last_datamodel = cur;
                    const int64_t place = current.place_id( );
                    rebind_place( place );
                    last_place = place;
                }
                break;
            }

            if ( cur != m_last_datamodel )
            {
                g->logger.print( "[rescan] datamodel changed — rebinding" );
                sdk::u_instance::clear_class_cache( );
                sdk::u_visualengine::clear( );
                g->engine.clear_bindings( );
                g->engine.game( );
                m_last_datamodel = cur;
                last_place = 0;

                const uintptr_t dm = g->engine.datamodel.load( );
                if ( dm )
                {
                    const int64_t place = sdk::u_datamodel( dm ).place_id( );
                    rebind_place( place );
                    last_place = place;
                }
                break;
            }

            g->engine.refresh( );

            const int64_t place = current.place_id( );
            if ( place != 0 && place != last_place )
            {
                if ( last_place != 0 )
                    g->logger.print( "[rescan] place %lld → %lld",
                        ( long long )last_place, ( long long )place );
                rebind_place( place );
                last_place = place;
            }
            else if ( place != 0 )
            {
                const uintptr_t ws = g->engine.workspace.load( );
                const games::game_id gid = games::detect( place, ws );
                if ( games::set_current( gid, place ) )
                    on_game_changed( gid, place );
            }
        } while ( false );

        std::this_thread::sleep_for( std::chrono::milliseconds( 75 ) );
    }
}
