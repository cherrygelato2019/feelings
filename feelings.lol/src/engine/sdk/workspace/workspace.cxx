#include <include/global.cuh>

namespace sdk
{

auto u_workspace::current_camera( ) const -> u_camera
{
    if ( !m_address )
        return u_camera( );
    return u_camera( usermode->read<uintptr_t>( m_address + offsets::workspace::current_camera ) );
}

auto u_workspace::distributed_game_time( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::workspace::distributed_game_time );
}

auto u_workspace::world( ) const -> uintptr_t
{
    if ( !m_address )
        return 0;
    return usermode->read<uintptr_t>( m_address + offsets::workspace::world );
}

auto u_workspace::gravity( ) const -> float
{
    if ( !m_address )
        return 0.f;
    const uintptr_t w = this->world( );
    if ( w )
    {
        const float g = usermode->read<float>( w + offsets::world::gravity );
        if ( g == g && g > 0.f )
            return g;
    }
    return usermode->read<float>( m_address + offsets::workspace::gravity );
}

auto u_workspace::set_gravity( float v ) const -> void
{
    if ( !m_address )
        return;
    const uintptr_t w = this->world( );
    if ( w )
        usermode->write<float>( w + offsets::world::gravity, v );
    usermode->write<float>( m_address + offsets::workspace::gravity, v );
}

}
