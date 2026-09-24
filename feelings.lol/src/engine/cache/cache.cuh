#pragma once

#include <src/engine/math/math.cuh>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace cache
{

struct player
{
    std::string name;
    std::string display_name;
    std::string tool;
    std::string team_name;
    std::string platform;
    std::string state; 
    int state_id = -1;
    uintptr_t address = 0;
    int64_t user_id = 0;
    bool is_npc = false;
    bool teammate = false;
    int rig_type = -1;

    uintptr_t team = 0;
    uintptr_t character = 0;
    uintptr_t humanoid = 0;
    uintptr_t root_part = 0;
    uintptr_t head = 0;
    uintptr_t health_nv = 0;
    uintptr_t max_health_nv = 0;
    uintptr_t armor_nv = 0;
    uintptr_t max_armor_nv = 0;
    uintptr_t ammo_nv = 0;
    uintptr_t max_ammo_nv = 0;

    float health = 0.f;
    float max_health = 0.f;
    float armor = 0.f;
    float max_armor = 0.f;
    float ammo = 0.f;
    float max_ammo = 0.f;
    float distance = 0.f;
    bool hearing = false;
    std::string hear_name;
    float box_min_x = 0.f;
    float box_min_y = 0.f;
    float box_max_x = 0.f;
    float box_max_y = 0.f;
    bool box_valid = false;
    uint64_t pose_qpc = 0;

    // ESP / aimbot team check — Team instance, PF ally (team==1), or teammate flag.
    auto same_team( uintptr_t client_team ) const -> bool
    {
        if ( teammate )
            return true;
        // Phantom Forces marks allies as team == 1 (not a Team instance pointer)
        if ( team == 1 )
            return true;
        if ( client_team != 0 && team != 0 && team == client_team )
            return true;
        return false;
    }

    auto is_ally( uintptr_t client_team, const std::string& local_team_name ) const -> bool
    {
        return same_team( client_team ) || same_team_name( local_team_name );
    }

    auto same_team_name( const std::string& local_team_name ) const -> bool
    {
        if ( local_team_name.empty( ) || team_name.empty( ) )
            return false;
        if ( team_name.size( ) != local_team_name.size( ) )
            return false;
        for ( size_t i = 0; i < team_name.size( ); ++i )
        {
            char a = team_name[i];
            char b = local_team_name[i];
            if ( a >= 'A' && a <= 'Z' ) a = ( char )( a + 32 );
            if ( b >= 'A' && b <= 'Z' ) b = ( char )( b + 32 );
            if ( a != b )
                return false;
        }
        return true;
    }

    struct bone
    {
        std::string name;
        std::string mesh_id;
        uintptr_t part = 0;
        uintptr_t primitive = 0;
        math::vector3_t size;
        math::vector3_t position;
        math::matrix3_t rotation;
        math::vector3_t velocity;
    };

    std::vector<bone> bones;
};

// Humanoid.RigType: 0 = R6, 1 = R15. Used by skeleton / box whitelist.
inline auto is_r15_bone_name( const char* n ) -> bool
{
    return std::strcmp( n, "Head" ) == 0
        || std::strcmp( n, "UpperTorso" ) == 0
        || std::strcmp( n, "LowerTorso" ) == 0
        || std::strcmp( n, "LeftUpperArm" ) == 0
        || std::strcmp( n, "LeftLowerArm" ) == 0
        || std::strcmp( n, "LeftHand" ) == 0
        || std::strcmp( n, "RightUpperArm" ) == 0
        || std::strcmp( n, "RightLowerArm" ) == 0
        || std::strcmp( n, "RightHand" ) == 0
        || std::strcmp( n, "LeftUpperLeg" ) == 0
        || std::strcmp( n, "LeftLowerLeg" ) == 0
        || std::strcmp( n, "LeftFoot" ) == 0
        || std::strcmp( n, "RightUpperLeg" ) == 0
        || std::strcmp( n, "RightLowerLeg" ) == 0
        || std::strcmp( n, "RightFoot" ) == 0;
}

inline auto is_r6_bone_name( const char* n ) -> bool
{
    return std::strcmp( n, "Torso" ) == 0
        || std::strcmp( n, "Left Arm" ) == 0
        || std::strcmp( n, "Right Arm" ) == 0
        || std::strcmp( n, "Left Leg" ) == 0
        || std::strcmp( n, "Right Leg" ) == 0
        || std::strcmp( n, "LeftArm" ) == 0
        || std::strcmp( n, "RightArm" ) == 0
        || std::strcmp( n, "LeftLeg" ) == 0
        || std::strcmp( n, "RightLeg" ) == 0;
}

inline auto is_hrp_name( const std::string& name ) -> bool
{
    return name == "HumanoidRootPart" || name == "humanoidrootpart"
        || name == "HRP" || name == "Hrp" || name == "RootPart";
}

inline auto is_bounds_bone_name( const std::string& name, int rig_type ) -> bool
{
    if ( name.empty( ) || name.rfind( "Acc:", 0 ) == 0 )
        return false;
    if ( is_hrp_name( name ) || name == "COMPart" || name == "Handle"
        || name.rfind( "Hitbox", 0 ) == 0 )
        return false;
    if ( name == "Head" || name == "head" )
        return true;

    const char* n = name.c_str( );
    if ( rig_type == 1 )
        return is_r15_bone_name( n );
    if ( rig_type == 0 )
        return is_r6_bone_name( n );
    return is_r6_bone_name( n ) || is_r15_bone_name( n )
        || name == "Abdomen" || name == "torso";
}

inline auto is_collect_bone_name( const std::string& name, bool want_accessories ) -> bool
{
    if ( name.rfind( "Acc:", 0 ) == 0 )
        return want_accessories;
    if ( name == "HumanoidRootPart" || name == "Abdomen" || name == "Head" )
        return true;
    const char* n = name.c_str( );
    return is_r6_bone_name( n ) || is_r15_bone_name( n );
}

struct world_part
{
    math::vector3_t min{};
    math::vector3_t max{};
    math::vector3_t center{};
    math::matrix3_t rotation{};
    math::vector3_t half{};
    uintptr_t part = 0;
    uintptr_t primitive = 0;
    bool obb = false;
    bool query = false;
};

} 

class cache_c
{
public:
    auto get_players( ) -> void;
    auto refresh_world( ) -> void;
    auto tick( ) -> void;
    auto transform( ) -> void;
    auto invalidate_character( uintptr_t character ) -> void;

    std::shared_ptr<std::vector<cache::player>> players = std::make_shared<std::vector<cache::player>>( );
    std::shared_ptr<std::vector<cache::world_part>> world = std::make_shared<std::vector<cache::world_part>>( );

    std::mutex swap_mutex;
    std::mutex world_mutex;
    std::mutex stale_mutex;
    std::unordered_set<uintptr_t> stale_characters;
    std::atomic<bool> force_players_refresh{ false };
    // Local HumanoidRootPart primitive — transform uses this for studs (never camera).
    std::atomic<uintptr_t> local_root_prim{ 0 };

    struct ve_cache_t
    {
        math::matrix4_t viewmatrix{};
        math::vector2_t ve{};
        math::vector2_t draw{};
        math::vector2_t offset{};
        float sx = 1.f;
        float sy = 1.f;
        bool ready = false;
    };
    std::mutex ve_mutex;
    ve_cache_t ve_cache{};

    auto snapshot_ve( ) -> ve_cache_t
    {
        std::lock_guard<std::mutex> lock( ve_mutex );
        return ve_cache;
    }

    // View + players from the same transform tick. Draw must not read a live camera.
    ve_cache_t pose_ve{};
    struct draw_snap_t
    {
        std::shared_ptr<std::vector<cache::player>> players;
        ve_cache_t ve{};
    };
    auto snapshot_draw( ) -> draw_snap_t
    {
        std::lock_guard<std::mutex> lock( swap_mutex );
        return {
            players ? players : std::make_shared<std::vector<cache::player>>( ),
            pose_ve
        };
    }

    // Thread-safe snapshot — pointer copy only. Transform publishes a new vector;
    // draw never clones bones and never RPMs.
    auto snapshot_players( ) -> std::shared_ptr<std::vector<cache::player>>
    {
        std::lock_guard<std::mutex> lock( swap_mutex );
        return players ? players : std::make_shared<std::vector<cache::player>>( );
    }

    auto snapshot_world( ) -> std::shared_ptr<std::vector<cache::world_part>>
    {
        std::lock_guard<std::mutex> lock( world_mutex );
        return world ? world : std::make_shared<std::vector<cache::world_part>>( );
    }
};
