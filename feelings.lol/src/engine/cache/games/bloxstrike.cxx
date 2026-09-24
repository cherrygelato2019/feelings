#include <src/engine/cache/games/bloxstrike.hxx>
#include <src/engine/cache/games/common.hxx>

#include <cmath>
#include <unordered_set>

namespace games
{
namespace bloxstrike
{
namespace
{

auto sane_hp( float v ) -> bool
{
    return std::isfinite( v ) && v >= 0.f && v <= 10000.f;
}

auto try_bind_health_nv( cache::player& entry, const sdk::u_instance& root ) -> bool
{
    if ( !root )
        return false;

    static const char* health_names[] = {
        "Health", "HP", "HitPoints", "health", "HealthValue", "CurrentHealth", "hp"
    };
    static const char* max_health_names[] = {
        "MaxHealth", "MaxHP", "MaximumHealth", "maxHealth", "MaxHitPoints", "maxhp"
    };

    sdk::u_instance hv = detail::find_armor_ammo_value(
        root, health_names, ( int )( sizeof( health_names ) / sizeof( health_names[0] ) ) );
    if ( !hv )
    {
        hv = root.find_first_child( "Health" );
        if ( hv && !detail::is_number_value_class( hv.class_name( ) ) )
            hv = {};
    }
    if ( !hv )
        return false;

    const float h = detail::read_number_value( hv );
    float mh = 100.f;
    sdk::u_instance maxv = detail::find_armor_ammo_value(
        root, max_health_names, ( int )( sizeof( max_health_names ) / sizeof( max_health_names[0] ) ) );
    if ( !maxv )
        maxv = hv.find_first_child( "MaxHealth" );
    if ( maxv )
        mh = detail::read_number_value( maxv );
    if ( !sane_hp( h ) || !sane_hp( mh ) || mh <= 0.f )
        return false;

    entry.health_nv = hv.address( );
    entry.max_health_nv = maxv ? maxv.address( ) : 0;
    entry.health = h;
    entry.max_health = mh;
    return true;
}

auto bind_health( cache::player& entry, const sdk::u_model& character ) -> void
{
    if ( try_bind_health_nv( entry, character ) )
        return;
    if ( entry.humanoid && try_bind_health_nv( entry, sdk::u_instance( entry.humanoid ) ) )
        return;
    if ( entry.address && entry.address != entry.character
        && try_bind_health_nv( entry, sdk::u_instance( entry.address ) ) )
        return;

    if ( character )
    {
        static const char* nests[] = {
            "BodyEffects", "Info", "Stats", "Status", "Values", "CS", "Data",
            "PlayerData", "Configuration", "Combat", "PlayerStats"
        };
        for ( const char* nest : nests )
        {
            sdk::u_instance folder = character.find_first_child( nest );
            if ( folder && try_bind_health_nv( entry, folder ) )
                return;
        }
    }

    if ( entry.humanoid )
    {
        sdk::u_humanoid hum( entry.humanoid );
        const float h = hum.health( );
        const float mh = hum.max_health( );
        if ( detail::humanoid_health_real( h, mh ) )
        {
            entry.health = h;
            entry.max_health = mh;
            return;
        }
    }

    if ( entry.max_health <= 0.f )
    {
        entry.health = 100.f;
        entry.max_health = 100.f;
    }
}

auto try_bind_armor_nv( cache::player& entry, const sdk::u_instance& root ) -> bool
{
    if ( !root || entry.armor_nv )
        return false;

    static const char* armor_names[] = {
        "Armor", "Kevlar", "Helmet", "Vest", "Shield", "BodyArmor",
        "ArmorValue", "HelmetArmor", "HeavyArmor", "armor", "kevlar", "helmet",
        "Armour", "armour"
    };
    static const char* max_armor_names[] = {
        "MaxArmor", "MaxKevlar", "MaxHelmet", "MaxVest", "MaxShield",
        "MaxBodyArmor", "ArmorMax", "maxArmor", "MaxArmorValue", "MaxArmour"
    };

    sdk::u_instance armor = detail::find_armor_ammo_value(
        root, armor_names, ( int )( sizeof( armor_names ) / sizeof( armor_names[0] ) ) );
    if ( !armor )
        return false;

    const float a = detail::read_number_value( armor );
    if ( !sane_hp( a ) )
        return false;

    entry.armor_nv = armor.address( );
    entry.armor = a;
    sdk::u_instance maxa = detail::find_armor_ammo_value(
        root, max_armor_names, ( int )( sizeof( max_armor_names ) / sizeof( max_armor_names[0] ) ) );
    if ( maxa )
    {
        entry.max_armor_nv = maxa.address( );
        entry.max_armor = detail::read_number_value( maxa );
    }
    if ( entry.max_armor <= 0.f )
        entry.max_armor = ( std::max )( entry.armor, 100.f );
    return true;
}

auto bind_armor( cache::player& entry, const sdk::u_model& character ) -> void
{
    try_bind_armor_nv( entry, character );
    if ( !entry.armor_nv && entry.humanoid )
        try_bind_armor_nv( entry, sdk::u_instance( entry.humanoid ) );
    if ( !entry.armor_nv && entry.address )
        try_bind_armor_nv( entry, sdk::u_instance( entry.address ) );

    if ( !entry.armor_nv && character )
    {
        static const char* nests[] = {
            "BodyEffects", "Info", "Stats", "Status", "Values", "CS", "Data",
            "PlayerData", "Configuration", "Combat", "PlayerStats", "Armor", "Shield"
        };
        for ( const char* nest : nests )
        {
            sdk::u_instance folder = character.find_first_child( nest );
            if ( !folder )
                continue;
            if ( try_bind_armor_nv( entry, folder ) )
                break;
        }
    }

    if ( entry.armor_nv && entry.max_armor <= 0.f )
        entry.max_armor = 100.f;
}

auto enrich_entry( cache::player& entry ) -> void
{
    if ( !entry.character )
        return;
    sdk::u_model character( entry.character );
    if ( !character )
        return;

    if ( !entry.humanoid )
    {
        sdk::u_instance hum = character.find_first_child_of_class( "Humanoid" );
        if ( hum )
            entry.humanoid = hum.address( );
    }

    bind_health( entry, character );
    detail::resolve_armor_ammo( entry, character );
    if ( !entry.armor_nv )
        bind_armor( entry, character );
    if ( entry.max_armor <= 0.f && entry.armor_nv )
        entry.max_armor = 100.f;
}

}

auto cache_players( std::vector<cache::player>& out, const cache_ctx& ctx ) -> void
{
    for ( auto& entry : out )
        enrich_entry( entry );

    if ( !g->engine.workspace )
        return;

    auto owners = detail::build_char_owners( );
    std::unordered_set<uintptr_t> seen;
    seen.reserve( out.size( ) * 2 + 16 );
    for ( const auto& pe : out )
        if ( pe.character )
            seen.insert( pe.character );

    sdk::u_instance ws( g->engine.workspace.load( ) );
    sdk::u_instance chars_folder = ws.find_first_child( "Characters" );
    if ( !chars_folder )
        return;

    for ( const auto& team_folder : chars_folder.get_children( ) )
    {
        if ( !team_folder )
            continue;
        for ( const auto& model : team_folder.get_children( ) )
        {
            if ( !model || model.class_name( ) != "Model" )
                continue;
            if ( ctx.local_char && model.address( ) == ctx.local_char )
                continue;
            if ( seen.count( model.address( ) ) )
                continue;

            sdk::u_instance hrp = model.find_first_child( "HumanoidRootPart" );
            if ( !hrp )
                hrp = model.find_first_child( "Torso" );
            if ( !hrp )
                continue;

            cache::player entry{};
            entry.character = model.address( );
            entry.address = model.address( );
            entry.is_npc = false;

            auto owner = owners.find( model.address( ) );
            if ( owner != owners.end( ) )
            {
                entry.address = owner->second.address( );
                entry.name = owner->second.name( );
                entry.display_name = owner->second.display_name( );
                entry.team = owner->second.team( ).address( );
                entry.user_id = owner->second.user_id( );
                if ( ctx.local_addr && entry.address == ctx.local_addr )
                    continue;
            }
            else
            {
                entry.name = model.name( );
                entry.display_name = entry.name;
            }

            for ( const auto& child : model.get_children( ) )
            {
                const std::string cn = child.class_name( );
                if ( cn == "Humanoid" )
                {
                    entry.humanoid = child.address( );
                    continue;
                }
                if ( cn != "Part" && cn != "MeshPart" )
                    continue;
                detail::push_bone( entry, child, child.name( ) );
            }

            if ( entry.bones.empty( ) )
                continue;

            enrich_entry( entry );
            detail::finalize_entry( entry, ctx.local_pos );
            seen.insert( model.address( ) );
            out.push_back( std::move( entry ) );
        }
    }
}

auto is_teammate( uintptr_t character, uintptr_t local_char ) -> bool
{
    if ( !character || !local_char || !g->engine.workspace )
        return false;

    sdk::u_instance ws( g->engine.workspace.load( ) );
    sdk::u_instance chars_folder = ws.find_first_child( "Characters" );
    if ( !chars_folder )
        return false;

    auto find_folder_containing = [&]( uintptr_t target ) -> uintptr_t
    {
        for ( const auto& folder : chars_folder.get_children( ) )
        {
            for ( const auto& model : folder.get_children( ) )
                if ( model.address( ) == target )
                    return folder.address( );
        }
        return 0;
    };

    const uintptr_t local_folder = find_folder_containing( local_char );
    const uintptr_t target_folder = find_folder_containing( character );
    return local_folder && local_folder == target_folder;
}

}
}
