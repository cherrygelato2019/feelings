#include <include/global.cuh>
#include <src/cheat/features/esp/helpers.hxx>
#include <src/cheat/features/music/feelings_fm.cuh>
#include <src/cheat/overlay/fonts/fonts.cuh>

#include <addons/imgui_addons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <d3d11.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment( lib, "winhttp.lib" )
#pragma comment( lib, "windowsapp.lib" )
#pragma comment( lib, "windowscodecs.lib" )

namespace feelings_fm
{
namespace
{

struct lyric_line_t
{
    float t = 0.f;
    std::string text;
};

struct track_t
{
    std::string title;
    std::string artist;
    std::string album;
    std::string raw_title;
};

struct playlist_entry_t
{
    track_t track{};
    std::chrono::steady_clock::time_point played_at{};
};

struct lyrics_t
{
    std::string key;
    std::vector<lyric_line_t> lines;
    std::string plain;
    bool ready = false;
    bool failed = false;
    bool soft_fail = false;
};

std::mutex g_mtx;
track_t g_track{};
lyrics_t g_lyrics{};
std::vector<playlist_entry_t> g_playlist{};
constexpr size_t k_playlist_max = 32;
std::atomic<bool> g_fetching{ false };
float g_playback_pos = 0.f;
float g_playback_duration = 0.f;
std::chrono::steady_clock::time_point g_playback_stamp = std::chrono::steady_clock::now( );
bool g_playback_playing = false;
bool g_have_playback_clock = false;
float g_last_smtc_raw = -1.f;
std::string g_status = "idle";
bool g_winrt_ready = false;

std::vector<uint8_t> g_cover_bytes;
std::string g_cover_key;
std::atomic<bool> g_cover_ready{ false };
std::atomic<bool> g_cover_fetching{ false };
ID3D11ShaderResourceView* g_cover_srv = nullptr;
std::string g_cover_srv_key;
int g_cover_w = 0;
int g_cover_h = 0;

auto fm_log( const char* fmt, ... ) -> void
{
    char buf[512]{};
    va_list ap;
    va_start( ap, fmt );
    vsnprintf_s( buf, sizeof( buf ), _TRUNCATE, fmt, ap );
    va_end( ap );
    std::printf( "%s\n", buf );
    if ( g )
        g->logger.print( "%s", buf );
}

auto set_status( const char* s ) -> void
{
    std::lock_guard<std::mutex> lk( g_mtx );
    g_status = s ? s : "";
}

auto trim( std::string s ) -> std::string
{
    while ( !s.empty( ) && ( unsigned char )s.front( ) <= ' ' )
        s.erase( s.begin( ) );
    while ( !s.empty( ) && ( unsigned char )s.back( ) <= ' ' )
        s.pop_back( );
    return s;
}

auto to_lower( std::string s ) -> std::string
{
    for ( char& c : s )
        c = ( char )std::tolower( ( unsigned char )c );
    return s;
}

auto strip_noise( std::string s ) -> std::string
{
    
    for ( ;; )
    {
        const size_t a = s.find( " (" );
        const size_t b = s.find( " [" );
        size_t p = std::string::npos;
        char close = 0;
        if ( a != std::string::npos && ( b == std::string::npos || a < b ) )
        {
            p = a;
            close = ')';
        }
        else if ( b != std::string::npos )
        {
            p = b;
            close = ']';
        }
        if ( p == std::string::npos )
            break;
        const size_t e = s.find( close, p );
        if ( e == std::string::npos )
            break;
        const std::string inside = to_lower( s.substr( p, e - p + 1 ) );
        if ( inside.find( "feat" ) != std::string::npos
            || inside.find( "remaster" ) != std::string::npos
            || inside.find( "remix" ) != std::string::npos
            || inside.find( "bonus" ) != std::string::npos
            || inside.find( "live" ) != std::string::npos
            || inside.find( "edit" ) != std::string::npos
            || inside.find( "version" ) != std::string::npos
            || inside.find( "explicit" ) != std::string::npos )
            s.erase( p, e - p + 1 );
        else
            break;
    }
    return trim( s );
}

auto wide_to_utf8( const std::wstring& w ) -> std::string
{
    if ( w.empty( ) )
        return {};
    const int n = WideCharToMultiByte( CP_UTF8, 0, w.c_str( ), ( int )w.size( ), nullptr, 0, nullptr, nullptr );
    if ( n <= 0 )
        return {};
    std::string out( ( size_t )n, '\0' );
    WideCharToMultiByte( CP_UTF8, 0, w.c_str( ), ( int )w.size( ), out.data( ), n, nullptr, nullptr );
    return out;
}

auto url_encode( const std::string& s ) -> std::string
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve( s.size( ) * 3 );
    for ( unsigned char c : s )
    {
        if ( std::isalnum( c ) || c == '-' || c == '_' || c == '.' || c == '~' )
            out.push_back( ( char )c );
        else if ( c == ' ' )
            out.push_back( '+' );
        else
        {
            out.push_back( '%' );
            out.push_back( hex[c >> 4] );
            out.push_back( hex[c & 15] );
        }
    }
    return out;
}

auto json_string_field( const std::string& json, const char* key ) -> std::string
{
    const std::string needle = std::string( "\"" ) + key + "\":";
    size_t p = json.find( needle );
    if ( p == std::string::npos )
        return {};
    p += needle.size( );
    while ( p < json.size( ) && ( json[p] == ' ' || json[p] == '\t' ) )
        ++p;
    if ( p >= json.size( ) )
        return {};
    if ( json[p] == 'n' ) 
        return {};
    if ( json[p] != '"' )
        return {};
    ++p;
    std::string out;
    for ( ; p < json.size( ); ++p )
    {
        const char c = json[p];
        if ( c == '\\' && p + 1 < json.size( ) )
        {
            const char n = json[++p];
            if ( n == 'n' )
                out.push_back( '\n' );
            else if ( n == 'r' )
                out.push_back( '\r' );
            else if ( n == 't' )
                out.push_back( '\t' );
            else if ( n == '"' || n == '\\' || n == '/' )
                out.push_back( n );
            else if ( n == 'u' && p + 4 < json.size( ) )
            {
                
                p += 4;
            }
            else
                out.push_back( n );
            continue;
        }
        if ( c == '"' )
            break;
        out.push_back( c );
    }
    return out;
}

auto parse_lrc( const std::string& lrc ) -> std::vector<lyric_line_t>
{
    std::vector<lyric_line_t> lines;
    size_t i = 0;
    while ( i < lrc.size( ) )
    {
        size_t eol = lrc.find( '\n', i );
        if ( eol == std::string::npos )
            eol = lrc.size( );
        std::string row = trim( lrc.substr( i, eol - i ) );
        i = eol + 1;
        if ( row.empty( ) || row.front( ) != '[' )
            continue;

        
        size_t close = row.find( ']' );
        if ( close == std::string::npos )
            continue;
        const std::string stamp = row.substr( 1, close - 1 );
        const std::string text = trim( row.substr( close + 1 ) );
        if ( text.empty( ) )
            continue;

        int mm = 0, ss = 0, frac = 0;
        if ( sscanf_s( stamp.c_str( ), "%d:%d.%d", &mm, &ss, &frac ) >= 2
            || sscanf_s( stamp.c_str( ), "%d:%d", &mm, &ss ) >= 2 )
        {
            float t = ( float )mm * 60.f + ( float )ss;
            if ( frac > 0 )
            {
                float denom = 1.f;
                int f = frac;
                while ( f >= 10 )
                {
                    denom *= 10.f;
                    f /= 10;
                }
                
                if ( frac < 100 )
                    t += ( float )frac / 100.f;
                else
                    t += ( float )frac / 1000.f;
            }
            lines.push_back( { t, text } );
        }
    }
    std::sort( lines.begin( ), lines.end( ),
        []( const lyric_line_t& a, const lyric_line_t& b ) { return a.t < b.t; } );
    return lines;
}

auto http_get_https( const std::wstring& host, const std::wstring& path ) -> std::string
{
    std::string body;
    HINTERNET session = WinHttpOpen(
        L"FeelingsFM/1.0 (https://feelings.lol)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0 );
    if ( !session )
        return body;

    
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption( session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof( protocols ) );

    HINTERNET connect = WinHttpConnect( session, host.c_str( ), INTERNET_DEFAULT_HTTPS_PORT, 0 );
    if ( !connect )
    {
        WinHttpCloseHandle( session );
        return body;
    }

    HINTERNET request = WinHttpOpenRequest( connect, L"GET", path.c_str( ),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE );
    if ( !request )
    {
        WinHttpCloseHandle( connect );
        WinHttpCloseHandle( session );
        return body;
    }

    
    WinHttpAddRequestHeaders(
        request,
        L"User-Agent: FeelingsFM/1.0 (https://feelings.lol)\r\nAccept: application/json\r\n",
        ( ULONG )-1L,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE );

    BOOL ok = WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0 );
    if ( ok )
        ok = WinHttpReceiveResponse( request, nullptr );

    if ( ok )
    {
        for ( ;; )
        {
            DWORD avail = 0;
            if ( !WinHttpQueryDataAvailable( request, &avail ) || avail == 0 )
                break;
            std::string chunk( avail, '\0' );
            DWORD read = 0;
            if ( !WinHttpReadData( request, chunk.data( ), avail, &read ) || read == 0 )
                break;
            chunk.resize( read );
            body += chunk;
        }
    }

    WinHttpCloseHandle( request );
    WinHttpCloseHandle( connect );
    WinHttpCloseHandle( session );
    return body;
}

auto utf8_to_wide( const std::string& s ) -> std::wstring
{
    if ( s.empty( ) )
        return {};
    const int n = MultiByteToWideChar( CP_UTF8, 0, s.c_str( ), -1, nullptr, 0 );
    if ( n <= 0 )
        return {};
    std::wstring out( ( size_t )n - 1, L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, s.c_str( ), -1, out.data( ), n );
    return out;
}

auto lyrics_from_json( const std::string& json ) -> lyrics_t
{
    lyrics_t next{};
    if ( json.empty( ) )
    {
        next.soft_fail = true;
        return next;
    }
    if ( json.find( "TrackNotFound" ) != std::string::npos
        || json.find( "\"statusCode\":404" ) != std::string::npos
        || json.find( "\"statusCode\": 404" ) != std::string::npos )
    {
        next.failed = true;
        return next;
    }

    const std::string synced = json_string_field( json, "syncedLyrics" );
    const std::string plain = json_string_field( json, "plainLyrics" );
    if ( !synced.empty( ) )
        next.lines = parse_lrc( synced );
    next.plain = plain;
    next.ready = !next.lines.empty( ) || !next.plain.empty( );
    if ( !next.ready )
        next.failed = true;
    return next;
}

auto artist_match_score( const std::string& want, const std::string& got ) -> int
{
    if ( want.empty( ) || got.empty( ) )
        return 0;
    const std::string a = to_lower( want );
    const std::string b = to_lower( got );
    if ( a == b )
        return 100;
    if ( a.find( b ) != std::string::npos || b.find( a ) != std::string::npos )
        return 70;
    
    int hits = 0;
    size_t i = 0;
    while ( i < a.size( ) )
    {
        while ( i < a.size( ) && !std::isalnum( ( unsigned char )a[i] ) )
            ++i;
        size_t j = i;
        while ( j < a.size( ) && std::isalnum( ( unsigned char )a[j] ) )
            ++j;
        if ( j > i )
        {
            const std::string tok = a.substr( i, j - i );
            if ( tok.size( ) >= 3 && b.find( tok ) != std::string::npos )
                ++hits;
        }
        i = j;
    }
    return hits * 15;
}

auto search_and_get( const std::string& query, const std::string& prefer_artist ) -> lyrics_t
{
    const std::string search = "/api/search?q=" + url_encode( query );
    const std::string list = http_get_https( L"lrclib.net", utf8_to_wide( search ) );
    if ( list.empty( ) )
    {
        lyrics_t miss{};
        miss.soft_fail = true;
        return miss;
    }
    if ( list == "[]" )
    {
        lyrics_t miss{};
        miss.failed = true;
        return miss;
    }

    size_t cursor = 0;
    int best_id = 0;
    int best_score = -1;
    std::string best_chunk;
    while ( ( cursor = list.find( "\"id\":", cursor ) ) != std::string::npos )
    {
        int id = 0;
        if ( sscanf_s( list.c_str( ) + cursor, "\"id\":%d", &id ) == 1 && id > 0 )
        {
            const size_t chunk_end = list.find( "\"id\":", cursor + 5 );
            const std::string chunk = list.substr( cursor, chunk_end == std::string::npos ? list.size( ) - cursor : chunk_end - cursor );
            const std::string artist = json_string_field( chunk, "artistName" );
            const bool has_sync = chunk.find( "\"syncedLyrics\":\"" ) != std::string::npos;
            const bool has_plain = chunk.find( "\"plainLyrics\":\"" ) != std::string::npos;
            
            const bool fetchable = has_sync || has_plain || id > 0;

            int score = artist_match_score( prefer_artist, artist );
            if ( has_sync )
                score += 40;
            else if ( has_plain )
                score += 15;
            else
                score += 5; 

            if ( prefer_artist.empty( ) )
                score += 10;

            
            if ( !prefer_artist.empty( ) && score < 10 && has_sync )
            {
                cursor += 5;
                continue;
            }
            if ( !prefer_artist.empty( ) && artist_match_score( prefer_artist, artist ) == 0 && !prefer_artist.empty( ) )
            {
                
                score -= 20;
            }

            if ( !fetchable )
            {
                cursor += 5;
                continue;
            }

            if ( score > best_score )
            {
                best_score = score;
                best_id = id;
                best_chunk = chunk;
            }
        }
        cursor += 5;
    }

    if ( best_id <= 0 )
    {
        lyrics_t miss{};
        miss.failed = true;
        return miss;
    }

    
    lyrics_t embedded = lyrics_from_json( best_chunk );
    if ( embedded.ready )
        return embedded;

    wchar_t wpath[80];
    swprintf_s( wpath, L"/api/get?id=%d", best_id );
    return lyrics_from_json( http_get_https( L"lrclib.net", wpath ) );
}

auto fetch_lyrics_ovh( const std::string& artist, const std::string& title ) -> lyrics_t
{
    lyrics_t next{};
    if ( artist.empty( ) || title.empty( ) )
    {
        next.failed = true;
        return next;
    }
    
    const std::string path = "/v1/" + url_encode( artist ) + "/" + url_encode( title );
    
    std::string path2;
    for ( char c : path )
        path2 += ( c == '+' ) ? "%20" : std::string( 1, c );

    const std::string json = http_get_https( L"api.lyrics.ovh", utf8_to_wide( path2 ) );
    if ( json.empty( ) )
    {
        next.soft_fail = true;
        return next;
    }
    const std::string plain = json_string_field( json, "lyrics" );
    if ( plain.empty( ) )
    {
        next.failed = true;
        return next;
    }
    next.plain = plain;
    next.ready = true;
    return next;
}

auto fetch_lyrics_async( std::string artist, std::string title ) -> void
{
    if ( title.empty( ) )
        return;
    if ( g_fetching.exchange( true ) )
        return;

    set_status( "fetching lyrics..." );
    fm_log( "[fm] fetch '%s' — '%s'", artist.c_str( ), title.c_str( ) );

    std::thread( [artist = std::move( artist ), title = std::move( title )]( )
    {
        const std::string key = artist + "\n" + title;
        const std::string clean_artist = strip_noise( artist );
        const std::string clean_title = strip_noise( title );

        lyrics_t next{};

        next = search_and_get( clean_title + " " + clean_artist, clean_artist );
        if ( !next.ready )
            next = search_and_get( clean_artist + " " + clean_title, clean_artist );
        if ( !next.ready )
            next = search_and_get( clean_title, clean_artist );

        if ( !next.ready && !clean_artist.empty( ) )
        {
            const std::string path_a =
                "/api/get?artist_name=" + url_encode( clean_artist ) + "&track_name=" + url_encode( clean_title );
            next = lyrics_from_json( http_get_https( L"lrclib.net", utf8_to_wide( path_a ) ) );
        }
        if ( !next.ready && !clean_artist.empty( ) )
        {
            const std::string path_b =
                "/api/get?artist_name=" + url_encode( clean_title ) + "&track_name=" + url_encode( clean_artist );
            next = lyrics_from_json( http_get_https( L"lrclib.net", utf8_to_wide( path_b ) ) );
        }
        if ( !next.ready )
            next = fetch_lyrics_ovh( clean_artist, clean_title );

        next.key = key;
        if ( !next.ready && !next.soft_fail )
            next.failed = true;

        const bool ok = next.ready;
        const bool soft = next.soft_fail;
        const size_t nlines = next.lines.size( );

        {
            std::lock_guard<std::mutex> lk( g_mtx );
            
            if ( g_track.artist + "\n" + g_track.title == key )
            {
                g_lyrics = std::move( next );
                if ( ok )
                    g_status = nlines == 0 ? "plain lyrics" : "synced lyrics";
                else if ( soft )
                    g_status = "lyrics network error — retrying";
                else
                    g_status = "no lyrics in database";
            }
        }

        if ( ok )
            fm_log( "[fm] lyrics ok (%zu synced lines)", nlines );
        else if ( soft )
            fm_log( "[fm] lyrics soft-fail (network)" );
        else
            fm_log( "[fm] lyrics miss for '%s' / '%s'", clean_artist.c_str( ), clean_title.c_str( ) );

        g_fetching = false;
    } ).detach( );
}

auto ensure_winrt( ) -> bool
{
    if ( g_winrt_ready )
        return true;
    try
    {
        winrt::init_apartment( winrt::apartment_type::multi_threaded );
        g_winrt_ready = true;
    }
    catch ( ... )
    {
        try
        {
            
            g_winrt_ready = true;
        }
        catch ( ... )
        {
            g_winrt_ready = false;
        }
    }
    return g_winrt_ready;
}

struct now_playing_t
{
    track_t track{};
    float position = 0.f;
    float position_raw = 0.f;
    float duration = 0.f;
    bool playing = false;
    bool ok = false;
};

auto poll_smtc( ) -> now_playing_t
{
    now_playing_t out{};
    if ( !ensure_winrt( ) )
        return out;

    try
    {
        using namespace winrt::Windows::Media::Control;
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync( ).get( );
        auto sessions = manager.GetSessions( );

        GlobalSystemMediaTransportControlsSession best{ nullptr };
        for ( auto const& session : sessions )
        {
            const std::string aumid = to_lower( winrt::to_string( session.SourceAppUserModelId( ) ) );
            if ( aumid.find( "spotify" ) != std::string::npos )
            {
                best = session;
                break;
            }
        }
        if ( !best )
            best = manager.GetCurrentSession( );
        if ( !best )
            return out;

        auto props = best.TryGetMediaPropertiesAsync( ).get( );
        if ( !props )
            return out;

        out.track.title = trim( winrt::to_string( props.Title( ) ) );
        out.track.artist = trim( winrt::to_string( props.Artist( ) ) );
        out.track.album = trim( winrt::to_string( props.AlbumTitle( ) ) );
        out.track.raw_title = out.track.artist + "\n" + out.track.title;

        try
        {
            const auto info = best.GetPlaybackInfo( );
            out.playing = info.PlaybackStatus( ) == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        }
        catch ( ... )
        {
            out.playing = true;
        }

        try
        {
            const auto tl = best.GetTimelineProperties( );
            out.position_raw = std::chrono::duration<float>( tl.Position( ) ).count( );
            out.position = out.position_raw;
            out.duration = std::chrono::duration<float>( tl.EndTime( ) ).count( );
            if ( out.duration <= 0.f )
                out.duration = std::chrono::duration<float>( tl.MaxSeekTime( ) ).count( );

            // SMTC Position freezes between sparse updates — advance with LastUpdatedTime
            // so the UI doesn't stick at e.g. 0:16 then jump to 0:24.
            try
            {
                const auto last = tl.LastUpdatedTime( );
                const auto now = winrt::clock::now( );
                if ( out.playing && now > last )
                {
                    const float lag = std::chrono::duration<float>( now - last ).count( );
                    if ( lag > 0.f && lag < 8.f )
                        out.position += lag;
                }
            }
            catch ( ... )
            {
            }
        }
        catch ( ... )
        {
            out.position = 0.f;
            out.duration = 0.f;
        }

        out.ok = !out.track.title.empty( );
    }
    catch ( ... )
    {
        out.ok = false;
    }
    return out;
}

auto release_cover_srv( ) -> void
{
    if ( g_cover_srv )
    {
        g_cover_srv->Release( );
        g_cover_srv = nullptr;
    }
    g_cover_srv_key.clear( );
    g_cover_w = 0;
    g_cover_h = 0;
}

auto read_smtc_thumbnail_bytes( ) -> std::vector<uint8_t>
{
    std::vector<uint8_t> out;
    if ( !ensure_winrt( ) )
        return out;

    try
    {
        using namespace winrt::Windows::Media::Control;
        using namespace winrt::Windows::Storage::Streams;

        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync( ).get( );
        auto sessions = manager.GetSessions( );

        GlobalSystemMediaTransportControlsSession best{ nullptr };
        for ( auto const& session : sessions )
        {
            const std::string aumid = to_lower( winrt::to_string( session.SourceAppUserModelId( ) ) );
            if ( aumid.find( "spotify" ) != std::string::npos )
            {
                best = session;
                break;
            }
        }
        if ( !best )
            best = manager.GetCurrentSession( );
        if ( !best )
            return out;

        auto props = best.TryGetMediaPropertiesAsync( ).get( );
        if ( !props )
            return out;

        auto thumb = props.Thumbnail( );
        if ( !thumb )
            return out;

        auto stream = thumb.OpenReadAsync( ).get( );
        if ( !stream )
            return out;

        const uint32_t size = ( uint32_t )stream.Size( );
        if ( size == 0 || size > 8u * 1024u * 1024u )
            return out;

        DataReader reader( stream );
        reader.LoadAsync( size ).get( );
        out.resize( size );
        reader.ReadBytes( out );
    }
    catch ( ... )
    {
        out.clear( );
    }
    return out;
}

auto fetch_cover_async( std::string key ) -> void
{
    if ( key.empty( ) )
        return;
    if ( g_cover_fetching.exchange( true ) )
        return;

    std::thread( [key = std::move( key )]( )
    {
        auto bytes = read_smtc_thumbnail_bytes( );
        {
            std::lock_guard<std::mutex> lk( g_mtx );
            if ( g_track.raw_title == key )
            {
                g_cover_bytes = std::move( bytes );
                g_cover_key = key;
                g_cover_ready = !g_cover_bytes.empty( );
                if ( g_cover_ready )
                    fm_log( "[fm] cover ok (%zu bytes)", g_cover_bytes.size( ) );
                else
                    fm_log( "[fm] cover miss" );
            }
        }
        g_cover_fetching = false;
    } ).detach( );
}

auto create_srv_from_image_bytes( ID3D11Device* device, const std::vector<uint8_t>& bytes, int* out_w, int* out_h ) -> ID3D11ShaderResourceView*
{
    if ( !device || bytes.empty( ) )
        return nullptr;

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    UINT w = 0, h = 0;
    std::vector<BYTE> pixels;

    HRESULT hr = CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) );
    if ( FAILED( hr ) )
        goto done;
    hr = factory->CreateStream( &stream );
    if ( FAILED( hr ) )
        goto done;
    hr = stream->InitializeFromMemory( ( BYTE* )bytes.data( ), ( DWORD )bytes.size( ) );
    if ( FAILED( hr ) )
        goto done;
    hr = factory->CreateDecoderFromStream( stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder );
    if ( FAILED( hr ) )
        goto done;
    hr = decoder->GetFrame( 0, &frame );
    if ( FAILED( hr ) )
        goto done;
    hr = factory->CreateFormatConverter( &converter );
    if ( FAILED( hr ) )
        goto done;
    hr = converter->Initialize( frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom );
    if ( FAILED( hr ) )
        goto done;
    hr = converter->GetSize( &w, &h );
    if ( FAILED( hr ) || w == 0 || h == 0 )
        goto done;

    pixels.resize( ( size_t )w * ( size_t )h * 4 );
    hr = converter->CopyPixels( nullptr, w * 4, ( UINT )pixels.size( ), pixels.data( ) );
    if ( FAILED( hr ) )
        goto done;

    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = pixels.data( );
        sub.SysMemPitch = w * 4;

        hr = device->CreateTexture2D( &desc, &sub, &tex );
        if ( FAILED( hr ) )
            goto done;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView( tex, &srv_desc, &srv );
        if ( FAILED( hr ) )
        {
            srv = nullptr;
            goto done;
        }
    }

    if ( out_w )
        *out_w = ( int )w;
    if ( out_h )
        *out_h = ( int )h;

done:
    if ( tex )
        tex->Release( );
    if ( converter )
        converter->Release( );
    if ( frame )
        frame->Release( );
    if ( decoder )
        decoder->Release( );
    if ( stream )
        stream->Release( );
    if ( factory )
        factory->Release( );
    return srv;
}

auto sync_cover_texture( ) -> void
{
    ID3D11Device* device = nullptr;
    if ( g && g->overlay.m_detail )
        device = g->overlay.m_detail->device;
    if ( !device )
        return;

    std::string key;
    {
        std::lock_guard<std::mutex> lk( g_mtx );
        key = g_cover_key;
        if ( key.empty( ) || g_cover_bytes.empty( ) )
        {
            if ( g_cover_srv )
                release_cover_srv( );
            return;
        }
        if ( g_cover_srv && g_cover_srv_key == key )
            return;
    }

    std::vector<uint8_t> bytes;
    {
        std::lock_guard<std::mutex> lk( g_mtx );
        if ( g_cover_key != key || g_cover_bytes.empty( ) )
            return;
        bytes = g_cover_bytes;
    }

    release_cover_srv( );
    int w = 0, h = 0;
    g_cover_srv = create_srv_from_image_bytes( device, bytes, &w, &h );
    if ( g_cover_srv )
    {
        g_cover_srv_key = key;
        g_cover_w = w;
        g_cover_h = h;
    }
}

auto process_is_spotify( DWORD pid ) -> bool
{
    if ( !pid )
        return false;
    HANDLE h = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
    if ( !h )
        return false;

    wchar_t path[ MAX_PATH ]{};
    DWORD n = MAX_PATH;
    const BOOL ok = QueryFullProcessImageNameW( h, 0, path, &n );
    CloseHandle( h );
    if ( !ok )
        return false;

    std::wstring p( path );
    for ( auto& c : p )
        c = ( wchar_t )towlower( c );
    return p.find( L"\\spotify.exe" ) != std::wstring::npos;
}

struct enum_ctx_t
{
    std::wstring best;
    int best_score = 0;
};

auto CALLBACK enum_spotify( HWND hwnd, LPARAM lp ) -> BOOL
{
    auto* ctx = reinterpret_cast<enum_ctx_t*>( lp );
    if ( !IsWindowVisible( hwnd ) )
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId( hwnd, &pid );
    if ( !process_is_spotify( pid ) )
        return TRUE;

    wchar_t title[ 512 ]{};
    const int n = GetWindowTextW( hwnd, title, 512 );
    if ( n <= 0 )
        return TRUE;

    std::wstring t( title, title + n );
    if ( t == L"Spotify" || t == L"Spotify Premium" || t == L"Spotify Free" )
        return TRUE;

    const bool looks_track =
        t.find( L" - " ) != std::wstring::npos
        || t.find( L" • " ) != std::wstring::npos
        || t.find( L" – " ) != std::wstring::npos
        || t.find( L" — " ) != std::wstring::npos;

    int score = looks_track ? 10 : 2;
    score += ( int )std::min<size_t>( t.size( ) / 6, 6 );
    if ( score > ctx->best_score )
    {
        ctx->best_score = score;
        ctx->best = std::move( t );
    }
    return TRUE;
}

auto poll_spotify_title( ) -> std::wstring
{
    enum_ctx_t ctx{};
    EnumWindows( enum_spotify, reinterpret_cast<LPARAM>( &ctx ) );
    if ( ctx.best_score >= 2 )
        return ctx.best;
    return {};
}

auto split_title_artist( const std::string& raw, std::string& left, std::string& right ) -> bool
{
    const char* seps[] = { " • ", " – ", " — ", " - " };
    for ( const char* sep : seps )
    {
        const size_t p = raw.find( sep );
        if ( p == std::string::npos )
            continue;
        left = trim( raw.substr( 0, p ) );
        right = trim( raw.substr( p + strlen( sep ) ) );
        return !left.empty( ) && !right.empty( );
    }
    return false;
}

auto parse_track_from_title( const std::wstring& title_w ) -> track_t
{
    track_t t{};
    std::string title = wide_to_utf8( title_w );
    t.raw_title = title;

    const char* suffixes[] = { " - Spotify Premium", " - Spotify Free", " - Spotify", " • Spotify" };
    for ( const char* s : suffixes )
    {
        const size_t p = title.rfind( s );
        if ( p != std::string::npos && p + strlen( s ) == title.size( ) )
        {
            title.resize( p );
            break;
        }
    }
    title = trim( title );

    std::string left, right;
    if ( !split_title_artist( title, left, right ) )
    {
        t.title = title;
        return t;
    }

    t.title = left;
    t.artist = right;
    return t;
}

auto poll_now_playing( ) -> now_playing_t
{
    now_playing_t np = poll_smtc( );
    if ( np.ok )
        return np;

    
    const std::wstring title_w = poll_spotify_title( );
    if ( title_w.empty( ) )
        return np;

    np.track = parse_track_from_title( title_w );
    np.ok = !np.track.title.empty( );
    np.position = -1.f; 
    np.playing = true;
    return np;
}

std::atomic<bool> g_worker_started{ false };

auto media_worker( ) -> void
{
    ensure_winrt( );
    std::string last_key;
    auto last_soft_retry = std::chrono::steady_clock::now( ) - std::chrono::seconds( 30 );

    while ( true )
    {
        if ( !config.settings.feelings_fm )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
            continue;
        }

        const now_playing_t np = poll_now_playing( );
        if ( np.ok )
        {
            bool changed = false;
            {
                std::lock_guard<std::mutex> lk( g_mtx );
                if ( np.position >= 0.f )
                {
                    const auto now = std::chrono::steady_clock::now( );
                    float predicted = g_playback_pos;
                    if ( g_have_playback_clock && g_playback_playing )
                    {
                        predicted += std::chrono::duration<float>( now - g_playback_stamp ).count( );
                    }
                    const float delta = np.position - predicted;
                    const bool raw_moved = g_last_smtc_raw < 0.f
                        || std::fabs( np.position_raw - g_last_smtc_raw ) > 0.45f;

                    // Hard sync on first sample / real seeks. Ignore frozen SMTC snapshots
                    // that would yank the clock backward while local time is still good.
                    if ( !g_have_playback_clock )
                    {
                        g_playback_pos = np.position;
                        g_playback_stamp = now;
                    }
                    else if ( delta > 1.75f )
                    {
                        g_playback_pos = np.position;
                        g_playback_stamp = now;
                    }
                    else if ( delta < -2.0f && raw_moved )
                    {
                        g_playback_pos = np.position;
                        g_playback_stamp = now;
                    }
                    else if ( delta > 0.3f )
                    {
                        g_playback_pos = predicted + delta * 0.4f;
                        g_playback_stamp = now;
                    }
                    else
                    {
                        g_playback_pos = predicted;
                        g_playback_stamp = now;
                    }
                    g_last_smtc_raw = np.position_raw;
                    g_playback_playing = np.playing;
                    g_have_playback_clock = true;
                }
                if ( np.duration > 1.f )
                    g_playback_duration = np.duration;
                if ( np.track.raw_title != g_track.raw_title
                    || np.track.title != g_track.title
                    || np.track.artist != g_track.artist )
                {
                    g_track = np.track;
                    if ( !np.track.title.empty( ) )
                    {
                        playlist_entry_t entry{};
                        entry.track = np.track;
                        entry.played_at = std::chrono::steady_clock::now( );
                        if ( !g_playlist.empty( ) && g_playlist.front( ).track.raw_title == np.track.raw_title )
                            g_playlist.erase( g_playlist.begin( ) );
                        g_playlist.insert( g_playlist.begin( ), entry );
                        if ( g_playlist.size( ) > k_playlist_max )
                            g_playlist.resize( k_playlist_max );
                    }
                    if ( np.position < 0.f )
                    {
                        g_playback_pos = 0.f;
                        g_playback_stamp = std::chrono::steady_clock::now( );
                        g_playback_playing = true;
                        g_have_playback_clock = true;
                        g_last_smtc_raw = -1.f;
                    }
                    g_lyrics = {};
                    g_cover_bytes.clear( );
                    g_cover_key.clear( );
                    g_cover_ready = false;
                    g_status = "track updated";
                    g_last_smtc_raw = -1.f;
                    if ( np.position >= 0.f )
                    {
                        g_playback_pos = np.position;
                        g_playback_stamp = std::chrono::steady_clock::now( );
                        g_playback_playing = np.playing;
                        g_have_playback_clock = true;
                        g_last_smtc_raw = np.position_raw;
                    }
                    changed = true;
                    last_key.clear( );
                }
            }

            const std::string key = np.track.artist + "\n" + np.track.title;
            if ( changed )
                fm_log( "[fm] now playing '%s' — '%s'", np.track.artist.c_str( ), np.track.title.c_str( ) );

            // fetch lyrics immediately on track change — don't wait for debounce
            if ( changed || ( key != last_key && !np.track.title.empty( ) ) )
            {
                last_key = key;
                fetch_lyrics_async( np.track.artist, np.track.title );
                fetch_cover_async( np.track.raw_title );
            }
            else if ( !g_fetching.load( ) )
            {
                lyrics_t lyr{};
                {
                    std::lock_guard<std::mutex> lk( g_mtx );
                    lyr = g_lyrics;
                }
                const auto nowc = std::chrono::steady_clock::now( );
                if ( !lyr.ready && !lyr.failed && !lyr.soft_fail )
                    fetch_lyrics_async( np.track.artist, np.track.title );
                else if ( lyr.soft_fail && nowc - last_soft_retry > std::chrono::seconds( 2 ) )
                {
                    last_soft_retry = nowc;
                    fetch_lyrics_async( np.track.artist, np.track.title );
                }
            }
        }
        else
        {
            std::lock_guard<std::mutex> lk( g_mtx );
            if ( g_track.raw_title.empty( ) )
                g_status = "no media session — play spotify";
        }

        // faster poll — SMTC still sparse, but play/pause + track changes pick up sooner
        std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 250 : 80 ) );
    }
}

auto ensure_worker( ) -> void
{
    if ( g_worker_started.exchange( true ) )
        return;
    std::thread( media_worker ).detach( );
}

enum class transport_t
{
    prev,
    play_pause,
    next
};

auto find_spotify_session( )
{
    using namespace winrt::Windows::Media::Control;
    GlobalSystemMediaTransportControlsSession best{ nullptr };
    if ( !ensure_winrt( ) )
        return best;

    try
    {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync( ).get( );
        auto sessions = manager.GetSessions( );
        for ( auto const& session : sessions )
        {
            const std::string aumid = to_lower( winrt::to_string( session.SourceAppUserModelId( ) ) );
            if ( aumid.find( "spotify" ) != std::string::npos )
            {
                best = session;
                break;
            }
        }
        if ( !best )
            best = manager.GetCurrentSession( );
    }
    catch ( ... )
    {
        best = nullptr;
    }
    return best;
}

auto media_key( WORD vk ) -> void
{
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput( 2, in, sizeof( INPUT ) );
}

auto transport( transport_t cmd ) -> void
{
    
    try
    {
        using namespace winrt::Windows::Media::Control;
        auto session = find_spotify_session( );
        if ( session )
        {
            switch ( cmd )
            {
            case transport_t::prev:
                session.TrySkipPreviousAsync( );
                return;
            case transport_t::next:
                session.TrySkipNextAsync( );
                return;
            case transport_t::play_pause:
            {
                const auto status = session.GetPlaybackInfo( ).PlaybackStatus( );
                if ( status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing )
                    session.TryPauseAsync( );
                else
                    session.TryPlayAsync( );
                return;
            }
            }
        }
    }
    catch ( ... )
    {
    }

    switch ( cmd )
    {
    case transport_t::prev: media_key( VK_MEDIA_PREV_TRACK ); break;
    case transport_t::next: media_key( VK_MEDIA_NEXT_TRACK ); break;
    case transport_t::play_pause: media_key( VK_MEDIA_PLAY_PAUSE ); break;
    }
}

auto current_lyric_index( const std::vector<lyric_line_t>& lines, float elapsed ) -> int
{
    if ( lines.empty( ) )
        return -1;
    // tiny lead only — larger lookahead jumped lines early / mismatched stamps
    const float t = elapsed + 0.04f;
    int best = 0;
    for ( int i = 0; i < ( int )lines.size( ); ++i )
    {
        if ( lines[i].t <= t )
            best = i;
        else
            break;
    }
    return best;
}

auto elapsed_playback( ) -> float
{
    std::lock_guard<std::mutex> lk( g_mtx );
    if ( !g_have_playback_clock )
        return 0.f;
    float t = g_playback_pos;
    if ( g_playback_playing )
    {
        const float dt = std::chrono::duration<float>(
            std::chrono::steady_clock::now( ) - g_playback_stamp ).count( );
        t += dt;
    }
    return t < 0.f ? 0.f : t;
}

float g_fm_screen_x0 = 0.f, g_fm_screen_y0 = 0.f;
float g_fm_screen_x1 = 0.f, g_fm_screen_y1 = 0.f;
bool g_fm_has_rect = false;

auto format_time( float sec ) -> std::string
{
    if ( sec < 0.f )
        sec = 0.f;
    const int total = ( int )sec;
    char buf[16];
    sprintf_s( buf, "%d:%02d", total / 60, total % 60 );
    return buf;
}

} 

auto wants_input( ) -> bool
{
    if ( !config.settings.feelings_fm || !g_fm_has_rect )
        return false;

    
    POINT pt{};
    if ( !GetCursorPos( &pt ) )
        return false;

    return pt.x >= ( LONG )g_fm_screen_x0 && pt.x <= ( LONG )g_fm_screen_x1
        && pt.y >= ( LONG )g_fm_screen_y0 && pt.y <= ( LONG )g_fm_screen_y1;
}

auto draw( const ImVec4& accent ) -> void
{
    if ( !config.settings.feelings_fm )
    {
        g_fm_has_rect = false;
        return;
    }

    ensure_worker( );
    sync_cover_texture( );

    track_t track{};
    lyrics_t lyrics{};
    std::string status;
    bool playing = false;
    float duration = 0.f;
    {
        std::lock_guard<std::mutex> lk( g_mtx );
        track = g_track;
        lyrics = g_lyrics;
        status = g_status;
        playing = g_playback_playing;
        duration = g_playback_duration;
    }
    const float elapsed = elapsed_playback( );

    if ( g_cover_srv && !track.raw_title.empty( ) && g_cover_srv_key != track.raw_title )
        release_cover_srv( );

    // single current lyric line (mock layout)
    std::string lyric_line;
    if ( !lyrics.lines.empty( ) )
    {
        const int idx = current_lyric_index( lyrics.lines, elapsed );
        if ( idx >= 0 && idx < ( int )lyrics.lines.size( ) )
            lyric_line = lyrics.lines[idx].text;
    }
    else if ( !lyrics.plain.empty( ) )
    {
        // first non-empty line of plain lyrics
        size_t start = 0;
        while ( start < lyrics.plain.size( ) )
        {
            const size_t end = lyrics.plain.find( '\n', start );
            std::string line = trim( lyrics.plain.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
            if ( !line.empty( ) )
            {
                lyric_line = line;
                break;
            }
            if ( end == std::string::npos )
                break;
            start = end + 1;
        }
    }

    ImFont* ui_font = ImGui::GetFont( );
    const float px = ImGui::GetFontSize( ) > 1.f ? ImGui::GetFontSize( ) : 13.f;

    static ImVec2 win_pos = ImVec2( 14.f, 360.f );
    const float win_w = 280.f;
    const float win_h = 148.f;

    ImGui::SetNextWindowPos( win_pos, ImGuiCond_Once );
    ImGui::SetNextWindowSize( ImVec2( win_w, win_h ), ImGuiCond_Always );
    ImGui::SetNextWindowBgAlpha( 0.94f );

    // match Feelings mirage menu: dark #0c0c0c child, accent #9896d6, SliderGrab border
    const ImVec4 mirage_bg = ImAdd::HexToColorVec4( 0x0c0c0c, 0.94f );
    const ImVec4 border = ImGui::GetStyle( ).Colors[ImGuiCol_SliderGrab];
    ImGui::PushStyleColor( ImGuiCol_WindowBg, mirage_bg );
    ImGui::PushStyleColor( ImGuiCol_Border, border );
    ImGui::PushStyleColor( ImGuiCol_BorderShadow, ImVec4( 0, 0, 0, 0 ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10.f, 8.f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 6.f, 3.f ) );

    ImGui::Begin( "feelings_fm", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoFocusOnAppearing );

    if ( ImGui::IsWindowHovered( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
    {
        win_pos.x += ImGui::GetIO( ).MouseDelta.x;
        win_pos.y += ImGui::GetIO( ).MouseDelta.y;
        ImGui::SetWindowPos( win_pos );
    }
    win_pos = ImGui::GetWindowPos( );

    ImDrawList* dl = ImGui::GetWindowDrawList( );
    const ImVec2 wp = ImGui::GetWindowPos( );

    ImGui::PushFont( ui_font, px );

    // header: "feelings fm live"
    {
        char head[64];
        const char* state = track.title.empty( ) ? "idle" : ( playing ? "live" : "paused" );
        sprintf_s( head, "feelings fm %s", state );
        dl->AddText( ui_font, px, ImVec2( wp.x + 10.f, wp.y + 8.f ), ImGui::GetColorU32( accent ), head );

        const float line_y = wp.y + 8.f + px + 4.f;
        dl->AddLine(
            ImVec2( wp.x + 10.f, line_y ),
            ImVec2( wp.x + win_w - 10.f, line_y ),
            ImGui::GetColorU32( accent ), 1.5f );
        ImGui::Dummy( ImVec2( 0.f, px + 10.f ) );
    }

    // cover + meta
    {
        const float cover = 56.f;
        const ImVec2 cover_pos = ImGui::GetCursorScreenPos( );
        const ImVec2 cover_max( cover_pos.x + cover, cover_pos.y + cover );

        dl->AddRectFilled( cover_pos, cover_max, IM_COL32( 12, 12, 14, 255 ) );
        if ( g_cover_srv )
        {
            dl->AddImage(
                ImTextureRef( ( void* )g_cover_srv ),
                cover_pos, cover_max,
                ImVec2( 0.f, 0.f ), ImVec2( 1.f, 1.f ) );
        }
        else
        {
            const char* ph = track.title.empty( ) ? "?" : track.title.c_str( );
            char letter[2]{ ( char )std::toupper( ( unsigned char )ph[0] ), 0 };
            const ImVec2 ls = ui_font->CalcTextSizeA( px, FLT_MAX, 0.f, letter );
            dl->AddText( ui_font, px,
                ImVec2( cover_pos.x + ( cover - ls.x ) * 0.5f, cover_pos.y + ( cover - ls.y ) * 0.5f ),
                ImGui::GetColorU32( ImVec4( accent.x, accent.y, accent.z, 0.55f ) ),
                letter );
        }
        dl->AddRect( cover_pos, cover_max, ImGui::GetColorU32( ImVec4( accent.x, accent.y, accent.z, 0.85f ) ), 0.f, 0, 1.f );

        const float text_x = cover_pos.x + cover + 10.f;
        const float text_max_w = wp.x + win_w - 10.f - text_x;
        float ty = cover_pos.y + 2.f;

        auto draw_clipped = [&]( const char* text, ImU32 col, float size )
        {
            if ( !text || !text[0] )
                return;
            char buf[96];
            strncpy_s( buf, text, _TRUNCATE );

            ImVec2 sz = ui_font->CalcTextSizeA( size, FLT_MAX, 0.f, buf );
            if ( sz.x > text_max_w && text_max_w > 8.f )
            {
                while ( sz.x > text_max_w && strlen( buf ) > 3 )
                {
                    buf[strlen( buf ) - 1] = 0;
                    sz = ui_font->CalcTextSizeA( size, FLT_MAX, 0.f, buf );
                }
                if ( strlen( buf ) >= 3 )
                {
                    buf[strlen( buf ) - 1] = '.';
                    buf[strlen( buf ) - 2] = '.';
                    buf[strlen( buf ) - 3] = '.';
                }
            }
            dl->AddText( ui_font, size, ImVec2( text_x, ty ), col, buf );
            ty += size + 2.f;
        };

        if ( track.title.empty( ) )
        {
            draw_clipped( "nothing playing", IM_COL32( 180, 180, 180, 180 ), px );
            draw_clipped( status.c_str( ), IM_COL32( 120, 120, 130, 160 ), px - 1.f );
        }
        else
        {
            draw_clipped( track.title.c_str( ), IM_COL32( 255, 255, 255, 245 ), px );
            if ( !track.artist.empty( ) )
                draw_clipped( track.artist.c_str( ), ImGui::GetColorU32( accent ), px - 1.f );
            if ( !lyric_line.empty( ) )
                draw_clipped( lyric_line.c_str( ), IM_COL32( 170, 170, 185, 220 ), px - 1.f );
        }

        ImGui::SetCursorScreenPos( ImVec2( cover_pos.x, cover_pos.y + cover + 8.f ) );
    }

    // progress + time (SliderGrab-style track)
    {
        float bar_dur = duration;
        if ( bar_dur < 1.f && !lyrics.lines.empty( ) )
            bar_dur = lyrics.lines.back( ).t + 3.f;
        if ( bar_dur < 1.f )
            bar_dur = 1.f;

        const float t = ImClamp( elapsed / bar_dur, 0.f, 1.f );
        const ImVec2 p0 = ImGui::GetCursorScreenPos( );
        const float bar_w = win_w - 20.f;
        const float bar_h = 3.f;
        dl->AddRectFilled( p0, ImVec2( p0.x + bar_w, p0.y + bar_h ), IM_COL32( 44, 44, 44, 255 ) );
        dl->AddRectFilled( p0, ImVec2( p0.x + bar_w * t, p0.y + bar_h ), ImGui::GetColorU32( accent ) );
        ImGui::Dummy( ImVec2( bar_w, bar_h + 4.f ) );

        char timebuf[32];
        if ( duration > 1.f )
            sprintf_s( timebuf, "%s / %s", format_time( elapsed ).c_str( ), format_time( duration ).c_str( ) );
        else
            sprintf_s( timebuf, "%s", format_time( elapsed ).c_str( ) );
        dl->AddText( ui_font, px - 1.f, ImGui::GetCursorScreenPos( ), IM_COL32( 220, 220, 225, 200 ), timebuf );
        ImGui::Dummy( ImVec2( 0.f, px ) );
    }

    ImGui::PopFont( );

    const ImVec2 wmin = ImGui::GetWindowPos( );
    const ImVec2 wsz = ImGui::GetWindowSize( );
    ImGui::End( );

    if ( g && g->overlay.m_detail && g->overlay.m_detail->window )
    {
        POINT tl{ ( LONG )wmin.x, ( LONG )wmin.y };
        POINT br{ ( LONG )( wmin.x + wsz.x ), ( LONG )( wmin.y + wsz.y ) };
        ClientToScreen( g->overlay.m_detail->window, &tl );
        ClientToScreen( g->overlay.m_detail->window, &br );
        g_fm_screen_x0 = ( float )tl.x;
        g_fm_screen_y0 = ( float )tl.y;
        g_fm_screen_x1 = ( float )br.x;
        g_fm_screen_y1 = ( float )br.y;
        g_fm_has_rect = true;
    }

    ImGui::PopStyleVar( 4 );
    ImGui::PopStyleColor( 3 );
}

}
