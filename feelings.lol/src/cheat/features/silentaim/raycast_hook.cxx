#include <include/global.cuh>
#include <src/cheat/features/silentaim/raycast_hook.cuh>
#include <src/engine/sdk/instance/instance.cuh>
#include <src/engine/sdk/humanoid/humanoid.cuh>
#include <src/engine/sdk/basepart/basepart.cuh>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

#ifndef CFG_CALL_TARGET_VALID
#define CFG_CALL_TARGET_VALID 0x00000001
#endif

namespace
{
uintptr_t g_img = 0;
uintptr_t g_img_end = 0;
uintptr_t g_str_raycast = 0;
uintptr_t g_str_blockcast = 0;
uintptr_t g_str_spherecast = 0;
uintptr_t g_str_shapecast = 0;
uintptr_t g_str_findpart = 0;
uintptr_t g_str_viewport = 0;
uintptr_t g_str_screen = 0;

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

auto set_image( uintptr_t img ) -> void
{
    g_img = img;
    const uint32_t lfanew = usermode->read<uint32_t>( img + 0x3C );
    const uint32_t so = usermode->read<uint32_t>( img + lfanew + 0x50 );
    g_img_end = img + ( so ? ( uintptr_t )so : 0x9318000ull );
}

auto in_image( uintptr_t p ) -> bool
{
    return g_img && g_img_end && p >= g_img && p < g_img_end && !( p & 3 );
}
auto bind_raycast_image( uintptr_t img ) -> bool
{
    if ( !pe_image( img ) || !intern_ok( img + offsets::rva::intern_raycast, "Raycast" ) )
        return false;
    set_image( img );
    g_str_raycast = img + offsets::rva::intern_raycast;
    g_str_blockcast = intern_ok( img + offsets::rva::intern_blockcast, "Blockcast" )
        ? img + offsets::rva::intern_blockcast : 0;
    g_str_spherecast = intern_ok( img + offsets::rva::intern_spherecast, "Spherecast" )
        ? img + offsets::rva::intern_spherecast : 0;
    g_str_shapecast = intern_ok( img + offsets::rva::intern_shapecast, "Shapecast" )
        ? img + offsets::rva::intern_shapecast : 0;
    g_str_findpart = intern_ok( img + offsets::rva::intern_findpart, "FindPartOnRay" )
        ? img + offsets::rva::intern_findpart : 0;
    return true;
}

auto bind_viewport_image( uintptr_t img ) -> bool
{
    if ( !pe_image( img ) || !intern_ok( img + offsets::rva::intern_viewport, "ViewportPointToRay" ) )
        return false;
    set_image( img );
    g_str_viewport = img + offsets::rva::intern_viewport;
    g_str_screen = intern_ok( img + offsets::rva::intern_screen, "ScreenPointToRay" )
        ? img + offsets::rva::intern_screen : 0;
    return true;
}

struct intern_key_t
{
    uintptr_t rva;
    const char* name;
};

auto resolve_from_keys( uintptr_t hint, const intern_key_t* keys, int key_n,
    bool ( *bind )( uintptr_t ) ) -> void
{
    const uintptr_t np = hint ? usermode->read<uintptr_t>( hint + offsets::descriptor::name ) : 0;
    if ( np )
    {
        for ( int i = 0; i < key_n; ++i )
        {
            if ( np > keys[i].rva && intern_ok( np, keys[i].name ) && bind( np - keys[i].rva ) )
                return;
        }
    }
    uintptr_t page = hint ? ( hint & ~0xFFFFull ) : 0;
    for ( int i = 0; page && i < 3072 && page > 0x10000; ++i, page -= 0x10000 )
    {
        if ( bind( page ) )
            return;
    }
}

auto resolve_raycast_image( uintptr_t hint ) -> void
{
    if ( intern_ok( g_str_raycast, "Raycast" ) )
        return;
    const intern_key_t keys[] = {
        { offsets::rva::intern_worldroot, "WorldRoot" },
        { offsets::rva::intern_workspace, "Workspace" },
        { offsets::rva::intern_raycast, "Raycast" },
    };
    resolve_from_keys( hint, keys, 3, bind_raycast_image );
}

auto resolve_viewport_image( uintptr_t hint ) -> void
{
    if ( intern_ok( g_str_viewport, "ViewportPointToRay" ) )
        return;
    const intern_key_t keys[] = {
        { offsets::rva::intern_camera, "Camera" },
        { offsets::rva::intern_viewport, "ViewportPointToRay" },
        { offsets::rva::intern_screen, "ScreenPointToRay" },
    };
    resolve_from_keys( hint, keys, 3, bind_viewport_image );
}

auto desc_cstr( uintptr_t desc ) -> std::string
{
    if ( !desc || !usermode->is_valid( desc ) )
        return {};
    const uintptr_t name_ptr = usermode->read<uintptr_t>( desc + offsets::descriptor::name );
    if ( !name_ptr )
        return {};
    std::string s = read_cstr( name_ptr );
    if ( !s.empty( ) )
        return s;
    return usermode->read_string( name_ptr );
}

auto name_is( uintptr_t desc, uintptr_t intern, const char* expect ) -> bool
{
    if ( !desc )
        return false;
    const uintptr_t name_ptr = usermode->read<uintptr_t>( desc + offsets::descriptor::name );
    if ( intern && name_ptr == intern )
        return true;
    return desc_cstr( desc ) == expect;
}

auto looks_code( uintptr_t p ) -> bool
{
    if ( !p || ( p & 3 ) || !usermode->is_valid( p ) )
        return false;
    uint8_t b[2]{};
    if ( !usermode->read_buf( p, b, sizeof( b ) ) )
        return false;
    return b[0] == 0x40 || b[0] == 0x41 || b[0] == 0x44 || b[0] == 0x48
        || b[0] == 0x49 || b[0] == 0x4C || b[0] == 0x4D || b[0] == 0x55
        || b[0] == 0x53 || b[0] == 0x56 || b[0] == 0x57
        || ( b[0] == 0x0F && b[1] == 0x1F );
}

auto native_at( uintptr_t fd ) -> uintptr_t
{
    if ( !fd )
        return 0;
    static constexpr uintptr_t k_fn[] = {
        offsets::function_descriptor::function, 0x78, 0x88, 0x70, 0x90,
    };
    for ( uintptr_t off : k_fn )
    {
        const uintptr_t fn = usermode->read<uintptr_t>( fd + off );
        if ( looks_code( fn ) )
            return fn;
    }
    return 0;
}

auto looks_class_desc( uintptr_t p ) -> bool
{
    return p && usermode->is_valid( p ) && !desc_cstr( p ).empty( );
}

template <typename Fn>
auto visit_fd_list( uintptr_t data, uint64_t n, Fn&& fn ) -> void
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

auto collect_cds( uintptr_t seed, uintptr_t* cds, int& n, int cap ) -> void
{
    auto push = [&]( uintptr_t cd )
    {
        if ( !looks_class_desc( cd ) )
            return;
        for ( int i = 0; i < n; ++i )
            if ( cds[i] == cd )
                return;
        if ( n < cap )
            cds[n++] = cd;
    };
    push( seed );
    for ( int i = 0; i < n; ++i )
    {
        for ( uintptr_t off = 0x10; off <= 0x1E0; off += 8 )
        {
            const uintptr_t cand = usermode->read<uintptr_t>( cds[i] + off );
            if ( cand && cand != cds[i] )
                push( cand );
        }
    }
}

auto walk_cd_for( uintptr_t cd, uintptr_t intern, const char* expect ) -> uintptr_t
{
    if ( std::strcmp( expect, "ViewportPointToRay" ) == 0
        || std::strcmp( expect, "ScreenPointToRay" ) == 0 )
        resolve_viewport_image( cd );
    else
        resolve_raycast_image( cd );

    if ( !intern )
    {
        if ( std::strcmp( expect, "Raycast" ) == 0 )
            intern = g_str_raycast;
        else if ( std::strcmp( expect, "Blockcast" ) == 0 )
            intern = g_str_blockcast;
        else if ( std::strcmp( expect, "Spherecast" ) == 0 )
            intern = g_str_spherecast;
        else if ( std::strcmp( expect, "Shapecast" ) == 0 )
            intern = g_str_shapecast;
        else if ( std::strcmp( expect, "FindPartOnRay" ) == 0 )
            intern = g_str_findpart;
        else if ( std::strcmp( expect, "ViewportPointToRay" ) == 0 )
            intern = g_str_viewport;
        else if ( std::strcmp( expect, "ScreenPointToRay" ) == 0 )
            intern = g_str_screen;
    }

    uintptr_t cds[24]{};
    int n = 0;
    collect_cds( cd, cds, n, 24 );

    uintptr_t found = 0;
    for ( int i = 0; i < n && !found; ++i )
    {
        if ( name_is( cds[i], intern, expect ) && native_at( cds[i] ) )
        {
            found = cds[i];
            break;
        }
        for ( uintptr_t off = 0x20; off <= 0x1E0 && !found; off += 8 )
        {
            visit_fd_list(
                usermode->read<uintptr_t>( cds[i] + off ),
                usermode->read<uint64_t>( cds[i] + off + 8 ),
                [&]( uintptr_t fd )
                {
                    if ( found || !fd || !usermode->is_valid( fd ) )
                        return;
                    if ( !name_is( fd, intern, expect ) )
                        return;
                    if ( native_at( fd ) )
                        found = fd;
                } );
        }
    }
    return found;
}

}

namespace raycast_hook
{
namespace
{

constexpr size_t k_stub_bytes = offsets::raycast_stub::stub_slot_bytes;

#pragma pack( push, 4 )
struct raycast_state_t
{
    uint32_t active = 0;
    uint32_t reserved = 0;
    float target_x = 0.f;
    float target_y = 0.f;
    float target_z = 0.f;
    float scale = 1.15f;
    uint64_t calls = 0;
    float ori_x = 0.f;
    float ori_y = 0.f;
    float ori_z = 0.f;
    float dir_x = 0.f;
    float dir_y = 0.f;
    float dir_z = 0.f;
    uint64_t rewrites = 0;
    uint64_t skips = 0;
    float last_ox = 0.f;
    float last_oy = 0.f;
    float last_oz = 0.f;
    float last_dx = 0.f;
    float last_dy = 0.f;
    float last_dz = 0.f;
    uint32_t last_action = 0;
};
#pragma pack( pop )

static_assert( offsetof( raycast_state_t, active ) == 0x00, "active" );
static_assert( offsetof( raycast_state_t, target_x ) == 0x08, "target" );
static_assert( offsetof( raycast_state_t, calls ) == 0x18, "calls" );
static_assert( offsetof( raycast_state_t, ori_x ) == 0x20, "ori" );
static_assert( offsetof( raycast_state_t, dir_x ) == 0x2C, "dir" );
static_assert( offsetof( raycast_state_t, rewrites ) == 0x38, "rewrites" );
static_assert( offsetof( raycast_state_t, skips ) == 0x40, "skips" );
static_assert( offsetof( raycast_state_t, last_ox ) == 0x48, "last_o" );
static_assert( offsetof( raycast_state_t, last_dx ) == 0x54, "last_d" );
static_assert( offsetof( raycast_state_t, last_action ) == 0x60, "last_action" );

enum thunk_kind : int
{
    thunk_rewrite = 0,
    thunk_count = 1,
    thunk_cpp = 2,
};

struct pin_t
{
    uintptr_t slot = 0;
    uintptr_t original = 0;
    uintptr_t thunk = 0;
    uintptr_t site = 0;
    bool owned = false;
    int kind = 0;
    uint32_t thunk_n = 0;
    uint8_t saved[8]{};
};

struct hook_t
{
    pin_t pins[12]{};
    int npins = 0;
    uintptr_t thunk = 0;
    uintptr_t state = 0;
    uintptr_t original = 0;
    uintptr_t slot = 0;
    bool thunk_owned = false;
    bool installed = false;
    bool active = false;
};

std::mutex g_mutex;
hook_t g_hook{};
auto g_last_fail = std::chrono::steady_clock::time_point{};

auto ok_addr( uintptr_t a ) -> bool
{
    return a >= 0x10000ull && a < 0x00007FFFFFFFFFFFull && usermode->is_valid( a );
}

auto page_size( ) -> size_t
{
    static const size_t p = []( )
    {
        SYSTEM_INFO i{};
        GetSystemInfo( &i );
        return i.dwPageSize ? ( size_t )i.dwPageSize : 0x1000u;
    }( );
    return p;
}

auto query_protect( uintptr_t a ) -> DWORD
{
    MEMORY_BASIC_INFORMATION mbi{};
    if ( !VirtualQueryEx( usermode->handle( ), ( void* )a, &mbi, sizeof( mbi ) ) )
        return 0;
    return mbi.Protect;
}

auto is_exec( DWORD p ) -> bool
{
    const DWORD x = p & 0xFF;
    return x == PAGE_EXECUTE || x == PAGE_EXECUTE_READ
        || x == PAGE_EXECUTE_READWRITE || x == PAGE_EXECUTE_WRITECOPY;
}

auto write_protected( uintptr_t address, const void* data, size_t size ) -> bool
{
    if ( !ok_addr( address ) || !data || !size )
        return false;
    ULONG old = 0;
    const bool changed = usermode->protect( address, size, PAGE_EXECUTE_READWRITE, &old );
    const bool wrote = usermode->write_buf( address, data, size );
    if ( changed && old )
        usermode->protect( address, size, old, nullptr );
    return wrote;
}

auto mark_cfg( uintptr_t t ) -> void
{
    auto proc = GetProcAddress( GetModuleHandleW( L"kernelbase.dll" ), "SetProcessValidCallTargets" );
    if ( !proc )
        proc = GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "SetProcessValidCallTargets" );
    if ( !proc )
        return;

    struct info_t
    {
        ULONG_PTR Offset;
        ULONG Flags;
    } info{};
    info.Offset = t & ( page_size( ) - 1 );
    info.Flags = CFG_CALL_TARGET_VALID;

    using fn_t = BOOL ( WINAPI* )( HANDLE, PVOID, SIZE_T, ULONG, void* );
    reinterpret_cast<fn_t>( proc )(
        usermode->handle( ),
        ( void* )( t & ~( ( uintptr_t )page_size( ) - 1 ) ),
        page_size( ), 1, &info );
}

auto append_u64( std::vector<uint8_t>& c, uint64_t v ) -> void
{
    const auto* b = reinterpret_cast<const uint8_t*>( &v );
    c.insert( c.end( ), b, b + 8 );
}

auto append_u32( std::vector<uint8_t>& c, uint32_t v ) -> void
{
    const auto* b = reinterpret_cast<const uint8_t*>( &v );
    c.insert( c.end( ), b, b + 4 );
}

auto bits_f32( float v ) -> uint32_t
{
    uint32_t u = 0;
    std::memcpy( &u, &v, 4 );
    return u;
}

auto patch_rel32( std::vector<uint8_t>& c, size_t o, size_t t ) -> void
{
    const int32_t v = ( int32_t )( ( ptrdiff_t )t - ( ptrdiff_t )( o + 4 ) );
    std::memcpy( c.data( ) + o, &v, 4 );
}

auto emit_jcc( std::vector<uint8_t>& c, std::vector<size_t>& to, uint8_t cc ) -> void
{
    c.insert( c.end( ), { 0x0F, cc } );
    to.push_back( c.size( ) );
    c.insert( c.end( ), { 0, 0, 0, 0 } );
}

auto emit_je( std::vector<uint8_t>& c, std::vector<size_t>& to ) -> void
{
    emit_jcc( c, to, 0x84 );
}

auto emit_jmp( std::vector<uint8_t>& c, size_t& slot ) -> void
{
    c.push_back( 0xE9 );
    slot = c.size( );
    c.insert( c.end( ), { 0, 0, 0, 0 } );
}

// eax = IEEE-754 bits. reject nan/inf/denormal. keep true zero + world-scale normals.
auto emit_float_ok( std::vector<uint8_t>& c, std::vector<size_t>& fail ) -> void
{
    c.insert( c.end( ), { 0x41, 0x89, 0xC3 } );
    c.insert( c.end( ), { 0x41, 0xC1, 0xEB, 0x17 } );
    c.insert( c.end( ), { 0x41, 0x83, 0xE3, 0xFF } );
    c.insert( c.end( ), { 0x41, 0x83, 0xFB, 0xFF } );
    emit_je( c, fail );
    c.insert( c.end( ), { 0x45, 0x85, 0xDB } );
    std::vector<size_t> to_range;
    emit_jcc( c, to_range, 0x85 );
    c.insert( c.end( ), { 0x25, 0xFF, 0xFF, 0x7F, 0x00 } );
    emit_jcc( c, fail, 0x85 );
    size_t to_ok = 0;
    emit_jmp( c, to_ok );
    const size_t range = c.size( );
    for ( auto o : to_range )
        patch_rel32( c, o, range );
    c.insert( c.end( ), { 0x41, 0x83, 0xFB, 0x64 } );
    emit_jcc( c, fail, 0x82 );
    c.insert( c.end( ), { 0x41, 0x83, 0xFB, 0x96 } );
    emit_jcc( c, fail, 0x87 );
    patch_rel32( c, to_ok, c.size( ) );
}

auto emit_user_ptr_r8( std::vector<uint8_t>& c, std::vector<size_t>& fail, uint8_t align_mask ) -> void
{
    c.insert( c.end( ), { 0x4D, 0x85, 0xC0 } );
    emit_je( c, fail );
    c.insert( c.end( ), { 0x49, 0x81, 0xF8, 0x00, 0x00, 0x01, 0x00 } );
    emit_jcc( c, fail, 0x82 );
    c.insert( c.end( ), { 0x49, 0xBB } );
    append_u64( c, 0x00007FFFFFFFFFFFull );
    c.insert( c.end( ), { 0x4D, 0x3B, 0xC3 } );
    emit_jcc( c, fail, 0x87 );
    c.insert( c.end( ), { 0x41, 0xF6, 0xC0, align_mask } );
    emit_jcc( c, fail, 0x85 );
}

auto emit_user_ptr_r9( std::vector<uint8_t>& c, std::vector<size_t>& fail, uint8_t align_mask ) -> void
{
    c.insert( c.end( ), { 0x4D, 0x85, 0xC9 } );
    emit_je( c, fail );
    c.insert( c.end( ), { 0x49, 0x81, 0xF9, 0x00, 0x00, 0x01, 0x00 } );
    emit_jcc( c, fail, 0x82 );
    c.insert( c.end( ), { 0x49, 0xBB } );
    append_u64( c, 0x00007FFFFFFFFFFFull );
    c.insert( c.end( ), { 0x4D, 0x3B, 0xCB } );
    emit_jcc( c, fail, 0x87 );
    c.insert( c.end( ), { 0x41, 0xF6, 0xC1, align_mask } );
    emit_jcc( c, fail, 0x85 );
}

auto emit_user_ptr_rdx( std::vector<uint8_t>& c, std::vector<size_t>& fail, uint8_t align_mask ) -> void
{
    c.insert( c.end( ), { 0x48, 0x85, 0xD2 } );
    emit_je( c, fail );
    c.insert( c.end( ), { 0x48, 0x81, 0xFA, 0x00, 0x00, 0x01, 0x00 } );
    emit_jcc( c, fail, 0x82 );
    c.insert( c.end( ), { 0x49, 0xBB } );
    append_u64( c, 0x00007FFFFFFFFFFFull );
    c.insert( c.end( ), { 0x4C, 0x3B, 0xD3 } );
    emit_jcc( c, fail, 0x87 );
    c.insert( c.end( ), { 0xF6, 0xC2, align_mask } );
    emit_jcc( c, fail, 0x85 );
}

auto emit_movd_xmm1_eax( std::vector<uint8_t>& c, uint32_t bits ) -> void
{
    c.push_back( 0xB8 );
    append_u32( c, bits );
    c.insert( c.end( ), { 0x66, 0x0F, 0x6E, 0xC8 } );
}

auto emit_cosine_gate( std::vector<uint8_t>& c, std::vector<size_t>& fail ) -> void
{
    c.insert( c.end( ), { 0x0F, 0x57, 0xDB } );
    c.insert( c.end( ), { 0x0F, 0x2E, 0xD3 } );
    emit_jcc( c, fail, 0x86 );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC1 } );
    emit_movd_xmm1_eax( c, bits_f32( 0.64f ) );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC1 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xD2 } );
    c.insert( c.end( ), { 0x0F, 0x2E, 0xD0 } );
    emit_jcc( c, fail, 0x82 );
}

auto emit_copy_last( std::vector<uint8_t>& c, uint8_t orig, uint8_t dir ) -> void
{
    c.insert( c.end( ), { 0x8B, 0x42, orig } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x48 } );
    c.insert( c.end( ), { 0x8B, 0x42, ( uint8_t )( orig + 4 ) } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x4C } );
    c.insert( c.end( ), { 0x8B, 0x42, ( uint8_t )( orig + 8 ) } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x50 } );
    c.insert( c.end( ), { 0x8B, 0x42, dir } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x54 } );
    c.insert( c.end( ), { 0x8B, 0x42, ( uint8_t )( dir + 4 ) } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x58 } );
    c.insert( c.end( ), { 0x8B, 0x42, ( uint8_t )( dir + 8 ) } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x5C } );
}

auto emit_mark_rewrite( std::vector<uint8_t>& c ) -> void
{
    c.insert( c.end( ), { 0x41, 0xC7, 0x42, 0x60, 0x01, 0x00, 0x00, 0x00 } );
    c.insert( c.end( ), { 0xF0, 0x49, 0xFF, 0x42, 0x38 } );
}

auto emit_mark_skip( std::vector<uint8_t>& c ) -> void
{
    c.insert( c.end( ), { 0x41, 0xC7, 0x42, 0x60, 0x00, 0x00, 0x00, 0x00 } );
    c.insert( c.end( ), { 0xF0, 0x49, 0xFF, 0x42, 0x40 } );
}

// Floor traces start below the camera and point down. Gun traces start at the
// camera and already point at the lock (cos > ~0.8). Combat fail must
// passthrough — never fall into another arg layout.
auto emit_combat_gate( std::vector<uint8_t>& c, uint8_t orig, uint8_t dir, std::vector<size_t>& fail ) -> void
{
    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x42, ( uint8_t )( orig + 4 ) } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x5C, 0x42, 0x24 } );
    emit_movd_xmm1_eax( c, bits_f32( -1.0f ) );
    c.insert( c.end( ), { 0x0F, 0x2E, 0xC1 } );
    emit_jcc( c, fail, 0x82 );

    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x42, dir } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC0 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x4A, ( uint8_t )( dir + 8 ) } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xC1 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x4A, ( uint8_t )( dir + 4 ) } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0x0F, 0x2E, 0xC8 } );
    emit_jcc( c, fail, 0x87 );

    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x42, dir } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC0 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x4A, ( uint8_t )( dir + 4 ) } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xC1 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x4A, ( uint8_t )( dir + 8 ) } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xC1 } );

    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x4A, 0x2C } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x4A, 0x2C } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x52, 0x30 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x52, 0x30 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xCA } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x52, 0x34 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x52, 0x34 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xCA } );

    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x52, dir } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x52, 0x2C } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x5A, ( uint8_t )( dir + 4 ) } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x5A, 0x30 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xD3 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x10, 0x5A, ( uint8_t )( dir + 8 ) } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x5A, 0x34 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xD3 } );

    emit_cosine_gate( c, fail );
}

auto emit_copy_last_r8r9( std::vector<uint8_t>& c ) -> void
{
    c.insert( c.end( ), { 0x41, 0x8B, 0x00 } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x48 } );
    c.insert( c.end( ), { 0x41, 0x8B, 0x40, 0x04 } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x4C } );
    c.insert( c.end( ), { 0x41, 0x8B, 0x40, 0x08 } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x50 } );
    c.insert( c.end( ), { 0x41, 0x8B, 0x01 } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x54 } );
    c.insert( c.end( ), { 0x41, 0x8B, 0x41, 0x04 } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x58 } );
    c.insert( c.end( ), { 0x41, 0x8B, 0x41, 0x08 } );
    c.insert( c.end( ), { 0x41, 0x89, 0x42, 0x5C } );
}

auto emit_combat_gate_r8r9( std::vector<uint8_t>& c, std::vector<size_t>& fail ) -> void
{
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x40, 0x04 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x5C, 0x42, 0x24 } );
    emit_movd_xmm1_eax( c, bits_f32( -1.0f ) );
    c.insert( c.end( ), { 0x0F, 0x2E, 0xC1 } );
    emit_jcc( c, fail, 0x82 );

    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x01 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC0 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x49, 0x08 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xC1 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x49, 0x04 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0x0F, 0x2E, 0xC8 } );
    emit_jcc( c, fail, 0x87 );

    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x01 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC0 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x49, 0x04 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xC1 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x49, 0x08 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x59, 0xC9 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xC1 } );

    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x4A, 0x2C } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x4A, 0x2C } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x52, 0x30 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x52, 0x30 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xCA } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x52, 0x34 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x52, 0x34 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xCA } );

    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x11 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x52, 0x2C } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x59, 0x04 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x5A, 0x30 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xD3 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x10, 0x59, 0x08 } );
    c.insert( c.end( ), { 0xF3, 0x41, 0x0F, 0x59, 0x5A, 0x34 } );
    c.insert( c.end( ), { 0xF3, 0x0F, 0x58, 0xD3 } );

    emit_cosine_gate( c, fail );
}

auto emit_save12( std::vector<uint8_t>& c, std::initializer_list<uint8_t> load0,
    std::initializer_list<uint8_t> load4, std::initializer_list<uint8_t> load8, uint8_t dst ) -> void
{
    c.insert( c.end( ), load0 );
    c.insert( c.end( ), { 0x89, 0x44, 0x24, dst } );
    c.insert( c.end( ), load4 );
    c.insert( c.end( ), { 0x89, 0x44, 0x24, ( uint8_t )( dst + 4 ) } );
    c.insert( c.end( ), load8 );
    c.insert( c.end( ), { 0x89, 0x44, 0x24, ( uint8_t )( dst + 8 ) } );
}

auto emit_restore12( std::vector<uint8_t>& c, uint8_t src, std::initializer_list<uint8_t> d0,
    std::initializer_list<uint8_t> d4, std::initializer_list<uint8_t> d8 ) -> void
{
    c.insert( c.end( ), { 0x8B, 0x44, 0x24, src } );
    c.insert( c.end( ), d0 );
    c.insert( c.end( ), { 0x8B, 0x44, 0x24, ( uint8_t )( src + 4 ) } );
    c.insert( c.end( ), d4 );
    c.insert( c.end( ), { 0x8B, 0x44, 0x24, ( uint8_t )( src + 8 ) } );
    c.insert( c.end( ), d8 );
}

auto emit_write12_to( std::vector<uint8_t>& c, uint8_t src, std::initializer_list<uint8_t> d0,
    std::initializer_list<uint8_t> d4, std::initializer_list<uint8_t> d8 ) -> void
{
    c.insert( c.end( ), { 0x41, 0x8B, 0x42, src } );
    c.insert( c.end( ), d0 );
    c.insert( c.end( ), { 0x41, 0x8B, 0x42, ( uint8_t )( src + 4 ) } );
    c.insert( c.end( ), d4 );
    c.insert( c.end( ), { 0x41, 0x8B, 0x42, ( uint8_t )( src + 8 ) } );
    c.insert( c.end( ), d8 );
}

auto emit_jmp_orig( std::vector<uint8_t>& c, uintptr_t orig ) -> void
{
    c.insert( c.end( ), { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 } );
    append_u64( c, orig );
}

auto looks_wrapper( uintptr_t fn ) -> bool;

// Fast path is a raw jmp orig so unhooked Raycast keeps working.
// Slow path rewrites Vector3s for one call, then puts the originals back.
auto make_safe_thunk( uintptr_t state, uintptr_t orig ) -> std::vector<uint8_t>
{
    std::vector<uint8_t> c;
    c.reserve( 800 );

    c.insert( c.end( ), { 0x49, 0xBA } );
    append_u64( c, state );
    c.insert( c.end( ), { 0xF0, 0x49, 0xFF, 0x42, 0x18 } );
    c.insert( c.end( ), { 0x41, 0x83, 0x3A, 0x00 } );
    std::vector<size_t> to_slow;
    emit_jcc( c, to_slow, 0x85 );
    const size_t pass = c.size( );
    emit_jmp_orig( c, orig );

    const size_t slow = c.size( );
    for ( auto o : to_slow )
        patch_rel32( c, o, slow );

    c.insert( c.end( ), { 0x48, 0x83, 0xEC, 0x68 } );
    c.insert( c.end( ), { 0x48, 0x89, 0x4C, 0x24, 0x28 } );
    c.insert( c.end( ), { 0x48, 0x89, 0x54, 0x24, 0x30 } );
    c.insert( c.end( ), { 0x4C, 0x89, 0x44, 0x24, 0x38 } );
    c.insert( c.end( ), { 0x4C, 0x89, 0x4C, 0x24, 0x40 } );

    auto restore_args = [&]( )
    {
        c.insert( c.end( ), { 0x48, 0x8B, 0x4C, 0x24, 0x28 } );
        c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
        c.insert( c.end( ), { 0x4C, 0x8B, 0x44, 0x24, 0x38 } );
        c.insert( c.end( ), { 0x4C, 0x8B, 0x4C, 0x24, 0x40 } );
        c.insert( c.end( ), { 0x48, 0x8B, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00 } );
        c.insert( c.end( ), { 0x48, 0x89, 0x44, 0x24, 0x20 } );
    };

    auto call_orig = [&]( )
    {
        restore_args( );
        c.insert( c.end( ), { 0x48, 0xB8 } );
        append_u64( c, orig );
        c.insert( c.end( ), { 0xFF, 0xD0 } );
    };

    auto epilogue = [&]( )
    {
        c.insert( c.end( ), { 0x48, 0x83, 0xC4, 0x68 } );
        c.push_back( 0xC3 );
    };

    auto float_at_r8 = [&]( std::vector<size_t>& fail )
    {
        c.insert( c.end( ), { 0x41, 0x8B, 0x00 } );
        emit_float_ok( c, fail );
    };
    auto float_at_r9 = [&]( std::vector<size_t>& fail )
    {
        c.insert( c.end( ), { 0x41, 0x8B, 0x01 } );
        emit_float_ok( c, fail );
    };
    auto float_at_rdx = [&]( std::vector<size_t>& fail )
    {
        c.insert( c.end( ), { 0x8B, 0x02 } );
        emit_float_ok( c, fail );
    };

    std::vector<size_t> to_b;
    std::vector<size_t> to_pass_a;
    emit_user_ptr_r8( c, to_b, 3 );
    emit_user_ptr_r9( c, to_b, 3 );
    float_at_r8( to_b );
    float_at_r9( to_b );
    emit_copy_last_r8r9( c );
    emit_combat_gate_r8r9( c, to_pass_a );
    emit_mark_rewrite( c );
    emit_save12( c, { 0x41, 0x8B, 0x00 }, { 0x41, 0x8B, 0x40, 0x04 }, { 0x41, 0x8B, 0x40, 0x08 }, 0x48 );
    emit_save12( c, { 0x41, 0x8B, 0x01 }, { 0x41, 0x8B, 0x41, 0x04 }, { 0x41, 0x8B, 0x41, 0x08 }, 0x54 );
    emit_write12_to( c, 0x2C, { 0x41, 0x89, 0x01 }, { 0x41, 0x89, 0x41, 0x04 }, { 0x41, 0x89, 0x41, 0x08 } );
    c.insert( c.end( ), { 0x41, 0xF6, 0x42, 0x04, 0x01 } );
    std::vector<size_t> skip_ori_a;
    emit_je( c, skip_ori_a );
    emit_write12_to( c, 0x20, { 0x41, 0x89, 0x00 }, { 0x41, 0x89, 0x40, 0x04 }, { 0x41, 0x89, 0x40, 0x08 } );
    const size_t after_ori_a = c.size( );
    for ( auto o : skip_ori_a )
        patch_rel32( c, o, after_ori_a );
    call_orig( );
    c.insert( c.end( ), { 0x4C, 0x8B, 0x44, 0x24, 0x38 } );
    c.insert( c.end( ), { 0x4C, 0x8B, 0x4C, 0x24, 0x40 } );
    emit_restore12( c, 0x48, { 0x41, 0x89, 0x00 }, { 0x41, 0x89, 0x40, 0x04 }, { 0x41, 0x89, 0x40, 0x08 } );
    emit_restore12( c, 0x54, { 0x41, 0x89, 0x01 }, { 0x41, 0x89, 0x41, 0x04 }, { 0x41, 0x89, 0x41, 0x08 } );
    epilogue( );

    const size_t lab_b = c.size( );
    for ( auto o : to_b )
        patch_rel32( c, o, lab_b );

    std::vector<size_t> to_c;
    c.insert( c.end( ), { 0x4C, 0x8B, 0xC2 } );
    emit_user_ptr_r8( c, to_c, 3 );
    c.insert( c.end( ), { 0x4C, 0x8B, 0x44, 0x24, 0x38 } );
    emit_user_ptr_r8( c, to_c, 3 );
    c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
    float_at_rdx( to_c );
    float_at_r8( to_c );
    emit_save12( c, { 0x8B, 0x02 }, { 0x8B, 0x42, 0x04 }, { 0x8B, 0x42, 0x08 }, 0x48 );
    emit_save12( c, { 0x41, 0x8B, 0x00 }, { 0x41, 0x8B, 0x40, 0x04 }, { 0x41, 0x8B, 0x40, 0x08 }, 0x54 );
    emit_write12_to( c, 0x20, { 0x89, 0x02 }, { 0x89, 0x42, 0x04 }, { 0x89, 0x42, 0x08 } );
    emit_write12_to( c, 0x2C, { 0x41, 0x89, 0x00 }, { 0x41, 0x89, 0x40, 0x04 }, { 0x41, 0x89, 0x40, 0x08 } );
    call_orig( );
    c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
    c.insert( c.end( ), { 0x4C, 0x8B, 0x44, 0x24, 0x38 } );
    emit_restore12( c, 0x48, { 0x89, 0x02 }, { 0x89, 0x42, 0x04 }, { 0x89, 0x42, 0x08 } );
    emit_restore12( c, 0x54, { 0x41, 0x89, 0x00 }, { 0x41, 0x89, 0x40, 0x04 }, { 0x41, 0x89, 0x40, 0x08 } );
    epilogue( );

    const size_t lab_c = c.size( );
    for ( auto o : to_c )
        patch_rel32( c, o, lab_c );

    std::vector<size_t> to_pass;
    c.insert( c.end( ), { 0x4C, 0x8B, 0xC2 } );
    emit_user_ptr_r8( c, to_pass, 7 );
    c.insert( c.end( ), { 0x41, 0x8B, 0x40, 0x08 } );
    emit_float_ok( c, to_pass );
    c.insert( c.end( ), { 0x41, 0x8B, 0x40, 0x28 } );
    emit_float_ok( c, to_pass );
    emit_save12( c, { 0x41, 0x8B, 0x40, 0x08 }, { 0x41, 0x8B, 0x40, 0x0C }, { 0x41, 0x8B, 0x40, 0x10 }, 0x48 );
    emit_save12( c, { 0x41, 0x8B, 0x40, 0x28 }, { 0x41, 0x8B, 0x40, 0x2C }, { 0x41, 0x8B, 0x40, 0x30 }, 0x54 );
    emit_write12_to( c, 0x20, { 0x41, 0x89, 0x40, 0x08 }, { 0x41, 0x89, 0x40, 0x0C }, { 0x41, 0x89, 0x40, 0x10 } );
    emit_write12_to( c, 0x2C, { 0x41, 0x89, 0x40, 0x28 }, { 0x41, 0x89, 0x40, 0x2C }, { 0x41, 0x89, 0x40, 0x30 } );
    call_orig( );
    c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
    c.insert( c.end( ), { 0x4C, 0x8B, 0xC2 } );
    emit_restore12( c, 0x48, { 0x41, 0x89, 0x40, 0x08 }, { 0x41, 0x89, 0x40, 0x0C }, { 0x41, 0x89, 0x40, 0x10 } );
    emit_restore12( c, 0x54, { 0x41, 0x89, 0x40, 0x28 }, { 0x41, 0x89, 0x40, 0x2C }, { 0x41, 0x89, 0x40, 0x30 } );
    epilogue( );

    const size_t skip_site = c.size( );
    for ( auto o : to_pass_a )
        patch_rel32( c, o, skip_site );
    emit_mark_skip( c );
    const size_t miss = c.size( );
    for ( auto o : to_pass )
        patch_rel32( c, o, miss );
    c.insert( c.end( ), { 0x48, 0x8B, 0x4C, 0x24, 0x28 } );
    c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
    c.insert( c.end( ), { 0x4C, 0x8B, 0x44, 0x24, 0x38 } );
    c.insert( c.end( ), { 0x4C, 0x8B, 0x4C, 0x24, 0x40 } );
    c.insert( c.end( ), { 0x48, 0x83, 0xC4, 0x68 } );
    emit_jmp_orig( c, orig );
    ( void )pass;
    return c;
}

auto make_variant_thunk( uintptr_t state, uintptr_t orig ) -> std::vector<uint8_t>
{
    std::vector<uint8_t> c;
    c.reserve( 1024 );

    c.insert( c.end( ), { 0x49, 0xBA } );
    append_u64( c, state );
    c.insert( c.end( ), { 0xF0, 0x49, 0xFF, 0x42, 0x18 } );
    c.insert( c.end( ), { 0x41, 0x83, 0x3A, 0x00 } );
    std::vector<size_t> to_slow;
    emit_jcc( c, to_slow, 0x85 );
    emit_jmp_orig( c, orig );

    const size_t slow = c.size( );
    for ( auto o : to_slow )
        patch_rel32( c, o, slow );

    c.insert( c.end( ), { 0x48, 0x83, 0xEC, 0x68 } );
    c.insert( c.end( ), { 0x48, 0x89, 0x4C, 0x24, 0x28 } );
    c.insert( c.end( ), { 0x48, 0x89, 0x54, 0x24, 0x30 } );
    c.insert( c.end( ), { 0x4C, 0x89, 0x44, 0x24, 0x38 } );
    c.insert( c.end( ), { 0x4C, 0x89, 0x4C, 0x24, 0x40 } );

    auto restore_args = [&]( )
    {
        c.insert( c.end( ), { 0x48, 0x8B, 0x4C, 0x24, 0x28 } );
        c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
        c.insert( c.end( ), { 0x4C, 0x8B, 0x44, 0x24, 0x38 } );
        c.insert( c.end( ), { 0x4C, 0x8B, 0x4C, 0x24, 0x40 } );
    };
    auto call_orig = [&]( )
    {
        restore_args( );
        c.insert( c.end( ), { 0x48, 0xB8 } );
        append_u64( c, orig );
        c.insert( c.end( ), { 0xFF, 0xD0 } );
    };
    auto epilogue = [&]( )
    {
        c.insert( c.end( ), { 0x48, 0x83, 0xC4, 0x68 } );
        c.push_back( 0xC3 );
    };

    auto try_pair = [&]( uint8_t a, uint8_t b, std::vector<size_t>& fail_layout, std::vector<size_t>& fail_combat )
    {
        emit_user_ptr_rdx( c, fail_layout, 7 );
        c.insert( c.end( ), { 0x8B, 0x42, a } );
        emit_float_ok( c, fail_layout );
        c.insert( c.end( ), { 0x8B, 0x42, b } );
        emit_float_ok( c, fail_layout );
        emit_copy_last( c, a, b );
        emit_combat_gate( c, a, b, fail_combat );
        emit_mark_rewrite( c );
        emit_save12( c, { 0x8B, 0x42, a }, { 0x8B, 0x42, ( uint8_t )( a + 4 ) }, { 0x8B, 0x42, ( uint8_t )( a + 8 ) }, 0x48 );
        emit_save12( c, { 0x8B, 0x42, b }, { 0x8B, 0x42, ( uint8_t )( b + 4 ) }, { 0x8B, 0x42, ( uint8_t )( b + 8 ) }, 0x54 );
        emit_write12_to( c, 0x2C, { 0x89, 0x42, b }, { 0x89, 0x42, ( uint8_t )( b + 4 ) }, { 0x89, 0x42, ( uint8_t )( b + 8 ) } );
        c.insert( c.end( ), { 0x41, 0xF6, 0x42, 0x04, 0x01 } );
        std::vector<size_t> skip_ori;
        emit_je( c, skip_ori );
        emit_write12_to( c, 0x20, { 0x89, 0x42, a }, { 0x89, 0x42, ( uint8_t )( a + 4 ) }, { 0x89, 0x42, ( uint8_t )( a + 8 ) } );
        const size_t after_ori = c.size( );
        for ( auto o : skip_ori )
            patch_rel32( c, o, after_ori );
        call_orig( );
        c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
        emit_restore12( c, 0x48, { 0x89, 0x42, a }, { 0x89, 0x42, ( uint8_t )( a + 4 ) }, { 0x89, 0x42, ( uint8_t )( a + 8 ) } );
        emit_restore12( c, 0x54, { 0x89, 0x42, b }, { 0x89, 0x42, ( uint8_t )( b + 4 ) }, { 0x89, 0x42, ( uint8_t )( b + 8 ) } );
        epilogue( );
    };

    std::vector<size_t> to_layout;
    std::vector<size_t> to_skip;
    std::vector<size_t> to_miss;
    try_pair( 0x08, 0x28, to_layout, to_skip );
    const size_t lab_b = c.size( );
    for ( auto o : to_layout )
        patch_rel32( c, o, lab_b );

    c.insert( c.end( ), { 0x48, 0x8B, 0x54, 0x24, 0x30 } );
    try_pair( 0x00, 0x20, to_miss, to_skip );
    const size_t skip_site = c.size( );
    for ( auto o : to_skip )
        patch_rel32( c, o, skip_site );
    emit_mark_skip( c );
    const size_t miss = c.size( );
    for ( auto o : to_miss )
        patch_rel32( c, o, miss );

    restore_args( );
    c.insert( c.end( ), { 0x48, 0x83, 0xC4, 0x68 } );
    emit_jmp_orig( c, orig );
    return c;
}

auto make_wrapper_thunk( uintptr_t state, uintptr_t orig ) -> std::vector<uint8_t>
{
    return make_variant_thunk( state, orig );
}

auto make_hook_thunk( uintptr_t state, uintptr_t orig, bool ) -> std::vector<uint8_t>
{
    if ( looks_wrapper( orig ) )
        return make_variant_thunk( state, orig );
    return make_safe_thunk( state, orig );
}

auto make_count_thunk( uintptr_t state, uintptr_t orig ) -> std::vector<uint8_t>
{
    return make_hook_thunk( state, orig, false );
}

auto slot_from_object( uintptr_t obj ) -> uintptr_t
{
    if ( !obj || !usermode->is_valid( obj ) )
        return 0;
    static constexpr uintptr_t k_off[] = {
        offsets::function_descriptor::function, 0x78, 0x88, 0x70, 0x90,
    };
    for ( uintptr_t off : k_off )
    {
        const uintptr_t fn = usermode->read<uintptr_t>( obj + off );
        if ( looks_code( fn ) )
            return obj + off;
    }
    return 0;
}

auto slot_from_data_scan( ) -> uintptr_t
{
    if ( !g_img || !g_str_raycast )
        return 0;

    IMAGE_DOS_HEADER dos{};
    if ( !usermode->read_buf( g_img, &dos, sizeof( dos ) ) || dos.e_magic != IMAGE_DOS_SIGNATURE )
        return 0;
    IMAGE_NT_HEADERS64 nt{};
    const uintptr_t nt_addr = g_img + ( uintptr_t )dos.e_lfanew;
    if ( !usermode->read_buf( nt_addr, &nt, sizeof( nt ) ) || nt.Signature != IMAGE_NT_SIGNATURE )
        return 0;
    const uintptr_t section_base = nt_addr + offsetof( IMAGE_NT_HEADERS64, OptionalHeader )
        + nt.FileHeader.SizeOfOptionalHeader;

    for ( WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i )
    {
        IMAGE_SECTION_HEADER section{};
        if ( !usermode->read_buf( section_base + ( uintptr_t )i * sizeof( section ), &section, sizeof( section ) ) )
            break;
        if ( ( section.Characteristics & IMAGE_SCN_MEM_WRITE ) == 0 )
            continue;
        const uintptr_t start = g_img + section.VirtualAddress;
        const size_t size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        if ( !start || size < 16 || size > 32 * 1024 * 1024 )
            continue;

        std::vector<uint8_t> buf( size );
        if ( !usermode->read_buf( start, buf.data( ), size ) )
            continue;
        for ( size_t off = 0; off + 8 <= size; off += 8 )
        {
            uintptr_t q = 0;
            std::memcpy( &q, buf.data( ) + off, sizeof( q ) );
            if ( q != g_str_raycast )
                continue;
            if ( uintptr_t s = slot_from_object( start + off - 8 ) )
                return s;
        }
    }
    return 0;
}

auto find_raycast_slot( ) -> uintptr_t
{
    const uintptr_t ws = g->engine.workspace.load( );
    const uintptr_t cd = ws ? usermode->read<uintptr_t>( ws + offsets::instance::class_descriptor ) : 0;
    resolve_raycast_image( cd ? cd : ws );

    if ( cd )
    {
        const uintptr_t fd = walk_cd_for( cd, g_str_raycast, "Raycast" );
        if ( uintptr_t s = slot_from_object( fd ) )
            return s;
    }
    if ( uintptr_t s = slot_from_data_scan( ) )
        return s;
    g->logger.print( "[silent] no heap BoundFn intern=%p cd=%p cdname=%s",
        ( void* )g_str_raycast, ( void* )cd, desc_cstr( cd ).c_str( ) );
    return 0;
}

auto find_cave_in_module( uintptr_t module_base, size_t need, uintptr_t min_offset ) -> uintptr_t
{
    if ( !ok_addr( module_base ) || !need )
        return 0;

    IMAGE_DOS_HEADER dos{};
    if ( !usermode->read_buf( module_base, &dos, sizeof( dos ) ) || dos.e_magic != IMAGE_DOS_SIGNATURE )
        return 0;

    IMAGE_NT_HEADERS64 nt{};
    const uintptr_t nt_addr = module_base + ( uintptr_t )dos.e_lfanew;
    if ( !usermode->read_buf( nt_addr, &nt, sizeof( nt ) ) || nt.Signature != IMAGE_NT_SIGNATURE )
        return 0;

    const uintptr_t section_base = nt_addr + offsetof( IMAGE_NT_HEADERS64, OptionalHeader )
        + nt.FileHeader.SizeOfOptionalHeader;

    for ( WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i )
    {
        IMAGE_SECTION_HEADER section{};
        if ( !usermode->read_buf( section_base + ( uintptr_t )i * sizeof( section ), &section, sizeof( section ) ) )
            break;
        if ( ( section.Characteristics & IMAGE_SCN_MEM_EXECUTE ) == 0 )
            continue;

        const uintptr_t section_off = section.VirtualAddress;
        size_t section_size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        if ( !section_off || section_size < need )
            continue;

        uintptr_t scan_off = section_off;
        if ( scan_off < min_offset )
            scan_off = min_offset;
        if ( scan_off >= section_off + section_size )
            continue;

        const uintptr_t scan_start = module_base + scan_off;
        const size_t scan_size = ( size_t )( section_off + section_size - scan_off );
        if ( scan_size > 8 * 1024 * 1024 )
            continue;

        std::vector<uint8_t> buf( scan_size );
        if ( !usermode->read_buf( scan_start, buf.data( ), buf.size( ) ) )
            continue;

        size_t run_start = 0;
        size_t run_len = 0;
        for ( size_t j = 0; j < buf.size( ); ++j )
        {
            const uint8_t b = buf[j];
            if ( b != 0x00 && b != 0xCC && b != 0x90 )
            {
                run_len = 0;
                run_start = j + 1;
                continue;
            }
            ++run_len;
            if ( run_len < need )
                continue;
            const uintptr_t cand = scan_start + run_start;
            const uintptr_t aligned = ( cand + 0x0F ) & ~ ( uintptr_t )0x0F;
            const size_t loss = ( size_t )( aligned - cand );
            if ( run_len < need + loss )
                continue;
            bool used = g_hook.thunk && aligned == g_hook.thunk;
            for ( int p = 0; p < g_hook.npins && !used; ++p )
                used = aligned == g_hook.pins[p].thunk;
            if ( used )
                continue;
            return aligned;
        }
    }
    return 0;
}

auto find_exec_cave( size_t need ) -> uintptr_t
{
    static const wchar_t* k_pref[] = {
        L"winsta.dll", L"win32u.dll", L"dxgi.dll", L"uxtheme.dll", L"dwmapi.dll", L"user32.dll",
    };
    for ( size_t i = 0; i < sizeof( k_pref ) / sizeof( k_pref[0] ); ++i )
    {
        const uintptr_t mod = usermode->module( k_pref[i] );
        if ( !mod )
            continue;
        const uintptr_t cave = find_cave_in_module( mod, need, i == 0 ? 0x2000u : 0x1000u );
        if ( cave )
        {
            g->logger.print( "[silent] raycast cave @ %p prot=0x%x",
                ( void* )cave, query_protect( cave ) );
            return cave;
        }
    }
    return 0;
}

auto find_rw_cave( size_t need ) -> uintptr_t
{
    static const wchar_t* k_pref[] = {
        L"winsta.dll", L"win32u.dll", L"dxgi.dll", L"uxtheme.dll", L"dwmapi.dll",
    };
    for ( size_t i = 0; i < sizeof( k_pref ) / sizeof( k_pref[0] ); ++i )
    {
        const uintptr_t module_base = usermode->module( k_pref[i] );
        if ( !ok_addr( module_base ) || !need )
            continue;

        IMAGE_DOS_HEADER dos{};
        if ( !usermode->read_buf( module_base, &dos, sizeof( dos ) ) || dos.e_magic != IMAGE_DOS_SIGNATURE )
            continue;
        IMAGE_NT_HEADERS64 nt{};
        const uintptr_t nt_addr = module_base + ( uintptr_t )dos.e_lfanew;
        if ( !usermode->read_buf( nt_addr, &nt, sizeof( nt ) ) || nt.Signature != IMAGE_NT_SIGNATURE )
            continue;

        const uintptr_t section_base = nt_addr + offsetof( IMAGE_NT_HEADERS64, OptionalHeader )
            + nt.FileHeader.SizeOfOptionalHeader;

        for ( WORD s = 0; s < nt.FileHeader.NumberOfSections; ++s )
        {
            IMAGE_SECTION_HEADER section{};
            if ( !usermode->read_buf( section_base + ( uintptr_t )s * sizeof( section ), &section, sizeof( section ) ) )
                break;
            const DWORD ch = section.Characteristics;
            if ( ( ch & IMAGE_SCN_MEM_WRITE ) == 0 || ( ch & IMAGE_SCN_MEM_EXECUTE ) )
                continue;

            const uintptr_t section_off = section.VirtualAddress;
            size_t section_size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
            if ( !section_off || section_size < need )
                continue;

            uintptr_t scan_off = section_off;
            const uintptr_t min_off = 0x40u;
            if ( scan_off < min_off )
                scan_off = min_off;
            if ( scan_off >= section_off + section_size )
                continue;

            const uintptr_t scan_start = module_base + scan_off;
            const size_t scan_size = ( size_t )( section_off + section_size - scan_off );
            if ( scan_size > 2 * 1024 * 1024 )
                continue;

            std::vector<uint8_t> buf( scan_size );
            if ( !usermode->read_buf( scan_start, buf.data( ), buf.size( ) ) )
                continue;

            size_t run_start = 0;
            size_t run_len = 0;
            for ( size_t j = 0; j < buf.size( ); ++j )
            {
                if ( buf[j] != 0x00 )
                {
                    run_len = 0;
                    run_start = j + 1;
                    continue;
                }
                ++run_len;
                if ( run_len < need )
                    continue;
                const uintptr_t cand = scan_start + run_start;
                const uintptr_t aligned = ( cand + 0x0F ) & ~ ( uintptr_t )0x0F;
                const size_t loss = ( size_t )( aligned - cand );
                if ( run_len < need + loss )
                    continue;
                if ( g_hook.state && aligned == g_hook.state )
                    continue;
                g->logger.print( "[silent] rw cave @ %p", ( void* )aligned );
                return aligned;
            }
        }
    }
    return 0;
}

auto clear_locked( ) -> void
{
    if ( !g_hook.state )
        return;
    uint32_t zero = 0;
    usermode->write_buf( g_hook.state + offsetof( raycast_state_t, active ), &zero, sizeof( zero ) );
    g_hook.active = false;
}

auto looks_wrapper( uintptr_t fn ) -> bool
{
    uint8_t b[32]{};
    if ( !ok_addr( fn ) || !usermode->read_buf( fn, b, sizeof( b ) ) )
        return false;
    for ( int i = 0; i < 24; ++i )
    {
        if ( b[i] != 0x48 || b[i + 1] != 0x81 || b[i + 2] != 0xEC )
            continue;
        uint32_t imm = 0;
        std::memcpy( &imm, b + i + 3, 4 );
        if ( imm >= 0x100 )
            return true;
    }
    return false;
}

auto unwrap_stub( uintptr_t p ) -> uintptr_t
{
    if ( !p || !usermode->is_valid( p ) )
        return 0;
    uint8_t b[2]{};
    if ( !usermode->read_buf( p, b, sizeof( b ) ) )
        return 0;
    if ( b[0] != 0x49 || b[1] != 0xBA )
        return 0;
    const uintptr_t orig = usermode->read<uintptr_t>( p + 0x1F );
    if ( in_image( orig ) && looks_code( orig ) )
        return orig;
    return 0;
}

auto restore_fn( uintptr_t fn ) -> uintptr_t
{
    if ( uintptr_t u = unwrap_stub( fn ) )
        fn = u;
    if ( in_image( fn ) && looks_code( fn ) )
        return fn;
    if ( g_img )
    {
        const uintptr_t fb = g_img + offsets::rva::raycast_orig;
        if ( in_image( fb ) && looks_code( fb ) )
            return fb;
    }
    return 0;
}

auto already_pinned( uintptr_t, uintptr_t slot ) -> bool
{
    for ( int i = 0; i < g_hook.npins; ++i )
    {
        if ( slot && g_hook.pins[i].slot == slot )
            return true;
        if ( slot && g_hook.pins[i].site == slot )
            return true;
    }
    return false;
}

auto place_thunk( uintptr_t orig, bool& owned, int kind, uint32_t& out_n ) -> uintptr_t
{
    owned = false;
    out_n = 0;
    std::vector<uint8_t> thunk;
    if ( kind == thunk_count )
        thunk = make_count_thunk( g_hook.state, orig );
    else
        thunk = make_hook_thunk( g_hook.state, orig, kind == thunk_cpp );
    if ( thunk.size( ) > k_stub_bytes )
    {
        g->logger.print( "[silent] thunk too big %zu > %zu", thunk.size( ), k_stub_bytes );
        return 0;
    }
    out_n = ( uint32_t )thunk.size( );

    const size_t need = ( thunk.size( ) + 0x0F ) & ~ ( size_t )0x0F;
    uintptr_t stub = find_exec_cave( need );
    if ( stub )
    {
        ULONG old = 0;
        usermode->protect( stub, thunk.size( ), PAGE_EXECUTE_READWRITE, &old );
        if ( !write_protected( stub, thunk.data( ), thunk.size( ) ) )
            stub = 0;
    }
    if ( !stub )
        return 0;
    if ( !stub )
        return 0;
    FlushInstructionCache( usermode->handle( ), ( void* )stub, thunk.size( ) );
    mark_cfg( stub );
    return stub;
}

auto remember_pin( uintptr_t slot, uintptr_t orig, uintptr_t stub, bool owned, int kind, uintptr_t site, uint32_t n ) -> void
{
    if ( g_hook.npins >= 12 )
        return;
    auto& p = g_hook.pins[g_hook.npins++];
    p.slot = slot;
    p.original = orig;
    p.thunk = stub;
    p.owned = owned;
    p.kind = kind;
    p.site = site;
    p.thunk_n = n;
    if ( !g_hook.slot )
    {
        g_hook.slot = slot ? slot : site;
        g_hook.original = orig;
        g_hook.thunk = stub;
        g_hook.thunk_owned = owned;
    }
}

auto pin_slot_swap( uintptr_t slot, uintptr_t fn, const char* name, int kind ) -> bool
{
    if ( !ok_addr( slot ) || !ok_addr( fn ) || already_pinned( fn, slot ) || g_hook.npins >= 12 )
        return false;
    bool owned = false;
    uint32_t n = 0;
    const uintptr_t stub = place_thunk( fn, owned, kind, n );
    if ( !stub )
        return false;
    if ( !write_protected( slot, &stub, sizeof( stub ) )
        || usermode->read<uintptr_t>( slot ) != stub )
    {
        if ( owned )
            usermode->free( stub );
        return false;
    }
    remember_pin( slot, fn, stub, owned, kind, 0, n );
    g->logger.print( "[silent] %s BoundFunc slot=%p orig=%p stub=%p rva=0x%llX %s",
        name, ( void* )slot, ( void* )fn, ( void* )stub,
        ( unsigned long long )( g_img ? fn - g_img : 0 ),
        looks_wrapper( fn ) ? "lua" : "native" );
    return true;
}

auto loads_vec3( uintptr_t fn ) -> bool
{
    uint8_t b[48]{};
    if ( !usermode->read_buf( fn, b, sizeof( b ) ) )
        return false;
    for ( int i = 0; i < 44; ++i )
    {
        if ( b[i] == 0xF3 && b[i + 1] == 0x0F && b[i + 2] == 0x10 )
            return true;
        if ( b[i] == 0xF3 && b[i + 1] == 0x41 && b[i + 2] == 0x0F && b[i + 3] == 0x10 )
            return true;
        if ( b[i] == 0x0F && b[i + 1] == 0x10 && ( b[i + 2] & 0xC7 ) == 0x02 )
            return true;
    }
    return false;
}

auto pin_call_site( uintptr_t site, uintptr_t native, const char* name ) -> bool
{
    if ( !ok_addr( site ) || !ok_addr( native ) || already_pinned( native, site ) || g_hook.npins >= 12 )
        return false;
    uint8_t op = 0;
    if ( !usermode->read_buf( site, &op, 1 ) || op != 0xE8 )
        return false;
    bool owned = false;
    uint32_t n = 0;
    const uintptr_t stub = place_thunk( native, owned, thunk_cpp, n );
    if ( !stub )
        return false;
    const int32_t rel = ( int32_t )( ( ptrdiff_t )stub - ( ptrdiff_t )( site + 5 ) );
    uint8_t buf[5] = { 0xE8, 0, 0, 0, 0 };
    std::memcpy( buf + 1, &rel, 4 );
    uint8_t saved[5]{};
    usermode->read_buf( site, saved, 5 );
    if ( !write_protected( site, buf, 5 ) )
    {
        if ( owned )
            usermode->free( stub );
        return false;
    }
    remember_pin( 0, native, stub, owned, thunk_cpp, site, n );
    g_hook.pins[g_hook.npins - 1].saved[0] = saved[0];
    g_hook.pins[g_hook.npins - 1].saved[1] = saved[1];
    g_hook.pins[g_hook.npins - 1].saved[2] = saved[2];
    g_hook.pins[g_hook.npins - 1].saved[3] = saved[3];
    g_hook.pins[g_hook.npins - 1].saved[4] = saved[4];
    g->logger.print( "[silent] %s inner orig=%p site=%p", name, ( void* )native, ( void* )site );
        return true;
    }

auto hook_inner_calls( uintptr_t, const char* ) -> void
{
}

auto bound_fn_slot( uintptr_t fd, uintptr_t& out_fn ) -> uintptr_t
{
    if ( !fd )
        return 0;
    static constexpr uintptr_t k_off[] = {
        offsets::function_descriptor::function, 0x78, 0x88, 0x70, 0x90,
    };
    for ( uintptr_t off : k_off )
    {
        uintptr_t fn = usermode->read<uintptr_t>( fd + off );
        fn = restore_fn( fn );
        if ( !fn )
            continue;
        out_fn = fn;
        return fd + off;
    }
    return 0;
}

auto log_cd_methods( uintptr_t cd ) -> void
{
    uintptr_t cds[24]{};
    int n = 0;
    collect_cds( cd, cds, n, 24 );
    int shown = 0;
    for ( int i = 0; i < n && shown < 16; ++i )
    {
        g->logger.print( "[silent] cd[%d]=%p %s", i, ( void* )cds[i], desc_cstr( cds[i] ).c_str( ) );
        for ( uintptr_t off = 0x20; off <= 0x1E0 && shown < 16; off += 8 )
        {
            visit_fd_list(
                usermode->read<uintptr_t>( cds[i] + off ),
                usermode->read<uint64_t>( cds[i] + off + 8 ),
                [&]( uintptr_t fd )
                {
                    if ( shown >= 16 || !fd )
                        return;
                    const std::string nm = desc_cstr( fd );
                    if ( nm.empty( ) )
                        return;
                    const uintptr_t fn = native_at( fd );
                    g->logger.print( "[silent]  method %s fd=%p fn=%p",
                        nm.c_str( ), ( void* )fd, ( void* )fn );
                    ++shown;
                } );
        }
    }
}

auto hook_fd_slots( uintptr_t fd, const char* name ) -> void
{
    uintptr_t fn = 0;
    const uintptr_t slot = bound_fn_slot( fd, fn );
    if ( !slot || !fn )
        return;
    pin_slot_swap( slot, fn, name, thunk_rewrite );
}

auto uninstall_locked( ) -> void
{
    clear_locked( );
    for ( int i = 0; i < g_hook.npins; ++i )
    {
        auto& p = g_hook.pins[i];
        if ( p.site && p.saved[0] == 0xE8 )
            write_protected( p.site, p.saved, 5 );
        if ( p.slot )
        {
            uintptr_t orig = restore_fn( p.original );
            if ( orig )
            {
                write_protected( p.slot, &orig, sizeof( orig ) );
                const uintptr_t now = usermode->read<uintptr_t>( p.slot );
                if ( now == p.thunk )
                    g->logger.print( "[silent] unhook slot still thunk %p, leaving stub", ( void* )p.thunk );
            }
        }
        if ( p.thunk && p.owned )
            usermode->free( p.thunk );
    }
    g_hook = {};
}

auto install_locked( ) -> bool
{
    if ( g_hook.installed )
        return true;
    if ( !usermode->can_rpm( ) )
        return false;

    const auto now = std::chrono::steady_clock::now( );
    if ( g_last_fail.time_since_epoch( ).count( )
        && now - g_last_fail < std::chrono::milliseconds( 1500 ) )
        return false;

    const uintptr_t ws = g->engine.workspace.load( );
    const uintptr_t cd = ws ? usermode->read<uintptr_t>( ws + offsets::instance::class_descriptor ) : 0;
    resolve_raycast_image( cd ? cd : ws );
    if ( !g_img )
    {
        g_last_fail = now;
        return false;
    }

    if ( !g_hook.state )
        g_hook.state = find_rw_cave( ( sizeof( raycast_state_t ) + 0x1F ) & ~ ( size_t )0x1F );
    if ( !g_hook.state )
    {
        g_last_fail = now;
        g->logger.print( "[silent] no rw cave for raycast state — skip hook" );
        return false;
    }
    raycast_state_t empty{};
    if ( !usermode->write_buf( g_hook.state, &empty, sizeof( empty ) ) )
    {
        g_last_fail = now;
        g_hook.state = 0;
        return false;
    }

    const uintptr_t fd = walk_cd_for( cd, g_str_raycast, "Raycast" );
    hook_fd_slots( fd, "Raycast" );
    if ( g_str_findpart )
    {
        const uintptr_t fpd = walk_cd_for( cd, g_str_findpart, "FindPartOnRay" );
        hook_fd_slots( fpd, "FindPartOnRay" );
    }
    if ( g_str_blockcast )
        hook_fd_slots( walk_cd_for( cd, g_str_blockcast, "Blockcast" ), "Blockcast" );

    if ( !g_hook.npins )
    {
        const uintptr_t slot = find_raycast_slot( );
        uintptr_t fn = 0;
        if ( slot )
            fn = restore_fn( usermode->read<uintptr_t>( slot ) );
        if ( !fn )
        {
            g_last_fail = now;
            g->logger.print( "[silent] raycast no BoundFn img=%p intern=%p fd=%p cd=%p",
                ( void* )g_img, ( void* )g_str_raycast, ( void* )fd, ( void* )cd );
            log_cd_methods( cd );
            g_hook.state = 0;
            return false;
        }
        if ( !pin_slot_swap( slot, fn, "Raycast", thunk_rewrite ) )
        {
            g_last_fail = now;
            g_hook.state = 0;
            return false;
        }
    }

    g_hook.installed = true;
    g_hook.active = false;
    return true;
}

}

auto ensure( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( g_hook.installed )
        return true;
    return install_locked( );
}

auto unhook( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    uninstall_locked( );
}

auto aim( const math::vector3_t& world, const math::vector3_t& cam, bool wallbang ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !g_hook.installed || !g_hook.state || !world.is_valid( ) )
        return;

    math::vector3_t ori = cam.is_valid( ) ? cam : math::vector3_t{ world.x, world.y + 4.f, world.z };
    math::vector3_t d{ world.x - ori.x, world.y - ori.y, world.z - ori.z };
    const float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if ( len2 < 0.000001f )
        return;
    const float inv = 1.f / std::sqrt( len2 );
    math::vector3_t dir{ d.x * inv * 10000.f, d.y * inv * 10000.f, d.z * inv * 10000.f };
    if ( wallbang )
    {
        ori.x = world.x - d.x * inv * 1.15f;
        ori.y = world.y - d.y * inv * 1.15f;
        ori.z = world.z - d.z * inv * 1.15f;
        dir.x = d.x * inv * 2.30f;
        dir.y = d.y * inv * 2.30f;
        dir.z = d.z * inv * 2.30f;
    }

    const uint32_t flags = wallbang ? 1u : 0u;
    const float scale = 1.15f;
    const uint32_t one = 1;
    float pos[3] = { world.x, world.y, world.z };
    float o[3] = { ori.x, ori.y, ori.z };
    float v[3] = { dir.x, dir.y, dir.z };
    usermode->write_buf( g_hook.state + offsetof( raycast_state_t, reserved ), &flags, sizeof( flags ) );
    usermode->write_buf( g_hook.state + offsetof( raycast_state_t, target_x ), pos, sizeof( pos ) );
    usermode->write_buf( g_hook.state + offsetof( raycast_state_t, scale ), &scale, sizeof( scale ) );
    usermode->write_buf( g_hook.state + offsetof( raycast_state_t, ori_x ), o, sizeof( o ) );
    usermode->write_buf( g_hook.state + offsetof( raycast_state_t, dir_x ), v, sizeof( v ) );
    usermode->write_buf( g_hook.state + offsetof( raycast_state_t, active ), &one, sizeof( one ) );
    g_hook.active = true;
}

auto passthrough( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    clear_locked( );
}

auto armed( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    return g_hook.installed && g_hook.active;
}

auto hooked_count( ) -> int
{
    std::lock_guard<std::mutex> lock( g_mutex );
    return g_hook.installed ? g_hook.npins : 0;
}

auto hits( ) -> uint64_t
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !g_hook.state )
        return 0;
    return usermode->read<uint64_t>( g_hook.state + offsetof( raycast_state_t, calls ) );
}

auto log_debug( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !g_hook.state )
        return;

    raycast_state_t s{};
    usermode->read_buf( g_hook.state, &s, sizeof( s ) );

    int ps = -1;
    int st = -1;
    float py = 0.f;
    float vy = 0.f;
    const uintptr_t model = g->engine.client_model.load( );
    if ( model )
    {
        sdk::u_instance humi = sdk::u_instance( model ).find_first_child_of_class( "Humanoid" );
        if ( humi )
        {
            sdk::u_humanoid hum( humi.address( ) );
            ps = hum.platform_stand( ) ? 1 : 0;
            st = hum.state( );
            sdk::u_base_part root = hum.root_part( );
            if ( root )
            {
                py = root.position( ).y;
                vy = root.assembly_linear_velocity( ).y;
            }
        }
    }

    g->logger.print(
        "[silent] dbg calls=%llu rw=%llu skip=%llu act=%u last_o=(%.1f,%.1f,%.1f) last_d=(%.1f,%.1f,%.1f) cam=(%.1f,%.1f,%.1f) hrp_y=%.1f vel_y=%.1f ps=%d state=%d(%s) fly=%d",
        ( unsigned long long )s.calls,
        ( unsigned long long )s.rewrites,
        ( unsigned long long )s.skips,
        s.last_action,
        s.last_ox, s.last_oy, s.last_oz,
        s.last_dx, s.last_dy, s.last_dz,
        s.ori_x, s.ori_y, s.ori_z,
        py, vy, ps, st, sdk::u_humanoid::state_name( st ), ( int )config.player.fly );
}

} // namespace raycast_hook

namespace viewport_hook
{
namespace
{

constexpr uintptr_t off_active = 0x00;
constexpr uintptr_t off_hits = 0x04;
constexpr uintptr_t off_sx = 0x08;
constexpr uintptr_t off_sy = 0x0C;
constexpr uintptr_t off_origin = 0x10;
constexpr uintptr_t off_direction = 0x1C;
constexpr size_t data_bytes = 0x40;
constexpr size_t stub_bytes = 0x200;
constexpr size_t cave_need = data_bytes + stub_bytes;

std::mutex g_mutex;
bool g_installed = false;
bool g_armed = false;
uintptr_t g_slot = 0;
uintptr_t g_orig = 0;
uintptr_t g_stub = 0;
uintptr_t g_data = 0;

auto push_bytes( std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes ) -> void
{
    for ( auto b : bytes )
        code.push_back( b );
}

auto push64( std::vector<uint8_t>& code, uint64_t value ) -> void
{
    for ( int i = 0; i < 8; ++i )
        code.push_back( static_cast<uint8_t>( ( value >> ( i * 8 ) ) & 0xFF ) );
}

auto write_ptr_protected( uintptr_t addr, uintptr_t value ) -> bool
{
    ULONG old = 0;
    const bool changed = usermode->protect( addr, sizeof( value ), PAGE_EXECUTE_READWRITE, &old );
    const bool ok = usermode->write<uintptr_t>( addr, value );
    if ( changed && old )
        usermode->protect( addr, sizeof( value ), old, nullptr );
    return ok && usermode->read<uintptr_t>( addr ) == value;
}

auto mark_vp_cfg( uintptr_t t ) -> void
{
    auto proc = GetProcAddress( GetModuleHandleW( L"kernelbase.dll" ), "SetProcessValidCallTargets" );
    if ( !proc )
        proc = GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "SetProcessValidCallTargets" );
    if ( !proc )
        return;
    CFG_CALL_TARGET_INFO info{};
    info.Offset = t & 0xFFFull;
    info.Flags = CFG_CALL_TARGET_VALID;
    using fn_t = BOOL( WINAPI* )( HANDLE, PVOID, SIZE_T, ULONG, PCFG_CALL_TARGET_INFO );
    ( ( fn_t )proc )( usermode->handle( ), ( PVOID )( t & ~0xFFFull ), 0x1000, 1, &info );
}

auto vp_slot( uintptr_t fd ) -> uintptr_t
{
    if ( !fd || !usermode->is_valid( fd ) )
        return 0;
    static constexpr uintptr_t k_off[] = {
        offsets::function_descriptor::function, 0x78, 0x88, 0x70, 0x90,
    };
    for ( uintptr_t off : k_off )
    {
        const uintptr_t fn = usermode->read<uintptr_t>( fd + off );
        if ( looks_code( fn ) )
            return fd + off;
    }
    return 0;
}

auto find_vp_cave( size_t need ) -> uintptr_t
{
    static const wchar_t* k_pref[] = {
        L"winsta.dll", L"win32u.dll", L"dxgi.dll", L"uxtheme.dll", L"dwmapi.dll",
    };
    for ( size_t i = 0; i < sizeof( k_pref ) / sizeof( k_pref[0] ); ++i )
    {
        const uintptr_t mod = usermode->module( k_pref[i] );
        if ( !mod )
            continue;

        IMAGE_DOS_HEADER dos{};
        if ( !usermode->read_buf( mod, &dos, sizeof( dos ) ) || dos.e_magic != IMAGE_DOS_SIGNATURE )
            continue;
        IMAGE_NT_HEADERS64 nt{};
        const uintptr_t nt_addr = mod + ( uintptr_t )dos.e_lfanew;
        if ( !usermode->read_buf( nt_addr, &nt, sizeof( nt ) ) || nt.Signature != IMAGE_NT_SIGNATURE )
            continue;
        const uintptr_t section_base = nt_addr + offsetof( IMAGE_NT_HEADERS64, OptionalHeader )
            + nt.FileHeader.SizeOfOptionalHeader;

        for ( WORD s = 0; s < nt.FileHeader.NumberOfSections; ++s )
        {
            IMAGE_SECTION_HEADER section{};
            if ( !usermode->read_buf( section_base + ( uintptr_t )s * sizeof( section ), &section, sizeof( section ) ) )
                break;
            if ( ( section.Characteristics & IMAGE_SCN_MEM_EXECUTE ) == 0 )
                continue;
            const uintptr_t section_off = section.VirtualAddress;
            size_t section_size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
            if ( !section_off || section_size < need )
                continue;

            // stay past mouse.hit (winsta+0x1000) and raycast's first cave
            uintptr_t scan_off = section_off;
            const uintptr_t min_off = i == 0 ? 0x3000u : 0x2000u;
            if ( scan_off < min_off )
                scan_off = min_off;
            if ( scan_off >= section_off + section_size )
                continue;

            const uintptr_t scan_start = mod + scan_off;
            const size_t scan_size = ( size_t )( section_off + section_size - scan_off );
            if ( scan_size > 4 * 1024 * 1024 )
                continue;
            std::vector<uint8_t> buf( scan_size );
            if ( !usermode->read_buf( scan_start, buf.data( ), buf.size( ) ) )
                continue;

            size_t run_start = 0;
            size_t run_len = 0;
            for ( size_t j = 0; j < buf.size( ); ++j )
            {
                const uint8_t b = buf[j];
                if ( b != 0x00 && b != 0xCC && b != 0x90 )
                {
                    run_len = 0;
                    run_start = j + 1;
                    continue;
                }
                ++run_len;
                if ( run_len < need )
                    continue;
                const uintptr_t cand = scan_start + run_start;
                const uintptr_t aligned = ( cand + 0x0F ) & ~( uintptr_t )0x0F;
                const size_t loss = ( size_t )( aligned - cand );
                if ( run_len < need + loss )
                    continue;
                if ( g_stub && aligned == g_stub )
                    continue;
                return aligned;
            }
        }
    }
    return 0;
}

auto patch_rel8( std::vector<uint8_t>& code, size_t site, size_t dest ) -> void
{
    const int d = ( int )dest - ( int )( site + 2 );
    code[site + 1] = ( uint8_t )d;
}

auto build_stub( uintptr_t data, uintptr_t original ) -> std::vector<uint8_t>
{
    std::vector<uint8_t> code;
    code.reserve( stub_bytes );

    auto jcc8 = [ & ]( uint8_t op ) -> size_t
    {
        const size_t s = code.size( );
        push_bytes( code, { op, 0x00 } );
        return s;
    };

    // BoundFunc same as Raycast: rcx=this, rdx=args Variant, r8=result.
    // ViewportPointToRay first arg is Vector2 at +8. two-number form y at +0x28.
    push_bytes( code, { 0x49, 0xBB } );
    push64( code, data );
    push_bytes( code, { 0xF0, 0x41, 0xFF, 0x43, 0x04 } );
    push_bytes( code, { 0x41, 0x83, 0x3B, 0x00 } );
    const size_t j_on = jcc8( 0x75 );
    push_bytes( code, { 0x49, 0xBB } );
    push64( code, original );
    push_bytes( code, { 0x41, 0xFF, 0xE3 } );
    patch_rel8( code, j_on, code.size( ) );

    push_bytes( code, { 0x53 } );
    push_bytes( code, { 0x49, 0x8B, 0xDB } );
    push_bytes( code, { 0x48, 0x83, 0xEC, 0x30 } );
    push_bytes( code, { 0x48, 0x89, 0x4C, 0x24, 0x28 } );
    push_bytes( code, { 0x48, 0x89, 0x54, 0x24, 0x20 } );
    push_bytes( code, { 0x4C, 0x89, 0x44, 0x24, 0x18 } );

    push_bytes( code, { 0x48, 0x85, 0xD2 } );
    const size_t j_skip = jcc8( 0x74 );
    push_bytes( code, { 0x48, 0x81, 0xFA, 0x00, 0x00, 0x01, 0x00 } );
    const size_t j_sk2 = jcc8( 0x72 );
    push_bytes( code, { 0xF6, 0xC2, 0x07 } );
    const size_t j_sk3 = jcc8( 0x75 );

    push_bytes( code, { 0x48, 0x8B, 0x42, 0x08 } );
    push_bytes( code, { 0x48, 0x89, 0x44, 0x24, 0x10 } );
    push_bytes( code, { 0x48, 0x8B, 0x42, 0x28 } );
    push_bytes( code, { 0x48, 0x89, 0x44, 0x24, 0x08 } );
    push_bytes( code, { 0x8B, 0x43, 0x08 } );
    push_bytes( code, { 0x89, 0x42, 0x08 } );
    push_bytes( code, { 0x8B, 0x43, 0x0C } );
    push_bytes( code, { 0x89, 0x42, 0x0C } );
    push_bytes( code, { 0xF3, 0x0F, 0x10, 0x43, 0x0C } );
    push_bytes( code, { 0xF3, 0x0F, 0x5A, 0xC0 } );
    push_bytes( code, { 0xF2, 0x0F, 0x11, 0x42, 0x28 } );
    push_bytes( code, { 0xFF, 0x43, 0x38 } );

    const size_t call_at = code.size( );
    patch_rel8( code, j_skip, call_at );
    patch_rel8( code, j_sk2, call_at );
    patch_rel8( code, j_sk3, call_at );
    push_bytes( code, { 0x48, 0x8B, 0x4C, 0x24, 0x28 } );
    push_bytes( code, { 0x48, 0x8B, 0x54, 0x24, 0x20 } );
    push_bytes( code, { 0x4C, 0x8B, 0x44, 0x24, 0x18 } );
    push_bytes( code, { 0x48, 0xB8 } );
    push64( code, original );
    push_bytes( code, { 0xFF, 0xD0 } );

    push_bytes( code, { 0x48, 0x8B, 0x54, 0x24, 0x20 } );
    push_bytes( code, { 0x48, 0x85, 0xD2 } );
    const size_t j_nr = jcc8( 0x74 );
    push_bytes( code, { 0x48, 0x8B, 0x44, 0x24, 0x10 } );
    push_bytes( code, { 0x48, 0x89, 0x42, 0x08 } );
    push_bytes( code, { 0x48, 0x8B, 0x44, 0x24, 0x08 } );
    push_bytes( code, { 0x48, 0x89, 0x42, 0x28 } );
    patch_rel8( code, j_nr, code.size( ) );

    push_bytes( code, { 0x48, 0x83, 0xC4, 0x30 } );
    push_bytes( code, { 0x5B } );
    push_bytes( code, { 0xC3 } );
    return code;
}

auto camera_addr( ) -> uintptr_t
{
    uintptr_t cam = g->engine.camera.load( );
    if ( cam )
        return cam;
    const uintptr_t ws = g->engine.workspace.load( );
    if ( !ws )
        return 0;
    sdk::u_instance found = sdk::u_instance( ws ).find_first_child_of_class( "Camera" );
    return found ? found.address( ) : 0;
}

auto install_locked( ) -> bool
{
    if ( g_installed && g_slot && g_stub )
        return true;
    if ( !usermode->can_rpm( ) )
        return false;

    const uintptr_t cam = camera_addr( );
    if ( !cam )
        return false;

    const uintptr_t cd = usermode->read<uintptr_t>( cam + offsets::instance::class_descriptor );
    resolve_viewport_image( cd ? cd : cam );
    if ( !g_img )
        bind_viewport_image( usermode->base( ) );
    uintptr_t fd = walk_cd_for( cd, g_str_viewport, "ViewportPointToRay" );
    if ( fd && in_image( fd ) )
    {
        uintptr_t heap = 0;
        uintptr_t cds[24]{};
        int n = 0;
        collect_cds( cd, cds, n, 24 );
        for ( int i = 0; i < n && !heap; ++i )
        {
            for ( uintptr_t off = 0x20; off <= 0x1E0 && !heap; off += 8 )
            {
                visit_fd_list(
                    usermode->read<uintptr_t>( cds[i] + off ),
                    usermode->read<uint64_t>( cds[i] + off + 8 ),
                    [&]( uintptr_t cand )
                    {
                        if ( heap || !cand || in_image( cand ) )
                            return;
                        if ( name_is( cand, g_str_viewport, "ViewportPointToRay" ) && native_at( cand ) )
                            heap = cand;
                    } );
            }
        }
        if ( heap )
            fd = heap;
    }
    if ( !fd )
    {
        static bool once = false;
        if ( !once )
        {
            once = true;
            g->logger.print( "[silent] viewport BoundFunc miss cam=%p cd=%p intern=%p",
                ( void* )cam, ( void* )cd, ( void* )g_str_viewport );
        }
        return false;
    }

    const uintptr_t slot = vp_slot( fd );
    if ( !slot )
    {
        g->logger.print( "[silent] viewport slot miss fd=%p", ( void* )fd );
        return false;
    }

    uintptr_t orig = usermode->read<uintptr_t>( slot );
    if ( g_stub && orig == g_stub && g_orig )
        orig = g_orig;
    if ( !orig || !looks_code( orig ) )
        return false;
    if ( !in_image( orig ) )
    {
        g->logger.print( "[silent] viewport orig %p not in client image", ( void* )orig );
        return false;
    }

    const uintptr_t cave = find_vp_cave( cave_need );
    if ( !cave )
    {
        g->logger.print( "[silent] viewport cave miss" );
        return false;
    }

    const uintptr_t data = cave;
    const uintptr_t stub = cave + data_bytes;
    auto code = build_stub( data, orig );
    if ( code.size( ) > stub_bytes )
    {
        g->logger.print( "[silent] viewport thunk too big %zu > %zu", code.size( ), stub_bytes );
        return false;
    }

    std::vector<uint8_t> zero( cave_need, 0 );
    if ( !usermode->write_buf( cave, zero.data( ), cave_need ) )
        return false;
    if ( !usermode->write_buf( stub, code.data( ), code.size( ) ) )
        return false;
    mark_vp_cfg( stub );
    if ( !write_ptr_protected( slot, stub ) )
        return false;

    usermode->write<uintptr_t>( data + 0x28, g_img );
    usermode->write<uintptr_t>( data + 0x30, g_img_end );
    g_slot = slot;
    g_orig = orig;
    g_stub = stub;
    g_data = data;
    g_installed = true;
    g_armed = false;
    uint8_t pro[4]{};
    usermode->read_buf( orig, pro, sizeof( pro ) );
    g->logger.print( "[silent] viewport BoundFunc slot=%p orig=%p stub=%p heap=%d pro=%02X%02X%02X%02X",
        ( void* )slot, ( void* )orig, ( void* )stub, ( int )!in_image( slot ),
        pro[0], pro[1], pro[2], pro[3] );
    return true;
}

auto uninstall_locked( ) -> void
{
    if ( g_data )
    {
        uint32_t zero = 0;
        usermode->write_buf( g_data + off_active, &zero, sizeof( zero ) );
    }
    if ( g_slot && g_orig && usermode->read<uintptr_t>( g_slot ) == g_stub )
        write_ptr_protected( g_slot, g_orig );
    g_slot = 0;
    g_orig = 0;
    g_stub = 0;
    g_data = 0;
    g_installed = false;
    g_armed = false;
}

}

auto ensure( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( g_installed )
        return true;
    return install_locked( );
}

auto unhook( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    uninstall_locked( );
}

auto redirect( const math::vector3_t& origin, const math::vector3_t& direction, float screen_x, float screen_y ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !g_installed || !g_data )
        return;
    uint32_t one = 1;
    float o[3] = { origin.x, origin.y, origin.z };
    float d[3] = { direction.x, direction.y, direction.z };
    usermode->write_buf( g_data + off_sx, &screen_x, sizeof( screen_x ) );
    usermode->write_buf( g_data + off_sy, &screen_y, sizeof( screen_y ) );
    usermode->write_buf( g_data + off_origin, o, sizeof( o ) );
    usermode->write_buf( g_data + off_direction, d, sizeof( d ) );
    usermode->write_buf( g_data + off_active, &one, sizeof( one ) );
    g_armed = true;
}

auto passthrough( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    g_armed = false;
    if ( !g_data )
        return;
    uint32_t zero = 0;
    usermode->write_buf( g_data + off_active, &zero, sizeof( zero ) );
}

auto armed( ) -> bool
{
    std::lock_guard<std::mutex> lock( g_mutex );
    return g_armed;
}

auto hooked_count( ) -> int
{
    std::lock_guard<std::mutex> lock( g_mutex );
    return g_installed && g_slot ? 1 : 0;
}

auto hits( ) -> uint64_t
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !g_data )
        return 0;
    return usermode->read<uint32_t>( g_data + off_hits );
}

auto rewrites( ) -> uint64_t
{
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( !g_data )
        return 0;
    return usermode->read<uint32_t>( g_data + 0x38 );
}

}
