#include <src/engine/cache/games/overkill.hxx>
#include <src/engine/cache/games/common.hxx>

#include <vector>

namespace games
{
namespace overkill
{
namespace
{

auto hitbox_to_r15( const std::string& name ) -> const char*
{
    if ( name.size( ) < 7 || name.compare( 0, 6, "Hitbox" ) != 0 )
        return nullptr;
    const std::string s = name.substr( 6 );
    if ( s == "Head" ) return "Head";
    if ( s == "Torso" || s == "UpperTorso" ) return "UpperTorso";
    if ( s == "LowerTorso" ) return "LowerTorso";
    if ( s == "LeftUpperArm" || s == "LeftArm" || s == "LArm" ) return "LeftUpperArm";
    if ( s == "LeftLowerArm" ) return "LeftLowerArm";
    if ( s == "LeftHand" ) return "LeftHand";
    if ( s == "RightUpperArm" || s == "RightArm" || s == "RArm" ) return "RightUpperArm";
    if ( s == "RightLowerArm" ) return "RightLowerArm";
    if ( s == "RightHand" ) return "RightHand";
    if ( s == "LeftUpperLeg" || s == "LeftLeg" || s == "LLeg" ) return "LeftUpperLeg";
    if ( s == "LeftLowerLeg" ) return "LeftLowerLeg";
    if ( s == "LeftFoot" ) return "LeftFoot";
    if ( s == "RightUpperLeg" || s == "RightLeg" || s == "RLeg" ) return "RightUpperLeg";
    if ( s == "RightLowerLeg" ) return "RightLowerLeg";
    if ( s == "RightFoot" ) return "RightFoot";
    return nullptr;
}

auto is_overkill_body_part( const std::string& name ) -> bool
{
    return name == "Head" || name == "HumanoidRootPart" || name == "UpperTorso" || name == "LowerTorso"
        || name == "LeftUpperArm" || name == "LeftLowerArm" || name == "LeftHand"
        || name == "RightUpperArm" || name == "RightLowerArm" || name == "RightHand"
        || name == "LeftUpperLeg" || name == "LeftLowerLeg" || name == "LeftFoot"
        || name == "RightUpperLeg" || name == "RightLowerLeg" || name == "RightFoot"
        || name == "Torso" || name == "Left Arm" || name == "Right Arm" || name == "Left Leg" || name == "Right Leg"
        || hitbox_to_r15( name ) != nullptr;
}

} // namespace

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    if ( !g->engine.workspace )
        return;

    std::vector<sdk::u_instance> char_roots;
    struct frame { uintptr_t addr; int depth; };
    std::vector<frame> queue;
    queue.push_back( { g->engine.workspace.load( ), 0 } );

    for ( size_t qi = 0; qi < queue.size( ); ++qi )
    {
        const uintptr_t addr = queue[ qi ].addr;
        const int depth = queue[ qi ].depth;
        if ( depth > 12 )
            continue;

        sdk::u_instance node( addr );
        for ( const auto& child : node.get_children( ) )
        {
            if ( !child )
                continue;
            if ( child.name( ) == "HitboxTorso" )
            {
                sdk::u_instance parent = child.parent( );
                if ( parent )
                    char_roots.push_back( parent );
                continue;
            }
            if ( depth < 12 )
                queue.push_back( { child.address( ), depth + 1 } );
        }
    }

    for ( const auto& char_root : char_roots )
    {
        if ( !char_root )
            continue;
        const std::string cname = char_root.name( );
        if ( cname.empty( ) || cname.rfind( "Dummy_", 0 ) == 0 )
            continue;

        sdk::u_humanoid humanoid = sdk::as<sdk::u_humanoid>( char_root.find_first_child_of_class( "Humanoid" ) );
        if ( !humanoid )
            continue;

        cache::player entry{};
        entry.humanoid = humanoid.address( );
        entry.name = cname;
        entry.display_name = cname;
        entry.character = char_root.address( );
        entry.address = char_root.address( );
        entry.health = humanoid.health( );
        entry.max_health = humanoid.max_health( );

        for ( const auto& child : char_root.get_children( ) )
        {
            const std::string cn = child.class_name( );
            if ( cn != "Part" && cn != "MeshPart" )
                continue;
            const std::string pname = child.name( );
            if ( pname.empty( ) || !is_overkill_body_part( pname ) )
                continue;
            detail::push_bone( entry, child, pname );
            if ( const char* mapped = hitbox_to_r15( pname ) )
            {
                if ( const auto* bone = cache::find_bone( entry, pname.c_str( ) ) )
                    detail::add_bone_alias( entry, mapped, *bone );
            }
        }

        if ( entry.bones.empty( ) )
            continue;

        detail::add_bone_alias_named( entry, "UpperTorso", "HitboxTorso" );
        detail::add_bone_alias_named( entry, "LowerTorso", "UpperTorso" );
        detail::add_bone_alias_named( entry, "HumanoidRootPart", "UpperTorso" );
        detail::add_bone_alias_named( entry, "LeftLowerArm", "LeftUpperArm" );
        detail::add_bone_alias_named( entry, "LeftHand", "LeftLowerArm" );
        detail::add_bone_alias_named( entry, "RightLowerArm", "RightUpperArm" );
        detail::add_bone_alias_named( entry, "RightHand", "RightLowerArm" );
        detail::add_bone_alias_named( entry, "LeftLowerLeg", "LeftUpperLeg" );
        detail::add_bone_alias_named( entry, "LeftFoot", "LeftLowerLeg" );
        detail::add_bone_alias_named( entry, "RightLowerLeg", "RightUpperLeg" );
        detail::add_bone_alias_named( entry, "RightFoot", "RightLowerLeg" );
        detail::add_bone_alias_named( entry, "Head", "UpperTorso" );
        detail::add_bone_alias_named( entry, "LeftUpperArm", "UpperTorso" );
        detail::add_bone_alias_named( entry, "RightUpperArm", "UpperTorso" );
        detail::add_bone_alias_named( entry, "LeftUpperLeg", "UpperTorso" );
        detail::add_bone_alias_named( entry, "RightUpperLeg", "UpperTorso" );

        if ( detail::parts_contain_prim( entry, ctx.local_prim ) )
            continue;

        detail::finalize_entry( entry, ctx.local_pos );
        out.push_back( std::move( entry ) );
    }
}

}
}
