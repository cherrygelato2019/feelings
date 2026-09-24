#include <include/global.cuh>

#include <src/cheat/features/combat/combat_state.cuh>
#include <src/cheat/features/hitsounds/hitsounds.cuh>
#include <src/cheat/features/notifications/notifications.cuh>

#include <addons/imgui_addons.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

constexpr int k_max_toasts = 8;
constexpr float k_toast_life = 3.5f;

struct toast_t
{
    std::string text;
    std::chrono::steady_clock::time_point born{};
};

std::mutex g_toast_mutex;
std::vector<toast_t> g_toasts;

std::mutex g_health_mutex;
std::unordered_map<uintptr_t, float> g_last_health;

} // namespace

namespace notifications
{

auto push_hit( const char* name, float dmg, float health_left ) -> void
{
    if ( config.settings.hitsounds )
        hitsounds::play_selected( );

    if ( !config.settings.notifications )
        return;

    char buf[192]{};
    std::snprintf( buf, sizeof( buf ), "hit %s for %.0f dmg  health left %.0f",
        name && name[0] ? name : "?", dmg, health_left );

    std::lock_guard<std::mutex> lock( g_toast_mutex );
    if ( static_cast<int>( g_toasts.size( ) ) >= k_max_toasts )
        g_toasts.erase( g_toasts.begin( ) );

    g_toasts.push_back( { buf, std::chrono::steady_clock::now( ) } );
}

auto push( const char* text ) -> void
{
    if ( !config.settings.notifications )
        return;
    if ( !text || !text[0] )
        return;

    std::lock_guard<std::mutex> lock( g_toast_mutex );
    if ( static_cast<int>( g_toasts.size( ) ) >= k_max_toasts )
        g_toasts.erase( g_toasts.begin( ) );

    g_toasts.push_back( { text, std::chrono::steady_clock::now( ) } );
}

auto draw( ) -> void
{
    if ( !config.settings.notifications )
        return;

    const auto now = std::chrono::steady_clock::now( );
    const ImVec4 accent = ImVec4(
        config.theme.accent[0], config.theme.accent[1], config.theme.accent[2], config.theme.accent[3] );

    std::vector<toast_t> live;
    {
        std::lock_guard<std::mutex> lock( g_toast_mutex );
        g_toasts.erase( std::remove_if( g_toasts.begin( ), g_toasts.end( ),
            [&]( const toast_t& t )
            {
                const float age = std::chrono::duration<float>( now - t.born ).count( );
                return age >= k_toast_life;
            } ), g_toasts.end( ) );
        live = g_toasts;
    }

    if ( live.empty( ) )
        return;

    ImGuiIO& io = ImGui::GetIO( );
    const float pad = 12.f;
    float y = pad;

    for ( int i = static_cast<int>( live.size( ) ) - 1; i >= 0; --i )
    {
        const toast_t& t = live[static_cast<size_t>( i )];
        const float age = std::chrono::duration<float>( now - t.born ).count( );
        float alpha = 1.f;
        if ( age > k_toast_life - 0.6f )
            alpha = std::clamp( ( k_toast_life - age ) / 0.6f, 0.f, 1.f );
        if ( age < 0.15f )
            alpha = std::clamp( age / 0.15f, 0.f, 1.f );

        const ImVec2 text_sz = ImGui::CalcTextSize( t.text.c_str( ) );
        const ImVec2 box_pad{ 10.f, 6.f };
        const ImVec2 box_sz{ text_sz.x + box_pad.x * 2.f, text_sz.y + box_pad.y * 2.f };
        const ImVec2 pos{ io.DisplaySize.x - box_sz.x - pad, y };

        ImDrawList* dl = ImGui::GetForegroundDrawList( );
        const ImU32 bg = ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, 0.80f * alpha ) );
        const ImU32 border = ImGui::GetColorU32( ImVec4( accent.x, accent.y, accent.z, alpha ) );
        const ImU32 text = ImGui::GetColorU32( ImVec4( accent.x, accent.y, accent.z, alpha ) );

        dl->AddRectFilled( pos, ImVec2( pos.x + box_sz.x, pos.y + box_sz.y ), bg, 0.f );
        dl->AddRect( pos, ImVec2( pos.x + box_sz.x, pos.y + box_sz.y ), border, 0.f, 0, 1.5f );
        dl->AddText( ImVec2( pos.x + box_pad.x, pos.y + box_pad.y ), text, t.text.c_str( ) );

        y += box_sz.y + 6.f;
    }
}

} // namespace notifications

namespace combat
{

auto track_damage( uintptr_t addr, const char* name, float health ) -> void
{
    if ( addr == 0 )
        return;

    float prev = health;
    bool had = false;
    {
        std::lock_guard<std::mutex> lock( g_health_mutex );
        auto it = g_last_health.find( addr );
        if ( it != g_last_health.end( ) )
        {
            prev = it->second;
            had = true;
        }
        g_last_health[addr] = health;
    }

    if ( !had )
        return;

    const float drop = prev - health;
    if ( drop > 0.5f )
        notifications::push_hit( name, drop, health );
}

} // namespace combat
