#pragma once

#include <climits>
#include <d3d11.h>
#include <dxgi.h>
#include <memory>
#include <windows.h>

class overlay_c
{
public:
    struct detail_t
    {
        HWND window = nullptr;
        WNDCLASSEXA window_class{};
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* device_context = nullptr;
        IDXGISwapChain* swap_chain = nullptr;
        ID3D11RenderTargetView* render_target_view = nullptr;
    };

    overlay_c( );
    ~overlay_c( );

    bool window( );
    bool device( );
    bool create_imgui( );

    void destroy_device( );
    void destroy_window( );
    void destroy_imgui( );

    void start_render( );
    void end_render( );
    void render_menu( );
    void render_visuals( );

    void run( );

    std::unique_ptr<detail_t> m_detail;
    bool m_running = false;

private:
    static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param );

    void sync_to_game( );
    bool resize_buffers( int width, int height );

    bool m_imgui = false;
    int m_synced_w = 0;
    int m_synced_h = 0;
    int m_synced_x = INT_MIN;
    int m_synced_y = INT_MIN;
    UINT m_swap_flags = 0;
    bool m_allow_tearing = false;
    LONG m_exstyle = 0;
};
