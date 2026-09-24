#include <include/global.cuh>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sdk
{

namespace
{
    // Camera.CFrame / Focus layout: Matrix3 @ +0, Vector3 @ +0x24 (12 floats, no pad)
    struct packed_cframe_t
    {
        float data[12];
    };

    auto pack_cframe( const math::cframe_t& cf ) -> packed_cframe_t
    {
        packed_cframe_t out{};
        std::memcpy( out.data + 0, cf.rotation.data, sizeof( float ) * 9 );
        out.data[9]  = cf.position.x;
        out.data[10] = cf.position.y;
        out.data[11] = cf.position.z;
        return out;
    }

    auto unpack_cframe( const packed_cframe_t& raw ) -> math::cframe_t
    {
        math::cframe_t cf{};
        std::memcpy( cf.rotation.data, raw.data + 0, sizeof( float ) * 9 );
        cf.position = { raw.data[9], raw.data[10], raw.data[11] };
        return cf;
    }
}

auto u_camera::cframe( ) const -> math::cframe_t
{
    if ( !m_address )
        return { };
    return unpack_cframe( usermode->read<packed_cframe_t>( m_address + offsets::camera::cframe ) );
}

auto u_camera::set_cframe( const math::cframe_t& cf ) const -> void
{
    if ( !m_address )
        return;
    // single write — matches Camera.CFrame property layout from docs
    usermode->write<packed_cframe_t>( m_address + offsets::camera::cframe, pack_cframe( cf ) );
}

auto u_camera::position( ) const -> math::vector3_t
{
    if ( !m_address )
        return math::vector3_t::nan( );
    return usermode->read<math::vector3_t>( m_address + offsets::camera::position );
}

auto u_camera::set_position( const math::vector3_t& pos ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::vector3_t>( m_address + offsets::camera::position, pos );
}

auto u_camera::orientation( ) const -> math::matrix3_t
{
    if ( !m_address )
        return { };
    return usermode->read<math::matrix3_t>( m_address + offsets::camera::orientation );
}

auto u_camera::set_orientation( const math::matrix3_t& orientation ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<math::matrix3_t>( m_address + offsets::camera::orientation, orientation );
}

auto u_camera::focus( ) const -> math::cframe_t
{
    if ( !m_address )
        return { };
    return unpack_cframe( usermode->read<packed_cframe_t>( m_address + offsets::camera::focus ) );
}

auto u_camera::set_focus( const math::cframe_t& cf ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<packed_cframe_t>( m_address + offsets::camera::focus, pack_cframe( cf ) );
}

auto u_camera::set_focus_position( const math::vector3_t& pos ) const -> void
{
    // Focus prioritizes position for lighting/detail — identity rot is fine
    this->set_focus( math::cframe_new( pos ) );
}

auto u_camera::camera_type( ) const -> int
{
    if ( !m_address )
        return offsets::camera_type::custom;
    return usermode->read<int>( m_address + offsets::camera::camera_type );
}

auto u_camera::set_camera_type( int type ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<int>( m_address + offsets::camera::camera_type, type );
}

auto u_camera::camera_subject( ) const -> u_instance
{
    if ( !m_address )
        return u_instance( );
    return u_instance( usermode->read<uintptr_t>( m_address + offsets::camera::camera_subject ) );
}

auto u_camera::set_camera_subject( uintptr_t subject ) const -> void
{
    if ( !m_address )
        return;
    usermode->write<uintptr_t>( m_address + offsets::camera::camera_subject, subject );
}

auto u_camera::field_of_view( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::camera::field_of_view );
}

auto u_camera::set_field_of_view( float fov_deg ) const -> void
{
    if ( !m_address )
        return;
    // docs: clamped 1..120
    const float v = std::clamp( fov_deg, 1.f, 120.f );
    usermode->write<float>( m_address + offsets::camera::field_of_view, v );
}

auto u_camera::diagonal_field_of_view( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::camera::diagonal_fov );
}

auto u_camera::near_plane_z( ) const -> float
{
    if ( !m_address )
        return 0.f;
    return usermode->read<float>( m_address + offsets::camera::near_plane_z );
}

auto u_camera::viewport_size( ) const -> math::vector2_t
{
    if ( !m_address )
        return { };
    return usermode->read<math::vector2_t>( m_address + offsets::camera::viewport_size );
}

auto u_camera::look_at( const math::vector3_t& target, float lerp_alpha ) const -> void
{
    if ( !m_address || !target.is_valid( ) )
        return;

    // docs: Scriptable stops default camera scripts from fighting CFrame
    this->set_camera_type( offsets::camera_type::scriptable );

    const math::vector3_t from = this->position( );
    if ( !from.is_valid( ) )
        return;

    const float t = std::clamp( lerp_alpha, 0.f, 1.f );
    math::vector3_t look = target;

    if ( t < 0.999f )
    {
        // Roblox basis: 3rd column is -LookVector → LookVector = -col2
        const math::matrix3_t rot = this->orientation( );
        const math::vector3_t look_vec = {
            -rot.data[2],
            -rot.data[5],
            -rot.data[8],
        };

        float dist = math::vec3_length( target - from );
        if ( dist < 1e-3f )
            dist = 1.f;

        const math::vector3_t current_look = {
            from.x + look_vec.x * dist,
            from.y + look_vec.y * dist,
            from.z + look_vec.z * dist,
        };
        look = math::vec3_lerp( current_look, target, t );
    }

    // docs: CFrame.lookAt(from, lookAtPos) — keep camera position, reorient only
    math::cframe_t desired = math::cframe_look_at( from, look );
    desired.position = from;
    this->set_cframe( desired );

    // docs: when Scriptable, Focus does not auto-update — set it every frame
    this->set_focus_position( target );
}

auto u_camera::aim_cframe( const math::vector3_t& target, float lerp_alpha ) const -> void
{
    if ( !m_address || !target.is_valid( ) )
        return;

    this->set_camera_type( offsets::camera_type::scriptable );

    const math::vector3_t from = this->position( );
    if ( !from.is_valid( ) )
        return;

    const float t = std::clamp( lerp_alpha, 0.05f, 1.f );
    const math::cframe_t current = this->cframe( );

    math::vector3_t look = target;
    if ( t < 0.999f )
    {
        const math::matrix3_t& rot = current.rotation;
        const math::vector3_t look_vec = {
            -rot.data[2],
            -rot.data[5],
            -rot.data[8],
        };
        float dist = math::vec3_length( target - from );
        if ( dist < 1e-3f )
            dist = 1.f;
        const math::vector3_t current_look = {
            from.x + look_vec.x * dist,
            from.y + look_vec.y * dist,
            from.z + look_vec.z * dist,
        };
        look = math::vec3_lerp( current_look, target, t );
    }

    math::cframe_t desired = math::cframe_look_at( from, look );
    desired.position = from;

    const math::cframe_t focus_cf = math::cframe_new( target );

    for ( int i = 0; i < 3; ++i )
    {
        this->set_cframe( desired );
        this->set_orientation( desired.rotation );
        this->set_focus( focus_cf );
        usermode->write<math::vector3_t>( m_address + offsets::camera::focus_position, target );
    }
}

auto u_camera::world_to_viewport_point( const math::vector3_t& world, const math::matrix4_t& viewmatrix ) const -> math::vector3_t
{
    // docs: WorldToViewportPoint — no GUI inset; pixel coords from top-left of viewport
    const math::vector2_t vp = this->viewport_size( );
    const math::vector2_t dims = ( vp.x > 1.f && vp.y > 1.f ) ? vp : sdk::u_visualengine::dimensions( );
    const math::vector2_t screen = math::world_to_screen( world, viewmatrix, dims );
    if ( !std::isfinite( screen.x ) || !std::isfinite( screen.y ) )
        return { -1.f, -1.f, -1.f };

    const math::vector3_t cam = this->position( );
    const float depth = cam.is_valid( ) ? math::vec3_length( world - cam ) : 0.f;
    return { screen.x, screen.y, depth };
}

} // namespace sdk
