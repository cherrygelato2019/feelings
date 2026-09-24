#include <src/engine/cache/games/arsenal.hxx>
#include <src/engine/cache/games/common.hxx>

#include <unordered_set>

namespace games
{
namespace arsenal
{
namespace
{

auto is_r15_rig_name( const std::string& name ) -> bool
{
    static const char* names[] = {
        "Head", "HumanoidRootPart", "UpperTorso", "LowerTorso",
        "LeftUpperArm", "LeftLowerArm", "LeftHand",
        "RightUpperArm", "RightLowerArm", "RightHand",
        "LeftUpperLeg", "LeftLowerLeg", "LeftFoot",
        "RightUpperLeg", "RightLowerLeg", "RightFoot"
    };
    for ( const char* n : names )
        if ( name == n )
            return true;
    return false;
}

} // namespace

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    if ( !g->engine.workspace )
        return;

    auto owners = detail::build_char_owners( );
    std::unordered_set<uintptr_t> seen;
    seen.reserve( out.size( ) * 2 + 8 );
    for ( const auto& pe : out )
        if ( pe.character )
            seen.insert( pe.character );

    auto add_models = [&]( sdk::u_instance folder, bool as_player )
    {
        if ( !folder )
            return;
        for ( const auto& model : folder.get_children( ) )
        {
            if ( !model || model.class_name( ) != "Model" )
                continue;
            if ( ctx.local_char && model.address( ) == ctx.local_char )
                continue;
            if ( seen.count( model.address( ) ) )
                continue;

            cache::player entry{};
            entry.health = 100.f;
            entry.max_health = 100.f;
            entry.is_npc = !as_player;
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
                if ( cn != "Part" && cn != "MeshPart" )
                    continue;
                const std::string pname = child.name( );
                if ( !is_r15_rig_name( pname ) )
                    continue;
                detail::push_bone( entry, child, pname );
                if ( pname == "HumanoidRootPart" || pname == "Head" )
                    found_root = true;
            }

            if ( !found_root )
                continue;

            auto owner = owners.find( model.address( ) );
            if ( owner != owners.end( ) )
            {
                entry.address = owner->second.address( );
                entry.is_npc = false;
                entry.name = owner->second.display_name( );
                if ( entry.name.empty( ) )
                    entry.name = owner->second.name( );
                entry.display_name = entry.name;
                entry.team = owner->second.team( ).address( );
            }
            else
            {
                std::string name = model.name( );
                if ( name.empty( ) || name == "unknown" )
                    name = as_player ? "player" : "zombie";
                entry.name = name;
                entry.display_name = name;
            }

            seen.insert( model.address( ) );
            detail::finalize_entry( entry, ctx.local_pos );
            out.push_back( std::move( entry ) );
        }
    };

    sdk::u_instance ws( g->engine.workspace.load( ) );
    add_models( ws.find_first_child( "Characters" ), true );
    add_models( ws.find_first_child( "Zombies" ), false );
}

}
}
