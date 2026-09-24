#include <include/global.cuh>

#include <src/cheat/overlay/fonts/fonts.cuh>
#include <src/cheat/features/world/world_lighting.cuh>
#include <src/cheat/features/music/feelings_fm.cuh>
#include <src/cheat/features/silentaim/silentaim.cuh>
#include <src/cheat/features/lua/lua_gc.cuh>
#include <src/cheat/features/triggerbot/triggerbot.cuh>
#include <src/cheat/features/notifications/notifications.cuh>
#include <src/cheat/config/config_io.hxx>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <dwmapi.h>
#include <dxgi1_5.h>
#include <mmsystem.h>
#include <thread>

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "dwmapi.lib" )
#pragma comment( lib, "winmm.lib" )

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param );

overlay_c::overlay_c( )
{
    this->m_detail = std::make_unique<detail_t>( );
}

overlay_c::~overlay_c( )
{
    this->destroy_imgui( );
    this->destroy_window( );
    this->destroy_device( );
}

LRESULT CALLBACK overlay_c::wnd_proc( HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param )
{
    if ( ImGui_ImplWin32_WndProcHandler( hwnd, msg, w_param, l_param ) )
        return true;

    switch ( msg )
    {
    case WM_SYSCOMMAND:
        if ( ( w_param & 0xfff0 ) == SC_KEYMENU )
            return 0;
        break;

    case WM_SYSKEYDOWN:
        if ( w_param == VK_F4 )
        {
            DestroyWindow( hwnd );
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage( 0 );
        break;

    case WM_CLOSE:
        return 0;
    }

    return DefWindowProcA( hwnd, msg, w_param, l_param );
}

bool overlay_c::window( )
{
    this->m_detail->window_class.cbSize = sizeof( this->m_detail->window_class );
    this->m_detail->window_class.style = CS_CLASSDC;
    this->m_detail->window_class.lpszClassName = "Feelings.lol";
    this->m_detail->window_class.hInstance = GetModuleHandleA( 0 );
    this->m_detail->window_class.lpfnWndProc = overlay_c::wnd_proc;

    RegisterClassExA( &this->m_detail->window_class );

    this->m_detail->window = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        this->m_detail->window_class.lpszClassName,
        "Feelings.lol(Roblox)",
        WS_POPUP,
        0,
        0,
        2,
        2,
        0,
        0,
        this->m_detail->window_class.hInstance,
        0 );

    if ( !this->m_detail->window )
        return false;

    SetLayeredWindowAttributes( this->m_detail->window, RGB( 0, 0, 0 ), BYTE( 255 ), LWA_ALPHA );

    MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea( this->m_detail->window, &margins );

    ShowWindow( this->m_detail->window, SW_SHOWNA );
    this->m_exstyle = GetWindowLongA( this->m_detail->window, GWL_EXSTYLE );

    return true;
}

bool overlay_c::device( )
{
    // Bitblt discard — flip-discard on a WS_EX_LAYERED HWND composites opaque black.
    this->m_allow_tearing = false;
    this->m_swap_flags = 0;

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    swap_chain_desc.BufferCount = 1;
    swap_chain_desc.BufferDesc.Width = 0;
    swap_chain_desc.BufferDesc.Height = 0;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_desc.OutputWindow = this->m_detail->window;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swap_chain_desc.Windowed = 1;
    swap_chain_desc.Flags = 0;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    D3D_FEATURE_LEVEL feature_level;
    D3D_FEATURE_LEVEL feature_level_list[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    auto create = [&]( D3D_DRIVER_TYPE type ) -> HRESULT
    {
        return D3D11CreateDeviceAndSwapChain(
            nullptr, type, nullptr, 0,
            feature_level_list, 2, D3D11_SDK_VERSION,
            &swap_chain_desc,
            &this->m_detail->swap_chain,
            &this->m_detail->device,
            &feature_level,
            &this->m_detail->device_context );
    };

    HRESULT result = create( D3D_DRIVER_TYPE_HARDWARE );
    if ( result == DXGI_ERROR_UNSUPPORTED )
        result = create( D3D_DRIVER_TYPE_WARP );

    if ( result != S_OK )
        return false;

    IDXGIDevice1* dxgi_dev = nullptr;
    if ( SUCCEEDED( this->m_detail->device->QueryInterface( IID_PPV_ARGS( &dxgi_dev ) ) ) && dxgi_dev )
    {
        dxgi_dev->SetMaximumFrameLatency( 1 );
        dxgi_dev->Release( );
    }

    ID3D11Texture2D* back_buffer = nullptr;
    this->m_detail->swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) );
    if ( !back_buffer )
        return false;

    this->m_detail->device->CreateRenderTargetView( back_buffer, nullptr, &this->m_detail->render_target_view );
    back_buffer->Release( );
    return this->m_detail->render_target_view != nullptr;
}

bool overlay_c::create_imgui( )
{
    ImGui::CreateContext( );
    ImGui::StyleColorsDark( );

    const float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor( MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY ) );

    ImGuiStyle& style = ImGui::GetStyle( );
    ImGuiIO& io = ImGui::GetIO( );
    io.IniFilename = nullptr;

    style.AntiAliasedLines = false;
    style.AntiAliasedLinesUseTex = false;
    style.AntiAliasedFill = false;

    style.ScaleAllSizes( main_scale );
    style.FontScaleDpi = main_scale;

    io.Fonts->AddFontDefault( );

    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    font_cfg.OversampleH = 1;
    font_cfg.OversampleV = 1;
    font_cfg.PixelSnapH = true;
    const float base_px = 13.f * main_scale;

    for ( int i = 0; i < fonts::count; ++i )
        esp_fonts[i] = nullptr;

    esp_fonts[fonts::tahoma_bold] = io.Fonts->AddFontFromMemoryTTF(
        ( void* )fonts::Tahoma_Bold, sizeof( fonts::Tahoma_Bold ), base_px, &font_cfg );

    auto load_file = [&]( fonts::font_id id, const char* path, float size_mul = 1.f ) -> void
    {
        ImFontConfig cfg{};
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        esp_fonts[id] = io.Fonts->AddFontFromFileTTF( path, base_px * size_mul, &cfg );
        if ( !esp_fonts[id] )
            esp_fonts[id] = esp_fonts[fonts::tahoma_bold];
    };

    load_file( fonts::segoe_ui, "C:\\Windows\\Fonts\\segoeui.ttf" );
    load_file( fonts::verdana, "C:\\Windows\\Fonts\\verdana.ttf" );
    load_file( fonts::consolas, "C:\\Windows\\Fonts\\consola.ttf" );
    load_file( fonts::arial, "C:\\Windows\\Fonts\\arial.ttf" );

    {
        ImFontConfig cfg{};
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        esp_fonts[fonts::pixel] = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\courbd.ttf", 11.f * main_scale, &cfg );
        if ( !esp_fonts[fonts::pixel] )
            esp_fonts[fonts::pixel] = io.Fonts->AddFontDefault( );
    }

    esp_font = esp_fonts[fonts::tahoma_bold];

    if ( !ImGui_ImplWin32_Init( this->m_detail->window ) )
        return false;

    if ( !this->m_detail->device || !this->m_detail->device_context )
        return false;

    if ( !ImGui_ImplDX11_Init( this->m_detail->device, this->m_detail->device_context ) )
        return false;

    this->m_imgui = true;
    return true;
}

void overlay_c::destroy_device( )
{
    if ( this->m_detail->render_target_view ) { this->m_detail->render_target_view->Release( ); this->m_detail->render_target_view = nullptr; }
    if ( this->m_detail->swap_chain ) { this->m_detail->swap_chain->Release( ); this->m_detail->swap_chain = nullptr; }
    if ( this->m_detail->device_context ) { this->m_detail->device_context->Release( ); this->m_detail->device_context = nullptr; }
    if ( this->m_detail->device ) { this->m_detail->device->Release( ); this->m_detail->device = nullptr; }
}

void overlay_c::destroy_window( )
{
    if ( this->m_detail->window )
    {
        DestroyWindow( this->m_detail->window );
        this->m_detail->window = nullptr;
    }

    if ( this->m_detail->window_class.lpszClassName )
    {
        UnregisterClassA( this->m_detail->window_class.lpszClassName, this->m_detail->window_class.hInstance );
        this->m_detail->window_class.lpszClassName = nullptr;
    }
}

void overlay_c::destroy_imgui( )
{
    if ( !this->m_imgui )
        return;

    if ( g )
        g->menu.shutdown_esp_preview( );

    ImGui_ImplDX11_Shutdown( );
    ImGui_ImplWin32_Shutdown( );
    ImGui::DestroyContext( );

    this->m_imgui = false;
}

bool overlay_c::resize_buffers( int width, int height )
{
    if ( !this->m_detail->swap_chain || !this->m_detail->device || width < 2 || height < 2 )
        return false;

    this->m_detail->device_context->OMSetRenderTargets( 0, nullptr, nullptr );
    if ( this->m_detail->render_target_view )
    {
        this->m_detail->render_target_view->Release( );
        this->m_detail->render_target_view = nullptr;
    }

    const HRESULT hr = this->m_detail->swap_chain->ResizeBuffers(
        0, ( UINT )width, ( UINT )height, DXGI_FORMAT_UNKNOWN, this->m_swap_flags );
    if ( FAILED( hr ) )
        return false;

    ID3D11Texture2D* back_buffer = nullptr;
    this->m_detail->swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) );
    if ( !back_buffer )
        return false;

    this->m_detail->device->CreateRenderTargetView( back_buffer, nullptr, &this->m_detail->render_target_view );
    back_buffer->Release( );

    D3D11_VIEWPORT vp{};
    vp.Width = ( float )width;
    vp.Height = ( float )height;
    vp.MinDepth = 0.f;
    vp.MaxDepth = 1.f;
    this->m_detail->device_context->RSSetViewports( 1, &vp );
    return this->m_detail->render_target_view != nullptr;
}

void overlay_c::sync_to_game( )
{
    HWND target = sdk::u_visualengine::sync_target( );
    if ( !target || !IsWindow( target ) || IsIconic( target ) )
    {
        sdk::u_visualengine::set_overlay_synced( false );
        return;
    }

    RECT client{};
    if ( !GetClientRect( target, &client ) )
    {
        sdk::u_visualengine::set_overlay_synced( false );
        return;
    }

    POINT tl{ 0, 0 };
    ClientToScreen( target, &tl );

    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    if ( w < 2 || h < 2 )
    {
        sdk::u_visualengine::set_overlay_synced( false );
        return;
    }

    constexpr int k_slack = 2;
    const bool moved = std::abs( this->m_synced_x - tl.x ) > k_slack
        || std::abs( this->m_synced_y - tl.y ) > k_slack;
    const bool resized = this->m_synced_w != w || this->m_synced_h != h;

    if ( moved || resized )
    {
        SetWindowPos(
            this->m_detail->window,
            HWND_TOPMOST,
            tl.x,
            tl.y,
            w,
            h,
            SWP_NOACTIVATE | SWP_NOREDRAW );

        this->m_synced_x = tl.x;
        this->m_synced_y = tl.y;

        if ( resized )
        {
            MARGINS margins{ -1, -1, -1, -1 };
            DwmExtendFrameIntoClientArea( this->m_detail->window, &margins );
            if ( this->resize_buffers( w, h ) )
            {
                this->m_synced_w = w;
                this->m_synced_h = h;
            }
        }
    }

    if ( ImGui::GetCurrentContext( ) )
    {
        ImGuiIO& io = ImGui::GetIO( );
        io.DisplaySize = ImVec2( ( float )w, ( float )h );
    }

    sdk::u_visualengine::set_overlay_synced( true );
}

void overlay_c::start_render( )
{
    MSG msg{};
    while ( PeekMessageA( &msg, nullptr, 0, 0, PM_REMOVE ) )
    {
        if ( msg.message == WM_QUIT )
            this->m_running = false;

        TranslateMessage( &msg );
        DispatchMessageA( &msg );
    }

    this->sync_to_game( );

    static bool last_streamproof = false;
    if ( last_streamproof != config.settings.streamproof )
    {
        last_streamproof = config.settings.streamproof;
        SetWindowDisplayAffinity( this->m_detail->window, last_streamproof ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE );
    }

    static bool last_hide_console = false;
    static bool console_applied = false;
    if ( !console_applied || last_hide_console != config.settings.hide_console )
    {
        console_applied = true;
        last_hide_console = config.settings.hide_console;
        if ( HWND con = GetConsoleWindow( ) )
            ShowWindow( con, last_hide_console ? SW_HIDE : SW_SHOW );
    }

    ImGui_ImplDX11_NewFrame( );
    ImGui_ImplWin32_NewFrame( );
    ImGui::NewFrame( );

    ImGuiStyle& frame_style = ImGui::GetStyle( );
    frame_style.AntiAliasedLines = false;
    frame_style.AntiAliasedLinesUseTex = false;
    frame_style.AntiAliasedFill = false;
    if ( ImDrawList* bg = ImGui::GetBackgroundDrawList( ) )
        bg->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill );
    if ( ImDrawList* fg = ImGui::GetForegroundDrawList( ) )
        fg->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill );

    if ( this->m_synced_w > 1 && this->m_synced_h > 1 )
    {
        ImGuiIO& io = ImGui::GetIO( );
        io.DisplaySize = ImVec2( ( float )this->m_synced_w, ( float )this->m_synced_h );
    }

    if ( GetAsyncKeyState( config.menu_key ) & 1 )
        config.menu_open = !config.menu_open;

    const bool want_input = config.menu_open || feelings_fm::wants_input( );
    constexpr LONG k_base_ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE;
    const LONG want_ex = want_input
        ? ( WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED )
        : ( k_base_ex | WS_EX_TRANSPARENT );

    if ( this->m_exstyle != want_ex )
    {
        SetWindowLongA( this->m_detail->window, GWL_EXSTYLE, want_ex );
        this->m_exstyle = want_ex;
        SetWindowPos(
            this->m_detail->window,
            HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW );
    }

    if ( !want_input )
    {
        ImGuiIO& io = ImGui::GetIO( );
        io.WantCaptureMouse = false;
        io.WantCaptureKeyboard = false;
    }
}

void overlay_c::end_render( )
{
    ImGui::Render( );

    float clear_color[4]{ 0.f, 0.f, 0.f, 0.f };
    this->m_detail->device_context->OMSetRenderTargets( 1, &this->m_detail->render_target_view, nullptr );
    this->m_detail->device_context->ClearRenderTargetView( this->m_detail->render_target_view, clear_color );

    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );

    this->m_detail->swap_chain->Present( config.settings.vsync ? 1 : 0, 0 );
}

auto roblox_or_overlay_focused( HWND overlay_hwnd ) -> bool
{
    HWND game = sdk::u_visualengine::window( );
    HWND fg = GetForegroundWindow( );
    if ( !fg )
        return false;
    if ( game && ( fg == game || GetAncestor( fg, GA_ROOT ) == game ) )
        return true;
    if ( overlay_hwnd && fg == overlay_hwnd )
        return true;
    return false;
}
void overlay_c::render_menu( )
{
    if ( !roblox_or_overlay_focused( this->m_detail->window ) )
        return;
    g->menu.draw( );
}

void overlay_c::render_visuals( )
{
    if ( config.player.freeze_gc && usermode->can_rpm( ) && g->engine.datamodel.load( ) )
    {
        static auto last_gc = std::chrono::steady_clock::now( );
        const auto now = std::chrono::steady_clock::now( );
        if ( now - last_gc >= std::chrono::milliseconds( 250 ) )
        {
            last_gc = now;
            lua_gc::probe( );
            lua_gc::tick( );
        }
    }

    begin_proj_frame( );

    if ( config.aimbot.enabled )
        g->aimbot.run( );

    silentaim::run( );
    if ( config.triggerbot.enabled )
        triggerbot::run( );
    player_mods::run( );

    if ( config.esp.enabled )
        g->esp.draw( );

    if ( !roblox_or_overlay_focused( this->m_detail->window ) )
        return;
    if ( ( config.silentaim.enabled || config.silentaim.fov_visualizer )
         )
        silentaim::draw( );
    player_mods::draw( );
    if ( config.settings.notifications )
        notifications::draw( );
}

void overlay_c::run( )
{
    if ( !this->window( ) )
        return;

    if ( !this->device( ) )
        return;

    if ( !this->create_imgui( ) )
        return;

    g->menu.init( );
    g->menu.init_esp_preview( this->m_detail->device );
    this->m_running = true;
    bool mods_started = false;

    timeBeginPeriod( 1 );

    using clock = std::chrono::steady_clock;
    auto next_frame = clock::now( );

    while ( this->m_running && !( GetAsyncKeyState( VK_END ) & 0x8000 ) )
    {
        config_io::tick( );

        if ( !mods_started  )
        {
            g->engine.start_runtime( );
            player_mods::start( );
            teleport::start( );
            animation_changer::start( );
            avatar_changer::start( );
            mods_started = true;
            g->logger.print( "[mods] runtime + player_mods + teleport + animation + avatar started" );
        }

        const bool combat_hot = config.esp.enabled
            || config.aimbot.enabled
            || config.silentaim.enabled
            || config.triggerbot.enabled
            || config.player.fly
            || config.player.speed
            || config.player.hip_height
            || config.player.gravity
            || config.player.jump
            || config.player.infinite_jump
            || config.player.no_jump_cooldown
            || config.player.spin
            || config.player.noclip
            || config.player.sit
            || config.player.target_strafe
            || config.player.rage_tp
            || config.player.teleport
            || config.player.fling
            || config.player.no_fall
            || config.player.antiaim
            || config.player.desync
            || config.player.fake_lag
            || config.player.freecam
            || config.player.hitbox_expander
            || config.player.freeze_gc
            || config.player.antiafk
            || config.player.state_force
            || config.player.animation_changer
            || config.player.emote_changer
            || config.player.face_changer
            || config.player.clothing_changer;
        const bool overlays_hot = config.menu_open
            || config.settings.watermark
            || config.settings.feelings_fm
            || config.settings.players_list
            || config.settings.notifications;
        const bool world_hot = config.world.enabled
            || config.world.skybox_anim
            || config.world.skybox_pulse_sun;

        if ( !combat_hot && !overlays_hot && !world_hot )
        {
            if ( GetAsyncKeyState( config.menu_key ) & 1 )
                config.menu_open = !config.menu_open;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            next_frame = clock::now( );
            continue;
        }

        this->start_render( );
        if ( world_hot )
            world_lighting::apply( );
        this->render_menu( );
        this->render_visuals( );
        this->end_render( );

        if ( config.settings.vsync )
        {
            next_frame = clock::now( );
            continue;
        }

        int cap = config.settings.fps_cap;
        if ( cap < 60 )
            cap = 60;
        if ( cap > 240 )
            cap = 240;

        const auto frame_time = std::chrono::microseconds( 1000000 / cap );
        next_frame += frame_time;
        const auto now = clock::now( );
        if ( next_frame + frame_time * 2 < now )
            next_frame = now;
        if ( next_frame > now )
            std::this_thread::sleep_until( next_frame );
    }

    timeEndPeriod( 1 );

    if ( mods_started )
    {
        animation_changer::stop( );
        avatar_changer::stop( );
        teleport::stop( );
        player_mods::stop( );
    }
    this->destroy_imgui( );
    this->destroy_window( );
    this->destroy_device( );
}
