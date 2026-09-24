#include <include/global.cuh>

#include <src/cheat/features/hitsounds/hitsounds.cuh>
#include <src/cheat/features/hitsounds/hitsounds_data.hxx>

#include <mmsystem.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#pragma comment( lib, "winmm.lib" )

namespace hitsounds
{
namespace
{

std::mutex g_mutex;
std::vector<std::string> g_paths; // one temp .mp3 per sound, written once

auto ensure_temp( int index ) -> const char*
{
    if ( index < 0 || index >= hitsounds_data::k_count )
        return nullptr;

    if ( g_paths.empty( ) )
        g_paths.resize( static_cast<size_t>( hitsounds_data::k_count ) );

    auto& path = g_paths[static_cast<size_t>( index )];
    if ( !path.empty( ) )
        return path.c_str( );

    char dir[MAX_PATH]{};
    if ( !GetTempPathA( MAX_PATH, dir ) )
        return nullptr;

    char file[MAX_PATH]{};
    std::snprintf( file, sizeof( file ), "%sfeelings_hit_%d.mp3", dir, index );

    FILE* fp = nullptr;
    if ( fopen_s( &fp, file, "wb" ) != 0 || !fp )
        return nullptr;

    const auto& e = hitsounds_data::k_entries[index];
    const size_t wrote = fwrite( e.data, 1, e.size, fp );
    fclose( fp );
    if ( wrote != e.size )
    {
        DeleteFileA( file );
        return nullptr;
    }

    path = file;
    return path.c_str( );
}

auto mci( const char* cmd ) -> void
{
    mciSendStringA( cmd, nullptr, 0, nullptr );
}

} // namespace

auto count( ) -> int
{
    return hitsounds_data::k_count;
}

auto name( int index ) -> const char*
{
    if ( index < 0 || index >= hitsounds_data::k_count )
        return "";
    return hitsounds_data::k_entries[index].name;
}

auto play( int index ) -> void
{
    std::lock_guard<std::mutex> lock( g_mutex );
    const char* path = ensure_temp( index );
    if ( !path )
        return;

    mci( "close feelings_hit" );

    char open_cmd[MAX_PATH + 64]{};
    std::snprintf( open_cmd, sizeof( open_cmd ),
        "open \"%s\" type mpegvideo alias feelings_hit", path );
    mci( open_cmd );

    const int vol = static_cast<int>( std::clamp( config.settings.hitsound_volume, 0.f, 1.f ) * 1000.f );
    char vol_cmd[64]{};
    std::snprintf( vol_cmd, sizeof( vol_cmd ), "setaudio feelings_hit volume to %d", vol );
    mci( vol_cmd );

    mci( "play feelings_hit from 0" );
}

auto play_selected( ) -> void
{
    if ( !config.settings.hitsounds )
        return;
    play( config.settings.hitsound );
}

} // namespace hitsounds
