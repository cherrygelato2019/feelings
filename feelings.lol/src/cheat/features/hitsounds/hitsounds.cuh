#pragma once

namespace hitsounds
{
    auto count( ) -> int;
    auto name( int index ) -> const char*;
    auto play( int index ) -> void;
    auto play_selected( ) -> void; // uses config.settings.hitsound
}
