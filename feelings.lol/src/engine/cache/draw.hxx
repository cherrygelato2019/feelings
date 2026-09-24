#pragma once

#include <windows.h>

#include <src/cheat/config/config.cuh>
#include <src/engine/cache/cache.cuh>
#include <src/engine/math/math.cuh>
#include <src/engine/sdk/visualengine/visualengine.cuh>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace cache
{

struct bone_pose_t
{
    math::matrix3_t rotation{};
    math::vector3_t position{};
    math::vector3_t size{};
    bool valid = false;
};

inline auto read_bone_pose( const player::bone& b ) -> bone_pose_t
{
    // Bone CACHE only — transform thread owns RPM. No mid-draw reads.
    bone_pose_t out{};
    out.rotation = b.rotation;
    out.position = b.position;
    out.size = b.size;
    out.valid = out.position.is_valid( )
        && ( std::fabs( out.position.x ) + std::fabs( out.position.y ) + std::fabs( out.position.z ) ) > 0.01f;
    if ( out.valid )
    {
        if ( !( out.size.x > 0.05f && out.size.y > 0.05f && out.size.z > 0.05f )
            || out.size.x > 20.f || out.size.y > 20.f || out.size.z > 20.f )
            out.size = { 1.f, 1.f, 1.f };
    }
    return out;
}

inline auto bone_world( const player::bone& b ) -> math::vector3_t
{
    return b.position;
}

inline auto px( float v ) -> float
{
    return std::floor( v + 0.5f );
}

inline auto px2( ImVec2 v ) -> ImVec2
{
    return ImVec2( px( v.x ), px( v.y ) );
}

inline auto begin_pixel( ImDrawList* canvas ) -> ImDrawListFlags
{
    const ImDrawListFlags prev = canvas->Flags;
    canvas->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill );
    return prev;
}

inline auto end_pixel( ImDrawList* canvas, ImDrawListFlags prev ) -> void
{
    canvas->Flags = prev;
}

// Toggle ImGui AA for a visual element from config.esp.aa_*.
inline auto begin_aa( ImDrawList* canvas, bool enabled ) -> ImDrawListFlags
{
    const ImDrawListFlags prev = canvas->Flags;
    if ( enabled )
        canvas->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill;
    else
        canvas->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill );
    return prev;
}

inline auto end_aa( ImDrawList* canvas, ImDrawListFlags prev ) -> void
{
    canvas->Flags = prev;
}

// CS-style 1px axis-aligned stroke via filled strips (no line AA / soft thickness)
inline auto stroke_rect_px( ImDrawList* canvas, float x1, float y1, float x2, float y2, ImU32 col ) -> void
{
    if ( !canvas || x2 < x1 || y2 < y1 )
        return;
    x1 = px( x1 ); y1 = px( y1 ); x2 = px( x2 ); y2 = px( y2 );
    if ( x2 < x1 || y2 < y1 )
        return;

    // top / bottom (inclusive ends)
    canvas->AddRectFilled( ImVec2( x1, y1 ), ImVec2( x2 + 1.f, y1 + 1.f ), col );
    canvas->AddRectFilled( ImVec2( x1, y2 ), ImVec2( x2 + 1.f, y2 + 1.f ), col );
    // left / right (skip corners already covered)
    if ( y2 > y1 + 1.f )
    {
        canvas->AddRectFilled( ImVec2( x1, y1 + 1.f ), ImVec2( x1 + 1.f, y2 ), col );
        canvas->AddRectFilled( ImVec2( x2, y1 + 1.f ), ImVec2( x2 + 1.f, y2 ), col );
    }
}

inline auto stroke_h_px( ImDrawList* canvas, float x1, float x2, float y, ImU32 col ) -> void
{
    if ( !canvas )
        return;
    x1 = px( x1 ); x2 = px( x2 ); y = px( y );
    if ( x2 < x1 )
        std::swap( x1, x2 );
    canvas->AddRectFilled( ImVec2( x1, y ), ImVec2( x2 + 1.f, y + 1.f ), col );
}

inline auto stroke_v_px( ImDrawList* canvas, float x, float y1, float y2, ImU32 col ) -> void
{
    if ( !canvas )
        return;
    x = px( x ); y1 = px( y1 ); y2 = px( y2 );
    if ( y2 < y1 )
        std::swap( y1, y2 );
    canvas->AddRectFilled( ImVec2( x, y1 ), ImVec2( x + 1.f, y2 + 1.f ), col );
}

inline auto line_px( ImDrawList* canvas, ImVec2 a, ImVec2 b, ImU32 color, float thickness = 1.f ) -> void
{
    ( void )thickness;
    a = px2( a );
    b = px2( b );
    if ( a.x == b.x )
        stroke_v_px( canvas, a.x, a.y, b.y, color );
    else if ( a.y == b.y )
        stroke_h_px( canvas, a.x, b.x, a.y, color );
    else
    {
        // diagonal fallback — still aliased, 1px
        const ImDrawListFlags prev = begin_pixel( canvas );
        canvas->AddLine( a, b, color, 1.f );
        end_pixel( canvas, prev );
    }
}

inline auto outline_offsets( int style ) -> const ImVec2*
{
    ( void )style;
    static const ImVec2 k8[8] = {
        {  1.f,  0.f }, { -1.f,  0.f }, {  0.f,  1.f }, {  0.f, -1.f },
        {  1.f,  1.f }, {  1.f, -1.f }, { -1.f,  1.f }, { -1.f, -1.f },
    };
    return k8;
}

inline auto outline_count( int style ) -> int
{
    ( void )style;
    return 8;
}

inline auto stroke_rect_8way( ImDrawList* canvas, float x1, float y1, float x2, float y2, ImU32 col ) -> void
{
    const ImVec2* dirs = outline_offsets( 1 );
    for ( int i = 0; i < 8; ++i )
        stroke_rect_px( canvas, x1 + dirs[i].x, y1 + dirs[i].y, x2 + dirs[i].x, y2 + dirs[i].y, col );
}

inline auto line_nway( ImDrawList* canvas, ImVec2 a, ImVec2 b, ImU32 color, ImU32 outline_col, float thickness = 1.f, int style = 1 ) -> void
{
    if ( !canvas )
        return;
    const bool aa = ( canvas->Flags & ImDrawListFlags_AntiAliasedLines ) != 0;
    const float t = ( std::max )( 1.f, thickness );
    if ( aa )
    {
        if ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff )
        {
            const ImVec2* dirs = outline_offsets( style );
            const int n = outline_count( style );
            for ( int i = 0; i < n; ++i )
                canvas->AddLine( ImVec2( a.x + dirs[i].x, a.y + dirs[i].y ), ImVec2( b.x + dirs[i].x, b.y + dirs[i].y ), outline_col, t );
        }
        canvas->AddLine( a, b, color, t );
        return;
    }

    a = px2( a );
    b = px2( b );
    const ImDrawListFlags prev = begin_pixel( canvas );
    if ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff )
    {
        const ImVec2* dirs = outline_offsets( style );
        const int n = outline_count( style );
        for ( int i = 0; i < n; ++i )
            line_px( canvas, ImVec2( a.x + dirs[i].x, a.y + dirs[i].y ), ImVec2( b.x + dirs[i].x, b.y + dirs[i].y ), outline_col, 1.f );
    }
    line_px( canvas, a, b, color, 1.f );
    end_pixel( canvas, prev );
}

inline auto line_outlined_px( ImDrawList* canvas, ImVec2 a, ImVec2 b, ImU32 color, ImU32 outline_col, float thickness = 1.f ) -> void
{
    line_nway( canvas, a, b, color, outline_col, thickness, config.esp.text_outline_style );
}

inline auto line_8way( ImDrawList* canvas, ImVec2 a, ImVec2 b, ImU32 color, ImU32 outline_col, float thickness, float outline_px = 1.f ) -> void
{
    line_nway( canvas, a, b, color, outline_col, thickness, config.esp.text_outline_style );
    ( void )outline_px;
}

inline auto line_8way_outline( ImDrawList* canvas, ImVec2 a, ImVec2 b, ImU32 outline_col, float thickness, float outline_px = 1.f ) -> void
{
    ( void )thickness;
    ( void )outline_px;
    a = px2( a );
    b = px2( b );
    const ImDrawListFlags prev = begin_pixel( canvas );
    const ImVec2* dirs = outline_offsets( 1 );
    for ( int i = 0; i < 8; ++i )
        line_px( canvas, ImVec2( a.x + dirs[i].x, a.y + dirs[i].y ), ImVec2( b.x + dirs[i].x, b.y + dirs[i].y ), outline_col, 1.f );
    end_pixel( canvas, prev );
}

inline auto polyline_nway( ImDrawList* canvas, const ImVec2* pts, int n, ImU32 color, ImU32 outline_col, float thickness = 1.f, int style = 1 ) -> void
{
    if ( !pts || n < 2 || !canvas )
        return;
    ( void )thickness;
    ( void )style;
    const ImDrawListFlags prev = begin_pixel( canvas );
    if ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff )
    {
        const ImVec2* dirs = outline_offsets( 1 );
        for ( int i = 0; i < 8; ++i )
        {
            ImVec2 shifted[ 32 ];
            const int use = ( n < 32 ) ? n : 32;
            for ( int k = 0; k < use; ++k )
                shifted[k] = px2( ImVec2( pts[k].x + dirs[i].x, pts[k].y + dirs[i].y ) );
            for ( int k = 0; k + 1 < use; ++k )
                line_px( canvas, shifted[k], shifted[k + 1], outline_col, 1.f );
        }
    }
    const int use = ( n < 32 ) ? n : 32;
    for ( int k = 0; k + 1 < use; ++k )
        line_px( canvas, px2( pts[k] ), px2( pts[k + 1] ), color, 1.f );
    end_pixel( canvas, prev );
}

inline auto draw_skeleton_star( ImDrawList* canvas, ImVec2 c, float r, ImU32 color, float w ) -> void
{
    const float d = r * 0.72f;
    canvas->AddLine( ImVec2( c.x, c.y - r ), ImVec2( c.x, c.y + r ), color, w );
    canvas->AddLine( ImVec2( c.x - r, c.y ), ImVec2( c.x + r, c.y ), color, w );
    canvas->AddLine( ImVec2( c.x - d, c.y - d ), ImVec2( c.x + d, c.y + d ), color, w * 0.75f );
    canvas->AddLine( ImVec2( c.x - d, c.y + d ), ImVec2( c.x + d, c.y - d ), color, w * 0.75f );
    canvas->AddCircleFilled( c, ( std::max )( 1.4f, r * 0.22f ), color, 10 );
}

inline auto draw_skeleton_figure(
    ImDrawList* canvas,
    ImVec2 chains[][6],
    const int* chain_len,
    int chain_n,
    ImVec2 head,
    float head_r,
    bool has_head,
    ImU32 color,
    ImU32 outline_col,
    float thickness ) -> void
{
    if ( !canvas || ( chain_n < 1 && !has_head ) )
        return;

    const float t = std::clamp( thickness, 1.1f, 3.2f );
    const bool outlined = ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) != 0;
    const ImDrawListFlags prev = begin_aa( canvas, true );
    canvas->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;

    auto bones = [&]( ImU32 col, float w )
    {
        for ( int i = 0; i < chain_n; ++i )
        {
            const int n = chain_len[i];
            if ( n < 2 )
                continue;
            canvas->AddPolyline( chains[i], n, col, ImDrawFlags_None, w );
        }
    };

    if ( outlined )
    {
        bones( outline_col, t + 1.8f );
        if ( has_head && head_r > 1.f && config.esp.skeleton_head_dot )
            draw_skeleton_star( canvas, head, head_r + 1.2f, outline_col, t + 1.4f );
    }

    bones( color, t );
    if ( has_head && head_r > 1.f && config.esp.skeleton_head_dot )
        draw_skeleton_star( canvas, head, head_r, color, t );

    end_aa( canvas, prev );
}

inline auto text_nway( ImDrawList* canvas, ImFont* font, float font_size, ImVec2 pos, ImU32 color, ImU32 outline_col, const char* str, bool outline, int style = 1 ) -> void
{
    if ( !canvas || !str || !str[0] )
        return;
    if ( !font )
        font = ImGui::GetFont( );
    if ( font_size < 1.f )
        font_size = ImGui::GetFontSize( );
    if ( font_size < 1.f )
        font_size = 13.f;

    if ( ( ( color >> IM_COL32_A_SHIFT ) & 0xff ) == 0 )
        color = ( color & ~IM_COL32_A_MASK ) | ( 255u << IM_COL32_A_SHIFT );

    pos = px2( pos );
    if ( outline && ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) )
    {
        const ImVec2* dirs = outline_offsets( style );
        const int n = outline_count( style );
        for ( int i = 0; i < n; ++i )
            canvas->AddText( font, font_size, ImVec2( pos.x + dirs[i].x, pos.y + dirs[i].y ), outline_col, str );
    }
    canvas->AddText( font, font_size, pos, color, str );
}

inline auto text_8way( ImDrawList* canvas, ImFont* font, float font_size, ImVec2 pos, ImU32 color, ImU32 outline_col, const char* str, bool outline ) -> void
{
    text_nway( canvas, font, font_size, pos, color, outline_col, str, outline, 1 );
}

inline auto rect_nway( ImDrawList* canvas, ImVec2 min, ImVec2 max, ImU32 color, ImU32 outline_col, bool outline, float thickness = 1.f, int style = 1 ) -> void
{
    ( void )style;
    min = px2( min );
    max = px2( max );
    if ( max.x <= min.x + 1.f )
        max.x = min.x + 2.f;
    if ( max.y <= min.y + 1.f )
        max.y = min.y + 2.f;

    const int layers = ( std::max )( 1, ( int )std::lround( thickness ) );
    const ImDrawListFlags prev = begin_pixel( canvas );
    if ( outline && ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) )
        stroke_rect_8way( canvas, min.x, min.y, max.x, max.y, outline_col );
    for ( int i = 0; i < layers; ++i )
        stroke_rect_px( canvas, min.x + ( float )i, min.y + ( float )i, max.x - ( float )i, max.y - ( float )i, color );
    end_pixel( canvas, prev );
}

inline auto rect_outlined_px( ImDrawList* canvas, ImVec2 min, ImVec2 max, ImU32 color, ImU32 outline_col, bool outline ) -> void
{
    rect_nway( canvas, min, max, color, outline_col, outline, 1.f, 1 );
}

inline auto find_bone( const player& p, const char* name ) -> const player::bone*
{
    for ( const auto& b : p.bones )
    {
        if ( b.name == name )
            return &b;
    }
    return nullptr;
}

inline auto project_world_pt(
    const math::vector3_t& world,
    const math::vector2_t& ve_dims,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    float sx,
    float sy,
    ImVec2& out ) -> bool
{
    if ( !world.is_valid( ) )
        return false;
    math::vector2_t s = math::world_to_screen( world, viewmatrix, ve_dims );
    if ( !std::isfinite( s.x ) || !std::isfinite( s.y ) )
        return false;
    out = ImVec2( s.x * sx + offset.x, s.y * sy + offset.y );
    return true;
}

inline auto project_bone(
    const player::bone& bone,
    const math::vector2_t& ve_dims,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    float sx,
    float sy,
    ImVec2& out,
    const math::vector3_t& world_bias = {} ) -> bool
{
    math::vector3_t world{};
    if ( bone.part )
    {
        const bone_pose_t pose = read_bone_pose( bone );
        world = pose.valid ? pose.position : bone_world( bone );
    }
    else
        world = bone_world( bone );
    if ( !world.is_valid( ) )
        return false;

    world.x += world_bias.x;
    world.y += world_bias.y;
    world.z += world_bias.z;
    return project_world_pt( world, ve_dims, viewmatrix, offset, sx, sy, out );
}

inline auto project_bone_local(
    const player::bone& bone,
    float lx,
    float ly,
    float lz,
    const math::vector2_t& ve_dims,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    float sx,
    float sy,
    ImVec2& out ) -> bool
{
    const bone_pose_t pose = read_bone_pose( bone );
    if ( !pose.valid )
        return project_bone( bone, ve_dims, viewmatrix, offset, sx, sy, out );
    const math::vector3_t world = math::mat3_mul_vec3( pose.rotation, { lx, ly, lz } ) + pose.position;
    return project_world_pt( world, ve_dims, viewmatrix, offset, sx, sy, out );
}

inline auto draw_skeleton( ImDrawList* canvas, const player& p, const math::vector2_t& ve_dims, const math::matrix4_t& viewmatrix, const math::vector2_t& offset, float sx, float sy, ImU32 color, ImU32 outline_col, float thickness ) -> void
{
    const float t = std::clamp( thickness, 0.75f, 4.f );

    enum joint : int
    {
        j_head = 0,
        j_neck,
        j_hip,
        j_l_ua, j_l_la, j_l_hand,
        j_r_ua, j_r_la, j_r_hand,
        j_l_ul, j_l_ll, j_l_foot,
        j_r_ul, j_r_ll, j_r_foot,
        j_torso,
        j_l_arm, j_r_arm, j_l_leg, j_r_leg,
        j_count
    };

    ImVec2 pos[j_count]{};
    bool ok[j_count]{};

    auto take = [&]( joint id, const char* name )
    {
        if ( ok[id] )
            return;
        const player::bone* b = find_bone( p, name );
        if ( !b )
            return;
        ImVec2 s;
        if ( project_bone( *b, ve_dims, viewmatrix, offset, sx, sy, s ) )
        {
            pos[id] = s;
            ok[id] = true;
        }
    };

    auto take_local = [&]( joint id, const char* name, float nx, float ny, float nz ) -> bool
    {
        const player::bone* b = find_bone( p, name );
        if ( !b )
            return false;
        const bone_pose_t pose = read_bone_pose( *b );
        if ( !pose.valid )
            return false;
        const float hx = pose.size.x * 0.5f;
        const float hy = pose.size.y * 0.5f;
        const float hz = pose.size.z * 0.5f;
        ImVec2 s;
        if ( !project_bone_local( *b, nx * hx, ny * hy, nz * hz, ve_dims, viewmatrix, offset, sx, sy, s ) )
            return false;
        pos[id] = s;
        ok[id] = true;
        return true;
    };

    take( j_l_ua, "LeftUpperArm" );
    take( j_l_la, "LeftLowerArm" );
    take( j_l_hand, "LeftHand" );
    take( j_r_ua, "RightUpperArm" );
    take( j_r_la, "RightLowerArm" );
    take( j_r_hand, "RightHand" );
    take( j_l_ul, "LeftUpperLeg" );
    take( j_l_ll, "LeftLowerLeg" );
    take( j_l_foot, "LeftFoot" );
    take( j_r_ul, "RightUpperLeg" );
    take( j_r_ll, "RightLowerLeg" );
    take( j_r_foot, "RightFoot" );

    take( j_torso, "Torso" );
    take( j_l_arm, "Left Arm" );
    if ( !ok[j_l_arm] ) take( j_l_arm, "LeftArm" );
    take( j_r_arm, "Right Arm" );
    if ( !ok[j_r_arm] ) take( j_r_arm, "RightArm" );
    take( j_l_leg, "Left Leg" );
    if ( !ok[j_l_leg] ) take( j_l_leg, "LeftLeg" );
    take( j_r_leg, "Right Leg" );
    if ( !ok[j_r_leg] ) take( j_r_leg, "RightLeg" );

    if ( !take_local( j_head, "Head", 0.f, 0.55f, 0.f ) )
        take( j_head, "Head" );
    if ( !take_local( j_neck, "UpperTorso", 0.f, 1.f, 0.f ) )
        take( j_neck, "UpperTorso" );
    if ( !take_local( j_hip, "LowerTorso", 0.f, -1.f, 0.f ) )
        take( j_hip, "LowerTorso" );

    const bool use_r15 = ok[j_neck] && ( ok[j_hip] || ok[j_l_ua] || ok[j_r_ua] );

    ImVec2 chains[8][6]{};
    int chain_len[8]{};
    int chain_n = 0;

    auto push_chain = [&]( std::initializer_list<joint> joints )
    {
        if ( chain_n >= 8 )
            return;
        int n = 0;
        for ( joint id : joints )
        {
            if ( !ok[id] || n >= 6 )
                continue;
            chains[chain_n][n++] = pos[id];
        }
        if ( n >= 2 )
        {
            chain_len[chain_n] = n;
            ++chain_n;
        }
    };

    if ( use_r15 )
    {
        push_chain( { j_head, j_neck, j_hip } );
        push_chain( { j_l_hand, j_l_la, j_l_ua, j_neck, j_r_ua, j_r_la, j_r_hand } );
        push_chain( { j_l_foot, j_l_ll, j_l_ul, j_hip } );
        push_chain( { j_r_foot, j_r_ll, j_r_ul, j_hip } );
    }
    else if ( ok[j_torso] || ok[j_head] )
    {
        const joint hub = ok[j_torso] ? j_torso : j_head;
        push_chain( { j_head, hub } );
        push_chain( { j_l_arm, hub, j_r_arm } );
        push_chain( { hub, j_l_leg } );
        push_chain( { hub, j_r_leg } );
    }

    if ( chain_n < 1 )
        return;

    float head_r = 5.5f;
    if ( ok[j_head] && ok[j_neck] )
    {
        const float dx = pos[j_head].x - pos[j_neck].x;
        const float dy = pos[j_head].y - pos[j_neck].y;
        head_r = std::clamp( std::sqrt( dx * dx + dy * dy ) * 0.28f, 4.f, 10.f );
    }

    const ImU32 out_col = ( ( outline_col >> IM_COL32_A_SHIFT ) & 0xff ) ? outline_col : IM_COL32( 0, 0, 0, 0 );
    draw_skeleton_figure( canvas, chains, chain_len, chain_n,
        pos[j_head], head_r, ok[j_head], color, out_col, t );
}

inline auto draw_tracer( ImDrawList* canvas, ImVec2 box_min, ImVec2 box_max, ImU32 color, int origin, const math::vector2_t& dimensions, const math::vector2_t& offset, ImU32 outline_col = IM_COL32( 0, 0, 0, 180 ) ) -> void
{
    ImVec2 from;
    switch ( origin )
    {
    case 1:
        from = ImVec2( dimensions.x * 0.5f + offset.x, dimensions.y * 0.5f + offset.y );
        break;
    case 2:
        from = ImVec2( dimensions.x * 0.5f + offset.x, offset.y + 2.f );
        break;
    case 3:
    {
        POINT pt{ };
        GetCursorPos( &pt );
        from = ImVec2( ( float )pt.x, ( float )pt.y );
        break;
    }
    default:
        from = ImVec2( dimensions.x * 0.5f + offset.x, dimensions.y + offset.y - 2.f );
        break;
    }

    ImVec2 to( ( box_min.x + box_max.x ) * 0.5f, box_max.y );
    const bool aa = config.esp.aa_tracers;
    if ( !aa )
    {
        from = px2( from );
        to = px2( to );
    }
    const ImDrawListFlags prev = begin_aa( canvas, aa );
    line_nway( canvas, from, to, color, outline_col, 1.f, config.esp.text_outline_style );
    end_aa( canvas, prev );
}

} 
