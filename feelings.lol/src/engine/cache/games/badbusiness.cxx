#include <src/engine/cache/games/badbusiness.hxx>
#include <src/engine/cache/games/common.hxx>

namespace games
{
namespace badbusiness
{
namespace
{

auto is_bb_bone( const std::string& name ) -> bool
{
    return name == "Head" || name == "Abdomen" || name == "Chest" || name == "LeftArm"
        || name == "RightArm" || name == "LeftLeg" || name == "RightLeg" || name == "Root";
}

} // namespace

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    if ( !g->engine.workspace )
        return;

    sdk::u_instance workspace( g->engine.workspace.load( ) );
    sdk::u_instance characters = workspace.find_first_child( "Characters" );
    if ( !characters )
        return;

    auto owners = detail::build_char_owners( );

    for ( const auto& model : characters.get_children( ) )
    {
        if ( !model )
            continue;

        const std::string mname = model.name( );
        if ( mname.empty( ) )
            continue;

        sdk::u_instance body = model.find_first_child( "Body" );
        if ( !body )
            continue;

        sdk::u_instance abdomen = body.find_first_child( "Abdomen" );
        if ( abdomen && ctx.cam_pos.is_valid( ) )
        {
            sdk::u_base_part abd = sdk::as<sdk::u_base_part>( abdomen );
            const math::vector3_t pos = abd.position( );
            if ( pos.is_valid( ) )
            {
                const float dx = pos.x - ctx.cam_pos.x;
                const float dy = pos.y - ctx.cam_pos.y;
                const float dz = pos.z - ctx.cam_pos.z;
                if ( dx * dx + dy * dy + dz * dz < 25.f )
                    continue;
            }
        }

        cache::player entry{};
        entry.character = model.address( );
        entry.address = model.address( );

        auto owner_it = owners.find( model.address( ) );
        if ( owner_it != owners.end( ) )
        {
            const sdk::u_player& p = owner_it->second;
            entry.address = p.address( );
            entry.name = p.name( );
            entry.display_name = p.display_name( );
            entry.user_id = p.user_id( );
            entry.team = p.team( ).address( );
            if ( entry.team )
                entry.team_name = p.team( ).name( );
            if ( ctx.local_addr && entry.address == ctx.local_addr )
                continue;
        }
        else
        {
            entry.name = mname;
            entry.display_name = mname;
        }

        for ( const auto& child : body.get_children( ) )
        {
            const std::string cn = child.class_name( );
            if ( cn.find( "Part" ) == std::string::npos )
                continue;
            const std::string pname = child.name( );
            if ( !is_bb_bone( pname ) )
                continue;
            detail::push_bone( entry, child, pname );
            const auto* bone = cache::find_bone( entry, pname.c_str( ) );
            if ( !bone )
                continue;
            if ( pname == "Abdomen" )
            {
                detail::add_bone_alias( entry, "HumanoidRootPart", *bone );
                detail::add_bone_alias( entry, "LowerTorso", *bone );
            }
            else if ( pname == "Chest" )
                detail::add_bone_alias( entry, "UpperTorso", *bone );
            else if ( pname == "LeftArm" )
                detail::add_bone_alias( entry, "LeftUpperArm", *bone );
            else if ( pname == "RightArm" )
                detail::add_bone_alias( entry, "RightUpperArm", *bone );
            else if ( pname == "LeftLeg" )
                detail::add_bone_alias( entry, "LeftUpperLeg", *bone );
            else if ( pname == "RightLeg" )
                detail::add_bone_alias( entry, "RightUpperLeg", *bone );
        }

        sdk::u_instance root_part = model.find_first_child( "Root" );
        if ( root_part )
        {
            detail::push_bone( entry, root_part, "Root" );
            if ( const auto* rp = cache::find_bone( entry, "Root" ) )
            {
                detail::add_bone_alias( entry, "LeftFoot", *rp );
                detail::add_bone_alias( entry, "RightFoot", *rp );
            }
        }

        if ( entry.bones.empty( ) )
            continue;
        if ( detail::parts_contain_prim( entry, ctx.local_prim ) )
            continue;

        sdk::u_instance health_obj = model.find_first_child( "Health" );
        if ( health_obj )
        {
            entry.health_nv = health_obj.address( );
            entry.health = detail::read_number_value( health_obj );
            sdk::u_instance max_obj = health_obj.find_first_child( "MaxHealth" );
            if ( max_obj )
            {
                entry.max_health_nv = max_obj.address( );
                entry.max_health = detail::read_number_value( max_obj );
            }
            else
                entry.max_health = 100.f;
        }
        else
        {
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
        }

        detail::finalize_entry( entry, ctx.local_pos );
        out.push_back( std::move( entry ) );
    }
}

}
}
