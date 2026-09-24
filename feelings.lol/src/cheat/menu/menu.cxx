#include <include/global.cuh>

#include <src/cheat/overlay/fonts/fonts.cuh>
#include <src/cheat/features/music/feelings_fm.cuh>
#include <src/cheat/features/world/world_lighting.cuh>
#include <src/cheat/config/config_io.hxx>
#include <src/cheat/features/notifications/notifications.cuh>
#include <src/cheat/features/hitsounds/hitsounds.cuh>
#include <src/cheat/features/players/players_list.cuh>
#include <src/engine/cache/games/games.cuh>

#include <addons/imgui_addons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <d3d11.h>
#include <wincodec.h>

#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#pragma comment( lib, "windowscodecs.lib" )

extern "C" unsigned char esppreview[432219];
static const int ESP_PREVIEW_PNG_SIZE = 432219;

namespace
{
    static bool g_show_keybinds = true;
    static int g_menu_mode = ImAdd::KeybindMode_Toggle;
    static int g_esp_box_type = 0;
    static float g_menu_open_anim = 0.f;
    static float g_content_fade = 1.f;
    static int g_content_fade_tab = -1;
    static int g_content_fade_section = -1;

    static ID3D11ShaderResourceView* g_esp_preview_srv = nullptr;
    static int g_esp_preview_w = 0;
    static int g_esp_preview_h = 0;
    static int g_esp_content_x0 = 0;
    static int g_esp_content_y0 = 0;
    static int g_esp_content_x1 = 0;
    static int g_esp_content_y1 = 0;

    static void sync_flags_from_config( )
    {
        g_esp_box_type = config.esp.box_type;
        config.esp.box_outline = config.esp.outline_box( );
        config.esp.text_outline = config.esp.outline_text( );
        config.esp.text_outline_style = 1;
    }

    static void sync_flags_to_config( )
    {
        config.esp.box_type = g_esp_box_type;
        config.esp.box_outline = config.esp.outline_box( );
        config.esp.text_outline = config.esp.outline_text( );
    }

    static void draw_accent_glow( const ImVec2& rmin, const ImVec2& rmax, const ImVec4& accent, float thickness )
    {
        float pulse = 1.f;
        if ( config.theme.animations )
            pulse = 0.70f + 0.30f * ( 0.5f * ( sinf( ( float )ImGui::GetTime( ) * 2.1f ) + 1.0f ) );
        ImVec4 c = accent;
        c.w = 0.22f * pulse;
        add_shaded_glow( ImGui::GetBackgroundDrawList( ), rmin, rmax, ImGui::GetColorU32( c ), thickness );
    }

    static void draw_corner_box( ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float thickness )
    {

        box( dl, a, b, col, thickness, true, false, 0, 1, IM_COL32( 0, 0, 0, 220 ), 0 );
    }

    static float get_esp_preview_width( float menu_h )
    {
        const float w = 240.0f * ( menu_h / 516.0f );
        return ImClamp( w, 168.0f, 320.0f );
    }

    static void get_pc_user_name( char* out, DWORD out_n )
    {
        DWORD n = out_n;
        if ( GetUserNameA( out, &n ) && out[0] )
            return;
        n = out_n;
        if ( GetComputerNameA( out, &n ) && out[0] )
            return;
        strncpy_s( out, out_n, "pc", _TRUNCATE );
    }

    static void world_status_chip( const char* label, bool ok, const ImVec4& accent )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList( );
        const ImVec2 p = ImGui::GetCursorScreenPos( );
        const ImVec2 ts = ImGui::CalcTextSize( label );
        const float pad_x = 8.f;
        const float pad_y = 3.f;
        const ImVec2 a = p;
        const ImVec2 b{ p.x + ts.x + pad_x * 2.f, p.y + ts.y + pad_y * 2.f };
        const ImU32 fill = ok
            ? ImGui::GetColorU32( ImVec4( accent.x, accent.y, accent.z, 0.22f ) )
            : ImGui::GetColorU32( ImVec4( 0.55f, 0.18f, 0.18f, 0.35f ) );
        const ImU32 border = ok
            ? ImGui::GetColorU32( ImVec4( accent.x, accent.y, accent.z, 0.85f ) )
            : ImGui::GetColorU32( ImVec4( 0.85f, 0.35f, 0.35f, 0.9f ) );
        const ImU32 text = ok
            ? ImGui::GetColorU32( accent )
            : ImGui::GetColorU32( ImVec4( 0.95f, 0.7f, 0.7f, 1.f ) );
        dl->AddRectFilled( a, b, fill, 0.f );
        dl->AddRect( a, b, border, 0.f, 0, 1.2f );
        dl->AddText( ImVec2( p.x + pad_x, p.y + pad_y ), text, label );
        ImGui::Dummy( ImVec2( b.x - a.x, b.y - a.y ) );
    }

    static void world_color_strip( const char* /*label*/, const float* c0, const float* c1 )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList( );
        const ImVec2 p = ImGui::GetCursorScreenPos( );
        const float w = ImGui::GetContentRegionAvail( ).x;
        const float h = 14.f;
        const ImVec2 a = p;
        const ImVec2 b{ p.x + w, p.y + h };
        const ImU32 left = ImGui::ColorConvertFloat4ToU32( ImVec4( c0[0], c0[1], c0[2], 1.f ) );
        const ImU32 right = ImGui::ColorConvertFloat4ToU32( ImVec4( c1[0], c1[1], c1[2], 1.f ) );
        dl->AddRectFilledMultiColor( a, b, left, right, right, left );
        dl->AddRect( a, b, ImGui::GetColorU32( ImVec4( 1.f, 1.f, 1.f, 0.18f ) ), 0.f, 0, 1.f );
        ImGui::Dummy( ImVec2( w, h + 2.f ) );
    }

    static void world_preset_row( )
    {
        const float gap = ImGui::GetStyle( ).ItemSpacing.x;
        const float w = ( ImGui::GetContentRegionAvail( ).x - gap * 3.f ) * 0.25f;
        if ( ImAdd::Button( "fullbright", ImVec2( w, 0 ) ) )
            world_lighting::apply_preset( 0 );
        ImGui::SameLine( 0.f, gap );
        if ( ImAdd::Button( "night", ImVec2( w, 0 ) ) )
            world_lighting::apply_preset( 1 );
        ImGui::SameLine( 0.f, gap );
        if ( ImAdd::Button( "sunset", ImVec2( w, 0 ) ) )
            world_lighting::apply_preset( 2 );
        ImGui::SameLine( 0.f, gap );
        if ( ImAdd::Button( "foggy", ImVec2( w, 0 ) ) )
            world_lighting::apply_preset( 3 );
    }

    static void draw_watermark( const ImVec4& accent )
    {
        char line[224];
        size_t pc = 0;
        if ( auto pl = g->cache.snapshot_players() )
            pc = pl->size( );
        ImFormatString( line, IM_ARRAYSIZE( line ),
            "Feelings.lol | %s | place %lld | players %zu | %s",
            games::name( games::current( ) ),
            ( long long )games::current_place( ),
            pc,
            "local" );

        ImGuiStyle& style = ImGui::GetStyle( );
        static ImVec2 watermark_pos = ImVec2( 10.0f, 10.0f );
        ImGui::SetNextWindowPos( watermark_pos, ImGuiCond_Once );
        ImGui::SetNextWindowBgAlpha( 0.80f );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0, 0, 0, 0.80f ) );
        ImGui::PushStyleColor( ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab] );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.5f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10.0f, 6.0f ) );
        ImGui::Begin( "watermark", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize );

        ImGui::PushStyleColor( ImGuiCol_Text, accent );
        ImGui::TextUnformatted( line );
        ImGui::PopStyleColor( );

        if ( ImGui::IsWindowHovered( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
        {
            watermark_pos.x += ImGui::GetIO( ).MouseDelta.x;
            watermark_pos.y += ImGui::GetIO( ).MouseDelta.y;
            ImGui::SetWindowPos( watermark_pos );
        }

        const ImVec2 wm_min = ImGui::GetWindowPos( );
        const ImVec2 wm_sz = ImGui::GetWindowSize( );
        const ImVec2 wm_max = ImVec2( wm_min.x + wm_sz.x, wm_min.y + wm_sz.y );
        ImGui::End( );
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor( 2 );
        draw_accent_glow( wm_min, wm_max, accent, 36.0f );
    }

    static const char* BindModeName( int mode )
    {
        if ( mode == ImAdd::KeybindMode_Toggle ) return "Toggle";
        if ( mode == ImAdd::KeybindMode_Always ) return "Always";
        return "Hold";
    }

    static void format_bind_key( int k, char* buf, int buf_n )
    {
        if ( k <= 0 )
        {
            strcpy_s( buf, buf_n, "-" );
            return;
        }
        if ( k >= 0 && k < ( int )IM_ARRAYSIZE( szKeyNames ) )
        {
            const char* s = szKeyNames[k];
            if ( s && strncmp( s, "KEY ", 4 ) == 0 )
                s += 4;
            if ( s && _stricmp( s, "INSERT" ) == 0 ) { strcpy_s( buf, buf_n, "INS" ); return; }
            if ( s && _stricmp( s, "DELETE" ) == 0 ) { strcpy_s( buf, buf_n, "DEL" ); return; }
            if ( s && _stricmp( s, "ESCAPE" ) == 0 ) { strcpy_s( buf, buf_n, "ESC" ); return; }
            if ( s && _stricmp( s, "LBUTTON" ) == 0 ) { strcpy_s( buf, buf_n, "M1" ); return; }
            if ( s && _stricmp( s, "RBUTTON" ) == 0 ) { strcpy_s( buf, buf_n, "M2" ); return; }
            if ( s && _stricmp( s, "MBUTTON" ) == 0 ) { strcpy_s( buf, buf_n, "M3" ); return; }
            if ( s && _stricmp( s, "XBUTTON1" ) == 0 ) { strcpy_s( buf, buf_n, "M4" ); return; }
            if ( s && _stricmp( s, "XBUTTON2" ) == 0 ) { strcpy_s( buf, buf_n, "M5" ); return; }
            if ( s && s[0] && _stricmp( s, "Unknown" ) != 0 )
            {
                strncpy_s( buf, buf_n, s, _TRUNCATE );
                if ( ( int )strlen( buf ) > 4 )
                    buf[4] = 0;
                return;
            }
        }
        strcpy_s( buf, buf_n, "-" );
    }

    static void draw_keybind_list( const ImVec4& accent )
    {
        struct Slot { const char* name; bool* enabled; int* key; int* mode; };
        const Slot slots[] = {
            { "menu", &config.menu_open, &config.menu_key, &g_menu_mode },
            { "aimbot", &config.aimbot.enabled, &config.aimbot.key, &config.aimbot.key_mode },
            { "silent", &config.silentaim.enabled, &config.silentaim.key, &config.silentaim.key_mode },
            { "trigger", &config.triggerbot.enabled, &config.triggerbot.key, &config.triggerbot.key_mode },
            { "fly", &config.player.fly, &config.player.fly_key, &config.player.fly_key_mode },
            { "speed", &config.player.speed, &config.player.speed_key, &config.player.speed_key_mode },
            { "hip height", &config.player.hip_height, &config.player.hip_height_key, &config.player.hip_height_key_mode },
            { "gravity", &config.player.gravity, &config.player.gravity_key, &config.player.gravity_key_mode },
            { "jumppower", &config.player.jump, &config.player.jump_key, &config.player.jump_key_mode },
            { "inf jump", &config.player.infinite_jump, &config.player.infinite_jump_key, &config.player.infinite_jump_key_mode },
            { "spinbot", &config.player.spin, &config.player.spin_key, &config.player.spin_key_mode },
            { "noclip", &config.player.noclip, &config.player.noclip_key, &config.player.noclip_key_mode },
            { "sit", &config.player.sit, &config.player.sit_key, &config.player.sit_key_mode },
            { "target strafe", &config.player.target_strafe, &config.player.target_strafe_key, &config.player.target_strafe_key_mode },
            { "rage tp", &config.player.rage_tp, &config.player.rage_tp_key, &config.player.rage_tp_key_mode },
            { "teleport", &config.player.teleport, &config.player.teleport_key, &config.player.teleport_key_mode },
            { "fling", &config.player.fling, &config.player.fling_key, &config.player.fling_key_mode },
            { "no fall", &config.player.no_fall, &config.player.no_fall_key, &config.player.no_fall_key_mode },
            { "antiaim", &config.player.antiaim, &config.player.antiaim_key, &config.player.antiaim_key_mode },
            { "desync", &config.player.desync, &config.player.desync_key, &config.player.desync_key_mode },
            { "fake lag", &config.player.fake_lag, &config.player.fake_lag_key, &config.player.fake_lag_key_mode },
            { "freecam", &config.player.freecam, &config.player.freecam_key, &config.player.freecam_key_mode },
            { "antiafk", &config.player.antiafk, &config.player.antiafk_key, &config.player.antiafk_key_mode },
        };
        const int n = ( int )IM_ARRAYSIZE( slots );

        char lines[24][72];
        int vis[24];
        int vis_n = 0;
        float line_w = ImGui::CalcTextSize( "Hotkeys" ).x;
        for ( int i = 0; i < n; ++i )
        {
            if ( *slots[i].key <= 0 )
                continue;
            char key[16];
            format_bind_key( *slots[i].key, key, IM_ARRAYSIZE( key ) );
            ImFormatString( lines[vis_n], IM_ARRAYSIZE( lines[vis_n] ), "%s - %s ( %s )", slots[i].name, key, BindModeName( *slots[i].mode ) );
            line_w = ImMax( line_w, ImGui::CalcTextSize( lines[vis_n] ).x );
            vis[vis_n++] = i;
        }
        if ( vis_n == 0 )
            return;

        static ImVec2 kb_pos = ImVec2( 10.0f, 48.0f );
        ImGuiStyle& style = ImGui::GetStyle( );

        ImGui::SetNextWindowPos( kb_pos, ImGuiCond_Once );
        ImGui::SetNextWindowBgAlpha( 0.80f );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0, 0, 0, 0.80f ) );
        ImGui::PushStyleColor( ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab] );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.5f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10.0f, 6.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.0f, 4.0f ) );
        ImGui::Begin( "keybinds", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing );

        ImGui::PushStyleColor( ImGuiCol_Text, accent );
        ImGui::TextUnformatted( "Hotkeys" );
        ImGui::PopStyleColor( );

        const ImVec2 title_max = ImGui::GetItemRectMax( );
        const ImVec2 win_pos = ImGui::GetWindowPos( );
        const float win_w = ImGui::GetWindowSize( ).x;
        ImGui::GetWindowDrawList( )->AddLine(
            ImVec2( win_pos.x + 10.0f, title_max.y + 4.0f ),
            ImVec2( win_pos.x + win_w - 10.0f, title_max.y + 4.0f ),
            ImGui::GetColorU32( accent ), 1.5f );
        ImGui::Dummy( ImVec2( line_w, 7.0f ) );

        static float hl[24] = {};
        const float anim_t = ImSaturate( ImGui::GetIO( ).DeltaTime / 0.12f );
        const ImVec4 idle = ImVec4( 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f );
        for ( int i = 0; i < vis_n; ++i )
        {
            const int si = vis[i];
            hl[si] = ImLerp( hl[si], *slots[si].enabled ? 1.0f : 0.0f, anim_t );
            ImGui::PushStyleColor( ImGuiCol_Text, ImLerp( idle, accent, hl[si] ) );
            ImGui::TextUnformatted( lines[i] );
            ImGui::PopStyleColor( );
        }

        if ( ImGui::IsWindowHovered( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
        {
            kb_pos.x += ImGui::GetIO( ).MouseDelta.x;
            kb_pos.y += ImGui::GetIO( ).MouseDelta.y;
            ImGui::SetWindowPos( kb_pos );
        }

        const ImVec2 kb_min = ImGui::GetWindowPos( );
        const ImVec2 kb_sz = ImGui::GetWindowSize( );
        ImGui::End( );
        ImGui::PopStyleVar( 4 );
        ImGui::PopStyleColor( 2 );
        draw_accent_glow( kb_min, ImVec2( kb_min.x + kb_sz.x, kb_min.y + kb_sz.y ), accent, 36.0f );
    }

    static void draw_esp_preview( const ImVec4& accent, float design_w )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList( );
        const ImVec2 p = ImGui::GetWindowPos( );
        const ImVec2 s = ImGui::GetWindowSize( );
        if ( s.x < 2.0f || s.y < 2.0f || design_w < 2.0f )
            return;

        const float box_pad_x = 18.0f + ( ( config.esp.health && ( config.esp.health_side == 0 || config.esp.health_side == 1 ) )
            ? ( config.esp.health_gap + config.esp.health_width + 6.f ) : 0.f );
        const float box_pad_y = 28.0f + ( ( config.esp.health && ( config.esp.health_side == 2 || config.esp.health_side == 3 ) )
            ? ( config.esp.health_gap + config.esp.health_width + 6.f ) : 0.f );

        const float text_pad_top = 22.0f;
        const float text_pad_bot = 18.0f;
        ImVec2 box_min( p.x + box_pad_x, p.y + box_pad_y + text_pad_top );
        ImVec2 box_max( p.x + design_w - box_pad_x, p.y + s.y - box_pad_y - text_pad_bot );

        ImVec2 img_min = box_min;
        ImVec2 img_max = box_max;
        ImVec2 uv_min( 0.0f, 0.0f );
        ImVec2 uv_max( 1.0f, 1.0f );

        if ( g_esp_preview_srv && g_esp_preview_w > 0 && g_esp_preview_h > 0 )
        {
            const float tex_w = ( float )g_esp_preview_w;
            const float tex_h = ( float )g_esp_preview_h;
            const float crop_x0 = ( float )g_esp_content_x0;
            const float crop_y0 = ( float )g_esp_content_y0;
            const float crop_x1 = ( float )g_esp_content_x1;
            const float crop_y1 = ( float )g_esp_content_y1;
            const float crop_w = ImMax( 1.0f, crop_x1 - crop_x0 );
            const float crop_h = ImMax( 1.0f, crop_y1 - crop_y0 );

            uv_min = ImVec2( crop_x0 / tex_w, crop_y0 / tex_h );
            uv_max = ImVec2( crop_x1 / tex_w, crop_y1 / tex_h );

            const float inner_pad_x = 8.0f;
            const float inner_pad_y = 6.0f;
            const float avail_w = ( box_max.x - box_min.x ) - inner_pad_x * 2.0f;
            const float avail_h = ( box_max.y - box_min.y ) - inner_pad_y * 2.0f;
            if ( avail_w > 1.0f && avail_h > 1.0f )
            {
                const float scale = ImMin( avail_w / crop_w, avail_h / crop_h );
                const float dw = crop_w * scale;
                const float dh = crop_h * scale;
                const float cx = ( box_min.x + box_max.x ) * 0.5f;
                const float cy = ( box_min.y + box_max.y ) * 0.5f;
                img_min = ImVec2( cx - dw * 0.5f, cy - dh * 0.5f );
                img_max = ImVec2( cx + dw * 0.5f, cy + dh * 0.5f );
                const float inflate = 3.0f;
                box_min = ImVec2( img_min.x - inflate, img_min.y - inflate );
                box_max = ImVec2( img_max.x + inflate, img_max.y + inflate );
            }
        }

        ImFont* font = resolve_esp_font( config.esp.font );
        ImGui::PushFont( font, fonts::clamp_size( config.esp.font_size ) );

        dl->PushClipRect( p, ImVec2( p.x + s.x, p.y + s.y ), true );

        if ( config.esp.glow )
            add_shaded_glow( dl, box_min, box_max, pack4( config.esp.glow_color ), config.esp.glow_size, pack4( config.esp.glow_outline ) );

        if ( config.esp.box_filled && !config.esp.box )
        {
            draw_box_fill( dl, box_min, box_max,
                pack4( config.esp.box_fill_color ),
                pack4( config.esp.box_fill_color2 ),
                box_fill_angle_deg( ) );
        }

        if ( g_esp_preview_srv && img_max.x > img_min.x && img_max.y > img_min.y )
            dl->AddImage( ImTextureRef( ( void* )g_esp_preview_srv ), img_min, img_max, uv_min, uv_max );

        if ( config.esp.box )
        {
            box( dl, box_min, box_max, pack4( config.esp.color ),
                config.esp.box_thickness,
                config.esp.outline_box( ),
                config.esp.box_filled,
                pack4( config.esp.box_fill_color ),
                config.esp.box_type,
                pack4( config.esp.color_outline ),
                pack4( config.esp.box_fill_color2 ) );
        }

        if ( config.esp.health )
        {
            health_bar( dl, box_min, box_max,
                72.f, 100.f,
                config.esp.box_thickness,
                config.esp.outline_box( ),
                config.esp.health_width,
                config.esp.health_side,
                config.esp.health_gap,
                pack4( config.esp.health_top ),
                pack4( config.esp.health_mid ),
                pack4( config.esp.health_bottom ),
                pack4( config.esp.health_bg ),
                config.esp.outline_health( ) ? pack4( config.esp.health_outline ) : IM_COL32( 0, 0, 0, 0 ),
                0,
                config.esp.health_text,
                "72",
                pack4( config.esp.health_text_color ),
                pack4( config.esp.health_text_outline ) );
        }
        int prev_slot = config.esp.health ? 1 : 0;
        if ( config.esp.armor )
        {
            health_bar( dl, box_min, box_max,
                50.f, 100.f,
                config.esp.box_thickness,
                config.esp.outline_box( ),
                config.esp.health_width,
                config.esp.health_side,
                config.esp.health_gap,
                pack4( config.esp.armor_top ),
                pack4( config.esp.armor_mid ),
                pack4( config.esp.armor_bottom ),
                pack4( config.esp.armor_bg ),
                config.esp.outline_health( ) ? pack4( config.esp.armor_outline ) : IM_COL32( 0, 0, 0, 0 ),
                prev_slot++,
                config.esp.armor_text,
                "50",
                pack4( config.esp.armor_text_color ),
                pack4( config.esp.armor_text_outline ) );
        }
        if ( config.esp.ammo )
        {
            health_bar( dl, box_min, box_max,
                18.f, 30.f,
                config.esp.box_thickness,
                config.esp.outline_box( ),
                config.esp.health_width,
                config.esp.health_side,
                config.esp.health_gap,
                pack4( config.esp.ammo_top ),
                pack4( config.esp.ammo_mid ),
                pack4( config.esp.ammo_bottom ),
                pack4( config.esp.ammo_bg ),
                config.esp.outline_health( ) ? pack4( config.esp.ammo_outline ) : IM_COL32( 0, 0, 0, 0 ),
                prev_slot,
                config.esp.ammo_text,
                "18",
                pack4( config.esp.ammo_text_color ),
                pack4( config.esp.ammo_text_outline ) );
        }

        esp_line_t top_lines[2];
        int top_n = 0;
        if ( config.esp.username && config.esp.display_name )
            top_lines[top_n++] = { "DisplayName / @username", pack4( config.esp.display_color ), pack4( config.esp.display_outline ) };
        else if ( config.esp.username )
            top_lines[top_n++] = { "username", pack4( config.esp.username_color ), pack4( config.esp.username_outline ) };
        else if ( config.esp.display_name )
            top_lines[top_n++] = { "DisplayName", pack4( config.esp.display_color ), pack4( config.esp.display_outline ) };
        if ( top_n > 0 )
        {
            text_top_stack( dl, top_lines, top_n, box_min, box_max,
                config.esp.outline_text( ), config.esp.text_dropshadow,
                config.esp.health_side, config.esp.health_gap, config.esp.health_width );
        }

        esp_line_t bottom_lines[6];
        int bottom_n = 0;
        if ( config.esp.state )
            bottom_lines[bottom_n++] = { "Jumping", pack4( config.esp.state_color ), pack4( config.esp.state_outline ) };
        if ( config.esp.tool )
            bottom_lines[bottom_n++] = { "ClassicSword", pack4( config.esp.tool_color ), pack4( config.esp.tool_outline ) };
        if ( config.esp.distance )
            bottom_lines[bottom_n++] = { "12.4m", pack4( config.esp.distance_color ), pack4( config.esp.distance_outline ) };
        if ( config.esp.platform )
            bottom_lines[bottom_n++] = { "Xbox", pack4( config.esp.platform_color ), pack4( config.esp.platform_outline ) };
        if ( config.esp.team )
            bottom_lines[bottom_n++] = { "Bright blue", pack4( config.esp.team_color ), pack4( config.esp.team_outline ) };
        if ( bottom_n > 0 )
        {
            text_bottom_stack( dl, bottom_lines, bottom_n, box_min, box_max,
                config.esp.outline_text( ), config.esp.text_dropshadow,
                config.esp.health_side, config.esp.health_gap, config.esp.health_width );
        }

        ( void )accent;
        dl->PopClipRect( );
        ImGui::PopFont( );
    }

    static float smooth_step( float t )
    {
        if ( t < 0.0f ) t = 0.0f;
        if ( t > 1.0f ) t = 1.0f;
        return t * t * ( 3.0f - 2.0f * t );
    }

    struct menu_feature_t
    {
        const char* name;
        const char* section;
        int tab;
        int section_id;
    };

    static const menu_feature_t k_menu_features[] =
    {
        { "esp enabled", "players", 0, 0 },
        { "team check", "players", 0, 0 },
        { "friend check", "players", 0, 0 },
        { "npc check", "players", 0, 0 },
        { "wall check", "players", 0, 0 },
        { "box", "players", 0, 0 },
        { "box fill", "players", 0, 0 },
        { "box thickness", "players", 0, 0 },
        { "box type", "players", 0, 0 },
        { "outline", "players", 0, 0 },
        { "health", "players", 0, 0 },
        { "display name", "players", 0, 0 },
        { "username", "players", 0, 0 },
        { "distance", "players", 0, 0 },
        { "tool", "extras", 0, 1 },
        { "platform", "extras", 0, 1 },
        { "state", "extras", 0, 1 },
        { "health text", "extras", 0, 1 },
        { "team", "extras", 0, 1 },
        { "tracers", "extras", 0, 1 },
        { "glow", "extras", 0, 1 },
        { "skeleton", "extras", 0, 1 },
        { "head dot", "extras", 0, 1 },
        { "chams", "extras", 0, 1 },
        { "meshchams", "extras", 0, 1 },
        { "world chams", "extras", 0, 1 },
        { "sound esp", "extras", 0, 1 },
        { "health top", "customization", 0, 2 },
        { "health mid", "customization", 0, 2 },
        { "health bottom", "customization", 0, 2 },
        { "health outline", "customization", 0, 2 },
        { "target strafe ring", "extras", 0, 1 },
        { "health bg", "customization", 0, 2 },
        { "health outline", "customization", 0, 2 },
        { "health side", "customization", 0, 2 },
        { "box fill rotate", "players", 0, 0 },
        { "box fill auto", "players", 0, 0 },
        { "text dropshadow", "customization", 0, 2 },
        { "font", "customization", 0, 2 },
        { "font size", "customization", 0, 2 },
        { "aimbot", "aim assist", 1, 0 },
        { "aim method", "aim assist", 1, 0 },
        { "aim part", "aim assist", 1, 0 },
        { "target mode", "aim assist", 1, 0 },
        { "smoothing", "aim assist", 1, 0 },
        { "silent aim", "silent aim", 1, 1 },
        { "mouse hit", "silent aim", 1, 1 },
        { "method", "silent aim", 1, 1 },
        { "triggerbot", "triggerbot", 1, 2 },
        { "vsync", "settings", 5, 0 },
        { "low power", "settings", 5, 0 },
        { "auto rescan", "settings", 5, 0 },
        { "watermark", "overlay", 5, 1 },
        { "keybinds", "overlay", 5, 1 },
        { "feelings fm", "overlay", 5, 1 },
        { "menu", "overlay", 5, 1 },
        { "theme", "theme", 5, 2 },
        { "config", "config", 5, 3 },
        { "save config", "config", 5, 3 },
        { "load config", "config", 5, 3 },
        { "accent", "theme", 5, 2 },
        { "opacity", "theme", 5, 2 },
        { "ui scale", "theme", 5, 2 },
        { "animations", "theme", 5, 2 },
        { "streamproof", "settings", 5, 0 },
        { "hide console", "settings", 5, 0 },
        { "fps cap", "settings", 5, 0 },
        { "dex", "explorer", 4, 0 },
        { "walkspeed", "self", 2, 0 },
        { "jumppower", "self", 2, 0 },
        { "fly", "self", 2, 0 },
        { "noclip", "self", 2, 0 },
        { "hip height", "self", 2, 0 },
        { "gravity", "self", 2, 0 },
        { "infinite jump", "self", 2, 0 },
        { "spinbot", "self", 2, 0 },
        { "sit", "self", 2, 0 },
        { "speed", "self", 2, 0 },
        { "target strafe", "others", 2, 1 },
        { "rage tp", "others", 2, 1 },
        { "fling", "others", 2, 1 },
        { "animation changer", "client", 2, 4 },
        { "emote changer", "client", 2, 4 },
        { "face changer", "client", 2, 4 },
        { "clothing changer", "client", 2, 4 },
        { "hitbox expander", "others", 2, 1 },
        { "force state", "hvh", 2, 2 },
        { "state", "extras", 0, 1 },
        { "no jump cooldown", "self", 2, 0 },
        { "waypoints", "misc", 2, 3 },
        { "no fall", "hvh", 2, 2 },
        { "antiaim", "hvh", 2, 2 },
        { "desync", "hvh", 2, 2 },
        { "fake lag", "hvh", 2, 2 },
        { "freecam", "hvh", 2, 2 },
        { "antiafk", "hvh", 2, 2 },
        { "visuals", "tab", 0, 0 },
        { "rage", "tab", 1, 0 },
        { "players", "tab", 2, 0 },
        { "world", "tab", 3, 0 },
        { "explorer", "tab", 4, 0 },
        { "settings", "tab", 5, 0 },
    };

    static const char* k_tab_sections[][5] =
    {
        { "players", "extras", "customization", "overlay", "" },
        { "aim assist", "silent aim", "triggerbot", "", "" },
        { "self", "others", "hvh", "misc", "client" },
        { "weather", "lighting", "materials", "sky", "" },
        { "dex", "", "", "", "" },
        { "settings", "overlay", "theme", "config", "" },
    };
    static const int k_tab_section_count[] = { 4, 3, 5, 4, 1, 4 };
    static int g_selected_section[6] = {};

    static char g_feature_pulse[64] = {};
    static double g_feature_pulse_until = 0.0;

    static bool contains_i( const char* hay, const char* needle )
    {
        if ( !hay || !needle )
            return false;
        if ( !needle[0] )
            return true;
        for ( const char* h = hay; *h; ++h )
        {
            const char* a = h;
            const char* b = needle;
            while ( *a && *b )
            {
                const int ca = tolower( ( unsigned char )*a );
                const int cb = tolower( ( unsigned char )*b );
                if ( ca != cb )
                    break;
                ++a;
                ++b;
            }
            if ( !*b )
                return true;
        }
        return false;
    }

    static void pulse_feature( const char* name )
    {
        if ( g_feature_pulse_until <= ImGui::GetTime( ) )
            return;
        if ( !name || _stricmp( g_feature_pulse, name ) != 0 )
            return;
        const ImVec2 a = ImGui::GetItemRectMin( ) - ImVec2( 4.0f, 3.0f );
        const ImVec2 b = ImGui::GetItemRectMax( ) + ImVec2( 4.0f, 3.0f );
        const float pulse = 0.40f + 0.45f * ( 0.5f * ( sinf( ( float )ImGui::GetTime( ) * 9.0f ) + 1.0f ) );
        ImVec4 c = ImGui::GetStyle( ).Colors[ImGuiCol_Separator];
        c.w = pulse;
        ImGui::GetWindowDrawList( )->AddRect( a, b, ImGui::GetColorU32( c ), 0.0f, 0, 1.5f );
    }

    static char g_search_buf[64] = {};
    static ImVec2 g_search_bar_min;
    static ImVec2 g_search_bar_max;
    static int g_search_nav = 0;
    static bool g_search_open = false;

    static void jump_to_feature( int* selected_tab, const menu_feature_t& feat )
    {
        *selected_tab = feat.tab;
        if ( feat.tab >= 0 && feat.tab < 8 )
            g_selected_section[feat.tab] = feat.section_id;
        strncpy_s( g_feature_pulse, feat.name, _TRUNCATE );
        g_feature_pulse_until = ImGui::GetTime( ) + 2.6;
        g_search_buf[0] = 0;
        g_search_open = false;
    }

    static int collect_feature_matches( int* matches, int max_matches )
    {
        int count = 0;
        if ( !g_search_buf[0] )
            return 0;
        for ( int i = 0; i < ( int )IM_ARRAYSIZE( k_menu_features ) && count < max_matches; ++i )
        {
            const menu_feature_t& f = k_menu_features[i];
            if ( contains_i( f.name, g_search_buf ) || contains_i( f.section, g_search_buf ) )
                matches[count++] = i;
        }
        return count;
    }

    static void draw_feature_search_bar( int* selected_tab, float width )
    {
        ImGuiIO& io = ImGui::GetIO( );
        if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_F ) && !ImGui::IsAnyItemActive( ) )
            ImGui::SetKeyboardFocusHere( );

        ImGui::SetNextItemWidth( width );
        ImAdd::SearchBar( "##feature_search", g_search_buf, IM_ARRAYSIZE( g_search_buf ), "search features...", width );
        const bool search_active = ImGui::IsItemActive( );
        g_search_bar_min = ImGui::GetItemRectMin( );
        g_search_bar_max = ImGui::GetItemRectMax( );
        g_search_open = g_search_buf[0] != 0;

        if ( search_active && ImGui::IsKeyPressed( ImGuiKey_Escape ) )
        {
            g_search_buf[0] = 0;
            g_search_open = false;
            ImGui::ClearActiveID( );
        }

        int matches[IM_ARRAYSIZE( k_menu_features )];
        const int match_count = collect_feature_matches( matches, IM_ARRAYSIZE( matches ) );
        if ( match_count <= 0 )
            g_search_nav = 0;
        else
            g_search_nav = ImClamp( g_search_nav, 0, match_count - 1 );

        if ( search_active && match_count > 0 )
        {
            if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow ) )
                g_search_nav = ( g_search_nav + 1 ) % match_count;
            if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow ) )
                g_search_nav = ( g_search_nav - 1 + match_count ) % match_count;
            if ( ImGui::IsKeyPressed( ImGuiKey_Enter ) )
                jump_to_feature( selected_tab, k_menu_features[matches[g_search_nav]] );
        }
    }

    static void draw_feature_search_results( int* selected_tab, const char* const* tabNames, int tabCount )
    {
        if ( !g_search_open )
            return;

        int matches[IM_ARRAYSIZE( k_menu_features )];
        const int match_count = collect_feature_matches( matches, IM_ARRAYSIZE( matches ) );
        if ( match_count <= 0 )
            g_search_nav = 0;
        else
            g_search_nav = ImClamp( g_search_nav, 0, match_count - 1 );

        ImGuiStyle& style = ImGui::GetStyle( );
        const float row_h = ImGui::GetTextLineHeight( ) + style.FramePadding.y * 2.0f;
        const int shown = match_count > 0 ? ImMin( match_count, 7 ) : 1;
        const float list_h = shown * row_h + style.WindowPadding.y * 2.0f;

        ImGui::SetNextWindowPos( ImVec2( g_search_bar_min.x, g_search_bar_max.y + 4.0f ), ImGuiCond_Always );
        ImGui::SetNextWindowSize( ImVec2( g_search_bar_max.x - g_search_bar_min.x, list_h ), ImGuiCond_Always );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0, 0, 0, 0.94f ) );
        ImGui::PushStyleColor( ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab] );
        ImGui::PushStyleColor( ImGuiCol_Header, style.Colors[ImGuiCol_Header] );
        ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.70f, 0.70f, 0.70f, 0.28f ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderActive, style.Colors[ImGuiCol_Header] );
        ImGui::Begin( "##feature_search_results", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav );
        ImGui::SetWindowSize( ImVec2( g_search_bar_max.x - g_search_bar_min.x, list_h ), ImGuiCond_Always );

        if ( match_count > 0 )
        {
            for ( int n = 0; n < match_count; ++n )
            {
                const menu_feature_t& f = k_menu_features[matches[n]];
                const char* tab_name = ( f.tab >= 0 && f.tab < tabCount ) ? tabNames[f.tab] : "";
                char id[32];
                ImFormatString( id, IM_ARRAYSIZE( id ), "##feat_%d", n );

                const bool selected = ( n == g_search_nav );
                if ( ImGui::Selectable( id, selected, 0, ImVec2( 0.0f, row_h ) ) )
                    jump_to_feature( selected_tab, f );
                if ( ImGui::IsItemHovered( ) )
                    g_search_nav = n;

                ImDrawList* dl = ImGui::GetWindowDrawList( );
                const ImVec2 rmin = ImGui::GetItemRectMin( );
                const ImVec2 rmax = ImGui::GetItemRectMax( );
                const float text_y = rmin.y + ( rmax.y - rmin.y - ImGui::GetTextLineHeight( ) ) * 0.5f;
                dl->AddText( ImVec2( rmin.x + 8.0f, text_y ), ImGui::GetColorU32( selected ? ImGuiCol_Separator : ImGuiCol_Text ), f.name );

                char where[96];
                if ( f.section && f.section[0] && strcmp( f.section, "tab" ) != 0 )
                    ImFormatString( where, IM_ARRAYSIZE( where ), "%s / %s", tab_name, f.section );
                else
                    ImFormatString( where, IM_ARRAYSIZE( where ), "%s", tab_name );
                const ImVec2 where_sz = ImGui::CalcTextSize( where );
                dl->AddText( ImVec2( rmax.x - where_sz.x - 8.0f, text_y ), IM_COL32( 130, 130, 130, 255 ), where );
            }
        }

        ImGui::End( );
        ImGui::PopStyleColor( 5 );
    }

    static void draw_section_bar( const char* const* names, int count, int* selected, float height, const ImVec4& accent )
    {
        if ( count <= 0 )
            return;

        ImGuiStyle& style = ImGui::GetStyle( );
        float avail_w = ImGui::GetContentRegionAvail( ).x;
        float tab_width = ( avail_w - style.ItemSpacing.x * ( count - 1 ) ) / ( float )count;
        if ( tab_width < 1.0f )
            tab_width = 1.0f;

        static float hl[8] = {};
        const float anim_t = config.theme.animations
            ? ImSaturate( ImGui::GetIO( ).DeltaTime / 0.12f )
            : 1.f;
        const ImVec4 idle = ImVec4( 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f );

        for ( int i = 0; i < count; ++i )
        {
            hl[i] = ImLerp( hl[i], ( *selected == i ) ? 1.0f : 0.0f, anim_t );

            ImGui::PushID( i + 100 );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.7f, 0.7f, 0.7f, 0.5f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.1f, 0.1f, 0.1f, 0.5f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImLerp( idle, accent, hl[i] ) );

            const float this_width = ( i + 1 == count ) ? ImGui::GetContentRegionAvail( ).x : tab_width;
            const ImVec2 buttonSize = ImVec2( this_width, height );
            if ( ImGui::Button( names[i], buttonSize ) )
                *selected = i;

            if ( hl[i] > 0.01f )
            {
                const ImVec2 pos = ImGui::GetItemRectMin( );
                const ImVec2 br = ImGui::GetItemRectMax( );
                ImVec4 line = accent;
                line.w *= hl[i];
                ImGui::GetWindowDrawList( )->AddLine(
                    ImVec2( pos.x, br.y - 1.0f ),
                    ImVec2( pos.x + buttonSize.x, br.y - 1.0f ),
                    ImGui::GetColorU32( line ), 2.0f );
            }

            ImGui::PopStyleColor( 4 );
            if ( i + 1 < count )
                ImGui::SameLine( );
            ImGui::PopID( );
        }
    }

    static void apply_mirage_style( ImGuiStyle& style, const ImVec4& accent, const ImVec4& sumthings )
    {
        auto& t = config.theme;
        style.Colors[ImGuiCol_WindowBg] = ImVec4( t.background[0], t.background[1], t.background[2], t.opacity );
        style.Colors[ImGuiCol_ChildBg] = ImVec4( t.child_bg[0], t.child_bg[1], t.child_bg[2], t.child_bg[3] );
        style.Colors[ImGuiCol_PopupBg] = ImVec4( t.child_bg[0], t.child_bg[1], t.child_bg[2], ImClamp( t.opacity + 0.1f, 0.f, 1.f ) );
        style.Colors[ImGuiCol_Text] = ImVec4( t.text[0], t.text[1], t.text[2], t.text[3] );
        style.Colors[ImGuiCol_TextDisabled] = ImVec4( t.text[0] * 0.55f, t.text[1] * 0.55f, t.text[2] * 0.55f, 0.7f );
        style.Colors[ImGuiCol_FrameBg] = ImVec4( t.frame_bg[0], t.frame_bg[1], t.frame_bg[2], t.frame_bg[3] );
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4( t.frame_bg[0], t.frame_bg[1], t.frame_bg[2], t.frame_bg[3] );
        style.Colors[ImGuiCol_FrameBgHovered] = ImAdd::HexToColorVec4( 0x535353, 1 );
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4( 0, 0, 0, 0 );
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4( 0, 0, 0, 0 );
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4( 0, 0, 0, 0 );
        style.WindowBorderSize = 1.5f;
        style.ChildBorderSize = 1.5f;
        style.Colors[ImGuiCol_Border] = ImVec4( t.border[0], t.border[1], t.border[2], t.border[3] );
        style.Colors[ImGuiCol_CheckMark] = sumthings;
        style.Colors[ImGuiCol_Header] = sumthings;
        style.Colors[ImGuiCol_HeaderActive] = sumthings;
        style.Colors[ImGuiCol_HeaderHovered] = ImColor( 35, 35, 35 );
        style.Colors[ImGuiCol_SliderGrab] = accent;
        style.Colors[ImGuiCol_SliderGrabActive] = accent;
        style.Colors[ImGuiCol_Separator] = accent;
        style.Colors[ImGuiCol_SeparatorActive] = accent;
        style.Colors[ImGuiCol_SeparatorHovered] = accent;
        style.Colors[ImGuiCol_Button] = sumthings;
        style.Colors[ImGuiCol_ButtonActive] = accent;
        style.Colors[ImGuiCol_ButtonHovered] = accent;
    }

    static auto vec4_from( const float c[4] ) -> ImVec4
    {
        return ImVec4( c[0], c[1], c[2], c[3] );
    }

    struct theme_preset_t
    {
        const char* name;
        unsigned accent_hex;
        unsigned secondary_hex;
        unsigned border_hex;
        unsigned background_hex;
        unsigned child_hex;
        unsigned frame_hex;
        unsigned text_hex;
        float opacity;
    };

    static const theme_preset_t k_theme_presets[] =
    {

        { "feelings / mirage", 0x9896d6, 0x57579c, 0x353244, 0x000000, 0x0c0c0c, 0x2c2c2c, 0xffffff, 0.80f },
        { "neverlose",         0x00a2ff, 0x0066aa, 0x1a2a3a, 0x0a0e14, 0x101820, 0x182230, 0xe8f0ff, 0.92f },
        { "gamesSense",        0x96c83c, 0x5a7a24, 0x2a3220, 0x0a0a0a, 0x121412, 0x1e2218, 0xd8e0c8, 0.90f },
        { "onetap",            0xff8a3d, 0xb85a20, 0x3a2a1e, 0x0c0a08, 0x141210, 0x242018, 0xfff0e0, 0.90f },
        { "fatality",          0xe83a4a, 0x8a1e28, 0x3a181c, 0x0c0808, 0x140c0c, 0x241414, 0xffe8e8, 0.90f },
        { "aimware",           0x9b59ff, 0x5a2eb8, 0x2a1e40, 0x0a0810, 0x120e18, 0x1e1828, 0xf0e8ff, 0.90f },
        { "primordial",        0x6ea8fe, 0x3d6aaa, 0x243040, 0x0c1014, 0x121820, 0x1c2430, 0xe8eef8, 0.88f },
        { "nixware",           0x00d4c8, 0x008a82, 0x1a3030, 0x080c0c, 0x0e1616, 0x1a2424, 0xe0fffc, 0.90f },
        { "spirthack",         0xff5c8a, 0xb83058, 0x3a1a28, 0x0c080a, 0x140c10, 0x241820, 0xffe8f0, 0.90f },
        { "skeet",             0x88b040, 0x4a6820, 0x282e20, 0x0a0a0a, 0x101010, 0x1c1c1c, 0xc8d0b8, 0.95f },
        { "dark ud",           0xb0b0b8, 0x606068, 0x2a2a2e, 0x000000, 0x0a0a0a, 0x1a1a1a, 0xf0f0f0, 0.85f },
        { "midnight",          0x7c6cff, 0x4038aa, 0x24204a, 0x06060e, 0x0c0c18, 0x161628, 0xe8e6ff, 0.90f },
        { "lapse",             0x988dff, 0x5750b8, 0x2e2a45, 0x121218, 0x1a1a22, 0x282834, 0xf0eef8, 0.80f },
        { "blushes",           0xff8fb8, 0xc45a82, 0x3a2430, 0x10080c, 0x180e12, 0x281820, 0xffeef4, 0.88f },
        { "genesis",           0x5b8def, 0x3a5aa0, 0x1e2a40, 0x080a10, 0x0e1218, 0x1a222c, 0xe8eef8, 0.90f },
    };

    static auto hex_rgb( unsigned hex, float a = 1.f ) -> ImVec4
    {
        return ImAdd::HexToColorVec4( hex, a );
    }

    static auto write4( float* dst, const ImVec4& v ) -> void
    {
        dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; dst[3] = v.w;
    }

    static auto apply_theme_preset( int idx ) -> void
    {
        if ( idx < 0 || idx >= ( int )IM_ARRAYSIZE( k_theme_presets ) )
            idx = 0;
        const auto& p = k_theme_presets[idx];
        auto& t = config.theme;
        t.preset = idx;
        t.opacity = p.opacity;
        write4( t.accent, hex_rgb( p.accent_hex ) );
        write4( t.secondary, hex_rgb( p.secondary_hex ) );
        write4( t.border, hex_rgb( p.border_hex ) );
        write4( t.background, hex_rgb( p.background_hex, p.opacity ) );
        write4( t.child_bg, hex_rgb( p.child_hex ) );
        write4( t.frame_bg, hex_rgb( p.frame_hex ) );
        write4( t.text, hex_rgb( p.text_hex ) );
    }

    static auto reset_theme_defaults( ) -> void
    {
        config.theme.ui_scale = 1.f;
        config.theme.animations = true;
        apply_theme_preset( 0 );
    }

    static auto theme_color_changed( ) -> void
    {
        config.theme.preset = -1;
        config.theme.background[3] = config.theme.opacity;
    }
}

auto menu_c::init( ) -> void
{
    ImGuiStyle& style = ImGui::GetStyle( );
    style.WindowRounding = 0.f;
    style.FrameRounding = 0.f;
    style.ChildRounding = 0.f;
    style.PopupRounding = 0.f;
    style.ScrollbarRounding = 0.f;
    style.GrabRounding = 0.f;
    style.TabRounding = 0.f;
    style.WindowBorderSize = 1.5f;
    style.ChildBorderSize = 1.5f;
    style.FrameBorderSize = 0.f;
    style.WindowPadding = ImVec2( 10, 10 );
    style.FramePadding = ImVec2( 6, 4 );
    style.ItemSpacing = ImVec2( 8, 6 );
    ImGui::StyleColorsDark( );

    sync_flags_from_config( );
}

auto menu_c::shutdown_esp_preview( ) -> void
{
    if ( g_esp_preview_srv )
    {
        g_esp_preview_srv->Release( );
        g_esp_preview_srv = nullptr;
    }
    g_esp_preview_w = 0;
    g_esp_preview_h = 0;
    g_esp_content_x0 = g_esp_content_y0 = 0;
    g_esp_content_x1 = g_esp_content_y1 = 0;
}

auto menu_c::init_esp_preview( ID3D11Device* device ) -> bool
{
    shutdown_esp_preview( );
    if ( !device )
        return false;

    HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
    const bool need_uninit = SUCCEEDED( hr );
    if ( FAILED( hr ) && hr != RPC_E_CHANGED_MODE )
        return false;

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* tex = nullptr;
    bool ok = false;
    UINT w = 0, h = 0;
    std::vector<BYTE> pixels;

    hr = CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) );
    if ( FAILED( hr ) )
        goto done;
    hr = factory->CreateStream( &stream );
    if ( FAILED( hr ) )
        goto done;
    hr = stream->InitializeFromMemory( esppreview, ( DWORD )ESP_PREVIEW_PNG_SIZE );
    if ( FAILED( hr ) )
        goto done;
    hr = factory->CreateDecoderFromStream( stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder );
    if ( FAILED( hr ) )
        goto done;
    hr = decoder->GetFrame( 0, &frame );
    if ( FAILED( hr ) )
        goto done;
    hr = factory->CreateFormatConverter( &converter );
    if ( FAILED( hr ) )
        goto done;
    hr = converter->Initialize( frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom );
    if ( FAILED( hr ) )
        goto done;
    hr = converter->GetSize( &w, &h );
    if ( FAILED( hr ) || w == 0 || h == 0 )
        goto done;

    pixels.resize( ( size_t )w * ( size_t )h * 4 );
    hr = converter->CopyPixels( nullptr, w * 4, ( UINT )pixels.size( ), pixels.data( ) );
    if ( FAILED( hr ) )
        goto done;

    {
        const BYTE* c0 = pixels.data( );
        const BYTE* c1 = pixels.data( ) + ( ( size_t )w - 1 ) * 4;
        const BYTE* c2 = pixels.data( ) + ( ( size_t )( h - 1 ) * ( size_t )w ) * 4;
        const BYTE* c3 = pixels.data( ) + ( ( size_t )( h - 1 ) * ( size_t )w + ( size_t )w - 1 ) * 4;
        const int bg_r = ( c0[0] + c1[0] + c2[0] + c3[0] ) / 4;
        const int bg_g = ( c0[1] + c1[1] + c2[1] + c3[1] ) / 4;
        const int bg_b = ( c0[2] + c1[2] + c2[2] + c3[2] ) / 4;

        size_t transparent = 0;
        const size_t total = ( size_t )w * ( size_t )h;
        for ( size_t i = 0; i < total; ++i )
        {
            if ( pixels[i * 4 + 3] < 40 )
                ++transparent;
        }
        const bool use_alpha = transparent > total / 20;

        int x0 = ( int )w, y0 = ( int )h, x1 = 0, y1 = 0;
        for ( UINT y = 0; y < h; ++y )
        {
            const BYTE* row = pixels.data( ) + ( size_t )y * ( size_t )w * 4;
            for ( UINT x = 0; x < w; ++x )
            {
                const BYTE* px = row + ( size_t )x * 4;
                const int a = px[3];
                bool fg = false;
                if ( a < 28 )
                    fg = false;
                else if ( use_alpha )
                    fg = a > 28;
                else
                {
                    const int dr = ( int )px[0] - bg_r;
                    const int dg = ( int )px[1] - bg_g;
                    const int db = ( int )px[2] - bg_b;
                    fg = ( dr * dr + dg * dg + db * db ) > ( 26 * 26 );
                }
                if ( fg )
                {
                    if ( ( int )x < x0 ) x0 = ( int )x;
                    if ( ( int )y < y0 ) y0 = ( int )y;
                    if ( ( int )x > x1 ) x1 = ( int )x;
                    if ( ( int )y > y1 ) y1 = ( int )y;
                }
            }
        }
        if ( x1 >= x0 && y1 >= y0 )
        {
            g_esp_content_x0 = x0;
            g_esp_content_y0 = y0;
            g_esp_content_x1 = x1 + 1;
            g_esp_content_y1 = y1 + 1;
        }
        else
        {
            g_esp_content_x0 = 0;
            g_esp_content_y0 = 0;
            g_esp_content_x1 = ( int )w;
            g_esp_content_y1 = ( int )h;
        }
    }

    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels.data( );
        sub.SysMemPitch = w * 4;

        hr = device->CreateTexture2D( &desc, &sub, &tex );
        if ( FAILED( hr ) )
            goto done;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView( tex, &srv_desc, &g_esp_preview_srv );
        if ( FAILED( hr ) )
            goto done;
    }

    g_esp_preview_w = ( int )w;
    g_esp_preview_h = ( int )h;
    ok = true;

done:
    if ( tex ) tex->Release( );
    if ( converter ) converter->Release( );
    if ( frame ) frame->Release( );
    if ( decoder ) decoder->Release( );
    if ( stream ) stream->Release( );
    if ( factory ) factory->Release( );
    if ( need_uninit )
        CoUninitialize( );
    return ok;
}

auto menu_c::draw( ) -> void
{
    ImGuiStyle& style = ImGui::GetStyle( );
    ImGuiIO& io = ImGui::GetIO( );

    const ImVec4 accent = vec4_from( config.theme.accent );
    const ImVec4 sumthings = vec4_from( config.theme.secondary );
    apply_mirage_style( style, accent, sumthings );

    io.FontGlobalScale = ImClamp( config.theme.ui_scale, 0.75f, 1.5f );

    if ( config.settings.watermark )
        draw_watermark( accent );
    if ( g_show_keybinds )
        draw_keybind_list( accent );

    players_list::draw( accent );

    feelings_fm::draw( accent );

    if ( !config.menu_open )
    {
        g_menu_open_anim = 0.f;
        return;
    }

    if ( config.theme.animations )
        g_menu_open_anim = ImMin( 1.f, g_menu_open_anim + io.DeltaTime / 0.18f );
    else
        g_menu_open_anim = 1.f;

    sync_flags_from_config( );

    static ImVec2 menu_pos = ImVec2( 100.0f, 100.0f );
    static ImVec2 menu_size = ImVec2( 556.0f, 516.0f );
    static bool title_dragging = false;
    static int selected_tab = 1;
    static float esp_anim = 0.0f;

    if ( title_dragging )
        ImGui::SetNextWindowPos( menu_pos, ImGuiCond_Always );
    else
        ImGui::SetNextWindowPos( menu_pos, ImGuiCond_Once );
    ImGui::SetNextWindowSize( ImVec2( 556, 516 ), ImGuiCond_Once );
    ImGui::SetNextWindowSizeConstraints( ImVec2( 500, 320 ), ImVec2( FLT_MAX, FLT_MAX ) );
    ImGui::PushStyleColor( ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab] );
    ImGui::PushStyleVar( ImGuiStyleVar_Alpha, style.Alpha * g_menu_open_anim );
    ImGui::Begin( "Feelings.lol", nullptr, ImGuiWindowFlags_NoTitleBar );
    menu_pos = ImGui::GetWindowPos( );
    menu_size = ImGui::GetWindowSize( );

    const char* tabNames[] = { "visuals", "rage", "players", "world", "explorer", "settings" };
    const int tabCount = ( int )IM_ARRAYSIZE( tabNames );

    draw_feature_search_bar( &selected_tab, ImGui::GetContentRegionAvail( ).x );
    const bool search_hot = ImGui::IsItemHovered( ) || ImGui::IsItemActive( );
    const float header_bottom = ImGui::GetItemRectMax( ).y;

    title_dragging = ImGui::IsWindowHovered( ) && !search_hot && ImGui::IsMouseDragging( ImGuiMouseButton_Left )
        && ImGui::GetIO( ).MousePos.y < header_bottom + 4.0f;
    if ( title_dragging )
    {
        menu_pos.x += ImGui::GetIO( ).MouseDelta.x;
        menu_pos.y += ImGui::GetIO( ).MouseDelta.y;
        ImGui::SetWindowPos( menu_pos );
    }

    const float tab_bar_height = 28.0f;
    const float section_bar_height = 24.0f;
    const float content_reserve = tab_bar_height + style.ItemSpacing.y;

    draw_section_bar( k_tab_sections[selected_tab], k_tab_section_count[selected_tab], &g_selected_section[selected_tab], section_bar_height, accent );
    if ( g_selected_section[selected_tab] >= k_tab_section_count[selected_tab] )
        g_selected_section[selected_tab] = 0;
    const int section = g_selected_section[selected_tab];

    if ( g_content_fade_tab != selected_tab || g_content_fade_section != section )
    {
        g_content_fade_tab = selected_tab;
        g_content_fade_section = section;
        g_content_fade = config.theme.animations ? 0.f : 1.f;
    }
    if ( config.theme.animations )
        g_content_fade = ImMin( 1.f, g_content_fade + io.DeltaTime / 0.14f );
    else
        g_content_fade = 1.f;

    ImGui::PushStyleVar( ImGuiStyleVar_Alpha, style.Alpha * g_menu_open_anim * g_content_fade );
    ImGui::BeginChild( "content", ImVec2( 0, -content_reserve ), true );
    if ( selected_tab == 4 )
    {

        if ( section == 0 )
            g->explorer.draw_tree( );
        else
        {
            ImGui::SeparatorText( k_tab_sections[selected_tab][section] );
        }
    }
    else
    {
    ImGui::Columns( 2, nullptr, false );
    ImGui::BeginChild( "child_1", ImVec2( 0, 0 ), true );
    {
        if ( selected_tab == 0 && section == 0 )
        {
            ImGui::SeparatorText( "players" );
            ImAdd::CheckBox( "esp enabled", &config.esp.enabled, nullptr, config.esp.color, config.esp.color_outline );
            pulse_feature( "esp enabled" );
            ImAdd::CheckBox( "team check", &config.esp.team_check );
            pulse_feature( "team check" );
            ImAdd::CheckBox( "friend check", &config.esp.friend_check );
            pulse_feature( "friend check" );
            ImAdd::CheckBox( "npc check", &config.esp.npc_check );
            pulse_feature( "npc check" );
            ImAdd::CheckBox( "wall check", &config.esp.wall_check );
            pulse_feature( "wall check" );
            ImAdd::CheckBox( "box", &config.esp.box, nullptr, config.esp.color, config.esp.color_outline );
            pulse_feature( "box" );
            ImAdd::CheckBox( "box fill", &config.esp.box_filled, nullptr, config.esp.box_fill_color, config.esp.box_fill_color2 );
            pulse_feature( "box fill" );
            if ( config.esp.box_filled )
            {
                ImAdd::CheckBox( "auto rotate", &config.esp.box_fill_auto_rotate );
                pulse_feature( "box fill auto" );
                if ( !config.esp.box_fill_auto_rotate )
                {
                    ImAdd::SliderFloat( "rotate", &config.esp.box_fill_rotate, 0.f, 360.f, -1.f, "%.0f deg" );
                    pulse_feature( "box fill rotate" );
                }
                else
                {
                    ImAdd::SliderFloat( "rotate speed", &config.esp.box_fill_rotate_speed, 0.f, 360.f, -1.f, "%.0f deg/s" );
                    pulse_feature( "box fill rotate" );
                }
            }
            {
                static std::vector<int> outline_sel;
                static const std::vector<const char*> outline_items = {
                    "box", "text", "health", "skeleton", "tracers", "meshchams"
                };
                if ( outline_sel.size( ) != outline_items.size( ) )
                    outline_sel.assign( outline_items.size( ), 0 );
                for ( int i = 0; i < 6; ++i )
                    outline_sel[i] = config.esp.outline_flags[i] ? 1 : 0;
                if ( ImAdd::MultiCombo( "outlines", &outline_sel, outline_items ) )
                {
                    for ( int i = 0; i < 6; ++i )
                        config.esp.outline_flags[i] = outline_sel[i] ? 1 : 0;
                    config.esp.box_outline = config.esp.outline_box( );
                    config.esp.text_outline = config.esp.outline_text( );
                }
                pulse_feature( "outline" );
            }
            ImAdd::CheckBox( "health bar", &config.esp.health, nullptr,
                config.esp.health_top, config.esp.health_bottom, nullptr,
                config.esp.health_mid, config.esp.health_outline );
            pulse_feature( "health" );
            ImAdd::CheckBox( "armor bar", &config.esp.armor, nullptr,
                config.esp.armor_top, config.esp.armor_bottom, nullptr,
                config.esp.armor_mid, config.esp.armor_outline );
            pulse_feature( "armor" );
            ImAdd::CheckBox( "ammo bar", &config.esp.ammo, nullptr,
                config.esp.ammo_top, config.esp.ammo_bottom, nullptr,
                config.esp.ammo_mid, config.esp.ammo_outline );
            pulse_feature( "ammo" );
            ImAdd::CheckBox( "display name", &config.esp.display_name, nullptr, config.esp.display_color, config.esp.display_outline );
            pulse_feature( "display name" );
            ImAdd::CheckBox( "username", &config.esp.username, nullptr, config.esp.username_color, config.esp.username_outline );
            pulse_feature( "username" );
            ImAdd::CheckBox( "distance", &config.esp.distance, nullptr, config.esp.distance_color, config.esp.distance_outline );
            pulse_feature( "distance" );
        }
        else if ( selected_tab == 0 && section == 1 )
        {
            ImGui::SeparatorText( "extras" );
            ImAdd::CheckBox( "tool", &config.esp.tool, nullptr, config.esp.tool_color, config.esp.tool_outline );
            pulse_feature( "tool" );
            ImAdd::CheckBox( "platform", &config.esp.platform, nullptr, config.esp.platform_color, config.esp.platform_outline );
            pulse_feature( "platform" );
            ImAdd::CheckBox( "state", &config.esp.state, nullptr, config.esp.state_color, config.esp.state_outline );
            pulse_feature( "state" );
            ImAdd::CheckBox( "health text", &config.esp.health_text, nullptr, config.esp.health_text_color, config.esp.health_text_outline );
            pulse_feature( "health text" );
            ImAdd::CheckBox( "armor text", &config.esp.armor_text, nullptr, config.esp.armor_text_color, config.esp.armor_text_outline );
            pulse_feature( "armor text" );
            ImAdd::CheckBox( "ammo text", &config.esp.ammo_text, nullptr, config.esp.ammo_text_color, config.esp.ammo_text_outline );
            pulse_feature( "ammo text" );
            ImAdd::CheckBox( "team", &config.esp.team, nullptr, config.esp.team_color, config.esp.team_outline );
            pulse_feature( "team" );
            ImAdd::CheckBox( "tracers", &config.esp.tracers, nullptr, config.esp.tracer_color, config.esp.tracer_outline );
            pulse_feature( "tracers" );
            ImAdd::CheckBox( "target strafe", &config.esp.target_strafe, nullptr, config.esp.target_strafe_color, nullptr );
            pulse_feature( "target strafe ring" );
            ImAdd::CheckBox( "glow", &config.esp.glow, nullptr, config.esp.glow_color, config.esp.glow_outline );
            pulse_feature( "glow" );
            ImAdd::CheckBox( "skeleton", &config.esp.skeleton, nullptr, config.esp.skeleton_color, config.esp.skeleton_outline );
            pulse_feature( "skeleton" );
            ImAdd::CheckBox( "head dot", &config.esp.skeleton_head_dot );
            pulse_feature( "head dot" );
            ImAdd::CheckBox( "meshchams", &config.esp.chams, nullptr, config.esp.chams_color, config.esp.chams_outline );
            pulse_feature( "chams" );
            ImAdd::CheckBox( "world chams", &config.esp.world_chams, nullptr, config.esp.chams_color, config.esp.chams_outline );
            pulse_feature( "world chams" );
            ImAdd::CheckBox( "sound esp", &config.esp.sound_esp, nullptr, config.esp.sound_color, config.esp.sound_outline );
            pulse_feature( "sound esp" );
        }
        else if ( selected_tab == 0 && section == 2 )
        {
            ImGui::SeparatorText( "customization" );
            ImAdd::CheckBox( "text dropshadow", &config.esp.text_dropshadow );
            pulse_feature( "text dropshadow" );
            config.esp.text_outline_style = 1;
            ImAdd::Combo( "font", &config.esp.font, fonts::names, fonts::count );
            pulse_feature( "font" );
            ImAdd::SliderFloat( "font size", &config.esp.font_size, fonts::size_min, fonts::size_max );
            pulse_feature( "font size" );
            ImGui::SeparatorText( "anti-aliasing" );
            {
                static std::vector<int> aa_sel;
                static const std::vector<const char*> aa_items = {
                    "box", "text", "health", "skeleton", "tracers", "meshchams", "glow", "target strafe"
                };
                bool* aa_map[8] = {
                    &config.esp.aa_box,
                    &config.esp.aa_text,
                    &config.esp.aa_health,
                    &config.esp.aa_skeleton,
                    &config.esp.aa_tracers,
                    &config.esp.aa_chams,
                    &config.esp.aa_glow,
                    &config.esp.aa_target_strafe
                };
                if ( aa_sel.size( ) != aa_items.size( ) )
                    aa_sel.assign( aa_items.size( ), 0 );
                for ( int i = 0; i < 8; ++i )
                    aa_sel[i] = *aa_map[i] ? 1 : 0;
                if ( ImAdd::MultiCombo( "aa", &aa_sel, aa_items ) )
                {
                    for ( int i = 0; i < 8; ++i )
                        *aa_map[i] = aa_sel[i] != 0;
                }
                pulse_feature( "aa" );
            }
        }
        else if ( selected_tab == 0 && section == 3 )
        {
            ImGui::SeparatorText( "overlay" );
            ImAdd::CheckBox( "watermark", &config.settings.watermark );
            pulse_feature( "watermark" );
            ImAdd::CheckBox( "keybinds", &g_show_keybinds );
            pulse_feature( "keybinds" );
            ImAdd::CheckBox( "feelings fm", &config.settings.feelings_fm );
            pulse_feature( "feelings fm" );
            ImAdd::CheckBox( "notifications", &config.settings.notifications );
            pulse_feature( "notifications" );
            ImAdd::CheckBox( "hitsounds", &config.settings.hitsounds );
            pulse_feature( "hitsounds" );
            if ( config.settings.hitsounds )
            {
                const int n = hitsounds::count( );
                if ( n > 0 )
                {
                    if ( config.settings.hitsound < 0 || config.settings.hitsound >= n )
                        config.settings.hitsound = 0;
                    std::vector<const char*> names;
                    names.reserve( static_cast<size_t>( n ) );
                    for ( int i = 0; i < n; ++i )
                        names.push_back( hitsounds::name( i ) );
                    ImAdd::Combo( "sound", &config.settings.hitsound, names.data( ), n );
                    ImAdd::SliderFloat( "volume", &config.settings.hitsound_volume, 0.f, 1.f, -1.f, "%.2f" );
                    if ( ImAdd::Button( "preview", ImVec2( -0.1f, 0 ) ) )
                        hitsounds::play_selected( );
                }
            }
        }
        else if ( selected_tab == 5 && section == 0 )
        {
            ImGui::SeparatorText( "settings" );
            ImAdd::CheckBox( "vsync", &config.settings.vsync );
            pulse_feature( "vsync" );
            ImAdd::CheckBox( "low power", &config.settings.low_power );
            pulse_feature( "low power" );
            ImAdd::CheckBox( "streamproof", &config.settings.streamproof );
            pulse_feature( "streamproof" );
            ImAdd::CheckBox( "hide console", &config.settings.hide_console );
            pulse_feature( "hide console" );
            ImAdd::CheckBox( "auto rescan", &config.settings.auto_rescan );
            pulse_feature( "auto rescan" );
            if ( ImAdd::Button( "rescan now", ImVec2( -0.1f, 0 ) ) )
            {
                if ( g->engine.rescan( ) )
                {
                    const uintptr_t dm = g->engine.datamodel.load( );
                    const int64_t place = dm ? sdk::u_datamodel( dm ).place_id( ) : 0;
                    const games::game_id gid = games::detect( place, g->engine.workspace.load( ) );
                    games::set_current( gid, place );
                    notifications::push( "rescan ok" );
                }
                else
                    notifications::push( "rescan failed — open roblox" );
            }
        }
        else if ( selected_tab == 5 && section == 1 )
        {
            ImGui::SeparatorText( "overlay" );
            ImAdd::CheckBox( "watermark", &config.settings.watermark );
            pulse_feature( "watermark" );
            ImAdd::CheckBox( "keybinds", &g_show_keybinds );
            pulse_feature( "keybinds" );
            ImAdd::CheckBox( "feelings fm", &config.settings.feelings_fm );
            pulse_feature( "feelings fm" );
            ImAdd::CheckBox( "notifications", &config.settings.notifications );
            pulse_feature( "notifications" );
            ImAdd::CheckBox( "hitsounds", &config.settings.hitsounds );
            pulse_feature( "hitsounds" );
            if ( config.settings.hitsounds )
            {
                const int n = hitsounds::count( );
                if ( n > 0 )
                {
                    if ( config.settings.hitsound < 0 || config.settings.hitsound >= n )
                        config.settings.hitsound = 0;
                    std::vector<const char*> names;
                    names.reserve( static_cast<size_t>( n ) );
                    for ( int i = 0; i < n; ++i )
                        names.push_back( hitsounds::name( i ) );
                    ImAdd::Combo( "sound", &config.settings.hitsound, names.data( ), n );
                    ImAdd::SliderFloat( "volume", &config.settings.hitsound_volume, 0.f, 1.f, -1.f, "%.2f" );
                    if ( ImAdd::Button( "preview", ImVec2( -0.1f, 0 ) ) )
                        hitsounds::play_selected( );
                }
            }
            ImAdd::CheckBox( "menu", &config.menu_open, &config.menu_key, nullptr, nullptr, &g_menu_mode );
            pulse_feature( "menu" );
        }
        else if ( selected_tab == 5 && section == 2 )
        {
            ImGui::SeparatorText( "theme" );

            const char* preset_names[IM_ARRAYSIZE( k_theme_presets )];
            for ( int i = 0; i < ( int )IM_ARRAYSIZE( k_theme_presets ); ++i )
                preset_names[i] = k_theme_presets[i].name;
            int preset = config.theme.preset;
            if ( preset < 0 || preset >= ( int )IM_ARRAYSIZE( k_theme_presets ) )
                preset = 0;
            if ( ImAdd::Combo( "preset", &preset, preset_names, ( int )IM_ARRAYSIZE( k_theme_presets ) ) )
                apply_theme_preset( preset );

            ImGui::SeparatorText( "colors" );
            if ( ImAdd::ColorEdit4( "accent", config.theme.accent ) )
                theme_color_changed( );
            if ( ImAdd::ColorEdit4( "secondary", config.theme.secondary ) )
                theme_color_changed( );
            if ( ImAdd::ColorEdit4( "background", config.theme.background ) )
                theme_color_changed( );
            if ( ImAdd::ColorEdit4( "border", config.theme.border ) )
                theme_color_changed( );
            if ( ImAdd::ColorEdit4( "text", config.theme.text ) )
                theme_color_changed( );

            if ( ImAdd::SliderFloat( "opacity", &config.theme.opacity, 0.35f, 1.f, -1.f, "%.2f" ) )
                config.theme.background[3] = config.theme.opacity;

            if ( ImAdd::Button( "reset theme", ImVec2( -0.1f, 0 ) ) )
                reset_theme_defaults( );
        }
        else if ( selected_tab == 5 && section == 3 )
        {
            static char slot_name[64] = "default";
            static char copy_name[64] = "default_copy";
            static int slot_idx = 0;
            static bool inited = false;
            if ( !inited )
            {
                const auto cur = config_io::current_name( );
                if ( !cur.empty( ) )
                    strncpy_s( slot_name, cur.c_str( ), _TRUNCATE );
                inited = true;
            }

            auto slots = config_io::list( );
            if ( slots.empty( ) )
                slots.push_back( slot_name[0] ? slot_name : "default" );

            ImGui::SeparatorText( "slot" );

            if ( slot_idx >= ( int )slots.size( ) )
                slot_idx = 0;
            std::vector<const char*> names;
            names.reserve( slots.size( ) );
            for ( auto& s : slots )
                names.push_back( s.c_str( ) );
            if ( ImAdd::Combo( "configs", &slot_idx, names.data( ), ( int )names.size( ) ) )
            {
                const auto& pick = slots[( size_t )slot_idx];
                strncpy_s( slot_name, pick.c_str( ), _TRUNCATE );
            }
            ImGui::InputText( "name", slot_name, IM_ARRAYSIZE( slot_name ) );

            ImGui::SeparatorText( "file" );
            ImAdd::CheckBox( "autosave", &config.autosave );
            if ( ImAdd::Button( "save", ImVec2( -0.1f, 0 ) ) )
            {
                if ( config_io::save( slot_name ) )
                    notifications::push( "config saved" );
                else
                    notifications::push( "config save failed" );
            }
            if ( ImAdd::Button( "load", ImVec2( -0.1f, 0 ) ) )
            {
                if ( config_io::load( slot_name ) )
                    notifications::push( "config loaded" );
                else
                    notifications::push( "config load failed" );
            }
            ImGui::InputText( "copy as", copy_name, IM_ARRAYSIZE( copy_name ) );
            if ( ImAdd::Button( "copy", ImVec2( -0.1f, 0 ) ) )
            {
                if ( config_io::copy( slot_name, copy_name ) )
                    notifications::push( "config copied" );
                else
                    notifications::push( "config copy failed" );
            }
            if ( ImAdd::Button( "reset defaults", ImVec2( -0.1f, 0 ) ) )
            {
                config_io::reset( );
                notifications::push( "config reset" );
            }
            if ( ImAdd::Button( "delete", ImVec2( -0.1f, 0 ) ) )
            {
                if ( config_io::remove( slot_name ) )
                    notifications::push( "config deleted" );
                else
                    notifications::push( "config delete failed" );
            }
        }
        else if ( selected_tab == 3 && section == 0 )
        {
            ImGui::SeparatorText( "master" );
            ImAdd::CheckBox( "world override", &config.world.enabled );
            pulse_feature( "world override" );
            ImGui::BeginDisabled( !config.world.enabled );
            ImAdd::CheckBox( "weather", &config.world.weather );
            pulse_feature( "weather" );
            ImAdd::CheckBox( "strip atmosphere", &config.world.strip_atmosphere );

            ImGui::SeparatorText( "fog" );
            ImAdd::ColorEdit4( "fog color", config.world.fog_color );
            ImAdd::SliderFloat( "fog start", &config.world.fog_start, 0.f, 1000.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "fog end", &config.world.fog_end, 0.f, 100000.f, -1.f, "%.0f" );
            ImGui::EndDisabled( );
        }
        else if ( selected_tab == 3 && section == 1 )
        {
            ImGui::SeparatorText( "master" );
            ImAdd::CheckBox( "world override", &config.world.enabled );
            pulse_feature( "world override" );
            ImGui::BeginDisabled( !config.world.enabled );
            ImAdd::CheckBox( "lighting", &config.world.lighting );
            pulse_feature( "lighting" );
            ImAdd::CheckBox( "ambience", &config.world.ambience );
            ImAdd::CheckBox( "global shadows", &config.world.global_shadows );
            ImAdd::CheckBox( "prioritize quality", &config.world.prioritize_lighting_quality );

            ImGui::SeparatorText( "day cycle" );
            ImAdd::SliderFloat( "brightness", &config.world.brightness, 0.f, 10.f, -0.05f, "%.2f" );
            ImAdd::SliderFloat( "exposure", &config.world.exposure, -3.f, 3.f, -0.05f, "%.2f" );
            ImAdd::SliderFloat( "clock time", &config.world.clock_time, 0.f, 24.f, -0.05f, "%.2f" );
            ImAdd::SliderFloat( "latitude", &config.world.geographic_latitude, -90.f, 90.f, -0.1f, "%.1f" );

            ImGui::SeparatorText( "environment" );
            ImAdd::SliderFloat( "env diffuse", &config.world.environment_diffuse, 0.f, 2.f, -0.01f, "%.2f" );
            ImAdd::SliderFloat( "env specular", &config.world.environment_specular, 0.f, 2.f, -0.01f, "%.2f" );
            ImAdd::SliderFloat( "shadow soft", &config.world.shadow_softness, 0.f, 1.f, -0.01f, "%.2f" );
            ImGui::EndDisabled( );
        }
        else if ( selected_tab == 3 && section == 2 )
        {
            ImGui::SeparatorText( "materials" );
            ImAdd::CheckBox( "materials override", &config.world.materials );
            pulse_feature( "materials" );
            ImGui::BeginDisabled( !config.world.materials );
            ImAdd::CheckBox( "use 2022 materials", &config.world.use_2022_materials );
            ImGui::EndDisabled( );
        }
        else if ( selected_tab == 3 && section == 3 )
        {
            ImGui::SeparatorText( "motion" );
            ImAdd::CheckBox( "skybox anim", &config.world.skybox_anim );
            pulse_feature( "skybox anim" );
            ImGui::BeginDisabled( !config.world.skybox_anim );
            ImAdd::SliderFloat( "spin yaw", &config.world.skybox_spin_yaw, -60.f, 60.f, -0.5f, "%.1f" );
            ImAdd::CheckBox( "pulse sun", &config.world.skybox_pulse_sun );
            ImGui::EndDisabled( );

            ImGui::SeparatorText( "celestial" );
            ImAdd::CheckBox( "celestial", &config.world.skybox_celestial );
            ImGui::BeginDisabled( !config.world.skybox_celestial );
            ImAdd::SliderFloat( "sun size", &config.world.skybox_sun_size, 0.f, 60.f, -0.5f, "%.1f" );
            ImAdd::SliderFloat( "moon size", &config.world.skybox_moon_size, 0.f, 60.f, -0.5f, "%.1f" );
            ImAdd::SliderInt( "stars", &config.world.skybox_stars, 0, 5000, -1.f, "%d" );
            ImGui::EndDisabled( );
        }
        else if ( selected_tab == 2 && section == 0 )
        {
            ImGui::SeparatorText( "self" );
            ImAdd::CheckBox( "hip height", &config.player.hip_height, &config.player.hip_height_key, nullptr, nullptr, &config.player.hip_height_key_mode );
            pulse_feature( "hip height" );
            ImAdd::CheckBox( "fly", &config.player.fly, &config.player.fly_key, nullptr, nullptr, &config.player.fly_key_mode );
            pulse_feature( "fly" );
            ImAdd::CheckBox( "speed", &config.player.speed, &config.player.speed_key, nullptr, nullptr, &config.player.speed_key_mode );
            pulse_feature( "speed" );
            ImAdd::CheckBox( "gravity", &config.player.gravity, &config.player.gravity_key, nullptr, nullptr, &config.player.gravity_key_mode );
            pulse_feature( "gravity" );
            ImAdd::CheckBox( "jumppower", &config.player.jump, &config.player.jump_key, nullptr, nullptr, &config.player.jump_key_mode );
            pulse_feature( "jumppower" );
            ImAdd::CheckBox( "infinite jump", &config.player.infinite_jump, &config.player.infinite_jump_key, nullptr, nullptr, &config.player.infinite_jump_key_mode );
            pulse_feature( "infinite jump" );
            ImAdd::CheckBox( "no jump cooldown", &config.player.no_jump_cooldown );
            pulse_feature( "no jump cooldown" );
            ImAdd::CheckBox( "spinbot", &config.player.spin, &config.player.spin_key, nullptr, nullptr, &config.player.spin_key_mode );
            pulse_feature( "spinbot" );
            ImAdd::CheckBox( "noclip", &config.player.noclip, &config.player.noclip_key, nullptr, nullptr, &config.player.noclip_key_mode );
            pulse_feature( "noclip" );
            ImAdd::CheckBox( "sit", &config.player.sit, &config.player.sit_key, nullptr, nullptr, &config.player.sit_key_mode );
            pulse_feature( "sit" );
        }
        else if ( selected_tab == 2 && section == 1 )
        {
            ImGui::SeparatorText( "others" );
            ImAdd::CheckBox( "target strafe", &config.player.target_strafe, &config.player.target_strafe_key, nullptr, nullptr, &config.player.target_strafe_key_mode );
            pulse_feature( "target strafe" );
            ImAdd::CheckBox( "rage tp", &config.player.rage_tp, &config.player.rage_tp_key, nullptr, nullptr, &config.player.rage_tp_key_mode );
            pulse_feature( "rage tp" );
            ImAdd::CheckBox( "teleport", &config.player.teleport, &config.player.teleport_key, nullptr, nullptr, &config.player.teleport_key_mode );
            pulse_feature( "teleport" );
            ImAdd::CheckBox( "fling", &config.player.fling, &config.player.fling_key, nullptr, nullptr, &config.player.fling_key_mode );
            pulse_feature( "fling" );
            ImAdd::CheckBox( "hitbox expander", &config.player.hitbox_expander );
            pulse_feature( "hitbox expander" );
            ImAdd::CheckBox( "hitbox visualizer", &config.player.hitbox_visualizer );
            ImAdd::CheckBox( "hitbox root only", &config.player.hitbox_root_only );
            ImAdd::CheckBox( "hitbox team check", &config.player.hitbox_team_check );
            ImAdd::CheckBox( "hitbox npc check", &config.player.hitbox_npc_check );
        }
        else if ( selected_tab == 2 && section == 2 )
        {
            ImGui::SeparatorText( "hvh" );
            ImAdd::CheckBox( "no fall", &config.player.no_fall, &config.player.no_fall_key, nullptr, nullptr, &config.player.no_fall_key_mode );
            pulse_feature( "no fall" );
            ImAdd::CheckBox( "antiaim", &config.player.antiaim, &config.player.antiaim_key, nullptr, nullptr, &config.player.antiaim_key_mode );
            pulse_feature( "antiaim" );
            ImAdd::CheckBox( "desync", &config.player.desync, &config.player.desync_key, nullptr, nullptr, &config.player.desync_key_mode );
            pulse_feature( "desync" );
            ImAdd::CheckBox( "fake lag", &config.player.fake_lag, &config.player.fake_lag_key, nullptr, nullptr, &config.player.fake_lag_key_mode );
            pulse_feature( "fake lag" );
            ImAdd::CheckBox( "freeze lua gc", &config.player.freeze_gc );
            pulse_feature( "freeze lua gc" );
            ImAdd::CheckBox( "freecam", &config.player.freecam, &config.player.freecam_key, nullptr, nullptr, &config.player.freecam_key_mode );
            pulse_feature( "freecam" );
            ImAdd::CheckBox( "antiafk", &config.player.antiafk, &config.player.antiafk_key, nullptr, nullptr, &config.player.antiafk_key_mode );
            pulse_feature( "antiafk" );
            ImAdd::CheckBox( "force state", &config.player.state_force );
            pulse_feature( "force state" );
        }
        else if ( selected_tab == 2 && section == 3 )
        {
            ImAdd::SeparatorText( "misc" );
        }
        else if ( selected_tab == 2 && section == 4 )
        {
            ImGui::SeparatorText( "client" );

            ImGui::Spacing( );
            ImAdd::CheckBox( "animations", &config.player.animation_changer );
            pulse_feature( "animation changer" );
            ImAdd::CheckBox( "emotes", &config.player.emote_changer );
            pulse_feature( "emote changer" );

            ImGui::Spacing( );
            ImGui::SeparatorText( "avatar" );
            ImAdd::CheckBox( "face", &config.player.face_changer );
            pulse_feature( "face changer" );
            ImAdd::CheckBox( "clothing", &config.player.clothing_changer );
            pulse_feature( "clothing changer" );
        }
        else if ( selected_tab == 1 && section == 0 )
        {
            ImGui::SeparatorText( "aim assist" );
            ImAdd::CheckBox( "enabled", &config.aimbot.enabled, &config.aimbot.key, nullptr, nullptr, &config.aimbot.key_mode );
            pulse_feature( "aimbot" );
            ImAdd::CheckBox( "sticky aim", &config.aimbot.sticky );
            ImAdd::CheckBox( "prediction", &config.aimbot.prediction );
            ImAdd::CheckBox( "humanize", &config.aimbot.humanize );

            ImGui::SeparatorText( "filters" );
            ImAdd::CheckBox( "team check", &config.aimbot.team_check );
            ImAdd::CheckBox( "friend check", &config.aimbot.friend_check );
            ImAdd::CheckBox( "npc check", &config.aimbot.npc_check );
            ImAdd::CheckBox( "wall check", &config.aimbot.wall_check );
            ImAdd::CheckBox( "wallbang", &config.aimbot.wallbang );
        }
        else if ( selected_tab == 1 && section == 1 )
        {
            ImGui::SeparatorText( "silent aim" );
            ImAdd::CheckBox( "enabled", &config.silentaim.enabled, &config.silentaim.key, nullptr, nullptr, &config.silentaim.key_mode );
            pulse_feature( "silent aim" );
            if ( config.silentaim.method > 2 )
                config.silentaim.method = 2;
            const char* silent_methods[] = { "raycast", "viewport", "mouse.hit" };
            ImAdd::Combo( "method", &config.silentaim.method, silent_methods, IM_ARRAYSIZE( silent_methods ) );
            pulse_feature( "silent aim" );
            ImAdd::CheckBox( "sticky aim", &config.silentaim.sticky );
            ImAdd::CheckBox( "prediction", &config.silentaim.prediction );
            ImAdd::CheckBox( "closest point", &config.silentaim.closest_point );
            ImAdd::CheckBox( "magic bullet", &config.silentaim.magic_bullet );
            ImAdd::CheckBox( "no spread", &config.silentaim.no_spread );
            ImAdd::CheckBox( "spectate", &config.silentaim.spectate );

            ImGui::SeparatorText( "filters" );
            ImAdd::CheckBox( "team check", &config.silentaim.team_check );
            ImAdd::CheckBox( "friend check", &config.silentaim.friend_check );
            ImAdd::CheckBox( "npc check", &config.silentaim.npc_check );
            ImAdd::CheckBox( "wall check", &config.silentaim.wall_check );
            ImAdd::CheckBox( "wallbang", &config.silentaim.wallbang );
        }
        else if ( selected_tab == 1 && section == 2 )
        {
            ImGui::SeparatorText( "triggerbot" );
            ImAdd::CheckBox( "enabled", &config.triggerbot.enabled, &config.triggerbot.key, nullptr, nullptr, &config.triggerbot.key_mode );
            pulse_feature( "triggerbot" );
            ImAdd::CheckBox( "prediction", &config.triggerbot.prediction );

            ImGui::SeparatorText( "filters" );
            ImAdd::CheckBox( "team check", &config.triggerbot.team_check );
            ImAdd::CheckBox( "friend check", &config.triggerbot.friend_check );
            ImAdd::CheckBox( "npc check", &config.triggerbot.npc_check );
            ImAdd::CheckBox( "wall check", &config.triggerbot.wall_check );
            ImAdd::CheckBox( "wallbang", &config.triggerbot.wallbang );
        }
        else
        {
            ImGui::SeparatorText( k_tab_sections[selected_tab][section] );
        }
    }
    ImGui::EndChild( );

    ImGui::NextColumn( );
    ImGui::BeginChild( "child_2", ImVec2( 0, 0 ), true );
    {
        if ( selected_tab == 0 && section == 0 )
        {
            ImGui::SeparatorText( "box" );
            ImAdd::SliderFloat( "box thickness", &config.esp.box_thickness, 1.0f, 4.0f, -1.f, "%.0f" );
            pulse_feature( "box thickness" );
            const char* box_types[] = { "full", "corner" };
            ImAdd::Combo( "box type", &config.esp.box_type, box_types, IM_ARRAYSIZE( box_types ) );
            g_esp_box_type = config.esp.box_type;
            pulse_feature( "box type" );
            const char* box_outline_modes[] = { "outside", "inside", "double" };
            if ( ImAdd::Combo( "box outline", &config.esp.box_outline_mode, box_outline_modes, IM_ARRAYSIZE( box_outline_modes ) ) )
            {
                // Combo is the mode — keep the Box outline flag on so it actually draws.
                config.esp.outline_flags[0] = 1;
                config.esp.box_outline = true;
            }
            pulse_feature( "box outline" );
            ImGui::SeparatorText( "box fill" );
            ImAdd::CheckBox( "auto rotate", &config.esp.box_fill_auto_rotate );
            pulse_feature( "box fill auto" );
            ImAdd::SliderFloat( "rotate", &config.esp.box_fill_rotate, 0.f, 360.f, -1.f, "%.0f deg" );
            pulse_feature( "box fill rotate" );
            ImAdd::SliderFloat( "rotate speed", &config.esp.box_fill_rotate_speed, 0.f, 360.f, -1.f, "%.0f deg/s" );
        }
        else if ( selected_tab == 0 && section == 1 )
        {
            ImGui::SeparatorText( "tracers / glow / meshchams" );
            const char* origins[] = { "bottom", "center", "top", "mouse" };
            ImAdd::Combo( "origin", &config.esp.tracer_origin, origins, IM_ARRAYSIZE( origins ) );
            pulse_feature( "tracers" );
            ImAdd::SliderFloat( "glow size", &config.esp.glow_size, 4.0f, 48.0f, -0.5f, "%.0f" );
            pulse_feature( "glow" );
            ImAdd::SliderFloat( "skeleton thick", &config.esp.skeleton_thickness, 1.0f, 6.0f, -0.1f, "%.1f" );
            pulse_feature( "skeleton" );
            ImAdd::SliderFloat( "sound range", &config.esp.sound_max_distance, 20.f, 600.f, -5.f, "%.0f" );
            pulse_feature( "sound esp" );
            const char* chams_kinds[] = { "mesh", "instance", "native", "wireframe" };
            ImAdd::Combo( "chams type", &config.esp.chams_kind, chams_kinds, IM_ARRAYSIZE( chams_kinds ) );
            pulse_feature( "chams type" );
            const char* chams_styles[] = { "glass", "xqz", "rainbow", "wire" };
            ImAdd::Combo( "chams look", &config.esp.chams_style, chams_styles, IM_ARRAYSIZE( chams_styles ) );
            pulse_feature( "chams look" );
            ImAdd::CheckBox( "also native highlight", &config.esp.chams_highlight );
            pulse_feature( "native highlight" );
            ImAdd::ColorEdit4( "xqz hidden", config.esp.chams_hidden );
            ImAdd::ColorEdit4( "xqz visible", config.esp.chams_visible );
            ImAdd::SliderFloat( "rainbow speed", &config.esp.chams_rainbow_speed, 0.1f, 4.0f, -0.05f, "%.2f" );
            ImAdd::SliderFloat( "fill transparency", &config.esp.chams_fill_transparency, 0.0f, 1.0f, -0.01f, "%.2f" );
            ImAdd::SliderFloat( "outline transparency", &config.esp.chams_outline_transparency, 0.0f, 1.0f, -0.01f, "%.2f" );
            ImAdd::SliderFloat( "chams thick", &config.esp.chams_thickness, 1.0f, 4.0f, -0.1f, "%.1f" );
        }
        else if ( selected_tab == 0 && section == 2 )
        {
            ImGui::SeparatorText( "health bar" );
            const char* sides[] = { "left", "right", "top", "bottom" };
            ImAdd::Combo( "side", &config.esp.health_side, sides, IM_ARRAYSIZE( sides ) );
            pulse_feature( "health side" );
            ImAdd::SliderFloat( "width", &config.esp.health_width, 1.f, 6.f, -0.1f, "%.1f" );
            ImAdd::SliderFloat( "gap", &config.esp.health_gap, 0.f, 10.f, -0.1f, "%.1f" );
            ImAdd::ColorEdit4( "top", config.esp.health_top );
            pulse_feature( "health top" );
            ImAdd::ColorEdit4( "mid", config.esp.health_mid );
            pulse_feature( "health mid" );
            ImAdd::ColorEdit4( "bottom", config.esp.health_bottom );
            pulse_feature( "health bottom" );
            ImAdd::ColorEdit4( "outline", config.esp.health_outline );
            pulse_feature( "health outline" );

            ImGui::SeparatorText( "armor bar" );
            ImAdd::ColorEdit4( "armor top", config.esp.armor_top );
            ImAdd::ColorEdit4( "armor mid", config.esp.armor_mid );
            ImAdd::ColorEdit4( "armor bottom", config.esp.armor_bottom );
            ImAdd::ColorEdit4( "armor outline", config.esp.armor_outline );

            ImGui::SeparatorText( "ammo bar" );
            ImAdd::ColorEdit4( "ammo top", config.esp.ammo_top );
            ImAdd::ColorEdit4( "ammo mid", config.esp.ammo_mid );
            ImAdd::ColorEdit4( "ammo bottom", config.esp.ammo_bottom );
            ImAdd::ColorEdit4( "ammo outline", config.esp.ammo_outline );
        }
        else if ( selected_tab == 5 && section == 0 )
        {
            ImGui::SeparatorText( "perf" );
            ImAdd::SliderInt( "fps cap", &config.settings.fps_cap, 60, 240, -0.1f, "%d" );
            pulse_feature( "fps cap" );
        }
        else if ( selected_tab == 5 && section == 1 )
        {
            ImGui::SeparatorText( "binds" );
        }
        else if ( selected_tab == 5 && section == 2 )
        {
            ImGui::SeparatorText( "layout" );
            ImAdd::SliderFloat( "ui scale", &config.theme.ui_scale, 0.75f, 1.50f, -1.f, "%.2f" );
            ImAdd::CheckBox( "animations", &config.theme.animations );

            ImGui::SeparatorText( "panels" );
            if ( ImAdd::ColorEdit4( "child bg", config.theme.child_bg ) )
                theme_color_changed( );
            if ( ImAdd::ColorEdit4( "frame bg", config.theme.frame_bg ) )
                theme_color_changed( );
        }
        else if ( selected_tab == 2 && section == 0 )
        {
            ImGui::SeparatorText( "methods" );
            const char* fly_methods[] = {
                "velocity",
                "velocity + cframe",
                "cframe slam",
                "hover",
                "noclip cframe",
                "look fly",
                "lerp cframe",
                "velocity burst"
            };
            ImAdd::Combo( "fly method", &config.player.fly_method, fly_methods, IM_ARRAYSIZE( fly_methods ) );
            pulse_feature( "fly" );
            const char* speed_methods[] = { "walkspeed", "cframe lerp", "velocity", "velocity burst" };
            ImAdd::Combo( "speed method", &config.player.speed_method, speed_methods, IM_ARRAYSIZE( speed_methods ) );
            pulse_feature( "speed" );

            ImGui::SeparatorText( "values" );
            ImAdd::SliderFloat( "hip height", &config.player.hip_height_value, -10.f, 50.f, -0.1f, "%.1f" );
            pulse_feature( "hip height" );
            ImAdd::SliderFloat( "fly speed", &config.player.fly_speed, 1.f, 300.f, -1.f, "%.0f" );
            pulse_feature( "fly" );
            ImAdd::SliderFloat( "inf jump power", &config.player.infinite_jump_power, 10.f, 300.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "speed", &config.player.walk_speed, 0.f, 500.f, -1.f, "%.0f" );
            pulse_feature( "speed" );
            ImAdd::SliderFloat( "gravity", &config.player.gravity_value, 0.f, 500.f, -1.f, "%.1f" );
            pulse_feature( "gravity" );
            ImAdd::SliderFloat( "jumppower", &config.player.jump_power, 0.f, 500.f, -1.f, "%.0f" );
            pulse_feature( "jumppower" );
            ImAdd::SliderFloat( "spin speed", &config.player.spin_speed, 1.f, 100.f, -0.5f, "%.1f" );
            pulse_feature( "spinbot" );
        }
        else if ( selected_tab == 2 && section == 1 )
        {
            ImGui::SeparatorText( "target strafe" );
            ImAdd::CheckBox( "use aim / silent target", &config.player.target_strafe_from_aim );
            ImAdd::SliderFloat( "orbit speed", &config.player.target_strafe_speed, 0.5f, 30.f, -0.1f, "%.1f" );
            ImAdd::SliderFloat( "radius", &config.player.target_strafe_radius, 2.f, 40.f, -0.5f, "%.1f" );
            ImAdd::SliderFloat( "height", &config.player.target_strafe_height, -20.f, 40.f, -0.5f, "%.1f" );
            pulse_feature( "target strafe" );

            ImGui::SeparatorText( "rage tp" );
            ImAdd::SliderFloat( "depth", &config.player.rage_tp_depth, 0.f, 20.f, -0.5f, "%.1f" );
            ImAdd::SliderFloat( "under", &config.player.rage_tp_under, 5.f, 40.f, -0.5f, "%.1f" );
            pulse_feature( "rage tp" );

            ImGui::SeparatorText( "teleport" );
            {
                const char* tp_methods[] = { "camera look", "closest", "aim target", "waypoint" };
                ImAdd::Combo( "tp method", &config.player.teleport_method, tp_methods, IM_ARRAYSIZE( tp_methods ) );
            }
            ImAdd::SliderFloat( "tp distance", &config.player.teleport_distance, 5.f, 500.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "tp height", &config.player.teleport_height, 0.f, 20.f, -0.5f, "%.1f" );
            ImAdd::KeyBind( "tp save", &config.player.teleport_save_key );
            pulse_feature( "teleport" );

            ImGui::SeparatorText( "fling" );
            ImAdd::SliderFloat( "power", &config.player.fling_power, 50.f, 2000.f, -10.f, "%.0f" );
            pulse_feature( "fling" );

            ImGui::SeparatorText( "hitbox" );
            ImAdd::SliderFloat( "size x", &config.player.hitbox_size_x, 1.f, 50.f, -0.5f, "%.1f" );
            ImAdd::SliderFloat( "size y", &config.player.hitbox_size_y, 1.f, 50.f, -0.5f, "%.1f" );
            ImAdd::SliderFloat( "size z", &config.player.hitbox_size_z, 1.f, 50.f, -0.5f, "%.1f" );
            ImAdd::ColorEdit4( "hitbox color", config.player.hitbox_color );
            pulse_feature( "hitbox expander" );
        }
        else if ( selected_tab == 2 && section == 2 )
        {
            ImGui::SeparatorText( "antiaim" );
            {
                const char* pitches[] = { "none", "down", "up", "jitter" };
                ImAdd::Combo( "pitch", &config.player.antiaim_pitch, pitches, IM_ARRAYSIZE( pitches ) );
            }
            {
                const char* yaws[] = { "none", "backward", "spin", "jitter", "left", "right" };
                ImAdd::Combo( "yaw", &config.player.antiaim_yaw, yaws, IM_ARRAYSIZE( yaws ) );
            }
            ImAdd::SliderFloat( "spin speed", &config.player.antiaim_spin_speed, 1.f, 40.f, -0.2f, "%.1f" );
            ImAdd::SliderFloat( "move jitter", &config.player.antiaim_jitter, 0.f, 15.f, -0.1f, "%.1f" );
            ImAdd::CheckBox( "at targets", &config.player.antiaim_at_targets );
            ImAdd::CheckBox( "aa arrows", &config.player.antiaim_arrows );
            pulse_feature( "antiaim" );

            ImGui::SeparatorText( "desync" );
            {
                const char* desync_methods[] = { "break lag", "freeze", "yaw 180" };
                ImAdd::Combo( "desync method", &config.player.desync_method, desync_methods, IM_ARRAYSIZE( desync_methods ) );
            }
            ImAdd::SliderFloat( "desync amount", &config.player.desync_amount, 1.f, 40.f, -0.5f, "%.1f" );
            ImAdd::CheckBox( "randomize", &config.player.desync_random );
            ImAdd::CheckBox( "on peek only", &config.player.desync_on_peek );
            pulse_feature( "desync" );

            ImGui::SeparatorText( "fake lag" );
            ImAdd::SliderFloat( "hold ms", &config.player.fake_lag_hold_ms, 40.f, 600.f, -5.f, "%.0f" );
            ImAdd::SliderFloat( "lag amount", &config.player.fake_lag_amount, 0.f, 30.f, -0.5f, "%.1f" );
            pulse_feature( "fake lag" );

            ImGui::SeparatorText( "freecam" );
            ImAdd::SliderFloat( "freecam speed", &config.player.freecam_speed, 5.f, 200.f, -1.f, "%.0f" );
            pulse_feature( "freecam" );

            ImGui::SeparatorText( "humanoid state" );
            pulse_feature( "force state" );
            static const char* states[] = {
                "FallingDown", "Ragdoll", "GettingUp", "Jumping", "Swimming",
                "Freefall", "Flying", "Landed", "Running", "?", "RunningNoPhysics",
                "StrafingNoPhysics", "Climbing", "Seated", "PlatformStanding",
                "Dead", "Physics", "?", "None"
            };
            ImAdd::Combo( "force type", &config.player.state_force_id, states, IM_ARRAYSIZE( states ) );
        }
        else if ( selected_tab == 2 && section == 3 )
        {
            ImAdd::SeparatorText( "waypoints" );
            pulse_feature( "waypoints" );
            ImGui::InputText( "name", config.player.waypoint_name, IM_ARRAYSIZE( config.player.waypoint_name ) );
            if ( ImAdd::Button( "save here", ImVec2( -1.f, 0 ) ) )
                teleport::save_current_waypoint( config.player.waypoint_name );

            auto list = teleport::waypoints( );
            const int sel = teleport::selected_waypoint( );
            ImAdd::BeginChild( "##wp_list", ImVec2( -1.f, 160.f ) );
            for ( int i = 0; i < ( int )list.size( ); ++i )
            {
                char label[96];
                ImFormatString( label, IM_ARRAYSIZE( label ), "%s  (%.0f, %.0f, %.0f)",
                    list[( size_t )i].name.c_str( ),
                    list[( size_t )i].pos.x, list[( size_t )i].pos.y, list[( size_t )i].pos.z );
                if ( ImAdd::Selectable( label, sel == i ) )
                    teleport::set_selected_waypoint( i );
            }
            ImAdd::EndChild( );

            if ( ImAdd::Button( "teleport", ImVec2( -1.f, 0 ) ) && sel >= 0 )
                teleport::teleport_to_waypoint( sel );
            if ( ImAdd::Button( "delete", ImVec2( -1.f, 0 ) ) && sel >= 0 )
                teleport::remove_waypoint( sel );
            if ( ImAdd::Button( "clear all", ImVec2( -1.f, 0 ) ) )
                teleport::clear_waypoints( );
        }
        else if ( selected_tab == 2 && section == 4 )
        {
            ImGui::SeparatorText( "animations" );
            pulse_feature( "animation changer" );
            ImGui::BeginDisabled( !config.player.animation_changer );
            static const char* packs[] = {
                "zombie", "ninja", "robot", "zombie 2", "levitation",
                "stylish", "cartoony", "super hero", "elder", "toy", "old school"
            };
            ImAdd::BeginChild( "##anim_packs", ImVec2( -1.f, 140.f ) );
            for ( int i = 0; i < IM_ARRAYSIZE( packs ); ++i )
            {
                if ( ImAdd::Selectable( packs[i], config.player.animation_preset == i ) )
                    config.player.animation_preset = i;
            }
            ImAdd::EndChild( );
            ImGui::EndDisabled( );

            ImGui::SeparatorText( "emotes" );
            pulse_feature( "emote changer" );
            ImGui::BeginDisabled( !config.player.emote_changer );
            static const char* emotes[] = {
                "tilting", "drip", "griddy", "floss", "orange justice",
                "default dance", "hype", "shuffle", "disco", "breakdance", "smug"
            };
            ImAdd::BeginChild( "##emote_list", ImVec2( -1.f, 110.f ) );
            for ( int i = 0; i < IM_ARRAYSIZE( emotes ); ++i )
            {
                if ( ImAdd::Selectable( emotes[i], config.player.emote_preset == i ) )
                    config.player.emote_preset = i;
            }
            ImAdd::EndChild( );
            ImGui::EndDisabled( );

            ImGui::SeparatorText( "face" );
            pulse_feature( "face changer" );
            ImGui::BeginDisabled( !config.player.face_changer );
            static const char* faces[] = {
                "Super Super Happy Face", "Playful Vampire", "Catching Snowflakes",
                "Roblox Madness Face", "Red Beast Mode", "Radioactive Beast Mode",
                "Blizzard Beast Mode", "Miss Scarlet", "Prankster", "Troublemaker",
                "Yum", "Green Glowing Eyes", "Red Glowing Eyes"
            };
            ImAdd::BeginChild( "##face_list", ImVec2( -1.f, 110.f ) );
            for ( int i = 0; i < IM_ARRAYSIZE( faces ); ++i )
            {
                if ( ImAdd::Selectable( faces[i], config.player.face_preset == i ) )
                {
                    config.player.face_preset = i;
                    static const char* ids[] = {
                        "2409898166", "1223111643", "155197736", "2945572033",
                        "2293675506", "479872542", "752273954", "90272641",
                        "90272223", "155288061", "365335647", "893052423", "482618732"
                    };
                    if ( i >= 0 && i < IM_ARRAYSIZE( ids ) )
                        ImStrncpy( config.player.face_id, ids[i], IM_ARRAYSIZE( config.player.face_id ) );
                }
            }
            ImAdd::EndChild( );
            ImGui::InputText( "face id", config.player.face_id, IM_ARRAYSIZE( config.player.face_id ) );
            ImGui::EndDisabled( );

            ImGui::SeparatorText( "clothing" );
            pulse_feature( "clothing changer" );
            ImGui::BeginDisabled( !config.player.clothing_changer );
            ImGui::InputText( "shirt id", config.player.shirt_id, IM_ARRAYSIZE( config.player.shirt_id ) );
            ImGui::InputText( "pants id", config.player.pants_id, IM_ARRAYSIZE( config.player.pants_id ) );
            ImGui::InputText( "tshirt id", config.player.tshirt_id, IM_ARRAYSIZE( config.player.tshirt_id ) );
            ImGui::EndDisabled( );
        }
        else if ( selected_tab == 1 && section == 0 )
        {
            ImGui::SeparatorText( "destination" );
            const char* methods[] = { "mouse", "camera", "cframe" };
            ImAdd::Combo( "method", &config.aimbot.method, methods, IM_ARRAYSIZE( methods ) );
            pulse_feature( "aim method" );
            const char* parts[] = { "head", "upper torso", "lower torso", "root part", "closest bone" };
            ImAdd::Combo( "aim part", &config.aimbot.aim_part, parts, IM_ARRAYSIZE( parts ) );
            pulse_feature( "aim part" );
            const char* targets[] = { "closest to crosshair", "closest distance", "lowest health", "highest health" };
            ImAdd::Combo( "target", &config.aimbot.target_mode, targets, IM_ARRAYSIZE( targets ) );
            pulse_feature( "target mode" );
            const char* styles[] = { "linear", "spring", "exponential", "cubed", "bezier", "humanized" };
            ImAdd::Combo( "style", &config.aimbot.style, styles, IM_ARRAYSIZE( styles ) );

            ImGui::SeparatorText( "feel" );
            ImAdd::SliderFloat( "smoothing", &config.aimbot.smoothing, 1.f, 20.f, -0.1f, "%.1f" );
            pulse_feature( "smoothing" );
            ImAdd::SliderFloat( "range", &config.aimbot.range, 20.f, 600.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "max distance", &config.aimbot.max_distance, 50.f, 5000.f, -1.f, "%.0f" );

            if ( config.aimbot.prediction )
            {
                ImGui::SeparatorText( "prediction" );
                ImAdd::SliderFloat( "pred xz", &config.aimbot.prediction_xz, 0.f, 3.f, -0.01f, "%.2f" );
                ImAdd::SliderFloat( "pred y", &config.aimbot.prediction_y, 0.f, 3.f, -0.01f, "%.2f" );
            }

            if ( config.aimbot.method == 1 || config.aimbot.method == 2 )
            {
                ImGui::SeparatorText( config.aimbot.method == 2 ? "cframe" : "camera" );
                ImAdd::CheckBox( "force scriptable", &config.aimbot.cam_force_scriptable );
                ImAdd::CheckBox( "restore type", &config.aimbot.cam_restore_type );
            }
        }
        else if ( selected_tab == 1 && section == 1 )
        {
            ImGui::SeparatorText( "silent method" );
            const char* parts[] = { "head", "upper torso", "lower torso", "root part", "closest bone" };
            ImAdd::Combo( "aim part", &config.silentaim.aim_part, parts, IM_ARRAYSIZE( parts ) );
            const char* targets[] = { "closest to crosshair", "closest distance", "lowest health", "highest health" };
            ImAdd::Combo( "target", &config.silentaim.target_mode, targets, IM_ARRAYSIZE( targets ) );
            const char* redirects[] = { "from camera", "near target" };
            ImAdd::Combo( "redirect", &config.silentaim.redirect_mode, redirects, IM_ARRAYSIZE( redirects ) );

            ImGui::SeparatorText( "timing" );
            ImAdd::SliderFloat( "range", &config.silentaim.range, 20.f, 600.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "max distance", &config.silentaim.max_distance, 50.f, 5000.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "restore delay", &config.silentaim.restore_delay_ms, 0.f, 200.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "point scale", &config.silentaim.closest_point_scale, 0.1f, 1.f, -0.01f, "%.2f" );

            ImGui::SeparatorText( "visuals" );
            ImAdd::CheckBox( "fov ring", &config.silentaim.fov_visualizer );
            ImAdd::CheckBox( "fov filled", &config.silentaim.fov_filled );
            ImAdd::CheckBox( "fov outline", &config.silentaim.fov_outline );
            ImAdd::SliderFloat( "fov thick", &config.silentaim.fov_thickness, 0.5f, 4.f, -0.1f, "%.1f" );
            ImAdd::ColorEdit4( "fov color", config.silentaim.fov_color );
            ImAdd::CheckBox( "tracer", &config.silentaim.tracer );
            ImAdd::CheckBox( "target dot", &config.silentaim.target_dot );
            ImAdd::SliderFloat( "dot radius", &config.silentaim.target_dot_radius, 1.f, 12.f, -0.1f, "%.1f" );
            ImAdd::ColorEdit4( "dot color", config.silentaim.target_dot_color );
        }
        else if ( selected_tab == 1 && section == 2 )
        {
            ImGui::SeparatorText( "destination" );
            const char* parts[] = { "head", "upper torso", "lower torso", "root part", "closest bone" };
            ImAdd::Combo( "aim part", &config.triggerbot.aim_part, parts, IM_ARRAYSIZE( parts ) );

            ImGui::SeparatorText( "timing" );
            ImAdd::SliderFloat( "delay", &config.triggerbot.delay_ms, 0.f, 300.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "click", &config.triggerbot.click_ms, 10.f, 200.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "hitchance", &config.triggerbot.hitchance, 1.f, 100.f, -1.f, "%.0f" );
            ImAdd::SliderFloat( "max distance", &config.triggerbot.max_distance, 50.f, 5000.f, -1.f, "%.0f" );
        }
        else if ( selected_tab == 3 && section == 0 )
        {
            ImGui::SeparatorText( "presets" );
            world_preset_row( );
            if ( ImAdd::Button( "capture live", ImVec2( -0.1f, 0 ) ) )
                world_lighting::capture( );

            ImGui::SeparatorText( "look" );
            world_color_strip( "fog", config.world.fog_color, config.world.fog_color );

            ImGui::SeparatorText( "status" );
            const auto st = world_lighting::status( );
            world_status_chip( st.resolved ? "lighting ok" : "lighting miss", st.resolved, accent );
            ImGui::SameLine( );
            world_status_chip( st.write_ok ? "write ok" : "write …", st.write_ok, accent );
        }
        else if ( selected_tab == 3 && section == 1 )
        {
            ImGui::SeparatorText( "palette" );
            ImGui::BeginDisabled( !config.world.enabled );
            ImAdd::ColorEdit4( "ambient", config.world.ambient );
            ImAdd::ColorEdit4( "outdoor", config.world.outdoor_ambient );
            ImAdd::ColorEdit4( "shift top", config.world.color_shift_top );
            ImAdd::ColorEdit4( "shift bottom", config.world.color_shift_bottom );
            ImGui::EndDisabled( );

            ImGui::SeparatorText( "look" );
            world_color_strip( "ambient → outdoor", config.world.ambient, config.world.outdoor_ambient );
            world_color_strip( "shift top → bottom", config.world.color_shift_top, config.world.color_shift_bottom );

            ImGui::SeparatorText( "presets" );
            world_preset_row( );
            if ( ImAdd::Button( "capture live", ImVec2( -0.1f, 0 ) ) )
                world_lighting::capture( );

            ImGui::SeparatorText( "status" );
            const auto st = world_lighting::status( );
            world_status_chip( st.resolved ? "lighting ok" : "lighting miss", st.resolved, accent );
            ImGui::SameLine( );
            world_status_chip( st.write_ok ? "write ok" : "write …", st.write_ok, accent );
        }
        else if ( selected_tab == 3 && section == 2 )
        {
            ImGui::SeparatorText( "status" );
            const auto st = world_lighting::status( );
            world_status_chip( st.materials_resolved ? "materials ok" : "materials miss", st.materials_resolved, accent );
        }
        else if ( selected_tab == 3 && section == 3 )
        {
            ImGui::SeparatorText( "sky pack" );
            ImAdd::Combo( "pack", &config.world.skybox_preset, world_lighting::skybox_preset_names( ), world_lighting::skybox_preset_count( ) );
            if ( ImAdd::Button( "apply sky pack", ImVec2( -0.1f, 0 ) ) )
                world_lighting::apply_sky_pack( config.world.skybox_preset );

            ImGui::SeparatorText( "status" );
            const auto st = world_lighting::status( );
            world_status_chip( st.sky_resolved ? "sky ok" : "sky miss", st.sky_resolved, accent );
        }
    }
    ImGui::EndChild( );
    ImGui::Columns( 1 );
    }
    ImGui::EndChild( );
    ImGui::PopStyleVar( );

    float avail_w = ImGui::GetContentRegionAvail( ).x;
    float tab_width = ( avail_w - style.ItemSpacing.x * ( tabCount - 1 ) ) / ( float )tabCount;
    if ( tab_width < 1.0f )
        tab_width = 1.0f;

    static float tab_hl[8] = {};
    const float tab_anim_t = config.theme.animations
        ? ImSaturate( io.DeltaTime / 0.12f )
        : 1.f;
    const ImVec4 tab_idle = ImVec4( 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f );

    for ( int i = 0; i < tabCount; ++i )
    {
        tab_hl[i] = ImLerp( tab_hl[i], ( selected_tab == i ) ? 1.0f : 0.0f, tab_anim_t );
        ImGui::PushID( i );

        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.7f, 0.7f, 0.7f, 0.5f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.1f, 0.1f, 0.1f, 0.5f ) );
        ImGui::PushStyleColor( ImGuiCol_Text, ImLerp( tab_idle, accent, tab_hl[i] ) );

        float this_width = ( i + 1 == tabCount ) ? ImGui::GetContentRegionAvail( ).x : tab_width;
        ImVec2 buttonSize = ImVec2( this_width, tab_bar_height );
        if ( ImGui::Button( tabNames[i], buttonSize ) )
            selected_tab = i;

        if ( tab_hl[i] > 0.01f )
        {
            ImVec2 pos = ImGui::GetItemRectMin( );
            ImVec4 line = accent;
            line.w *= tab_hl[i];
            ImGui::GetWindowDrawList( )->AddLine(
                ImVec2( pos.x, pos.y ),
                ImVec2( pos.x + buttonSize.x, pos.y ),
                ImGui::GetColorU32( line ), 2.0f );
        }

        ImGui::PopStyleColor( 4 );
        if ( i + 1 < tabCount )
            ImGui::SameLine( );
        ImGui::PopID( );
    }

    ImGui::End( );
    ImGui::PopStyleVar( );
    ImGui::PopStyleColor( );
    draw_accent_glow( menu_pos, ImVec2( menu_pos.x + menu_size.x, menu_pos.y + menu_size.y ), accent, 52.0f );
    ImAdd::RenderColorPickers( );
    draw_feature_search_results( &selected_tab, tabNames, tabCount );

    const bool want_esp = ( selected_tab == 0 );
    const bool want_dex_props = false; // explorer is dual-pane in-tab (decompiler-master layout)
    static float dex_props_anim = 0.0f;

    if ( !config.theme.animations )
    {
        esp_anim = want_esp ? 1.f : 0.f;
        dex_props_anim = want_dex_props ? 1.f : 0.f;
    }
    else
    {
        const float target = want_esp ? 1.0f : 0.0f;
        const float duration = want_esp ? 0.28f : 0.22f;
        if ( esp_anim < target )
            esp_anim = ImMin( target, esp_anim + io.DeltaTime / duration );
        else if ( esp_anim > target )
            esp_anim = ImMax( target, esp_anim - io.DeltaTime / duration );

        if ( !want_esp && esp_anim < 0.02f )
            esp_anim = 0.0f;

        const float dex_target = want_dex_props ? 1.0f : 0.0f;
        const float dex_duration = want_dex_props ? 0.28f : 0.22f;
        if ( dex_props_anim < dex_target )
            dex_props_anim = ImMin( dex_target, dex_props_anim + io.DeltaTime / dex_duration );
        else if ( dex_props_anim > dex_target )
            dex_props_anim = ImMax( dex_target, dex_props_anim - io.DeltaTime / dex_duration );
        if ( !want_dex_props && dex_props_anim < 0.02f )
            dex_props_anim = 0.0f;
    }

    if ( esp_anim > 0.0f )
    {
        const float t = smooth_step( esp_anim );
        const float preview_w = get_esp_preview_width( menu_size.y );
        const float preview_gap = 14.0f;
        const float shown_w = preview_w * t;
        if ( shown_w >= 1.0f )
        {
            ImGui::SetNextWindowPos( ImVec2( menu_pos.x + menu_size.x + preview_gap, menu_pos.y ), ImGuiCond_Always );
            ImGui::SetNextWindowSize( ImVec2( shown_w, menu_size.y ), ImGuiCond_Always );
            ImGui::PushStyleColor( ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab] );
            ImGui::Begin( "esp_preview", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs );
            draw_esp_preview( accent, preview_w );
            ImGui::End( );
            ImGui::PopStyleColor( );
        }
    }

    if ( dex_props_anim > 0.0f )
    {
        const float t = smooth_step( dex_props_anim );
        const float panel_w = get_esp_preview_width( menu_size.y );
        const float panel_gap = 14.0f;
        const float shown_w = panel_w * t;
        if ( shown_w >= 1.0f )
        {
            ImGui::SetNextWindowPos( ImVec2( menu_pos.x - shown_w - panel_gap, menu_pos.y ), ImGuiCond_Always );
            ImGui::SetNextWindowSize( ImVec2( shown_w, menu_size.y ), ImGuiCond_Always );
            ImGui::PushStyleColor( ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab] );
            ImGui::Begin( "dex_properties", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove );
            ImGui::BeginChild( "dex_props_body", ImVec2( 0, 0 ), true );
            g->explorer.draw_properties( );
            ImGui::EndChild( );
            ImGui::End( );
            ImGui::PopStyleColor( );
        }
    }

    sync_flags_to_config( );
}
