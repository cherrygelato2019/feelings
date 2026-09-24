#include <include/global.cuh>
#include <src/engine/cache/pf_cache.hxx>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pf_cache
{
namespace
{

constexpr float k_max_limb_dist_sq = 4.5f * 4.5f;

struct pf_pin
{
    uintptr_t model = 0;
    uintptr_t torso_prim = 0;
    uintptr_t torso = 0;
    uintptr_t head = 0;
    uintptr_t lower = 0;
    uintptr_t larm = 0;
    uintptr_t rarm = 0;
    uintptr_t lleg = 0;
    uintptr_t rleg = 0;
    uintptr_t team = 2;
    uintptr_t team_folder = 0;
    uintptr_t player = 0;
    float health = 100.f;
    float max_health = 100.f;
    char name[ 64 ]{};
    char tool[ 64 ]{};
    int miss = 0;
    bool team_known = false;
};

struct raw_part
{
    uintptr_t part = 0;
    uintptr_t prim = 0;
    math::cframe_t cf{};
    math::vector3_t vel{};
    bool spot = false;
    bool billboard = false;
};

struct local_part
{
    uintptr_t part = 0;
    uintptr_t prim = 0;
    math::cframe_t cf{};
    math::vector3_t vel{};
    float local_right = 0.f;
    float local_up = 0.f;
};

std::mutex g_pin_mtx;
std::unordered_map<uintptr_t, pf_pin> g_pins;

auto abs_f( float v ) -> float
{
    return v < 0.f ? -v : v;
}

auto copy_name( char* dest, const std::string& s ) -> void
{
    if ( !dest )
        return;
    const auto n = ( std::min )( s.size( ), ( size_t )63 );
    if ( n )
        std::memcpy( dest, s.data( ), n );
    dest[ n ] = '\0';
}

auto clean_name( const std::string& raw, std::size_t max_len = 32 ) -> std::string
{
    std::string out;
    for ( unsigned char ch : raw )
    {
        if ( !ch )
            break;
        if ( ch < 0x20 || ch > 0x7E )
            continue;
        if ( ch == '{' || ch == '}' || ch == '<' || ch == '>' || ch == '|' )
            continue;
        out.push_back( static_cast<char>( ch ) );
        if ( out.size( ) >= max_len )
            break;
    }

    while ( !out.empty( ) && out.front( ) == ' ' )
        out.erase( out.begin( ) );
    while ( !out.empty( ) && out.back( ) == ' ' )
        out.pop_back( );

    if ( out.size( ) < 2 || out.size( ) > 40 )
        return {};

    int alnum = 0;
    for ( unsigned char ch : out )
    {
        if ( std::isalnum( ch ) )
            ++alnum;
    }
    if ( alnum < 2 || alnum * 2 < static_cast<int>( out.size( ) ) )
        return {};

    if ( out == "Part" || out == "Model" || out == "Head" || out == "Torso"
        || out == "Humanoid" || out == "Misc" || out == "Player" )
        return {};

    return out;
}

auto is_weapon( const std::string& name ) -> bool
{
    return name.find( "Main" ) != std::string::npos
        || name.find( "Gun" ) != std::string::npos
        || name.find( "Weapon" ) != std::string::npos
        || name.find( "Arms" ) != std::string::npos
        || name.find( "Knife" ) != std::string::npos
        || name.find( "Blade" ) != std::string::npos
        || name.find( "Grenade" ) != std::string::npos
        || name.find( "Drop" ) != std::string::npos;
}

auto prim_alive( uintptr_t prim ) -> bool
{
    if ( !prim || !usermode->is_valid( prim ) )
        return false;
    const math::vector3_t p = usermode->read<math::vector3_t>( prim + offsets::primitive::position );
    return p.is_valid( ) && ( fabsf( p.x ) + fabsf( p.y ) + fabsf( p.z ) > 0.01f );
}

auto read_part( uintptr_t part, uintptr_t& prim, math::cframe_t& cf, math::vector3_t& vel ) -> bool
{
    if ( !part )
        return false;
    prim = usermode->read<uintptr_t>( part + offsets::basepart::primitive );
    if ( !prim_alive( prim ) )
        return false;

    uint8_t xform[ 60 ];
    if ( !usermode->read_buf( prim + offsets::primitive::rotation, xform, sizeof( xform ) ) )
        return false;
    std::memcpy( &cf.rotation, xform + 0, sizeof( math::matrix3_t ) );
    std::memcpy( &cf.position, xform + 36, sizeof( math::vector3_t ) );
    std::memcpy( &vel, xform + 48, sizeof( math::vector3_t ) );
    return cf.position.is_valid( );
}

auto near_torso( const math::vector3_t& pos, const math::vector3_t& torso ) -> bool
{
    const float dx = pos.x - torso.x;
    const float dy = pos.y - torso.y;
    const float dz = pos.z - torso.z;
    return dx * dx + dy * dy + dz * dz <= k_max_limb_dist_sq;
}

auto dot3( const math::vector3_t& a, const math::vector3_t& b ) -> float
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

auto pick_pair( std::vector<local_part>& src, local_part& L, local_part& R ) -> bool
{
    if ( src.size( ) < 2 )
        return false;
    std::sort( src.begin( ), src.end( ),
        []( const local_part& a, const local_part& b ) { return a.local_right < b.local_right; } );
    L = src.front( );
    R = src.back( );
    return true;
}

auto push_named_bone(
    cache::player& pe,
    const char* name,
    uintptr_t part,
    uintptr_t prim,
    const math::cframe_t& cf,
    const math::vector3_t& vel,
    const math::vector3_t& size ) -> void
{
    if ( !part || !prim )
        return;
    cache::player::bone b{};
    b.name = name;
    b.part = part;
    b.primitive = prim;
    b.size = size;
    b.position = cf.position;
    b.rotation = cf.rotation;
    b.velocity = vel;
    pe.bones.push_back( std::move( b ) );
}

auto bind_limb(
    cache::player& pe,
    const char* name,
    uintptr_t part,
    const math::vector3_t& torso_pos,
    const math::vector3_t& size ) -> void
{
    if ( !part )
        return;
    uintptr_t prim = 0;
    math::cframe_t cf{};
    math::vector3_t vel{};
    if ( !read_part( part, prim, cf, vel ) )
        return;
    if ( !near_torso( cf.position, torso_pos ) )
        return;
    push_named_bone( pe, name, part, prim, cf, vel, size );
}

auto model_owns_prim( uintptr_t model, uintptr_t want_prim ) -> bool
{
    if ( !model || !want_prim )
        return false;

    sdk::u_instance m( model );
    for ( const auto& child : m.get_children( ) )
    {
        if ( !child || child.class_name( ).find( "Part" ) == std::string::npos )
            continue;

        bool spot = false;
        for ( const auto& gc : child.get_children( ) )
        {
            if ( gc && gc.class_name( ) == "SpotLight" )
            {
                spot = true;
                break;
            }
        }
        if ( !spot )
            continue;

        const uintptr_t prim = usermode->read<uintptr_t>( child.address( ) + offsets::basepart::primitive );
        if ( prim == want_prim && prim_alive( prim ) )
            return true;
    }
    return false;
}

auto names_equal_ci( const std::string& a, const char* b ) -> bool
{
    if ( !b || !b[ 0 ] || a.empty( ) )
        return false;
    size_t i = 0;
    for ( ; a[ i ] && b[ i ]; ++i )
    {
        const unsigned char ca = ( unsigned char )a[ i ];
        const unsigned char cb = ( unsigned char )b[ i ];
        if ( std::tolower( ca ) != std::tolower( cb ) )
            return false;
    }
    return a[ i ] == '\0' && b[ i ] == '\0';
}

auto head_team_mark( uintptr_t head ) -> int
{
    if ( !head )
        return 0;

    sdk::u_instance h( head );
    sdk::u_instance name_tag = h.find_first_child( "NameTagGui" );
    if ( !name_tag )
        name_tag = h.find_first_child_of_class( "BillboardGui" );
    if ( !name_tag )
        return 0;

    sdk::u_instance player_tag = name_tag.find_first_child( "PlayerTag" );
    if ( !player_tag )
        player_tag = name_tag.find_first_child_of_class( "Frame" );
    if ( !player_tag )
        return 0;

    sdk::u_instance dot = player_tag.find_first_child( "Dot" );
    if ( !dot )
        return 0;

    const math::vector3_t bg = usermode->read<math::vector3_t>(
        dot.address( ) + offsets::gui_object::background_color3 );
    const bool looks_like_color =
        bg.x >= -0.01f && bg.x <= 1.01f
        && bg.y >= -0.01f && bg.y <= 1.01f
        && bg.z >= -0.01f && bg.z <= 1.01f;
    if ( !looks_like_color )
        return 0;

    const bool cyan = ( bg.x * 255.f ) < 60.f && ( bg.y * 255.f ) > 195.f && ( bg.z * 255.f ) > 175.f;
    return cyan ? 1 : 2;
}

auto read_billboard_name( uintptr_t head ) -> std::string
{
    if ( !head )
        return {};
    sdk::u_instance h( head );
    sdk::u_instance gui = h.find_first_child( "NameTagGui" );
    if ( !gui )
        gui = h.find_first_child_of_class( "BillboardGui" );
    if ( !gui )
        return {};
    sdk::u_instance tl = gui.find_first_child_of_class( "TextLabel" );
    if ( !tl )
        return {};
    return clean_name( usermode->read_string( tl.address( ) + offsets::gui_object::text ) );
}

auto fill_weapon( pf_pin& pin, uintptr_t model ) -> void
{
    if ( !model || pin.tool[ 0 ] != '\0' )
        return;

    sdk::u_instance character( model );
    std::string fallback;
    for ( const auto& child : character.get_children( ) )
    {
        if ( !child || child.class_name( ) != "Model" )
            continue;

        auto weapon = clean_name( child.name( ) );
        if ( weapon.empty( ) )
            continue;

        if ( weapon.size( ) > 5 && weapon.compare( weapon.size( ) - 5, 5, " Main" ) == 0 )
            weapon.resize( weapon.size( ) - 5 );
        else if ( weapon.size( ) > 4 && weapon.compare( weapon.size( ) - 4, 4, "Main" ) == 0 )
            weapon.resize( weapon.size( ) - 4 );

        while ( !weapon.empty( ) && weapon.back( ) == ' ' )
            weapon.pop_back( );
        if ( weapon.empty( ) )
            continue;

        if ( child.name( ).find( "Main" ) != std::string::npos )
        {
            copy_name( pin.tool, weapon );
            return;
        }
        if ( fallback.empty( ) )
            fallback = std::move( weapon );
    }
    if ( !fallback.empty( ) )
        copy_name( pin.tool, fallback );
}

auto soft_refresh_head( uintptr_t model, pf_pin& pin ) -> void
{
    if ( !model )
        return;
    sdk::u_instance m( model );
    for ( const auto& child : m.get_children( ) )
    {
        if ( !child || child.class_name( ).find( "Part" ) == std::string::npos )
            continue;

        bool mesh = false;
        sdk::u_instance billboard;
        for ( const auto& gc : child.get_children( ) )
        {
            if ( !gc )
                continue;
            const std::string gcls = gc.class_name( );
            if ( gcls == "SpecialMesh" )
                mesh = true;
            else if ( gcls == "BillboardGui" )
                billboard = gc;
        }
        if ( !mesh || !billboard )
            continue;

        uintptr_t prim = 0;
        math::cframe_t cf{};
        math::vector3_t vel{};
        if ( !read_part( child.address( ), prim, cf, vel ) )
            continue;

        pin.head = child.address( );
        sdk::u_instance tl = billboard.find_first_child_of_class( "TextLabel" );
        if ( tl )
        {
            const std::string tag = clean_name( usermode->read_string( tl.address( ) + offsets::gui_object::text ) );
            if ( !tag.empty( ) )
                copy_name( pin.name, tag );
        }
        return;
    }
}

auto erase_model_orphans( uintptr_t model, uintptr_t keep_prim ) -> void
{
    if ( !model )
        return;
    for ( auto it = g_pins.begin( ); it != g_pins.end( ); )
    {
        if ( it->second.model == model && it->first != keep_prim )
            it = g_pins.erase( it );
        else
            ++it;
    }
}

auto classify_limbs_onto_pin( uintptr_t model, pf_pin& pin, const math::cframe_t& torso_cf ) -> void
{
    if ( !model || !pin.torso )
        return;

    sdk::u_instance m( model );
    std::vector<raw_part> limbs;
    uintptr_t head = pin.head;

    for ( const auto& child : m.get_children( ) )
    {
        if ( !child || child.class_name( ).find( "Part" ) == std::string::npos )
            continue;

        bool mesh = false, spot = false;
        sdk::u_instance billboard;
        for ( const auto& gc : child.get_children( ) )
        {
            if ( !gc )
                continue;
            const std::string gcls = gc.class_name( );
            if ( gcls == "SpecialMesh" )
                mesh = true;
            else if ( gcls == "SpotLight" )
                spot = true;
            else if ( gcls == "BillboardGui" )
                billboard = gc;
        }

        // Match njx: BillboardGui part is head; SpotLight is torso; SpecialMesh limbs only.
        if ( billboard )
        {
            head = child.address( );
            continue;
        }
        if ( spot || !mesh || child.address( ) == pin.torso )
            continue;

        raw_part rp{};
        if ( !read_part( child.address( ), rp.prim, rp.cf, rp.vel ) )
            continue;
        rp.part = child.address( );
        limbs.push_back( rp );
    }

    if ( head )
        pin.head = head;

    // Torso-local axes (same indexing as njx) — NOT world Y.
    const math::vector3_t right_axis{
        torso_cf.rotation.data[ 0 ], torso_cf.rotation.data[ 3 ], torso_cf.rotation.data[ 6 ]
    };
    const math::vector3_t up_axis{
        torso_cf.rotation.data[ 1 ], torso_cf.rotation.data[ 4 ], torso_cf.rotation.data[ 7 ]
    };

    std::vector<local_part> remaining;
    remaining.reserve( limbs.size( ) );
    for ( const auto& e : limbs )
    {
        if ( !near_torso( e.cf.position, torso_cf.position ) )
            continue;

        math::vector3_t rel{
            e.cf.position.x - torso_cf.position.x,
            e.cf.position.y - torso_cf.position.y,
            e.cf.position.z - torso_cf.position.z
        };

        local_part lp{};
        lp.part = e.part;
        lp.prim = e.prim;
        lp.cf = e.cf;
        lp.vel = e.vel;
        lp.local_right = dot3( rel, right_axis );
        lp.local_up = dot3( rel, up_axis );
        remaining.push_back( lp );
    }

    uintptr_t lower = 0;
    if ( remaining.size( ) >= 5 )
    {
        auto best = remaining.begin( );
        float best_score = ( std::numeric_limits<float>::max )( );
        for ( auto it = remaining.begin( ); it != remaining.end( ); ++it )
        {
            const float score = abs_f( it->local_right ) + abs_f( it->local_up );
            if ( score < best_score )
            {
                best_score = score;
                best = it;
            }
        }
        lower = best->part;
        remaining.erase( best );
    }

    std::vector<local_part> legs, arms;
    for ( const auto& lp : remaining )
    {
        if ( lp.local_up < -0.5f )
            legs.push_back( lp );
        else
            arms.push_back( lp );
    }

    if ( legs.size( ) < 2 && remaining.size( ) >= 2 )
    {
        auto sorted = remaining;
        std::sort( sorted.begin( ), sorted.end( ),
            []( const local_part& a, const local_part& b ) { return a.local_up < b.local_up; } );
        legs = { sorted[ 0 ], sorted[ 1 ] };
        arms.clear( );
        for ( size_t i = 2; i < sorted.size( ); ++i )
            arms.push_back( sorted[ i ] );
    }

    local_part larm{}, rarm{}, lleg{}, rleg{};
    const bool have_arms = pick_pair( arms, larm, rarm );
    const bool have_legs = pick_pair( legs, lleg, rleg );

    pin.lower = lower;
    pin.larm = have_arms ? larm.part : 0;
    pin.rarm = have_arms ? rarm.part : 0;
    pin.lleg = have_legs ? lleg.part : 0;
    pin.rleg = have_legs ? rleg.part : 0;
}

auto classify_new( uintptr_t model, uintptr_t local_prim, pf_pin& pin ) -> bool
{
    sdk::u_instance m( model );
    const auto model_name = m.name( );
    if ( is_weapon( model_name ) )
        return false;

    auto raw_parts = m.get_children( );
    if ( raw_parts.empty( ) )
        return false;

    std::vector<raw_part> parts;
    parts.reserve( raw_parts.size( ) );
    std::string display;

    for ( const auto& child : raw_parts )
    {
        if ( !child )
            continue;
        if ( child.class_name( ).find( "Part" ) == std::string::npos )
            continue;

        bool mesh = false, spot = false;
        sdk::u_instance billboard;
        for ( const auto& gc : child.get_children( ) )
        {
            if ( !gc )
                continue;
            const std::string gcls = gc.class_name( );
            if ( gcls == "SpecialMesh" )
                mesh = true;
            else if ( gcls == "SpotLight" )
                spot = true;
            else if ( gcls == "BillboardGui" )
                billboard = gc;
        }
        if ( !mesh && !spot && !billboard )
            continue;

        raw_part rp{};
        if ( !read_part( child.address( ), rp.prim, rp.cf, rp.vel ) )
            continue;
        rp.part = child.address( );
        rp.spot = spot;
        rp.billboard = (bool)billboard;

        if ( rp.billboard && display.empty( ) )
        {
            sdk::u_instance tl = billboard.find_first_child_of_class( "TextLabel" );
            if ( tl )
                display = clean_name( usermode->read_string( tl.address( ) + offsets::gui_object::text ) );
        }
        parts.push_back( rp );
    }

    uintptr_t torso = 0, torso_prim = 0, head = 0;
    math::cframe_t torso_cf{};
    for ( const auto& rp : parts )
    {
        if ( rp.billboard )
        {
            head = rp.part;
            continue;
        }
        if ( rp.spot )
        {
            torso = rp.part;
            torso_prim = rp.prim;
            torso_cf = rp.cf;
        }
    }
    if ( !torso || !torso_prim )
        return false;
    if ( local_prim && torso_prim == local_prim )
        return false;

    if ( display.empty( ) )
        display = clean_name( model_name );
    if ( display.empty( ) || is_weapon( display ) )
        return false;

    pin = {};
    pin.model = model;
    pin.torso_prim = torso_prim;
    pin.torso = torso;
    pin.head = head;
    copy_name( pin.name, display );
    pin.miss = 0;
    fill_weapon( pin, model );
    classify_limbs_onto_pin( model, pin, torso_cf );
    return true;
}

auto emit_from_pin( pf_pin& pin, cache::player& pe, const math::vector3_t& local_pos ) -> bool
{
    // Same default part sizes as njx — ESP box / skeleton consume these.
    const math::vector3_t torso_size{ 2.f, 2.f, 1.f };
    const math::vector3_t head_size{ 1.f, 1.f, 1.f };
    const math::vector3_t limb_size{ 1.f, 2.f, 1.f };

    uintptr_t prim = 0;
    math::cframe_t cf{};
    math::vector3_t vel{};
    if ( !read_part( pin.torso, prim, cf, vel ) )
        return false;

    const math::vector3_t torso_pos = cf.position;
    classify_limbs_onto_pin( pin.model, pin, cf );
    soft_refresh_head( pin.model, pin );

    pe = {};
    pe.character = pin.model;
    pe.address = pin.player ? pin.player : pin.model;
    pe.name = pin.name;
    pe.display_name = pin.name;
    pe.team = pin.team;
    // team 1 = same folder / ally mark as local; only trust when team was resolved this frame
    pe.teammate = pin.team_known && pin.team == 1;
    if ( pin.team_folder )
        pe.team_name = sdk::u_instance( pin.team_folder ).name( );
    else if ( pe.teammate )
        pe.team_name = "ally";
    else if ( pin.team_known && pin.team == 2 )
        pe.team_name = "enemy";
    else
        pe.team_name.clear( );
    pe.is_npc = false;
    pe.rig_type = 0;
    pe.health = pin.health;
    pe.max_health = pin.max_health > 0.f ? pin.max_health : 100.f;
    if ( pin.tool[ 0 ] )
        pe.tool = pin.tool;

    push_named_bone( pe, "Torso", pin.torso, prim, cf, vel, torso_size );
    push_named_bone( pe, "HumanoidRootPart", pin.torso, prim, cf, vel, torso_size );

    bind_limb( pe, "Head", pin.head, torso_pos, head_size );
    bind_limb( pe, "Left Arm", pin.larm, torso_pos, limb_size );
    bind_limb( pe, "Right Arm", pin.rarm, torso_pos, limb_size );
    bind_limb( pe, "Left Leg", pin.lleg, torso_pos, limb_size );
    bind_limb( pe, "Right Leg", pin.rleg, torso_pos, limb_size );

    if ( pe.bones.empty( ) )
        return false;

    pe.root_part = pin.torso;
    pe.head = pin.head;
    if ( local_pos.is_valid( ) && torso_pos.is_valid( ) )
        pe.distance = ( torso_pos - local_pos ).length( );

    if ( pe.name.empty( ) && pin.model )
    {
        const std::string model_name = sdk::u_instance( pin.model ).name( );
        if ( !model_name.empty( ) && !is_weapon( model_name ) )
            pe.name = model_name;
    }
    if ( pe.display_name.empty( ) )
        pe.display_name = pe.name;
    if ( pe.name.empty( ) )
        pe.name = pe.display_name;

    return true;
}

} 


auto resolve_local_prim( uintptr_t local_char ) -> uintptr_t
{
    if ( !local_char )
        return 0;

    sdk::u_instance model( local_char );
    for ( const auto& child : model.get_children( ) )
    {
        if ( !child || child.class_name( ).find( "Part" ) == std::string::npos )
            continue;
        bool spot = false;
        for ( const auto& gc : child.get_children( ) )
        {
            if ( gc && gc.class_name( ) == "SpotLight" )
            {
                spot = true;
                break;
            }
        }
        if ( !spot )
            continue;
        const uintptr_t prim = usermode->read<uintptr_t>( child.address( ) + offsets::basepart::primitive );
        if ( prim_alive( prim ) )
            return prim;
    }
    return 0;
}

auto find_pf_players_folder( const sdk::u_instance& workspace ) -> sdk::u_instance
{
    auto score_folder = []( const sdk::u_instance& child ) -> int
    {
        if ( !child )
            return 0;

        const std::string cn = child.class_name( );
        const std::string nm = child.name( );
        if ( cn != "Folder" && cn != "Model" && cn != "Configuration" )
            return 0;

        const auto teams = child.get_children( );
        if ( teams.size( ) < 1 || teams.size( ) > 16 )
            return 0;

        int score = 0;
        if ( nm == "Players" )
            score += 500;
        if ( cn == "Folder" )
            score += 50;
        if ( cn == "Model" && nm != "Players" )
            score -= 200;

        int team_hits = 0;
        int model_hits = 0;
        int total_models = 0;

        for ( const auto& team : teams )
        {
            if ( !team )
                continue;
            const std::string tcn = team.class_name( );
            if ( tcn != "Folder" && tcn != "Model" )
                continue;

            bool team_has_player = false;
            for ( const auto& model : team.get_children( ) )
            {
                if ( !model || model.class_name( ) != "Model" )
                    continue;
                ++total_models;
                if ( total_models > 128 )
                    return 0;

                for ( const auto& part : model.get_children( ) )
                {
                    if ( !part || part.class_name( ).find( "Part" ) == std::string::npos )
                        continue;
                    if ( part.find_first_child_of_class( "SpotLight" ) )
                    {
                        ++model_hits;
                        team_has_player = true;
                        break;
                    }
                }
            }
            if ( team_has_player )
                ++team_hits;
        }

        if ( model_hits < 1 )
            return ( nm == "Players" ) ? score : 0;

        score += team_hits * 40;
        score += model_hits * 15;
        return score;
    };

    sdk::u_instance best{};
    int best_score = 0;

    auto consider = [&]( const sdk::u_instance& node )
    {
        const int s = score_folder( node );
        if ( s > best_score )
        {
            best_score = s;
            best = node;
        }
    };

    consider( workspace.find_first_child( "Players" ) );

    for ( const auto& child : workspace.get_children( ) )
    {
        consider( child );
        if ( child.class_name( ) == "Folder" )
        {
            for ( const auto& nested : child.get_children( ) )
            {
                if ( nested.name( ) == "Players" || nested.class_name( ) == "Folder" )
                    consider( nested );
            }
        }
    }

    return best_score > 0 ? best : sdk::u_instance( );
}

auto cache_players(
    std::vector<cache::player>& out,
    const math::vector3_t& local_pos,
    uintptr_t local_prim,
    const std::string& local_name ) -> void
{
    static auto last_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now( );
    const bool should_log = last_log.time_since_epoch( ).count( ) == 0
        || ( now - last_log ) >= std::chrono::seconds( 2 );

    if ( !g->engine.workspace )
    {
        if ( should_log )
        {
            g->logger.print( "[pf] workspace missing" );
            last_log = now;
        }
        return;
    }

    sdk::u_instance workspace( g->engine.workspace.load( ) );
    // Prefer the real Workspace.Players folder (njx). Heuristic is fallback only.
    sdk::u_instance players_folder = workspace.find_first_child( "Players" );
    if ( !players_folder )
        players_folder = find_pf_players_folder( workspace );
    if ( !players_folder )
    {
        if ( should_log )
        {
            const auto kids = workspace.get_children( );
            g->logger.print( "[pf] no Players folder (ws kids=%zu local='%s')",
                kids.size( ), local_name.c_str( ) );
            const size_t show = kids.size( ) < 12 ? kids.size( ) : 12;
            for ( size_t i = 0; i < show; ++i )
            {
                g->logger.print( "[pf]   ws[%zu] class='%s' name='%s' childs=%zu",
                    i,
                    kids[ i ].class_name( ).c_str( ),
                    kids[ i ].name( ).c_str( ),
                    kids[ i ].get_children( ).size( ) );
            }
            last_log = now;
        }
        return;
    }

    if ( should_log )
    {
        static uintptr_t last_folder = 0;
        if ( players_folder.address( ) != last_folder )
        {
            g->logger.print( "[pf] using folder '%s' class='%s' addr=0x%llx",
                players_folder.name( ).c_str( ),
                players_folder.class_name( ).c_str( ),
                ( unsigned long long )players_folder.address( ) );
            last_folder = players_folder.address( );
        }
    }

    auto teams = players_folder.get_children( );
    if ( teams.empty( ) )
    {
        if ( should_log )
        {
            g->logger.print( "[pf] Players folder empty" );
            last_log = now;
        }
        return;
    }

    uintptr_t local_team_folder = 0;
    uintptr_t resolved_local_prim = local_prim;

    int team_count = 0;
    int model_count = 0;
    int classify_ok = 0;
    int classify_fail = 0;
    int weapon_skip = 0;

    std::unordered_map<uintptr_t, uintptr_t> live_models;
    std::unordered_map<uintptr_t, uintptr_t> model_team_folder;
    live_models.reserve( 64 );

    for ( const auto& team : teams )
    {
        if ( !team )
            continue;
        ++team_count;
        for ( const auto& model : team.get_children( ) )
        {
            if ( !model )
                continue;
            ++model_count;
            model_team_folder[ model.address( ) ] = team.address( );

            // Resolve local team by SpotLight prim — local is never pinned when local_prim is known,
            // so name-based lookup alone leaves local_team_folder=0 and teammates look like enemies.
            if ( !local_team_folder )
            {
                const uintptr_t want = resolved_local_prim ? resolved_local_prim : local_prim;
                if ( want && model_owns_prim( model.address( ), want ) )
                {
                    local_team_folder = team.address( );
                    resolved_local_prim = want;
                }
            }

            const auto model_name = model.name( );
            if ( is_weapon( model_name ) )
            {
                ++weapon_skip;
                std::lock_guard<std::mutex> lk( g_pin_mtx );
                for ( auto it = g_pins.begin( ); it != g_pins.end( ); )
                {
                    if ( it->second.model == model.address( ) )
                        it = g_pins.erase( it );
                    else
                        ++it;
                }
                continue;
            }

            uintptr_t found_prim = 0;
            {
                std::lock_guard<std::mutex> lk( g_pin_mtx );
                for ( auto it = g_pins.begin( ); it != g_pins.end( ); )
                {
                    if ( it->second.model != model.address( ) )
                    {
                        ++it;
                        continue;
                    }

                    it->second.team_folder = team.address( );
                    if ( prim_alive( it->first ) )
                    {
                        found_prim = it->first;
                        it->second.miss = 0;
                        live_models[ it->first ] = model.address( );

                        if ( ( resolved_local_prim && it->first == resolved_local_prim )
                            || ( local_prim && it->first == local_prim )
                            || ( !local_name.empty( ) && it->second.name[ 0 ]
                                && names_equal_ci( local_name, it->second.name ) ) )
                        {
                            resolved_local_prim = it->first;
                            local_team_folder = team.address( );
                        }
                        ++it;
                    }
                    else
                    {
                        // Dead torso on this model — drop pin so classify can bind the new body.
                        it = g_pins.erase( it );
                    }
                    break;
                }
            }
            if ( found_prim )
                continue;

            pf_pin neu{};
            if ( !classify_new( model.address( ), resolved_local_prim, neu ) )
            {
                ++classify_fail;
                continue;
            }
            neu.team_folder = team.address( );
            ++classify_ok;
            live_models[ neu.torso_prim ] = model.address( );

            if ( !local_name.empty( ) && neu.name[ 0 ] && names_equal_ci( local_name, neu.name ) )
            {
                resolved_local_prim = neu.torso_prim;
                local_team_folder = team.address( );
            }

            std::lock_guard<std::mutex> lk( g_pin_mtx );
            erase_model_orphans( neu.model, neu.torso_prim );
            auto it = g_pins.find( neu.torso_prim );
            if ( it != g_pins.end( ) )
            {
                it->second.model = neu.model;
                it->second.team_folder = neu.team_folder;
                it->second.miss = 0;
                if ( !it->second.head && neu.head )
                    it->second.head = neu.head;
                if ( !it->second.lower && neu.lower )
                    it->second.lower = neu.lower;
                if ( !it->second.larm && neu.larm )
                    it->second.larm = neu.larm;
                if ( !it->second.rarm && neu.rarm )
                    it->second.rarm = neu.rarm;
                if ( !it->second.lleg && neu.lleg )
                    it->second.lleg = neu.lleg;
                if ( !it->second.rleg && neu.rleg )
                    it->second.rleg = neu.rleg;
                if ( neu.name[ 0 ] )
                    std::memcpy( it->second.name, neu.name, sizeof( neu.name ) );
                if ( neu.tool[ 0 ] && !it->second.tool[ 0 ] )
                    std::memcpy( it->second.tool, neu.tool, sizeof( neu.tool ) );
            }
            else
            {
                g_pins.emplace( neu.torso_prim, neu );
            }
        }
    }

    if ( !local_team_folder && !local_name.empty( ) )
    {
        std::lock_guard<std::mutex> lk( g_pin_mtx );
        for ( auto& [ prim, pin ] : g_pins )
        {
            if ( pin.name[ 0 ] && names_equal_ci( local_name, pin.name ) )
            {
                local_team_folder = pin.team_folder;
                if ( !resolved_local_prim )
                    resolved_local_prim = prim;
                break;
            }
        }
    }

    std::vector<pf_pin> snapshot;
    {
        std::lock_guard<std::mutex> lk( g_pin_mtx );
        for ( auto it = g_pins.begin( ); it != g_pins.end( ); )
        {
            auto live = live_models.find( it->first );
            if ( live == live_models.end( ) )
            {
                if ( ++it->second.miss > 45 )
                {
                    it = g_pins.erase( it );
                    continue;
                }
            }
            else
            {
                it->second.model = live->second;
                it->second.miss = 0;
            }
            if ( it->second.miss == 0 )
                snapshot.push_back( it->second );
            ++it;
        }
    }

    out.reserve( snapshot.size( ) );
    for ( auto pin : snapshot )
    {
        soft_refresh_head( pin.model, pin );
        fill_weapon( pin, pin.model );

        if ( pin.head )
        {
            const std::string tag = read_billboard_name( pin.head );
            if ( !tag.empty( ) )
                copy_name( pin.name, tag );
        }

        if ( auto it = model_team_folder.find( pin.model ); it != model_team_folder.end( ) )
            pin.team_folder = it->second;

        if ( local_team_folder && pin.team_folder )
        {
            pin.team = ( pin.team_folder == local_team_folder ) ? 1 : 2;
            pin.team_known = true;
        }
        else if ( !pin.team_known && pin.head )
        {
            const int mark = head_team_mark( pin.head );
            if ( mark == 1 || mark == 2 )
            {
                pin.team = ( uintptr_t )mark;
                pin.team_known = true;
            }
        }

        if ( pin.health <= 0.f )
        {
            pin.health = 100.f;
            pin.max_health = 100.f;
        }

        {
            std::lock_guard<std::mutex> lk( g_pin_mtx );
            auto it = g_pins.find( pin.torso_prim );
            if ( it != g_pins.end( ) )
            {
                it->second.team = pin.team;
                it->second.team_known = pin.team_known;
                it->second.team_folder = pin.team_folder;
                it->second.health = pin.health;
                it->second.max_health = pin.max_health;
                it->second.head = pin.head;
                if ( pin.name[ 0 ] )
                    std::memcpy( it->second.name, pin.name, sizeof( pin.name ) );
                if ( pin.tool[ 0 ] )
                    std::memcpy( it->second.tool, pin.tool, sizeof( pin.tool ) );
            }
        }

        if ( resolved_local_prim && pin.torso_prim == resolved_local_prim )
            continue;
        if ( local_name.size( ) >= 2 && names_equal_ci( local_name, pin.name ) )
            continue;

        // Folder match is authoritative for PF team check.
        if ( local_team_folder && pin.team_folder )
        {
            pin.team = ( pin.team_folder == local_team_folder ) ? 1 : 2;
            pin.team_known = true;
        }

        cache::player pe{};
        if ( !emit_from_pin( pin, pe, local_pos ) )
            continue;
        out.push_back( std::move( pe ) );
    }

    static size_t last_cached = SIZE_MAX;
    if ( should_log || out.size( ) != last_cached )
    {
        size_t allies = 0, enemies = 0, unknown = 0;
        for ( const auto& p : out )
        {
            if ( p.teammate )
                ++allies;
            else if ( p.team == 2 )
                ++enemies;
            else
                ++unknown;
        }

        g->logger.print(
            "[pf] teams=%d models=%d classify_ok=%d fail=%d pins=%zu cached=%zu local_prim=0x%llx local_team=0x%llx allies=%zu enemies=%zu unk=%zu",
            team_count, model_count, classify_ok, classify_fail,
            g_pins.size( ), out.size( ),
            ( unsigned long long )resolved_local_prim,
            ( unsigned long long )local_team_folder,
            allies, enemies, unknown );

        const size_t show = out.size( ) < 6 ? out.size( ) : 6;
        for ( size_t i = 0; i < show; ++i )
        {
            const auto& p = out[ i ];
            g->logger.print(
                "[pf]   #%zu '%s' team=%llu bones=%zu head=%s dist=%.1f",
                i, p.name.c_str( ), ( unsigned long long )p.team, p.bones.size( ),
                p.head ? "yes" : "no", p.distance );
        }
        last_log = now;
        last_cached = out.size( );
    }
}

auto clear_pins( ) -> void
{
    std::lock_guard<std::mutex> lk( g_pin_mtx );
    g_pins.clear( );
}

}
