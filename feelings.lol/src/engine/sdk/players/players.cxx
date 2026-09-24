#include <include/global.cuh>

namespace sdk
{

auto u_players::local_player( ) const -> u_player
{
    if ( !m_address )
        return u_player( );
    return u_player( usermode->read<uintptr_t>( m_address + offsets::players::local_player ) );
}

auto u_players::get_players( ) const -> std::vector<u_player>
{
    std::vector<u_player> out;
    for ( const auto& child : this->get_children( ) )
    {
        if ( child.class_name( ) == "Player" )
            out.emplace_back( child.address( ) );
    }
    return out;
}

} // namespace sdk
