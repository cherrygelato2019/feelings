#include <include/global.cuh>

#include <cmath>

namespace sdk
{

auto u_base_part::primitive( ) const -> uintptr_t
{
    if ( !m_address )
        return 0;

    static constexpr uintptr_t k_prim_offs[] = {
        offsets::basepart::primitive,
        0x188,
        0x148,
        0x140,
        0x158,
        0x170,
    };

    for ( uintptr_t off : k_prim_offs )
    {
        const uintptr_t prim = usermode->read<uintptr_t>( m_address + off );
        if ( !prim || prim < 0x10000 || !usermode->is_valid( prim ) )
            continue;

        const math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
        if ( pos.is_valid( )
            && ( std::fabs( pos.x ) + std::fabs( pos.y ) + std::fabs( pos.z ) ) > 0.01f
            && ( std::fabs( pos.x ) + std::fabs( pos.y ) + std::fabs( pos.z ) ) < 1.0e7f )
            return prim;
        return prim;
    }
    return 0;
}

auto u_base_part::position( ) const -> math::vector3_t
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return math::vector3_t::nan( );

    const math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
    return pos.is_valid( ) ? pos : math::vector3_t::nan( );
}

auto u_base_part::set_position( const math::vector3_t& pos ) const -> void
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return;
    usermode->write<math::vector3_t>( prim + offsets::primitive::position, pos );
}

auto u_base_part::orientation( ) const -> math::matrix3_t
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return { };
    return usermode->read<math::matrix3_t>( prim + offsets::primitive::rotation );
}

auto u_base_part::cframe( ) const -> math::cframe_t
{
    return { this->orientation( ), this->position( ) };
}

auto u_base_part::set_cframe( const math::cframe_t& cf ) const -> void
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return;
    usermode->write<math::matrix3_t>( prim + offsets::primitive::rotation, cf.rotation );
    usermode->write<math::vector3_t>( prim + offsets::primitive::position, cf.position );
}

auto u_base_part::size( ) const -> math::vector3_t
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return { };
    return usermode->read<math::vector3_t>( prim + offsets::primitive::size );
}

auto u_base_part::velocity( ) const -> math::vector3_t
{
    return this->assembly_linear_velocity( );
}

auto u_base_part::set_velocity( const math::vector3_t& v ) const -> void
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return;
    usermode->write<math::vector3_t>( prim + offsets::primitive::linear_velocity, v );
}

auto u_base_part::assembly_linear_velocity( ) const -> math::vector3_t
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return { };
    return usermode->read<math::vector3_t>( prim + offsets::primitive::linear_velocity );
}

auto u_base_part::assembly_angular_velocity( ) const -> math::vector3_t
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return { };
    return usermode->read<math::vector3_t>( prim + offsets::primitive::angular_velocity );
}

auto u_base_part::set_angular_velocity( const math::vector3_t& v ) const -> void
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return;
    usermode->write<math::vector3_t>( prim + offsets::primitive::angular_velocity, v );
}

auto u_base_part::anchored( ) const -> bool
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return false;
    return ( usermode->read<uint8_t>( prim + offsets::primitive::flags ) & offsets::primitive_flags::anchored ) != 0;
}

auto u_base_part::can_collide( ) const -> bool
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return false;
    return ( usermode->read<uint8_t>( prim + offsets::primitive::flags ) & offsets::primitive_flags::can_collide ) != 0;
}

auto u_base_part::can_query( ) const -> bool
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return false;
    return ( usermode->read<uint8_t>( prim + offsets::primitive::flags ) & offsets::primitive_flags::can_query ) != 0;
}

auto u_base_part::can_touch( ) const -> bool
{
    uintptr_t prim = this->primitive( );
    if ( !prim )
        return false;
    return ( usermode->read<uint8_t>( prim + offsets::primitive::flags ) & offsets::primitive_flags::can_touch ) != 0;
}

} 
