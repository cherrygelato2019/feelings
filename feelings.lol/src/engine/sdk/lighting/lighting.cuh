#pragma once

#include <src/engine/sdk/instance/instance.cuh>
#include <src/engine/math/math.cuh>

namespace sdk
{

class u_lighting : public u_instance
{
public:
    using u_instance::u_instance;

    static auto resolve( ) -> u_lighting;

    auto brightness( ) const -> float;
    auto set_brightness( float v ) const -> void;

    auto exposure_compensation( ) const -> float;
    auto set_exposure_compensation( float v ) const -> void;

    auto clock_time( ) const -> float;
    auto set_clock_time( float hours ) const -> void;

    auto geographic_latitude( ) const -> float;
    auto set_geographic_latitude( float v ) const -> void;

    auto environment_diffuse_scale( ) const -> float;
    auto set_environment_diffuse_scale( float v ) const -> void;

    auto environment_specular_scale( ) const -> float;
    auto set_environment_specular_scale( float v ) const -> void;

    auto global_shadows( ) const -> bool;
    auto set_global_shadows( bool v ) const -> void;

    auto shadow_softness( ) const -> float;
    auto set_shadow_softness( float v ) const -> void;

    auto prioritize_lighting_quality( ) const -> bool;
    auto set_prioritize_lighting_quality( bool v ) const -> void;

    auto ambient( ) const -> math::vector3_t;
    auto set_ambient( const math::vector3_t& v ) const -> void;

    auto outdoor_ambient( ) const -> math::vector3_t;
    auto set_outdoor_ambient( const math::vector3_t& v ) const -> void;

    auto color_shift_top( ) const -> math::vector3_t;
    auto set_color_shift_top( const math::vector3_t& v ) const -> void;

    auto color_shift_bottom( ) const -> math::vector3_t;
    auto set_color_shift_bottom( const math::vector3_t& v ) const -> void;

    auto shadow_color( ) const -> math::vector3_t;
    auto set_shadow_color( const math::vector3_t& v ) const -> void;

    auto sky_ambient( ) const -> math::vector3_t;
    auto set_sky_ambient( const math::vector3_t& v ) const -> void;

    auto light_color( ) const -> math::vector3_t;
    auto set_light_color( const math::vector3_t& v ) const -> void;

    auto fog_color( ) const -> math::vector3_t;
    auto set_fog_color( const math::vector3_t& v ) const -> void;

    auto fog_start( ) const -> float;
    auto set_fog_start( float v ) const -> void;

    auto fog_end( ) const -> float;
    auto set_fog_end( float v ) const -> void;

    auto atmosphere( ) const -> uintptr_t;
    auto set_atmosphere( uintptr_t v ) const -> void;

    auto sky( ) const -> uintptr_t;
};

}
