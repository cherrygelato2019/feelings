#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sdk
{

class u_instance
{
public:
    u_instance( ) : m_address( 0 ) {}
    u_instance( uintptr_t address ) : m_address( address ) {}

    explicit operator bool( ) const { return m_address != 0; }

    auto address( ) const -> uintptr_t { return m_address; }

    auto name( ) const -> std::string;
    auto class_name( ) const -> std::string;
    auto parent( ) const -> u_instance;

    auto is_a( const char* classname ) const -> bool;

    auto get_children( ) const -> std::vector<u_instance>;
    auto find_first_child( const char* name ) const -> u_instance;
    auto find_first_child_of_class( const char* classname ) const -> u_instance;
    auto find_first_descendant( const char* name, int max_depth = 8 ) const -> u_instance;

    static auto resolve_class_name( uintptr_t descriptor ) -> std::string;
    static auto clear_class_cache( ) -> void;

protected:
    uintptr_t m_address;
};

template<typename t>
inline auto as( const u_instance& inst ) -> t
{
    return t( inst.address( ) );
}

template<typename t>
inline auto get_child_as( const u_instance& parent, const char* name ) -> t
{
    return t( parent.find_first_child( name ).address( ) );
}

} // namespace sdk
