#include <include/global.cuh>

#include <src/cheat/features/explorer/decompiler/decompiler.cuh>
#include <src/cheat/features/explorer/bytecode/bytecode.hpp>
#include <src/cheat/features/explorer/bytecode/luau_disasm.hpp>

#include <windows.h>
#include <winhttp.h>

#include <cstring>
#include <atomic>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

#pragma comment( lib, "winhttp.lib" )

namespace script_decompiler
{

std::atomic<bool> g_busy{ false };

namespace
{

// Ported from decompiler-master endpoints
constexpr const char* k_lunaux = "https://lunaux-decompiler.dev/api/decompile";
constexpr const char* k_konstant = "https://api.plusgiant5.com/konstant/decompile";
constexpr const char* k_bytefall = "https://decompiler.bytefall.dev/decompile";

constexpr uintptr_t k_min_ptr = 0x10000ull;
constexpr uintptr_t k_max_ptr = 0x00007FF000000000ull;
constexpr size_t k_min_blob = 8;
constexpr size_t k_max_blob = 64ull * 1024 * 1024;

std::mutex g_mu;
std::vector<result_t> g_decompiled;
std::vector<result_t> g_disassembled;

auto ptr_ok( uintptr_t p ) -> bool
{
    return p > k_min_ptr && p < k_max_ptr;
}

// Bulk RPM first; word fallback if the driver flakes on large blobs (master pattern).
// Do NOT gate on can_rpm — word path still works when bulk RPM flakes.
auto read_exact( uintptr_t address, size_t size ) -> std::vector<char>
{
    if ( !ptr_ok( address ) || size < k_min_blob || size > k_max_blob )
        return {};

    std::vector<char> buf( size );
    if ( usermode->can_rpm( ) && usermode->read_buf( address, buf.data( ), size ) )
        return buf;

    std::vector<uint64_t> words( ( size + 7 ) / 8 );
    for ( size_t i = 0; i < words.size( ); ++i )
        words[i] = usermode->read<uint64_t>( address + i * 8ull );
    std::memcpy( buf.data( ), words.data( ), size );
    return buf;
}

auto decode_blob( const std::vector<char>& bytes ) -> std::optional<std::string>
{
    if ( bytes.size( ) < k_min_blob )
        return std::nullopt;

    const std::string out = Bytecode::decompress( std::string( bytes.data( ), bytes.size( ) ) );
    if ( !out.empty( ) )
        return out;

    // Raw RSB1 + zstd (already unxored)
    if ( bytes[0] == 'R' && bytes[1] == 'S' && bytes[2] == 'B' && bytes[3] == '1' )
    {
        const uint32_t size = ( uint32_t )( uint8_t )bytes[4]
            | ( ( uint32_t )( uint8_t )bytes[5] << 8 )
            | ( ( uint32_t )( uint8_t )bytes[6] << 16 )
            | ( ( uint32_t )( uint8_t )bytes[7] << 24 );
        if ( size >= k_min_blob && size < ( 1u << 27 ) && bytes.size( ) > k_min_blob )
        {
            std::string data( size, '\0' );
            const size_t written = ZSTD_decompress( data.data( ), size, bytes.data( ) + 8, bytes.size( ) - 8 );
            if ( !ZSTD_isError( written ) && written == size )
                return data;
        }
    }
    return std::nullopt;
}

auto bytecode_offset_for( script_type type ) -> uintptr_t
{
    switch ( type )
    {
    case module_script: return offsets::module_script::bytecode;
    case script: return offsets::script::bytecode;
    case local_script:
    default: return offsets::local_script::bytecode;
    }
}

auto decompress_script( uintptr_t script, script_type type ) -> std::optional<std::string>
{
    if ( !ptr_ok( script ) || !usermode->is_valid( script ) )
        return std::nullopt;

    const uintptr_t container = usermode->read<uintptr_t>( script + bytecode_offset_for( type ) );
    if ( !ptr_ok( container ) )
    {
        g->logger.print( "[decompiler] bad bytecode container 0x%llx", ( unsigned long long )container );
        return std::nullopt;
    }

    std::string found;

    auto try_blob = [&]( uintptr_t pointer, size_t size )
    {
        if ( !found.empty( ) || !ptr_ok( pointer ) || size < k_min_blob || size > k_max_blob )
            return;
        const auto decoded = decode_blob( read_exact( pointer, size ) );
        if ( decoded )
        {
            g->logger.print( "[decompiler] bytecode %zu bytes @ 0x%llx",
                decoded->size( ), ( unsigned long long )pointer );
            found = *decoded;
        }
    };

    // MASTER try_pair: truncate 64-bit size to 32-bit FIRST, then clamp.
    // (Checking range before trunc kills valid blobs when high dword is junk.)
    auto try_pair = [&]( uintptr_t address )
    {
        if ( !found.empty( ) || !ptr_ok( address ) )
            return;
        const uintptr_t pointer = usermode->read<uintptr_t>( address );
        if ( !ptr_ok( pointer ) )
            return;

        const uint64_t raw_size = usermode->read<uint64_t>( address + 0x10 );
        const size_t size = raw_size > 0xFFFFFFFFull
            ? ( size_t )( uint32_t )raw_size
            : ( size_t )raw_size;
        if ( size < k_min_blob || size > k_max_blob )
            return;
        try_blob( pointer, size );
    };

    // Absolute layouts from container: size @ +0x20 (pair) and legacy +0x28
    auto try_feelings_layout = [&]( )
    {
        if ( !found.empty( ) )
            return;
        const uintptr_t pointer = usermode->read<uintptr_t>( container + offsets::bytecode::pointer );
        for ( uintptr_t size_off : { offsets::bytecode::size, offsets::bytecode::size_alt } )
        {
            if ( !found.empty( ) )
                return;
            const uint64_t raw_size = usermode->read<uint64_t>( container + size_off );
            const size_t size = raw_size > 0xFFFFFFFFull
                ? ( size_t )( uint32_t )raw_size
                : ( size_t )raw_size;
            try_blob( pointer, size );
        }
    };

    auto scan_pointer = [&]( uintptr_t value )
    {
        if ( !found.empty( ) || !ptr_ok( value ) )
            return;
        // Master order: +0x18 then +0x10
        try_pair( value + 0x18 );
        try_pair( value + 0x10 );
    };

    // Master path first: pair at container+0x10 => size lives at container+0x20
    try_pair( container + offsets::bytecode::pointer );
    try_pair( container );
    try_feelings_layout( );

    // Master window scan — do NOT abort if the window read flakes; keep scanning.
    constexpr size_t k_words = 0x400 / 8;
    const auto window_data = read_exact( container, k_words * 8 );
    if ( window_data.empty( ) )
    {
        g->logger.print( "[decompiler] empty container window 0x%llx (continuing)", ( unsigned long long )container );
    }
    else
    {
        const auto* words = reinterpret_cast<const uint64_t*>( window_data.data( ) );
        const size_t n = window_data.size( ) / 8;
        for ( size_t i = 0; i < n && found.empty( ); ++i )
        {
            if ( ptr_ok( words[i] ) )
                try_pair( container + i * 8 );
        }
        for ( size_t i = 0; i < n && found.empty( ); ++i )
        {
            if ( ptr_ok( words[i] ) )
                scan_pointer( words[i] );
        }
    }

    // Sweep script object for all types — bytecode wrappers drift across versions.
    for ( uintptr_t off = 0x100; off < 0x280 && found.empty( ); off += 8 )
    {
        const uintptr_t pointer = usermode->read<uintptr_t>( script + off );
        if ( !ptr_ok( pointer ) )
            continue;
        try_pair( script + off );
        scan_pointer( pointer );
    }

    if ( found.empty( ) )
    {
        const uintptr_t p10 = usermode->read<uintptr_t>( container + 0x10 );
        const uint64_t s20 = usermode->read<uint64_t>( container + 0x20 );
        const uint64_t s28 = usermode->read<uint64_t>( container + 0x28 );
        g->logger.print(
            "[decompiler] no bytecode for 0x%llx container=0x%llx ptr10=0x%llx sz20=%llu sz28=%llu",
            ( unsigned long long )script,
            ( unsigned long long )container,
            ( unsigned long long )p10,
            ( unsigned long long )s20,
            ( unsigned long long )s28 );
        return std::nullopt;
    }
    return found;
}

auto to_base64( const void* data, size_t size ) -> std::string
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = static_cast<const unsigned char*>( data );
    std::string out;
    out.reserve( ( ( size + 2 ) / 3 ) * 4 );
    size_t i = 0;
    while ( i + 2 < size )
    {
        const unsigned n = ( bytes[i] << 16 ) | ( bytes[i + 1] << 8 ) | bytes[i + 2];
        out.push_back( table[( n >> 18 ) & 63] );
        out.push_back( table[( n >> 12 ) & 63] );
        out.push_back( table[( n >> 6 ) & 63] );
        out.push_back( table[n & 63] );
        i += 3;
    }
    if ( i < size )
    {
        unsigned n = bytes[i] << 16;
        out.push_back( table[( n >> 18 ) & 63] );
        if ( i + 1 < size )
        {
            n |= bytes[i + 1] << 8;
            out.push_back( table[( n >> 12 ) & 63] );
            out.push_back( table[( n >> 6 ) & 63] );
            out.push_back( '=' );
        }
        else
        {
            out.push_back( table[( n >> 12 ) & 63] );
            out.push_back( '=' );
            out.push_back( '=' );
        }
    }
    return out;
}

auto strip_lines( const std::string& text, int count ) -> std::string
{
    std::istringstream stream( text );
    std::string line;
    std::string output;
    int skipped = 0;
    while ( std::getline( stream, line ) )
    {
        if ( skipped < count )
        {
            ++skipped;
            continue;
        }
        output += line;
        output.push_back( '\n' );
    }
    return output;
}

class winhttp_handle
{
public:
    explicit winhttp_handle( HINTERNET v = nullptr ) : value( v ) {}
    ~winhttp_handle( ) { reset( ); }
    winhttp_handle( winhttp_handle&& o ) noexcept : value( o.value ) { o.value = nullptr; }
    winhttp_handle& operator=( winhttp_handle&& o ) noexcept
    {
        if ( this != &o ) { reset( ); value = o.value; o.value = nullptr; }
        return *this;
    }
    winhttp_handle( const winhttp_handle& ) = delete;
    winhttp_handle& operator=( const winhttp_handle& ) = delete;
    HINTERNET get( ) const { return value; }
    explicit operator bool( ) const { return value != nullptr; }
private:
    void reset( )
    {
        if ( value ) { WinHttpCloseHandle( value ); value = nullptr; }
    }
    HINTERNET value = nullptr;
};

auto post( const char* endpoint, const void* data, size_t size, const wchar_t* content_type ) -> std::string
{
    URL_COMPONENTS url{};
    url.dwStructSize = sizeof( url );
    wchar_t host[256]{};
    wchar_t path[1024]{};
    wchar_t extra[1024]{};
    url.lpszHostName = host;
    url.dwHostNameLength = _countof( host );
    url.lpszUrlPath = path;
    url.dwUrlPathLength = _countof( path );
    url.lpszExtraInfo = extra;
    url.dwExtraInfoLength = _countof( extra );

    const std::wstring wide( endpoint, endpoint + std::strlen( endpoint ) );
    if ( !WinHttpCrackUrl( wide.c_str( ), 0, 0, &url ) )
    {
        g->logger.print( "[decompiler] WinHttpCrackUrl err=%lu", GetLastError( ) );
        return {};
    }

    winhttp_handle session( WinHttpOpen( L"feelings-decompiler/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
    if ( !session )
    {
        g->logger.print( "[decompiler] WinHttpOpen err=%lu", GetLastError( ) );
        return {};
    }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
        | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
        | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption( session.get( ), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof( protocols ) );
    WinHttpSetTimeouts( session.get( ), 60000, 60000, 60000, 60000 );

    const std::wstring host_name( url.lpszHostName, url.dwHostNameLength );
    winhttp_handle connection( WinHttpConnect( session.get( ), host_name.c_str( ), url.nPort, 0 ) );
    if ( !connection )
    {
        g->logger.print( "[decompiler] WinHttpConnect err=%lu", GetLastError( ) );
        return {};
    }

    std::wstring url_path( url.lpszUrlPath, url.dwUrlPathLength );
    if ( url.dwExtraInfoLength > 0 )
        url_path.append( url.lpszExtraInfo, url.dwExtraInfoLength );

    const DWORD flags = url.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    winhttp_handle request( WinHttpOpenRequest( connection.get( ), L"POST", url_path.c_str( ),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags ) );
    if ( !request )
    {
        g->logger.print( "[decompiler] WinHttpOpenRequest err=%lu", GetLastError( ) );
        return {};
    }

    if ( !WinHttpSendRequest( request.get( ), content_type, ( DWORD )-1,
        const_cast<LPVOID>( data ), ( DWORD )size, ( DWORD )size, 0 ) )
    {
        g->logger.print( "[decompiler] WinHttpSendRequest err=%lu", GetLastError( ) );
        return {};
    }
    if ( !WinHttpReceiveResponse( request.get( ), nullptr ) )
    {
        g->logger.print( "[decompiler] WinHttpReceiveResponse err=%lu", GetLastError( ) );
        return {};
    }

    DWORD status = 0;
    DWORD status_bytes = sizeof( status );
    WinHttpQueryHeaders( request.get( ), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_bytes, WINHTTP_NO_HEADER_INDEX );

    std::string body;
    DWORD available = 0;
    do
    {
        if ( !WinHttpQueryDataAvailable( request.get( ), &available ) || available == 0 )
            break;
        std::string chunk( available, '\0' );
        DWORD read = 0;
        if ( !WinHttpReadData( request.get( ), chunk.data( ), available, &read ) )
            break;
        body.append( chunk.data( ), read );
    } while ( available > 0 );

    if ( status != 0 && status != 200 )
    {
        g->logger.print( "[decompiler] http %lu", status );
        return {};
    }
    return body;
}

auto fetch_source( const std::string& bytecode, backend be ) -> std::string
{
    const char* endpoint = be == backend::konstant ? k_konstant : k_lunaux;
    const std::string encoded = to_base64( bytecode.data( ), bytecode.size( ) );
    std::string source;

    if ( be == backend::konstant )
    {
        source = post( endpoint, bytecode.data( ), bytecode.size( ), L"Content-Type: text/plain\r\n" );
        if ( !source.empty( ) )
            source = strip_lines( source, 6 );
    }
    else
    {
        const std::string body = "{\"bytecode\":\"" + encoded + "\"}";
        source = post( endpoint, body.data( ), body.size( ), L"Content-Type: application/json\r\n" );
    }

    if ( source.empty( ) || source.find_first_not_of( " \t\r\n" ) == std::string::npos
        || source[source.find_first_not_of( " \t\r\n" )] == '{' )
    {
        g->logger.print( "[decompiler] fallback ByteFall" );
        const std::string body = "{\"script\":\"" + encoded + "\"}";
        source = post( k_bytefall, body.data( ), body.size( ), L"Content-Type: application/json\r\n" );
        if ( !source.empty( ) )
            source = strip_lines( source, 2 );
    }
    return source;
}

auto push_result( std::vector<result_t>& list, result_t entry ) -> void
{
    std::lock_guard<std::mutex> lock( g_mu );
    list.push_back( std::move( entry ) );
}

} // namespace

auto is_script_class( const std::string& class_name ) -> bool
{
    return class_name == "LocalScript" || class_name == "ModuleScript" || class_name == "Script";
}

auto type_from_class( const std::string& class_name ) -> script_type
{
    if ( class_name == "ModuleScript" )
        return module_script;
    if ( class_name == "Script" )
        return script;
    return local_script;
}

auto results( ) -> std::vector<result_t>&
{
    return g_decompiled;
}

auto disasm_results( ) -> std::vector<result_t>&
{
    return g_disassembled;
}

auto clear_results( ) -> void
{
    std::lock_guard<std::mutex> lock( g_mu );
    g_decompiled.clear( );
    g_disassembled.clear( );
}

auto decompile( uintptr_t script, script_type type, backend be ) -> bool
{
    g_busy.store( true, std::memory_order_release );
    struct busy_guard { ~busy_guard( ) { g_busy.store( false, std::memory_order_release ); } } guard;

    const auto bytecode = decompress_script( script, type );
    if ( !bytecode )
    {
        g->logger.print( "[decompiler] decompile aborted — no bytecode" );
        return false;
    }

    g->logger.print( "[decompiler] got %zu bytecode bytes, hitting api...", bytecode->size( ) );
    std::string source = fetch_source( *bytecode, be );

    // Public APIs flake / 503 a lot — never leave Him empty if we already have bytecode.
    if ( source.empty( ) || source.find_first_not_of( " \t\r\n" ) == std::string::npos
        || source[source.find_first_not_of( " \t\r\n" )] == '{' )
    {
        g->logger.print( "[decompiler] api dead — falling back to local disasm" );
        std::string listing = luau_deasm::disassemble( *bytecode );
        if ( listing.empty( ) )
            listing = "-- decompile api offline\n-- bytecode extracted (" + std::to_string( bytecode->size( ) )
                + " bytes) but local disasm also failed\n";
        source = "-- [feelings] remote decompiler offline — local disassembly\n" + listing;
    }

    if ( source.back( ) != '\n' )
        source.push_back( '\n' );

    result_t entry;
    entry.title = "Script_" + std::to_string( script );
    entry.code = std::move( source );
    entry.script = script;
    entry.disasm = false;
    push_result( g_decompiled, std::move( entry ) );
    return true;
}

auto disassemble( uintptr_t script, script_type type ) -> bool
{
    g_busy.store( true, std::memory_order_release );
    struct busy_guard { ~busy_guard( ) { g_busy.store( false, std::memory_order_release ); } } guard;

    const auto bytecode = decompress_script( script, type );
    if ( !bytecode )
        return false;

    std::string listing = luau_deasm::disassemble( *bytecode );
    if ( listing.empty( ) )
        return false;

    const bool failed = listing.rfind( "!! disassemble failed", 0 ) == 0;
    result_t entry;
    entry.title = failed
        ? ( "DisasmFail_" + std::to_string( script ) )
        : ( "Disasm_" + std::to_string( script ) );
    entry.code = std::move( listing );
    entry.script = script;
    entry.disasm = true;
    push_result( g_disassembled, std::move( entry ) );
    return !failed;
}

}
