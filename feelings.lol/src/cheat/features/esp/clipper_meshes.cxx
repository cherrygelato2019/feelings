#include <include/global.cuh>

#include <src/cheat/features/esp/clipper_meshes.cuh>
#include <src/cheat/features/esp/helpers.hxx>

#include <clipper2/clipper.h>

#include <vector>

namespace
{
    constexpr int k_clip_scale = 100;

    auto to_path64( const ImVec2* pts, int n ) -> Clipper2Lib::Path64
    {
        Clipper2Lib::Path64 path;
        path.reserve( ( size_t )n );
        for ( int i = 0; i < n; ++i )
        {
            path.emplace_back(
                ( int64_t )std::llround( pts[i].x * ( double )k_clip_scale ),
                ( int64_t )std::llround( pts[i].y * ( double )k_clip_scale ) );
        }
        return path;
    }

    auto path_to_imvec( const Clipper2Lib::Path64& path, std::vector<ImVec2>& out ) -> void
    {
        out.clear( );
        out.reserve( path.size( ) );
        const double inv = 1.0 / ( double )k_clip_scale;
        for ( const auto& p : path )
            out.emplace_back( ( float )( p.x * inv ), ( float )( p.y * inv ) );
    }

    auto build_union_paths( const cache::player& player, const math::vector2_t& dimensions, const math::matrix4_t& viewmatrix ) -> Clipper2Lib::Paths64
    {
        const math::vector2_t offset = sdk::u_visualengine::client_offset( );
        Clipper2Lib::Paths64 subjects;
        subjects.reserve( player.bones.size( ) );

        for ( const auto& bone : player.bones )
        {
            if ( bone.name.rfind( "Acc:", 0 ) == 0 || bone.name == "Handle" )
                continue;
            if ( bone.name == "HumanoidRootPart" )
                continue;
            if ( bone.name != "Head" && bone.name != "head"
                && bone.name != "UpperTorso" && bone.name != "LowerTorso"
                && bone.name != "Torso" && bone.name != "torso"
                && bone.name != "LeftUpperArm" && bone.name != "LeftLowerArm" && bone.name != "LeftHand"
                && bone.name != "RightUpperArm" && bone.name != "RightLowerArm" && bone.name != "RightHand"
                && bone.name != "LeftUpperLeg" && bone.name != "LeftLowerLeg" && bone.name != "LeftFoot"
                && bone.name != "RightUpperLeg" && bone.name != "RightLowerLeg" && bone.name != "RightFoot"
                && bone.name != "Left Arm" && bone.name != "Right Arm"
                && bone.name != "Left Leg" && bone.name != "Right Leg"
                && bone.name != "LeftArm" && bone.name != "RightArm"
                && bone.name != "LeftLeg" && bone.name != "RightLeg" )
                continue;

            ImVec2 hull[8];
            int hull_n = 0;
            if ( !part_screen_hull( bone, dimensions, viewmatrix, offset, hull, hull_n ) )
                continue;
            if ( hull_n < 3 )
                continue;

            subjects.push_back( to_path64( hull, hull_n ) );
        }

        if ( subjects.empty( ) )
            return { };

        return Clipper2Lib::Union( subjects, Clipper2Lib::FillRule::NonZero );
    }

    auto draw_paths( ImDrawList* canvas, const Clipper2Lib::Paths64& paths, int style, ImU32 fill_col, ImU32 outline_col, float thickness ) -> void
    {
        const float t = ( std::max )( 1.f, thickness );
        std::vector<ImVec2> pts;
        pts.reserve( 64 );

        for ( const auto& path : paths )
        {
            if ( path.size( ) < 3 )
                continue;

            path_to_imvec( path, pts );
            if ( pts.size( ) < 3 )
                continue;

            if ( style == 0 || style == 2 )
                canvas->AddConcavePolyFilled( pts.data( ), ( int )pts.size( ), fill_col );

            if ( style == 1 || style == 2 || style == 3 )
                canvas->AddPolyline( pts.data( ), ( int )pts.size( ), outline_col, ImDrawFlags_Closed, t );
        }

        if ( style != 2 )
            return;

        ImU32 soft = fill_col;
        const int a = ( int )( ( ( soft >> IM_COL32_A_SHIFT ) & 0xff ) * 0.35f );
        soft = ( soft & ~IM_COL32_A_MASK ) | ( ( ImU32 )a << IM_COL32_A_SHIFT );

        for ( int expand = 1; expand <= 2; ++expand )
        {
            const double delta = ( double )( expand * 2 * k_clip_scale );
            const Clipper2Lib::Paths64 glow = Clipper2Lib::InflatePaths(
                paths,
                delta,
                Clipper2Lib::JoinType::Round,
                Clipper2Lib::EndType::Polygon );

            for ( const auto& path : glow )
            {
                if ( path.size( ) < 3 )
                    continue;
                path_to_imvec( path, pts );
                if ( pts.size( ) < 3 )
                    continue;
                canvas->AddPolyline( pts.data( ), ( int )pts.size( ), soft, ImDrawFlags_Closed, 2.f );
            }
        }
    }
}

auto clipper_mesh_chams(
    ImDrawList* canvas,
    const cache::player& player,
    const math::vector2_t& dimensions,
    const math::matrix4_t& viewmatrix,
    int style,
    ImU32 fill_col,
    ImU32 outline_col,
    float thickness ) -> void
{
    if ( !canvas || player.bones.empty( ) )
        return;

    const Clipper2Lib::Paths64 solution = build_union_paths( player, dimensions, viewmatrix );
    if ( solution.empty( ) )
        return;

    const ImDrawListFlags aa_prev = cache::begin_aa( canvas, config.esp.aa_chams );
    draw_paths( canvas, solution, style, fill_col, outline_col, thickness );
    cache::end_aa( canvas, aa_prev );
}
