#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace math
{

struct vector2_t
{
    float x, y;

    vector2_t( ) : x( 0.f ), y( 0.f ) {}
    vector2_t( float _x, float _y ) : x( _x ), y( _y ) {}
};

struct vector3_t
{
    float x, y, z;

    vector3_t( ) : x( 0.f ), y( 0.f ), z( 0.f ) {}
    vector3_t( float _x, float _y, float _z ) : x( _x ), y( _y ), z( _z ) {}

    static auto nan( ) -> vector3_t
    {
        constexpr float n = std::numeric_limits<float>::quiet_NaN( );
        return { n, n, n };
    }

    auto is_valid( ) const -> bool
    {
        return !std::isnan( x ) && !std::isnan( y ) && !std::isnan( z );
    }

    auto length( ) const -> float
    {
        return std::sqrt( x * x + y * y + z * z );
    }

    vector3_t operator-( const vector3_t& o ) const { return { x - o.x, y - o.y, z - o.z }; }
    vector3_t operator+( const vector3_t& o ) const { return { x + o.x, y + o.y, z + o.z }; }
    vector3_t operator*( float s ) const { return { x * s, y * s, z * s }; }
};

enum class aim_style_t : int
{
    linear = 0,
    spring,
    exponential,
    cubed,
    bezier,
    humanized,
};

inline auto alpha_linear( float t ) -> float
{
    return t;
}

inline auto alpha_exp( float t ) -> float
{
    return 1.f - std::exp( -t * 4.f );
}

inline auto alpha_cubed( float t ) -> float
{
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

inline auto bezier_component( float t, float a, float b, float c, float d ) -> float
{
    const float u = 1.f - t;
    return u * u * u * a + 3.f * u * u * t * b + 3.f * u * t * t * c + t * t * t * d;
}

inline auto alpha_bezier( float t ) -> float
{
    float lo = 0.f, hi = 1.f, mid = t;
    for ( int i = 0; i < 16; ++i )
    {
        mid = ( lo + hi ) * 0.5f;
        const float x = bezier_component( mid, 0.f, 0.42f, 0.58f, 1.f );
        if ( x < t )
            lo = mid;
        else
            hi = mid;
    }
    return std::clamp( bezier_component( mid, 0.f, 0.f, 1.f, 1.f ), 0.f, 1.f );
}

inline auto alpha_humanized( float t ) -> float
{
    float lo = 0.f, hi = 1.f, mid = t;
    for ( int i = 0; i < 12; ++i )
    {
        mid = ( lo + hi ) * 0.5f;
        const float x = bezier_component( mid, 0.f, 0.25f, 0.25f, 1.f );
        if ( x < t )
            lo = mid;
        else
            hi = mid;
    }
    return std::clamp( bezier_component( mid, 0.f, 0.1f, 1.f, 1.f ), 0.f, 1.f );
}

inline auto alpha_from_style( aim_style_t style, float t ) -> float
{
    t = std::clamp( t, 0.01f, 1.f );
    switch ( style )
    {
    case aim_style_t::exponential: return alpha_exp( t );
    case aim_style_t::cubed:       return alpha_cubed( t );
    case aim_style_t::bezier:      return alpha_bezier( t );
    case aim_style_t::humanized:   return alpha_humanized( t );
    case aim_style_t::spring:      return alpha_exp( t );
    case aim_style_t::linear:
    default:                       return alpha_linear( t );
    }
}

inline auto spring_update( float& pos, float& vel, float target, float stiffness, float damping, float dt ) -> void
{
    if ( dt <= 0.f )
        dt = 0.016f;
    const float force = -stiffness * ( pos - target ) - damping * vel;
    vel += force * dt;
    pos += vel * dt;
}

inline auto lead_target( const vector3_t& shooter, const vector3_t& target, const vector3_t& target_vel,
    float prediction_xz, float prediction_y, float bullet_drop ) -> vector3_t
{
    const vector3_t to_target = target - shooter;
    const float dist = to_target.length( );
    if ( dist < 1e-3f )
        return target;

    const float t = dist / 2000.f;
    vector3_t lead = {
        target.x + target_vel.x * t * prediction_xz,
        target.y + target_vel.y * t * prediction_y,
        target.z + target_vel.z * t * prediction_xz,
    };
    if ( bullet_drop > 0.f )
        lead.y += 0.5f * bullet_drop * t * t;
    return lead;
}

inline auto smart_fov_radius( float base_fov, float distance, float max_distance ) -> float
{
    if ( max_distance <= 0.f || distance >= max_distance )
        return base_fov * 0.4f;
    const float t = std::clamp( distance / max_distance, 0.f, 1.f );
    return base_fov * ( 1.f - t * 0.6f );
}

inline auto scatter_point( const vector3_t& center, float scale, std::uint32_t seed ) -> vector3_t
{
    if ( scale <= 0.f )
        return center;

    auto hash = []( std::uint32_t n ) -> float
    {
        n = ( n ^ 61 ) ^ ( n >> 16 );
        n = n + ( n << 3 );
        n = n ^ ( n >> 4 );
        n = n * 0x27d4eb2d;
        n = n ^ ( n >> 15 );
        return static_cast<float>( n ) / 4294967295.f;
    };

    const float r = ( hash( seed ) - 0.5f ) * 2.f * scale;
    return {
        center.x + r * ( hash( seed * 7 + 1 ) - 0.5f ),
        center.y + r * ( hash( seed * 13 + 3 ) - 0.5f ),
        center.z + r * ( hash( seed * 29 + 5 ) - 0.5f ),
    };
}

struct vector4_t
{
    float x, y, z, w;
};

struct matrix4_t
{
    float data[16];
};

struct matrix3_t
{
    float data[9];
};

struct cframe_t
{
    matrix3_t rotation;
    vector3_t position;
};

inline auto world_to_screen( const vector3_t& world, const matrix4_t& vm, const vector2_t& screen ) -> vector2_t
{
    // NaN = behind / invalid. Do NOT use (-1,-1): left/top off-screen is legitimately negative.
    if ( !world.is_valid( ) )
        return { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };

    // row-vector * row-major viewprojection (Roblox VisualEngine layout)
    const float clip_x = world.x * vm.data[0]  + world.y * vm.data[1]  + world.z * vm.data[2]  + vm.data[3];
    const float clip_y = world.x * vm.data[4]  + world.y * vm.data[5]  + world.z * vm.data[6]  + vm.data[7];
    const float clip_w = world.x * vm.data[12] + world.y * vm.data[13] + world.z * vm.data[14] + vm.data[15];

    if ( clip_w < 0.15f )
        return { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };

    const float inv = 1.f / clip_w;
    const float ndc_x = clip_x * inv;
    const float ndc_y = clip_y * inv;
    if ( ndc_x < -1.6f || ndc_x > 1.6f || ndc_y < -1.6f || ndc_y > 1.6f )
        return { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };

    // map NDC into draw-space pixels (client / synced overlay)
    return
    {
        ( screen.x * 0.5f ) * ( ndc_x + 1.f ),
        ( screen.y * 0.5f ) * ( 1.f - ndc_y )
    };
}

inline auto mat3_mul_vec3( const matrix3_t& m, const vector3_t& v ) -> vector3_t
{
    return
    {
        m.data[0] * v.x + m.data[1] * v.y + m.data[2] * v.z,
        m.data[3] * v.x + m.data[4] * v.y + m.data[5] * v.z,
        m.data[6] * v.x + m.data[7] * v.y + m.data[8] * v.z,
    };
}

// R^T * v — bring world vectors into part-local space for OBB tests
inline auto mat3_transpose_mul_vec3( const matrix3_t& m, const vector3_t& v ) -> vector3_t
{
    return
    {
        m.data[0] * v.x + m.data[3] * v.y + m.data[6] * v.z,
        m.data[1] * v.x + m.data[4] * v.y + m.data[7] * v.z,
        m.data[2] * v.x + m.data[5] * v.y + m.data[8] * v.z,
    };
}

inline auto vec3_length( const vector3_t& v ) -> float
{
    return std::sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
}

inline auto vec3_normalize( const vector3_t& v ) -> vector3_t
{
    const float len = vec3_length( v );
    if ( len < 1e-6f )
        return { 0.f, 0.f, -1.f };
    return { v.x / len, v.y / len, v.z / len };
}

inline auto vec3_cross( const vector3_t& a, const vector3_t& b ) -> vector3_t
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline auto vec3_dot( const vector3_t& a, const vector3_t& b ) -> float
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline auto vec3_lerp( const vector3_t& a, const vector3_t& b, float t ) -> vector3_t
{
    return {
        a.x + ( b.x - a.x ) * t,
        a.y + ( b.y - a.y ) * t,
        a.z + ( b.z - a.z ) * t,
    };
}

// CFrame.lookAt(from, to) — same basis Roblox uses for Camera.CFrame
inline auto cframe_look_at( const vector3_t& from, const vector3_t& to ) -> cframe_t
{
    vector3_t forward = vec3_normalize( to - from );
    vector3_t world_up = { 0.f, 1.f, 0.f };
    if ( std::fabs( vec3_dot( forward, world_up ) ) > 0.999f )
        world_up = { 0.f, 0.f, 1.f };

    const vector3_t right = vec3_normalize( vec3_cross( world_up, forward ) );
    const vector3_t up = vec3_cross( forward, right );

    cframe_t cf{};
    cf.position = from;
    cf.rotation.data[0] = right.x;  cf.rotation.data[1] = up.x;  cf.rotation.data[2] = -forward.x;
    cf.rotation.data[3] = right.y;  cf.rotation.data[4] = up.y;  cf.rotation.data[5] = -forward.y;
    cf.rotation.data[6] = right.z;  cf.rotation.data[7] = up.z;  cf.rotation.data[8] = -forward.z;
    return cf;
}

inline auto cframe_new( const vector3_t& pos ) -> cframe_t
{
    cframe_t cf{};
    cf.position = pos;
    cf.rotation.data[0] = 1.f; cf.rotation.data[4] = 1.f; cf.rotation.data[8] = 1.f;
    return cf;
}

inline auto cframe_lerp( const cframe_t& a, const cframe_t& b, float t ) -> cframe_t
{
    cframe_t out{};
    out.position = vec3_lerp( a.position, b.position, t );
    for ( int i = 0; i < 9; ++i )
        out.rotation.data[i] = a.rotation.data[i] + ( b.rotation.data[i] - a.rotation.data[i] ) * t;
    return out;
}

} // namespace math
