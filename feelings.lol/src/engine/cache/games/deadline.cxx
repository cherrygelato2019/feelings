#include <src/engine/cache/games/deadline.hxx>
#include <src/engine/cache/games/common.hxx>

#include <unordered_set>

namespace games
{
namespace deadline
{
namespace
{

auto is_deadline_bone( const std::string& name ) -> bool
{
    return name == "head" || name == "torso" || name == "right_arm_vis" || name == "left_arm_vis"
        || name == "right_leg_vis" || name == "left_leg_vis";
}

} // namespace

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    if ( !g->engine.workspace )
        return;

    sdk::u_instance workspace( g->engine.workspace.load( ) );
    sdk::u_instance chars_folder = workspace.find_first_child( "characters" );
    if ( !chars_folder )
        return;

    auto owners = detail::build_char_owners( );
    std::unordered_set<uintptr_t> seen;
    for ( const auto& pe : out )
        if ( pe.character )
            seen.insert( pe.character );

    for ( const auto& model : chars_folder.get_children( ) )
    {
        if ( !model )
            continue;
        const std::string mname = model.name( );
        if ( mname.empty( ) || mname == "StarterCharacter" )
            continue;
        if ( seen.count( model.address( ) ) )
            continue;

        cache::player entry{};
        entry.character = model.address( );
        entry.address = model.address( );
        entry.name = mname;
        entry.display_name = mname;

        auto info_it = owners.find( model.address( ) );
        if ( info_it != owners.end( ) )
        {
            if ( ctx.local_addr && info_it->second.address( ) == ctx.local_addr )
                continue;
            if ( info_it->second.name( ) == ctx.local_name )
                continue;
            entry.address = info_it->second.address( );
            entry.name = info_it->second.name( );
            entry.display_name = info_it->second.display_name( );
            entry.team = info_it->second.team( ).address( );
        }

        for ( const auto& child : model.get_children( ) )
        {
            const std::string pname = child.name( );
            if ( !is_deadline_bone( pname ) )
                continue;
            if ( child.class_name( ).find( "Part" ) == std::string::npos )
                continue;
            detail::push_bone( entry, child, pname );
            const auto* bone = cache::find_bone( entry, pname.c_str( ) );
            if ( !bone )
                continue;
            if ( pname == "head" )
                detail::add_bone_alias( entry, "Head", *bone );
            else if ( pname == "torso" )
            {
                detail::add_bone_alias( entry, "UpperTorso", *bone );
                detail::add_bone_alias( entry, "HumanoidRootPart", *bone );
            }
            else if ( pname == "left_arm_vis" )
                detail::add_bone_alias( entry, "LeftUpperArm", *bone );
            else if ( pname == "right_arm_vis" )
                detail::add_bone_alias( entry, "RightUpperArm", *bone );
            else if ( pname == "left_leg_vis" )
                detail::add_bone_alias( entry, "LeftUpperLeg", *bone );
            else if ( pname == "right_leg_vis" )
                detail::add_bone_alias( entry, "RightUpperLeg", *bone );
        }

        if ( entry.bones.empty( ) )
            continue;
        if ( detail::parts_contain_prim( entry, ctx.local_prim ) )
            continue;
        if ( ctx.local_addr && entry.address == ctx.local_addr )
            continue;

        sdk::u_humanoid hum = sdk::as<sdk::u_humanoid>( model.find_first_child( "Humanoid" ) );
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

        seen.insert( model.address( ) );
        detail::finalize_entry( entry, ctx.local_pos );
        out.push_back( std::move( entry ) );
    }
}

}
}
