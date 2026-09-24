#include <include/global.cuh>

#include <src/cheat/overlay/fonts/fonts.cuh>
#include <src/cheat/features/combat/combat_state.cuh>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>

auto esp_c::draw( ) -> void
{
    if ( !config.esp.enabled )
        return;

    auto players = g->cache.snapshot_players( );
    if ( !players )
        return;

    begin_proj_frame( );
    const proj_frame_t& pf = proj_frame( );
    const math::vector2_t dimensions = pf.draw.x > 1.f ? pf.draw : sdk::u_visualengine::dimensions( );
    if ( dimensions.x < 1.f || dimensions.y < 1.f )
        return;

    const math::matrix4_t viewmatrix = sdk::u_visualengine::view_matrix( );
    const math::vector2_t offset = pf.offset;

    cache_c::ve_cache_t frame{};
    frame.ready = true;
    frame.viewmatrix = viewmatrix;
    frame.ve = pf.ve;
    frame.draw = pf.draw;
    frame.offset = pf.offset;
    frame.sx = pf.sx;
    frame.sy = pf.sy;

    const bool need_bones = config.esp.skeleton || config.esp.chams || config.esp.target_strafe;
    if ( need_bones )
        begin_esp_bone_cache( *players, frame );
    else
        esp_bone_cache( ).ready = false;

    ImDrawList* canvas = ImGui::GetBackgroundDrawList( );

    ImFont* font = resolve_esp_font( config.esp.font );
    ImGuiIO& io = ImGui::GetIO( );
    const float prev_font_scale = io.FontGlobalScale;
    io.FontGlobalScale = 1.f;
    ImGui::PushFont( font, fonts::clamp_size( config.esp.font_size ) );

    std::shared_ptr<std::vector<cache::world_part>> world;
    math::vector3_t cam_pos = math::vector3_t::nan( );
    if ( config.esp.wall_check || config.esp.world_chams )
    {
        world = g->cache.snapshot_world( );
        sdk::u_camera cam( g->engine.camera.load( ) );
        cam_pos = cam ? cam.position( ) : math::vector3_t::nan( );
    }

    const uintptr_t client = g->engine.client.load( );
    const uintptr_t client_team = g->engine.client_team.load( );
    const int64_t local_uid = g->engine.client_user_id.load( );
    const std::string local_team_name{};

    const ImU32 box_color = pack4( config.esp.color );
    const ImU32 box_outline = pack4( config.esp.color_outline );
    const ImU32 fill_color = pack4( config.esp.box_fill_color );
    const ImU32 fill_color2 = pack4( config.esp.box_fill_color2 );
    const ImU32 hp_top = pack4( config.esp.health_top );
    const ImU32 hp_mid = pack4( config.esp.health_mid );
    const ImU32 hp_bot = pack4( config.esp.health_bottom );
    const ImU32 hp_bg = pack4( config.esp.health_bg );
    const ImU32 hp_outline = pack4( config.esp.health_outline );
    const ImU32 display_color = pack4( config.esp.display_color );
    const ImU32 display_outline = pack4( config.esp.display_outline );
    const ImU32 username_color = pack4( config.esp.username_color );
    const ImU32 username_outline = pack4( config.esp.username_outline );
    const ImU32 distance_color = pack4( config.esp.distance_color );
    const ImU32 distance_outline = pack4( config.esp.distance_outline );
    const ImU32 tool_color = pack4( config.esp.tool_color );
    const ImU32 tool_outline = pack4( config.esp.tool_outline );
    const ImU32 platform_color = pack4( config.esp.platform_color );
    const ImU32 platform_outline = pack4( config.esp.platform_outline );
    const ImU32 team_color = pack4( config.esp.team_color );
    const ImU32 team_outline = pack4( config.esp.team_outline );
    const ImU32 health_text_color = pack4( config.esp.health_text_color );
    const ImU32 health_text_outline = pack4( config.esp.health_text_outline );
    const ImU32 tracer_color = pack4( config.esp.tracer_color );
    const ImU32 tracer_outline = pack4( config.esp.tracer_outline );
    const ImU32 glow_color = pack4( config.esp.glow_color );

    const bool text_outline = config.esp.outline_text( );
    const bool text_shadow = config.esp.text_dropshadow;

    if ( config.esp.world_chams && world )
    {
        const ImU32 fill = apply_transparency( pack4( config.esp.chams_color ), config.esp.chams_fill_transparency );
        const ImU32 outline = apply_transparency( pack4( config.esp.chams_outline ), config.esp.chams_outline_transparency );
        world_chams( canvas, *world, dimensions, viewmatrix, offset, cam_pos,
            fill, outline, config.esp.chams_thickness );
    }

    int drawn = 0;
    int skipped_team = 0;
    int skipped_dead = 0;
    int skipped_bounds = 0;

    for ( const auto& player : *players )
    {
        if ( player.address == client )
            continue;

        if ( config.esp.team_check && player.is_ally( client_team, local_team_name ) )
        {
            ++skipped_team;
            continue;
        }

        if ( config.esp.friend_check && local_uid > 0 && player.user_id > 0 )
        {
            if ( sdk::is_friend( local_uid, player.user_id ) )
                continue;
        }

        if ( config.esp.npc_check && player.is_npc )
            continue;

        if ( player.max_health > 5.f && player.max_health < 10000.f && player.health <= 0.f )
        {
            ++skipped_dead;
            continue;
        }

        if ( config.esp.wall_check && world && cam_pos.is_valid( ) )
        {
            const math::vector3_t aim = player_aim_point( player );
            if ( aim.is_valid( ) && !wall_visible( cam_pos, aim, *world ) )
                continue;
        }

        const bounding_box_t bounds = get_bounds( player, dimensions, viewmatrix );
        if ( !bounds.valid )
            ++skipped_bounds;
        else
            ++drawn;

        if ( config.esp.tracers && bounds.valid )
        {
            const ImU32 tr_out = config.esp.outline_tracers( ) ? tracer_outline : IM_COL32( 0, 0, 0, 0 );
            tracer( canvas, bounds.min, bounds.max, tracer_color, config.esp.tracer_origin, dimensions, offset, tr_out );
        }

        if ( config.esp.target_strafe )
        {
            const uintptr_t mark = combat::aim_sticky.load( )
                ? combat::aim_sticky.load( )
                : ( combat::silent_sticky.load( ) ? combat::silent_sticky.load( ) : combat::last_target.load( ) );
            if ( mark && ( player.address == mark || player.character == mark || player.root_part == mark ) )
            {
                math::vector3_t center{};
                bool ok = false;
                if ( player.root_part )
                {
                    const uintptr_t prim = usermode->read<uintptr_t>( player.root_part + offsets::basepart::primitive );
                    if ( prim )
                    {
                        const math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                        if ( pos.is_valid( ) )
                        {
                            center = pos;
                            ok = true;
                        }
                    }
                }
                if ( !ok )
                {
                    for ( const auto& b : player.bones )
                    {
                        if ( ( b.name == "HumanoidRootPart" || b.name == "Torso" || b.name == "UpperTorso" )
                            && b.position.is_valid( ) )
                        {
                            center = b.position;
                            ok = true;
                            break;
                        }
                    }
                }
                if ( ok )
                {
                    const float r = config.player.target_strafe_radius;
                    const float h = config.player.target_strafe_height;
                    const ImU32 col = pack4( config.esp.target_strafe_color );
                    ImVec2 pts[32];
                    int n = 0;
                    for ( int i = 0; i < 32; ++i )
                    {
                        const float a = ( 6.2831853f * ( float )i ) / 32.f;
                        const math::vector3_t w{
                            center.x + std::cos( a ) * r,
                            center.y + h,
                            center.z + std::sin( a ) * r
                        };
                        ImVec2 s{};
                        if ( project_world( w, dimensions, viewmatrix, offset, s ) )
                            pts[n++] = s;
                    }
                    if ( n >= 2 )
                    {
                        const bool aa = config.esp.aa_target_strafe;
                        const ImDrawListFlags prev = cache::begin_aa( canvas, aa );
                        if ( aa )
                            canvas->AddPolyline( pts, n, col, ImDrawFlags_Closed, 1.6f );
                        else
                        {
                            for ( int i = 0; i < n; ++i )
                                pts[i] = cache::px2( pts[i] );
                            for ( int i = 0; i < n; ++i )
                                cache::line_px( canvas, pts[i], pts[( i + 1 ) % n], col, 1.f );
                        }
                        cache::end_aa( canvas, prev );
                    }
                }
            }
        }

        if ( config.esp.glow && bounds.valid )
            add_shaded_glow( canvas, bounds.min, bounds.max, glow_color, config.esp.glow_size, pack4( config.esp.glow_outline ) );

        if ( config.esp.chams )
        {
            const ImU32 fill = apply_transparency( pack4( config.esp.chams_color ), config.esp.chams_fill_transparency );
            const ImU32 outline = config.esp.outline_meshchams( )
                ? apply_transparency( pack4( config.esp.chams_outline ), config.esp.chams_outline_transparency )
                : IM_COL32( 0, 0, 0, 0 );
            mesh_chams( canvas, player, dimensions, viewmatrix,
                fill,
                outline,
                config.esp.chams_thickness );
        }

        if ( config.esp.skeleton )
        {
            const ImU32 sk_out = config.esp.outline_skeleton( )
                ? pack4( config.esp.skeleton_outline )
                : IM_COL32( 0, 0, 0, 0 );
            skeleton_esp( canvas, player, dimensions, viewmatrix,
                pack4( config.esp.skeleton_color ),
                sk_out,
                config.esp.skeleton_thickness );
        }

        if ( config.esp.sound_esp )
        {
            sound_esp( canvas, player, dimensions, viewmatrix, offset,
                pack4( config.esp.sound_color ), pack4( config.esp.sound_outline ) );
        }

        if ( config.esp.box && bounds.valid )
        {
            box( canvas, bounds.min, bounds.max, box_color,
                config.esp.box_thickness,
                config.esp.outline_box( ) || config.esp.box_outline,
                config.esp.box_filled,
                fill_color,
                config.esp.box_type,
                box_outline,
                fill_color2 );
        }
        else if ( config.esp.box_filled && bounds.valid )
        {
            const ImVec2 fill_min( bounds.min.x + 1.f, bounds.min.y + 1.f );
            const ImVec2 fill_max( bounds.max.x - 1.f, bounds.max.y - 1.f );
            if ( fill_max.x > fill_min.x && fill_max.y > fill_min.y )
                draw_box_fill( canvas, fill_min, fill_max, fill_color, fill_color2, box_fill_angle_deg( ) );
        }

        if ( bounds.valid && config.esp.health )
        {
            char hp_buf[32]{};
            const bool hp_txt = config.esp.health_text && player.max_health > 0.f;
            if ( hp_txt )
                std::snprintf( hp_buf, sizeof( hp_buf ), "%.0f", player.health );
            health_bar( canvas, bounds.min, bounds.max,
                player.health, player.max_health,
                config.esp.box_thickness,
                config.esp.outline_box( ),
                config.esp.health_width,
                config.esp.health_side,
                config.esp.health_gap,
                hp_top,
                hp_mid,
                hp_bot,
                hp_bg,
                config.esp.outline_health( ) ? hp_outline : IM_COL32( 0, 0, 0, 0 ),
                0,
                hp_txt,
                hp_buf,
                health_text_color,
                health_text_outline );
        }

        int bar_slot = config.esp.health ? 1 : 0;
        const int armor_side = config.esp.armor_side >= 0 ? config.esp.armor_side : config.esp.health_side;
        if ( bounds.valid && config.esp.armor && player.max_armor > 0.f )
        {
            char ar_buf[32]{};
            const bool ar_txt = config.esp.armor_text;
            if ( ar_txt )
                std::snprintf( ar_buf, sizeof( ar_buf ), "%.0f", player.armor );
            const int slot = ( armor_side == config.esp.health_side ) ? bar_slot : 0;
            health_bar( canvas, bounds.min, bounds.max,
                player.armor, player.max_armor,
                config.esp.box_thickness,
                config.esp.outline_box( ),
                config.esp.health_width,
                armor_side,
                config.esp.health_gap,
                pack4( config.esp.armor_top ),
                pack4( config.esp.armor_mid ),
                pack4( config.esp.armor_bottom ),
                pack4( config.esp.armor_bg ),
                config.esp.outline_health( ) ? pack4( config.esp.armor_outline ) : IM_COL32( 0, 0, 0, 0 ),
                slot,
                ar_txt,
                ar_buf,
                pack4( config.esp.armor_text_color ),
                pack4( config.esp.armor_text_outline ) );
            if ( armor_side == config.esp.health_side )
                ++bar_slot;
        }

        const int ammo_side = config.esp.ammo_side >= 0 ? config.esp.ammo_side : config.esp.health_side;
        if ( bounds.valid && config.esp.ammo && player.max_ammo > 0.f )
        {
            char am_buf[32]{};
            const bool am_txt = config.esp.ammo_text;
            if ( am_txt )
                std::snprintf( am_buf, sizeof( am_buf ), "%.0f", player.ammo );
            const int slot = ( ammo_side == config.esp.health_side ) ? bar_slot
                : ( ( ammo_side == armor_side && config.esp.armor ) ? 1 : 0 );
            health_bar( canvas, bounds.min, bounds.max,
                player.ammo, player.max_ammo,
                config.esp.box_thickness,
                config.esp.outline_box( ),
                config.esp.health_width,
                ammo_side,
                config.esp.health_gap,
                pack4( config.esp.ammo_top ),
                pack4( config.esp.ammo_mid ),
                pack4( config.esp.ammo_bottom ),
                pack4( config.esp.ammo_bg ),
                config.esp.outline_health( ) ? pack4( config.esp.ammo_outline ) : IM_COL32( 0, 0, 0, 0 ),
                slot,
                am_txt,
                am_buf,
                pack4( config.esp.ammo_text_color ),
                pack4( config.esp.ammo_text_outline ) );
        }

        esp_line_t top_lines[2];
        int top_n = 0;
        char name_buf[160];
        char user_buf[96];

        const char* user_raw = !player.name.empty( ) ? player.name.c_str( ) : nullptr;
        const char* disp = !player.display_name.empty( ) ? player.display_name.c_str( )
            : user_raw;

        const char* user = nullptr;
        if ( user_raw )
        {

            std::snprintf( user_buf, sizeof( user_buf ), "@%s", user_raw );
            user = user_buf;
        }

        const bool want_user = config.esp.username && user && user[0];
        const bool want_disp = config.esp.display_name && disp && disp[0];

        if ( want_user && want_disp )
        {

            std::snprintf( name_buf, sizeof( name_buf ), "%s / %s", disp, user );
            top_lines[top_n++] = { name_buf, display_color, display_outline };
        }
        else if ( want_user )
        {
            top_lines[top_n++] = { user, username_color, username_outline };
        }
        else if ( want_disp )
        {
            top_lines[top_n++] = { disp, display_color, display_outline };
        }

        if ( bounds.valid && top_n > 0 )
        {
            text_top_stack( canvas, top_lines, top_n, bounds.min, bounds.max,
                text_outline, text_shadow,
                config.esp.health_side,
                config.esp.health_gap,
                config.esp.health_width );
        }

        esp_line_t bottom_lines[5];
        int bottom_n = 0;
        char dist_buf[32];

        if ( config.esp.tool && !player.tool.empty( ) )
            bottom_lines[bottom_n++] = { player.tool.c_str( ), tool_color, tool_outline };

        if ( config.esp.distance )
        {
            format_distance( player.distance, dist_buf, sizeof( dist_buf ) );
            bottom_lines[bottom_n++] = { dist_buf, distance_color, distance_outline };
        }

        if ( config.esp.platform && !player.platform.empty( ) )
            bottom_lines[bottom_n++] = { player.platform.c_str( ), platform_color, platform_outline };

        if ( config.esp.team && !player.team_name.empty( ) )
            bottom_lines[bottom_n++] = { player.team_name.c_str( ), team_color, team_outline };

        if ( bounds.valid && bottom_n > 0 )
        {
            text_bottom_stack( canvas, bottom_lines, bottom_n, bounds.min, bounds.max,
                text_outline, text_shadow,
                config.esp.health_side,
                config.esp.health_gap,
                config.esp.health_width );
        }
    }

    ImGui::PopFont( );
    io.FontGlobalScale = prev_font_scale;
    ( void )drawn;
    ( void )skipped_team;
    ( void )skipped_dead;
    ( void )skipped_bounds;
}
