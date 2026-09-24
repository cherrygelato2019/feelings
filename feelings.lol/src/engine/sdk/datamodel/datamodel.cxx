#include <include/global.cuh>

#include <chrono>
#include <string>

namespace sdk
{

namespace
{
    auto class_is( uintptr_t inst, const char* want ) -> bool
    {
        if ( !inst || inst < 0x10000 || !usermode->is_valid( inst ) )
            return false;

        const uintptr_t desc = usermode->read<uintptr_t>( inst + offsets::instance::class_descriptor );
        if ( !desc || !usermode->is_valid( desc ) )
            return false;

        const uintptr_t class_ptr = usermode->read<uintptr_t>( desc + offsets::instance::class_name );
        if ( !class_ptr )
            return false;

        return usermode->read_string( class_ptr ) == want;
    }

    auto is_ingame_datamodel( uintptr_t candidate ) -> bool
    {
        if ( !class_is( candidate, "DataModel" ) )
            return false;

        const bool loaded = usermode->read<bool>( candidate + offsets::datamodel::loaded )
            || usermode->read<bool>( candidate + 0x630 )
            || usermode->read<bool>( candidate + 0x5e0 );
        ( void )loaded;

        static constexpr uintptr_t k_place_offs[] = {
            offsets::datamodel::place_id, 0x188, 0x198, 0x1a0, 0x380
        };
        int64_t place = 0;
        for ( uintptr_t off : k_place_offs )
        {
            place = usermode->read<int64_t>( candidate + off );
            if ( place != 0 )
                break;
        }
        if ( place == 0 )
            return false;

        static constexpr uintptr_t k_ws_offs[] = {
            offsets::datamodel::workspace, 0x150, 0x160, 0x178, 0x180
        };
        for ( uintptr_t off : k_ws_offs )
        {
            const uintptr_t ws = usermode->read<uintptr_t>( candidate + off );
            if ( class_is( ws, "Workspace" ) )
                return true;
        }

        sdk::u_instance dm( candidate );
        return !dm.get_children( ).empty( );
    }

    auto from_fake( uintptr_t fake ) -> uintptr_t
    {
        if ( !fake || fake < 0x10000 || !usermode->is_valid( fake ) )
            return 0;

        const uintptr_t candidates[] = {
            offsets::fake_datamodel::real_datamodel,
            offsets::render_job::real_datamodel,
            0x1c0,
            0x1d0,
            0x1d8,
            0x1e0,
            0x1e8,
            0x200,
            0x208,
        };

        for ( uintptr_t off : candidates )
        {
            const uintptr_t dm = usermode->read<uintptr_t>( fake + off );
            if ( is_ingame_datamodel( dm ) )
                return dm;
        }
        return 0;
    }

    auto from_render_jobs( uintptr_t sched ) -> uintptr_t
    {
        if ( !sched || !usermode->is_valid( sched ) )
            return 0;

        const uintptr_t start = usermode->read<uintptr_t>( sched + offsets::task_scheduler::job_start );
        const uintptr_t end = usermode->read<uintptr_t>( sched + offsets::task_scheduler::job_end );
        if ( !start || !end || end <= start || ( end - start ) > 0x8000 )
            return 0;

        for ( uintptr_t it = start; it + 8 <= end; it += offsets::task_scheduler::job_stride )
        {
            const uintptr_t job = usermode->read<uintptr_t>( it );
            if ( !job || !usermode->is_valid( job ) )
                continue;

            const std::string jn = usermode->read_string( job + offsets::task_scheduler::job_name );

            if ( jn.find( "Render" ) == std::string::npos )
                continue;

            uintptr_t dm = usermode->read<uintptr_t>( job + offsets::render_job::real_datamodel );
            if ( is_ingame_datamodel( dm ) )
                return dm;

            dm = from_fake( usermode->read<uintptr_t>( job + offsets::render_job::fake_datamodel ) );
            if ( dm )
                return dm;
        }
        return 0;
    }

    auto from_visual_engine( uintptr_t ve ) -> uintptr_t
    {
        if ( !ve || !usermode->is_valid( ve ) )
            return 0;

        return from_fake( usermode->read<uintptr_t>( ve + offsets::visual_engine::fake_datamodel ) );
    }
}

auto u_datamodel::resolve( ) -> u_datamodel
{
    static auto last_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now( );
    const bool should_log = last_log.time_since_epoch( ).count( ) == 0
        || ( now - last_log ) >= std::chrono::seconds( 3 );

    const uintptr_t base = usermode->base( );
    if ( !base )
        return u_datamodel( );

    const uintptr_t sched = usermode->read<uintptr_t>( base + offsets::task_scheduler::pointer );
    const uintptr_t ve = usermode->read<uintptr_t>( base + offsets::visual_engine::pointer );
    const uintptr_t fake_ptr = usermode->read<uintptr_t>( base + offsets::fake_datamodel::pointer );

    if ( uintptr_t dm = from_render_jobs( sched ) )
    {
        if ( should_log )
        {
            g->logger.print( "resolve: ok via RenderJob dm=0x%llx place=%lld",
                ( unsigned long long )dm,
                ( long long )usermode->read<int64_t>( dm + offsets::datamodel::place_id ) );
            last_log = now;
        }
        return u_datamodel( dm );
    }

    if ( uintptr_t dm = from_visual_engine( ve ) )
    {
        if ( should_log )
        {
            g->logger.print( "resolve: ok via VisualEngine dm=0x%llx place=%lld",
                ( unsigned long long )dm,
                ( long long )usermode->read<int64_t>( dm + offsets::datamodel::place_id ) );
            last_log = now;
        }
        return u_datamodel( dm );
    }

    if ( uintptr_t dm = from_fake( fake_ptr ) )
    {
        if ( should_log )
        {
            g->logger.print( "resolve: ok via FakeDataModel dm=0x%llx place=%lld",
                ( unsigned long long )dm,
                ( long long )usermode->read<int64_t>( dm + offsets::datamodel::place_id ) );
            last_log = now;
        }
        return u_datamodel( dm );
    }

    if ( should_log )
    {
        g->logger.print( "resolve: fail fake=0x%llx ve=0x%llx sched=0x%llx",
            ( unsigned long long )fake_ptr,
            ( unsigned long long )ve,
            ( unsigned long long )sched );
        last_log = now;
    }

    return u_datamodel( );
}

auto u_datamodel::workspace( ) const -> u_workspace
{
    if ( !m_address )
        return u_workspace( );

    static constexpr uintptr_t k_ws_offs[] = {
        offsets::datamodel::workspace, 0x150, 0x160, 0x178, 0x180
    };
    for ( uintptr_t off : k_ws_offs )
    {
        const uintptr_t ws = usermode->read<uintptr_t>( m_address + off );
        if ( !ws || !usermode->is_valid( ws ) )
            continue;
        sdk::u_instance inst( ws );
        if ( inst.class_name( ) == "Workspace" )
            return u_workspace( ws );
    }

    sdk::u_instance by_class = this->find_first_child_of_class( "Workspace" );
    if ( by_class )
        return u_workspace( by_class.address( ) );
    return u_workspace( );
}

auto u_datamodel::players( ) const -> u_players
{
    sdk::u_instance by_class = this->find_first_child_of_class( "Players" );
    if ( by_class )
        return u_players( by_class.address( ) );
    return get_child_as<u_players>( *this, "Players" );
}

auto u_datamodel::lighting( ) const -> u_lighting
{
    u_instance by_class = this->find_first_child_of_class( "Lighting" );
    if ( by_class )
        return as<u_lighting>( by_class );
    return as<u_lighting>( this->find_first_child( "Lighting" ) );
}

auto u_datamodel::job_id( ) const -> std::string
{
    if ( !m_address )
        return { };
    return usermode->read_string( m_address + offsets::datamodel::job_id );
}

auto u_datamodel::place_id( ) const -> int64_t
{
    if ( !m_address )
        return 0;
    static constexpr uintptr_t k_place_offs[] = {
        offsets::datamodel::place_id, 0x188, 0x198, 0x1a0, 0x380
    };
    for ( uintptr_t off : k_place_offs )
    {
        const int64_t place = usermode->read<int64_t>( m_address + off );
        if ( place != 0 )
            return place;
    }
    return 0;
}

auto u_datamodel::loaded( ) const -> bool
{
    if ( !m_address )
        return false;
    return usermode->read<bool>( m_address + offsets::datamodel::loaded );
}

}
