#pragma once

#include <src/engine/math/math.cuh>

#include <string>
#include <vector>

namespace teleport
{

struct waypoint_t
{
    std::string name;
    math::vector3_t pos{};
};

auto start( ) -> void;
auto stop( ) -> void;

auto to_world( const math::vector3_t& world, float height_pad = 0.f ) -> bool;

auto waypoints( ) -> std::vector<waypoint_t>;
auto selected_waypoint( ) -> int;
auto set_selected_waypoint( int index ) -> void;
auto add_waypoint( const char* name, const math::vector3_t& pos ) -> bool;
auto save_current_waypoint( const char* name ) -> bool;
auto remove_waypoint( int index ) -> void;
auto clear_waypoints( ) -> void;
auto teleport_to_waypoint( int index ) -> bool;
auto load_waypoints( ) -> void;
auto save_waypoints( ) -> void;

} // namespace teleport
