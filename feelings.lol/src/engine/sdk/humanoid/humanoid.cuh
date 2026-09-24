#pragma once

#include <src/engine/sdk/basepart/basepart.cuh>
#include <src/engine/sdk/instance/instance.cuh>

namespace sdk
{

class u_humanoid : public u_instance
{
public:
    using u_instance::u_instance;

    auto health( ) const -> float;
    auto max_health( ) const -> float;
    auto walk_speed( ) const -> float;
    auto jump_power( ) const -> float;
    auto hip_height( ) const -> float;

    auto root_part( ) const -> u_base_part;

    auto display_name( ) const -> std::string;
    auto rig_type( ) const -> int;

    auto state( ) const -> int;
    static auto state_name( int id ) -> const char*;

    auto set_walk_speed( float v ) const -> void;
    auto set_jump_power( float v ) const -> void;
    auto set_hip_height( float v ) const -> void;
    auto set_state( int id ) const -> void;
    auto set_use_jump_power( bool v ) const -> void;
    auto set_platform_stand( bool v ) const -> void;
    auto platform_stand( ) const -> bool;
    auto set_sit( bool v ) const -> void;
    auto set_move_direction( const math::vector3_t& v ) const -> void;
    auto move_to( const math::vector3_t& world, double timeout_sec = 1e7 ) const -> void;
};

} 
