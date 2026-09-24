#include <include/global.cuh>
#include <src/cheat/features/silentaim/mouse_hit.cuh>
#include <src/engine/sdk/instance/instance.cuh>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace silentaim::mouse_hit
{
namespace
{

constexpr size_t k_cframe_bytes = 48;
constexpr size_t k_cave_bytes = 256;
constexpr size_t k_stub_region = 0x80;
constexpr uintptr_t k_data_real = 0x00;
constexpr uintptr_t k_data_active = 0x08;
constexpr uintptr_t k_data_cframe = 0x10;

std::mutex g_mutex;
uintptr_t g_stub = 0;
uintptr_t g_data = 0;
uintptr_t g_real = 0;
uintptr_t g_img = 0;
uintptr_t g_mouse = 0;
uintptr_t g_str_hit = 0;
uintptr_t g_cave = 0;
uint8_t g_cave_save[k_cave_bytes]{};
bool g_cave_saved = false;
bool g_installed = false;
bool g_spoof = false;
std::vector<uintptr_t> g_patched_slots;

auto log_fail( const char* msg ) -> void
{
    static uintptr_t last = 0;
    static auto t0 = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now( );
    if ( t0.time_since_epoch( ).count( ) && now - t0 < std::chrono::seconds( 2 )
        && last == reinterpret_cast<uintptr_t>( msg ) )
        return;
    last = reinterpret_cast<uintptr_t>( msg );
    t0 = now;
    g->logger.print( "%s", msg );
}

auto push_bytes( std::vector<uint8_t>& code, std::initializer_list<uint8_t> xs ) -> void
{
    for ( auto x : xs )
        code.push_back( x );
}

auto push_u64( std::vector<uint8_t>& code, uint64_t value ) -> void
{
    for ( int i = 0; i < 8; ++i )
        code.push_back( static_cast<uint8_t>( ( value >> ( i * 8 ) ) & 0xFF ) );
}

auto build_stub( uintptr_t data ) -> std::vector<uint8_t>
{
    std::vector<uint8_t> code;
    code.reserve( 96 );

    push_bytes( code, { 0x53 } );
    push_bytes( code, { 0x56 } );
    push_bytes( code, { 0x57 } );
    push_bytes( code, { 0x48, 0x89, 0xD3 } );
    push_bytes( code, { 0x48, 0xB8 } );
    push_u64( code, data );
    push_bytes( code, { 0x80, 0x78, 0x08, 0x00 } );
    push_bytes( code, { 0x74, 0x21 } );

    push_bytes( code, { 0x48, 0x8D, 0x70, 0x10 } );
    push_bytes( code, { 0x0F, 0x10, 0x06 } );
    push_bytes( code, { 0x0F, 0x10, 0x4E, 0x10 } );
    push_bytes( code, { 0x0F, 0x10, 0x56, 0x20 } );
    push_bytes( code, { 0x0F, 0x11, 0x03 } );
    push_bytes( code, { 0x0F, 0x11, 0x4B, 0x10 } );
    push_bytes( code, { 0x0F, 0x11, 0x53, 0x20 } );
    push_bytes( code, { 0x48, 0x89, 0xD8 } );
    push_bytes( code, { 0x5F, 0x5E, 0x5B, 0xC3 } );

    push_bytes( code, { 0x48, 0x8B, 0x00 } );
    push_bytes( code, { 0x48, 0x89, 0xDA } );
    push_bytes( code, { 0x5F, 0x5E, 0x5B } );
    push_bytes( code, { 0xFF, 0xE0 } );

    return code;
}

auto is_our_stub( uintptr_t addr ) -> bool
{
    uint8_t sig[3]{};
    if ( !addr || !usermode->read_buf( addr, sig, sizeof( sig ) ) )
        return false;
    return sig[0] == 0x53 && sig[1] == 0x56 && sig[2] == 0x57;
}

auto read_cstr( uintptr_t p ) -> std::string
{
    if ( !p || !usermode->is_valid( p ) )
        return {};
    char buf[80]{};
    if ( !usermode->read_buf( p, buf, sizeof( buf ) - 1 ) )
        return {};
    int n = 0;
    while ( n < 79 && buf[n] )
    {
        const unsigned char c = ( unsigned char )buf[n];
        if ( c < 0x20 || c > 0x7E )
            return {};
        ++n;
    }
    if ( n < 2 )
        return {};
    return std::string( buf, n );
}

auto intern_ok( uintptr_t p, const char* expect ) -> bool
{
    return p && read_cstr( p ) == expect;
}

auto pe_image( uintptr_t base ) -> bool
{
    if ( !base || ( base & 0xFFFF ) )
        return false;
    if ( usermode->read<uint16_t>( base ) != 0x5A4D )
        return false;
    const uint32_t lfanew = usermode->read<uint32_t>( base + 0x3C );
    if ( lfanew < 0x40 || lfanew > 0x400 )
        return false;
    return usermode->read<uint32_t>( base + lfanew ) == 0x4550;
}

auto pe_size( uintptr_t base ) -> uint32_t
{
    if ( !pe_image( base ) )
        return 0;
    const uint32_t lfanew = usermode->read<uint32_t>( base + 0x3C );
    return usermode->read<uint32_t>( base + lfanew + 0x50 );
}

auto looks_code( uintptr_t p ) -> bool
{
    if ( !p || ( p & 3 ) || !usermode->is_valid( p ) )
        return false;
    uint8_t b[2]{};
    if ( !usermode->read_buf( p, b, sizeof( b ) ) )
        return false;
    return b[0] == 0x40 || b[0] == 0x41 || b[0] == 0x44 || b[0] == 0x48
        || b[0] == 0x4C || b[0] == 0x55 || b[0] == 0x53 || b[0] == 0x56
        || b[0] == 0x57 || ( b[0] == 0x0F && b[1] == 0x1F );
}

auto write_ptr( uintptr_t addr, uintptr_t value ) -> bool
{
    if ( usermode->write<uintptr_t>( addr, value ) && usermode->read<uintptr_t>( addr ) == value )
        return true;
    ULONG old = 0;
    usermode->protect( addr, sizeof( value ), PAGE_READWRITE, &old );
    const bool ok = usermode->write<uintptr_t>( addr, value );
    if ( old )
        usermode->protect( addr, sizeof( value ), old, nullptr );
    return ok && usermode->read<uintptr_t>( addr ) == value;
}

auto class_name_of( uintptr_t inst ) -> std::string
{
    if ( !inst )
        return {};
    return sdk::u_instance( inst ).class_name( );
}

auto mouse_from( uintptr_t inst ) -> uintptr_t
{
    if ( !inst || !usermode->is_valid( inst ) )
        return 0;
    const std::string cn = class_name_of( inst );
    if ( cn == "PlayerMouse" || cn == "Mouse" )
        return inst;
    return 0;
}

auto find_mouse( ) -> uintptr_t
{
    if ( g_mouse && mouse_from( g_mouse ) )
        return g_mouse;
    g_mouse = 0;

    const uintptr_t lp = g->engine.client.load( );
    if ( !lp )
        return 0;

    const uintptr_t cached = usermode->read<uintptr_t>( lp + offsets::player::mouse );
    if ( uintptr_t m = mouse_from( cached ) )
    {
        g_mouse = m;
        return m;
    }

    for ( const auto& k : sdk::u_instance( lp ).get_children( ) )
    {
        if ( uintptr_t m = mouse_from( k.address( ) ) )
        {
            g_mouse = m;
            return m;
        }
    }
    return 0;
}

auto bind_interns( uintptr_t img ) -> bool
{
    if ( !intern_ok( img + offsets::rva::intern_hit, "Hit" ) )
        return false;
    g_img = img;
    g_str_hit = img + offsets::rva::intern_hit;
    return true;
}

auto resolve_interns( uintptr_t cd ) -> void
{
    if ( g_str_hit && intern_ok( g_str_hit, "Hit" ) )
        return;

    const uintptr_t np = cd ? usermode->read<uintptr_t>( cd + offsets::descriptor::name ) : 0;
    const struct { uintptr_t rva; const char* name; } keys[] = {
        { offsets::rva::intern_playermouse, "PlayerMouse" },
        { offsets::rva::intern_mouse, "Mouse" },
        { offsets::rva::intern_hit, "Hit" },
    };
    if ( np )
    {
        for ( const auto& k : keys )
        {
            if ( np > k.rva && intern_ok( np, k.name ) && bind_interns( np - k.rva ) )
                return;
        }
    }

    if ( cd > offsets::rva::playermouse_cd )
    {
        const uintptr_t fast = cd - offsets::rva::playermouse_cd;
        if ( pe_image( fast ) && bind_interns( fast ) )
            return;
    }
    uintptr_t page = cd & ~0xFFFFull;
    for ( int i = 0; i < 3072 && page > 0x10000; ++i, page -= 0x10000 )
    {
        if ( pe_image( page ) && bind_interns( page ) )
            return;
    }
}

auto fd_name( uintptr_t fd ) -> std::string
{
    if ( !fd || !usermode->is_valid( fd ) )
        return {};
    const uintptr_t np = usermode->read<uintptr_t>( fd + offsets::descriptor::name );
    if ( auto s = read_cstr( np ); !s.empty( ) )
        return s;
    if ( auto s = read_cstr( usermode->read<uintptr_t>( np ) ); !s.empty( ) )
        return s;
    return {};
}

auto prop_named( uintptr_t fd, const char* name, uintptr_t intern ) -> bool
{
    if ( !fd || !usermode->is_valid( fd ) )
        return false;
    const uintptr_t np = usermode->read<uintptr_t>( fd + offsets::descriptor::name );
    if ( intern && np == intern )
        return true;
    return fd_name( fd ) == name;
}

auto visit_list( uintptr_t data, uint64_t n, const auto& fn ) -> void
{
    if ( !data || !usermode->is_valid( data ) )
        return;
    uint64_t count = 0;
    if ( n > 0 && n <= 1024 )
        count = n;
    else if ( n > data && ( ( n - data ) % 8 ) == 0 )
    {
        count = ( n - data ) / 8;
        if ( count == 0 || count > 1024 )
            return;
    }
    else
        return;
    for ( uint64_t i = 0; i < count; ++i )
        fn( usermode->read<uintptr_t>( data + i * 8 ) );
}

auto for_each_cd( uintptr_t mouse, const auto& fn ) -> void
{
    const uintptr_t pm_cd = usermode->read<uintptr_t>( mouse + offsets::instance::class_descriptor );
    resolve_interns( pm_cd );

    uintptr_t cds[16]{};
    int n = 0;
    auto push = [&]( uintptr_t cd )
    {
        if ( !cd || !usermode->is_valid( cd ) )
            return;
        for ( int i = 0; i < n; ++i )
            if ( cds[i] == cd )
                return;
        if ( n < 16 )
            cds[n++] = cd;
    };

    push( pm_cd );
    push( usermode->read<uintptr_t>( pm_cd + offsets::class_descriptor::super ) );
    push( usermode->read<uintptr_t>( pm_cd + offsets::class_descriptor::super_a ) );
    push( usermode->read<uintptr_t>( pm_cd + offsets::class_descriptor::super_b ) );
    push( usermode->read<uintptr_t>( pm_cd + offsets::class_descriptor::super_c ) );
    for ( int i = 0; i < n; ++i )
        push( usermode->read<uintptr_t>( cds[i] + offsets::class_descriptor::super ) );

    for ( int i = 0; i < n; ++i )
        fn( cds[i] );
}

auto for_each_prop( uintptr_t mouse, const auto& fn ) -> void
{
    static constexpr uintptr_t k_lists[] = { 0x40, 0x38, 0x50, 0x58, 0x60, 0x70, 0x80 };
    for_each_cd( mouse, [&]( uintptr_t cd )
    {
        for ( uintptr_t off : k_lists )
        {
            const uintptr_t data = usermode->read<uintptr_t>( cd + off );
            const uint64_t n = usermode->read<uint64_t>( cd + off + 8 );
            visit_list( data, n, fn );
        }
    } );
}

auto collect_hit_getimpls( uintptr_t mouse, std::vector<uintptr_t>& out ) -> void
{
    for_each_prop( mouse, [&]( uintptr_t fd )
    {
        if ( !prop_named( fd, "Hit", g_str_hit ) )
            return;
        const uintptr_t getimpl = usermode->read<uintptr_t>( fd + offsets::property_descriptor::get_set_impl );
        if ( !getimpl || !usermode->is_valid( getimpl ) )
            return;
        for ( uintptr_t p : out )
            if ( p == getimpl )
                return;
        out.push_back( getimpl );
    } );
}

auto make_hit( const math::vector3_t& camera, const math::vector3_t& target ) -> math::cframe_t
{
    math::vector3_t d{ target.x - camera.x, target.y - camera.y, target.z - camera.z };
    const float len = std::sqrt( d.x * d.x + d.y * d.y + d.z * d.z );
    if ( len < 0.001f )
        return { {}, target };

    d.x /= len;
    d.y /= len;
    d.z /= len;

    math::vector3_t right{ -d.z, 0.f, d.x };
    const float rlen = std::sqrt( right.x * right.x + right.z * right.z );
    if ( rlen > 0.001f )
    {
        right.x /= rlen;
        right.z /= rlen;
    }
    else
    {
        right = { 1.f, 0.f, 0.f };
    }

    const math::vector3_t up{
        right.y * d.z - right.z * d.y,
        right.z * d.x - right.x * d.z,
        right.x * d.y - right.y * d.x
    };

    math::cframe_t cf{};
    cf.rotation = {
        right.x, up.x, -d.x,
        right.y, up.y, -d.y,
        right.z, up.z, -d.z
    };
    cf.position = target;
    return cf;
}

auto plant_cave( ) -> bool
{
    if ( g_stub && is_our_stub( g_stub ) )
        return true;

    auto try_mod = [&]( const wchar_t* name ) -> bool
    {
        size_t wsize = 0;
        const uintptr_t w = usermode->module( name, &wsize );
        if ( !w || wsize < offsets::winsta::cave + k_cave_bytes )
            return false;

        const uintptr_t cave = w + offsets::winsta::cave;
        g_cave = cave;
        if ( !g_cave_saved && usermode->read_buf( cave, g_cave_save, k_cave_bytes ) )
            g_cave_saved = true;

        const uintptr_t page = cave & ~0xFFFull;
        ULONG old = 0;
        usermode->protect( page, 0x1000, PAGE_EXECUTE_READWRITE, &old );

        g_stub = cave;
        g_data = ( cave + k_stub_region + 0xF ) & ~static_cast<uintptr_t>( 0xF );
        if ( g_data + 0x40 > cave + k_cave_bytes )
            g_data = cave + k_stub_region;

        const auto stub = build_stub( g_data );
        if ( stub.size( ) > k_stub_region )
            return false;
        if ( !usermode->write_buf( g_stub, stub.data( ), stub.size( ) ) )
            return false;
        return is_our_stub( g_stub );
    };

    if ( try_mod( L"dxgi.dll" ) )
        return true;
    if ( try_mod( L"winsta.dll" ) )
        return true;
    log_fail( "[mouse.hit] no dxgi/winsta cave" );
    return false;
}

auto already_patched( uintptr_t slot ) -> bool
{
    for ( uintptr_t p : g_patched_slots )
        if ( p == slot )
            return true;
    return false;
}

auto patch_slot( uintptr_t slot, uintptr_t real ) -> bool
{
    if ( !slot || !g_stub )
        return false;

    const uintptr_t cur = usermode->read<uintptr_t>( slot );
    if ( !cur )
        return false;
    if ( cur != real && cur != g_stub && !is_our_stub( cur ) )
        return false;
    if ( cur == g_stub )
    {
        if ( !already_patched( slot ) )
            g_patched_slots.push_back( slot );
        return true;
    }
    if ( !write_ptr( slot, g_stub ) )
        return false;
    if ( !already_patched( slot ) )
        g_patched_slots.push_back( slot );
    return true;
}

auto patch_getimpl( uintptr_t getimpl, uintptr_t real ) -> bool
{
    return getimpl && patch_slot( getimpl + offsets::get_set::get, real );
}

auto scan_image_slots( uintptr_t real ) -> int
{
    if ( !g_img || !real )
        return 0;
    const uint32_t size = pe_size( g_img );
    if ( size < 0x2000 )
        return 0;

    const uintptr_t intern = g_str_hit ? g_str_hit : g_img + offsets::rva::intern_hit;
    uintptr_t lo = intern > 0x200000 ? intern - 0x200000 : g_img + 0x1000;
    uintptr_t hi = intern + 0x300000;
    const uintptr_t end = g_img + size;
    if ( lo < g_img + 0x1000 )
        lo = g_img + 0x1000;
    if ( hi > end )
        hi = end;

    int n = 0;
    uint8_t page[0x1000]{};
    for ( uintptr_t p = lo & ~0xFFFull; p < hi; p += 0x1000 )
    {
        if ( !usermode->read_buf( p, page, sizeof( page ) ) )
            continue;
        for ( size_t i = 0; i + 8 <= sizeof( page ); i += 8 )
        {
            uintptr_t v{};
            std::memcpy( &v, page + i, 8 );
            if ( v != real )
                continue;
            if ( patch_slot( p + i, real ) )
                ++n;
        }
    }
    return n;
}

auto slots_live( ) -> bool
{
    if ( !g_stub || g_patched_slots.empty( ) )
        return false;
    int live = 0;
    for ( uintptr_t slot : g_patched_slots )
    {
        const uintptr_t cur = usermode->read<uintptr_t>( slot );
        if ( cur == g_stub || is_our_stub( cur ) )
            ++live;
    }
    return live > 0;
}

auto drop_dead( ) -> void
{
    g_installed = false;
    g_spoof = false;
    g_stub = 0;
    g_data = 0;
    g_real = 0;
    g_cave = 0;
    g_mouse = 0;
    g_patched_slots.clear( );
}

auto install_locked( ) -> bool
{
    if ( g_installed && is_our_stub( g_stub ) && slots_live( ) )
        return true;

    static auto last_try = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now( );
    if ( last_try.time_since_epoch( ).count( )
        && now - last_try < std::chrono::milliseconds( 400 ) )
        return false;
    last_try = now;

    if ( !usermode->can_rpm( ) )
        return false;

    const uintptr_t mouse = find_mouse( );
    if ( !mouse )
    {
        log_fail( "[mouse.hit] waiting PlayerMouse" );
        return false;
    }

    if ( !plant_cave( ) )
        return false;

    std::vector<uintptr_t> getimpls;
    collect_hit_getimpls( mouse, getimpls );
    if ( getimpls.empty( ) )
    {
        log_fail( "[mouse.hit] no Hit GetImpl" );
        return false;
    }

    uintptr_t real = g_real;
    if ( !real )
    {
        real = usermode->read<uintptr_t>( getimpls[0] + offsets::get_set::get );
        if ( real == g_stub || is_our_stub( real ) )
            real = usermode->read<uintptr_t>( g_data + k_data_real );
        if ( !real || !looks_code( real ) )
        {
            log_fail( "[mouse.hit] no Hit getter" );
            return false;
        }
    }

    uint8_t zero[0x40]{};
    usermode->write_buf( g_data, zero, sizeof( zero ) );
    usermode->write<uintptr_t>( g_data + k_data_real, real );
    usermode->write<uint8_t>( g_data + k_data_active, 0 );

    g_patched_slots.clear( );
    bool patched = false;
    for ( uintptr_t gi : getimpls )
        patched = patch_getimpl( gi, real ) || patched;
    const int extra = scan_image_slots( real );
    patched = extra > 0 || patched;

    if ( !patched )
    {
        log_fail( "[mouse.hit] slot reject" );
        return false;
    }

    g_real = real;
    g_mouse = mouse;
    g_installed = true;
    g_spoof = false;
    g->logger.print(
        "[mouse.hit] hooked getter=%p stub=%p slots=%zu img=%p mouse=%p prop=%p extra=%d",
        ( void* )g_real, ( void* )g_stub, g_patched_slots.size( ),
        ( void* )g_img, ( void* )mouse,
        ( void* )( mouse + offsets::mouse::prop ), extra );
    return true;
}

auto uninstall_locked( ) -> void
{
    if ( g_data )
        usermode->write<uint8_t>( g_data + k_data_active, 0 );

    for ( const uintptr_t slot : g_patched_slots )
    {
        if ( g_real && usermode->read<uintptr_t>( slot ) == g_stub )
            write_ptr( slot, g_real );
    }
    g_patched_slots.clear( );

    if ( g_cave_saved && g_cave && usermode->can_rpm( ) )
        usermode->write_buf( g_cave, g_cave_save, k_cave_bytes );

    drop_dead( );
}

auto set_locked( const math::cframe_t& hit ) -> bool
{
    if ( !g_installed || !g_data )
        return false;
    usermode->write_buf( g_data + k_data_cframe, &hit, k_cframe_bytes );
    usermode->write<uint8_t>( g_data + k_data_active, 1 );
    g_spoof = true;
    return true;
}

auto clear_locked( ) -> void
{
    g_spoof = false;
    if ( g_data )
        usermode->write<uint8_t>( g_data + k_data_active, 0 );
}

}

auto ensure( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( g_installed && is_our_stub( g_stub ) && slots_live( ) )
        return true;
    return install_locked( );
}

auto unhook( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    uninstall_locked( );
}

auto hooked( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    return g_installed && is_our_stub( g_stub ) && slots_live( );
}

auto armed( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    return g_installed && g_spoof;
}

auto passthrough( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    clear_locked( );
}

auto apply(
    const math::vector3_t& hit_pos,
    const math::vector3_t& cam_pos,
    uintptr_t,
    float,
    float ) -> bool
{
    if ( !hit_pos.is_valid( ) )
        return false;

    const math::vector3_t from = cam_pos.is_valid( ) ? cam_pos : hit_pos;
    const math::cframe_t cf = make_hit( from, hit_pos );

    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !g_installed || !is_our_stub( g_stub ) || !slots_live( ) )
        install_locked( );
    if ( !set_locked( cf ) )
        return false;

    static auto last_aim = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now( );
    if ( !last_aim.time_since_epoch( ).count( ) || now - last_aim > std::chrono::seconds( 1 ) )
    {
        last_aim = now;
        g->logger.print(
            "[overlay] mouse.hit set %.1f %.1f %.1f slots=%zu",
            hit_pos.x, hit_pos.y, hit_pos.z, g_patched_slots.size( ) );
    }
    return true;
}

}
