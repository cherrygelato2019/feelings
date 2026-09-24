#pragma once

/*
 * meshchams — MeshContentProvider LRU summary
 *
 * MeshContentProvider
 *   +0xE8 → CacheObject*
 * CacheObject
 *   +0x20 → lruCache1*  (SizeEnforced / MemEnforced — REAL cache)
 *   +0x28 → lruCache2*  (deprecated — ignore)
 * lruCache1
 *   +0x08 → sentinel (doubly linked list head)
 *
 * LRU node (~72 bytes):
 *   +0x00 next
 *   +0x08 prev
 *   +0x10 content_id  (MSVC std::string SSO)
 *   +0x30 shared_ptr<FileMeshData>::ptr   (NOT +0x38 control block)
 *   +0x38 control block
 *
 * FileMeshData:
 *   +0x00 vertex_begin / +0x08 vertex_end   (stride 40)
 *   +0x30 face_begin   / +0x38 face_end     (stride 12)
 *   vertex: pos.xyz, normal.xyz, uv, tangent u32, color u32
 *   face:   3x uint32 indices
 *
 * Usage: resolve MeshContentProvider → walk lruCache1 from sentinel→next,
 * match part MeshId to content_id, transform verts by part CFrame/size, draw.
 */

#include <imgui.h>

#include <src/engine/cache/cache.cuh>
#include <src/engine/math/math.cuh>

namespace meshchams
{

auto tick_cache( ) -> void;

auto draw(
    ImDrawList* canvas,
    const cache::player& player,
    const math::vector2_t& ve_dims,
    const math::matrix4_t& viewmatrix,
    const math::vector2_t& offset,
    float sx,
    float sy,
    ImU32 fill_col,
    ImU32 outline_col,
    float thickness ) -> void;

auto apply_highlight( const cache::player& player ) -> void;
auto apply_instance( const cache::player& player ) -> void;

} // namespace meshchams
