#pragma once

#include <src/engine/math/math.cuh>
#include <src/engine/sdk/instance/instance.cuh>

namespace sdk
{

class u_base_part : public u_instance
{
public:
    using u_instance::u_instance;

    auto position( ) const -> math::vector3_t;
    auto set_position( const math::vector3_t& pos ) const -> void;
    auto orientation( ) const -> math::matrix3_t;
    auto cframe( ) const -> math::cframe_t;
    auto set_cframe( const math::cframe_t& cf ) const -> void;
    auto size( ) const -> math::vector3_t;

    auto velocity( ) const -> math::vector3_t;
    auto set_velocity( const math::vector3_t& v ) const -> void;
    auto assembly_linear_velocity( ) const -> math::vector3_t;
    auto assembly_angular_velocity( ) const -> math::vector3_t;
    auto set_angular_velocity( const math::vector3_t& v ) const -> void;

    auto anchored( ) const -> bool;
    auto can_collide( ) const -> bool;
    auto can_query( ) const -> bool;
    auto can_touch( ) const -> bool;

    auto primitive( ) const -> uintptr_t;
};

} 
