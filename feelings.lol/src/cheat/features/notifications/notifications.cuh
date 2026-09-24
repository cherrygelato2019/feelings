#pragma once

namespace notifications
{

auto push_hit( const char* name, float dmg, float health_left ) -> void;
auto push( const char* text ) -> void;
auto draw( ) -> void;

} // namespace notifications
