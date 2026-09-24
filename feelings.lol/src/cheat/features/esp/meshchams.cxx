#include <include/global.cuh>

#include <src/cheat/features/esp/meshchams.cuh>
#include <src/engine/cache/draw.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace meshchams
{
namespace
{

struct mesh_t
{
    std::vector<math::vector3_t> positions;
    std::vector<std::uint32_t> indices; 
    math::vector3_t aabb_min{};
    math::vector3_t aabb_max{};
    bool has_aabb = false;
};

std::mutex g_mutex;
std::unordered_map<std::string, std::shared_ptr<mesh_t>> g_meshes;
std::uintptr_t g_provider = 0;
auto g_last_scan = std::chrono::steady_clock::time_point{};

auto read_content_string( std::uintptr_t addr ) -> std::string
{
    if ( !addr || !usermode->is_valid( addr ) )
        return {};

    auto s = usermode->read_string( addr );
    if ( !s.empty( ) )
        return s;

    s = usermode->read_string( addr + 0x10 );
    if ( !s.empty( ) )
        return s;

    return {};
}

auto normalize_id( std::string id ) -> std::string
{
    if ( id.empty( ) )
        return {};

    const auto dig = id.find_first_of( "0123456789" );
    if ( dig != std::string::npos )
    {
        std::size_t end = dig;
        while ( end < id.size( ) && id[end] >= '0' && id[end] <= '9' )
            ++end;
        if ( end > dig )
            return id.substr( dig, end - dig );
    }

    for ( char& c : id )
    {
        if ( c >= 'A' && c <= 'Z' )
            c = ( char )( c - 'A' + 'a' );
    }
    return id;
}

auto find_provider( ) -> std::uintptr_t
{
    if ( g_provider && usermode->is_valid( g_provider ) )
    {
        const auto cn = sdk::u_instance( g_provider ).class_name( );
        if ( cn == "MeshContentProvider" )
            return g_provider;
        g_provider = 0;
    }

    const std::uintptr_t dm = g->engine.datamodel.load( );
    if ( !dm )
        return 0;

    const auto svc = sdk::u_instance( dm ).find_first_child_of_class( "MeshContentProvider" );
    g_provider = svc.address( );
    return g_provider;
}

auto load_file_mesh( std::uintptr_t data ) -> mesh_t
{
    mesh_t out;
    if ( !data || !usermode->is_valid( data ) )
        return out;

    const auto v0 = usermode->read<std::uintptr_t>( data + offsets::file_mesh_data::vertex_begin );
    const auto v1 = usermode->read<std::uintptr_t>( data + offsets::file_mesh_data::vertex_end );
    const auto f0 = usermode->read<std::uintptr_t>( data + offsets::file_mesh_data::face_begin );
    const auto f1 = usermode->read<std::uintptr_t>( data + offsets::file_mesh_data::face_end );

    if ( !v0 || !v1 || v1 <= v0 || !f0 || !f1 || f1 <= f0 )
        return out;

    const std::size_t vcount = ( v1 - v0 ) / offsets::file_mesh_data::vertex_stride;
    const std::size_t fcount = ( f1 - f0 ) / offsets::file_mesh_data::face_stride;
    if ( vcount < 3 || fcount < 1 || vcount > 200000 || fcount > 400000 )
        return out;

    constexpr std::size_t k_max_v = 2048;
    constexpr std::size_t k_max_f = 3072;
    const std::size_t use_v = ( std::min )( vcount, k_max_v );
    const std::size_t use_f = ( std::min )( fcount, k_max_f );

    out.positions.resize( use_v );
    std::vector<std::uint8_t> vbuf( use_v * offsets::file_mesh_data::vertex_stride );
    if ( !usermode->read_buf( v0, vbuf.data( ), vbuf.size( ) ) )
        return {};

    out.aabb_min = { 1e9f, 1e9f, 1e9f };
    out.aabb_max = { -1e9f, -1e9f, -1e9f };
    for ( std::size_t i = 0; i < use_v; ++i )
    {
        float p[3];
        std::memcpy( p, vbuf.data( ) + i * offsets::file_mesh_data::vertex_stride, sizeof( p ) );
        out.positions[i] = { p[0], p[1], p[2] };
        out.aabb_min.x = ( std::min )( out.aabb_min.x, p[0] );
        out.aabb_min.y = ( std::min )( out.aabb_min.y, p[1] );
        out.aabb_min.z = ( std::min )( out.aabb_min.z, p[2] );
        out.aabb_max.x = ( std::max )( out.aabb_max.x, p[0] );
        out.aabb_max.y = ( std::max )( out.aabb_max.y, p[1] );
        out.aabb_max.z = ( std::max )( out.aabb_max.z, p[2] );
    }
    out.has_aabb = true;

    const auto amin = usermode->read<math::vector3_t>( data + offsets::file_mesh_data::aabb_min );
    const auto amax = usermode->read<math::vector3_t>( data + offsets::file_mesh_data::aabb_max );
    if ( amin.is_valid( ) && amax.is_valid( )
        && amax.x > amin.x && amax.y > amin.y && amax.z > amin.z )
    {
        out.aabb_min = amin;
        out.aabb_max = amax;
    }

    out.indices.resize( use_f * 3 );
    std::vector<std::uint8_t> fbuf( use_f * offsets::file_mesh_data::face_stride );
    if ( !usermode->read_buf( f0, fbuf.data( ), fbuf.size( ) ) )
        return {};

    for ( std::size_t i = 0; i < use_f; ++i )
    {
        std::uint32_t idx[3];
        std::memcpy( idx, fbuf.data( ) + i * offsets::file_mesh_data::face_stride, sizeof( idx ) );
        if ( idx[0] >= use_v || idx[1] >= use_v || idx[2] >= use_v )
        {
            out.indices[i * 3 + 0] = 0;
            out.indices[i * 3 + 1] = 0;
            out.indices[i * 3 + 2] = 0;
            continue;
        }
        out.indices[i * 3 + 0] = idx[0];
        out.indices[i * 3 + 1] = idx[1];
        out.indices[i * 3 + 2] = idx[2];
    }

    return out;
}

auto walk_lru( std::uintptr_t sentinel, std::unordered_map<std::string, std::shared_ptr<mesh_t>>& next ) -> void
{
    std::uintptr_t node = usermode->read<std::uintptr_t>( sentinel + offsets::lru_node::next );
    for ( int guard = 0; guard < 400 && node && node != sentinel; ++guard )
    {
        if ( !usermode->is_valid( node ) )
            break;

        auto raw_id = usermode->read_string( node + offsets::lru_node::content_id );
        if ( raw_id.empty( ) )
            raw_id = usermode->read_string( node + offsets::lru_node::content_id + 0x10 );
        const auto key = normalize_id( raw_id );

        if ( !key.empty( ) )
        {
            if ( next.find( key ) != next.end( ) )
            {
                const auto n = usermode->read<std::uintptr_t>( node + offsets::lru_node::next );
                if ( !n || n == node )
                    break;
                node = n;
                continue;
            }

            {
                std::lock_guard<std::mutex> lock( g_mutex );
                auto it = g_meshes.find( key );
                if ( it != g_meshes.end( ) )
                {
                    next.emplace( key, it->second );
                    const auto n = usermode->read<std::uintptr_t>( node + offsets::lru_node::next );
                    if ( !n || n == node )
                        break;
                    node = n;
                    continue;
                }
            }

            uintptr_t mesh_ptr = usermode->read<std::uintptr_t>( node + offsets::lru_node::mesh_data );
            if ( !mesh_ptr || !usermode->is_valid( mesh_ptr ) )
                mesh_ptr = usermode->read<std::uintptr_t>( node + offsets::lru_node::mesh_data_alt );

            if ( mesh_ptr && usermode->is_valid( mesh_ptr ) )
            {
                auto mesh = std::make_shared<mesh_t>( load_file_mesh( mesh_ptr ) );
                if ( mesh && !mesh->positions.empty( ) && !mesh->indices.empty( ) )
                    next.emplace( key, std::move( mesh ) );
            }
        }

        const auto n = usermode->read<std::uintptr_t>( node + offsets::lru_node::next );
        if ( !n || n == node )
            break;
        node = n;
    }
}

auto scan_lru( ) -> void
{
    const auto provider = find_provider( );
    if ( !provider )
        return;

    std::unordered_map<std::string, std::shared_ptr<mesh_t>> next;
    next.reserve( 128 );

    static constexpr uintptr_t k_cache[] = {
        offsets::mesh_content_provider::cache_object,
        offsets::mesh_content_provider::cache_object_alt,
    };

    for ( uintptr_t off : k_cache )
    {
        const auto cache_obj = usermode->read<std::uintptr_t>( provider + off );
        if ( !cache_obj || !usermode->is_valid( cache_obj ) )
            continue;

        const auto lru1 = usermode->read<std::uintptr_t>( cache_obj + offsets::mesh_content_provider::lru_cache1 );
        if ( !lru1 || !usermode->is_valid( lru1 ) )
            continue;

        const auto sentinel = usermode->read<std::uintptr_t>( lru1 + offsets::mesh_content_provider::sentinel );
        if ( !sentinel || !usermode->is_valid( sentinel ) )
            continue;

        walk_lru( sentinel, next );
        if ( !next.empty( ) )
            break;
    }

    if ( !next.empty( ) )
    {
        std::lock_guard<std::mutex> lock( g_mutex );
        g_meshes.swap( next );
    }
}

auto lookup_mesh( const std::string& mesh_id ) -> std::shared_ptr<mesh_t>
{
    const auto key = normalize_id( mesh_id );
    if ( key.empty( ) )
        return {};

    std::lock_guard<std::mutex> lock( g_mutex );
    auto it = g_meshes.find( key );
    if ( it != g_meshes.end( ) )
        return it->second;
    return {};
}

auto project(
    const math::vector3_t& world,
    const math::vector2_t& ve_dims,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    float sx,
    float sy,
    ImVec2& out ) -> bool
{
    math::vector2_t s = math::world_to_screen( world, viewmatrix, ve_dims );
    if ( !std::isfinite( s.x ) || !std::isfinite( s.y ) )
        return false;
    out = ImVec2( s.x * sx + offset.x, s.y * sy + offset.y );
    return true;
}

auto pack_col( const float c[4] ) -> ImU32
{
    return IM_COL32(
        ( int )( c[0] * 255.f ),
        ( int )( c[1] * 255.f ),
        ( int )( c[2] * 255.f ),
        ( int )( c[3] * 255.f ) );
}

auto hsv_col( float h, float s, float v, float a ) -> ImU32
{
    float r = 0.f, g = 0.f, b = 0.f;
    ImGui::ColorConvertHSVtoRGB( h - std::floor( h ), s, v, r, g, b );
    return IM_COL32(
        ( int )( r * 255.f ),
        ( int )( g * 255.f ),
        ( int )( b * 255.f ),
        ( int )( a * 255.f ) );
}

auto hsv_rgb( float h, float s, float v ) -> math::vector3_t
{
    float r = 0.f, g = 0.f, b = 0.f;
    ImGui::ColorConvertHSVtoRGB( h - std::floor( h ), s, v, r, g, b );
    return { r, g, b };
}

auto facing( const ImVec2& a, const ImVec2& b, const ImVec2& c ) -> bool
{
    return ( b.x - a.x ) * ( c.y - a.y ) - ( b.y - a.y ) * ( c.x - a.x ) > 0.f;
}

auto edge_key( std::uint32_t a, std::uint32_t b ) -> std::uint64_t
{
    if ( a > b )
        std::swap( a, b );
    return ( ( std::uint64_t )a << 32 ) | b;
}

auto is_cham_bone( const std::string& name ) -> bool
{
    if ( name.empty( ) )
        return false;
    if ( name == "HumanoidRootPart" || name == "Handle" )
        return false;
    if ( name.rfind( "Acc:", 0 ) == 0 )
        return false;

    return name == "Head" || name == "head"
        || name == "UpperTorso" || name == "LowerTorso" || name == "Torso" || name == "torso"
        || name == "LeftUpperArm" || name == "LeftLowerArm" || name == "LeftHand"
        || name == "RightUpperArm" || name == "RightLowerArm" || name == "RightHand"
        || name == "LeftUpperLeg" || name == "LeftLowerLeg" || name == "LeftFoot"
        || name == "RightUpperLeg" || name == "RightLowerLeg" || name == "RightFoot"
        || name == "Left Arm" || name == "Right Arm" || name == "Left Leg" || name == "Right Leg"
        || name == "LeftArm" || name == "RightArm" || name == "LeftLeg" || name == "RightLeg";
}

auto draw_obb(
    ImDrawList* canvas,
    const cache::player::bone& bone,
    const math::vector2_t& ve_dims,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    float sx,
    float sy,
    ImU32 fill_col,
    ImU32 outline_col,
    float thickness,
    bool do_fill,
    bool do_outline ) -> void
{
    const cache::bone_pose_t pose = cache::read_bone_pose( bone );
    if ( !pose.valid )
        return;

    static const float k_local[8][3] = {
        { -1.f, -1.f, -1.f }, {  1.f, -1.f, -1.f },
        { -1.f,  1.f, -1.f }, {  1.f,  1.f, -1.f },
        { -1.f, -1.f,  1.f }, {  1.f, -1.f,  1.f },
        { -1.f,  1.f,  1.f }, {  1.f,  1.f,  1.f },
    };

    ImVec2 pts[8];
    int n = 0;
    for ( int i = 0; i < 8; ++i )
    {
        const math::vector3_t local{
            k_local[i][0] * pose.size.x * 0.5f,
            k_local[i][1] * pose.size.y * 0.5f,
            k_local[i][2] * pose.size.z * 0.5f
        };
        const math::vector3_t world = math::mat3_mul_vec3( pose.rotation, local ) + pose.position;
        ImVec2 s;
        if ( !project( world, ve_dims, viewmatrix, offset, sx, sy, s ) )
            continue;
        pts[n++] = s;
    }
    if ( n < 3 )
        return;

    ImVec2 c{ 0.f, 0.f };
    for ( int i = 0; i < n; ++i )
    {
        c.x += pts[i].x;
        c.y += pts[i].y;
    }
    c.x /= ( float )n;
    c.y /= ( float )n;

    int order[8];
    for ( int i = 0; i < n; ++i )
        order[i] = i;
    for ( int i = 0; i < n; ++i )
    {
        for ( int j = i + 1; j < n; ++j )
        {
            const float ai = std::atan2( pts[order[i]].y - c.y, pts[order[i]].x - c.x );
            const float aj = std::atan2( pts[order[j]].y - c.y, pts[order[j]].x - c.x );
            if ( aj < ai )
                std::swap( order[i], order[j] );
        }
    }

    ImVec2 hull[8];
    for ( int i = 0; i < n; ++i )
        hull[i] = pts[order[i]];

    if ( do_fill )
        canvas->AddConvexPolyFilled( hull, n, fill_col );
    if ( do_outline )
        canvas->AddPolyline( hull, n, outline_col, ImDrawFlags_Closed, ( std::max )( 1.f, thickness ) );
}

struct hl_pin_t
{
    uintptr_t addr = 0;
    std::chrono::steady_clock::time_point at{};
};

std::unordered_map<uintptr_t, hl_pin_t> g_hl;

auto find_highlight( uintptr_t character ) -> sdk::u_instance
{
    if ( !character )
        return {};

    const auto now = std::chrono::steady_clock::now( );
    auto it = g_hl.find( character );
    if ( it != g_hl.end( ) && now - it->second.at < std::chrono::milliseconds( 2000 ) )
        return it->second.addr ? sdk::u_instance( it->second.addr ) : sdk::u_instance{};

    sdk::u_instance found{};
    sdk::u_instance root( character );
    for ( const auto& child : root.get_children( ) )
    {
        if ( child.class_name( ) == "Highlight" )
        {
            found = child;
            break;
        }
    }

    g_hl[character] = { found.address( ), now };
    if ( g_hl.size( ) > 64 )
        g_hl.clear( );
    return found;
}

} 

auto tick_cache( ) -> void
{
    if ( !config.esp.chams || config.esp.chams_kind == 1 || config.esp.chams_kind == 2 )
        return;

    const auto now = std::chrono::steady_clock::now( );
    if ( g_last_scan.time_since_epoch( ).count( ) != 0
        && now - g_last_scan < std::chrono::milliseconds( 4000 ) )
        return;
    g_last_scan = now;
    scan_lru( );
}

auto draw(
    ImDrawList* canvas,
    const cache::player& player,
    const math::vector2_t& ve_dims,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    float sx,
    float sy,
    ImU32 fill_col,
    ImU32 outline_col,
    float thickness ) -> void
{
    if ( !canvas )
        return;

    const ImDrawListFlags aa_prev = cache::begin_aa( canvas, config.esp.aa_chams );
    struct aa_guard_t { ImDrawList* c; ImDrawListFlags p; ~aa_guard_t( ) { cache::end_aa( c, p ); } } _aa{ canvas, aa_prev };
    tick_cache( );

    const float t = ( std::max )( 1.f, thickness );
    const int kind = config.esp.chams_kind;
    const int style = config.esp.chams_style;
    const bool xqz = style == 1;
    const bool rainbow = style == 2;
    const bool wire_look = style == 3 || kind == 3;
    const bool glass = style == 0 || ( !wire_look && !xqz && !rainbow );
    const ImU32 hidden_col = pack_col( config.esp.chams_hidden );
    const ImU32 visible_col = pack_col( config.esp.chams_visible );
    const float rainbow_h = ( float )std::fmod( ImGui::GetTime( ) * ( double )config.esp.chams_rainbow_speed, 1.0 );
    const float fill_a = ( ( fill_col >> IM_COL32_A_SHIFT ) & 0xff ) / 255.f;
    const bool do_fill = !wire_look && ( ( ( fill_col >> IM_COL32_A_SHIFT ) & 0xff ) > 8 || xqz || rainbow || glass );
    const bool do_outline = wire_look || glass || ( ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) > 8 );

    if ( kind == 1 )
    {
        apply_instance( player );
        if ( config.esp.chams_highlight )
            apply_highlight( player );
        return;
    }
    if ( kind == 2 )
    {
        apply_highlight( player );
        return;
    }

    if ( config.esp.chams_highlight )
        apply_highlight( player );

    for ( const auto& bone : player.bones )
    {
        if ( !is_cham_bone( bone.name ) )
            continue;
        if ( !bone.position.is_valid( ) )
            continue;

        const std::shared_ptr<mesh_t> mesh = bone.mesh_id.empty( ) ? std::shared_ptr<mesh_t>{} : lookup_mesh( bone.mesh_id );
        if ( !mesh || mesh->positions.empty( ) || mesh->indices.size( ) < 3 )
        {
            ImU32 hull_fill = fill_col;
            if ( xqz )
                hull_fill = hidden_col;
            else if ( rainbow )
                hull_fill = hsv_col( rainbow_h + bone.position.x * 0.01f, 0.82f, 1.f, fill_a > 0.05f ? fill_a : 0.55f );
            draw_obb( canvas, bone, ve_dims, viewmatrix, offset, sx, sy,
                hull_fill, outline_col, t, do_fill || xqz || rainbow, do_outline || wire_look );
            continue;
        }

        const mesh_t& local = *mesh;

        math::vector3_t aabb_min = local.aabb_min;
        math::vector3_t aabb_max = local.aabb_max;
        if ( !local.has_aabb )
        {
            aabb_min = { 1e9f, 1e9f, 1e9f };
            aabb_max = { -1e9f, -1e9f, -1e9f };
            for ( const auto& v : local.positions )
            {
                aabb_min.x = ( std::min )( aabb_min.x, v.x );
                aabb_min.y = ( std::min )( aabb_min.y, v.y );
                aabb_min.z = ( std::min )( aabb_min.z, v.z );
                aabb_max.x = ( std::max )( aabb_max.x, v.x );
                aabb_max.y = ( std::max )( aabb_max.y, v.y );
                aabb_max.z = ( std::max )( aabb_max.z, v.z );
            }
        }

        math::vector3_t scale{ 1.f, 1.f, 1.f };
        const float ex = aabb_max.x - aabb_min.x;
        const float ey = aabb_max.y - aabb_min.y;
        const float ez = aabb_max.z - aabb_min.z;
        if ( ex > 1e-4f ) scale.x = bone.size.x / ex;
        if ( ey > 1e-4f ) scale.y = bone.size.y / ey;
        if ( ez > 1e-4f ) scale.z = bone.size.z / ez;

        const math::vector3_t center{
            ( aabb_min.x + aabb_max.x ) * 0.5f,
            ( aabb_min.y + aabb_max.y ) * 0.5f,
            ( aabb_min.z + aabb_max.z ) * 0.5f
        };

        std::vector<ImVec2> screen( local.positions.size( ) );
        std::vector<std::uint8_t> ok( local.positions.size( ), 0 );

        const cache::bone_pose_t pose = cache::read_bone_pose( bone );
        if ( !pose.valid )
            continue;

        for ( std::size_t i = 0; i < local.positions.size( ); ++i )
        {
            const math::vector3_t& v = local.positions[i];
            const math::vector3_t local_pos{
                ( v.x - center.x ) * scale.x,
                ( v.y - center.y ) * scale.y,
                ( v.z - center.z ) * scale.z
            };
            const math::vector3_t world = math::mat3_mul_vec3( pose.rotation, local_pos ) + pose.position;
            ImVec2 s;
            if ( project( world, ve_dims, viewmatrix, offset, sx, sy, s ) )
            {
                screen[i] = s;
                ok[i] = 1;
            }
        }

        const std::size_t tris = ( std::min )( local.indices.size( ) / 3, ( std::size_t )720 );
        std::unordered_map<std::uint64_t, int> edges;
        if ( do_outline )
            edges.reserve( tris * 2 );

        for ( std::size_t i = 0; i < tris; ++i )
        {
            const std::uint32_t i0 = local.indices[i * 3 + 0];
            const std::uint32_t i1 = local.indices[i * 3 + 1];
            const std::uint32_t i2 = local.indices[i * 3 + 2];
            if ( !ok[i0] || !ok[i1] || !ok[i2] )
                continue;

            const bool front = facing( screen[i0], screen[i1], screen[i2] );
            ImU32 tri_col = fill_col;
            if ( xqz )
                tri_col = front ? visible_col : hidden_col;
            else if ( rainbow )
                tri_col = hsv_col( rainbow_h + ( float )i * 0.004f + bone.position.x * 0.01f, 0.82f, 1.f, fill_a > 0.05f ? fill_a : 0.55f );

            if ( do_fill && ( !xqz || !front ) )
                canvas->AddTriangleFilled( screen[i0], screen[i1], screen[i2], xqz ? hidden_col : tri_col );

            if ( do_outline && ( wire_look || front ) )
            {
                edges[edge_key( i0, i1 )]++;
                edges[edge_key( i1, i2 )]++;
                edges[edge_key( i2, i0 )]++;
            }
        }

        if ( xqz && do_fill )
        {
            for ( std::size_t i = 0; i < tris; ++i )
            {
                const std::uint32_t i0 = local.indices[i * 3 + 0];
                const std::uint32_t i1 = local.indices[i * 3 + 1];
                const std::uint32_t i2 = local.indices[i * 3 + 2];
                if ( !ok[i0] || !ok[i1] || !ok[i2] )
                    continue;
                if ( !facing( screen[i0], screen[i1], screen[i2] ) )
                    continue;
                canvas->AddTriangleFilled( screen[i0], screen[i1], screen[i2], visible_col );
            }
        }

        if ( do_outline )
        {
            ImU32 wcol = outline_col;
            if ( rainbow )
                wcol = hsv_col( rainbow_h + 0.08f, 0.55f, 1.f, 0.92f );
            else if ( ( ( wcol >> IM_COL32_A_SHIFT ) & 0xff ) < 8 )
                wcol = IM_COL32( 90, 16, 24, 210 );

            for ( const auto& kv : edges )
            {
                const std::uint32_t a = ( std::uint32_t )( kv.first >> 32 );
                const std::uint32_t b = ( std::uint32_t )( kv.first & 0xffffffffu );
                if ( !ok[a] || !ok[b] )
                    continue;
                const float w = ( !wire_look && kv.second == 1 ) ? t + 0.35f : t;
                canvas->AddLine( screen[a], screen[b], wcol, w );
            }
        }
    }
}

auto apply_instance( const cache::player& player ) -> void
{
    if ( !player.character )
        return;

    static std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> last_write;
    const auto now = std::chrono::steady_clock::now( );
    auto it = last_write.find( player.character );
    if ( it != last_write.end( ) && now - it->second < std::chrono::milliseconds( 80 ) )
        return;
    last_write[player.character] = now;
    if ( last_write.size( ) > 64 )
        last_write.clear( );

    math::vector3_t fill{
        config.esp.chams_color[0],
        config.esp.chams_color[1],
        config.esp.chams_color[2]
    };
    if ( config.esp.chams_style == 2 )
    {
        const float h = ( float )std::fmod( ImGui::GetTime( ) * ( double )config.esp.chams_rainbow_speed, 1.0 );
        fill = hsv_rgb( h, 0.85f, 1.f );
    }
    else if ( config.esp.chams_style == 1 )
    {
        fill = {
            config.esp.chams_visible[0],
            config.esp.chams_visible[1],
            config.esp.chams_visible[2]
        };
    }

    if ( !std::isfinite( fill.x ) || !std::isfinite( fill.y ) || !std::isfinite( fill.z ) )
        return;

    const float tr = std::clamp( config.esp.chams_fill_transparency, 0.f, 0.95f );
    for ( const auto& bone : player.bones )
    {
        if ( !bone.part || !is_cham_bone( bone.name ) )
            continue;
        if ( !usermode->is_valid( bone.part ) )
            continue;
        usermode->write<math::vector3_t>( bone.part + offsets::basepart::color3, fill );
        usermode->write<float>( bone.part + offsets::basepart::transparency, tr );
    }
}

auto apply_highlight( const cache::player& player ) -> void
{
    if ( !player.character )
        return;

    static std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> last_write;
    const auto now = std::chrono::steady_clock::now( );
    auto it = last_write.find( player.character );
    if ( it != last_write.end( ) && now - it->second < std::chrono::milliseconds( 80 ) )
        return;
    last_write[player.character] = now;
    if ( last_write.size( ) > 64 )
        last_write.clear( );

    const sdk::u_instance hl = find_highlight( player.character );
    if ( !hl )
        return;

    const uintptr_t addr = hl.address( );
    math::vector3_t fill{
        config.esp.chams_color[0],
        config.esp.chams_color[1],
        config.esp.chams_color[2]
    };
    math::vector3_t outline{
        config.esp.chams_outline[0],
        config.esp.chams_outline[1],
        config.esp.chams_outline[2]
    };

    if ( config.esp.chams_style == 1 )
    {
        fill = { config.esp.chams_visible[0], config.esp.chams_visible[1], config.esp.chams_visible[2] };
        outline = { config.esp.chams_hidden[0], config.esp.chams_hidden[1], config.esp.chams_hidden[2] };
    }
    else if ( config.esp.chams_style == 2 )
    {
        const float h = ( float )std::fmod( ImGui::GetTime( ) * ( double )config.esp.chams_rainbow_speed, 1.0 );
        fill = hsv_rgb( h, 0.85f, 1.f );
        outline = hsv_rgb( h + 0.15f, 0.45f, 1.f );
    }

    if ( !usermode->is_valid( addr ) )
        return;

    usermode->write<math::vector3_t>( addr + offsets::highlight::fill_color, fill );
    usermode->write<math::vector3_t>( addr + offsets::highlight::outline_color, outline );
    usermode->write<int>( addr + offsets::highlight::depth_mode, 0 );
    usermode->write<float>( addr + offsets::highlight::fill_transparency, config.esp.chams_fill_transparency );
    usermode->write<float>( addr + offsets::highlight::outline_transparency, config.esp.chams_outline_transparency );
    usermode->write<uint8_t>( addr + offsets::highlight::enabled, 1 );
}

} 
