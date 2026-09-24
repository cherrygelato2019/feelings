#pragma once

#include <imgui.h>

#include <src/engine/cache/cache.cuh>
#include <src/engine/math/math.cuh>

auto clipper_mesh_chams(
    ImDrawList* canvas,
    const cache::player& player,
    const math::vector2_t& dimensions,
    const math::matrix4_t& viewmatrix,
    int style,
    ImU32 fill_col,
    ImU32 outline_col,
    float thickness ) -> void;
