#pragma once

#include <src/engine/sdk/instance/instance.cuh>
#include <src/engine/math/math.cuh>

namespace sdk
{

class u_sky : public u_instance
{
public:
    using u_instance::u_instance;

    static auto resolve( ) -> u_sky;

    auto celestial_bodies_shown( ) const -> bool;
    auto set_celestial_bodies_shown( bool v ) const -> void;

    auto star_count( ) const -> int;
    auto set_star_count( int v ) const -> void;

    auto sun_angular_size( ) const -> float;
    auto set_sun_angular_size( float v ) const -> void;

    auto moon_angular_size( ) const -> float;
    auto set_moon_angular_size( float v ) const -> void;

    auto orientation( ) const -> math::vector3_t;
    auto set_orientation( const math::vector3_t& v ) const -> void;

    auto set_skybox_faces(
        const char* bk,
        const char* dn,
        const char* ft,
        const char* lf,
        const char* rt,
        const char* up ) const -> void;
};

}
