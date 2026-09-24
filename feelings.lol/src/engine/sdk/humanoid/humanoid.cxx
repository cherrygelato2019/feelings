#include <include/global.cuh>

#include <cmath>
#include <cstdint>

namespace sdk
{
namespace
{

auto looks_like_hp( float v, bool require_positive ) -> bool
{
    if ( !std::isfinite( v ) )
        return false;
    if ( require_positive )
        return v > 0.f && v <= 100000.f;
    return v >= 0.f && v <= 100000.f;
}

auto read_plain_or_xor( uintptr_t base, uintptr_t off, bool require_positive ) -> float
{
    if ( !base || !off )
        return -1.f;

    const float plain = usermode->read<float>( base + off );
    if ( looks_like_hp( plain, require_positive ) )
        return plain;

    const uint64_t one = usermode->read<uint64_t>( base + off );
    if ( one >= 0x100000ull && ( one >> 48 ) <= 0x7fffull )
    {
        const uint64_t two = usermode->read<uint64_t>( one );
        if ( two )
        {
            union
            {
                uint64_t hex;
                float f;
            } conv{};
            conv.hex = one ^ two;
            if ( looks_like_hp( conv.f, require_positive ) )
                return conv.f;
        }
    }
    return -1.f;
}

auto decrypt_humanoid_float( uintptr_t base, uintptr_t primary, const uintptr_t* alts, int alt_count, float fallback, bool require_positive ) -> float
{
    if ( !base )
        return fallback;

    float best = read_plain_or_xor( base, primary, require_positive );
    if ( best >= 0.f )
        return best;

    for ( int i = 0; i < alt_count; ++i )
    {
        if ( alts[i] == primary )
            continue;
        best = read_plain_or_xor( base, alts[i], require_positive );
        if ( best >= 0.f )
            return best;
    }
    return fallback;
}

}

float u_humanoid::health( ) const
{
    static constexpr uintptr_t alts[] = { 0x180, 0x190, 0x194 };
    return decrypt_humanoid_float( m_address, offsets::humanoid::health, alts, 3, 0.f, false );
}

float u_humanoid::max_health( ) const
{
    static constexpr uintptr_t alts[] = { 0x198, 0x1a8, 0x1b4 };
    return decrypt_humanoid_float( m_address, offsets::humanoid::max_health, alts, 3, 100.f, true );
}

float u_humanoid::walk_speed( ) const
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::humanoid::walk_speed );
}

float u_humanoid::jump_power( ) const
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::humanoid::jump_power );
}

float u_humanoid::hip_height( ) const
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::humanoid::hip_height );
}

auto u_humanoid::set_walk_speed( float v ) const -> void
{
    if ( !m_address )
        return;

    usermode->write<float>( m_address + offsets::humanoid::walk_speed, v );
    usermode->write<float>( m_address + offsets::humanoid::walk_speed_check, v );
}

auto u_humanoid::set_jump_power( float v ) const -> void
{
    if ( !m_address )
        return;

    usermode->write<uint8_t>( m_address + offsets::humanoid::use_jump_power, 1 );
    usermode->write<float>( m_address + offsets::humanoid::jump_power, v );
    usermode->write<float>( m_address + offsets::humanoid::jump_height, v / 50.f * 7.2f );
}

auto u_humanoid::set_use_jump_power( bool v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uint8_t>( m_address + offsets::humanoid::use_jump_power, v ? 1 : 0 );
}

auto u_humanoid::set_hip_height( float v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<float>( m_address + offsets::humanoid::hip_height, v );
}

auto u_humanoid::set_platform_stand( bool v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uint8_t>( m_address + offsets::humanoid::platform_stand, v ? 1 : 0 );
}

auto u_humanoid::platform_stand( ) const -> bool
{
    if ( !m_address )
        return false;
    return usermode->read<uint8_t>( m_address + offsets::humanoid::platform_stand ) != 0;
}

auto u_humanoid::set_sit( bool v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uint8_t>( m_address + offsets::humanoid::sit, v ? 1 : 0 );
}

auto u_humanoid::set_move_direction( const math::vector3_t& v ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::humanoid::move_direction, v );
}

auto u_humanoid::move_to( const math::vector3_t& world, double timeout_sec ) const -> void
{
    if ( !m_address || !world.is_valid( ) || !usermode->can_rpm( ) )
        return;

    const double timer = ( timeout_sec <= 0.0 ) ? 1e7 : timeout_sec;
    usermode->write<double>( m_address + offsets::humanoid::walk_timer, timer );
    usermode->write<math::vector3_t>( m_address + offsets::humanoid::move_to_point, world );
    usermode->write<math::vector3_t>( m_address + offsets::humanoid::target_point, world );
    usermode->write<uintptr_t>( m_address + offsets::humanoid::move_to_part, 0 );
    usermode->write<uint8_t>( m_address + offsets::humanoid::is_walking, 1 );
}

auto u_humanoid::set_state( int id ) const -> void
{
    if ( !m_address )
        return;
    const uintptr_t st = usermode->read<uintptr_t>( m_address + offsets::humanoid::state );
    if ( !st )
        return;
    usermode->write<std::int16_t>( st + offsets::humanoid::state_id, static_cast<std::int16_t>( id ) );
}

u_base_part u_humanoid::root_part( ) const
{
    if ( !m_address )
        return u_base_part( );
    uintptr_t part = usermode->read<uintptr_t>( m_address + offsets::humanoid::root_part );
    if ( !part )
        part = usermode->read<uintptr_t>( m_address + 0x5d8 );
    if ( !part )
        part = usermode->read<uintptr_t>( m_address + 0x478 );
    return u_base_part( part );
}

std::string u_humanoid::display_name( ) const
{
    if ( !m_address )
        return { };

    return usermode->read_string( m_address + offsets::humanoid::display_name );
}

int u_humanoid::rig_type( ) const
{
    if ( !m_address )
        return -1;
    return usermode->read<int>( m_address + offsets::humanoid::rig_type );
}

int u_humanoid::state( ) const
{
    if ( !m_address )
        return -1;
    const uintptr_t st = usermode->read<uintptr_t>( m_address + offsets::humanoid::state );
    if ( !st )
        return -1;
    return static_cast<int>( usermode->read<std::int16_t>( st + offsets::humanoid::state_id ) );
}

const char* u_humanoid::state_name( int id )
{
    static const char* names[] = {
        "FallingDown",
        "Ragdoll",
        "GettingUp",
        "Jumping",
        "Swimming",
        "Freefall",
        "Flying",
        "Landed",
        "Running",
        "Unknown",
        "RunningNoPhysics",
        "StrafingNoPhysics",
        "Climbing",
        "Seated",
        "PlatformStanding",
        "Dead",
        "Physics",
        "Unknown",
        "None",
    };
    if ( id >= 0 && id < 19 )
        return names[id];
    return "Unknown";
}

}
