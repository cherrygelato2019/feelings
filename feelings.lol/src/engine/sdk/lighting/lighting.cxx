#include <include/global.cuh>
#include <src/engine/sdk/lighting/lighting.cuh>

namespace sdk
{

auto u_lighting::resolve( ) -> u_lighting
{
    uintptr_t dm_addr = 0;
    if ( g )
        dm_addr = g->engine.datamodel.load( );

    u_datamodel dm = dm_addr ? u_datamodel( dm_addr ) : u_datamodel::resolve( );
    if ( !dm )
        return {};

    return dm.lighting( );
}

auto u_lighting::brightness( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::brightness );
}

auto u_lighting::set_brightness( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::brightness, v );
}

auto u_lighting::exposure_compensation( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::exposure_compensation );
}

auto u_lighting::set_exposure_compensation( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::exposure_compensation, v );
}

auto u_lighting::clock_time( ) const -> float
{
    if ( !m_address )
        return 0.f;
    const int64_t us = usermode->read<int64_t>( m_address + offsets::lighting::time_microseconds );
    double hours = ( ( double )us / 1000000.0 ) / 3600.0;
    while ( hours >= 24.0 )
        hours -= 24.0;
    while ( hours < 0.0 )
        hours += 24.0;
    return ( float )hours;
}

auto u_lighting::set_clock_time( float hours ) const -> void
{
    if ( !m_address )
        return;
    while ( hours >= 24.f )
        hours -= 24.f;
    while ( hours < 0.f )
        hours += 24.f;
    const int64_t us = ( int64_t )( ( double )hours * 3600.0 * 1000000.0 );
    usermode->write<int64_t>( m_address + offsets::lighting::time_microseconds, us );
}

auto u_lighting::geographic_latitude( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::geographic_latitude );
}

auto u_lighting::set_geographic_latitude( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::geographic_latitude, v );
}

auto u_lighting::environment_diffuse_scale( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::environment_diffuse_scale );
}

auto u_lighting::set_environment_diffuse_scale( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::environment_diffuse_scale, v );
}

auto u_lighting::environment_specular_scale( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::environment_specular_scale );
}

auto u_lighting::set_environment_specular_scale( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::environment_specular_scale, v );
}

auto u_lighting::global_shadows( ) const -> bool
{
    if ( !m_address )
        return false;
    return usermode->read<uint8_t>( m_address + offsets::lighting::global_shadows ) != 0;
}

auto u_lighting::set_global_shadows( bool v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uint8_t>( m_address + offsets::lighting::global_shadows, v ? 1 : 0 );
}

auto u_lighting::shadow_softness( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::shadow_softness );
}

auto u_lighting::set_shadow_softness( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::shadow_softness, v );
}

auto u_lighting::prioritize_lighting_quality( ) const -> bool
{
    if ( !m_address )
        return false;
    return usermode->read<uint8_t>( m_address + offsets::lighting::prioritize_lighting_quality ) != 0;
}

auto u_lighting::set_prioritize_lighting_quality( bool v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uint8_t>( m_address + offsets::lighting::prioritize_lighting_quality, v ? 1 : 0 );
}

auto u_lighting::ambient( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::ambient );
}

auto u_lighting::set_ambient( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::ambient, v );
}

auto u_lighting::outdoor_ambient( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::outdoor_ambient );
}

auto u_lighting::set_outdoor_ambient( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::outdoor_ambient, v );
}

auto u_lighting::color_shift_top( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::color_shift_top );
}

auto u_lighting::set_color_shift_top( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::color_shift_top, v );
}

auto u_lighting::color_shift_bottom( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::color_shift_bottom );
}

auto u_lighting::set_color_shift_bottom( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::color_shift_bottom, v );
}

auto u_lighting::shadow_color( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::shadow_color );
}

auto u_lighting::set_shadow_color( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::shadow_color, v );
}

auto u_lighting::sky_ambient( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::sky_ambient );
}

auto u_lighting::set_sky_ambient( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::sky_ambient, v );
}

auto u_lighting::light_color( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::light_color );
}

auto u_lighting::set_light_color( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::light_color, v );
}

auto u_lighting::fog_color( ) const -> math::vector3_t
{
    if ( !m_address )
        return {};
    return usermode->read<math::vector3_t>( m_address + offsets::lighting::fog_color );
}

auto u_lighting::set_fog_color( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::lighting::fog_color, v );
}

auto u_lighting::fog_start( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::fog_start );
}

auto u_lighting::set_fog_start( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::fog_start, v );
}

auto u_lighting::fog_end( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::lighting::fog_end );
}

auto u_lighting::set_fog_end( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::lighting::fog_end, v );
}

auto u_lighting::atmosphere( ) const -> uintptr_t
{
    if ( !m_address )
        return 0;
    return usermode->read<uintptr_t>( m_address + offsets::lighting::atmosphere );
}

auto u_lighting::set_atmosphere( uintptr_t v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uintptr_t>( m_address + offsets::lighting::atmosphere, v );
}

auto u_lighting::sky( ) const -> uintptr_t
{
    if ( !m_address )
        return 0;
    return usermode->read<uintptr_t>( m_address + offsets::lighting::sky );
}

}
