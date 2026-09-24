#include <include/global.cuh>

namespace sdk
{

auto u_model::primary_part( ) const -> u_base_part
{
    if ( !m_address )
        return u_base_part( );
    return u_base_part( usermode->read<uintptr_t>( m_address + offsets::model::primary_part ) );
}

auto u_model::scale( ) const -> float
{
    if ( !m_address )
        return 1.f;
    return usermode->read<float>( m_address + offsets::model::scale );
}

} // namespace sdk
