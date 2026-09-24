#include <include/global.cuh>
#include <src/engine/raycast/engine_ray.hxx>

#include <cmath>
#include <cstring>

namespace engine_ray
{
namespace
{

uint64_t g_present_gen = 0;
int g_present = -1;

auto finite3( const math::vector3_t& v ) -> bool
{
    return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
}

auto intern_named( uintptr_t rva, const char* expect ) -> bool
{
    const uintptr_t img = usermode ? usermode->base( ) : 0;
    if ( !img || !expect || !usermode->can_rpm( ) )
        return false;

    char buf[32]{};
    const size_t n = std::strlen( expect );
    if ( n == 0 || n >= sizeof( buf ) )
        return false;
    if ( !usermode->read_buf( img + rva, buf, n + 1 ) )
        return false;
    return std::memcmp( buf, expect, n ) == 0 && buf[n] == 0;
}

} // namespace

auto present( ) -> bool
{
    if ( !usermode || !usermode->can_rpm( ) )
        return false;
    const uint64_t gen = usermode->generation( );
    if ( g_present >= 0 && g_present_gen == gen )
        return g_present != 0;
    g_present_gen = gen;
    g_present = ( intern_named( offsets::rva::intern_raycast, "Raycast" )
        && intern_named( offsets::rva::intern_worldroot, "WorldRoot" )
        && intern_named( offsets::rva::intern_raycast_params, "raycastParams" )
        && intern_named( offsets::rva::intern_raycast_result, "RaycastResult" ) ) ? 1 : 0;
    return g_present != 0;
}

auto invalidate( ) -> void
{
    g_present = -1;
}

auto probe( const math::vector3_t& from, const math::vector3_t& to, const std::vector<cache::world_part>& parts ) -> hit_t
{
    hit_t miss{};
    if ( !from.is_valid( ) || !to.is_valid( ) || !usermode || !usermode->can_rpm( ) )
        return miss;

    if ( !present( ) )
        return miss;

    const math::vector3_t delta = { to.x - from.x, to.y - from.y, to.z - from.z };
    if ( !finite3( from ) || !finite3( delta ) )
        return miss;
    const float span = delta.length( );
    if ( span < 0.05f )
        return miss;

    query_t q{};
    q.origin = from;
    q.dir = { delta.x / span, delta.y / span, delta.z / span };
    q.max_t = span;
    if ( !q.dir.is_valid( ) )
        return miss;

    // Local OBB only. Do not allocate or write a remote slab in Roblox.
    return cast_world( q, parts, true );
}

} // namespace engine_ray
