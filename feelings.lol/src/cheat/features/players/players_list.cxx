#include <include/global.cuh>
#include <src/cheat/features/esp/helpers.hxx>
#include <src/cheat/features/players/players_list.cuh>
#include <src/cheat/features/player/teleport.cuh>

#include <addons/imgui_addons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <d3d11.h>
#include <winhttp.h>
#pragma comment( lib, "winhttp.lib" )

#include <stb_image.h>

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace players_list
{
namespace
{

uintptr_t g_selected_addr = 0;
uintptr_t g_spectate_restore = 0;
bool g_spectating = false;
int g_option = 0;

enum class avatar_state : int { idle = 0, queued, loading, ready, failed };

struct avatar_entry_t
{
    avatar_state state = avatar_state::idle;
    ID3D11ShaderResourceView* srv = nullptr;
    std::vector<std::uint8_t> pending_png;
    bool upload_ready = false;
};

std::mutex g_avatar_mtx;
std::unordered_map<std::int64_t, avatar_entry_t> g_avatars;
std::queue<std::int64_t> g_avatar_q;
std::atomic<bool> g_avatar_worker{ false };

auto http_get_bin( const std::wstring& host, const std::wstring& path, bool https = true ) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    HINTERNET session = WinHttpOpen( L"Feelings.lol/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
    if ( !session )
        return out;

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption( session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof( protocols ) );
    WinHttpSetTimeouts( session, 8000, 8000, 8000, 8000 );

    const INTERNET_PORT port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET connect = WinHttpConnect( session, host.c_str( ), port, 0 );
    if ( !connect )
    {
        WinHttpCloseHandle( session );
        return out;
    }

    const DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest( connect, L"GET", path.c_str( ),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags );
    if ( !request )
    {
        WinHttpCloseHandle( connect );
        WinHttpCloseHandle( session );
        return out;
    }

    WinHttpAddRequestHeaders( request,
        L"User-Agent: Feelings.lol/1.0\r\nAccept: */*\r\n",
        ( ULONG )-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE );

    BOOL ok = WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0 );
    if ( ok )
        ok = WinHttpReceiveResponse( request, nullptr );

    if ( ok )
    {
        for ( ;; )
        {
            DWORD avail = 0;
            if ( !WinHttpQueryDataAvailable( request, &avail ) || avail == 0 )
                break;
            const size_t off = out.size( );
            out.resize( off + avail );
            DWORD read = 0;
            if ( !WinHttpReadData( request, out.data( ) + off, avail, &read ) || read == 0 )
            {
                out.resize( off );
                break;
            }
            out.resize( off + read );
        }
    }

    WinHttpCloseHandle( request );
    WinHttpCloseHandle( connect );
    WinHttpCloseHandle( session );
    return out;
}

auto http_get_url( const std::string& url ) -> std::vector<std::uint8_t>
{
    if ( url.size( ) < 8 )
        return {};

    std::wstring wide( url.begin( ), url.end( ) );
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof( uc );
    wchar_t host[256]{};
    wchar_t path[2048]{};
    wchar_t extra[1024]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 1024;
    if ( !WinHttpCrackUrl( wide.c_str( ), 0, 0, &uc ) )
        return {};

    const bool https = ( uc.nScheme == INTERNET_SCHEME_HTTPS );
    std::wstring path_q = path;
    if ( uc.dwExtraInfoLength && uc.lpszExtraInfo )
        path_q += uc.lpszExtraInfo;
    return http_get_bin( host, path_q, https );
}

auto extract_json_string( const std::string& json, const char* key ) -> std::string
{
    const std::string needle = std::string( "\"" ) + key + "\":\"";
    const size_t at = json.find( needle );
    if ( at == std::string::npos )
        return {};
    size_t i = at + needle.size( );
    std::string out;
    while ( i < json.size( ) && json[i] != '"' )
    {
        if ( json[i] == '\\' && i + 1 < json.size( ) )
        {
            out.push_back( json[i + 1] );
            i += 2;
            continue;
        }
        out.push_back( json[i++] );
    }
    return out;
}

auto fetch_headshot_png( std::int64_t user_id ) -> std::vector<std::uint8_t>
{
    if ( user_id <= 0 )
        return {};

    wchar_t path[256];
    _snwprintf_s( path, _TRUNCATE,
        L"/v1/users/avatar-headshot?userIds=%lld&size=150x150&format=Png&isCircular=false",
        ( long long )user_id );

    for ( int attempt = 0; attempt < 4; ++attempt )
    {
        const auto meta = http_get_bin( L"thumbnails.roblox.com", path, true );
        if ( meta.empty( ) )
        {
            Sleep( 200 );
            continue;
        }

        const std::string json( meta.begin( ), meta.end( ) );
        const std::string state = extract_json_string( json, "state" );
        const std::string image_url = extract_json_string( json, "imageUrl" );
        if ( !image_url.empty( ) && ( state.empty( ) || state == "Completed" ) )
            return http_get_url( image_url );

        // Pending / Error — brief wait then retry (same as GetUserThumbnailAsync).
        Sleep( 250 + attempt * 150 );
    }
    return {};
}

auto create_srv_from_png( ID3D11Device* device, const std::vector<std::uint8_t>& png ) -> ID3D11ShaderResourceView*
{
    if ( !device || png.empty( ) )
        return nullptr;

    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load_from_memory( png.data( ), ( int )png.size( ), &w, &h, &ch, 4 );
    if ( !pixels || w <= 0 || h <= 0 )
    {
        if ( pixels )
            stbi_image_free( pixels );
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = ( UINT )w;
    desc.Height = ( UINT )h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels;
    init.SysMemPitch = ( UINT )( w * 4 );

    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if ( SUCCEEDED( device->CreateTexture2D( &desc, &init, &tex ) ) )
        device->CreateShaderResourceView( tex, nullptr, &srv );
    if ( tex )
        tex->Release( );
    stbi_image_free( pixels );
    return srv;
}

auto avatar_worker_loop( ) -> void
{
    for ( ;; )
    {
        std::int64_t uid = 0;
        {
            std::lock_guard<std::mutex> lk( g_avatar_mtx );
            if ( g_avatar_q.empty( ) )
            {
                g_avatar_worker.store( false );
                return;
            }
            uid = g_avatar_q.front( );
            g_avatar_q.pop( );
            g_avatars[uid].state = avatar_state::loading;
        }

        auto png = fetch_headshot_png( uid );

        std::lock_guard<std::mutex> lk( g_avatar_mtx );
        auto& e = g_avatars[uid];
        if ( png.empty( ) )
        {
            e.state = avatar_state::failed;
            e.upload_ready = false;
            e.pending_png.clear( );
        }
        else
        {
            e.pending_png = std::move( png );
            e.upload_ready = true;
        }
    }
}

auto request_avatar( std::int64_t user_id ) -> void
{
    if ( user_id <= 0 )
        return;

    bool start = false;
    {
        std::lock_guard<std::mutex> lk( g_avatar_mtx );
        auto& e = g_avatars[user_id];
        if ( e.state != avatar_state::idle && e.state != avatar_state::failed )
            return;
        e.state = avatar_state::queued;
        g_avatar_q.push( user_id );
        if ( !g_avatar_worker.exchange( true ) )
            start = true;
    }
    if ( start )
        std::thread( avatar_worker_loop ).detach( );
}

auto avatar_srv( std::int64_t user_id ) -> ID3D11ShaderResourceView*
{
    if ( user_id <= 0 )
        return nullptr;

    request_avatar( user_id );

    ID3D11Device* device = ( g && g->overlay.m_detail ) ? g->overlay.m_detail->device : nullptr;

    std::lock_guard<std::mutex> lk( g_avatar_mtx );
    auto it = g_avatars.find( user_id );
    if ( it == g_avatars.end( ) )
        return nullptr;

    auto& e = it->second;
    if ( e.upload_ready && device && !e.pending_png.empty( ) )
    {
        if ( e.srv )
        {
            e.srv->Release( );
            e.srv = nullptr;
        }
        e.srv = create_srv_from_png( device, e.pending_png );
        e.pending_png.clear( );
        e.upload_ready = false;
        e.state = e.srv ? avatar_state::ready : avatar_state::failed;
    }
    return e.srv;
}

auto draw_avatar( const ImVec2& size, std::int64_t user_id, const char* name, const ImVec4& accent ) -> void
{
    const ImVec2 p0 = ImGui::GetCursorScreenPos( );
    ImDrawList* dl = ImGui::GetWindowDrawList( );
    const ImU32 border = ImGui::GetColorU32( accent );

    if ( ID3D11ShaderResourceView* srv = avatar_srv( user_id ) )
    {
        dl->AddImage( ( ImTextureID )srv, p0, ImVec2( p0.x + size.x, p0.y + size.y ) );
        dl->AddRect( p0, ImVec2( p0.x + size.x, p0.y + size.y ), border, 0.f, 0, 1.f );
        ImGui::Dummy( size );
        return;
    }

    const ImU32 bg = ImGui::GetColorU32( ImVec4( 0.12f, 0.12f, 0.14f, 1.f ) );
    dl->AddRectFilled( p0, ImVec2( p0.x + size.x, p0.y + size.y ), bg );
    dl->AddRect( p0, ImVec2( p0.x + size.x, p0.y + size.y ), border, 0.f, 0, 1.f );

    char letter[2] = { '?', 0 };
    if ( name && name[0] )
        letter[0] = ( char )toupper( ( unsigned char )name[0] );
    const ImVec2 ts = ImGui::CalcTextSize( letter );
    dl->AddText(
        ImVec2( p0.x + ( size.x - ts.x ) * 0.5f, p0.y + ( size.y - ts.y ) * 0.5f ),
        border, letter );
    ImGui::Dummy( size );
}

auto status_for( const cache::player& p ) -> const char*
{
    if ( p.is_npc )
        return "npc";
    if ( p.same_team( 0 ) || p.team == 1 )
        return "teammate";
    if ( !p.state.empty( ) )
        return p.state.c_str( );
    if ( p.team == 2 )
        return "enemy";
    return "none";
}

auto team_label( const cache::player& p ) -> std::string
{
    if ( !p.team_name.empty( ) )
        return p.team_name;
    if ( p.same_team( 0 ) || p.team == 1 )
        return "teammate";
    if ( p.team == 2 )
        return "enemy";
    return "none";
}

auto find_selected( const std::shared_ptr<std::vector<cache::player>>& players ) -> const cache::player*
{
    if ( !players || !g_selected_addr )
        return nullptr;
    for ( const auto& p : *players )
    {
        if ( p.address == g_selected_addr || p.character == g_selected_addr )
            return &p;
    }
    return nullptr;
}

auto root_world_pos( const cache::player& p ) -> math::vector3_t
{
    for ( const auto& b : p.bones )
    {
        if ( ( b.name == "HumanoidRootPart" || b.name == "Torso" || b.name == "UpperTorso" || b.part == p.root_part )
            && b.position.is_valid( ) )
            return b.position;
    }
    for ( const auto& b : p.bones )
    {
        if ( b.position.is_valid( ) )
            return b.position;
    }
    return math::vector3_t::nan( );
}

auto spectate_player( const cache::player& p ) -> void
{
    if ( !g->engine.camera )
        return;

    sdk::u_camera cam( g->engine.camera );
    if ( !cam )
        return;

    const uintptr_t subject = p.humanoid ? p.humanoid : p.character;
    if ( !subject )
        return;

    if ( !g_spectating )
    {
        const auto cur = cam.camera_subject( );
        g_spectate_restore = cur ? cur.address( ) : 0;
        g_spectating = true;
    }

    cam.set_camera_subject( subject );
    g->logger.print( "[players] spectate '%s'", p.name.c_str( ) );
}

auto stop_spectate( ) -> void
{
    if ( !g_spectating || !g->engine.camera )
        return;

    sdk::u_camera cam( g->engine.camera );
    if ( cam && g_spectate_restore )
        cam.set_camera_subject( g_spectate_restore );

    g_spectating = false;
    g_spectate_restore = 0;
    g->logger.print( "[players] spectate stopped" );
}

auto teleport_to( const cache::player& p ) -> void
{
    const math::vector3_t dest = root_world_pos( p );
    if ( !dest.is_valid( ) )
    {
        g->logger.print( "[players] teleport failed — no position for '%s'", p.name.c_str( ) );
        return;
    }

    if ( !teleport::to_world( dest, 3.f ) )
    {
        g->logger.print( "[players] teleport failed — no local root" );
        return;
    }
    g->logger.print( "[players] teleport -> '%s'", p.name.c_str( ) );
}

auto run_option( const cache::player& p ) -> void
{
    char buf[128];
    switch ( g_option )
    {
    case 1:
        ImGui::SetClipboardText( !p.display_name.empty( ) ? p.display_name.c_str( ) : p.name.c_str( ) );
        break;
    case 2:
        ImFormatString( buf, IM_ARRAYSIZE( buf ), "%lld", ( long long )p.user_id );
        ImGui::SetClipboardText( buf );
        break;
    case 3:
        ImFormatString( buf, IM_ARRAYSIZE( buf ), "0x%llx",
            ( unsigned long long )( p.character ? p.character : p.address ) );
        ImGui::SetClipboardText( buf );
        break;
    default:
        break;
    }
}

} // namespace

auto draw( const ImVec4& accent ) -> void
{
    if ( !config.menu_open || !config.settings.players_list )
        return;

    ImGuiStyle& style = ImGui::GetStyle( );
    static ImVec2 win_pos = ImVec2( 10.f, 140.f );

    ImGui::SetNextWindowPos( win_pos, ImGuiCond_Once );
    ImGui::SetNextWindowSize( ImVec2( 400.f, 440.f ), ImGuiCond_Once );
    ImGui::SetNextWindowSizeConstraints( ImVec2( 320.f, 280.f ), ImVec2( 640.f, 820.f ) );
    ImGui::SetNextWindowBgAlpha( 0.80f );

    ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0, 0, 0, 0.80f ) );
    ImGui::PushStyleColor( ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab] );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0, 0, 0, 0.35f ) );
    ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( accent.x, accent.y, accent.z, 0.28f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( accent.x, accent.y, accent.z, 0.40f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( accent.x, accent.y, accent.z, 0.55f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.5f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10.f, 6.f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8.f, 5.f ) );

    ImGui::Begin( "players", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing );

    if ( ImGui::IsWindowHovered( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left )
        && ImGui::GetIO( ).MousePos.y < ImGui::GetWindowPos( ).y + 28.f )
    {
        win_pos.x += ImGui::GetIO( ).MouseDelta.x;
        win_pos.y += ImGui::GetIO( ).MouseDelta.y;
        ImGui::SetWindowPos( win_pos );
    }
    win_pos = ImGui::GetWindowPos( );

    ImGui::PushStyleColor( ImGuiCol_Text, accent );
    ImAdd::TextUnformatted( "Players" );
    ImGui::PopStyleColor( );

    const ImVec2 title_max = ImGui::GetItemRectMax( );
    const ImVec2 cur_pos = ImGui::GetWindowPos( );
    const float win_w = ImGui::GetWindowSize( ).x;
    ImGui::GetWindowDrawList( )->AddLine(
        ImVec2( cur_pos.x + 10.f, title_max.y + 4.f ),
        ImVec2( cur_pos.x + win_w - 10.f, title_max.y + 4.f ),
        ImGui::GetColorU32( accent ), 1.5f );
    ImGui::Dummy( ImVec2( 0.f, 7.f ) );

    ImAdd::Text( ImVec4( 0.5f, 0.5f, 0.5f, 1.f ), "options" );

    auto players = g->cache.snapshot_players( );
    const size_t count = players ? players->size( ) : 0;

    const float list_h = ImMax( 120.f, ImGui::GetContentRegionAvail( ).y - 120.f );
    ImGui::BeginChild( "players_rows", ImVec2( 0.f, list_h ), true );

    const float w = ImGui::GetContentRegionAvail( ).x;
    const float col_name = w * 0.50f;
    const float col_team = w * 0.28f;

    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( accent.x, accent.y, accent.z, 0.75f ) );
    ImAdd::TextUnformatted( "name" );
    ImGui::SameLine( col_name );
    ImAdd::TextUnformatted( "team" );
    ImGui::SameLine( col_name + col_team );
    ImAdd::TextUnformatted( "status" );
    ImGui::PopStyleColor( );
    ImGui::Separator( );

    if ( !players || count == 0 )
    {
        ImAdd::Text( ImVec4( 0.5f, 0.5f, 0.5f, 1.f ), "no players cached" );
    }
    else
    {
        bool still_selected = false;
        for ( const auto& p : *players )
        {
            const uintptr_t id = p.address ? p.address : p.character;
            if ( id == g_selected_addr )
                still_selected = true;
            if ( p.user_id > 0 )
                request_avatar( p.user_id );
        }
        if ( !still_selected )
            g_selected_addr = 0;

        for ( size_t i = 0; i < count; ++i )
        {
            const auto& p = ( *players )[i];
            const uintptr_t id = p.address ? p.address : p.character;
            const bool selected = ( id != 0 && id == g_selected_addr );

            const std::string team = team_label( p );
            const char* status = status_for( p );
            const char* label = !p.display_name.empty( ) ? p.display_name.c_str( )
                : ( !p.name.empty( ) ? p.name.c_str( ) : "?" );

            ImGui::PushID( ( int )i );

            const float row_h = ImGui::GetTextLineHeight( ) + 8.f;
            const float row_w = ImGui::GetContentRegionAvail( ).x;
            const ImVec2 row0 = ImGui::GetCursorScreenPos( );

            // Full-width hit target — ImAdd::Selectable centers text which looked broken.
            if ( ImGui::InvisibleButton( "##plrow", ImVec2( row_w, row_h ) ) )
                g_selected_addr = id;
            const bool hovered = ImGui::IsItemHovered( );

            ImDrawList* dl = ImGui::GetWindowDrawList( );
            if ( selected || hovered )
            {
                const float a = selected ? 0.40f : 0.22f;
                dl->AddRectFilled(
                    row0,
                    ImVec2( row0.x + row_w, row0.y + row_h ),
                    ImGui::GetColorU32( ImVec4( accent.x, accent.y, accent.z, a ) ) );
            }

            const float ty = row0.y + ( row_h - ImGui::GetTextLineHeight( ) ) * 0.5f;
            const ImU32 col = selected
                ? ImGui::GetColorU32( accent )
                : ImGui::GetColorU32( ImGuiCol_Text );
            // Clip name so team/status stay readable; whole row still selects.
            const ImVec2 name_max( row0.x + col_name - 8.f, row0.y + row_h );
            dl->PushClipRect( row0, name_max, true );
            dl->AddText( ImVec2( row0.x + 4.f, ty ), col, label );
            dl->PopClipRect( );
            dl->AddText( ImVec2( row0.x + col_name, ty ), col, team.c_str( ) );
            dl->AddText( ImVec2( row0.x + col_name + col_team, ty ), col, status );

            ImGui::PopID( );
        }
    }

    ImGui::EndChild( );


    const cache::player* sel = find_selected( players );
    ImGui::BeginChild( "players_card", ImVec2( 0.f, 0.f ), true );

    const float avatar = 52.f;
    if ( sel )
    {
        const char* name = !sel->display_name.empty( ) ? sel->display_name.c_str( ) : sel->name.c_str( );
        draw_avatar( ImVec2( avatar, avatar ), sel->user_id, name, accent );
        ImGui::SameLine( );

        ImGui::BeginGroup( );
        ImAdd::Text( "name: %s", name && name[0] ? name : "?" );
        ImAdd::Text( "user id: %lld", ( long long )sel->user_id );
        ImAdd::Text( "character: 0x%llx", ( unsigned long long )( sel->character ? sel->character : sel->address ) );
        ImGui::EndGroup( );

        ImGui::SameLine( ImMax( avatar + 160.f, ImGui::GetWindowContentRegionMax( ).x - 140.f ) );
        ImGui::BeginGroup( );
        const char* options[] = { "none", "copy name", "copy user id", "copy character" };
        ImGui::SetNextItemWidth( -1.f );
        ImAdd::Combo( "##pl_opt", &g_option, options, IM_ARRAYSIZE( options ) );
        if ( g_option > 0 )
        {
            run_option( *sel );
            g_option = 0;
        }

        if ( ImAdd::Button( g_spectating ? "unspectate" : "spectate", ImVec2( -1.f, 0 ) ) )
        {
            if ( g_spectating )
                stop_spectate( );
            else
                spectate_player( *sel );
        }
        if ( ImAdd::Button( "teleport", ImVec2( -1.f, 0 ) ) )
            teleport_to( *sel );
        ImGui::EndGroup( );
    }
    else
    {
        ImAdd::Text( ImVec4( 0.5f, 0.5f, 0.5f, 1.f ), "select a player" );
        if ( g_spectating && ImAdd::Button( "unspectate", ImVec2( 120.f, 0 ) ) )
            stop_spectate( );
    }

    ImGui::EndChild( );

    const ImVec2 wmin = ImGui::GetWindowPos( );
    const ImVec2 wsz = ImGui::GetWindowSize( );
    ImGui::End( );

    ImGui::PopStyleVar( 4 );
    ImGui::PopStyleColor( 6 );

    {
        const float pulse = 0.70f + 0.30f * ( 0.5f * ( sinf( ( float )ImGui::GetTime( ) * 2.1f ) + 1.0f ) );
        ImVec4 c = accent;
        c.w = 0.22f * pulse;
        add_shaded_glow(
            ImGui::GetBackgroundDrawList( ),
            wmin,
            ImVec2( wmin.x + wsz.x, wmin.y + wsz.y ),
            ImGui::GetColorU32( c ),
            36.f );
    }
}

} // namespace players_list
