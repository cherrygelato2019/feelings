#include <src/engine/cache/games/games.cuh>

#include <src/engine/cache/games/arsenal.hxx>
#include <src/engine/cache/games/badbusiness.hxx>
#include <src/engine/cache/games/bloxstrike.hxx>
#include <src/engine/cache/games/deadline.hxx>
#include <src/engine/cache/games/frontlines.hxx>
#include <src/engine/cache/games/havoc.hxx>
#include <src/engine/cache/games/hypershot.hxx>
#include <src/engine/cache/games/overkill.hxx>
#include <src/engine/cache/games/rivals.hxx>

#include <src/engine/offsets/offsets.cuh>
#include <src/engine/sdk/instance/instance.cuh>

#include <include/global.cuh>

#include <atomic>

namespace games
{
namespace
{
std::atomic<int> g_current{ ( int )game_id::none };
std::atomic<int64_t> g_place{ 0 };
}

auto detect( int64_t place_id, uintptr_t workspace ) -> game_id
{
    // Qualify places::* — names collide with games::arsenal / frontlines / etc. namespaces.
    if ( place_id == offsets::places::phantom_forces_a || place_id == offsets::places::phantom_forces_b )
        return game_id::phantom_forces;
    if ( place_id == offsets::places::bad_business_a || place_id == offsets::places::bad_business_b )
        return game_id::bad_business;
    if ( place_id == offsets::places::frontlines )
        return game_id::frontlines;
    if ( place_id == offsets::places::overkill )
        return game_id::overkill;
    if ( place_id == offsets::places::arsenal )
        return game_id::arsenal;
    if ( place_id == offsets::places::deadline )
        return game_id::deadline;
    if ( place_id == offsets::places::havoc )
        return game_id::havoc;
    if ( place_id == offsets::places::bloxstrike )
        return game_id::bloxstrike;
    if ( place_id == offsets::places::rivals )
        return game_id::rivals;
    if ( place_id == offsets::places::multiversal )
        return game_id::multiversal;

    // Hypershot: no place_id in njx — detect Workspace.Mobs folder.
    if ( hypershot::is_active( workspace ) )
        return game_id::hypershot;

    return game_id::none;
}

auto name( game_id id ) -> const char*
{
    switch ( id )
    {
    case game_id::phantom_forces: return "Phantom Forces";
    case game_id::arsenal: return "Arsenal";
    case game_id::bad_business: return "Bad Business";
    case game_id::bloxstrike: return "BloxStrike";
    case game_id::deadline: return "Deadline";
    case game_id::frontlines: return "Frontlines";
    case game_id::havoc: return "Havoc";
    case game_id::hypershot: return "Hypershot";
    case game_id::overkill: return "Overkill";
    case game_id::rivals: return "Rivals";
    case game_id::multiversal: return "Multiversal";
    case game_id::none:
    default: return "Universal";
    }
}

auto current( ) -> game_id
{
    return static_cast<game_id>( g_current.load( std::memory_order_relaxed ) );
}

auto current_place( ) -> int64_t
{
    return g_place.load( std::memory_order_relaxed );
}

auto set_current( game_id id, int64_t place_id ) -> bool
{
    const int prev = g_current.exchange( ( int )id, std::memory_order_relaxed );
    const int64_t prev_place = g_place.exchange( place_id, std::memory_order_relaxed );
    return prev != ( int )id || prev_place != place_id;
}

auto replaces_default( game_id id ) -> bool
{
    switch ( id )
    {
    case game_id::phantom_forces:
    case game_id::bad_business:
    case game_id::frontlines:
    case game_id::overkill:
        return true;
    default:
        return false;
    }
}

auto cache_players( game_id id, std::vector<cache::player>& out, const cache_ctx& ctx ) -> bool
{
    switch ( id )
    {
    case game_id::bad_business:
        badbusiness::cache_players( out, ctx );
        return true;
    case game_id::frontlines:
        frontlines::cache_players( out, ctx );
        return true;
    case game_id::overkill:
        overkill::cache_players( out, ctx );
        return true;
    case game_id::arsenal:
        arsenal::cache_players( out, ctx );
        return false;
    case game_id::deadline:
        deadline::cache_players( out, ctx );
        return false;
    case game_id::havoc:
        havoc::cache_players( out, ctx );
        return false;
    case game_id::hypershot:
        hypershot::cache_players( out, ctx );
        return false;
    case game_id::bloxstrike:
        bloxstrike::cache_players( out, ctx );
        return false;
    case game_id::rivals:
        rivals::cache_players( out, ctx );
        return false;
    case game_id::phantom_forces:
    case game_id::none:
    default:
        return false;
    }
}

auto is_teammate( game_id id, const cache::player& p, uintptr_t local_char ) -> bool
{
    switch ( id )
    {
    case game_id::bloxstrike:
        return bloxstrike::is_teammate( p.character, local_char );
    case game_id::rivals:
        return rivals::is_teammate( p.character );
    default:
    {
        const uintptr_t client_team = g->engine.client_team.load( );
        return client_team != 0 && p.team != 0 && p.team == client_team;
    }
    }
}

auto apply_teammates( game_id id, std::vector<cache::player>& players, uintptr_t local_char ) -> void
{
    const uintptr_t client_team = g->engine.client_team.load( );
    std::string local_team_name;
    if ( client_team )
        local_team_name = sdk::u_instance( client_team ).name( );

    // Live local team if engine cache is stale
    if ( local_team_name.empty( ) && g->engine.players )
    {
        sdk::u_players service( g->engine.players.load( ) );
        sdk::u_player lp = service.local_player( );
        if ( lp )
        {
            sdk::u_instance t = lp.team( );
            if ( t )
            {
                g->engine.client_team.store( t.address( ), std::memory_order_relaxed );
                local_team_name = t.name( );
            }
        }
    }

    for ( auto& p : players )
    {
        if ( p.is_npc )
        {
            p.teammate = false;
            continue;
        }

        bool ally = is_teammate( id, p, local_char );
        if ( !ally && client_team && p.team && p.team == client_team )
            ally = true;
        if ( !ally && p.same_team_name( local_team_name ) )
            ally = true;
        // PF-style marker already on entry
        if ( !ally && p.team == 1 )
            ally = true;
        p.teammate = ally;
    }
}

auto clear_state( ) -> void
{
    // Per-game pin maps live in individual modules if added later.
    // PF pins are cleared via pf_cache::clear_pins from cache.cxx.
}

}
