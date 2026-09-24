#include <include/global.cuh>
#include <src/engine/cache/pf_cache.hxx>
#include <src/engine/cache/games/games.cuh>
#include <src/engine/cache/games/common.hxx>

#include <cmath>
#include <chrono>
#include <cfloat>
#include <algorithm>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

auto try_id_at( uintptr_t addr ) -> std::string
{
    if ( !addr || !usermode->is_valid( addr ) )
        return {};
    auto id = usermode->read_string( addr );
    if ( !id.empty( ) )
        return id;
    return usermode->read_string( addr + 0x10 );
}

auto read_mesh_id( const sdk::u_instance& part ) -> std::string
{
    const std::string cn = part.class_name( );
    if ( cn == "MeshPart" )
    {
        static constexpr uintptr_t k_off[] = {
            offsets::mesh_part::mesh_id,
        };
        for ( uintptr_t off : k_off )
        {
            auto id = try_id_at( part.address( ) + off );
            if ( !id.empty( ) )
                return id;
        }
    }

    if ( cn != "Part" )
        return {};

    const auto sm = part.find_first_child_of_class( "SpecialMesh" );
    if ( sm )
    {
        static constexpr uintptr_t k_off[] = {
            offsets::special_mesh::mesh_id,
        };
        for ( uintptr_t off : k_off )
        {
            auto id = try_id_at( sm.address( ) + off );
            if ( !id.empty( ) )
                return id;
        }
    }
    return {};
}

auto detect_rig_type( const sdk::u_humanoid& humanoid, const std::vector<cache::player::bone>& bones ) -> int
{

    for ( const auto& b : bones )
    {
        if ( b.name == "UpperTorso" || b.name == "LowerTorso"
            || b.name == "LeftUpperArm" || b.name == "RightUpperArm" )
            return 1;
        if ( b.name == "Torso" || b.name == "Left Arm" || b.name == "Right Arm" )
            return 0;
    }

    const int from_hum = humanoid ? humanoid.rig_type( ) : -1;
    if ( from_hum == 0 || from_hum == 1 )
        return from_hum;
    return -1;
}

auto push_bone( cache::player& entry, const sdk::u_instance& inst, const std::string& name ) -> void
{
    if ( !inst )
        return;

    sdk::u_base_part bp = sdk::as<sdk::u_base_part>( inst );
    if ( !bp.primitive( ) )
        return;

    cache::player::bone b{};
    b.name = name.empty( ) ? "Part" : name;
    b.part = bp.address( );
    b.primitive = bp.primitive( );
    b.size = bp.size( );
    b.position = bp.position( );
    b.rotation = bp.orientation( );
    b.velocity = bp.assembly_linear_velocity( );
    if ( config.esp.chams && config.esp.chams_kind == 0 )
        b.mesh_id = read_mesh_id( inst );
    entry.bones.push_back( std::move( b ) );
}

auto collect_character_parts( cache::player& entry, const sdk::u_model& character ) -> void
{
    entry.bones.clear( );
    entry.bones.reserve( 28 );
    // Always cache Acc:* handles like the old Feelings player cache (chams filter is draw-time).
    int acc_n = 0;
    auto walk = [&]( auto&& self, const sdk::u_instance& node, int depth ) -> void
    {
        for ( const auto& child : node.get_children( ) )
        {
            const std::string cn = child.class_name( );
            if ( cn == "Part" || cn == "MeshPart" || cn == "WedgePart" || cn == "CornerWedgePart"
                || cn == "TrussPart" || cn == "UnionOperation" )
            {
                push_bone( entry, child, child.name( ) );
            }
            else if ( depth == 0 && acc_n < 6 && ( cn == "Accessory" || cn == "Hat" || cn == "Accoutrement" ) )
            {
                const std::string acc_name = child.name( );
                ++acc_n;
                sdk::u_instance handle = child.find_first_child( "Handle" );
                if ( handle )
                    push_bone( entry, handle, "Acc:" + acc_name );
                else
                {
                    int nested_i = 0;
                    for ( const auto& nested : child.get_children( ) )
                    {
                        const std::string ncn = nested.class_name( );
                        if ( ncn != "Part" && ncn != "MeshPart" )
                            continue;
                        std::string key = "Acc:" + acc_name;
                        if ( nested_i++ > 0 )
                            key += "_" + nested.name( );
                        push_bone( entry, nested, key );
                    }
                }
            }
            else if ( depth < 2 && ( cn == "Folder" || cn == "Model" ) )
            {
                self( self, child, depth + 1 );
            }
        }
    };

    walk( walk, character, 0 );
}

auto fill_player_from_character( cache::player& entry, const sdk::u_model& character, const math::vector3_t& local_pos ) -> bool
{
    if ( !character )
        return false;

    entry.character = character.address( );

    sdk::u_humanoid humanoid = sdk::as<sdk::u_humanoid>( character.find_first_child_of_class( "Humanoid" ) );
    if ( humanoid )
    {
        entry.humanoid = humanoid.address( );
        entry.health = humanoid.health( );
        entry.max_health = humanoid.max_health( );

        const std::string hum_disp = humanoid.display_name( );

        if ( !hum_disp.empty( ) && entry.display_name.empty( ) )
            entry.display_name = hum_disp;
    }

    if ( entry.name.empty( ) )
    {
        const std::string model_name = character.name( );
        if ( !model_name.empty( ) && model_name != "Character" && model_name != "Model" )
            entry.name = model_name;
    }

    if ( entry.display_name.empty( ) && !entry.name.empty( ) )
        entry.display_name = entry.name;

    if ( config.esp.tool || config.esp.ammo )
    {
        sdk::u_instance equipped = character.find_first_child_of_class( "Tool" );
        if ( !equipped )
            equipped = character.find_first_child_of_class( "HopperBin" );
        entry.tool = equipped ? equipped.name( ) : std::string{};
    }

    collect_character_parts( entry, character );
    if ( entry.bones.empty( ) )
        return false;

    entry.root_part = 0;
    entry.head = 0;
    for ( const auto& b : entry.bones )
    {
        if ( b.name == "HumanoidRootPart" )
            entry.root_part = b.part;
        else if ( b.name == "Head" )
            entry.head = b.part;
    }
    // Only if character has no HumanoidRootPart (odd R6/custom) fall back.
    if ( !entry.root_part )
    {
        for ( const auto& b : entry.bones )
        {
            if ( b.name == "Torso" || b.name == "UpperTorso" || b.name == "Abdomen" )
            {
                entry.root_part = b.part;
                break;
            }
        }
    }
    if ( !entry.root_part && !entry.bones.empty( ) )
        entry.root_part = entry.bones.front( ).part;

    entry.rig_type = detect_rig_type( humanoid, entry.bones );

    games::detail::resolve_health( entry, character );

    if ( config.esp.armor || config.esp.ammo || config.esp.armor_text || config.esp.ammo_text )
        games::detail::resolve_armor_ammo( entry, character );

    // Studs from MY HumanoidRootPart (local_pos) to THEIR HumanoidRootPart.
    if ( local_pos.is_valid( ) )
    {
        math::vector3_t their_hrp = math::vector3_t::nan( );
        for ( const auto& b : entry.bones )
        {
            if ( b.name == "HumanoidRootPart" && b.position.is_valid( ) )
            {
                their_hrp = b.position;
                break;
            }
        }
        if ( !their_hrp.is_valid( ) )
        {
            for ( const auto& b : entry.bones )
            {
                if ( !b.position.is_valid( ) )
                    continue;
                if ( b.part == entry.root_part || b.name == "Torso" || b.name == "UpperTorso" )
                {
                    their_hrp = b.position;
                    break;
                }
            }
        }
        if ( their_hrp.is_valid( ) )
            entry.distance = ( their_hrp - local_pos ).length( );
    }

    games::detail::resolve_humanoid_state( entry );
    games::detail::normalize_player_labels( entry );

    entry.hearing = false;
    entry.hear_name.clear( );
    if ( config.esp.sound_esp )
    {
        float speed = 0.f;
        for ( const auto& b : entry.bones )
        {
            if ( b.name == "HumanoidRootPart" || b.name == "LowerTorso" || b.name == "Torso" )
            {
                const float xz = std::sqrt( b.velocity.x * b.velocity.x + b.velocity.z * b.velocity.z );
                speed = ( std::max )( speed, xz );
            }
        }
        if ( speed > 2.4f && entry.state_id != offsets::humanoid_state::freefall
            && entry.state_id != offsets::humanoid_state::jumping
            && entry.state_id != offsets::humanoid_state::dead )
        {
            entry.hearing = true;
            entry.hear_name = "footsteps";
        }
    }
    return true;
}

auto cache_standard_players( std::vector<cache::player>& out, const math::vector3_t& local_pos, uintptr_t local_addr ) -> void
{
    if ( !g->engine.players )
        return;

    sdk::u_players service( g->engine.players );
    for ( const auto& p : service.get_players( ) )
    {
        if ( !p || p.address( ) == local_addr )
            continue;

        cache::player entry{};
        entry.address = p.address( );
        entry.name = p.username( );
        entry.display_name = p.display_name( );
        entry.user_id = p.user_id( );
        entry.is_npc = false;
        if ( entry.display_name.empty( ) )
            entry.display_name = entry.name;

        sdk::u_instance team_inst = p.team( );
        entry.team = team_inst.address( );
        if ( entry.team )
            entry.team_name = team_inst.name( );

        if ( config.esp.platform )
            entry.platform = p.platform( );

        sdk::u_model character = p.character( );
        if ( !fill_player_from_character( entry, character, local_pos ) )
            continue;

        out.push_back( std::move( entry ) );
    }
}

auto name_has_npc_hint( const std::string& name ) -> bool
{
    if ( name.empty( ) )
        return false;
    char buf[128]{};
    const size_t n = ( std::min )( name.size( ), sizeof( buf ) - 1 );
    for ( size_t i = 0; i < n; ++i )
    {
        const char c = name[i];
        buf[i] = ( c >= 'A' && c <= 'Z' ) ? ( char )( c + 32 ) : c;
    }
    static const char* keys[] = {
        "npc", "mobs", "mob", "enem", "bot", "zombie", "unit", "entity",
        "monster", "creature", "dummy", "soldier", "bandit", "living", "alive", "ai"
    };
    for ( const char* k : keys )
        if ( std::strstr( buf, k ) )
            return true;
    return false;
}

auto name_skip_workspace( const std::string& name ) -> bool
{
    if ( name.empty( ) )
        return true;
    char buf[128]{};
    const size_t n = ( std::min )( name.size( ), sizeof( buf ) - 1 );
    for ( size_t i = 0; i < n; ++i )
    {
        const char c = name[i];
        buf[i] = ( c >= 'A' && c <= 'Z' ) ? ( char )( c + 32 ) : c;
    }
    static const char* skip[] = {
        "terrain", "camera", "baseplate", "spawnlocation", "spawns",
        "lighting", "sound", "folder", "map", "buildings", "loot", "loots",
        "debris", "effects", "vfx", "particles", "ignore", "holes"
    };
    for ( const char* k : skip )
        if ( std::strcmp( buf, k ) == 0 )
            return true;
    return false;
}

auto model_looks_like_npc( const sdk::u_instance& model ) -> bool
{
    if ( !model )
        return false;
    const std::string cn = model.class_name( );
    if ( cn != "Model" )
        return false;
    if ( model.find_first_child_of_class( "Humanoid" ) )
        return true;

    if ( model.find_first_child( "HumanoidRootPart" )
        && ( model.find_first_child( "Head" ) || model.find_first_child( "UpperTorso" ) || model.find_first_child( "Torso" ) ) )
        return true;
    return false;
}

auto try_push_npc(
    std::vector<cache::player>& out,
    std::unordered_set<uintptr_t>& seen,
    const sdk::u_instance& model,
    const math::vector3_t& local_pos,
    uintptr_t local_char ) -> void
{
    if ( !model || out.size( ) >= 96 )
        return;
    const uintptr_t addr = model.address( );
    if ( !addr || addr == local_char || seen.count( addr ) )
        return;
    if ( !model_looks_like_npc( model ) )
        return;

    cache::player entry{};
    entry.is_npc = true;
    entry.address = addr;

    sdk::u_model character( addr );
    if ( !fill_player_from_character( entry, character, local_pos ) )
        return;

    if ( entry.name.empty( ) )
        entry.name = model.name( );
    if ( entry.display_name.empty( ) )
        entry.display_name = entry.name;
    if ( entry.name.find( "[NPC]" ) != 0 )
        entry.name = "[NPC] " + entry.name;

    seen.insert( addr );
    out.push_back( std::move( entry ) );
}

auto cache_workspace_npcs( std::vector<cache::player>& out, const math::vector3_t& local_pos, uintptr_t local_char ) -> void
{
    if ( !g->engine.workspace )
        return;

    std::unordered_set<uintptr_t> seen;
    if ( local_char )
        seen.insert( local_char );
    for ( const auto& pe : out )
        if ( pe.character )
            seen.insert( pe.character );

    sdk::u_instance ws( g->engine.workspace.load( ) );
    if ( !ws )
        return;

    for ( const auto& child : ws.get_children( ) )
    {
        if ( !child )
            continue;

        const std::string cn = child.class_name( );
        const std::string nm = child.name( );

        if ( cn == "Model" )
        {
            try_push_npc( out, seen, child, local_pos, local_char );
            continue;
        }

        if ( cn != "Folder" && cn != "Model" && cn != "Configuration" )
            continue;
        if ( name_skip_workspace( nm ) )
            continue;

        const bool prefer = name_has_npc_hint( nm );

        int humanoid_hits = 0;
        if ( !prefer )
        {
            for ( const auto& nested : child.get_children( ) )
            {
                if ( model_looks_like_npc( nested ) && ++humanoid_hits >= 2 )
                    break;
            }
            if ( humanoid_hits < 2 )
                continue;
        }

        for ( const auto& nested : child.get_children( ) )
        {
            if ( !nested )
                continue;
            if ( nested.class_name( ) == "Model" )
            {
                try_push_npc( out, seen, nested, local_pos, local_char );
                continue;
            }

            if ( prefer && ( nested.class_name( ) == "Folder" || nested.class_name( ) == "Model" ) )
            {
                for ( const auto& deep : nested.get_children( ) )
                {
                    if ( deep.class_name( ) == "Model" )
                        try_push_npc( out, seen, deep, local_pos, local_char );
                }
            }
        }
    }
}

}

auto cache_c::get_players( ) -> void
{
    sdk::u_camera cam( g->engine.camera );
    const math::vector3_t cam_pos = cam ? cam.position( ) : math::vector3_t::nan( );

    uintptr_t local_addr = 0;
    uintptr_t local_char = 0;
    uintptr_t local_prim = 0;
    std::string local_name;
    // Distance is HRP→HRP — never seed from camera.
    math::vector3_t local_pos = math::vector3_t::nan( );

    if ( g->engine.players )
    {
        sdk::u_players service( g->engine.players );
        sdk::u_player lp = service.local_player( );
        if ( lp )
        {
            local_addr = lp.address( );
            local_name = lp.name( );
            if ( local_name.empty( ) )
                local_name = lp.display_name( );
            sdk::u_model lc = lp.character( );
            if ( lc )
            {
                local_char = lc.address( );
                sdk::u_base_part hrp = sdk::get_child_as<sdk::u_base_part>( lc, "HumanoidRootPart" );
                // Distance origin is YOUR HumanoidRootPart — do not fall back to camera/torso here.
                if ( hrp )
                {
                    local_prim = hrp.primitive( );
                    if ( !local_prim )
                        local_prim = usermode->read<uintptr_t>( hrp.address( ) + offsets::basepart::primitive );
                    if ( hrp.position( ).is_valid( ) )
                        local_pos = hrp.position( );
                }
            }
        }
    }

    local_root_prim.store( local_prim, std::memory_order_relaxed );

    int64_t place = 0;
    if ( g->engine.datamodel )
    {
        sdk::u_datamodel dm( g->engine.datamodel.load( ) );
        place = dm.place_id( );
    }
    else
    {
        static auto last_dm_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now( );
        if ( last_dm_log.time_since_epoch( ).count( ) == 0
            || ( now - last_dm_log ) >= std::chrono::seconds( 3 ) )
        {
            g->logger.print( "[cache] no datamodel yet — esp idle" );
            last_dm_log = now;
        }
        return;
    }

    const uintptr_t workspace = g->engine.workspace.load( );
    const games::game_id gid = games::detect( place, workspace );

    {
        static int64_t last_place = -1;
        static games::game_id last_gid = games::game_id::none;
        if ( place != last_place || gid != last_gid )
        {
            g->logger.print( "[cache] place_id=%lld game=%s", ( long long )place, games::name( gid ) );
            last_place = place;
            last_gid = gid;
            pf_cache::clear_pins( );
            games::clear_state( );
            games::set_current( gid, place );
        }
    }

    auto next = std::make_shared<std::vector<cache::player>>( );
    next->reserve( 32 );

    games::cache_ctx ctx{};
    ctx.local_pos = local_pos;
    ctx.cam_pos = cam_pos;
    ctx.local_addr = local_addr;
    ctx.local_char = local_char;
    ctx.local_prim = local_prim;
    ctx.local_name = local_name;

    if ( gid == games::game_id::phantom_forces )
    {
        if ( !local_prim && local_char )
            local_prim = pf_cache::resolve_local_prim( local_char );
        ctx.local_prim = local_prim;

        static auto last_pf_mode = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now( );
        if ( last_pf_mode.time_since_epoch( ).count( ) == 0
            || ( now - last_pf_mode ) >= std::chrono::seconds( 5 ) )
        {
            g->logger.print( "[pf] mode active place=%lld local='%s' local_char=0x%llx local_prim=0x%llx",
                ( long long )place, local_name.c_str( ),
                ( unsigned long long )local_char, ( unsigned long long )local_prim );
            last_pf_mode = now;
        }
        pf_cache::cache_players( *next, local_pos, local_prim, local_name );
    }
    else
    {
        const bool skip_default = games::replaces_default( gid );
        if ( !skip_default )
            cache_standard_players( *next, local_pos, local_addr );

        if ( gid != games::game_id::none )
            games::cache_players( gid, *next, ctx );

        if ( !skip_default )
            cache_workspace_npcs( *next, local_pos, local_char );

        if ( !skip_default )
            games::apply_teammates( gid, *next, local_char );
    }

    {
        std::lock_guard<std::mutex> lock( swap_mutex );
        players = next;
    }

    static size_t last_pc = SIZE_MAX;
    if ( next->size( ) != last_pc )
    {
        g->logger.print( "[cache] players=%zu place=%lld game=%s",
            next->size( ), ( long long )place, games::name( gid ) );
        last_pc = next->size( );
    }
}

auto wall_check_wanted( ) -> bool
{

    return config.esp.wall_check
        || config.esp.world_chams
        || ( config.aimbot.wall_check && !config.aimbot.wallbang )
        || ( config.silentaim.wall_check && !config.silentaim.wallbang )
        || ( config.triggerbot.wall_check && !config.triggerbot.wallbang );
}

auto is_world_part_class( const std::string& cn ) -> bool
{
    return cn == "Part"
        || cn == "MeshPart"
        || cn == "WedgePart"
        || cn == "CornerWedgePart"
        || cn == "TrussPart"
        || cn == "UnionOperation"
        || cn == "IntersectionOperation"
        || cn == "SpawnLocation"
        || cn == "Seat"
        || cn == "VehicleSeat";
}

auto should_skip_world_branch( const sdk::u_instance& inst ) -> bool
{
    if ( !inst )
        return true;
    const std::string cn = inst.class_name( );
    if ( cn == "Camera" || cn == "Terrain" || cn == "Players"
        || cn == "Chat" || cn == "Teams" || cn == "Lighting"
        || cn == "SoundService" || cn == "StarterGui" )
        return true;

    if ( cn == "Model" && inst.find_first_child_of_class( "Humanoid" ) )
        return true;

    const std::string nm = inst.name( );
    if ( nm == "Camera" || nm == "Ignore" || nm == "RayIgnore" || nm == "Viewmodel" || nm == "Arms" )
        return true;

    if ( ( cn == "Folder" || cn == "Model" || cn == "Configuration" ) && nm == "Players" )
        return true;

    return false;
}

auto cache_c::refresh_world( ) -> void
{
    if ( !wall_check_wanted( ) )
    {
        auto empty = std::make_shared<std::vector<cache::world_part>>( );
        std::lock_guard<std::mutex> lock( world_mutex );
        world = empty;
        return;
    }

    const uintptr_t ws_addr = g->engine.workspace.load( );
    if ( !ws_addr )
        return;

    sdk::u_camera cam( g->engine.camera.load( ) );
    const math::vector3_t cam_pos = cam ? cam.position( ) : math::vector3_t::nan( );

    std::unordered_set<uintptr_t> skip_models;
    skip_models.reserve( 64 );
    {
        const uintptr_t local_char = g->engine.client_model.load( );
        if ( local_char )
            skip_models.insert( local_char );

        auto players_snap = this->snapshot_players( );
        if ( players_snap )
        {
            for ( const auto& p : *players_snap )
            {
                if ( p.character )
                    skip_models.insert( p.character );
            }
        }
    }

    constexpr int k_max_depth = 6;
    constexpr size_t k_max_parts = 900;
    constexpr float k_max_dist = 220.f;
    constexpr float k_max_dist_sq = k_max_dist * k_max_dist;

    auto next = std::make_shared<std::vector<cache::world_part>>( );
    next->reserve( 512 );

    std::vector<std::pair<uintptr_t, int>> stack;
    stack.emplace_back( ws_addr, 0 );

    size_t visited = 0;
    constexpr size_t k_max_nodes = 5000;

    while ( !stack.empty( ) && next->size( ) < k_max_parts && visited < k_max_nodes )
    {
        const auto [ addr, depth ] = stack.back( );
        stack.pop_back( );
        ++visited;

        sdk::u_instance node( addr );
        if ( !node )
            continue;

        if ( addr != ws_addr && should_skip_world_branch( node ) )
            continue;
        if ( skip_models.count( addr ) )
            continue;

        const std::string cn = node.class_name( );
        if ( is_world_part_class( cn ) )
        {
            sdk::u_base_part bp( addr );
            const bool query = bp.can_query( );
            if ( query || bp.can_collide( ) )
            {
                const uintptr_t prim = bp.primitive( );
                if ( prim )
                {
                    const math::vector3_t pos = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
                    const math::vector3_t size = usermode->read<math::vector3_t>( prim + offsets::primitive::size );
                    if ( pos.is_valid( ) && size.is_valid( )
                        && size.x > 0.12f && size.y > 0.12f && size.z > 0.12f
                        && size.x < 800.f && size.y < 800.f && size.z < 800.f )
                    {
                        bool in_range = true;
                        if ( cam_pos.is_valid( ) )
                        {
                            const float dx = pos.x - cam_pos.x;
                            const float dy = pos.y - cam_pos.y;
                            const float dz = pos.z - cam_pos.z;
                            in_range = ( dx * dx + dy * dy + dz * dz ) <= k_max_dist_sq;
                        }

                        if ( in_range )
                        {
                            math::matrix3_t rot{};
                            usermode->read_buf( prim + offsets::primitive::rotation, &rot, sizeof( rot ) );

                            cache::world_part wp{};
                            wp.part = addr;
                            wp.primitive = prim;
                            wp.center = pos;
                            wp.rotation = rot;
                            wp.half = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
                            wp.obb = true;
                            wp.query = query;

                            wp.min = { FLT_MAX, FLT_MAX, FLT_MAX };
                            wp.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
                            for ( int ix = -1; ix <= 1; ix += 2 )
                            for ( int iy = -1; iy <= 1; iy += 2 )
                            for ( int iz = -1; iz <= 1; iz += 2 )
                            {
                                const math::vector3_t local{
                                    wp.half.x * ( float )ix,
                                    wp.half.y * ( float )iy,
                                    wp.half.z * ( float )iz
                                };
                                const math::vector3_t world = math::mat3_mul_vec3( rot, local ) + pos;
                                wp.min.x = ( std::min )( wp.min.x, world.x );
                                wp.min.y = ( std::min )( wp.min.y, world.y );
                                wp.min.z = ( std::min )( wp.min.z, world.z );
                                wp.max.x = ( std::max )( wp.max.x, world.x );
                                wp.max.y = ( std::max )( wp.max.y, world.y );
                                wp.max.z = ( std::max )( wp.max.z, world.z );
                            }

                            next->push_back( wp );
                        }
                    }
                }
            }
        }

        if ( depth >= k_max_depth )
            continue;

        if ( is_world_part_class( cn ) )
            continue;

        for ( const auto& child : node.get_children( ) )
        {
            if ( !child )
                continue;
            if ( skip_models.count( child.address( ) ) )
                continue;
            stack.emplace_back( child.address( ), depth + 1 );
        }
    }

    {
        std::lock_guard<std::mutex> lock( world_mutex );
        world = next;
    }

    static size_t last_n = SIZE_MAX;
    if ( next->size( ) != last_n )
    {
        g->logger.print( "[world] raycast cache parts=%zu nodes=%zu", next->size( ), visited );
        last_n = next->size( );
    }
}

auto cache_c::invalidate_character( uintptr_t character ) -> void
{
    if ( !character )
        return;
    {
        std::lock_guard<std::mutex> lock( stale_mutex );
        stale_characters.insert( character );
    }
    force_players_refresh.store( true, std::memory_order_relaxed );
}

auto cache_c::tick( ) -> void
{
    auto last_world = std::chrono::steady_clock::time_point{};
    while ( true )
    {
        this->get_players( );

        {
            std::lock_guard<std::mutex> lock( stale_mutex );
            stale_characters.clear( );
        }
        force_players_refresh.store( false, std::memory_order_relaxed );

        const auto now = std::chrono::steady_clock::now( );
        const bool want = wall_check_wanted( );
        if ( want && ( last_world.time_since_epoch( ).count( ) == 0
            || ( now - last_world ) >= std::chrono::milliseconds( 750 ) ) )
        {
            this->refresh_world( );
            last_world = std::chrono::steady_clock::now( );
        }
        else if ( !want )
        {
            auto world_snap = this->snapshot_world( );
            if ( world_snap && !world_snap->empty( ) )
            {
                this->refresh_world( );
                last_world = now;
            }
        }

        // Old Feelings player cache: rebuild entity graph ~750ms; transform thread owns poses.
        const int naps = config.settings.low_power ? 30 : 15;
        for ( int i = 0; i < naps; ++i )
        {
            if ( force_players_refresh.load( std::memory_order_relaxed ) )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
    }
}


namespace
{
auto refresh_ve_cache_unlocked( cache_c::ve_cache_t& out ) -> void
{
    out.viewmatrix = sdk::u_visualengine::view_matrix( );
    out.ve = sdk::u_visualengine::ve_dimensions( );
    out.draw = sdk::u_visualengine::dimensions( );
    out.offset = sdk::u_visualengine::client_offset( );
    out.sx = 1.f;
    out.sy = 1.f;
    // Simplest: one pixel space. Prefer VE dims (matches viewmatrix). If dead, use client.
    if ( out.ve.x < 16.f || out.ve.y < 16.f )
        out.ve = out.draw;
    // Only scale when sizes actually differ a lot (avoid micro sx jitter).
    if ( out.draw.x > 16.f && out.draw.y > 16.f
        && ( std::fabs( out.draw.x - out.ve.x ) > 2.f || std::fabs( out.draw.y - out.ve.y ) > 2.f ) )
    {
        out.sx = out.draw.x / out.ve.x;
        out.sy = out.draw.y / out.ve.y;
    }
    else
    {
        // Treat as same space — no fractional scale.
        out.draw = out.ve;
        out.sx = 1.f;
        out.sy = 1.f;
    }
    if ( sdk::u_visualengine::overlay_synced( ) )
        out.offset = { 0.f, 0.f };
    out.ready = out.ve.x > 1.f && out.ve.y > 1.f;
}
} // namespace
auto cache_c::transform( ) -> void
{
    while ( true )
    {
        std::shared_ptr<std::vector<cache::player>> current;
        {
            std::lock_guard<std::mutex> lock( swap_mutex );
            current = players;
        }

        if ( !current || current->empty( ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            continue;
        }

        // Studs = |enemy HumanoidRootPart - MY HumanoidRootPart|. Prim is cached.
        math::vector3_t local_root = math::vector3_t::nan( );
        {
            uintptr_t hrp_prim = local_root_prim.load( std::memory_order_relaxed );
            if ( !hrp_prim && g->engine.players )
            {
                sdk::u_players service( g->engine.players );
                sdk::u_player lp = service.local_player( );
                if ( lp )
                {
                    sdk::u_model lc = lp.character( );
                    if ( lc )
                    {
                        sdk::u_base_part hrp = sdk::get_child_as<sdk::u_base_part>( lc, "HumanoidRootPart" );
                        if ( hrp )
                        {
                            hrp_prim = usermode->read<uintptr_t>( hrp.address( ) + offsets::basepart::primitive );
                            if ( hrp_prim )
                                local_root_prim.store( hrp_prim, std::memory_order_relaxed );
                        }
                    }
                }
            }
            if ( hrp_prim )
            {
                const math::vector3_t pos = usermode->read<math::vector3_t>( hrp_prim + offsets::primitive::position );
                if ( pos.is_valid( )
                    && ( std::fabs( pos.x ) + std::fabs( pos.y ) + std::fabs( pos.z ) ) > 0.01f )
                    local_root = pos;
                else
                    local_root_prim.store( 0, std::memory_order_relaxed );
            }
        }

        cache_c::ve_cache_t ve_snap{};
        refresh_ve_cache_unlocked( ve_snap );
        {
            std::lock_guard<std::mutex> lock( ve_mutex );
            ve_cache = ve_snap;
        }

        auto next = std::make_shared<std::vector<cache::player>>( *current );

        static int tool_pass = 0;
        const bool check_tools = ( ++tool_pass & 7 ) == 0;

        for ( auto& p : *next )
        {
            for ( auto& b : p.bones )
            {
                if ( !b.part )
                    continue;

                // Old Feelings transform: keep cached primitive, one contiguous RPM.
                // rotation@0xb0 (36) position@0xd4/+36 (12) velocity@0xf0/+64 (12)
                if ( !b.primitive )
                {
                    b.primitive = usermode->read<uintptr_t>( b.part + offsets::basepart::primitive );
                    if ( !b.primitive )
                        continue;
                }

                alignas( 16 ) uint8_t xform[76]{};
                if ( !usermode->read_buf( b.primitive + offsets::primitive::rotation, xform, sizeof( xform ) ) )
                {
                    // Primitive may have moved — refresh pointer next tick.
                    b.primitive = 0;
                    continue;
                }

                std::memcpy( &b.rotation, xform + 0, sizeof( math::matrix3_t ) );
                std::memcpy( &b.position, xform + 36, sizeof( math::vector3_t ) );
                std::memcpy( &b.velocity, xform + 64, sizeof( math::vector3_t ) );
            }

            if ( p.health_nv )
            {
                p.health = games::detail::read_number_at( p.health_nv );
                if ( p.max_health_nv )
                    p.max_health = games::detail::read_number_at( p.max_health_nv );
                else if ( p.max_health <= 0.f )
                    p.max_health = ( std::max )( p.health, 100.f );
            }
            else if ( p.humanoid )
            {
                const float hh = usermode->read<float>( p.humanoid + offsets::humanoid::health );
                const float mh = usermode->read<float>( p.humanoid + offsets::humanoid::max_health );
                if ( games::detail::humanoid_health_real( hh, mh ) )
                {
                    p.health = hh;
                    p.max_health = mh;
                }
            }

            if ( p.armor_nv )
            {
                p.armor = games::detail::read_number_at( p.armor_nv );
                if ( p.max_armor_nv )
                    p.max_armor = games::detail::read_number_at( p.max_armor_nv );
                else if ( p.max_armor <= 0.f )
                    p.max_armor = ( std::max )( p.armor, 100.f );
            }
            if ( p.ammo_nv )
            {
                p.ammo = games::detail::read_number_at( p.ammo_nv );
                if ( p.max_ammo_nv )
                    p.max_ammo = games::detail::read_number_at( p.max_ammo_nv );
                else if ( p.max_ammo <= 0.f )
                    p.max_ammo = ( std::max )( p.ammo, 30.f );
            }

            if ( check_tools
                && ( config.esp.ammo || config.esp.ammo_text || config.esp.tool ) && p.character )
            {
                sdk::u_model character( p.character );
                sdk::u_instance equipped = character.find_first_child_of_class( "Tool" );
                if ( !equipped )
                    equipped = character.find_first_child_of_class( "HopperBin" );
                const std::string tool_name = equipped ? equipped.name( ) : std::string{};
                if ( tool_name != p.tool )
                {
                    p.tool = tool_name;
                    if ( config.esp.ammo || config.esp.ammo_text || config.esp.armor || config.esp.armor_text )
                        games::detail::resolve_armor_ammo( p, character );
                }
            }

            p.box_valid = false;

            // Distance from MY HumanoidRootPart → their HumanoidRootPart (studs).
            p.distance = 0.f;
            if ( local_root.is_valid( ) )
            {
                math::vector3_t their_hrp = math::vector3_t::nan( );
                for ( const auto& b : p.bones )
                {
                    if ( b.name == "HumanoidRootPart" && b.position.is_valid( ) )
                    {
                        their_hrp = b.position;
                        break;
                    }
                }
                // R6 / custom rigs without a literal HRP name — last resort only.
                if ( !their_hrp.is_valid( ) )
                {
                    for ( const auto& b : p.bones )
                    {
                        if ( !b.position.is_valid( ) )
                            continue;
                        if ( b.part == p.root_part
                            || b.name == "Torso" || b.name == "UpperTorso" || b.name == "LowerTorso" )
                        {
                            their_hrp = b.position;
                            break;
                        }
                    }
                }
                if ( their_hrp.is_valid( ) )
                {
                    const float dx = their_hrp.x - local_root.x;
                    const float dy = their_hrp.y - local_root.y;
                    const float dz = their_hrp.z - local_root.z;
                    p.distance = std::sqrt( dx * dx + dy * dy + dz * dz );
                }
            }

        }

        {
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter( &qpc );
            for ( auto& p : *next )
                p.pose_qpc = ( uint64_t )qpc.QuadPart;
        }

        {
            std::lock_guard<std::mutex> lock( swap_mutex );

            if ( players == current )
            {
                players = next;
                pose_ve = ve_snap;
            }
        }

        // Old Feelings transform cadence (~10ms).
        std::this_thread::sleep_for( std::chrono::milliseconds( config.settings.low_power ? 12 : 10 ) );
    }
}
