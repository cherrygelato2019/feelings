#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

class engine_c
{
public:
    auto init( ) -> void;
    auto start_runtime( ) -> void;
    auto game( ) -> void;
    auto refresh( ) -> void;

    auto rescan( ) -> bool;
    auto clear_bindings( ) -> void;

    std::mutex rescan_mutex;
    std::atomic<bool> runtime_started{ false };

    std::atomic<uintptr_t> datamodel{ 0 };
    std::atomic<uintptr_t> workspace{ 0 };
    std::atomic<uintptr_t> camera{ 0 };
    std::atomic<uintptr_t> players{ 0 };
    std::atomic<uintptr_t> mouse_service{ 0 };
    std::atomic<uintptr_t> client{ 0 };
    std::atomic<uintptr_t> client_team{ 0 };
    std::atomic<uintptr_t> client_model{ 0 };
    std::atomic<int64_t> client_user_id{ 0 };
};
