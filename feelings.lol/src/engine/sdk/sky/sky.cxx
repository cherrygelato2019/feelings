#include <include/global.cuh>
#include <src/engine/sdk/sky/sky.cuh>
#include <src/engine/sdk/lighting/lighting.cuh>

namespace sdk
{

auto u_sky::resolve( ) -> u_sky
{
    const auto lighting = u_lighting::resolve( );
    if ( !lighting )
        return {};

    // Prefer live child lookup — Lighting.Sky pointer can lag / be stale.
    u_instance by_class = lighting.find_first_child_of_class( "Sky" );
    if ( by_class )
        return as<u_sky>( by_class );

    u_instance by_name = lighting.find_first_child( "Sky" );
    if ( by_name )
        return as<u_sky>( by_name );

    const uintptr_t ptr = lighting.sky( );
    if ( ptr )
        return u_sky( ptr );

    return {};
}

auto u_sky::celestial_bodies_shown( ) const -> bool
{
    if ( !m_address )
        return false;
    return usermode->read<uint8_t>( m_address + offsets::sky::celestial_bodies_shown ) != 0;
}

auto u_sky::set_celestial_bodies_shown( bool v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uint8_t>( m_address + offsets::sky::celestial_bodies_shown, v ? 1 : 0 );
}

auto u_sky::star_count( ) const -> int
{
    if ( !m_address )
        return 0;
    return usermode->read<int>( m_address + offsets::sky::star_count );
}

auto u_sky::set_star_count( int v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<int>( m_address + offsets::sky::star_count, v );
}

auto u_sky::sun_angular_size( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::sky::sun_angular_size );
}

auto u_sky::set_sun_angular_size( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::sky::sun_angular_size, v );
}

auto u_sky::moon_angular_size( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::sky::moon_angular_size );
}

auto u_sky::set_moon_angular_size( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::sky::moon_angular_size, v );
}

auto u_sky::orientation( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    // SkyboxOrientation is a Vector3 on modern clients (pitch/yaw/roll degrees).
    return usermode->read<math::vector3_t>( m_address + offsets::sky::skybox_orientation );
}

auto u_sky::set_orientation( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::sky::skybox_orientation, v );
}

auto u_sky::set_skybox_faces(
    const char* bk,
    const char* dn,
    const char* ft,
    const char* lf,
    const char* rt,
    const char* up ) const -> void
{
    if ( !m_address )
        return;

    auto write_face = [ this ]( uintptr_t off, const char* s )
    {
        if ( !s || !*s )
            return;
        usermode->write_string( m_address + off, std::string( s ) );
    };

    write_face( offsets::sky::skybox_bk, bk );
    write_face( offsets::sky::skybox_dn, dn );
    write_face( offsets::sky::skybox_ft, ft );
    write_face( offsets::sky::skybox_lf, lf );
    write_face( offsets::sky::skybox_rt, rt );
    write_face( offsets::sky::skybox_up, up );
}

}
