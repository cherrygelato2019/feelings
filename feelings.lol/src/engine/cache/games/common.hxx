#pragma once

#include <include/global.cuh>
#include <src/engine/cache/cache.cuh>
#include <src/engine/cache/draw.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace games
{
namespace detail
{

inline auto read_number_value( const sdk::u_instance& nv ) -> float
{
    if ( !nv )
        return 0.f;
    const std::string cn = nv.class_name( );
    const uintptr_t addr = nv.address( );

    // Double/IntConstrainedValue: Value is at +0xC8 (MaxValue sits at misc::value / 0xB8).
    constexpr uintptr_t k_constrained_value = 0xC8;

    if ( cn == "IntConstrainedValue" )
        return static_cast<float>( usermode->read<int64_t>( addr + k_constrained_value ) );
    if ( cn == "DoubleConstrainedValue" )
        return static_cast<float>( usermode->read<double>( addr + k_constrained_value ) );
    if ( cn == "IntValue" )
        return static_cast<float>( usermode->read<int64_t>( addr + offsets::misc::value ) );

    return static_cast<float>( usermode->read<double>( addr + offsets::misc::value ) );
}

inline auto read_number_at( uintptr_t addr ) -> float
{
    if ( !addr )
        return 0.f;
    return read_number_value( sdk::u_instance( addr ) );
}

inline auto is_number_value_class( const std::string& cn ) -> bool
{
    return cn == "NumberValue" || cn == "IntValue"
        || cn == "DoubleConstrainedValue" || cn == "IntConstrainedValue";
}

inline auto name_eq_i( const std::string& a, const char* b ) -> bool
{
    if ( !b || a.empty( ) )
        return false;
    size_t i = 0;
    for ( ; b[i] && i < a.size( ); ++i )
    {
        char ca = a[i];
        char cb = b[i];
        if ( ca >= 'A' && ca <= 'Z' ) ca = ( char )( ca + 32 );
        if ( cb >= 'A' && cb <= 'Z' ) cb = ( char )( cb + 32 );
        if ( ca != cb )
            return false;
    }
    return b[i] == 0 && i == a.size( );
}

inline auto name_contains_i( const std::string& a, const char* needle ) -> bool
{
    if ( !needle || !*needle || a.empty( ) )
        return false;
    const size_t nlen = std::strlen( needle );
    if ( a.size( ) < nlen )
        return false;
    for ( size_t i = 0; i + nlen <= a.size( ); ++i )
    {
        size_t j = 0;
        for ( ; j < nlen; ++j )
        {
            char ca = a[i + j];
            char cb = needle[j];
            if ( ca >= 'A' && ca <= 'Z' ) ca = ( char )( ca + 32 );
            if ( cb >= 'A' && cb <= 'Z' ) cb = ( char )( cb + 32 );
            if ( ca != cb )
                break;
        }
        if ( j == nlen )
            return true;
    }
    return false;
}

inline auto find_named_value( const sdk::u_instance& root, const char* const* names, int count ) -> sdk::u_instance
{
    if ( !root || !names || count <= 0 )
        return {};
    for ( const auto& child : root.get_children( ) )
    {
        if ( !child )
            continue;
        if ( !is_number_value_class( child.class_name( ) ) )
            continue;
        for ( int i = 0; i < count; ++i )
            if ( name_eq_i( child.name( ), names[i] ) )
                return child;
    }
    return {};
}


inline auto find_named_value_deep( const sdk::u_instance& root, const char* const* names, int count, int max_depth = 5 ) -> sdk::u_instance
{
    if ( !root || !names || count <= 0 || max_depth < 0 )
        return {};

    struct frame_t { uintptr_t addr; int depth; };
    thread_local std::vector<frame_t> queue;
    queue.clear( );
    queue.push_back( { root.address( ), 0 } );

    for ( size_t qi = 0; qi < queue.size( ); ++qi )
    {
        const auto [addr, depth] = queue[qi];
        if ( depth > max_depth )
            continue;

        sdk::u_instance node( addr );
        for ( const auto& child : node.get_children( ) )
        {
            if ( !child.address( ) )
                continue;
            const std::string cn = child.class_name( );
            if ( is_number_value_class( cn ) )
            {
                for ( int i = 0; i < count; ++i )
                    if ( name_eq_i( child.name( ), names[i] ) )
                        return child;
            }
            else if ( depth < max_depth )
            {

                if ( cn == "Workspace" || cn == "Players" || cn == "Terrain" )
                    continue;
                queue.push_back( { child.address( ), depth + 1 } );
            }
            if ( queue.size( ) > 2000 )
                break;
        }
        if ( queue.size( ) > 2000 )
            break;
    }
    return {};
}

inline auto find_armor_ammo_value( const sdk::u_instance& root, const char* const* names, int count ) -> sdk::u_instance
{
    if ( !root )
        return {};

    static const char* nests[] = {
        "Stats", "Values", "Info", "Data", "Status", "Configuration",
        "Settings", "PlayerData", "Attributes", "Gun", "Weapon", "Ammo", "Armor", "Shield",
        "Health", "HP", "Vitals", "Combat", "BodyEffects", "CS", "PlayerStats",
        "GameStats", "Main", "MainStats", "Properties", "Information", "leaderstats"
    };
    for ( const char* nest : nests )
    {
        sdk::u_instance folder = root.find_first_child( nest );
        if ( !folder )
            continue;
        sdk::u_instance hit = find_named_value( folder, names, count );
        if ( hit )
            return hit;
        hit = find_named_value_deep( folder, names, count, 3 );
        if ( hit )
            return hit;
    }
    sdk::u_instance hit = find_named_value( root, names, count );
    if ( hit )
        return hit;
    return find_named_value_deep( root, names, count, 5 );
}

inline auto humanoid_health_real( float h, float mh ) -> bool
{
    if ( !std::isfinite( h ) || !std::isfinite( mh ) )
        return false;
    if ( mh < 2.f || mh > 100000.f || h < 0.f || h > 100000.f )
        return false;
    // Custom rigs leave a dummy Humanoid at 0/100.
    if ( h <= 0.05f )
        return false;
    return h <= mh * 1.2f;
}

inline auto resolve_health( cache::player& entry, const sdk::u_instance& character ) -> void
{
    static const char* health_names[] = {
        "Health", "HP", "HitPoints", "health", "HealthValue", "CurrentHealth",
        "hp", "CurrentHP", "HealthNum", "PlayerHealth", "CharHealth",
        "Life", "Lives", "Blood", "Vitality", "HPValue", "Hitpoints",
        "HealthPoints", "CurrentHitPoints", "EntityHealth", "TorsoHealth"
    };
    static const char* max_health_names[] = {
        "MaxHealth", "MaxHP", "MaximumHealth", "maxHealth", "MaxHitPoints",
        "maxhp", "MaxLife", "HealthMax", "HPMax", "MaxHealthValue",
        "MaxHealthPoints", "MaximumHP", "MaxHitpoints"
    };

    auto try_nv = [&]( const sdk::u_instance& root ) -> bool
    {
        if ( !root )
            return false;

        static const char* health_folders[] = { "Health", "HP", "Vitals", "HealthFolder" };
        static const char* folder_inner[] = { "Health", "HP", "Value", "Current", "CurrentHealth", "Amount" };
        for ( const char* folder : health_folders )
        {
            sdk::u_instance nest = root.find_first_child( folder );
            if ( !nest )
                continue;
            if ( is_number_value_class( nest.class_name( ) ) )
            {
                const float h = read_number_value( nest );
                if ( std::isfinite( h ) && h >= 0.f && h <= 100000.f )
                {
                    sdk::u_instance mv = find_named_value(
                        nest, max_health_names, ( int )( sizeof( max_health_names ) / sizeof( max_health_names[0] ) ) );
                    if ( !mv )
                        mv = find_named_value(
                            root, max_health_names,
                            ( int )( sizeof( max_health_names ) / sizeof( max_health_names[0] ) ) );
                    float mh = mv ? read_number_value( mv ) : 0.f;
                    if ( !std::isfinite( mh ) || mh <= 0.f )
                        mh = ( std::max )( h, 100.f );
                    entry.health_nv = nest.address( );
                    entry.max_health_nv = mv ? mv.address( ) : 0;
                    entry.health = h;
                    entry.max_health = mh;
                    return true;
                }
            }
            sdk::u_instance inner = find_named_value(
                nest, folder_inner, ( int )( sizeof( folder_inner ) / sizeof( folder_inner[0] ) ) );
            if ( inner )
            {
                const float h = read_number_value( inner );
                if ( std::isfinite( h ) && h >= 0.f && h <= 100000.f )
                {
                    sdk::u_instance mv = find_named_value(
                        nest, max_health_names, ( int )( sizeof( max_health_names ) / sizeof( max_health_names[0] ) ) );
                    float mh = mv ? read_number_value( mv ) : 0.f;
                    if ( !std::isfinite( mh ) || mh <= 0.f )
                        mh = ( std::max )( h, 100.f );
                    entry.health_nv = inner.address( );
                    entry.max_health_nv = mv ? mv.address( ) : 0;
                    entry.health = h;
                    entry.max_health = mh;
                    return true;
                }
            }
        }

        sdk::u_instance hv = find_armor_ammo_value(
            root, health_names, ( int )( sizeof( health_names ) / sizeof( health_names[0] ) ) );
        if ( !hv )
            return false;
        const float h = read_number_value( hv );
        if ( !std::isfinite( h ) || h < 0.f || h > 100000.f )
            return false;
        sdk::u_instance mv = find_armor_ammo_value(
            root, max_health_names, ( int )( sizeof( max_health_names ) / sizeof( max_health_names[0] ) ) );
        float mh = mv ? read_number_value( mv ) : 0.f;
        if ( !std::isfinite( mh ) || mh <= 0.f )
            mh = ( std::max )( h, 100.f );
        entry.health_nv = hv.address( );
        entry.max_health_nv = mv ? mv.address( ) : 0;
        entry.health = h;
        entry.max_health = mh;
        return true;
    };

    if ( entry.health_nv )
    {
        entry.health = read_number_at( entry.health_nv );
        if ( entry.max_health_nv )
            entry.max_health = read_number_at( entry.max_health_nv );
        else if ( entry.max_health <= 0.f )
            entry.max_health = ( std::max )( entry.health, 100.f );
        return;
    }

    // Custom rigs keep a dummy Humanoid at 0/100 or 100/100. Prefer NumberValue HP first.
    if ( try_nv( character ) )
        return;
    if ( entry.humanoid && try_nv( sdk::u_instance( entry.humanoid ) ) )
        return;
    if ( entry.address && entry.address != character.address( )
        && try_nv( sdk::u_instance( entry.address ) ) )
        return;
    if ( entry.address )
    {
        sdk::u_instance player( entry.address );
        sdk::u_instance ls = player.find_first_child( "leaderstats" );
        if ( ls && try_nv( ls ) )
            return;
    }

    if ( entry.humanoid )
    {
        const float h = usermode->read<float>( entry.humanoid + offsets::humanoid::health );
        const float mh = usermode->read<float>( entry.humanoid + offsets::humanoid::max_health );
        if ( humanoid_health_real( h, mh ) )
        {
            entry.health = h;
            entry.max_health = mh;
        }
    }
}

inline auto resolve_armor_ammo( cache::player& entry, const sdk::u_model& character ) -> void
{
    entry.armor = 0.f;
    entry.max_armor = 0.f;
    entry.ammo = 0.f;
    entry.max_ammo = 0.f;
    entry.armor_nv = 0;
    entry.max_armor_nv = 0;
    entry.ammo_nv = 0;
    entry.max_ammo_nv = 0;

    if ( !character )
        return;

    static const char* armor_names[] = {
        "Armor", "Shield", "BodyArmor", "Vest", "Defence", "Defense",
        "ArmorValue", "ShieldHP", "ShieldHealth", "Energy", "Barrier",
        "Kevlar", "Plating", "Absorption", "armor", "shield"
    };
    static const char* max_armor_names[] = {
        "MaxArmor", "MaxShield", "MaxBodyArmor", "MaxVest",
        "MaxArmorValue", "MaxShieldHP", "MaxShieldHealth", "MaxEnergy", "MaxBarrier",
        "ArmorMax", "ShieldMax", "maxArmor", "maxShield"
    };
    static const char* ammo_names[] = {
        "Ammo", "AmmoInClip", "CurrentAmmo", "Mag", "Clip", "ammoCurrent",
        "AmmoCount", "Bullets", "Rounds", "Magazine", "AmmoClip", "LeftInClip",
        "ClipAmmo", "GunAmmo", "AmmoLeft", "RemainingAmmo", "Chamber",
        "ammo", "clip", "mag"
    };
    static const char* max_ammo_names[] = {
        "MaxAmmo", "MaxAmmoInClip", "AmmoSize", "MagSize", "ClipSize", "maxAmmo",
        "MagazineSize", "MaxClip", "MaxMag", "Capacity", "AmmoCapacity",
        "ClipCapacity", "MaxRounds", "maxClip", "magSize"
    };

    auto try_armor_from = [&]( const sdk::u_instance& root )
    {
        if ( entry.armor_nv || !root )
            return;
        sdk::u_instance armor = find_armor_ammo_value( root, armor_names, ( int )( sizeof( armor_names ) / sizeof( armor_names[0] ) ) );
        if ( !armor )
            return;
        entry.armor_nv = armor.address( );
        entry.armor = read_number_value( armor );
        sdk::u_instance maxa = find_armor_ammo_value( root, max_armor_names, ( int )( sizeof( max_armor_names ) / sizeof( max_armor_names[0] ) ) );
        if ( maxa )
        {
            entry.max_armor_nv = maxa.address( );
            entry.max_armor = read_number_value( maxa );
        }

        if ( entry.max_armor <= 0.f )
            entry.max_armor = ( std::max )( entry.armor, 100.f );
    };

    try_armor_from( character );
    if ( !entry.armor_nv && entry.humanoid )
        try_armor_from( sdk::u_instance( entry.humanoid ) );
    if ( !entry.armor_nv && entry.address )
        try_armor_from( sdk::u_instance( entry.address ) );

    auto bind_ammo = [&]( const sdk::u_instance& scope )
    {
        if ( entry.ammo_nv || !scope )
            return;
        sdk::u_instance ammo = find_armor_ammo_value( scope, ammo_names, ( int )( sizeof( ammo_names ) / sizeof( ammo_names[0] ) ) );
        if ( !ammo )
            return;
        entry.ammo_nv = ammo.address( );
        entry.ammo = read_number_value( ammo );
        sdk::u_instance maxm = find_armor_ammo_value( scope, max_ammo_names, ( int )( sizeof( max_ammo_names ) / sizeof( max_ammo_names[0] ) ) );
        if ( maxm )
        {
            entry.max_ammo_nv = maxm.address( );
            entry.max_ammo = read_number_value( maxm );
        }
        if ( entry.max_ammo <= 0.f )
            entry.max_ammo = ( std::max )( entry.ammo, 30.f );
    };

    sdk::u_instance tool = character.find_first_child_of_class( "Tool" );
    if ( !tool )
        tool = character.find_first_child_of_class( "HopperBin" );

    if ( !tool )
    {
        for ( const auto& child : character.get_children( ) )
        {
            if ( !child )
                continue;
            const std::string cn = child.class_name( );
            if ( cn != "Model" && cn != "Folder" )
                continue;
            const std::string nm = child.name( );
            if ( name_contains_i( nm, "gun" ) || name_contains_i( nm, "weapon" )
                || name_eq_i( nm, "Equipped" ) || name_eq_i( nm, "CurrentTool" ) )
            {
                tool = child;
                break;
            }
        }
    }

    if ( tool )
    {
        entry.tool = tool.name( );
        bind_ammo( tool );
    }

    if ( !entry.ammo_nv )
        bind_ammo( character );
    if ( !entry.ammo_nv && entry.address )
        bind_ammo( sdk::u_instance( entry.address ) );
}

inline auto resolve_humanoid_state( cache::player& entry ) -> void
{
    entry.state_id = -1;
    entry.state.clear( );
    if ( !entry.humanoid || !config.esp.state )
        return;

    const sdk::u_humanoid hum( entry.humanoid );
    const int id = hum.state( );
    entry.state_id = id;

    if ( id == offsets::humanoid_state::running )
    {
        float speed = 0.f;
        for ( const auto& b : entry.bones )
        {
            if ( b.name == "Head" || b.part == entry.head )
            {
                speed = b.velocity.length( );
                break;
            }
        }
        if ( speed < 0.1f )
        {
            entry.state = "Idle";
            return;
        }
    }

    entry.state = sdk::u_humanoid::state_name( id );
}

inline auto push_bone( cache::player& entry, const sdk::u_instance& inst, const std::string& name ) -> void
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
    entry.bones.push_back( std::move( b ) );
}

inline auto add_bone_alias( cache::player& entry, const char* dst, const cache::player::bone& src ) -> void
{
    if ( cache::find_bone( entry, dst ) )
        return;
    cache::player::bone copy = src;
    copy.name = dst;
    entry.bones.push_back( std::move( copy ) );
}

inline auto add_bone_alias_named( cache::player& entry, const char* dst, const char* src_name ) -> void
{
    const auto* src = cache::find_bone( entry, src_name );
    if ( !src )
        return;
    add_bone_alias( entry, dst, *src );
}

inline auto normalize_player_labels( cache::player& entry ) -> void
{

    if ( entry.display_name.empty( ) && !entry.name.empty( ) )
        entry.display_name = entry.name;


    if ( entry.is_npc || !entry.address )
    {
        if ( entry.name.empty( ) && entry.character )
        {
            const std::string model_name = sdk::u_instance( entry.character ).name( );
            if ( !model_name.empty( ) && model_name != "Character" && model_name != "Model" )
            {
                entry.name = model_name;
                if ( entry.display_name.empty( ) )
                    entry.display_name = model_name;
            }
        }
        if ( entry.name.empty( ) && !entry.display_name.empty( ) )
            entry.name = entry.display_name;
    }
}

inline auto finalize_entry( cache::player& entry, const math::vector3_t& local_pos ) -> void
{
    entry.root_part = 0;
    entry.head = 0;
    for ( const auto& b : entry.bones )
    {
        if ( b.name == "HumanoidRootPart" || b.name == "Abdomen" || b.name == "torso" )
            entry.root_part = b.part;
        else if ( b.name == "Head" || b.name == "head" )
            entry.head = b.part;
    }
    if ( !entry.root_part && !entry.bones.empty( ) )
        entry.root_part = entry.bones.front( ).part;

    if ( local_pos.is_valid( ) )
    {
        for ( const auto& b : entry.bones )
        {
            if ( ( b.part == entry.root_part || b.name == "HumanoidRootPart" || b.name == "Abdomen" ) && b.position.is_valid( ) )
            {
                entry.distance = ( b.position - local_pos ).length( );
                break;
            }
        }
    }

    normalize_player_labels( entry );

    if ( entry.humanoid )
        resolve_humanoid_state( entry );
}

inline auto parts_contain_prim( const cache::player& entry, uintptr_t prim ) -> bool
{
    if ( !prim )
        return false;
    for ( const auto& b : entry.bones )
        if ( b.primitive == prim )
            return true;
    return false;
}

inline auto build_char_owners( ) -> std::unordered_map<uintptr_t, sdk::u_player>
{
    std::unordered_map<uintptr_t, sdk::u_player> owners;
    if ( !g->engine.players )
        return owners;

    sdk::u_players service( g->engine.players );
    for ( const auto& p : service.get_players( ) )
    {
        if ( !p )
            continue;
        sdk::u_model c = p.character( );
        if ( c )
            owners.emplace( c.address( ), p );
    }
    return owners;
}

}
}
