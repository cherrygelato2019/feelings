#include <include/global.cuh>

#include <cctype>

namespace sdk
{

namespace
{
    auto iequals( const std::string& a, const char* b ) -> bool
    {
        for ( size_t i = 0; ; ++i )
        {
            const char ca = ( char )tolower( ( unsigned char )( i < a.size( ) ? a[i] : 0 ) );
            const char cb = ( char )tolower( ( unsigned char )b[i] );
            if ( ca != cb )
                return false;
            if ( !cb )
                return true;
        }
    }

    auto is_os_platform_token( const std::string& s ) -> bool
    {
        static const char* kKnown[] = {
            "Windows", "OSX", "IOS", "Android", "XBoxOne", "XboxOne", "PS4", "PS5", "PS3",
            "XBox360", "Xbox360", "WiiU", "NX", "Ouya", "AndroidTV", "Chromecast", "Linux",
            "SteamOS", "WebOS", "DOS", "BeOS", "UWP", "MetaOS", "Web", "None"
        };
        for ( const char* k : kKnown )
        {
            if ( iequals( s, k ) )
                return true;
        }
        return false;
    }

    auto is_platform_name_token( const std::string& s ) -> bool
    {
        if ( s.empty( ) || s.size( ) > 48 )
            return false;

        if ( is_os_platform_token( s ) )
            return true;
        static const char* kExtra[] = {
            "Xbox", "PlayStation", "Playstation", "Durango", "Orbis", "Prospero", "Scarlett",
            "iPhone", "iPad", "Android", "Windows", "Mac", "Quest"
        };
        for ( const char* k : kExtra )
        {
            if ( iequals( s, k ) )
                return true;
        }
        return false;
    }

    auto read_string_prop( uintptr_t player, uintptr_t off ) -> std::string
    {
        if ( !player || !off )
            return { };
        uintptr_t ptr = usermode->read<uintptr_t>( player + off );
        if ( !ptr || !usermode->is_valid( ptr ) )
            return { };
        return usermode->read_string( ptr );
    }

    auto discover_platform_offsets( uintptr_t local_player ) -> void
    {
        if ( !local_player || ( offsets::player::os_platform && offsets::player::platform_name ) )
            return;

        for ( uintptr_t off = 0x90; off < 0x480; off += 0x8 )
        {
            if ( off == offsets::player::display_name ||
                 off == offsets::player::locale_id ||
                 off == offsets::player::user_id ||
                 off == offsets::player::team ||
                 off == offsets::player::model_instance )
                continue;

            const std::string s = read_string_prop( local_player, off );
            if ( s.empty( ) )
                continue;

            if ( !offsets::player::os_platform && is_os_platform_token( s ) )
                offsets::player::os_platform = off;
            else if ( !offsets::player::platform_name && is_platform_name_token( s ) && !is_os_platform_token( s ) )
                offsets::player::platform_name = off;

            if ( offsets::player::os_platform && offsets::player::platform_name )
                break;
        }

    }

    auto friendly_platform( const std::string& raw ) -> std::string
    {
        if ( raw.empty( ) )
            return { };

        if ( iequals( raw, "Windows" ) || iequals( raw, "UWP" ) || iequals( raw, "DOS" ) || iequals( raw, "BeOS" ) )
            return "PC";
        if ( iequals( raw, "OSX" ) || iequals( raw, "Mac" ) )
            return "Mac";
        if ( iequals( raw, "Linux" ) || iequals( raw, "SteamOS" ) )
            return "Linux";
        if ( iequals( raw, "IOS" ) || iequals( raw, "iPhone" ) || iequals( raw, "iPad" ) )
            return "iOS";
        if ( iequals( raw, "Android" ) || iequals( raw, "AndroidTV" ) || iequals( raw, "Ouya" ) || iequals( raw, "WebOS" ) )
            return "Android";
        if ( iequals( raw, "XBoxOne" ) || iequals( raw, "XboxOne" ) || iequals( raw, "XBox360" ) ||
             iequals( raw, "Xbox360" ) || iequals( raw, "Xbox" ) || iequals( raw, "Durango" ) || iequals( raw, "Scarlett" ) )
            return "Xbox";
        if ( iequals( raw, "PS5" ) || iequals( raw, "PS4" ) || iequals( raw, "PS3" ) ||
             iequals( raw, "PlayStation" ) || iequals( raw, "Playstation" ) || iequals( raw, "Orbis" ) || iequals( raw, "Prospero" ) )
            return "PlayStation";
        if ( iequals( raw, "MetaOS" ) || iequals( raw, "Quest" ) )
            return "VR";
        if ( iequals( raw, "NX" ) || iequals( raw, "WiiU" ) )
            return "Nintendo";
        if ( iequals( raw, "Web" ) || iequals( raw, "Chromecast" ) )
            return "Web";
        if ( iequals( raw, "None" ) )
            return { };

        return raw;
    }
}

auto u_player::character( ) const -> u_model
{
    if ( !m_address )
        return u_model( );

    auto looks_like_char = []( uintptr_t model ) -> bool
    {
        if ( !model || model < 0x10000 || !usermode->is_valid( model ) )
            return false;
        sdk::u_instance m( model );
        if ( m.find_first_child_of_class( "Humanoid" ) )
            return true;
        if ( m.find_first_child( "HumanoidRootPart" ) )
            return true;
        if ( m.find_first_child( "UpperTorso" ) || m.find_first_child( "Torso" ) )
            return true;
        const std::string cn = m.class_name( );
        return cn == "Model" || cn == "Actor";
    };

    static constexpr uintptr_t k_model_offs[] = {
        offsets::player::model_instance,
        0x398,
        0x380,
        0x338,
        0x330,
        0x340,
        0x298,
        0x360,
    };

    for ( uintptr_t off : k_model_offs )
    {
        const uintptr_t model = usermode->read<uintptr_t>( m_address + off );
        if ( looks_like_char( model ) )
            return u_model( model );
    }
    return u_model( );
}

auto u_player::team( ) const -> u_instance
{
    if ( !m_address )
        return u_instance( );
    return u_instance( usermode->read<uintptr_t>( m_address + offsets::player::team ) );
}

auto u_player::display_name( ) const -> std::string
{
    if ( !m_address )
        return { };

    std::string embedded = usermode->read_string( m_address + offsets::player::display_name );
    if ( !embedded.empty( ) )
        return embedded;

    uintptr_t ptr = usermode->read<uintptr_t>( m_address + offsets::player::display_name );
    if ( ptr && ptr != m_address + offsets::player::display_name && usermode->is_valid( ptr ) )
    {
        std::string s = usermode->read_string( ptr );
        if ( !s.empty( ) )
            return s;
    }
    return { };
}

auto u_player::username( ) const -> std::string
{

    return this->name( );
}

auto u_player::user_id( ) const -> int64_t
{
    if ( !m_address )
        return 0;
    return usermode->read<int64_t>( m_address + offsets::player::user_id );
}

auto u_player::platform( ) const -> std::string
{
    if ( !m_address )
        return { };

    if ( !offsets::player::os_platform && !offsets::player::platform_name )
    {
        uintptr_t local = 0;
        if ( g && g->engine.players.load( ) )
            local = usermode->read<uintptr_t>( g->engine.players.load( ) + offsets::players::local_player );
        if ( !local )
            local = m_address;
        discover_platform_offsets( local );
    }

    std::string raw;
    if ( offsets::player::platform_name )
        raw = read_string_prop( m_address, offsets::player::platform_name );
    if ( raw.empty( ) && offsets::player::os_platform )
        raw = read_string_prop( m_address, offsets::player::os_platform );

    return friendly_platform( raw );
}

}
