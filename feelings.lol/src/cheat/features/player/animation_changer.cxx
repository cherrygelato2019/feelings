#include <include/global.cuh>

#include <src/cheat/features/player/animation_changer.cuh>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace animation_changer
{
namespace
{

std::atomic<bool> g_alive{ false };
std::thread g_thread;

auto set_animation_id( const sdk::u_instance& anim, const char* url ) -> void
{
    if ( !anim || !url || !url[0] || !usermode->can_rpm( ) )
        return;

    const uintptr_t base = anim.address( );
    const std::string cn = anim.class_name( );
    if ( cn == "StringValue" )
    {
        usermode->write_string( base + offsets::misc::value, url );
        return;
    }

    usermode->write_string( base + offsets::animation::animation_id, url );
}

auto find_anim( const sdk::u_instance& animate, const char* folder, const char* prefer ) -> sdk::u_instance
{
    if ( !animate || !folder )
        return {};

    sdk::u_instance node = animate.find_first_child( folder );
    if ( !node )
        return {};

    if ( prefer && prefer[0] )
    {
        sdk::u_instance named = node.find_first_child( prefer );
        if ( named )
            return named;
    }

    for ( const auto& ch : node.get_children( ) )
    {
        const std::string cn = ch.class_name( );
        if ( cn == "Animation" || cn == "StringValue" )
            return ch;
    }

    const std::string cn = node.class_name( );
    if ( cn == "Animation" || cn == "StringValue" )
        return node;
    return {};
}

auto local_animate( ) -> sdk::u_instance
{
    if ( !g->engine.players )
        return {};

    sdk::u_players service( g->engine.players.load( ) );
    sdk::u_player lp = service.local_player( );
    if ( !lp )
        return {};

    sdk::u_model character = lp.character( );
    if ( !character )
        return {};

    return character.find_first_child( "Animate" );
}

struct anim_pack_t
{
    const char* idle1;
    const char* idle2;
    const char* run;
    const char* walk;
    const char* jump;
    const char* fall;
    const char* climb;
    const char* swim;
};

constexpr anim_pack_t k_packs[] = {
    {
        "http://www.roblox.com/asset/?id=10921344533",
        "http://www.roblox.com/asset/?id=10921345304",
        "http://www.roblox.com/asset/?id=616163682",
        "http://www.roblox.com/asset/?id=616168032",
        "http://www.roblox.com/asset/?id=616161997",
        "http://www.roblox.com/asset/?id=616157476",
        "http://www.roblox.com/asset/?id=616156119",
        "http://www.roblox.com/asset/?id=616165109",
    },
    {
        "http://www.roblox.com/asset/?id=656117400",
        "http://www.roblox.com/asset/?id=656118341",
        "http://www.roblox.com/asset/?id=656118852",
        "http://www.roblox.com/asset/?id=656121766",
        "http://www.roblox.com/asset/?id=656117878",
        "http://www.roblox.com/asset/?id=656115606",
        "http://www.roblox.com/asset/?id=656114359",
        "http://www.roblox.com/asset/?id=656119721",
    },
    {
        "http://www.roblox.com/asset/?id=616136790",
        "http://www.roblox.com/asset/?id=616138447",
        "http://www.roblox.com/asset/?id=616140816",
        "http://www.roblox.com/asset/?id=616146177",
        "http://www.roblox.com/asset/?id=616139451",
        "http://www.roblox.com/asset/?id=616134815",
        "http://www.roblox.com/asset/?id=616133594",
        "http://www.roblox.com/asset/?id=616143378",
    },
    {
        "http://www.roblox.com/asset/?id=616158929",
        "http://www.roblox.com/asset/?id=616160636",
        "http://www.roblox.com/asset/?id=616163682",
        "http://www.roblox.com/asset/?id=616168032",
        "http://www.roblox.com/asset/?id=616161997",
        "http://www.roblox.com/asset/?id=616157476",
        "http://www.roblox.com/asset/?id=616156119",
        "http://www.roblox.com/asset/?id=616165109",
    },
    {
        "http://www.roblox.com/asset/?id=616006778",
        "http://www.roblox.com/asset/?id=616008087",
        "http://www.roblox.com/asset/?id=616010382",
        "http://www.roblox.com/asset/?id=616013216",
        "http://www.roblox.com/asset/?id=616008936",
        "http://www.roblox.com/asset/?id=616005863",
        "http://www.roblox.com/asset/?id=616003713",
        "http://www.roblox.com/asset/?id=616011509",
    },
    {
        "http://www.roblox.com/asset/?id=616136790",
        "http://www.roblox.com/asset/?id=616138447",
        "http://www.roblox.com/asset/?id=616140816",
        "http://www.roblox.com/asset/?id=616146177",
        "http://www.roblox.com/asset/?id=616139451",
        "http://www.roblox.com/asset/?id=616134815",
        "http://www.roblox.com/asset/?id=616133594",
        "http://www.roblox.com/asset/?id=616143378",
    },
    {
        "http://www.roblox.com/asset/?id=742637544",
        "http://www.roblox.com/asset/?id=742638445",
        "http://www.roblox.com/asset/?id=742638842",
        "http://www.roblox.com/asset/?id=742640026",
        "http://www.roblox.com/asset/?id=742637942",
        "http://www.roblox.com/asset/?id=742637151",
        "http://www.roblox.com/asset/?id=742636889",
        "http://www.roblox.com/asset/?id=742639220",
    },
    {
        "http://www.roblox.com/asset/?id=616111295",
        "http://www.roblox.com/asset/?id=616113536",
        "http://www.roblox.com/asset/?id=616117076",
        "http://www.roblox.com/asset/?id=616122287",
        "http://www.roblox.com/asset/?id=616115533",
        "http://www.roblox.com/asset/?id=616118211",
        "http://www.roblox.com/asset/?id=616120861",
        "http://www.roblox.com/asset/?id=616119360",
    },
    {
        "http://www.roblox.com/asset/?id=845397899",
        "http://www.roblox.com/asset/?id=845400520",
        "http://www.roblox.com/asset/?id=845386501",
        "http://www.roblox.com/asset/?id=845403856",
        "http://www.roblox.com/asset/?id=845398858",
        "http://www.roblox.com/asset/?id=845396048",
        "http://www.roblox.com/asset/?id=845392038",
        "http://www.roblox.com/asset/?id=845401742",
    },
    {
        "http://www.roblox.com/asset/?id=782841498",
        "http://www.roblox.com/asset/?id=782845736",
        "http://www.roblox.com/asset/?id=782842708",
        "http://www.roblox.com/asset/?id=782843345",
        "http://www.roblox.com/asset/?id=782847020",
        "http://www.roblox.com/asset/?id=782846423",
        "http://www.roblox.com/asset/?id=782843869",
        "http://www.roblox.com/asset/?id=782845186",
    },
    {
        "http://www.roblox.com/asset/?id=5319828216",
        "http://www.roblox.com/asset/?id=5319831086",
        "http://www.roblox.com/asset/?id=5319844329",
        "http://www.roblox.com/asset/?id=5319847204",
        "http://www.roblox.com/asset/?id=5319841935",
        "http://www.roblox.com/asset/?id=5319839762",
        "http://www.roblox.com/asset/?id=5319816685",
        "http://www.roblox.com/asset/?id=5319850266",
    },
};

constexpr const char* k_emotes[] = {
    "http://www.roblox.com/asset/?id=6862001787",
    "http://www.roblox.com/asset/?id=18526288497",
    "http://www.roblox.com/asset/?id=15693621070",
    "http://www.roblox.com/asset/?id=112084042063926",
    "http://www.roblox.com/asset/?id=131895014808795",
    "http://www.roblox.com/asset/?id=104151027065011",
    "http://www.roblox.com/asset/?id=101944868952743",
    "http://www.roblox.com/asset/?id=124982597491660",
    "http://www.roblox.com/asset/?id=113006894305390",
    "http://www.roblox.com/asset/?id=77840765435893",
    "http://www.roblox.com/asset/?id=102969907406039",
};

auto apply_pack( const sdk::u_instance& animate, const anim_pack_t& pack ) -> void
{
    set_animation_id( find_anim( animate, "idle", "Animation1" ), pack.idle1 );
    set_animation_id( find_anim( animate, "idle", "Animation2" ), pack.idle2 );
    set_animation_id( find_anim( animate, "run", "RunAnim" ), pack.run );
    set_animation_id( find_anim( animate, "walk", "WalkAnim" ), pack.walk );
    set_animation_id( find_anim( animate, "jump", "JumpAnim" ), pack.jump );
    set_animation_id( find_anim( animate, "fall", "FallAnim" ), pack.fall );
    set_animation_id( find_anim( animate, "climb", "ClimbAnim" ), pack.climb );
    set_animation_id( find_anim( animate, "swim", "Swim" ), pack.swim );
}

auto apply_emote( const sdk::u_instance& animate, const char* url ) -> void
{
    set_animation_id( find_anim( animate, "idle", "Animation1" ), url );
    set_animation_id( find_anim( animate, "idle", "Animation2" ), url );
    set_animation_id( find_anim( animate, "run", "RunAnim" ), url );
    set_animation_id( find_anim( animate, "walk", "WalkAnim" ), url );
    set_animation_id( find_anim( animate, "jump", "JumpAnim" ), url );
    set_animation_id( find_anim( animate, "fall", "FallAnim" ), url );
}

auto thread_main( ) -> void
{
    while ( g_alive.load( std::memory_order_relaxed ) )
    {
        const bool want_anim = config.player.animation_changer;
        const bool want_emote = config.player.emote_changer;
        if ( !want_anim && !want_emote )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            continue;
        }

        if ( !usermode->can_rpm( ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            continue;
        }

        sdk::u_instance animate = local_animate( );
        if ( !animate )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            continue;
        }

        if ( want_emote )
        {
            const int id = config.player.emote_preset;
            if ( id >= 0 && id < ( int )( sizeof( k_emotes ) / sizeof( k_emotes[0] ) ) )
                apply_emote( animate, k_emotes[id] );
        }
        else if ( want_anim )
        {
            const int id = config.player.animation_preset;
            if ( id >= 0 && id < ( int )( sizeof( k_packs ) / sizeof( k_packs[0] ) ) )
                apply_pack( animate, k_packs[id] );
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
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
