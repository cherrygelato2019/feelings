#pragma once

#include <src/engine/math/math.cuh>
#include <src/engine/sdk/instance/instance.cuh>

namespace sdk
{

class u_camera : public u_instance
{
public:
    using u_instance::u_instance;

    auto cframe( ) const -> math::cframe_t;
    auto set_cframe( const math::cframe_t& cf ) const -> void;

    auto position( ) const -> math::vector3_t;
    auto set_position( const math::vector3_t& pos ) const -> void;

    auto orientation( ) const -> math::matrix3_t;
    auto set_orientation( const math::matrix3_t& orientation ) const -> void;

    auto focus( ) const -> math::cframe_t;
    auto set_focus( const math::cframe_t& cf ) const -> void;
    auto set_focus_position( const math::vector3_t& pos ) const -> void;

    auto camera_type( ) const -> int;
    auto set_camera_type( int type ) const -> void;

    auto camera_subject( ) const -> u_instance;
    auto set_camera_subject( uintptr_t subject ) const -> void;

    auto field_of_view( ) const -> float;
    auto set_field_of_view( float fov_deg ) const -> void;
    auto diagonal_field_of_view( ) const -> float;
    auto near_plane_z( ) const -> float;

    auto viewport_size( ) const -> math::vector2_t;

    // Scriptable + CFrame.lookAt + Focus — camera aim path
    auto look_at( const math::vector3_t& target, float lerp_alpha = 1.f ) const -> void;
    auto aim_cframe( const math::vector3_t& target, float lerp_alpha = 1.f ) const -> void;
    auto world_to_viewport_point( const math::vector3_t& world, const math::matrix4_t& viewmatrix ) const -> math::vector3_t;
};

} // namespace sdk
