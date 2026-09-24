#pragma once

#include <include/global.cuh>

#include <cstdint>

namespace stealth
{

// Force CameraType::Scriptable once, remember previous type for restore.
inline auto ensure_camera_scriptable(
    const sdk::u_camera& cam,
    bool& overridden,
    uintptr_t& saved_addr,
    int& saved_type ) -> void
{
    if ( !cam )
        return;

    const uintptr_t addr = cam.address( );
    if ( !overridden || saved_addr != addr )
    {
        saved_type = cam.camera_type( );
        saved_addr = addr;
        overridden = true;
    }

    if ( cam.camera_type( ) != offsets::camera_type::scriptable )
        cam.set_camera_type( offsets::camera_type::scriptable );
}

} // namespace stealth
