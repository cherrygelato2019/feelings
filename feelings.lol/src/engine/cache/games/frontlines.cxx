#include <src/engine/cache/games/frontlines.hxx>
#include <src/engine/cache/games/common.hxx>

#include <cstdio>

namespace games
{
namespace frontlines
{

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    if ( !g->engine.workspace )
        return;

    auto owners = detail::build_char_owners( );
    sdk::u_instance workspace( g->engine.workspace.load( ) );

    for ( const auto& model : workspace.get_children( ) )
    {
        if ( model.name( ) != "soldier_model" )
            continue;
        if ( ctx.local_char && model.address( ) == ctx.local_char )
            continue;
        if ( model.find_first_child( "friendly_marker" ) )
            continue;

        sdk::u_instance hrp = model.find_first_child( "HumanoidRootPart" );
        if ( !hrp )
            continue;

        sdk::u_base_part hrp_bp = sdk::as<sdk::u_base_part>( hrp );
        if ( !hrp_bp.primitive( ) )
            continue;
        if ( ctx.local_prim && hrp_bp.primitive( ) == ctx.local_prim )
            continue;

        float hip_height = 1.35f;
        sdk::u_humanoid hum = sdk::as<sdk::u_humanoid>( model.find_first_child( "Humanoid" ) );
        if ( hum )
            hip_height = usermode->read<float>( hum.address( ) + offsets::humanoid::hip_height );

        const math::vector3_t full_body{ 2.0f, ( hip_height + 1.5f ) * 2.0f, 1.0f };
        const math::vector3_t head_sz{ 1.0f, 1.0f, 1.0f };
        const math::vector3_t limb_sz{ 0.5f, hip_height, 0.5f };
        const math::vector3_t foot_sz{ 0.5f, 0.3f, 0.7f };

        cache::player entry{};
        entry.character = model.address( );
        entry.humanoid = hum ? hum.address( ) : 0;

        auto owner_it = owners.find( model.address( ) );
        if ( owner_it != owners.end( ) )
        {
            entry.address = owner_it->second.address( );
            entry.name = owner_it->second.name( );
            entry.display_name = owner_it->second.display_name( );
            entry.team = owner_it->second.team( ).address( );
            if ( ctx.local_addr && entry.address == ctx.local_addr )
                continue;
        }
        else
        {
            char buf[ 40 ];
            std::snprintf( buf, sizeof( buf ), "FL_%llx", ( unsigned long long )model.address( ) );
            entry.name = buf;
            entry.display_name = buf;
            entry.address = model.address( );
        }

        auto bind_fake = [&]( const char* name, const math::vector3_t& size )
        {
            cache::player::bone b{};
            b.name = name;
            b.part = hrp.address( );
            b.primitive = hrp_bp.primitive( );
            b.size = size;
            b.position = hrp_bp.position( );
            b.rotation = hrp_bp.orientation( );
            b.velocity = hrp_bp.assembly_linear_velocity( );
            entry.bones.push_back( std::move( b ) );
        };

        bind_fake( "HumanoidRootPart", full_body );
        bind_fake( "UpperTorso", full_body );
        bind_fake( "LowerTorso", full_body );
        bind_fake( "Head", head_sz );
        bind_fake( "LeftUpperArm", limb_sz );
        bind_fake( "RightUpperArm", limb_sz );
        bind_fake( "LeftUpperLeg", limb_sz );
        bind_fake( "RightUpperLeg", limb_sz );
        bind_fake( "LeftFoot", foot_sz );
        bind_fake( "RightFoot", foot_sz );

        if ( hum )
        {
            entry.health = hum.health( );
            entry.max_health = hum.max_health( );
        }
        else
        {
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
