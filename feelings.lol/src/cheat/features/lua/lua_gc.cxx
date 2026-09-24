#include <include/global.cuh>
#include <src/cheat/features/lua/lua_gc.cuh>
#include <src/engine/sdk/instance/instance.cuh>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace lua_gc
{
namespace
{

std::mutex g_mutex;
bool g_ready = false;
bool g_logged = false;
bool g_we_stopped = false;
uintptr_t g_last_dm = 0;
std::vector<vm_t> g_vms;

auto rpm_ok( uintptr_t p ) -> bool
{
    return usermode && usermode->can_rpm( ) && usermode->is_valid( p );
}

template<typename t>
auto rd( uintptr_t p ) -> t
{
    return usermode->read<t>( p );
}

template<typename t>
auto wr( uintptr_t p, const t& v ) -> bool
{
    return usermode->write<t>( p, v );
}

auto looks_heap( uintptr_t p ) -> bool
{
    if ( !rpm_ok( p ) || ( p & 7 ) )
        return false;
    if ( ( p >> 40 ) >= 0x7F0ull )
        return false;
    return true;
}

auto looks_lua_state( uintptr_t L, uintptr_t top_off, uintptr_t base_off, uintptr_t glob_off, uintptr_t& out_G ) -> bool
{
    if ( !rpm_ok( L ) || ( L & 0xF ) )
        return false;

    const uintptr_t top = rd<uintptr_t>( L + top_off );
    const uintptr_t base = rd<uintptr_t>( L + base_off );
    const uintptr_t G = rd<uintptr_t>( L + glob_off );

    if ( !rpm_ok( top ) || !rpm_ok( base ) || !rpm_ok( G ) )
        return false;
    if ( ( top | base ) & 0xF )
        return false;
    if ( top < base || ( top - base ) > 0x40000 )
        return false;

    out_G = G;
    return true;
}

auto discover_g( uintptr_t G ) -> bool
{
    using namespace offsets::luau_global;

    for ( uint32_t s = 0; s <= 0x40; s += 8 )
    {
        const uintptr_t hash = rd<uintptr_t>( G + s );
        const uint32_t nuse = rd<uint32_t>( G + s + 8 );
        const int32_t size = rd<int32_t>( G + s + 12 );
        if ( !rpm_ok( hash ) || ( hash & 7 ) )
            continue;
        if ( size < 32 || size > ( 1 << 22 ) || ( size & ( size - 1 ) ) )
            continue;
        if ( nuse > static_cast<uint32_t>( size ) * 2u + 16u )
            continue;

        for ( uint32_t gs = s + 32; gs <= s + 72; ++gs )
        {
            const uint8_t st = rd<uint8_t>( G + gs );
            if ( st > 4 )
                continue;

            const uint32_t gr0 = ( gs + 8u ) & ~7u;
            for ( uint32_t gr = gr0; gr <= gs + 32; gr += 8 )
            {
                const uint64_t tb = rd<uint64_t>( G + gr + 32 );
                if ( tb < 1024ull || tb > ( 512ull << 20 ) )
                    continue;

                const int32_t goal = rd<int32_t>( G + gr + 40 );
                const int32_t mul = rd<int32_t>( G + gr + 44 );
                if ( goal < 0 || goal > 1000 )
                    continue;
                if ( mul < 0 || mul > 10000 )
                    continue;

                strt_hash = s;
                strt_nuse = s + 8;
                strt_size = s + 12;
                gcstate = gs;
                gray = gr;
                grayagain = gr + 8;
                weak = gr + 16;
                GCthreshold = gr + 24;
                totalbytes = gr + 32;
                gcgoal = gr + 40;
                gcstepmul = gr + 44;
                allgcopages = 0;

                for ( uint32_t po = gr + 48; po < gr + 0x800 && !allgcopages; po += 8 )
                {
                    const uintptr_t pg = rd<uintptr_t>( G + po );
                    if ( !looks_heap( pg ) )
                        continue;
                    const int32_t ps = rd<int32_t>( pg + page_size );
                    const int32_t bs = rd<int32_t>( pg + page_block );
                    if ( bs >= 24 && bs <= 512 && ps >= 128 && ps <= 65536 )
                        allgcopages = po;
                }

                return true;
            }
        }
    }

    return false;
}

auto looks_like_table( uintptr_t o ) -> bool
{
    using namespace offsets::luau_object;
    if ( !looks_heap( o ) )
        return false;

    const uint8_t a = rd<uint8_t>( o );
    const uint8_t b = rd<uint8_t>( o + tt );
    const bool is_tbl = ( a == tt_table || b == tt_table || a == 6 || b == 6 );
    if ( !is_tbl )
        return false;

    const uint8_t lsz = rd<uint8_t>( o + table_lsz );
    if ( lsz == 0 || lsz > 18 )
        return false;

    const int32_t sz = rd<int32_t>( o + table_sizearray );
    if ( sz < 0 || sz > 1000000 )
        return false;

    const uintptr_t node = rd<uintptr_t>( o + table_node );
    if ( !looks_heap( node ) )
        return false;

    return true;
}

auto obj_tt( uintptr_t o ) -> int
{
    const uint8_t a = rd<uint8_t>( o );
    if ( a >= 4 && a <= 15 )
        return a;
    return static_cast<int>( rd<uint8_t>( o + offsets::luau_object::tt ) );
}

auto gco_next( uintptr_t obj, int tt ) -> uintptr_t
{
    if ( tt == offsets::luau_object::tt_table || tt == 6 )
        return rd<uintptr_t>( obj + offsets::luau_object::table_gclist );
    return rd<uintptr_t>( obj + offsets::luau_object::gclist );
}

auto ts_eq( uintptr_t ts, const char* key, size_t klen ) -> bool
{
    using namespace offsets::luau_object;
    if ( !looks_heap( ts ) )
        return false;

    const uint8_t a = rd<uint8_t>( ts );
    const uint8_t b = rd<uint8_t>( ts + tt );
    if ( a != tt_string && b != tt_string && a != 5 && b != 5 )
        return false;

    const uint32_t len = rd<uint32_t>( ts + ts_len );
    if ( len != static_cast<uint32_t>( klen ) || len > 512 )
        return false;

    char buf[512];
    if ( !usermode->read_buf( ts + ts_data, buf, len ) )
        return false;
    return std::memcmp( buf, key, klen ) == 0;
}

auto luau_str_hash( const char* str, size_t len ) -> unsigned
{
    unsigned h = static_cast<unsigned>( len );
    for ( size_t i = len; i > 0; --i )
        h ^= ( h << 5 ) + ( h >> 2 ) + static_cast<unsigned char>( str[i - 1] );
    return h;
}

auto strt_find( uintptr_t G, const char* needle ) -> uintptr_t
{
    if ( !needle || !G )
        return 0;

    const int size = rd<int32_t>( G + offsets::luau_global::strt_size );
    const uintptr_t hash = rd<uintptr_t>( G + offsets::luau_global::strt_hash );
    if ( size <= 0 || size > ( 1 << 22 ) || !rpm_ok( hash ) )
        return 0;

    const size_t nlen = std::strlen( needle );
    auto chain = [&]( int bucket ) -> uintptr_t
    {
        uintptr_t ts = rd<uintptr_t>( hash + static_cast<uintptr_t>( bucket ) * 8ull );
        for ( int k = 0; k < 100000 && ts; ++k )
        {
            if ( ts_eq( ts, needle, nlen ) )
                return ts;
            const uintptr_t nxt = rd<uintptr_t>( ts + offsets::luau_object::ts_next );
            if ( nxt == ts )
                break;
            ts = nxt;
        }
        return 0;
    };

    if ( ( size & ( size - 1 ) ) == 0 )
    {
        const int buck = static_cast<int>( luau_str_hash( needle, nlen ) & static_cast<unsigned>( size - 1 ) );
        if ( const uintptr_t hit = chain( buck ) )
            return hit;
    }

    for ( int i = 0; i < size; ++i )
    {
        if ( const uintptr_t hit = chain( i ) )
            return hit;
    }
    return 0;
}

auto table_find_str( uintptr_t tbl, const char* key, uintptr_t key_ts, uintptr_t& val_node ) -> bool
{
    using namespace offsets::luau_object;
    if ( !looks_like_table( tbl ) || !key )
        return false;

    const uint8_t lsz = rd<uint8_t>( tbl + table_lsz );
    const uintptr_t node = rd<uintptr_t>( tbl + table_node );
    const int n = 1 << lsz;
    const size_t klen = std::strlen( key );

    unsigned char buf[32 * 64];
    int off = 0;
    while ( off < n )
    {
        int chunk = n - off;
        if ( chunk > 64 )
            chunk = 64;
        const size_t bytes = static_cast<size_t>( chunk ) * 32ull;
        if ( !usermode->read_buf( node + static_cast<uintptr_t>( off ) * 32ull, buf, bytes ) )
            return false;

        for ( int i = 0; i < chunk; ++i )
        {
            const unsigned char* nd = buf + static_cast<size_t>( i ) * 32ull;
            const uint8_t ktt = static_cast<uint8_t>( nd[node_key_tt] & 0xF );
            if ( ktt != tt_string && ktt != 5 )
                continue;

            uintptr_t ts = 0;
            std::memcpy( &ts, nd + node_key_gc, 8 );
            if ( key_ts )
            {
                if ( ts != key_ts )
                    continue;
            }
            else if ( !ts_eq( ts, key, klen ) )
            {
                continue;
            }

            val_node = node + static_cast<uintptr_t>( off + i ) * 32ull;
            return true;
        }
        off += chunk;
    }
    return false;
}

auto find_script_context( uintptr_t dm ) -> uintptr_t
{
    if ( !dm )
        return 0;

    const auto inst = sdk::u_instance( dm );
    const auto sc = inst.find_first_child_of_class( "ScriptContext" );
    if ( sc )
        return sc.address( );

    for ( const auto& c : inst.get_children( ) )
    {
        if ( c.class_name( ) == "ScriptContext" )
            return c.address( );
    }

    const uintptr_t slot = rd<uintptr_t>( dm + offsets::datamodel::script_context );
    if ( rpm_ok( slot ) && sdk::u_instance( slot ).class_name( ) == "ScriptContext" )
        return slot;

    return 0;
}

constexpr uintptr_t k_l_triples[][3] = {
    { 0x08, 0x10, 0x18 },
    { 0x10, 0x18, 0x20 },
    { 0x18, 0x20, 0x28 },
    { 0x20, 0x28, 0x30 },
    { 0x28, 0x30, 0x38 },
};

auto try_state( uintptr_t L, std::vector<vm_t>& out, std::unordered_set<uintptr_t>& seen_g, uint32_t wrap ) -> bool
{
    uintptr_t G = 0;
    for ( const auto& t : k_l_triples )
    {
        if ( !looks_lua_state( L, t[0], t[1], t[2], G ) )
            continue;
        if ( !discover_g( G ) )
            continue;
        if ( !seen_g.insert( G ).second )
            return true;

        offsets::lua_state::top = t[0];
        offsets::lua_state::base = t[1];
        offsets::lua_state::global = t[2];

        vm_t vm{};
        vm.L = L;
        vm.G = G;
        vm.kb = rd<uint64_t>( G + offsets::luau_global::totalbytes ) >> 10;
        vm.wrap = wrap;
        out.push_back( vm );
        return true;
    }
    return false;
}

auto collect_vms( uintptr_t sc, std::vector<vm_t>& out ) -> void
{
    out.clear( );
    if ( !sc )
        return;

    std::unordered_set<uintptr_t> seen_g;
    seen_g.reserve( 8 );

    constexpr uintptr_t wrap_scan_end = 0xA00;
    constexpr uintptr_t inner_offs[] = {
        0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48,
        0x80, 0x88, 0xA0, 0xC0, 0xE8, 0x100, 0x118, 0x130, 0x138,
        0x148, 0x150, 0x160, 0x1A8, 0x1B0, 0x1C8, 0x1D0, 0x1E8,
        0x1F0, 0x1F8, 0x208, 0x210, 0x218, 0x248, 0x250
    };

    for ( uintptr_t off = 0x80; off < wrap_scan_end; off += 8 )
    {
        const uintptr_t p = rd<uintptr_t>( sc + off );
        if ( !rpm_ok( p ) )
            continue;

        try_state( p, out, seen_g, static_cast<uint32_t>( off ) );

        for ( uintptr_t io : inner_offs )
        {
            const uintptr_t L = rd<uintptr_t>( p + io );
            try_state( L, out, seen_g, static_cast<uint32_t>( off ) );
        }
    }

    std::sort( out.begin( ), out.end( ), []( const vm_t& a, const vm_t& b )
    {
        return a.kb > b.kb;
    } );
}

auto game_G( ) -> uintptr_t
{
    if ( g_vms.empty( ) )
        return 0;
    return g_vms.front( ).G;
}

auto walk_gco_tables( uintptr_t head, const char* key, uintptr_t key_ts, const char* val, uintptr_t val_ts,
    int& n, int cap, std::unordered_set<uintptr_t>& seen, std::vector<table_hit_t>& hits, uintptr_t G ) -> void
{
    uintptr_t cur = head;
    for ( int i = 0; i < 80000 && n < cap; ++i )
    {
        if ( !looks_heap( cur ) )
            break;
        const int tt = obj_tt( cur );
        if ( tt < 0 || tt > 15 )
            break;

        if ( looks_like_table( cur ) && seen.insert( cur ).second )
        {
            bool ok = true;
            if ( key )
            {
                uintptr_t nd = 0;
                ok = table_find_str( cur, key, key_ts, nd );
                if ( ok && val )
                {
                    const int vtt = rd<int32_t>( nd + offsets::luau_object::node_val_tt ) & 0xF;
                    if ( vtt != offsets::luau_object::tt_string && vtt != 5 )
                        ok = false;
                    else
                    {
                        const uintptr_t vs = rd<uintptr_t>( nd );
                        ok = val_ts ? ( vs == val_ts ) : ts_eq( vs, val, std::strlen( val ) );
                    }
                }
            }
            if ( ok )
            {
                hits.push_back( { cur, G } );
                ++n;
            }
        }

        const uintptr_t nxt = gco_next( cur, tt );
        if ( nxt == cur )
            break;
        cur = nxt;
    }
}

auto walk_page_tables( uintptr_t head, const char* key, uintptr_t key_ts, const char* val, uintptr_t val_ts,
    int& n, int cap, std::unordered_set<uintptr_t>& seen, std::vector<table_hit_t>& hits, uintptr_t G ) -> void
{
    using namespace offsets::luau_global;
    uintptr_t p = head;
    for ( int pi = 0; pi < 200000 && n < cap; ++pi )
    {
        if ( !looks_heap( p ) )
            break;

        const int32_t pageSize = rd<int32_t>( p + page_size );
        const int32_t blockSize = rd<int32_t>( p + page_block );
        if ( blockSize >= 40 && blockSize <= 512 && pageSize >= 128 && pageSize <= 65536 )
        {
            const int cnt = ( pageSize - 64 ) / blockSize;
            if ( cnt > 0 && cnt < 20000 )
            {
                for ( int i = 0; i < cnt && n < cap; ++i )
                {
                    const uintptr_t obj = p + page_data + static_cast<uintptr_t>( i ) * static_cast<uintptr_t>( blockSize );
                    if ( !looks_like_table( obj ) || !seen.insert( obj ).second )
                        continue;

                    bool ok = true;
                    if ( key )
                    {
                        uintptr_t nd = 0;
                        ok = table_find_str( obj, key, key_ts, nd );
                        if ( ok && val )
                        {
                            const int vtt = rd<int32_t>( nd + offsets::luau_object::node_val_tt ) & 0xF;
                            if ( vtt != offsets::luau_object::tt_string && vtt != 5 )
                                ok = false;
                            else
                            {
                                const uintptr_t vs = rd<uintptr_t>( nd );
                                ok = val_ts ? ( vs == val_ts ) : ts_eq( vs, val, std::strlen( val ) );
                            }
                        }
                    }
                    if ( ok )
                    {
                        hits.push_back( { obj, G } );
                        ++n;
                    }
                }
            }
        }

        const uintptr_t nxt = rd<uintptr_t>( p + page_next );
        if ( nxt == p )
            break;
        p = nxt;
    }
}

auto refresh_locked( ) -> bool
{
    if ( !usermode || !usermode->can_rpm( ) )
        return false;

    const uintptr_t dm = g->engine.datamodel.load( );
    if ( !dm )
        return false;

    if ( g_ready && g_last_dm == dm && !g_vms.empty( ) )
        return true;

    g_ready = false;
    g_vms.clear( );
    g_last_dm = dm;
    g_we_stopped = false;

    const uintptr_t sc = find_script_context( dm );
    collect_vms( sc, g_vms );
    g_ready = !g_vms.empty( );

    if ( !g_logged )
    {
        g_logged = true;
        if ( !sc )
        {
            g->logger.print( "[lua.gc] no ScriptContext dm=0x%llx", ( unsigned long long )dm );
        }
        else if ( g_vms.empty( ) )
        {
            g->logger.print( "[lua.gc] no game lua state sc=0x%llx", ( unsigned long long )sc );
        }
        else
        {
            g->logger.print( "[lua.gc] vms=%d L=0x%llx G=0x%llx kb=%llu running=%d gcstate=%u wrap=0x%X top=0x%llX glob=0x%llX",
                ( int )g_vms.size( ),
                ( unsigned long long )g_vms[0].L,
                ( unsigned long long )g_vms[0].G,
                ( unsigned long long )g_vms[0].kb,
                ( int )( rd<int64_t>( g_vms[0].G + offsets::luau_global::GCthreshold ) != -1 ),
                ( unsigned )rd<uint8_t>( g_vms[0].G + offsets::luau_global::gcstate ),
                g_vms[0].wrap,
                ( unsigned long long )offsets::lua_state::top,
                ( unsigned long long )offsets::lua_state::global );
        }
    }

    return g_ready;
}

}

auto reset( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    g_ready = false;
    g_logged = false;
    g_we_stopped = false;
    g_last_dm = 0;
    g_vms.clear( );
}

auto probe( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    return refresh_locked( );
}

auto vms( ) -> std::vector<vm_t>
{
    std::lock_guard<std::mutex> lock( g_mutex );
    refresh_locked( );
    return g_vms;
}

auto set( const char* opt, int arg ) -> bool
{
    if ( !opt )
        return false;

    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !refresh_locked( ) )
        return false;

    const uintptr_t G = game_G( );
    if ( !G )
        return false;

    if ( std::strcmp( opt, "stop" ) == 0 )
    {
        const int64_t m1 = -1;
        wr<int64_t>( G + offsets::luau_global::GCthreshold, m1 );
        g_we_stopped = true;
        return true;
    }

    if ( std::strcmp( opt, "restart" ) == 0 )
    {
        const uint64_t tb = rd<uint64_t>( G + offsets::luau_global::totalbytes );
        wr<uint64_t>( G + offsets::luau_global::GCthreshold, tb );
        g_we_stopped = false;
        return true;
    }

    if ( std::strcmp( opt, "pause" ) == 0 || std::strcmp( opt, "setpause" ) == 0 )
    {
        if ( arg >= 0 )
        {
            int v = arg;
            if ( v > 1000 )
                v = 1000;
            wr<int32_t>( G + offsets::luau_global::gcgoal, v );
        }
        return true;
    }

    if ( std::strcmp( opt, "stepmul" ) == 0 || std::strcmp( opt, "setstepmul" ) == 0 )
    {
        if ( arg >= 0 )
        {
            int v = arg;
            if ( v > 10000 )
                v = 10000;
            wr<int32_t>( G + offsets::luau_global::gcstepmul, v );
        }
        return true;
    }

    return false;
}

auto count_kb( ) -> uint64_t
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !refresh_locked( ) )
        return 0;
    const uintptr_t G = game_G( );
    if ( !G )
        return 0;
    return rd<uint64_t>( G + offsets::luau_global::totalbytes ) >> 10;
}

auto is_running( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !refresh_locked( ) )
        return false;
    const uintptr_t G = game_G( );
    if ( !G )
        return false;
    return rd<int64_t>( G + offsets::luau_global::GCthreshold ) != -1;
}

auto find( const char* key, const char* val, int cap ) -> std::vector<table_hit_t>
{
    std::vector<table_hit_t> hits;
    if ( cap <= 0 )
        return hits;

    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !refresh_locked( ) )
        return hits;

    std::unordered_set<uintptr_t> seen;
    seen.reserve( 8192 );
    int n = 0;

    for ( const auto& vm : g_vms )
    {
        uintptr_t key_ts = 0;
        uintptr_t val_ts = 0;
        if ( key )
        {
            key_ts = strt_find( vm.G, key );
            if ( !key_ts )
                continue;
        }
        if ( val )
        {
            val_ts = strt_find( vm.G, val );
            if ( !val_ts )
                continue;
        }

        walk_gco_tables( rd<uintptr_t>( vm.G + offsets::luau_global::gray ), key, key_ts, val, val_ts, n, cap, seen, hits, vm.G );
        walk_gco_tables( rd<uintptr_t>( vm.G + offsets::luau_global::grayagain ), key, key_ts, val, val_ts, n, cap, seen, hits, vm.G );
        walk_gco_tables( rd<uintptr_t>( vm.G + offsets::luau_global::weak ), key, key_ts, val, val_ts, n, cap, seen, hits, vm.G );

        if ( offsets::luau_global::allgcopages )
        {
            walk_page_tables( rd<uintptr_t>( vm.G + offsets::luau_global::allgcopages ),
                key, key_ts, val, val_ts, n, cap, seen, hits, vm.G );
        }

        if ( n >= cap )
            break;
    }

    return hits;
}

auto tick( ) -> void
{
    const bool freeze = config.player.freeze_gc;
    bool do_stop = false;
    bool do_restart = false;
    {
        std::lock_guard<std::mutex> lock( g_mutex );
        if ( !refresh_locked( ) )
            return;

        if ( freeze && !g_we_stopped )
            do_stop = true;
        else if ( !freeze && g_we_stopped )
            do_restart = true;
    }

    if ( do_stop && set( "stop" ) )
        g->logger.print( "[lua.gc] setgc stop" );
    else if ( do_restart && set( "restart" ) )
        g->logger.print( "[lua.gc] setgc restart" );
}

}
