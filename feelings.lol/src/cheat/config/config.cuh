#pragma once

#include <windows.h>

struct config_t
{
    bool menu_open = false;
    int menu_key = VK_F1;

    struct
    {
        bool vsync = false;
        int fps_cap = 144;
        bool low_power = false;
        bool watermark = true;
        bool streamproof = false;
        bool hide_console = false;
        bool players_list = true;
        bool feelings_fm = true;
        bool auto_rescan = true;
        bool notifications = true;
        bool hitsounds = false;
        int hitsound = 4;
        float hitsound_volume = 1.f;
    } settings;

    struct
    {
        int decompiler_backend = 0; // 0 lunaux, 1 konstant
    } explorer;

    struct
    {
        int preset = 0;
        float accent[4] = { 0.596f, 0.588f, 0.839f, 1.f };
        float secondary[4] = { 0.341f, 0.341f, 0.612f, 1.f };
        float background[4] = { 0.f, 0.f, 0.f, 0.80f };
        float child_bg[4] = { 0.047f, 0.047f, 0.047f, 1.f };
        float border[4] = { 0.208f, 0.196f, 0.267f, 1.f };
        float text[4] = { 1.f, 1.f, 1.f, 1.f };
        float frame_bg[4] = { 0.173f, 0.173f, 0.173f, 1.f };
        float opacity = 0.80f;
        float ui_scale = 1.f;
        bool animations = true;
    } theme;

    struct
    {
        bool enabled = false;
        int key = VK_RBUTTON;
        int key_mode = 0;

        int method = 0;
        int aim_part = 0;
        int target_mode = 0;
        int style = 0;

        bool sticky = true;
        bool prediction = false;
        bool humanize = false;

        float smoothing = 5.f;
        float speed = 1.f;
        float range = 150.f;
        float max_distance = 1000.f;

        float prediction_xz = 1.f;
        float prediction_y = 1.01f;

        bool team_check = false;
        bool friend_check = false;
        bool wall_check = false;
        bool wallbang = false;
        bool npc_check = true;

        bool cam_force_scriptable = true;
        bool cam_restore_type = true;
    } aimbot;

    struct
    {
        bool enabled = false;
        int key = VK_LBUTTON;
        int key_mode = 2;

        int method = 0; // 0 raycast, 1 viewport, 2 mouse.hit
        int aim_part = 0;
        int target_mode = 0;

        bool sticky = true;
        bool prediction = false;

        float range = 200.f;
        float max_distance = 1000.f;
        float restore_delay_ms = 35.f;

        bool team_check = false;
        bool friend_check = false;
        bool wall_check = false;
        bool wallbang = false;
        bool npc_check = false;

        bool closest_point = true;
        float closest_point_scale = 0.85f;
        bool magic_bullet = false;
        int redirect_mode = 0;
        bool no_spread = true;
        bool spectate = false;

        bool fov_visualizer = false;
        bool fov_filled = false;
        bool fov_outline = true;
        float fov_thickness = 1.5f;
        float fov_color[4] = { 1.f, 1.f, 1.f, 0.55f };

        bool tracer = false;
        bool target_dot = true;
        float target_dot_radius = 3.f;
        float target_dot_color[4] = { 1.f, 0.25f, 0.25f, 1.f };
    } silentaim;

    struct
    {
        bool enabled = false;
        int key = VK_XBUTTON2;
        int key_mode = 0;

        int aim_part = 0;
        float delay_ms = 75.f;
        float click_ms = 50.f;
        float hitchance = 100.f;
        float max_distance = 1000.f;

        bool prediction = false;
        bool team_check = true;
        bool friend_check = false;
        bool wall_check = false;
        bool wallbang = false;
        bool npc_check = true;
    } triggerbot;

    struct
    {
        bool enabled = true;
        bool team_check = true;
        bool friend_check = false;
        bool wall_check = false;
        bool npc_check = true;
        float max_distance = 1000.f;

        float color[4] = { 1.f, 1.f, 1.f, 1.f };
        float color_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool box = true;
        float box_thickness = 1.f;
        bool box_outline = true;
        bool box_filled = false;
        float box_fill_color[4] = { 0.f, 0.f, 0.f, 0.4f };
        float box_fill_color2[4] = { 0.34f, 0.34f, 0.61f, 0.35f };
        float box_fill_rotate = 90.f;
        bool box_fill_auto_rotate = false;
        float box_fill_rotate_speed = 60.f;
        int box_type = 0;
        int box_outline_mode = 2;

        int outline_flags[6] = { 1, 1, 1, 1, 1, 1 };

        bool display_name = true;
        float display_color[4] = { 1.f, 1.f, 1.f, 1.f };
        float display_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool username = false;
        float username_color[4] = { 0.85f, 0.85f, 0.85f, 1.f };
        float username_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool distance = false;
        float distance_color[4] = { 1.f, 1.f, 1.f, 1.f };
        float distance_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool tool = false;
        float tool_color[4] = { 0.85f, 0.85f, 1.f, 1.f };
        float tool_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool platform = true;
        float platform_color[4] = { 0.75f, 0.85f, 1.f, 1.f };
        float platform_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool state = true;
        float state_color[4] = { 1.f, 0.85f, 0.45f, 1.f };
        float state_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool team = false;
        float team_color[4] = { 1.f, 0.85f, 0.45f, 1.f };
        float team_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool health_text = true;
        float health_text_color[4] = { 1.f, 1.f, 1.f, 1.f };
        float health_text_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool health = true;
        int health_side = 0;
        float health_width = 3.f;
        float health_gap = 3.f;

        float health_top[4] = { 0.f, 1.f, 0.f, 1.f };
        float health_mid[4] = { 1.f, 1.f, 0.f, 1.f };
        float health_bottom[4] = { 1.f, 0.15f, 0.15f, 1.f };
        float health_outline[4] = { 0.f, 0.f, 0.f, 1.f };
        float health_bg[4] = { 0.12f, 0.12f, 0.12f, 0.78f };

        float health_color[4] = { 0.f, 1.f, 0.f, 1.f };
        float health_color2[4] = { 1.f, 0.15f, 0.15f, 1.f };

        bool armor = false;
        bool armor_text = true;
        int armor_side = -1;
        float armor_top[4] = { 0.45f, 0.75f, 1.f, 1.f };
        float armor_mid[4] = { 0.2f, 0.45f, 0.95f, 1.f };
        float armor_bottom[4] = { 0.05f, 0.2f, 0.7f, 1.f };
        float armor_outline[4] = { 0.f, 0.f, 0.f, 1.f };
        float armor_bg[4] = { 0.12f, 0.12f, 0.12f, 0.78f };
        float armor_text_color[4] = { 1.f, 1.f, 1.f, 1.f };
        float armor_text_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool ammo = false;
        bool ammo_text = true;
        int ammo_side = -1;
        float ammo_top[4] = { 1.f, 0.85f, 0.25f, 1.f };
        float ammo_mid[4] = { 1.f, 0.55f, 0.1f, 1.f };
        float ammo_bottom[4] = { 0.85f, 0.2f, 0.05f, 1.f };
        float ammo_outline[4] = { 0.f, 0.f, 0.f, 1.f };
        float ammo_bg[4] = { 0.12f, 0.12f, 0.12f, 0.78f };
        float ammo_text_color[4] = { 1.f, 1.f, 1.f, 1.f };
        float ammo_text_outline[4] = { 0.f, 0.f, 0.f, 1.f };

        bool tracers = false;
        float tracer_color[4] = { 1.f, 1.f, 1.f, 0.55f };
        float tracer_outline[4] = { 0.f, 0.f, 0.f, 0.7f };
        int tracer_origin = 0;

        bool target_strafe = false;
        float target_strafe_color[4] = { 1.f, 0.35f, 0.55f, 0.85f };

        bool glow = false;
        float glow_color[4] = { 1.f, 1.f, 1.f, 0.55f };
        float glow_outline[4] = { 1.f, 1.f, 1.f, 0.2f };
        float glow_size = 18.f;

        bool skeleton = false;
        float skeleton_color[4] = { 1.f, 1.f, 1.f, 1.f };
        float skeleton_outline[4] = { 0.f, 0.f, 0.f, 1.f };
        float skeleton_thickness = 2.0f;
        bool skeleton_head_dot = true;

        bool chams = false;
        float chams_color[4] = { 1.f, 0.22f, 0.28f, 0.55f };
        float chams_outline[4] = { 0.72f, 0.10f, 0.16f, 0.90f };
        float chams_thickness = 1.2f;
        float chams_fill_transparency = 0.42f;
        float chams_outline_transparency = 0.15f;
        int chams_kind = 0; // 0 mesh, 1 instance, 2 native, 3 wireframe
        int chams_style = 0; // 0 glass, 1 xqz, 2 rainbow, 3 wire
        float chams_hidden[4] = { 1.f, 0.08f, 0.12f, 0.55f };
        float chams_visible[4] = { 0.15f, 1.f, 0.42f, 0.72f };
        float chams_rainbow_speed = 0.85f;
        bool chams_highlight = false;
        bool world_chams = false;

        bool sound_esp = false;
        float sound_color[4] = { 1.f, 0.78f, 0.18f, 0.95f };
        float sound_outline[4] = { 0.f, 0.f, 0.f, 1.f };
        float sound_max_distance = 250.f;

        bool text_outline = true;
        bool text_dropshadow = false;
        int text_outline_style = 1;

        // Per-element anti-aliasing (visuals → customization)
        bool aa_box = false;
        bool aa_skeleton = true;
        bool aa_tracers = false;
        bool aa_glow = false;
        bool aa_chams = true;
        bool aa_text = false;
        bool aa_health = false;
        bool aa_target_strafe = false;

        int font = 0;
        float font_size = 13.f;

        auto outline_box( ) const -> bool { return outline_flags[0] != 0; }
        auto outline_text( ) const -> bool { return outline_flags[1] != 0; }
        auto outline_health( ) const -> bool { return outline_flags[2] != 0; }
        auto outline_skeleton( ) const -> bool { return outline_flags[3] != 0; }
        auto outline_tracers( ) const -> bool { return outline_flags[4] != 0; }
        auto outline_meshchams( ) const -> bool { return outline_flags[5] != 0; }
    } esp;

    struct
    {
        bool enabled = false;
        bool lighting = true;
        bool ambience = true;
        bool weather = true;

        float brightness = 2.f;
        float exposure = 0.f;
        float clock_time = 14.f;
        float geographic_latitude = 41.733f;
        float environment_diffuse = 1.f;
        float environment_specular = 1.f;
        bool global_shadows = true;
        float shadow_softness = 0.2f;
        bool prioritize_lighting_quality = true;

        float ambient[4] = { 0.f, 0.f, 0.f, 1.f };
        float outdoor_ambient[4] = { 0.5f, 0.5f, 0.5f, 1.f };
        float color_shift_top[4] = { 0.f, 0.f, 0.f, 1.f };
        float color_shift_bottom[4] = { 0.f, 0.f, 0.f, 1.f };
        float shadow_color[4] = { 0.f, 0.f, 0.f, 1.f };
        float sky_ambient[4] = { 1.f, 1.f, 1.f, 1.f };
        float light_color[4] = { 1.f, 1.f, 1.f, 1.f };

        float fog_color[4] = { 0.75f, 0.75f, 0.75f, 1.f };
        float fog_start = 0.f;
        float fog_end = 100000.f;
        bool strip_atmosphere = true;

        bool skybox_anim = false;
        int skybox_preset = 0;
        float skybox_spin_yaw = 12.f;
        float skybox_spin_pitch = 0.f;
        float skybox_spin_roll = 0.f;
        bool skybox_pulse_sun = false;
        float skybox_sun_size = 11.f;
        float skybox_moon_size = 11.f;
        int skybox_stars = 3000;
        bool skybox_celestial = true;

        bool materials = false;
        bool use_2022_materials = true;
    } world;

    struct
    {

        bool hip_height = false;
        float hip_height_value = 2.f;
        int hip_height_key = 0;
        int hip_height_key_mode = 2;

        bool fly = false;
        int fly_method = 1;
        float fly_speed = 50.f;
        int fly_key = 0;
        int fly_key_mode = 2;

        bool speed = false;
        int speed_method = 0;
        float walk_speed = 16.f;
        int speed_key = 0;
        int speed_key_mode = 2;

        bool gravity = false;
        float gravity_value = 196.2f;
        int gravity_key = 0;
        int gravity_key_mode = 2;

        bool jump = false;
        float jump_power = 50.f;
        int jump_key = 0;
        int jump_key_mode = 2;

        bool infinite_jump = false;
        float infinite_jump_power = 50.f;
        int infinite_jump_key = 0;
        int infinite_jump_key_mode = 2;
        bool no_jump_cooldown = false;

        bool spin = false;
        float spin_speed = 15.f;
        int spin_key = 0;
        int spin_key_mode = 2;

        bool noclip = false;
        int noclip_key = 0;
        int noclip_key_mode = 2;

        bool sit = false;
        int sit_key = 0;
        int sit_key_mode = 2;

        bool target_strafe = false;
        float target_strafe_speed = 15.f;
        float target_strafe_radius = 10.f;
        float target_strafe_height = 0.f;
        bool target_strafe_from_aim = false;
        int target_strafe_key = 0;
        int target_strafe_key_mode = 2;

        bool rage_tp = false;
        float rage_tp_depth = 3.f;
        float rage_tp_under = 15.f;
        int rage_tp_key = 0;
        int rage_tp_key_mode = 0;

        bool teleport = false;
        int teleport_method = 0;
        float teleport_distance = 50.f;
        float teleport_height = 3.f;
        int teleport_key = 0;
        int teleport_key_mode = 0;
        int teleport_save_key = 0;
        char waypoint_name[32] = "waypoint";

        bool animation_changer = false;
        int animation_preset = 0;

        bool emote_changer = false;
        int emote_preset = 0;

        bool face_changer = false;
        int face_preset = 0;
        char face_id[64] = "2409285798";
        bool clothing_changer = false;
        char shirt_id[64] = "";
        char pants_id[64] = "";
        char tshirt_id[64] = "";

        bool hitbox_expander = false;
        bool hitbox_visualizer = true;
        bool hitbox_root_only = true;
        bool hitbox_team_check = true;
        bool hitbox_npc_check = true;
        float hitbox_size_x = 10.f;
        float hitbox_size_y = 10.f;
        float hitbox_size_z = 10.f;
        float hitbox_color[4] = { 1.f, 0.35f, 0.35f, 0.85f };

        bool state_force = false;
        int state_force_id = 16;

        bool fling = false;
        float fling_power = 500.f;
        int fling_key = 0;
        int fling_key_mode = 0;

        bool no_fall = false;
        int no_fall_key = 0;
        int no_fall_key_mode = 2;

        bool antiaim = false;
        float antiaim_jitter = 3.f;
        int antiaim_key = 0;
        int antiaim_key_mode = 2;

        bool desync = false;
        int desync_method = 0; // 0 break, 1 freeze, 2 yaw
        float desync_amount = 8.f;
        int desync_key = 0;
        int desync_key_mode = 2;

        bool freeze_gc = false;

        bool freecam = false;
        float freecam_speed = 50.f;
        int freecam_key = 0;
        int freecam_key_mode = 0;

        bool antiafk = false;
        int antiafk_key = 0;
        int antiafk_key_mode = 2;

        int antiaim_pitch = 1; // 0 none, 1 down, 2 up, 3 jitter
        int antiaim_yaw = 1;   // 0 none, 1 backward, 2 spin, 3 jitter, 4 left, 5 right
        float antiaim_spin_speed = 12.f;
        bool antiaim_at_targets = false;
        bool antiaim_arrows = true;

        bool desync_random = false;
        bool desync_on_peek = false;

        bool fake_lag = false;
        int fake_lag_key = 0;
        int fake_lag_key_mode = 2;
        float fake_lag_hold_ms = 180.f;
        float fake_lag_amount = 8.f;
    } player;

    // appended — old .flcfg files stay aligned if we only grow here
    bool autosave = false;
};

extern config_t config;
