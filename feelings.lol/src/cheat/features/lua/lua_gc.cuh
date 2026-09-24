#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lua_gc
{

struct vm_t
{
    uintptr_t L = 0;
    uintptr_t G = 0;
    uint64_t kb = 0;
    uint32_t wrap = 0;
};

struct table_hit_t
{
    uintptr_t table = 0;
    uintptr_t G = 0;
};

auto probe( ) -> bool;
auto reset( ) -> void;
auto tick( ) -> void;

auto set( const char* opt, int arg = -1 ) -> bool;
auto count_kb( ) -> uint64_t;
auto is_running( ) -> bool;

auto vms( ) -> std::vector<vm_t>;
auto find( const char* key, const char* val = nullptr, int cap = 64 ) -> std::vector<table_hit_t>;

}
