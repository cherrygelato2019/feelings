#include <include/global.cuh>
#include <src/cheat/config/config_io.hxx>

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment( lib, "shlwapi.lib" )

namespace config_io
{
namespace
{

constexpr char k_magic[4] = { 'F', 'L', 'C', 'F' };

struct header_t
{
    char magic[4];
    int version;
    uint32_t bytes;
};

std::string g_current = "default";
std::string g_status = "defaults";
config_t g_saved{};
bool g_have_saved = false;
auto g_last_check = std::chrono::steady_clock::time_point{};

auto exe_dir( ) -> std::string
{
    char path[MAX_PATH]{};
    if ( !GetModuleFileNameA( nullptr, path, MAX_PATH ) )
        return "configs";
    PathRemoveFileSpecA( path );
    std::string dir = path;
    dir += "\\configs";
    return dir;
}

auto ensure_dir( const std::string& dir ) -> bool
{
    if ( dir.empty( ) )
        return false;
    if ( PathFileExistsA( dir.c_str( ) ) )
        return true;
    return CreateDirectoryA( dir.c_str( ), nullptr ) != 0 || GetLastError( ) == ERROR_ALREADY_EXISTS;
}

auto sanitize( const char* name ) -> std::string
{
    std::string out;
    if ( !name )
        return "default";
    for ( const char* p = name; *p; ++p )
    {
        const char c = *p;
        if ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
            || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == ' ' )
            out.push_back( c );
    }
    while ( !out.empty( ) && out.front( ) == ' ' )
        out.erase( out.begin( ) );
    while ( !out.empty( ) && out.back( ) == ' ' )
        out.pop_back( );
    if ( out.empty( ) )
        out = "default";
    if ( out.size( ) > 48 )
        out.resize( 48 );
    return out;
}

auto slot_path( const std::string& name ) -> std::string
{
    return exe_dir( ) + "\\" + name + ".flcfg";
}

auto last_path( ) -> std::string
{
    return exe_dir( ) + "\\last.txt";
}

auto write_last( const std::string& name ) -> void
{
    if ( !ensure_dir( exe_dir( ) ) )
        return;
    FILE* f = nullptr;
    if ( fopen_s( &f, last_path( ).c_str( ), "wb" ) != 0 || !f )
        return;
    fwrite( name.data( ), 1, name.size( ), f );
    fclose( f );
}

auto clamp_cfg( config_t& c ) -> void
{
    if ( c.silentaim.method < 0 || c.silentaim.method > 2 )
        c.silentaim.method = 0;
    if ( c.aimbot.method < 0 )
        c.aimbot.method = 0;
    if ( c.esp.chams_kind < 0 || c.esp.chams_kind > 3 )
        c.esp.chams_kind = 0;
    if ( c.esp.chams_style < 0 || c.esp.chams_style > 3 )
        c.esp.chams_style = 0;
    if ( c.theme.ui_scale < 0.5f || c.theme.ui_scale > 2.f )
        c.theme.ui_scale = 1.f;
    if ( c.theme.opacity < 0.2f || c.theme.opacity > 1.f )
        c.theme.opacity = 0.80f;
    if ( c.settings.fps_cap < 30 )
        c.settings.fps_cap = 30;
    if ( c.settings.fps_cap > 360 )
        c.settings.fps_cap = 360;
    if ( c.player.antiaim_pitch < 0 || c.player.antiaim_pitch > 3 )
        c.player.antiaim_pitch = 1;
    if ( c.player.antiaim_yaw < 0 || c.player.antiaim_yaw > 5 )
        c.player.antiaim_yaw = 1;
    if ( c.player.desync_method < 0 || c.player.desync_method > 2 )
        c.player.desync_method = 0;
    if ( c.player.fly_method < 0 || c.player.fly_method > 7 )
        c.player.fly_method = 1;
    if ( c.player.antiaim_spin_speed < 0.5f || c.player.antiaim_spin_speed > 80.f )
        c.player.antiaim_spin_speed = 12.f;
    if ( c.player.fake_lag_hold_ms < 20.f )
        c.player.fake_lag_hold_ms = 20.f;
    if ( c.player.fake_lag_hold_ms > 800.f )
        c.player.fake_lag_hold_ms = 800.f;
}

auto same_cfg( const config_t& a, const config_t& b ) -> bool
{
    config_t x = a;
    config_t y = b;
    x.menu_open = false;
    y.menu_open = false;
    return std::memcmp( &x, &y, sizeof( config_t ) ) == 0;
}

auto remember( const config_t& snap ) -> void
{
    g_saved = snap;
    g_saved.menu_open = false;
    g_have_saved = true;
}

} // namespace

auto dir( ) -> std::string
{
    return exe_dir( );
}

auto current_name( ) -> std::string
{
    return g_current;
}

auto last_name( ) -> std::string
{
    FILE* f = nullptr;
    if ( fopen_s( &f, last_path( ).c_str( ), "rb" ) != 0 || !f )
        return {};
    char buf[128]{};
    const size_t n = fread( buf, 1, sizeof( buf ) - 1, f );
    fclose( f );
    return sanitize( std::string( buf, n ).c_str( ) );
}

auto last_status( ) -> std::string
{
    return g_status;
}

auto list( ) -> std::vector<std::string>
{
    std::vector<std::string> out;
    const std::string folder = exe_dir( );
    const std::string glob = folder + "\\*.flcfg";
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA( glob.c_str( ), &fd );
    if ( h == INVALID_HANDLE_VALUE )
        return out;
    do
    {
        if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
            continue;
        std::string n = fd.cFileName;
        if ( n.size( ) > 6 && n.substr( n.size( ) - 6 ) == ".flcfg" )
            n.resize( n.size( ) - 6 );
        if ( !n.empty( ) )
            out.push_back( n );
    } while ( FindNextFileA( h, &fd ) );
    FindClose( h );
    std::sort( out.begin( ), out.end( ) );
    return out;
}

auto save( const char* name ) -> bool
{
    const std::string slot = sanitize( name );
    const std::string folder = exe_dir( );
    if ( !ensure_dir( folder ) )
    {
        g_status = "save failed (folder)";
        return false;
    }

    config_t snap = config;
    snap.menu_open = false;
    clamp_cfg( snap );

    header_t hdr{};
    std::memcpy( hdr.magic, k_magic, 4 );
    hdr.version = k_version;
    hdr.bytes = ( uint32_t )sizeof( config_t );

    FILE* f = nullptr;
    if ( fopen_s( &f, slot_path( slot ).c_str( ), "wb" ) != 0 || !f )
    {
        g_status = "save failed";
        return false;
    }
    const bool ok = fwrite( &hdr, sizeof( hdr ), 1, f ) == 1
        && fwrite( &snap, sizeof( snap ), 1, f ) == 1;
    fclose( f );
    if ( !ok )
    {
        g_status = "save failed";
        return false;
    }

    g_current = slot;
    write_last( slot );
    remember( snap );
    g_status = "saved  " + slot;
    return true;
}

auto load( const char* name ) -> bool
{
    const std::string slot = sanitize( name );
    FILE* f = nullptr;
    if ( fopen_s( &f, slot_path( slot ).c_str( ), "rb" ) != 0 || !f )
    {
        g_status = "load failed";
        return false;
    }

    header_t hdr{};
    if ( fread( &hdr, sizeof( hdr ), 1, f ) != 1
        || std::memcmp( hdr.magic, k_magic, 4 ) != 0
        || hdr.version < 1
        || hdr.version > k_version
        || hdr.bytes < 64
        || hdr.bytes > 1u << 20 )
    {
        fclose( f );
        g_status = "bad config file";
        return false;
    }

    std::vector<uint8_t> blob( hdr.bytes );
    const bool ok = fread( blob.data( ), 1, hdr.bytes, f ) == hdr.bytes;
    fclose( f );
    if ( !ok )
    {
        g_status = "load failed";
        return false;
    }

    config_t snap{};
    const size_t n = ( std::min )( ( size_t )hdr.bytes, sizeof( config_t ) );
    std::memcpy( &snap, blob.data( ), n );
    snap.menu_open = config.menu_open;
    clamp_cfg( snap );

    config = snap;
    g_current = slot;
    write_last( slot );
    remember( snap );
    g_status = "loaded  " + slot;
    return true;
}

auto remove( const char* name ) -> bool
{
    const std::string slot = sanitize( name );
    if ( !DeleteFileA( slot_path( slot ).c_str( ) ) )
    {
        g_status = "delete failed";
        return false;
    }
    if ( g_current == slot )
        g_status = "deleted  " + slot;
    else
        g_status = "deleted  " + slot;
    return true;
}

auto copy( const char* from, const char* to ) -> bool
{
    const std::string src = sanitize( from );
    const std::string dst = sanitize( to );
    if ( src == dst )
        return save( dst.c_str( ) );
    if ( !ensure_dir( exe_dir( ) ) )
        return false;
    if ( !CopyFileA( slot_path( src ).c_str( ), slot_path( dst ).c_str( ), FALSE ) )
    {
        if ( !load( src.c_str( ) ) )
            return false;
        return save( dst.c_str( ) );
    }
    g_current = dst;
    write_last( dst );
    g_status = "copied  " + src + " -> " + dst;
    return true;
}

auto reset( ) -> void
{
    const bool open = config.menu_open;
    const int key = config.menu_key;
    config = config_t{};
    config.menu_open = open;
    config.menu_key = key;
    remember( config );
    g_status = "reset to defaults";
}

auto load_last( ) -> bool
{
    const std::string name = last_name( );
    if ( !name.empty( ) && load( name.c_str( ) ) )
        return true;
    if ( load( "default" ) )
        return true;
    remember( config );
    g_current = "default";
    g_status = "defaults";
    return false;
}

auto tick( ) -> void
{
    if ( !config.autosave )
        return;

    const auto now = std::chrono::steady_clock::now( );
    if ( g_last_check.time_since_epoch( ).count( ) != 0
        && now - g_last_check < std::chrono::seconds( 4 ) )
        return;
    g_last_check = now;

    if ( !g_have_saved )
    {
        remember( config );
        return;
    }

    if ( same_cfg( config, g_saved ) )
        return;

    save( g_current.c_str( ) );
}

} // namespace config_io
