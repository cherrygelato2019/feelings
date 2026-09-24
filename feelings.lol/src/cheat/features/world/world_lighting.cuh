#pragma once

#include <cstdint>

namespace world_lighting
{

struct status_t
{
    bool resolved = false;
    uintptr_t address = 0;
    float live_brightness = 0.f;
    float live_clock = 0.f;
    float live_fog_end = 0.f;
    bool write_ok = false;

    bool sky_resolved = false;
    uintptr_t sky_address = 0;
    float sky_yaw = 0.f;

    bool materials_resolved = false;
    uintptr_t materials_address = 0;
};

auto apply( ) -> void;
auto capture( ) -> bool;
auto apply_preset( int id ) -> void;
auto apply_sky_pack( int id ) -> bool;
auto status( ) -> status_t;
auto skybox_preset_names( ) -> const char* const*;
auto skybox_preset_count( ) -> int;

}
