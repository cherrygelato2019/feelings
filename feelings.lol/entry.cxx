#include <include/global.cuh>
#include <src/cheat/config/config_io.hxx>
#include <src/cheat/overlay/fonts/fonts.cuh>

usermode_c* usermode = new usermode_c( );
global_t* g = new global_t( );
config_t config;
ImFont* esp_font = nullptr;
ImFont* esp_fonts[fonts::count] = {};

// < had to let ai take take of my sdk a lil so its not as clean as it could be but its still good 4 now >

auto entry( ) -> int
{
    g->logger.setup( "Feelings.lol(Roblox)" );
    g->logger.print( "config: defaults (load a slot from settings)" );

    g->logger.print( "waiting for roblox..." );
    while ( !FindWindowW( nullptr, L"Roblox" ) )
        Sleep( 500 );

    while ( !usermode->attach( ) )
        Sleep( 500 );

    g->logger.print( "attached ( pid %lu, base 0x%llx, size 0x%zx, %s )",
        usermode->pid( ),
        ( unsigned long long )usermode->base( ),
        usermode->image_size( ),
        usermode->version( ).empty( ) ? "version-unknown" : usermode->version( ).c_str( ) );

    if ( !usermode->version( ).empty( ) && usermode->version( ) != offsets::client_version )
    {
        g->logger.print( "offsets tagged %s — live is %s, dump again",
            offsets::client_version.c_str( ), usermode->version( ).c_str( ) );
    }

    char intern[16]{};
    if ( usermode->read_buf( usermode->base( ) + offsets::rva::intern_raycast, intern, 8 ) )
    {
        intern[8] = 0;
        g->logger.print( "intern Raycast @ +0x%llx = '%s'",
            ( unsigned long long )offsets::rva::intern_raycast, intern );
    }

    g->logger.print( "starting engine" );
    g->engine.init( );

    g->logger.print( "starting overlay" );
    g->overlay.run( );
    return 0;
}

auto main( ) -> int
{
    return entry( );
}
