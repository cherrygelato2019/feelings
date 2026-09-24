#include <include/global.cuh>

#include <src/cheat/features/player/avatar_changer.cuh>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace avatar_changer
{
namespace
{

std::atomic<bool> g_alive{ false };
std::thread g_thread;

struct face_preset_t
{
    const char* name;
    const char* asset_id;
};

constexpr face_preset_t k_faces[] = {
    { "Super Super Happy Face", "2409898166" },
    { "Playful Vampire", "1223111643" },
    { "Catching Snowflakes", "155197736" },
    { "Roblox Madness Face", "2945572033" },
    { "Red Beast Mode", "2293675506" },
    { "Radioactive Beast Mode", "479872542" },
    { "Blizzard Beast Mode", "752273954" },
    { "Miss Scarlet", "90272641" },
    { "Prankster", "90272223" },
    { "Troublemaker", "155288061" },
    { "Yum", "365335647" },
    { "Green Glowing Eyes", "893052423" },
    { "Red Glowing Eyes", "482618732" },
};

auto local_character( ) -> sdk::u_model
{
    if ( !g->engine.players )
        return {};
    sdk::u_players service( g->engine.players.load( ) );
    sdk::u_player lp = service.local_player( );
    if ( !lp )
        return {};
    return lp.character( );
}

auto looks_like_string( uintptr_t addr ) -> bool
{
    if ( !addr || !usermode->is_valid( addr ) )
        return false;
    const int len = usermode->read<int>( addr + 0x10 );
    return len >= 0 && len < 512;
}

auto write_content_string( uintptr_t base, const std::string& value, std::initializer_list<uintptr_t> offs ) -> bool
{
    if ( !base || value.empty( ) )
        return false;

    for ( uintptr_t off : offs )
    {
        const uintptr_t addr = base + off;
        if ( !looks_like_string( addr ) )
            continue;
        if ( usermode->write_string( addr, value ) )
            return true;
    }
    return false;
}

auto to_asset_url( const char* id_or_url ) -> std::string
{
    if ( !id_or_url || !id_or_url[0] )
        return {};
    if ( std::strncmp( id_or_url, "http", 4 ) == 0 || std::strncmp( id_or_url, "rbxasset", 8 ) == 0 )
        return id_or_url;

    std::string out = "rbxassetid://";
    out += id_or_url;
    return out;
}

auto desired_face_id( ) -> std::string
{
    if ( config.player.face_id[0] )
        return to_asset_url( config.player.face_id );

    const int idx = config.player.face_preset;
    if ( idx >= 0 && idx < ( int )( sizeof( k_faces ) / sizeof( k_faces[0] ) ) )
        return to_asset_url( k_faces[idx].asset_id );
    return {};
}

auto set_face_texture( const sdk::u_instance& head, const std::string& face_url ) -> bool
{
    if ( !head || face_url.empty( ) )
        return false;

    bool wrote = false;
    for ( const auto& child : head.get_children( ) )
    {
        const std::string cn = child.class_name( );
        const std::string nm = child.name( );
        const bool is_face =
            cn == "Decal"
            || nm == "face" || nm == "Face"
            || nm == "faceTex" || nm == "FaceTexture";
        if ( !is_face )
            continue;

        if ( write_content_string( child.address( ), face_url, {
                offsets::decal::texture,
                offsets::decal::texture_alt_a,
                offsets::decal::texture_alt_b,
                offsets::decal::texture_alt_c,
                offsets::tool::texture_id,
            } ) )
            wrote = true;
    }
    return wrote;
}

auto set_clothing( const sdk::u_model& character ) -> void
{
    if ( !character )
        return;

    const std::string shirt = to_asset_url( config.player.shirt_id );
    const std::string pants = to_asset_url( config.player.pants_id );
    const std::string tee = to_asset_url( config.player.tshirt_id );

    for ( const auto& child : character.get_children( ) )
    {
        const std::string cn = child.class_name( );
        if ( cn == "Shirt" && !shirt.empty( ) )
        {
            write_content_string( child.address( ), shirt, {
                offsets::clothing::shirt_template,
                offsets::decal::texture_alt_b,
                offsets::tool::texture_id,
                0x318,
            } );
        }
        else if ( cn == "Pants" && !pants.empty( ) )
        {
            write_content_string( child.address( ), pants, {
                offsets::clothing::pants_template,
                offsets::decal::texture_alt_b,
                offsets::tool::texture_id,
                0x318,
            } );
        }
        else if ( ( cn == "ShirtGraphic" ) && !tee.empty( ) )
        {
            write_content_string( child.address( ), tee, {
                offsets::clothing::graphic,
                offsets::decal::texture_alt_b,
                offsets::tool::texture_id,
                0x318,
            } );
        }
    }
}

auto thread_main( ) -> void
{
    std::string cached_face;
    uintptr_t cached_char = 0;

    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const bool want_face = config.player.face_changer;
        const bool want_cloth = config.player.clothing_changer;
        if ( !want_face && !want_cloth )
        {
            cached_face.clear( );
            cached_char = 0;
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            continue;
        }

        if ( !usermode->can_rpm( ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            continue;
        }

        sdk::u_model character = local_character( );
        if ( !character )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            continue;
        }

        if ( character.address( ) != cached_char )
        {
            cached_char = character.address( );
            cached_face.clear( );
        }

        if ( want_face )
        {
            const std::string face = desired_face_id( );
            if ( !face.empty( ) && face != cached_face )
            {
                sdk::u_instance head = character.find_first_child( "Head" );
                if ( !head )
                    head = character.find_first_child( "head" );
                if ( head && set_face_texture( head, face ) )
                    cached_face = face;
            }
            else if ( !face.empty( ) )
            {
                sdk::u_instance head = character.find_first_child( "Head" );
                if ( head )
                    set_face_texture( head, face );
            }
        }

        if ( want_cloth )
            set_clothing( character );

        std::this_thread::sleep_for( std::chrono::milliseconds( 80 ) );
    }
}

}

auto start( ) -> void
{
    if ( g_alive.exchange( true ) )
        return;
    g_thread = std::thread( thread_main );
}

auto stop( ) -> void
{
    if ( !g_alive.exchange( false ) )
        return;
    if ( g_thread.joinable( ) )
        g_thread.join( );
}

}
