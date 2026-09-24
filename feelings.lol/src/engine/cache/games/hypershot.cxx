#include <src/engine/cache/games/hypershot.hxx>
#include <src/engine/cache/games/common.hxx>

#include <cstdio>
#include <unordered_set>

namespace games
{
namespace hypershot
{

auto is_active( uintptr_t workspace ) -> bool
{
    if ( !workspace )
        return false;
    sdk::u_instance ws( workspace );
    return ( bool )ws.find_first_child( "Mobs" );
}

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    if ( !g->engine.workspace )
        return;

    sdk::u_instance ws( g->engine.workspace.load( ) );
    sdk::u_instance mobs = ws.find_first_child( "Mobs" );
    if ( !mobs )
        return;

    std::unordered_set<uintptr_t> seen;
    for ( const auto& pe : out )
        if ( pe.character )
            seen.insert( pe.character );

    for ( const auto& model : mobs.get_children( ) )
    {
        if ( !model || seen.count( model.address( ) ) )
            continue;

        std::string mname = model.name( );
        if ( mname.empty( ) )
            mname = "Mob";

        cache::player entry{};
        entry.character = model.address( );
        entry.address = model.address( );
        entry.is_npc = true;

        char keybuf[ 128 ];
        std::snprintf( keybuf, sizeof( keybuf ), "[Mob]%s_%llx", mname.c_str( ), ( unsigned long long )model.address( ) );
        entry.name = keybuf;
        entry.display_name = mname;

        for ( const auto& child : model.get_children( ) )
        {
            if ( child.class_name( ).find( "Part" ) == std::string::npos )
                continue;
            if ( child.name( ).empty( ) )
                continue;
            detail::push_bone( entry, child, child.name( ) );
        }

        if ( entry.bones.empty( ) )
            continue;

        sdk::u_humanoid hum = sdk::as<sdk::u_humanoid>( model.find_first_child_of_class( "Humanoid" ) );
        if ( hum )
        {
            entry.humanoid = hum.address( );
            entry.health = hum.health( );
            entry.max_health = hum.max_health( );
        }
        else
        {
            entry.health = 100.f;
            entry.max_health = 100.f;
        }

        detail::finalize_entry( entry, ctx.local_pos );
        out.push_back( std::move( entry ) );
    }
}

}
}
