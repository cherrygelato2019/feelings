#pragma once

#include <atomic>
#include <cstdint>

namespace combat
{

inline std::atomic<uintptr_t> aim_sticky{ 0 };
inline std::atomic<uintptr_t> silent_sticky{ 0 };
inline std::atomic<uintptr_t> last_target{ 0 };

auto track_damage( uintptr_t addr, const char* name, float health ) -> void;

}
