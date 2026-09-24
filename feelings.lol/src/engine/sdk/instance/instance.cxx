#include <include/global.cuh>

#include <cstring>
#include <shared_mutex>
#include <unordered_map>

namespace sdk
{

namespace
{
    std::shared_mutex g_class_cache_mutex;
    std::unordered_map<uintptr_t, std::string> g_class_cache;

    std::shared_mutex g_desc_cache_mutex;
    std::unordered_map<uintptr_t, uintptr_t> g_desc_cache;

    auto class_descriptor_for( uintptr_t address ) -> uintptr_t
    {
        if ( !address )
            return 0;

        {
            std::shared_lock<std::shared_mutex> lock( g_desc_cache_mutex );
            auto it = g_desc_cache.find( address );
            if ( it != g_desc_cache.end( ) )
                return it->second;
        }

        uintptr_t desc = usermode->read<uintptr_t>( address + offsets::instance::class_descriptor );
        if ( desc )
        {
            std::unique_lock<std::shared_mutex> lock( g_desc_cache_mutex );
            g_desc_cache[address] = desc;
        }
        return desc;
    }
}

auto u_instance::name( ) const -> std::string
{
    if ( !m_address )
        return { };

    static constexpr uintptr_t k_name_offs[] = {
        offsets::instance::name_container,
        0x70,
        0xb0,
        0x48,
    };

    for ( uintptr_t off : k_name_offs )
    {
        uintptr_t ptr = usermode->read<uintptr_t>( m_address + off );
        if ( usermode->is_valid( ptr ) )
        {
            std::string s = usermode->read_string( ptr );
            if ( !s.empty( ) && s.size( ) < 256 )
                return s;
        }

        std::string embedded = usermode->read_string( m_address + off );
        if ( !embedded.empty( ) && embedded.size( ) < 256 )
            return embedded;
    }

    return { };
}

auto u_instance::resolve_class_name( uintptr_t descriptor ) -> std::string
{
    if ( !descriptor )
        return { };

    {
        std::shared_lock<std::shared_mutex> lock( g_class_cache_mutex );
        auto it = g_class_cache.find( descriptor );
        if ( it != g_class_cache.end( ) )
            return it->second;
    }

    uintptr_t class_ptr = usermode->read<uintptr_t>( descriptor + offsets::instance::class_name );
    std::string cn = usermode->read_string( class_ptr );

    {
        std::unique_lock<std::shared_mutex> lock( g_class_cache_mutex );
        g_class_cache[descriptor] = cn;
    }
    return cn;
}

auto u_instance::clear_class_cache( ) -> void
{
    {
        std::unique_lock<std::shared_mutex> lock( g_class_cache_mutex );
        g_class_cache.clear( );
    }
    {
        std::unique_lock<std::shared_mutex> lock( g_desc_cache_mutex );
        g_desc_cache.clear( );
    }
}

auto u_instance::class_name( ) const -> std::string
{
    return resolve_class_name( class_descriptor_for( m_address ) );
}

auto u_instance::parent( ) const -> u_instance
{
    if ( !m_address )
        return u_instance( );

    static constexpr uintptr_t k_parent_offs[] = {
        offsets::instance::parent,
        0x68,
        0x70,
        0x60,
    };

    for ( uintptr_t off : k_parent_offs )
    {
        const uintptr_t p = usermode->read<uintptr_t>( m_address + off );
        if ( !p || p < 0x10000 || p == m_address || !usermode->is_valid( p ) )
            continue;
        const uintptr_t desc = usermode->read<uintptr_t>( p + offsets::instance::class_descriptor );
        if ( desc && usermode->is_valid( desc ) )
            return u_instance( p );
    }
    return u_instance( );
}

auto u_instance::is_a( const char* classname ) const -> bool
{
    return this->class_name( ) == classname;
}

auto u_instance::get_children( ) const -> std::vector<u_instance>
{
    std::vector<u_instance> out;
    if ( !m_address || !usermode->is_valid( m_address ) )
        return out;

    auto try_list = [&]( uintptr_t child_ptr ) -> bool
    {
        if ( !usermode->is_valid( child_ptr ) )
            return false;

        uintptr_t start = usermode->read<uintptr_t>( child_ptr );
        uintptr_t end = usermode->read<uintptr_t>( child_ptr + offsets::instance::children_end );
        if ( !start || !end || start >= end )
        {
            // Some builds store end at +0x10
            end = usermode->read<uintptr_t>( child_ptr + 0x10 );
            if ( !start || !end || start >= end )
                return false;
        }

        const size_t diff = end - start;
        if ( diff > 160'000 )
            return false;

        const size_t count = diff / 16;
        if ( count > 10'000 )
            return false;

        thread_local std::vector<uint8_t> buf;
        if ( buf.size( ) < diff )
            buf.resize( diff );

        if ( !usermode->read_buf( start, buf.data( ), diff ) )
            return false;

        out.clear( );
        out.reserve( count );
        for ( size_t i = 0; i < diff; i += 16 )
        {
            uintptr_t child = 0;
            std::memcpy( &child, buf.data( ) + i, sizeof( uintptr_t ) );
            if ( child && child != m_address && usermode->is_valid( child ) )
                out.emplace_back( child );
            if ( out.size( ) >= 10'000 )
                break;
        }
        return !out.empty( );
    };

    static constexpr uintptr_t k_children_offs[] = {
        offsets::instance::children_start,
        0x78,
        0x80,
        0x70,
    };

    for ( uintptr_t off : k_children_offs )
    {
        const uintptr_t child_ptr = usermode->read<uintptr_t>( m_address + off );
        if ( try_list( child_ptr ) )
            return out;
    }
    return out;
}

auto u_instance::find_first_child( const char* target ) const -> u_instance
{
    for ( const auto& child : this->get_children( ) )
    {
        if ( child.name( ) == target )
            return child;
    }
    return u_instance( );
}

auto u_instance::find_first_child_of_class( const char* classname ) const -> u_instance
{
    for ( const auto& child : this->get_children( ) )
    {
        if ( resolve_class_name( class_descriptor_for( child.address( ) ) ) == classname )
            return child;
    }
    return u_instance( );
}

auto u_instance::find_first_descendant( const char* target, int max_depth ) const -> u_instance
{
    if ( !m_address || max_depth < 0 )
        return u_instance( );

    struct frame_t { uintptr_t addr; int depth; };

    thread_local std::vector<frame_t> queue;
    queue.clear( );
    queue.push_back( { m_address, 0 } );

    for ( size_t qi = 0; qi < queue.size( ); ++qi )
    {
        auto [addr, depth] = queue[qi];
        if ( depth > max_depth )
            continue;

        u_instance node( addr );
        for ( const auto& child : node.get_children( ) )
        {
            if ( !child.address( ) )
                continue;
            if ( child.name( ) == target )
                return child;
            if ( depth < max_depth )
                queue.push_back( { child.address( ), depth + 1 } );
        }
    }
    return u_instance( );
}

}
