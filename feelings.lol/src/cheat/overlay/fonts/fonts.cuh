#pragma once

#include <imgui.h>

namespace fonts
{
#include "tahbold.h"

    enum font_id : int
    {
        tahoma_bold = 0,
        segoe_ui,
        verdana,
        consolas,
        arial,
        pixel,
        count
    };

    inline const char* const names[count] = {
        "tahoma bold",
        "segoe ui",
        "verdana",
        "consolas",
        "arial",
        "pixel"
    };

    inline constexpr int size_count = 6;
    inline const char* const size_names[size_count] = {
        "10", "12", "13", "14", "16", "18"
    };
    inline constexpr float size_values[size_count] = {
        10.f, 12.f, 13.f, 14.f, 16.f, 18.f
    };

    inline constexpr float size_min = 8.f;
    inline constexpr float size_max = 24.f;

    inline auto clamp_size( float px ) -> float
    {
        if ( px < size_min )
            return size_min;
        if ( px > size_max )
            return size_max;
        return px;
    }

    inline auto resolve_size( int idx ) -> float
    {
        if ( idx < 0 || idx >= size_count )
            idx = 2;
        return size_values[idx];
    }
}

struct ImFont;
extern ImFont* esp_font;
extern ImFont* esp_fonts[fonts::count];

inline auto resolve_esp_font( int id ) -> ImFont*
{
    if ( id < 0 || id >= fonts::count )
        id = 0;
    if ( esp_fonts[id] )
        return esp_fonts[id];
    if ( esp_font )
        return esp_font;
    return ImGui::GetFont( );
}
