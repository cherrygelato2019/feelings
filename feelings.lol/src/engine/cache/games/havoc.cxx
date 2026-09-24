#include <src/engine/cache/games/havoc.hxx>
#include <src/engine/cache/games/common.hxx>

#include <unordered_set>

namespace games
{
namespace havoc
{

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    if ( !g->engine.workspace )
        return;

    std::unordered_set<uintptr_t> known;
    if ( ctx.local_char )
        known.insert( ctx.local_char );
    for ( const auto& pe : out )
        if ( pe.character )
            known.insert( pe.character );

    sdk::u_instance ws( g->engine.workspace.load( ) );
    sdk::u_instance root{};

    for ( const auto& child : ws.get_children( ) )
    {
        if ( !child )
            continue;
        const std::string name = child.name( );
        if ( name == "Buildings" || name == "Camera" || name == "Terrain" || name == "Loots" )
            continue;

        int humanoids = 0;
        for ( const auto& model : child.get_children( ) )
        {
            if ( model.class_name( ) != "Model" )
                continue;
            if ( model.find_first_child( "Humanoid" ) || model.find_first_child( "HumanoidRootPart" ) )
                ++humanoids;
        }

        if ( humanoids >= 2 || ( humanoids >= 1 && child.find_first_child( "NPCs" ) ) )
        {
            root = child;
            break;
        }
    }

    if ( !root )
        return;

    for ( const auto& model : root.get_children( ) )
    {
        if ( !model || model.class_name( ) != "Model" )
            continue;
        if ( ctx.local_char && model.address( ) == ctx.local_char )
            continue;
        if ( known.count( model.address( ) ) )
            continue;

        const bool bot = model.find_first_child( "ActiveScripts" ) || model.find_first_child( "WeldObjectsLink" );
        if ( !bot )
            continue;

        cache::player entry{};
        entry.is_npc = true;
        entry.character = model.address( );
        entry.address = model.address( );

        bool found_root = false;
        for ( const auto& child : model.get_children( ) )
        {
            const std::string cn = child.class_name( );
            if ( cn == "Humanoid" )
            {
                sdk::u_humanoid h = sdk::as<sdk::u_humanoid>( child );
                entry.humanoid = h.address( );
                entry.health = h.health( );
                entry.max_health = h.max_health( );
                continue;
            }

            sdk::u_instance source{};
            if ( cn == "Part" || cn == "MeshPart" )
                source = child;
            else if ( cn == "Tool" )
            {
                entry.tool = child.name( );
                source = child.find_first_child( "Handle" );
            }
            else if ( cn == "Accessory" || cn == "Hat" || cn == "Accoutrement" )
                source = child.find_first_child( "Handle" );

            if ( !source )
                continue;

            const std::string pname = source.name( );
            detail::push_bone( entry, source, pname );
            if ( pname == "HumanoidRootPart" || pname == "Head" )
                found_root = true;
        }

        if ( !found_root )
            continue;

        entry.name = model.name( );
        if ( entry.name.empty( ) )
            entry.name = "bot";
        entry.display_name = entry.name;

        known.insert( model.address( ) );
        detail::finalize_entry( entry, ctx.local_pos );
        out.push_back( std::move( entry ) );
    }
}

}
}
