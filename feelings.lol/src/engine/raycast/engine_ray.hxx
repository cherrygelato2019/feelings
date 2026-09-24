#pragma once

#include <src/engine/cache/cache.cuh>
#include <src/engine/math/math.cuh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// WorldRoot:Raycast without BoundFunc hooks.
// Write origin@+0x08 / direction@+0x28 (dump Variant), read RaycastResult.
namespace engine_ray
{

struct query_t
{
    math::vector3_t origin{};
    math::vector3_t dir{};
    float max_t = 0.f;
};

struct hit_t
{
    bool ok = false;
    float t = 0.f;
    math::vector3_t pos{};
    uintptr_t part = 0;
    uintptr_t character = 0;
};

inline auto look( const math::cframe_t& cf ) -> math::vector3_t
{
    return { -cf.rotation.data[2], -cf.rotation.data[5], -cf.rotation.data[8] };
}

inline auto right( const math::cframe_t& cf ) -> math::vector3_t
{
    return { cf.rotation.data[0], cf.rotation.data[3], cf.rotation.data[6] };
}

inline auto up( const math::cframe_t& cf ) -> math::vector3_t
{
    return { cf.rotation.data[1], cf.rotation.data[4], cf.rotation.data[7] };
}

inline auto ray_aabb(
    const math::vector3_t& o,
    const math::vector3_t& d,
    const math::vector3_t& mn,
    const math::vector3_t& mx,
    float& out_t ) -> bool
{
    float tmin = 0.f;
    float tmax = 1e9f;
    const float o3[3] = { o.x, o.y, o.z };
    const float d3[3] = { d.x, d.y, d.z };
    const float mn3[3] = { mn.x, mn.y, mn.z };
    const float mx3[3] = { mx.x, mx.y, mx.z };

    for ( int i = 0; i < 3; ++i )
    {
        if ( std::fabs( d3[i] ) < 1e-8f )
        {
            if ( o3[i] < mn3[i] || o3[i] > mx3[i] )
                return false;
            continue;
        }
        float inv = 1.f / d3[i];
        float t1 = ( mn3[i] - o3[i] ) * inv;
        float t2 = ( mx3[i] - o3[i] ) * inv;
        if ( t1 > t2 )
        {
            const float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        tmin = ( t1 > tmin ) ? t1 : tmin;
        tmax = ( t2 < tmax ) ? t2 : tmax;
        if ( tmin > tmax )
            return false;
    }
    out_t = tmin;
    return tmax >= 0.f && tmin <= tmax;
}

inline auto ray_obb(
    const math::vector3_t& origin,
    const math::vector3_t& dir,
    const math::vector3_t& center,
    const math::matrix3_t& rotation,
    const math::vector3_t& half,
    float& out_t ) -> bool
{
    const math::vector3_t local_o = math::mat3_transpose_mul_vec3( rotation, origin - center );
    const math::vector3_t local_d = math::mat3_transpose_mul_vec3( rotation, dir );
    return ray_aabb( local_o, local_d, { -half.x, -half.y, -half.z }, { half.x, half.y, half.z }, out_t );
}

inline auto make_query( const math::vector3_t& from, const math::vector3_t& to ) -> query_t
{
    query_t q{};
    q.origin = from;
    const math::vector3_t delta = to - from;
    const float dist = delta.length( );
    if ( dist < 0.05f )
        return q;
    q.dir = { delta.x / dist, delta.y / dist, delta.z / dist };
    q.max_t = dist;
    return q;
}

inline auto viewport_ray(
    const math::cframe_t& cf,
    float fov_deg,
    float width,
    float height,
    float sx,
    float sy,
    float max_t = 2000.f ) -> query_t
{
    query_t q{};
    q.origin = cf.position;
    q.max_t = max_t;
    if ( width < 1.f || height < 1.f || !cf.position.is_valid( ) )
        return q;

    float fov = fov_deg;
    if ( fov > 0.01f && fov < 3.5f )
        fov *= 57.2957795f;
    if ( fov < 1.f )
        fov = 70.f;

    const float tan_half = std::tan( fov * 0.5f * 0.01745329252f );
    const float nx = ( sx / width ) * 2.f - 1.f;
    const float ny = 1.f - ( sy / height ) * 2.f;
    const float aspect = width / height;

    const math::vector3_t f = look( cf );
    const math::vector3_t r = right( cf );
    const math::vector3_t u = up( cf );
    q.dir = math::vec3_normalize( {
        f.x + r.x * ( nx * tan_half * aspect ) + u.x * ( ny * tan_half ),
        f.y + r.y * ( nx * tan_half * aspect ) + u.y * ( ny * tan_half ),
        f.z + r.z * ( nx * tan_half * aspect ) + u.z * ( ny * tan_half ),
    } );
    return q;
}

inline auto hit_part(
    const query_t& q,
    const math::vector3_t& center,
    const math::matrix3_t& rotation,
    const math::vector3_t& half,
    bool obb,
    const math::vector3_t& aabb_min,
    const math::vector3_t& aabb_max ) -> float
{
    if ( q.max_t <= 0.05f || !q.origin.is_valid( ) || !q.dir.is_valid( ) )
        return -1.f;
    float t = 0.f;
    const bool hit = obb
        ? ray_obb( q.origin, q.dir, center, rotation, half, t )
        : ray_aabb( q.origin, q.dir, aabb_min, aabb_max, t );
    if ( !hit || t < 0.08f || t >= q.max_t )
        return -1.f;
    return t;
}

inline auto cast_world( const query_t& q, const std::vector<cache::world_part>& parts, bool any = false ) -> hit_t
{
    hit_t best{};
    best.t = q.max_t;
    for ( const auto& part : parts )
    {
        if ( !part.query )
            continue;
        float aabb_t = 0.f;
        if ( !ray_aabb( q.origin, q.dir, part.min, part.max, aabb_t ) || aabb_t >= best.t )
            continue;
        const float t = hit_part( q, part.center, part.rotation, part.half, part.obb, part.min, part.max );
        if ( t < 0.f || t >= best.t )
            continue;
        best.ok = true;
        best.t = t;
        best.pos = {
            q.origin.x + q.dir.x * t,
            q.origin.y + q.dir.y * t,
            q.origin.z + q.dir.z * t,
        };
        best.part = part.part;
        if ( any )
            return best;
    }
    return best;
}

inline auto cast_players(
    const query_t& q,
    const std::vector<cache::player>& players,
    uintptr_t skip_character,
    int aim_part ) -> hit_t
{
    hit_t best{};
    best.t = q.max_t;

    auto consider = [&]( const cache::player& player, const cache::player::bone& bone )
    {
        if ( !bone.position.is_valid( ) )
            return;
        math::vector3_t half{
            ( std::max )( bone.size.x, 0.2f ) * 0.5f,
            ( std::max )( bone.size.y, 0.2f ) * 0.5f,
            ( std::max )( bone.size.z, 0.2f ) * 0.5f,
        };
        const float t = hit_part( q, bone.position, bone.rotation, half, true, {}, {} );
        if ( t < 0.f || t >= best.t )
            return;
        best.ok = true;
        best.t = t;
        best.pos = {
            q.origin.x + q.dir.x * t,
            q.origin.y + q.dir.y * t,
            q.origin.z + q.dir.z * t,
        };
        best.part = bone.part;
        best.character = player.character;
    };

    for ( const auto& player : players )
    {
        if ( !player.character || player.character == skip_character )
            continue;
        if ( player.max_health > 0.f && player.health <= 0.f )
            continue;

        if ( aim_part == 4 )
        {
            for ( const auto& bone : player.bones )
            {
                if ( bone.name.rfind( "Acc:", 0 ) == 0 || bone.name == "HumanoidRootPart" )
                    continue;
                consider( player, bone );
            }
            continue;
        }

        const char* names[4]{};
        int n = 1;
        names[0] = "Head";
        if ( aim_part == 1 )
        {
            names[0] = "UpperTorso";
            names[1] = "Torso";
            n = 2;
        }
        else if ( aim_part == 2 )
        {
            names[0] = "LowerTorso";
            names[1] = "Torso";
            n = 2;
        }
        else if ( aim_part == 3 )
        {
            names[0] = "HumanoidRootPart";
            names[1] = "UpperTorso";
            names[2] = "Torso";
            n = 3;
        }

        bool any = false;
        for ( const auto& bone : player.bones )
        {
            for ( int i = 0; i < n; ++i )
            {
                if ( bone.name == names[i] )
                {
                    consider( player, bone );
                    any = true;
                }
            }
        }
        if ( !any )
        {
            for ( const auto& bone : player.bones )
            {
                if ( bone.name == "Head" || bone.name == "Torso" || bone.name == "UpperTorso" )
                    consider( player, bone );
            }
        }
    }
    return best;
}

auto present( ) -> bool;
auto invalidate( ) -> void;
auto probe( const math::vector3_t& from, const math::vector3_t& to, const std::vector<cache::world_part>& parts ) -> hit_t;

inline auto visible( const math::vector3_t& from, const math::vector3_t& to, const std::vector<cache::world_part>& parts ) -> bool
{
    if ( !from.is_valid( ) || !to.is_valid( ) )
        return true;
    query_t q = make_query( from, to );
    if ( q.max_t <= 0.45f )
        return true;
    return !cast_world( q, parts, true ).ok;
}

} // namespace engine_ray
