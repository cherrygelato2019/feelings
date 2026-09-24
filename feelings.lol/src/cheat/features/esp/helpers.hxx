#pragma once

#include <src/cheat/config/config.cuh>
#include <src/engine/cache/cache.cuh>
#include <src/engine/cache/draw.hxx>
#include <src/engine/math/math.cuh>
#include <src/engine/raycast/engine_ray.hxx>
#include <src/engine/sdk/visualengine/visualengine.cuh>

#include <imgui.h>
#include <src/cheat/features/esp/meshchams.cuh>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <immintrin.h>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

struct bounding_box_t
{
    ImVec2 min;
    ImVec2 max;
    bool valid;
    bool stretched = false;
};

inline auto pack4( const float c[4] ) -> ImU32
{
    return IM_COL32(
        ( int )( c[0] * 255.f ),
        ( int )( c[1] * 255.f ),
        ( int )( c[2] * 255.f ),
        ( int )( c[3] * 255.f ) );
}

struct proj_frame_t
{
    math::vector2_t ve{};
    math::vector2_t draw{};
    math::vector2_t offset{};
    float sx = 1.f;
    float sy = 1.f;
    bool ready = false;
};

inline auto proj_frame( ) -> proj_frame_t&
{
    static proj_frame_t f;
    return f;
}

inline auto begin_proj_frame( ) -> void
{
    auto& f = proj_frame( );
    // Viewmatrix is authored for VisualEngine dims — W2S there, then scale to overlay.
    f.ve = sdk::u_visualengine::ve_dimensions( );
    f.draw = sdk::u_visualengine::dimensions( );
    if ( ImGui::GetCurrentContext( ) )
    {
        const ImVec2 ds = ImGui::GetIO( ).DisplaySize;
        if ( ds.x > 16.f && ds.y > 16.f )
            f.draw = { ds.x, ds.y };
    }
    f.offset = sdk::u_visualengine::client_offset( );
    f.sx = 1.f;
    f.sy = 1.f;
    if ( f.ve.x < 16.f || f.ve.y < 16.f )
        f.ve = f.draw;
    else if ( f.draw.x > 16.f && f.draw.y > 16.f
        && ( std::fabs( f.draw.x - f.ve.x ) > 0.5f || std::fabs( f.draw.y - f.ve.y ) > 0.5f ) )
    {
        f.sx = f.draw.x / f.ve.x;
        f.sy = f.draw.y / f.ve.y;
    }
    if ( sdk::u_visualengine::overlay_synced( ) )
        f.offset = { 0.f, 0.f };
    f.ready = true;
}

inline auto begin_proj_frame_from( const cache_c::ve_cache_t& ve ) -> void
{
    if ( !ve.ready || ve.ve.x < 16.f )
    {
        begin_proj_frame( );
        return;
    }

    auto& f = proj_frame( );
    f.ve = ve.ve;
    f.draw = ve.draw.x > 16.f ? ve.draw : ve.ve;
    f.offset = sdk::u_visualengine::overlay_synced( ) ? math::vector2_t{} : ve.offset;
    f.sx = ve.sx;
    f.sy = ve.sy;
    f.ready = true;
}

inline auto project_to_draw( const math::vector3_t& world, const math::matrix4_t& viewmatrix ) -> math::vector2_t
{
    const proj_frame_t& f = proj_frame( );
    math::vector2_t space = f.ready ? f.ve : sdk::u_visualengine::ve_dimensions( );
    if ( space.x < 16.f || space.y < 16.f )
        space = f.ready ? f.draw : sdk::u_visualengine::dimensions( );

    math::vector2_t s = math::world_to_screen( world, viewmatrix, space );
    if ( !std::isfinite( s.x ) || !std::isfinite( s.y ) )
        return s;
    if ( f.ready )
    {
        s.x *= f.sx;
        s.y *= f.sy;
    }
    return s;
}

// Inferno-style VisualEngineCache + bone/box cache. Transform thread owns bone RPM.
// Draw path never RPM's parts or viewmatrix — only reads this snapshot.
struct esp_bone_pose_t
{
    std::string name;
    math::vector3_t position{};
    math::vector3_t size{};
    math::matrix3_t rotation{};
};

struct esp_player_bones_t
{
    uintptr_t key = 0;
    std::vector<esp_bone_pose_t> bones;
    ImVec2 box_min{};
    ImVec2 box_max{};
    bool box_valid = false;
};

struct esp_bone_cache_t
{
    math::matrix4_t viewmatrix{};
    math::vector2_t ve{};
    math::vector2_t draw{};
    math::vector2_t offset{};
    float sx = 1.f;
    float sy = 1.f;
    std::vector<esp_player_bones_t> players;
    bool ready = false;
};

inline auto esp_bone_cache( ) -> esp_bone_cache_t&
{
    static esp_bone_cache_t c;
    return c;
}

inline auto ve_w2s( const math::vector3_t& world, const math::matrix4_t& vm, const math::vector2_t& dims ) -> math::vector2_t
{
    return math::world_to_screen( world, vm, dims );
}

inline auto begin_esp_bone_cache( const std::vector<cache::player>& players, const cache_c::ve_cache_t& ve ) -> void
{
    auto& c = esp_bone_cache( );

    // Snapshot VE for skeleton/chams only. Boxes are already final pixels from transform.
    c.viewmatrix = ve.viewmatrix;
    c.ve = ve.ve;
    c.draw = ve.draw;
    c.offset = ve.offset;
    c.sx = ve.sx;
    c.sy = ve.sy;

    c.players.clear( );
    c.players.reserve( players.size( ) );

    for ( const auto& p : players )
    {
        esp_player_bones_t entry{};
        entry.key = p.address ? p.address : p.character;
        entry.bones.reserve( p.bones.size( ) );

        for ( const auto& b : p.bones )
        {
            if ( !b.position.is_valid( )
                || ( std::fabs( b.position.x ) + std::fabs( b.position.y ) + std::fabs( b.position.z ) ) < 0.01f )
                continue;
            esp_bone_pose_t pose{};
            pose.name = b.name;
            pose.position = b.position;
            pose.size = b.size;
            pose.rotation = b.rotation;
            entry.bones.push_back( std::move( pose ) );
        }

        // Copy baked pixels as-is — no per-frame remap (that was the jitter).
        if ( p.box_valid )
        {
            entry.box_min = ImVec2( p.box_min_x, p.box_min_y );
            entry.box_max = ImVec2( p.box_max_x, p.box_max_y );
            entry.box_valid = true;
        }

        c.players.push_back( std::move( entry ) );
    }
    c.ready = ve.ready;
}
inline auto find_esp_bones( uintptr_t key ) -> const esp_player_bones_t*
{
    const auto& c = esp_bone_cache( );
    if ( !key )
        return nullptr;
    for ( const auto& e : c.players )
    {
        if ( e.key == key )
            return &e;
    }
    return nullptr;
}

inline auto project_cached( const math::vector3_t& world, const math::matrix4_t& /*viewmatrix*/ ) -> math::vector2_t
{
    const esp_bone_cache_t& c = esp_bone_cache( );
    if ( !c.ready )
        return { std::numeric_limits<float>::quiet_NaN( ), std::numeric_limits<float>::quiet_NaN( ) };

    math::vector2_t space = c.ve;
    if ( space.x < 16.f || space.y < 16.f )
        space = c.draw;

    // Cached VisualEngineCache viewmatrix + dims — never mid-draw RPM.
    math::vector2_t s = ve_w2s( world, c.viewmatrix, space );
    if ( !std::isfinite( s.x ) || !std::isfinite( s.y ) )
        return s;
    s.x = s.x * c.sx + c.offset.x;
    s.y = s.y * c.sy + c.offset.y;
    return s;
}

inline auto get_bounds( const cache::player& player, const math::vector2_t& dimensions, const math::matrix4_t& viewmatrix ) -> bounding_box_t
{
    const proj_frame_t& pf = proj_frame( );
    const math::vector2_t offset = ( pf.ready && sdk::u_visualengine::overlay_synced( ) )
        ? math::vector2_t{}
        : ( pf.ready ? pf.offset : math::vector2_t{} );
    ( void )dimensions;

    math::vector2_t space = pf.ready ? pf.ve : dimensions;
    if ( space.x < 16.f || space.y < 16.f )
        space = pf.ready ? pf.draw : dimensions;
    const float scale_x = pf.ready ? pf.sx : 1.f;
    const float scale_y = pf.ready ? pf.sy : 1.f;

    math::vector2_t bmin = { FLT_MAX, FLT_MAX };
    math::vector2_t bmax = { -FLT_MAX, -FLT_MAX };
    int hits = 0;

    auto add_world = [&]( const math::vector3_t& world ) -> bool
    {
        if ( !world.is_valid( ) )
            return false;
        math::vector2_t s = math::world_to_screen( world, viewmatrix, space );
        if ( !std::isfinite( s.x ) || !std::isfinite( s.y ) )
            return false;
        s.x = s.x * scale_x + offset.x;
        s.y = s.y * scale_y + offset.y;
        bmin.x = ( std::min )( bmin.x, s.x );
        bmin.y = ( std::min )( bmin.y, s.y );
        bmax.x = ( std::max )( bmax.x, s.x );
        bmax.y = ( std::max )( bmax.y, s.y );
        return true;
    };

    auto add_part = [&]( const cache::player::bone& part, bool use_obb ) -> int
    {
        if ( !part.position.is_valid( )
            || ( std::fabs( part.position.x ) + std::fabs( part.position.y ) + std::fabs( part.position.z ) ) < 0.01f )
            return 0;

        if ( !use_obb )
            return add_world( part.position ) ? 1 : 0;

        math::vector3_t half{
            part.size.x * 0.5f,
            part.size.y * 0.5f,
            part.size.z * 0.5f
        };
        if ( !( half.x > 0.08f && half.y > 0.08f && half.z > 0.08f ) )
            half = { 0.35f, 0.35f, 0.35f };
        half.x = std::clamp( half.x, 0.12f, 1.8f );
        half.y = std::clamp( half.y, 0.12f, 1.8f );
        half.z = std::clamp( half.z, 0.12f, 1.8f );

        static const float k_c[8][3] = {
            { -1.f, -1.f, -1.f }, {  1.f, -1.f, -1.f },
            { -1.f,  1.f, -1.f }, {  1.f,  1.f, -1.f },
            { -1.f, -1.f,  1.f }, {  1.f, -1.f,  1.f },
            { -1.f,  1.f,  1.f }, {  1.f,  1.f,  1.f },
        };

        int part_hits = 0;
        for ( int i = 0; i < 8; ++i )
        {
            const math::vector3_t local{
                k_c[i][0] * half.x,
                k_c[i][1] * half.y,
                k_c[i][2] * half.z
            };
            if ( add_world( math::mat3_mul_vec3( part.rotation, local ) + part.position ) )
                ++part_hits;
        }
        if ( part_hits < 1 && add_world( part.position ) )
            ++part_hits;
        return part_hits;
    };

    for ( const auto& part : player.bones )
    {
        if ( !cache::is_bounds_bone_name( part.name, player.rig_type ) )
            continue;
        hits += add_part( part, true );
    }

    if ( hits < 1 )
    {
        for ( const auto& part : player.bones )
        {
            if ( cache::is_hrp_name( part.name ) || part.name.rfind( "Acc:", 0 ) == 0
                || part.name == "Handle" || part.name.rfind( "Hitbox", 0 ) == 0 )
                continue;
            hits += add_part( part, true );
        }
    }

    if ( hits < 1 )
    {
        for ( const auto& part : player.bones )
        {
            if ( part.name == "Head" || part.name == "UpperTorso" || part.name == "Torso"
                || part.name == "LowerTorso" )
                hits += add_part( part, false );
        }
    }

    if ( hits < 1 )
        return { {}, {}, false, false };

    return {
        ImVec2( std::floor( bmin.x ), std::floor( bmin.y ) ),
        ImVec2( std::ceil( bmax.x ), std::ceil( bmax.y ) ),
        true,
        false
    };
}
inline auto ray_aabb( const math::vector3_t& origin, const math::vector3_t& dir, const math::vector3_t& bmin, const math::vector3_t& bmax, float& out_t ) -> bool
{
    float tmin = 0.f;
    float tmax = 1e9f;

    const float o[3] = { origin.x, origin.y, origin.z };
    const float d[3] = { dir.x, dir.y, dir.z };
    const float mn[3] = { bmin.x, bmin.y, bmin.z };
    const float mx[3] = { bmax.x, bmax.y, bmax.z };

    for ( int i = 0; i < 3; ++i )
    {
        if ( std::fabs( d[i] ) < 1e-8f )
        {
            if ( o[i] < mn[i] || o[i] > mx[i] )
                return false;
            continue;
        }

        float inv = 1.f / d[i];
        float t1 = ( mn[i] - o[i] ) * inv;
        float t2 = ( mx[i] - o[i] ) * inv;
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

// Engine raycast wall check: write WorldRoot Variant, read RaycastResult.
inline auto wall_visible( const math::vector3_t& from, const math::vector3_t& to, const std::vector<cache::world_part>& parts ) -> bool
{
    return engine_ray::visible( from, to, parts );
}

inline auto player_aim_point( const cache::player& player ) -> math::vector3_t
{
    for ( const auto& b : player.bones )
    {
        if ( b.name == "Head" && b.position.is_valid( ) )
            return cache::bone_world( b );
    }
    for ( const auto& b : player.bones )
    {
        if ( b.name == "HumanoidRootPart" && b.position.is_valid( ) )
            return cache::bone_world( b );
    }
    return math::vector3_t::nan( );
}

inline auto add_shaded_glow( ImDrawList* canvas, ImVec2 box_min, ImVec2 box_max, ImU32 color, float size, ImU32 outer_col = 0 ) -> void
{
    if ( !canvas || size <= 0.5f )
        return;
    const ImDrawListFlags aa_prev = cache::begin_aa( canvas, config.esp.aa_glow );

    const ImU32 a = color & IM_COL32_A_MASK;
    if ( a == 0 )
    {
        cache::end_aa( canvas, aa_prev );
        return;
    }

    auto paint = [&]( ImU32 col, float s )
    {
        if ( ( col & IM_COL32_A_MASK ) == 0 || s <= 0.5f )
            return;
        const ImU32 clear = col & ~IM_COL32_A_MASK;
        canvas->AddRectFilledMultiColor(
            ImVec2( box_min.x, box_min.y - s ), ImVec2( box_max.x, box_min.y ),
            clear, clear, col, col );
        canvas->AddRectFilledMultiColor(
            ImVec2( box_min.x, box_max.y ), ImVec2( box_max.x, box_max.y + s ),
            col, col, clear, clear );
        canvas->AddRectFilledMultiColor(
            ImVec2( box_min.x - s, box_min.y ), ImVec2( box_min.x, box_max.y ),
            clear, col, col, clear );
        canvas->AddRectFilledMultiColor(
            ImVec2( box_max.x, box_min.y ), ImVec2( box_max.x + s, box_max.y ),
            col, clear, clear, col );
        canvas->AddRectFilledMultiColor(
            ImVec2( box_min.x - s, box_min.y - s ), ImVec2( box_min.x, box_min.y ),
            clear, clear, col, clear );
        canvas->AddRectFilledMultiColor(
            ImVec2( box_max.x, box_min.y - s ), ImVec2( box_max.x + s, box_min.y ),
            clear, clear, clear, col );
        canvas->AddRectFilledMultiColor(
            ImVec2( box_min.x - s, box_max.y ), ImVec2( box_min.x, box_max.y + s ),
            clear, col, clear, clear );
        canvas->AddRectFilledMultiColor(
            ImVec2( box_max.x, box_max.y ), ImVec2( box_max.x + s, box_max.y + s ),
            col, clear, clear, clear );
    };

    if ( ( outer_col & IM_COL32_A_MASK ) != 0 )
        paint( outer_col, size * 1.35f );
    paint( color, size );
    cache::end_aa( canvas, aa_prev );
}

inline auto lerp_col32( ImU32 a, ImU32 b, float t ) -> ImU32
{
    t = std::clamp( t, 0.f, 1.f );
    const int ar = ( int )( ( a >> IM_COL32_R_SHIFT ) & 0xff );
    const int ag = ( int )( ( a >> IM_COL32_G_SHIFT ) & 0xff );
    const int ab = ( int )( ( a >> IM_COL32_B_SHIFT ) & 0xff );
    const int aa = ( int )( ( a >> IM_COL32_A_SHIFT ) & 0xff );
    const int br = ( int )( ( b >> IM_COL32_R_SHIFT ) & 0xff );
    const int bg = ( int )( ( b >> IM_COL32_G_SHIFT ) & 0xff );
    const int bb = ( int )( ( b >> IM_COL32_B_SHIFT ) & 0xff );
    const int ba = ( int )( ( b >> IM_COL32_A_SHIFT ) & 0xff );
    return IM_COL32(
        ( int )( ar + ( br - ar ) * t ),
        ( int )( ag + ( bg - ag ) * t ),
        ( int )( ab + ( bb - ab ) * t ),
        ( int )( aa + ( ba - aa ) * t ) );
}

inline auto box_fill_angle_deg( ) -> float
{
    if ( config.esp.box_fill_auto_rotate )
        return ( float )( ImGui::GetTime( ) * ( double )config.esp.box_fill_rotate_speed );
    return config.esp.box_fill_rotate;
}

inline auto draw_box_fill( ImDrawList* canvas, ImVec2 fill_min, ImVec2 fill_max, ImU32 col_a, ImU32 col_b, float degrees ) -> void
{
    if ( !canvas || fill_max.x <= fill_min.x || fill_max.y <= fill_min.y )
        return;

    const ImU32 bottom = col_b ? col_b : col_a;
    if ( !col_b || col_a == bottom )
    {
        canvas->AddRectFilled( fill_min, fill_max, col_a );
        return;
    }

    // True angled gradient: oversized dual-color quad clipped to the box.
    const float rad = degrees * 0.01745329251f;
    const float dx = std::cos( rad );
    const float dy = std::sin( rad );
    const float cx = ( fill_min.x + fill_max.x ) * 0.5f;
    const float cy = ( fill_min.y + fill_max.y ) * 0.5f;
    const float w = fill_max.x - fill_min.x;
    const float h = fill_max.y - fill_min.y;
    const float half = 0.5f * std::sqrt( w * w + h * h ) + 2.f;
    const float px = -dy;
    const float py = dx;

    const ImVec2 v0( cx - dx * half - px * half, cy - dy * half - py * half ); // col_a
    const ImVec2 v1( cx + dx * half - px * half, cy + dy * half - py * half ); // col_b
    const ImVec2 v2( cx + dx * half + px * half, cy + dy * half + py * half ); // col_b
    const ImVec2 v3( cx - dx * half + px * half, cy - dy * half + py * half ); // col_a

    canvas->PushClipRect( fill_min, fill_max, true );
    canvas->PrimReserve( 6, 4 );
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel( );
    const int vtx = canvas->_VtxCurrentIdx;
    canvas->PrimWriteVtx( v0, uv, col_a );
    canvas->PrimWriteVtx( v1, uv, bottom );
    canvas->PrimWriteVtx( v2, uv, bottom );
    canvas->PrimWriteVtx( v3, uv, col_a );
    canvas->PrimWriteIdx( ( ImDrawIdx )( vtx + 0 ) );
    canvas->PrimWriteIdx( ( ImDrawIdx )( vtx + 1 ) );
    canvas->PrimWriteIdx( ( ImDrawIdx )( vtx + 2 ) );
    canvas->PrimWriteIdx( ( ImDrawIdx )( vtx + 0 ) );
    canvas->PrimWriteIdx( ( ImDrawIdx )( vtx + 2 ) );
    canvas->PrimWriteIdx( ( ImDrawIdx )( vtx + 3 ) );
    canvas->PopClipRect( );
}

inline auto box( ImDrawList* canvas, ImVec2 box_min, ImVec2 box_max, ImU32 color, float thickness, bool outline, bool filled, ImU32 filled_col, int box_type = 0, ImU32 outline_col = IM_COL32( 0, 0, 0, 255 ), ImU32 filled_col2 = 0 ) -> void
{
    const bool aa = config.esp.aa_box;
    if ( !aa )
    {
        box_min = cache::px2( box_min );
        box_max = cache::px2( box_max );
    }
    if ( box_max.x <= box_min.x + 1.f )
        box_max.x = box_min.x + 2.f;
    if ( box_max.y <= box_min.y + 1.f )
        box_max.y = box_min.y + 2.f;

    thickness = ( std::max )( 1.f, thickness );
    const int layers = ( std::max )( 1, ( int )std::lround( thickness ) );
    const ImU32 oc = outline_col ? outline_col : IM_COL32( 0, 0, 0, 255 );
    const bool do_outline = outline && ( ( ( oc >> IM_COL32_A_SHIFT ) & 0xff ) != 0 );
    const int mode = ( config.esp.box_outline_mode < 0 || config.esp.box_outline_mode > 2 ) ? 2 : config.esp.box_outline_mode;
    const bool outer = do_outline && ( mode == 0 || mode == 2 );
    const bool inner = do_outline && ( mode == 1 || mode == 2 );
    const ImDrawListFlags prev = cache::begin_aa( canvas, aa );

    if ( filled )
    {
        const float inset = ( float )layers + ( inner ? 1.f : 0.f );
        const ImVec2 fill_min( box_min.x + inset, box_min.y + inset );
        const ImVec2 fill_max( box_max.x - inset, box_max.y - inset );
        if ( fill_max.x > fill_min.x && fill_max.y > fill_min.y )
        {
            const ImU32 bottom = filled_col2 ? filled_col2 : filled_col;
            draw_box_fill( canvas, fill_min, fill_max, filled_col, bottom, box_fill_angle_deg( ) );
        }
    }

    auto stroke_px = [ & ]( float x1, float y1, float x2, float y2, ImU32 col ) -> void
    {
        cache::stroke_rect_px( canvas, x1, y1, x2, y2, col );
    };
    auto stroke_aa = [ & ]( float x1, float y1, float x2, float y2, ImU32 col ) -> void
    {
        canvas->AddRect( ImVec2( x1, y1 ), ImVec2( x2, y2 ), col, 0.f, 0, 1.f );
    };

    if ( box_type == 1 )
    {
        const float w = box_max.x - box_min.x;
        const float h = box_max.y - box_min.y;
        const float lx = ( std::max )( 6.f, w * 0.25f );
        const float ly = ( std::max )( 6.f, h * 0.25f );

        auto arms_px = [ & ]( float ox, float oy, float arm_x, float arm_y, ImU32 col ) -> void
        {
            cache::stroke_h_px( canvas, box_min.x + ox, box_min.x + ox + arm_x, box_min.y + oy, col );
            cache::stroke_v_px( canvas, box_min.x + ox, box_min.y + oy, box_min.y + oy + arm_y, col );
            cache::stroke_h_px( canvas, box_max.x + ox - arm_x, box_max.x + ox, box_min.y + oy, col );
            cache::stroke_v_px( canvas, box_max.x + ox, box_min.y + oy, box_min.y + oy + arm_y, col );
            cache::stroke_h_px( canvas, box_min.x + ox, box_min.x + ox + arm_x, box_max.y + oy, col );
            cache::stroke_v_px( canvas, box_min.x + ox, box_max.y + oy - arm_y, box_max.y + oy, col );
            cache::stroke_h_px( canvas, box_max.x + ox - arm_x, box_max.x + ox, box_max.y + oy, col );
            cache::stroke_v_px( canvas, box_max.x + ox, box_max.y + oy - arm_y, box_max.y + oy, col );
        };

        auto arms_aa = [ & ]( float ox, float oy, float arm_x, float arm_y, ImU32 col ) -> void
        {
            ImVec2 c[4][3] = {
                { ImVec2( box_min.x + ox + arm_x, box_min.y + oy ), ImVec2( box_min.x + ox, box_min.y + oy ), ImVec2( box_min.x + ox, box_min.y + oy + arm_y ) },
                { ImVec2( box_max.x + ox - arm_x, box_min.y + oy ), ImVec2( box_max.x + ox, box_min.y + oy ), ImVec2( box_max.x + ox, box_min.y + oy + arm_y ) },
                { ImVec2( box_min.x + ox + arm_x, box_max.y + oy ), ImVec2( box_min.x + ox, box_max.y + oy ), ImVec2( box_min.x + ox, box_max.y + oy - arm_y ) },
                { ImVec2( box_max.x + ox - arm_x, box_max.y + oy ), ImVec2( box_max.x + ox, box_max.y + oy ), ImVec2( box_max.x + ox, box_max.y + oy - arm_y ) },
            };
            for ( int i = 0; i < 4; ++i )
                canvas->AddPolyline( c[i], 3, col, ImDrawFlags_None, 1.f );
        };

        auto arms = [ & ]( float ox, float oy, float arm_x, float arm_y, ImU32 col ) -> void
        {
            if ( aa )
                arms_aa( ox, oy, arm_x, arm_y, col );
            else
                arms_px( ox, oy, arm_x, arm_y, col );
        };

        if ( outer )
            arms( -1.f, -1.f, lx + 1.f, ly + 1.f, oc );
        for ( int i = 0; i < layers; ++i )
            arms( ( float )i, ( float )i, ( std::max )( 2.f, lx - ( float )i ), ( std::max )( 2.f, ly - ( float )i ), color );
        if ( inner )
            arms( ( float )layers, ( float )layers, ( std::max )( 2.f, lx - ( float )layers ), ( std::max )( 2.f, ly - ( float )layers ), oc );

        cache::end_aa( canvas, prev );
        return;
    }

    if ( outer )
    {
        if ( aa )
            stroke_aa( box_min.x - 1.f, box_min.y - 1.f, box_max.x + 1.f, box_max.y + 1.f, oc );
        else
            stroke_px( box_min.x - 1.f, box_min.y - 1.f, box_max.x + 1.f, box_max.y + 1.f, oc );
    }
    if ( aa )
    {
        for ( int i = 0; i < layers; ++i )
            stroke_aa( box_min.x + ( float )i, box_min.y + ( float )i, box_max.x - ( float )i, box_max.y - ( float )i, color );
    }
    else
    {
        for ( int i = 0; i < layers; ++i )
            stroke_px( box_min.x + ( float )i, box_min.y + ( float )i, box_max.x - ( float )i, box_max.y - ( float )i, color );
    }
    if ( inner )
    {
        const float in = ( float )layers;
        if ( aa )
            stroke_aa( box_min.x + in, box_min.y + in, box_max.x - in, box_max.y - in, oc );
        else
            stroke_px( box_min.x + in, box_min.y + in, box_max.x - in, box_max.y - in, oc );
    }

    cache::end_aa( canvas, prev );
}
inline auto draw_outlined_text( ImDrawList* canvas, const char* str, ImVec2 pos, ImU32 color, bool outline, bool dropshadow, int outline_style = 1, ImU32 outline_col = IM_COL32( 0, 0, 0, 255 ) ) -> ImVec2
{
    ImFont* font = ImGui::GetFont( );
    const float font_size = ImGui::GetFontSize( );
    const ImVec2 size = font->CalcTextSizeA( font_size, FLT_MAX, 0.f, str );
    pos = cache::px2( pos );

    // Outline XOR dropshadow — both at once stacks a second dark ring over the outline.
    const bool use_outline = outline && ( ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) != 0 );
    if ( dropshadow && !use_outline )
    {
        ImU32 shadow = outline_col ? outline_col : IM_COL32( 0, 0, 0, 180 );
        const int a = ( int )( ( ( shadow >> IM_COL32_A_SHIFT ) & 0xff ) * 0.7f );
        shadow = ( shadow & ~IM_COL32_A_MASK ) | ( ( ImU32 )a << IM_COL32_A_SHIFT );
        canvas->AddText( font, font_size, ImVec2( pos.x + 1.f, pos.y + 1.f ), shadow, str );
    }

    {
        const ImDrawListFlags aa_prev = cache::begin_aa( canvas, config.esp.aa_text && !use_outline );
        cache::text_nway( canvas, font, font_size, pos, color, outline_col, str, use_outline, outline_style );
        cache::end_aa( canvas, aa_prev );
    }
    return size;
}

inline auto text( ImDrawList* canvas, const std::string& str, ImVec2 box_min, ImVec2 box_max, ImU32 color, bool outline, bool dropshadow, int health_side, float health_gap, float health_width, ImU32 outline_col = IM_COL32( 0, 0, 0, 255 ) ) -> void
{
    if ( str.empty( ) )
        return;

    ImFont* font = ImGui::GetFont( );
    const float font_size = ImGui::GetFontSize( );
    const ImVec2 text_size = font->CalcTextSizeA( font_size, FLT_MAX, 0.f, str.c_str( ) );
    const ImVec2 size = { box_max.x - box_min.x, box_max.y - box_min.y };

    float y_offset = 3.f;
    if ( health_side == 2 )
        y_offset += health_gap + health_width;

    const ImVec2 pos = {
        box_min.x + ( size.x * 0.5f ) - ( text_size.x * 0.5f ),
        box_min.y - ( text_size.y + y_offset )
    };

    draw_outlined_text( canvas, str.c_str( ), pos, color, outline, dropshadow, config.esp.text_outline_style, outline_col );
}

struct esp_line_t
{
    const char* text;
    ImU32 color;
    ImU32 outline;
};

inline auto text_top_stack( ImDrawList* canvas, const esp_line_t* lines, int count, ImVec2 box_min, ImVec2 box_max, bool outline, bool dropshadow, int health_side, float health_gap, float health_width ) -> void
{
    if ( !lines || count <= 0 )
        return;

    ImFont* font = ImGui::GetFont( );
    const float font_size = ImGui::GetFontSize( );
    float y_offset = 3.f;
    if ( health_side == 2 )
        y_offset += health_gap + health_width;

    float total_h = 0.f;
    for ( int i = 0; i < count; ++i )
    {
        if ( !lines[i].text || !lines[i].text[0] )
            continue;
        total_h += font->CalcTextSizeA( font_size, FLT_MAX, 0.f, lines[i].text ).y + 1.f;
    }

    float y = box_min.y - total_h - y_offset + 1.f;
    const float cx = ( box_min.x + box_max.x ) * 0.5f;

    for ( int i = 0; i < count; ++i )
    {
        if ( !lines[i].text || !lines[i].text[0] )
            continue;
        const ImVec2 sz = font->CalcTextSizeA( font_size, FLT_MAX, 0.f, lines[i].text );
        draw_outlined_text( canvas, lines[i].text, ImVec2( cx - sz.x * 0.5f, y ), lines[i].color, outline, dropshadow, config.esp.text_outline_style, lines[i].outline );
        y += sz.y + 1.f;
    }
}

inline auto text_bottom_stack( ImDrawList* canvas, const esp_line_t* lines, int count, ImVec2 box_min, ImVec2 box_max, bool outline, bool dropshadow, int health_side, float health_gap, float health_width ) -> void
{
    if ( !lines || count <= 0 )
        return;

    ImFont* font = ImGui::GetFont( );
    const float font_size = ImGui::GetFontSize( );
    float y = box_max.y + 3.f;
    if ( health_side == 3 )
        y += health_gap + health_width;

    const float cx = ( box_min.x + box_max.x ) * 0.5f;
    for ( int i = 0; i < count; ++i )
    {
        if ( !lines[i].text || !lines[i].text[0] )
            continue;
        const ImVec2 sz = font->CalcTextSizeA( font_size, FLT_MAX, 0.f, lines[i].text );
        draw_outlined_text( canvas, lines[i].text, ImVec2( std::round( cx - sz.x * 0.5f ), std::round( y ) ), lines[i].color, outline, dropshadow, config.esp.text_outline_style, lines[i].outline );
        y += sz.y + 1.f;
    }
}

inline auto text_top_stack( ImDrawList* canvas, const std::vector<std::pair<std::string, ImU32>>& lines, ImVec2 box_min, ImVec2 box_max, bool outline, bool dropshadow, int health_side, float health_gap, float health_width ) -> void
{
    if ( lines.empty( ) )
        return;
    esp_line_t tmp[8];
    const int n = ( int )( std::min )( lines.size( ), ( size_t )8 );
    const ImU32 oc = pack4( config.esp.display_outline );
    for ( int i = 0; i < n; ++i )
        tmp[i] = { lines[i].first.c_str( ), lines[i].second, oc };
    text_top_stack( canvas, tmp, n, box_min, box_max, outline, dropshadow, health_side, health_gap, health_width );
}

inline auto text_bottom_stack( ImDrawList* canvas, const std::vector<std::pair<std::string, ImU32>>& lines, ImVec2 box_min, ImVec2 box_max, bool outline, bool dropshadow, int health_side, float health_gap, float health_width ) -> void
{
    if ( lines.empty( ) )
        return;
    esp_line_t tmp[8];
    const int n = ( int )( std::min )( lines.size( ), ( size_t )8 );
    const ImU32 oc = pack4( config.esp.distance_outline );
    for ( int i = 0; i < n; ++i )
        tmp[i] = { lines[i].first.c_str( ), lines[i].second, oc };
    text_bottom_stack( canvas, tmp, n, box_min, box_max, outline, dropshadow, health_side, health_gap, health_width );
}

inline auto tracer( ImDrawList* canvas, ImVec2 box_min, ImVec2 box_max, ImU32 color, int origin, const math::vector2_t& dimensions, const math::vector2_t& offset, ImU32 outline_col = IM_COL32( 0, 0, 0, 180 ) ) -> void
{
    cache::draw_tracer( canvas, box_min, box_max, color, origin, dimensions, offset, outline_col );
}

inline auto lerp_u32( ImU32 a, ImU32 b, float t ) -> ImU32
{
    t = std::clamp( t, 0.f, 1.f );
    const int ar = ( a >> IM_COL32_R_SHIFT ) & 0xff;
    const int ag = ( a >> IM_COL32_G_SHIFT ) & 0xff;
    const int ab = ( a >> IM_COL32_B_SHIFT ) & 0xff;
    const int aa = ( a >> IM_COL32_A_SHIFT ) & 0xff;
    const int br = ( b >> IM_COL32_R_SHIFT ) & 0xff;
    const int bg = ( b >> IM_COL32_G_SHIFT ) & 0xff;
    const int bb = ( b >> IM_COL32_B_SHIFT ) & 0xff;
    const int ba = ( b >> IM_COL32_A_SHIFT ) & 0xff;
    return IM_COL32(
        ( int )( ar + ( br - ar ) * t ),
        ( int )( ag + ( bg - ag ) * t ),
        ( int )( ab + ( bb - ab ) * t ),
        ( int )( aa + ( ba - aa ) * t ) );
}

// t=0 at bar bottom (low / empty end), t=1 at bar top (full)
inline auto health_sample( ImU32 top, ImU32 mid, ImU32 bottom, float t ) -> ImU32
{
    t = std::clamp( t, 0.f, 1.f );
    if ( t < 0.5f )
        return lerp_u32( bottom, mid, t * 2.f );
    return lerp_u32( mid, top, ( t - 0.5f ) * 2.f );
}

inline auto health_bar( ImDrawList* canvas, ImVec2 box_min, ImVec2 box_max, float health, float max_health, float box_thickness, bool box_outline, float bar_width, int side, float gap, ImU32 fill_top, ImU32 fill_mid, ImU32 fill_bottom, ImU32 bg_col, ImU32 outline_col = IM_COL32( 0, 0, 0, 255 ), int slot = 0, bool draw_text = false, const char* text = nullptr, ImU32 text_col = 0, ImU32 text_outline_col = 0 ) -> void
{
    if ( max_health <= 0.f )
        return;

    box_min.x = std::round( box_min.x );
    box_min.y = std::round( box_min.y );
    box_max.x = std::round( box_max.x );
    box_max.y = std::round( box_max.y );
    if ( box_max.x <= box_min.x + 1.f )
        box_max.x = box_min.x + 2.f;
    if ( box_max.y <= box_min.y + 1.f )
        box_max.y = box_min.y + 2.f;

    const float ratio = std::clamp( health / max_health, 0.f, 1.f );
    const float outer = ( std::max )( 0.f, box_thickness - 1.f );
    const float adjust = box_outline ? 0.f : 1.f;
    const float bw = ( std::max )( 1.f, std::round( bar_width ) );
    const float g = ( std::max )( 1.f, std::round( gap ) );
    const float stack = ( float )( std::max )( 0, slot ) * ( bw + 2.f );

    ImVec2 bar_min, bar_max;
    switch ( side )
    {
    case 0:
        bar_min = ImVec2( std::round( box_min.x - outer - g - bw - stack ), std::round( box_min.y - outer + adjust ) );
        bar_max = ImVec2( std::round( box_min.x - outer - g - stack ), std::round( box_max.y + outer - adjust ) );
        break;
    case 1:
        bar_min = ImVec2( std::round( box_max.x + outer + g + stack ), std::round( box_min.y - outer + adjust ) );
        bar_max = ImVec2( std::round( box_max.x + outer + g + bw + stack ), std::round( box_max.y + outer - adjust ) );
        break;
    case 2:
        bar_min = ImVec2( std::round( box_min.x - outer + adjust ), std::round( box_min.y - outer - g - bw - stack ) );
        bar_max = ImVec2( std::round( box_max.x + outer - adjust ), std::round( box_min.y - outer - g - stack ) );
        break;
    case 3:
        bar_min = ImVec2( std::round( box_min.x - outer + adjust ), std::round( box_max.y + outer + g + stack ) );
        bar_max = ImVec2( std::round( box_max.x + outer - adjust ), std::round( box_max.y + outer + g + bw + stack ) );
        break;
    default:
        return;
    }

    const bool vertical = ( side == 0 || side == 1 );
    const float bar_len = vertical ? ( bar_max.y - bar_min.y ) : ( bar_max.x - bar_min.x );
    const float health_length = std::round( ( std::max )( 0.f, bar_len ) * ratio );
    const float fill_top_y = vertical ? ( bar_max.y - health_length ) : bar_min.y;
    const float fill_right_x = vertical ? bar_max.x : ( bar_min.x + health_length );

    const ImU32 track = bg_col ? bg_col : IM_COL32( 30, 30, 30, 200 );
    const bool aa = config.esp.aa_health;
    const ImDrawListFlags prev = cache::begin_aa( canvas, aa );
    canvas->AddRectFilled( bar_min, bar_max, track );
    if ( health_length > 0.f )
    {
        const ImU32 mid = fill_mid ? fill_mid : lerp_u32( fill_top, fill_bottom, 0.5f );

        if ( vertical )
        {
            const float fill_bot_y = bar_max.y;
            const float mid_y = fill_bot_y - ( fill_bot_y - fill_top_y ) * 0.5f;
            const ImU32 c_bot = health_sample( fill_top, mid, fill_bottom, 0.f );
            const ImU32 c_mid = health_sample( fill_top, mid, fill_bottom, ratio * 0.5f );
            const ImU32 c_top = health_sample( fill_top, mid, fill_bottom, ratio );

            canvas->AddRectFilledMultiColor(
                ImVec2( bar_min.x, mid_y ), ImVec2( bar_max.x, fill_bot_y ),
                c_mid, c_mid, c_bot, c_bot );
            if ( mid_y > fill_top_y + 0.5f )
                canvas->AddRectFilledMultiColor(
                    ImVec2( bar_min.x, fill_top_y ), ImVec2( bar_max.x, mid_y ),
                    c_top, c_top, c_mid, c_mid );
        }
        else
        {
            const float fill_left = bar_min.x;
            const float fill_right = fill_right_x;
            const float mid_x = fill_left + ( fill_right - fill_left ) * 0.5f;
            const ImU32 c0 = health_sample( fill_top, mid, fill_bottom, 0.f );
            const ImU32 c1 = health_sample( fill_top, mid, fill_bottom, ratio * 0.5f );
            const ImU32 c2 = health_sample( fill_top, mid, fill_bottom, ratio );
            canvas->AddRectFilledMultiColor(
                ImVec2( fill_left, bar_min.y ), ImVec2( mid_x, bar_max.y ),
                c0, c1, c1, c0 );
            if ( fill_right > mid_x + 0.5f )
                canvas->AddRectFilledMultiColor(
                    ImVec2( mid_x, bar_min.y ), ImVec2( fill_right, bar_max.y ),
                    c1, c2, c2, c1 );
        }
    }
    if ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff )
        cache::stroke_rect_px( canvas, bar_min.x - 1.f, bar_min.y - 1.f, bar_max.x, bar_max.y, outline_col );

    if ( draw_text && text && text[0] && health_length > 0.f )
    {
        ImFont* font = ImGui::GetFont( );
        const float fs = ( std::max )( 8.f, ( std::min )( 11.f, ImGui::GetFontSize( ) ) );
        const ImVec2 sz = font->CalcTextSizeA( fs, FLT_MAX, 0.f, text );

        ImVec2 tp{};
        if ( vertical )
        {
            float ty = fill_top_y - sz.y * 0.25f;
            ty = std::clamp( ty, bar_min.y, bar_max.y - sz.y );
            tp.y = std::floor( ty + 0.5f );
            if ( side == 0 )
                tp.x = std::floor( bar_min.x - sz.x - 2.f + 0.5f );
            else
                tp.x = std::floor( bar_max.x + 2.f + 0.5f );
        }
        else
        {
            float tx = fill_right_x - sz.x * 0.5f;
            tx = std::clamp( tx, bar_min.x, bar_max.x - sz.x );
            tp.x = std::floor( tx + 0.5f );
            if ( side == 2 )
                tp.y = std::floor( bar_min.y - sz.y - 1.f + 0.5f );
            else
                tp.y = std::floor( bar_max.y + 1.f + 0.5f );
        }

        const ImU32 tc = text_col ? text_col : IM_COL32( 255, 255, 255, 255 );
        const ImU32 toc = text_outline_col ? text_outline_col : IM_COL32( 0, 0, 0, 220 );
        cache::text_nway( canvas, font, fs, tp, tc, toc, text, ( toc >> IM_COL32_A_SHIFT ) & 0xff, 1 );
    }

    cache::end_aa( canvas, prev );
}

inline auto format_distance( float studs, char* buf, size_t buf_n ) -> void
{
    // Studs between MY HumanoidRootPart and theirs.
    if ( !std::isfinite( studs ) || studs < 0.f )
    {
        std::snprintf( buf, buf_n, "--" );
        return;
    }
    if ( studs < 10.f )
        std::snprintf( buf, buf_n, "%.1f studs", studs );
    else
        std::snprintf( buf, buf_n, "%.0f studs", studs );
}

inline auto distance( ImDrawList* canvas, ImVec2 box_min, ImVec2 box_max, float dist, ImU32 color ) -> void
{
    char buf[32];
    format_distance( dist, buf, sizeof( buf ) );
    text_bottom_stack( canvas, { { buf, color } }, box_min, box_max, true, false, -1, 0.f, 0.f );
}

inline auto project_world( const math::vector3_t& world, const math::vector2_t& dimensions, const math::matrix4_t& viewmatrix, const math::vector2_t& offset, ImVec2& out ) -> bool
{
    ( void )dimensions;
    const bool frame = proj_frame( ).ready;
    const math::vector2_t s = ( !frame && esp_bone_cache( ).ready )
        ? project_cached( world, viewmatrix )
        : project_to_draw( world, viewmatrix );
    if ( !std::isfinite( s.x ) || !std::isfinite( s.y ) )
        return false;
    if ( !frame && esp_bone_cache( ).ready )
        out = ImVec2( s.x, s.y );
    else
        out = ImVec2( s.x + offset.x, s.y + offset.y );
    return true;
}

inline auto skeleton_esp( ImDrawList* canvas, const cache::player& player, const math::vector2_t& dimensions, const math::matrix4_t& viewmatrix, ImU32 color, ImU32 outline_col, float thickness ) -> void
{
    ( void )dimensions;
    const auto& c = esp_bone_cache( );
    math::vector2_t ve = c.ready ? c.ve : math::vector2_t{};
    if ( ve.x < 16.f || ve.y < 16.f )
        ve = c.ready ? c.draw : math::vector2_t{};
    const math::vector2_t off = c.ready ? c.offset : math::vector2_t{};
    const float sx = c.ready ? c.sx : 1.f;
    const float sy = c.ready ? c.sy : 1.f;
    cache::draw_skeleton( canvas, player, ve, viewmatrix, off, sx, sy, color, outline_col, thickness );
}

inline auto part_screen_hull( const cache::player::bone& bone, const math::vector2_t& dimensions, const math::matrix4_t& viewmatrix, const math::vector2_t& offset, ImVec2* out, int& out_n ) -> bool
{
    out_n = 0;
    const cache::bone_pose_t pose = cache::read_bone_pose( bone );
    if ( !pose.valid )
        return false;

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
        if ( !project_world( world, dimensions, viewmatrix, offset, s ) )
            continue;
        pts[n++] = s;
    }
    if ( n < 3 )
        return false;

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
            {
                const int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    out_n = n;
    for ( int i = 0; i < n; ++i )
        out[i] = pts[order[i]];
    return true;
}

inline auto convex_hull_from_points( ImVec2* pts, int n, ImVec2* out, int& out_n ) -> bool
{
    out_n = 0;
    if ( n < 3 )
        return false;

    ImVec2 c{ 0.f, 0.f };
    for ( int i = 0; i < n; ++i )
    {
        c.x += pts[i].x;
        c.y += pts[i].y;
    }
    c.x /= ( float )n;
    c.y /= ( float )n;

    int order[256];
    const int use_n = ( n > 256 ) ? 256 : n;
    for ( int i = 0; i < use_n; ++i )
        order[i] = i;

    for ( int i = 0; i < use_n; ++i )
    {
        for ( int j = i + 1; j < use_n; ++j )
        {
            const float ai = std::atan2( pts[order[i]].y - c.y, pts[order[i]].x - c.x );
            const float aj = std::atan2( pts[order[j]].y - c.y, pts[order[j]].x - c.x );
            if ( aj < ai )
            {
                const int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    out_n = use_n;
    for ( int i = 0; i < use_n; ++i )
        out[i] = pts[order[i]];
    return out_n >= 3;
}

inline auto apply_transparency( ImU32 col, float transparency ) -> ImU32
{
    const float t = std::clamp( transparency, 0.f, 1.f );
    const int a = ( int )( ( 1.f - t ) * 255.f );
    return ( col & ~IM_COL32_A_MASK ) | ( ( ImU32 )a << IM_COL32_A_SHIFT );
}

inline auto mesh_chams( ImDrawList* canvas, const cache::player& player, const math::vector2_t& dimensions, const math::matrix4_t& viewmatrix, ImU32 fill_col, ImU32 outline_col, float thickness ) -> void
{
    ( void )dimensions;
    const auto& c = esp_bone_cache( );
    math::vector2_t ve = c.ready ? c.ve : math::vector2_t{};
    if ( ve.x < 16.f || ve.y < 16.f )
        ve = c.ready ? c.draw : math::vector2_t{};
    const math::vector2_t off = c.ready ? c.offset : math::vector2_t{};
    const float sx = c.ready ? c.sx : 1.f;
    const float sy = c.ready ? c.sy : 1.f;
    meshchams::draw( canvas, player, ve, viewmatrix, off, sx, sy, fill_col, outline_col, thickness );
}

inline auto world_chams(
    ImDrawList* canvas,
    const std::vector<cache::world_part>& parts,
    const math::vector2_t& dimensions,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    const math::vector3_t& cam_pos,
    ImU32 fill_col,
    ImU32 outline_col,
    float thickness ) -> void
{
    if ( !canvas || parts.empty( ) )
        return;

    const ImDrawListFlags prev = cache::begin_aa( canvas, config.esp.aa_chams );
    const float t = ( std::max )( 1.f, thickness );
    const float max_d = 160.f;
    const float max_d2 = max_d * max_d;
    int drawn = 0;

    static const float k_local[8][3] = {
        { -1.f, -1.f, -1.f }, {  1.f, -1.f, -1.f },
        { -1.f,  1.f, -1.f }, {  1.f,  1.f, -1.f },
        { -1.f, -1.f,  1.f }, {  1.f, -1.f,  1.f },
        { -1.f,  1.f,  1.f }, {  1.f,  1.f,  1.f },
    };
    static const int k_edge[12][2] = {
        { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
        { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };

    for ( const auto& part : parts )
    {
        if ( drawn >= 140 )
            break;
        if ( !part.center.is_valid( ) )
            continue;
        if ( cam_pos.is_valid( ) )
        {
            const float dx = part.center.x - cam_pos.x;
            const float dy = part.center.y - cam_pos.y;
            const float dz = part.center.z - cam_pos.z;
            if ( dx * dx + dy * dy + dz * dz > max_d2 )
                continue;
        }

        ImVec2 pts[8];
        bool ok[8]{};
        int n = 0;
        for ( int i = 0; i < 8; ++i )
        {
            math::vector3_t world = part.center;
            if ( part.obb )
            {
                const math::vector3_t local{
                    k_local[i][0] * part.half.x,
                    k_local[i][1] * part.half.y,
                    k_local[i][2] * part.half.z
                };
                world = math::mat3_mul_vec3( part.rotation, local ) + part.center;
            }
            else
            {
                world = {
                    ( i & 1 ) ? part.max.x : part.min.x,
                    ( i & 2 ) ? part.max.y : part.min.y,
                    ( i & 4 ) ? part.max.z : part.min.z
                };
            }
            if ( !project_world( world, dimensions, viewmatrix, offset, pts[i] ) )
                continue;
            ok[i] = true;
            ++n;
        }
        if ( n < 3 )
            continue;

        ImVec2 hull[8];
        int hn = 0;
        ImVec2 c{ 0.f, 0.f };
        int idx[8];
        for ( int i = 0; i < 8; ++i )
        {
            if ( !ok[i] )
                continue;
            c.x += pts[i].x;
            c.y += pts[i].y;
            idx[hn++] = i;
        }
        if ( hn < 3 )
            continue;
        c.x /= ( float )hn;
        c.y /= ( float )hn;
        for ( int i = 0; i < hn; ++i )
        {
            for ( int j = i + 1; j < hn; ++j )
            {
                const float ai = std::atan2( pts[idx[i]].y - c.y, pts[idx[i]].x - c.x );
                const float aj = std::atan2( pts[idx[j]].y - c.y, pts[idx[j]].x - c.x );
                if ( aj < ai )
                    std::swap( idx[i], idx[j] );
            }
        }
        for ( int i = 0; i < hn; ++i )
            hull[i] = pts[idx[i]];

        if ( ( ( fill_col >> IM_COL32_A_SHIFT ) & 0xff ) > 8 )
            canvas->AddConvexPolyFilled( hull, hn, fill_col );
        if ( ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) > 8 )
        {
            for ( const auto& e : k_edge )
            {
                if ( !ok[e[0]] || !ok[e[1]] )
                    continue;
                canvas->AddLine( pts[e[0]], pts[e[1]], outline_col, t );
            }
        }
        ++drawn;
    }

    cache::end_aa( canvas, prev );
}

inline auto sound_esp(
    ImDrawList* canvas,
    const cache::player& player,
    const math::vector2_t& dimensions,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    ImU32 color,
    ImU32 outline_col ) -> void
{
    if ( !canvas || !player.hearing )
        return;
    if ( config.esp.sound_max_distance > 0.f && player.distance > config.esp.sound_max_distance )
        return;

    const cache::player::bone* foot = cache::find_bone( player, "LeftFoot" );
    if ( !foot )
        foot = cache::find_bone( player, "RightFoot" );
    if ( !foot )
        foot = cache::find_bone( player, "Left Leg" );
    if ( !foot )
        foot = cache::find_bone( player, "HumanoidRootPart" );
    if ( !foot )
        foot = cache::find_bone( player, "LowerTorso" );
    if ( !foot || !foot->position.is_valid( ) )
        return;

    ImVec2 s{};
    if ( !project_world( foot->position, dimensions, viewmatrix, offset, s ) )
        return;

    const float pulse = 0.55f + 0.45f * std::fabs( std::sinf( ( float )ImGui::GetTime( ) * 9.f ) );
    const float r = 5.5f + pulse * 4.f;
    const ImDrawListFlags prev = cache::begin_aa( canvas, true );
    if ( ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) > 8 )
        canvas->AddCircle( s, r + 1.8f, outline_col, 20, 2.4f );
    canvas->AddCircle( s, r, color, 20, 1.8f );
    canvas->AddCircleFilled( s, 2.2f, color, 10 );
    cache::end_aa( canvas, prev );

    const char* label = player.hear_name.empty( ) ? "sound" : player.hear_name.c_str( );
    cache::text_nway( canvas, ImGui::GetFont( ), ImGui::GetFontSize( ),
        ImVec2( s.x + 7.f, s.y - 8.f ), color, outline_col, label, true, 1 );
}

