#include <include/global.cuh>
#include <src/engine/sdk/materialservice/materialservice.cuh>

namespace sdk
{

auto u_material_service::resolve( ) -> u_material_service
{
    uintptr_t dm_addr = 0;
    if ( g )
        dm_addr = g->engine.datamodel.load( );

    u_datamodel dm = dm_addr ? u_datamodel( dm_addr ) : u_datamodel::resolve( );
    if ( !dm )
        return {};

    u_instance by_class = dm.find_first_child_of_class( "MaterialService" );
    if ( by_class )
        return as<u_material_service>( by_class );

    return as<u_material_service>( dm.find_first_child( "MaterialService" ) );
}

auto u_material_service::use_2022_materials( ) const -> bool
{
    if ( !m_address )
        return false;
    return usermode->read<uint8_t>( m_address + offsets::material_service::use_2022_materials ) != 0;
}

auto u_material_service::set_use_2022_materials( bool v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uint8_t>( m_address + offsets::material_service::use_2022_materials, v ? 1 : 0 );
}

}
