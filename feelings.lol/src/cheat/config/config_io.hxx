#pragma once

#include <string>
#include <vector>

namespace config_io
{
constexpr int k_version = 2;

auto dir( ) -> std::string;
auto current_name( ) -> std::string;
auto last_name( ) -> std::string;
auto last_status( ) -> std::string;
auto list( ) -> std::vector<std::string>;

auto save( const char* name ) -> bool;
auto load( const char* name ) -> bool;
auto remove( const char* name ) -> bool;
auto copy( const char* from, const char* to ) -> bool;
auto reset( ) -> void;
auto load_last( ) -> bool;
auto tick( ) -> void;
}
