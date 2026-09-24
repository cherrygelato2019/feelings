#include <include/global.cuh>
#include <src/cheat/features/world/world_lighting.cuh>
#include <src/engine/sdk/lighting/lighting.cuh>
#include <src/engine/sdk/sky/sky.cuh>
#include <src/engine/sdk/materialservice/materialservice.cuh>
#include <cmath>
#include <chrono>

namespace world_lighting
{

namespace
{
    status_t g_status{};
    float g_yaw = 0.f;
    float g_pitch = 0.f;
    float g_roll = 0.f;
    auto g_last_tick = std::chrono::steady_clock::now( );

    struct sky_pack_t
    {
        const char* name;
        const char* bk;
        const char* dn;
        const char* ft;
        const char* lf;
        const char* rt;
        const char* up;
    };

    // common public skybox packs (rbxassetid)
    const sky_pack_t k_packs[] = {
        { "keep current", "", "", "", "", "", "" },
        {
            "blue sky",
            "rbxassetid://591058823",
            "rbxassetid://591059876",
            "rbxassetid://591058104",
            "rbxassetid://591057861",
            "rbxassetid://591057625",
            "rbxassetid://591059642",
        },
        {
            "space",
            "rbxassetid://159454299",
            "rbxassetid://159454296",
            "rbxassetid://159454293",
            "rbxassetid://159454286",
            "rbxassetid://159454300",
            "rbxassetid://159454288",
        },
        {
            "nebula",
            "rbxassetid://149397684",
            "rbxassetid://149397700",
            "rbxassetid://149397725",
            "rbxassetid://149397684",
            "rbxassetid://149397725",
            "rbxassetid://149397700",
        },
        {
            "sunset",
            "rbxassetid://600830446",
            "rbxassetid://600831635",
            "rbxassetid://600832720",
            "rbxassetid://600833862",
            "rbxassetid://600835177",
            "rbxassetid://600837233",
        },
        {
            "night city",
            "rbxassetid://12064107",
            "rbxassetid://12064152",
            "rbxassetid://12064121",
            "rbxassetid://12064115",
            "rbxassetid://12064125",
            "rbxassetid://12064131",
        },
    };

    auto color3_from( const float c[4] ) -> math::vector3_t
    {
        return { c[0], c[1], c[2] };
    }

    auto color3_to( const math::vector3_t& v, float out[4] ) -> void
    {
        out[0] = v.x;
        out[1] = v.y;
        out[2] = v.z;
        out[3] = 1.f;
    }

    auto near_eq( float a, float b ) -> bool
    {
        return std::fabs( a - b ) < 0.08f;
    }

    auto wrap360( float v ) -> float
    {
        while ( v >= 360.f )
            v -= 360.f;
        while ( v < 0.f )
            v += 360.f;
        return v;
    }

    auto apply_sky( float dt ) -> void
    {
        const auto sky = sdk::u_sky::resolve( );
        g_status.sky_resolved = ( bool )sky;
        g_status.sky_address = sky ? sky.address( ) : 0;
        if ( !sky )
            return;

        sky.set_celestial_bodies_shown( config.world.skybox_celestial );
        sky.set_star_count( config.world.skybox_stars );

        float sun = config.world.skybox_sun_size;
        float moon = config.world.skybox_moon_size;
        if ( config.world.skybox_pulse_sun )
        {
            const float pulse = 0.65f + 0.35f * ( 0.5f * ( std::sin( g_yaw * 0.045f ) + 1.f ) );
            sun *= pulse;
            moon *= pulse;
        }
        sky.set_sun_angular_size( sun );
        sky.set_moon_angular_size( moon );

        if ( config.world.skybox_anim )
        {
            g_yaw = wrap360( g_yaw + config.world.skybox_spin_yaw * dt );
            g_pitch = wrap360( g_pitch + config.world.skybox_spin_pitch * dt );
            g_roll = wrap360( g_roll + config.world.skybox_spin_roll * dt );
            sky.set_orientation( { g_pitch, g_yaw, g_roll } );
        }

        const auto ori = sky.orientation( );
        g_status.sky_yaw = ori.y;
    }
}

auto status( ) -> status_t
{
    return g_status;
}

auto skybox_preset_names( ) -> const char* const*
{
    static const char* names[] = {
        "keep current", "blue sky", "space", "nebula", "sunset", "night city"
    };
    return names;
}

auto skybox_preset_count( ) -> int
{
    return 6;
}

auto apply( ) -> void
{
    const auto now = std::chrono::steady_clock::now( );
    float dt = std::chrono::duration<float>( now - g_last_tick ).count( );
    g_last_tick = now;
    if ( dt < 0.f || dt > 0.25f )
        dt = 0.016f;

    const bool want_writes = config.world.enabled
        || config.world.skybox_anim
        || config.world.skybox_pulse_sun;
    if ( !want_writes )
    {
        // menu status chips only — don't hammer Lighting/Sky every overlay frame
        static auto last_status = std::chrono::steady_clock::time_point{};
        if ( last_status.time_since_epoch( ).count( ) != 0
            && ( now - last_status ) < std::chrono::milliseconds( 250 ) )
            return;
        last_status = now;

        const auto lighting = sdk::u_lighting::resolve( );
        g_status.resolved = ( bool )lighting;
        g_status.address = lighting ? lighting.address( ) : 0;
        g_status.write_ok = false;
        if ( lighting )
        {
            g_status.live_brightness = lighting.brightness( );
            g_status.live_clock = lighting.clock_time( );
            g_status.live_fog_end = lighting.fog_end( );
        }
        const auto sky = sdk::u_sky::resolve( );
        g_status.sky_resolved = ( bool )sky;
        g_status.sky_address = sky ? sky.address( ) : 0;
        if ( sky )
            g_status.sky_yaw = sky.orientation( ).y;
        const auto mats = sdk::u_material_service::resolve( );
        g_status.materials_resolved = ( bool )mats;
        g_status.materials_address = mats ? mats.address( ) : 0;
        return;
    }

    const auto lighting = sdk::u_lighting::resolve( );
    g_status.resolved = ( bool )lighting;
    g_status.address = lighting ? lighting.address( ) : 0;
    g_status.write_ok = false;

    if ( !lighting )
    {
        g_status.sky_resolved = false;
        // still try MaterialService even if Lighting is missing
        const auto mats = sdk::u_material_service::resolve( );
        g_status.materials_resolved = ( bool )mats;
        g_status.materials_address = mats ? mats.address( ) : 0;
        if ( mats && config.world.enabled && config.world.materials )
            mats.set_use_2022_materials( config.world.use_2022_materials );
        return;
    }

    g_status.live_brightness = lighting.brightness( );
    g_status.live_clock = lighting.clock_time( );
    g_status.live_fog_end = lighting.fog_end( );

    if ( config.world.enabled )
    {
        if ( config.world.lighting )
        {
            lighting.set_brightness( config.world.brightness );
            lighting.set_exposure_compensation( config.world.exposure );
            lighting.set_clock_time( config.world.clock_time );
            lighting.set_geographic_latitude( config.world.geographic_latitude );
            lighting.set_environment_diffuse_scale( config.world.environment_diffuse );
            lighting.set_environment_specular_scale( config.world.environment_specular );
            lighting.set_global_shadows( config.world.global_shadows );
            lighting.set_shadow_softness( config.world.shadow_softness );
            lighting.set_prioritize_lighting_quality( config.world.prioritize_lighting_quality );
        }

        if ( config.world.ambience )
        {
            lighting.set_ambient( color3_from( config.world.ambient ) );
            lighting.set_outdoor_ambient( color3_from( config.world.outdoor_ambient ) );
            lighting.set_color_shift_top( color3_from( config.world.color_shift_top ) );
            lighting.set_color_shift_bottom( color3_from( config.world.color_shift_bottom ) );
            lighting.set_shadow_color( color3_from( config.world.shadow_color ) );
            lighting.set_sky_ambient( color3_from( config.world.sky_ambient ) );
            lighting.set_light_color( color3_from( config.world.light_color ) );
        }

        if ( config.world.weather )
        {
            if ( config.world.strip_atmosphere && lighting.atmosphere( ) )
                lighting.set_atmosphere( 0 );

            lighting.set_fog_color( color3_from( config.world.fog_color ) );
            lighting.set_fog_start( config.world.fog_start );
            lighting.set_fog_end( config.world.fog_end );
        }

        g_status.live_brightness = lighting.brightness( );
        g_status.live_clock = lighting.clock_time( );
        g_status.live_fog_end = lighting.fog_end( );

        if ( config.world.lighting )
            g_status.write_ok = near_eq( g_status.live_brightness, config.world.brightness );
        else if ( config.world.weather )
            g_status.write_ok = near_eq( g_status.live_fog_end, config.world.fog_end );
        else
            g_status.write_ok = true;
    }

    if ( config.world.skybox_anim || config.world.skybox_pulse_sun )
        apply_sky( dt );
    else
    {
        const auto sky = sdk::u_sky::resolve( );
        g_status.sky_resolved = ( bool )sky;
        g_status.sky_address = sky ? sky.address( ) : 0;
        if ( sky )
            g_status.sky_yaw = sky.orientation( ).y;
    }

    // MaterialService Use2022Materials — best-effort offset (see offsets::material_service)
    {
        const auto mats = sdk::u_material_service::resolve( );
        g_status.materials_resolved = ( bool )mats;
        g_status.materials_address = mats ? mats.address( ) : 0;
        if ( mats && config.world.enabled && config.world.materials )
            mats.set_use_2022_materials( config.world.use_2022_materials );
    }
}

auto apply_sky_pack( int id ) -> bool
{
    if ( id <= 0 || id >= ( int )( sizeof( k_packs ) / sizeof( k_packs[0] ) ) )
        return false;

    const auto sky = sdk::u_sky::resolve( );
    if ( !sky )
        return false;

    const sky_pack_t& p = k_packs[id];
    sky.set_skybox_faces( p.bk, p.dn, p.ft, p.lf, p.rt, p.up );
    config.world.skybox_preset = id;
    return true;
}

auto capture( ) -> bool
{
    const auto lighting = sdk::u_lighting::resolve( );
    if ( !lighting )
        return false;

    config.world.brightness = lighting.brightness( );
    config.world.exposure = lighting.exposure_compensation( );
    config.world.clock_time = lighting.clock_time( );
    config.world.geographic_latitude = lighting.geographic_latitude( );
    config.world.environment_diffuse = lighting.environment_diffuse_scale( );
    config.world.environment_specular = lighting.environment_specular_scale( );
    config.world.global_shadows = lighting.global_shadows( );
    config.world.shadow_softness = lighting.shadow_softness( );
    config.world.prioritize_lighting_quality = lighting.prioritize_lighting_quality( );

    color3_to( lighting.ambient( ), config.world.ambient );
    color3_to( lighting.outdoor_ambient( ), config.world.outdoor_ambient );
    color3_to( lighting.color_shift_top( ), config.world.color_shift_top );
    color3_to( lighting.color_shift_bottom( ), config.world.color_shift_bottom );
    color3_to( lighting.shadow_color( ), config.world.shadow_color );
    color3_to( lighting.sky_ambient( ), config.world.sky_ambient );
    color3_to( lighting.light_color( ), config.world.light_color );

    color3_to( lighting.fog_color( ), config.world.fog_color );
    config.world.fog_start = lighting.fog_start( );
    config.world.fog_end = lighting.fog_end( );

    if ( const auto sky = sdk::u_sky::resolve( ) )
    {
        config.world.skybox_sun_size = sky.sun_angular_size( );
        config.world.skybox_moon_size = sky.moon_angular_size( );
        config.world.skybox_stars = sky.star_count( );
        config.world.skybox_celestial = sky.celestial_bodies_shown( );
        const auto ori = sky.orientation( );
        g_pitch = ori.x;
        g_yaw = ori.y;
        g_roll = ori.z;
    }
    return true;
}

auto apply_preset( int id ) -> void
{
    config.world.enabled = true;
    config.world.lighting = true;
    config.world.ambience = true;
    config.world.weather = true;

    switch ( id )
    {
    case 0:
        config.world.brightness = 10.f;
        config.world.exposure = 0.5f;
        config.world.clock_time = 14.f;
        config.world.environment_diffuse = 1.f;
        config.world.environment_specular = 1.f;
        config.world.global_shadows = false;
        for ( int i = 0; i < 3; ++i )
        {
            config.world.ambient[i] = 1.f;
            config.world.outdoor_ambient[i] = 1.f;
            config.world.sky_ambient[i] = 1.f;
            config.world.light_color[i] = 1.f;
        }
        config.world.fog_start = 0.f;
        config.world.fog_end = 100000.f;
        config.world.strip_atmosphere = true;
        break;
    case 1:
        config.world.brightness = 0.4f;
        config.world.exposure = -0.4f;
        config.world.clock_time = 0.f;
        config.world.environment_diffuse = 0.2f;
        config.world.environment_specular = 0.4f;
        config.world.global_shadows = true;
        config.world.ambient[0] = 0.08f; config.world.ambient[1] = 0.09f; config.world.ambient[2] = 0.14f;
        config.world.outdoor_ambient[0] = 0.12f; config.world.outdoor_ambient[1] = 0.14f; config.world.outdoor_ambient[2] = 0.22f;
        config.world.sky_ambient[0] = 0.1f; config.world.sky_ambient[1] = 0.12f; config.world.sky_ambient[2] = 0.2f;
        config.world.light_color[0] = 0.35f; config.world.light_color[1] = 0.4f; config.world.light_color[2] = 0.7f;
        config.world.fog_color[0] = 0.05f; config.world.fog_color[1] = 0.06f; config.world.fog_color[2] = 0.1f;
        config.world.fog_start = 40.f;
        config.world.fog_end = 420.f;
        config.world.strip_atmosphere = true;
        break;
    case 2:
        config.world.brightness = 1.6f;
        config.world.exposure = 0.15f;
        config.world.clock_time = 18.2f;
        config.world.environment_diffuse = 0.7f;
        config.world.environment_specular = 0.85f;
        config.world.global_shadows = true;
        config.world.ambient[0] = 0.18f; config.world.ambient[1] = 0.1f; config.world.ambient[2] = 0.08f;
        config.world.outdoor_ambient[0] = 1.f; config.world.outdoor_ambient[1] = 0.55f; config.world.outdoor_ambient[2] = 0.25f;
        config.world.color_shift_top[0] = 1.f; config.world.color_shift_top[1] = 0.45f; config.world.color_shift_top[2] = 0.15f;
        config.world.color_shift_bottom[0] = 0.2f; config.world.color_shift_bottom[1] = 0.1f; config.world.color_shift_bottom[2] = 0.35f;
        config.world.sky_ambient[0] = 1.f; config.world.sky_ambient[1] = 0.5f; config.world.sky_ambient[2] = 0.25f;
        config.world.light_color[0] = 1.f; config.world.light_color[1] = 0.55f; config.world.light_color[2] = 0.2f;
        config.world.fog_color[0] = 0.85f; config.world.fog_color[1] = 0.45f; config.world.fog_color[2] = 0.25f;
        config.world.fog_start = 80.f;
        config.world.fog_end = 900.f;
        config.world.strip_atmosphere = true;
        break;
    case 3:
        config.world.brightness = 1.1f;
        config.world.exposure = -0.1f;
        config.world.clock_time = 10.f;
        config.world.environment_diffuse = 0.5f;
        config.world.environment_specular = 0.3f;
        config.world.global_shadows = true;
        config.world.ambient[0] = 0.35f; config.world.ambient[1] = 0.38f; config.world.ambient[2] = 0.4f;
        config.world.outdoor_ambient[0] = 0.55f; config.world.outdoor_ambient[1] = 0.58f; config.world.outdoor_ambient[2] = 0.6f;
        config.world.sky_ambient[0] = 0.65f; config.world.sky_ambient[1] = 0.68f; config.world.sky_ambient[2] = 0.7f;
        config.world.light_color[0] = 0.8f; config.world.light_color[1] = 0.8f; config.world.light_color[2] = 0.85f;
        config.world.fog_color[0] = 0.7f; config.world.fog_color[1] = 0.72f; config.world.fog_color[2] = 0.75f;
        config.world.fog_start = 8.f;
        config.world.fog_end = 120.f;
        config.world.strip_atmosphere = true;
        break;
    default:
        break;
    }
}

}
