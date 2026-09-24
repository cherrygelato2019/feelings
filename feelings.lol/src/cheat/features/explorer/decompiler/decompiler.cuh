#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace script_decompiler
{

enum script_type : int
{
    local_script = 0,
    module_script = 1,
    script = 2,
};

enum class backend : int
{
    lunaux = 0,
    konstant = 1,
};

struct result_t
{
    std::string title;
    std::string code;
    uintptr_t script = 0;
    bool disasm = false;
};

// Set while a decompile/disasm worker is chewing RPM so ESP can use cached poses.
extern std::atomic<bool> g_busy;

auto decompile( uintptr_t script, script_type type, backend be = backend::lunaux ) -> bool;
auto disassemble( uintptr_t script, script_type type ) -> bool;

auto results( ) -> std::vector<result_t>&;
auto disasm_results( ) -> std::vector<result_t>&;
auto clear_results( ) -> void;

auto type_from_class( const std::string& class_name ) -> script_type;
auto is_script_class( const std::string& class_name ) -> bool;

}
