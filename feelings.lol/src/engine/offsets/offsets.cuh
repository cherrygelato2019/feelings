#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace offsets
{

inline std::string client_version = "version-2366ba214ec740ca";

namespace instance
{
    inline constexpr uintptr_t class_descriptor = 0x18;
    inline constexpr uintptr_t class_name = 0x8;
    inline constexpr uintptr_t name = 0x8;
    inline constexpr uintptr_t name_container = 0x70;
    inline constexpr uintptr_t parent = 0x68;
    inline constexpr uintptr_t children_start = 0x78;
    inline constexpr uintptr_t children_end = 0x8;
}

namespace datamodel
{
    inline constexpr uintptr_t workspace = 0x150;
    inline constexpr uintptr_t place_id = 0x188;
    inline constexpr uintptr_t job_id = 0x110;
    inline constexpr uintptr_t loaded = 0x5d0;
    inline constexpr uintptr_t creator_id = 0x178;
    inline constexpr uintptr_t game_id = 0x180;
    inline constexpr uintptr_t script_context = 0x440;
}

namespace lua_state
{
    inline uintptr_t top = 0x20;
    inline uintptr_t base = 0x18;
    inline uintptr_t global = 0x28;
}

namespace luau_global
{
    inline uintptr_t strt_hash = 0x00;
    inline uintptr_t strt_nuse = 0x08;
    inline uintptr_t strt_size = 0x0c;
    inline uintptr_t gcstate = 0x21;
    inline uintptr_t gray = 0x28;
    inline uintptr_t grayagain = 0x30;
    inline uintptr_t weak = 0x38;
    inline uintptr_t GCthreshold = 0x40;
    inline uintptr_t totalbytes = 0x48;
    inline uintptr_t gcgoal = 0x50;
    inline uintptr_t gcstepmul = 0x54;
    inline uintptr_t allgcopages = 0;
    inline uintptr_t page_next = 0x08;
    inline constexpr uintptr_t page_size = 0x20;
    inline constexpr uintptr_t page_block = 0x24;
    inline constexpr uintptr_t page_data = 0x40;
}

namespace luau_object
{
    inline constexpr uintptr_t tt = 0x01;
    inline constexpr uintptr_t gclist = 0x08;
    inline constexpr uintptr_t table_lsz = 0x03;
    inline constexpr uintptr_t table_sizearray = 0x08;
    inline constexpr uintptr_t table_node = 0x18;
    inline constexpr uintptr_t table_array = 0x20;
    inline constexpr uintptr_t table_gclist = 0x28;
    inline constexpr uintptr_t ts_next = 0x08;
    inline constexpr uintptr_t ts_hash = 0x10;
    inline constexpr uintptr_t ts_len = 0x14;
    inline constexpr uintptr_t ts_data = 0x18;
    inline constexpr int tt_string = 6;
    inline constexpr int tt_table = 7;
    inline constexpr int node_stride = 32;
    inline constexpr int node_val_tt = 12;
    inline constexpr int node_key_gc = 16;
    inline constexpr int node_key_tt = 28;
}

namespace fake_datamodel
{
    inline constexpr uintptr_t pointer = 0x8ee1728;
    inline constexpr uintptr_t real_datamodel = 0x1f8;
}

namespace workspace
{
    inline constexpr uintptr_t current_camera = 0x4a8;
    inline constexpr uintptr_t distributed_game_time = 0x4c8;
    inline constexpr uintptr_t gravity = 0x9f0;
    inline constexpr uintptr_t world = 0x400;
}

namespace world
{
    inline constexpr uintptr_t gravity = 0x22c;
    inline constexpr uintptr_t primitives = 0x2b0;
}

namespace players
{
    inline constexpr uintptr_t local_player = 0x120;
}

namespace friend_service
{
    inline constexpr uintptr_t sentinel = 0x278;
    inline constexpr uintptr_t bucket_base = 0x288;
    inline constexpr uintptr_t mask = 0x2a0;
}

namespace player
{

    inline constexpr uintptr_t display_name = 0x128;
    inline constexpr uintptr_t model_instance = 0x288;
    inline constexpr uintptr_t team = 0x2c8;
    inline constexpr uintptr_t user_id = 0xc0;
    inline constexpr uintptr_t locale_id = 0x108;
    inline constexpr uintptr_t mouse = 0x1200;

    inline uintptr_t os_platform = 0;
    inline uintptr_t platform_name = 0;
}

namespace mouse
{
    inline constexpr uintptr_t prop = 0xb0;
}

namespace rva
{
    inline constexpr uintptr_t intern_hit = 0x6E01B88;
    inline constexpr uintptr_t intern_origin = 0x6E01BA4;
    inline constexpr uintptr_t intern_unitray = 0x6E01BC0;
    inline constexpr uintptr_t intern_target = 0x6DF3D6C;
    inline constexpr uintptr_t intern_targetfilter = 0x6E01BB0;
    inline constexpr uintptr_t intern_viewport = 0x6E117C0;
    inline constexpr uintptr_t intern_screen = 0x6E11798;
    inline constexpr uintptr_t intern_getmouse = 0x6E04198;
    inline constexpr uintptr_t intern_getmouseloc = 0x6E01698;
    inline constexpr uintptr_t intern_getmousedelta = 0x6E01688;
    inline constexpr uintptr_t intern_mouseservice = 0x6E01B78;
    inline constexpr uintptr_t intern_inputobject = 0x6E01CF0;
    inline constexpr uintptr_t intern_delta = 0x6E01CBC;
    inline constexpr uintptr_t intern_uis = 0x6E01A18;
    inline constexpr uintptr_t intern_highlight = 0x6E0F608;
    inline constexpr uintptr_t intern_playback_loudness = 0x6E2DC48;
    inline constexpr uintptr_t intern_sound_id = 0x6E2DCC0;
    inline constexpr uintptr_t intern_is_playing = 0x6DF63F0;
    inline constexpr uintptr_t intern_mouse = 0x6E01C80;
    inline constexpr uintptr_t intern_playermouse = 0x6E05128;
    inline constexpr uintptr_t intern_camera = 0x6E02FBC;
    inline constexpr uintptr_t intern_workspace = 0x6E071D0;
    inline constexpr uintptr_t intern_worldroot = 0x6E276E0;
    inline constexpr uintptr_t intern_raycast = 0x6E27590;
    inline constexpr uintptr_t intern_raycast_params = 0x6E275D0;
    inline constexpr uintptr_t intern_raycast_result = 0x6E49BB8;
    inline constexpr uintptr_t intern_ignore_water = 0x6E27538;
    inline constexpr uintptr_t intern_blockcast = 0x6E27508;
    inline constexpr uintptr_t intern_spherecast = 0x6E27838;
    inline constexpr uintptr_t intern_findpart = 0x6E27750;
    inline constexpr uintptr_t intern_shapecast = 0x6E278A0;
    inline constexpr uintptr_t playermouse_cd = 0x8945AF0;
    inline constexpr uintptr_t raycast_orig = 0xED53A0;
    inline constexpr uintptr_t raycast_bound_ptr = 0x8217318;
    inline constexpr uintptr_t raycast_bound_fn_store = 0x8217320;
    inline constexpr uintptr_t blockcast_bound_ptr = 0x8216B58;
    inline constexpr uintptr_t spherecast_bound_ptr = 0x8217748;
    inline constexpr uintptr_t shapecast_bound_ptr = 0x8217528;
    inline constexpr uintptr_t raycast_bound_cluster = 0x8217180;
    inline constexpr size_t raycast_bound_cluster_bytes = 0x200;
}

namespace winsta
{
    inline constexpr uintptr_t cave = 0x1000;
}

namespace misc
{
    inline constexpr uintptr_t value = 0xa8;
    inline constexpr uintptr_t adornee = 0xe0;
    inline constexpr uintptr_t string_length = 0x10;
}

namespace places
{
    inline constexpr int64_t phantom_forces_a = 292439477;
    inline constexpr int64_t phantom_forces_b = 113491250;
    inline constexpr int64_t bad_business_a = 3233893879;
    inline constexpr int64_t bad_business_b = 1168263273;
    inline constexpr int64_t frontlines = 5938036553;
    inline constexpr int64_t overkill = 124842176624983LL;
    inline constexpr int64_t arsenal = 863266079;
    inline constexpr int64_t deadline = 119426593006199LL;
    inline constexpr int64_t havoc = 16530963934LL;
    inline constexpr int64_t bloxstrike = 114234929420007LL;
    inline constexpr int64_t rivals = 5995470825LL;
    inline constexpr int64_t multiversal = 15259246552LL;
}

namespace humanoid
{
    inline constexpr uintptr_t health = 0x180;
    inline constexpr uintptr_t max_health = 0x198;
    inline constexpr uintptr_t walk_speed = 0x1c0;
    inline constexpr uintptr_t walk_speed_check = 0x39c;
    inline constexpr uintptr_t jump_power = 0x194;
    inline constexpr uintptr_t jump_height = 0x190;
    inline constexpr uintptr_t use_jump_power = 0x1d0;
    inline constexpr uintptr_t hip_height = 0x184;
    inline constexpr uintptr_t root_part = 0x458;

    inline constexpr uintptr_t display_name = 0xa8;
    inline constexpr uintptr_t rig_type = 0x1b0;
    inline constexpr uintptr_t move_direction = 0x130;
    inline constexpr uintptr_t platform_stand = 0x1cc;
    inline constexpr uintptr_t sit = 0x1cd;
    inline constexpr uintptr_t jump_bool = 0x1ca;
    inline constexpr uintptr_t walk_timer = 0x3c8;
    inline constexpr uintptr_t move_to_point = 0x154;
    inline constexpr uintptr_t target_point = 0x13c;
    inline constexpr uintptr_t move_to_part = 0x108;
    inline constexpr uintptr_t is_walking = 0xa1f;

    inline constexpr uintptr_t state = 0x8a0;
    inline constexpr uintptr_t state_id = 0x20;
}

namespace humanoid_state
{
    inline constexpr int falling_down = 0;
    inline constexpr int ragdoll = 1;
    inline constexpr int getting_up = 2;
    inline constexpr int jumping = 3;
    inline constexpr int swimming = 4;
    inline constexpr int freefall = 5;
    inline constexpr int flying = 6;
    inline constexpr int landed = 7;
    inline constexpr int running = 8;
    inline constexpr int running_no_physics = 10;
    inline constexpr int strafing_no_physics = 11;
    inline constexpr int climbing = 12;
    inline constexpr int seated = 13;
    inline constexpr int platform_standing = 14;
    inline constexpr int dead = 15;
    inline constexpr int physics = 16;
    inline constexpr int none = 18;
}

namespace basepart
{
    inline constexpr uintptr_t primitive = 0x178;
    inline constexpr uintptr_t transparency = 0x120;
    inline constexpr uintptr_t color3 = 0x198;
}

namespace sound
{
    inline constexpr uintptr_t playing_a = 0x155;
    inline constexpr uintptr_t playing_b = 0x159;
    inline constexpr uintptr_t playing_c = 0x161;
    inline constexpr uintptr_t loudness_a = 0x178;
    inline constexpr uintptr_t loudness_b = 0x188;
    inline constexpr uintptr_t loudness_c = 0x1a8;
}

namespace model
{
    inline constexpr uintptr_t primary_part = 0x248;
    inline constexpr uintptr_t scale = 0x134;
}

namespace primitive
{
    inline constexpr uintptr_t rotation = 0xb0;
    inline constexpr uintptr_t position = 0xd4;
    inline constexpr uintptr_t linear_velocity = 0xe0;
    inline constexpr uintptr_t angular_velocity = 0xec;

    inline constexpr uintptr_t gravity = 0x1c8;
    inline constexpr uintptr_t flags = 0x1b6;
    inline constexpr uintptr_t size = 0x1bc;
    inline constexpr uintptr_t owner = 0x210;
}

namespace primitive_flags
{
    inline constexpr uint8_t anchored = 0x2;
    inline constexpr uint8_t can_collide = 0x8;
    inline constexpr uint8_t can_touch = 0x10;
    inline constexpr uint8_t can_query = 0x20;
}

namespace camera
{
    inline constexpr uintptr_t camera_subject = 0xb8;
    inline constexpr uintptr_t cframe = 0xc8;
    inline constexpr uintptr_t orientation = 0xc8;
    inline constexpr uintptr_t position = 0xec;
    inline constexpr uintptr_t focus = 0xf8;
    inline constexpr uintptr_t focus_orientation = 0xf8;
    inline constexpr uintptr_t focus_position = 0x11c;
    inline constexpr uintptr_t camera_type = 0x128;
    inline constexpr uintptr_t diagonal_fov = 0x12c;
    inline constexpr uintptr_t field_of_view = 0x130;
    inline constexpr uintptr_t head_scale = 0x138;
    inline constexpr uintptr_t max_axis_fov = 0x13c;
    inline constexpr uintptr_t near_plane_z = 0x140;
    inline constexpr uintptr_t head_locked = 0x144;
    inline constexpr uintptr_t viewport_size = 0x2bc;
}

namespace camera_type
{
    inline constexpr int fixed = 0;
    inline constexpr int attach = 1;
    inline constexpr int watch = 2;
    inline constexpr int track = 3;
    inline constexpr int follow = 4;
    inline constexpr int custom = 5;
    inline constexpr int scriptable = 6;
    inline constexpr int orbital = 7;
}

namespace visual_engine
{
    inline constexpr uintptr_t pointer = 0x851bf08;
    inline constexpr uintptr_t view_matrix = 0x1b0;
    inline constexpr uintptr_t fake_datamodel = 0xaf0;
    inline constexpr uintptr_t dimensions = 0xb10;
    inline constexpr uintptr_t render_view = 0xc30;
}

namespace task_scheduler
{
    inline constexpr uintptr_t pointer = 0x8c8d108;
    inline constexpr uintptr_t job_start = 0xc8;
    inline constexpr uintptr_t job_end = 0xd0;
    inline constexpr uintptr_t job_name = 0x18;
    inline constexpr uintptr_t job_stride = 0x10;
    inline constexpr uintptr_t max_fps = 0xb0;
}

namespace render_job
{
    inline constexpr uintptr_t fake_datamodel = 0x38;
    inline constexpr uintptr_t real_datamodel = 0x1f0;
    inline constexpr uintptr_t render_view = 0x1d8;
}

namespace class_descriptor
{
    inline constexpr uintptr_t class_name = 0x8;
    inline uintptr_t creator = 0x230;
    inline uintptr_t property_descriptors = 0x40;
    inline uintptr_t property_count = 0x48;
    inline uintptr_t event_descriptors = 0x88;
    inline uintptr_t event_count = 0x90;
    inline uintptr_t function_descriptors = 0xd0;
    inline uintptr_t function_count = 0xd8;
    inline constexpr uintptr_t super = 0x1B0;

    inline constexpr uintptr_t super_a = 0x28;
    inline constexpr uintptr_t super_b = 0x30;
    inline constexpr uintptr_t super_c = 0x38;
    inline constexpr uintptr_t super_d = 0x40;
}

namespace stl_vector
{
    inline constexpr uintptr_t begin = 0x0;
    inline constexpr uintptr_t end = 0x8;
}

// WorldRoot BoundFunc args from the dump wrapper (rdx):
//   Vector3 origin    @ +0x08
//   Vector3 direction @ +0x28  (magnitude = max distance, not unit)
//   raycastParams     @ +0x38  (null = default CanQuery / Exclude)
// Result packed the same way the Lua RaycastResult is read back.
namespace raycast_query
{
    inline constexpr uintptr_t magic = 0x00;
    inline constexpr uintptr_t seq = 0x04;
    inline constexpr uintptr_t origin = 0x08;
    inline constexpr uintptr_t magic2 = 0x20;
    inline constexpr uintptr_t seq2 = 0x24;
    inline constexpr uintptr_t direction = 0x28;
    inline constexpr uintptr_t params = 0x38;
    inline constexpr uintptr_t result_instance = 0x40;
    inline constexpr uintptr_t result_position = 0x48;
    inline constexpr uintptr_t result_normal = 0x54;
    inline constexpr uintptr_t result_distance = 0x60;
    inline constexpr uintptr_t result_material = 0x64;
    inline constexpr size_t bytes = 0x80;
    inline constexpr uint32_t k_magic = 0x54534352; // 'RCST'
}

namespace raycast_stub
{
    inline constexpr uintptr_t active = 0x00;
    inline constexpr uintptr_t reserved = 0x04;
    inline constexpr uintptr_t target = 0x08;
    inline constexpr uintptr_t scale = 0x14;
    inline constexpr uintptr_t calls = 0x18;
    inline constexpr uintptr_t origin = 0x20;
    inline constexpr uintptr_t dir = 0x2C;
    inline constexpr size_t data_block_bytes = 0x40;
    inline constexpr size_t stub_slot_bytes = 0x800;
}

namespace descriptor
{
    inline constexpr uintptr_t name = 0x8;
}

namespace function_descriptor
{
    inline constexpr uintptr_t function = 0x80;
}

namespace property_descriptor
{
    inline constexpr uintptr_t get_set_impl = 0x90;
    inline constexpr uintptr_t ttype = 0x68;
}

namespace get_set
{
    inline constexpr uintptr_t vfptr = 0x00;
    inline constexpr uintptr_t get = 0x08;
    inline constexpr uintptr_t set = 0x10;
    inline constexpr uintptr_t vt_get = 0x00;
}

namespace icreator
{
    inline constexpr uintptr_t create = 0x0;
    inline constexpr uintptr_t is_creatable = 0x8;
}

namespace creator_map
{
    inline uintptr_t map_start = 0;
    inline uintptr_t map_end = 0;
}

namespace functions
{
    inline uintptr_t set_parent = 0;
    inline uintptr_t destroy = 0;
}

namespace highlight
{
    // Dump intern cluster 0x6E0F608: Highlight, DepthMode, OutlineColor,
    // OutlineTransparency, FillColor, FillTransparency. Adornee is shared 0x6DFC708.
    inline constexpr uintptr_t fill_color = 0xc8;
    inline constexpr uintptr_t outline_color = 0xd4;
    inline constexpr uintptr_t depth_mode = 0xe0;
    inline constexpr uintptr_t fill_transparency = 0xe4;
    inline constexpr uintptr_t outline_transparency = 0xec;
    inline constexpr uintptr_t enabled = 0xf4;
    inline constexpr uintptr_t adornee = 0x100;
}

namespace gui_object
{
    inline constexpr uintptr_t background_color3 = 0x530;
    inline constexpr uintptr_t text = 0xdf0;
    inline constexpr uintptr_t text_color3 = 0xea0;
    inline constexpr uintptr_t visible = 0x59d;
    inline constexpr uintptr_t position = 0x500;
    inline constexpr uintptr_t size = 0x520;

    inline constexpr uintptr_t frame_pos_x = 0x3ac;
    inline constexpr uintptr_t frame_pos_y = 0x3b4;
    inline constexpr uintptr_t frame_pos_x_alt = 0x4d0;
    inline constexpr uintptr_t frame_pos_y_alt = 0x4d8;
}

namespace mesh_content_provider
{
    inline constexpr uintptr_t cache_object = 0xe8;
    inline constexpr uintptr_t cache_object_alt = 0xc8;
    inline constexpr uintptr_t lru_cache1 = 0x20;
    inline constexpr uintptr_t sentinel = 0x08;
}

namespace lru_node
{
    inline constexpr uintptr_t next = 0x00;
    inline constexpr uintptr_t prev = 0x08;
    inline constexpr uintptr_t content_id = 0x10;
    inline constexpr uintptr_t mesh_data = 0x30;
    inline constexpr uintptr_t mesh_data_alt = 0x38;
    inline constexpr uintptr_t stride = 0x48;
}

namespace file_mesh_data
{
    inline constexpr uintptr_t vertex_begin = 0x00;
    inline constexpr uintptr_t vertex_end = 0x08;
    inline constexpr uintptr_t face_begin = 0x30;
    inline constexpr uintptr_t face_end = 0x38;
    inline constexpr uintptr_t aabb_min = 0x180;
    inline constexpr uintptr_t aabb_max = 0x18c;
    inline constexpr uintptr_t vertex_stride = 40;
    inline constexpr uintptr_t face_stride = 12;
}

namespace mesh_part
{
    inline constexpr uintptr_t mesh_id = 0x300;
    inline constexpr uintptr_t texture = 0x330;
}

namespace decal
{
    inline constexpr uintptr_t texture = 0x1d0;
    inline constexpr uintptr_t texture_alt_a = 0x1d0;
    inline constexpr uintptr_t texture_alt_b = 0x1d0;
    inline constexpr uintptr_t texture_alt_c = 0x1d0;
}

namespace clothing
{
    inline constexpr uintptr_t shirt_template = 0xf0;
    inline constexpr uintptr_t pants_template = 0xf0;
    inline constexpr uintptr_t graphic = 0xf0;
}

namespace special_mesh
{
    inline constexpr uintptr_t mesh_id = 0xe8;
    inline constexpr uintptr_t scale = 0xb4;
}

namespace local_script
{
    inline constexpr uintptr_t bytecode = 0x190;
    inline constexpr uintptr_t guid = 0xc0;
    inline constexpr uintptr_t hash = 0x190;
}

namespace module_script
{
    inline constexpr uintptr_t bytecode = 0x138;
    inline constexpr uintptr_t guid = 0xc0;
    inline constexpr uintptr_t hash = 0x350;
    inline constexpr uintptr_t is_core_script = 0x0;
}

namespace script
{
    inline constexpr uintptr_t bytecode = 0x190;
    inline constexpr uintptr_t guid = 0xc0;
    inline constexpr uintptr_t hash = 0x190;
}

namespace bytecode
{
    inline constexpr uintptr_t pointer = 0x10;
    inline constexpr uintptr_t size = 0x28;
    inline constexpr uintptr_t size_alt = 0x28;
}

namespace lighting
{
    inline constexpr uintptr_t time_microseconds = 0xb8;
    inline constexpr uintptr_t ambient = 0xc0;
    inline constexpr uintptr_t color_shift_bottom = 0xd8;
    inline constexpr uintptr_t color_shift_top = 0xcc;
    inline constexpr uintptr_t fog_color = 0xe4;
    inline constexpr uintptr_t outdoor_ambient = 0xf0;
    inline constexpr uintptr_t shadow_color = 0xfc;
    inline constexpr uintptr_t brightness = 0x108;
    inline constexpr uintptr_t environment_diffuse_scale = 0x10c;
    inline constexpr uintptr_t environment_specular_scale = 0x110;
    inline constexpr uintptr_t exposure_compensation = 0x114;
    inline constexpr uintptr_t fog_end = 0x11c;
    inline constexpr uintptr_t fog_start = 0x120;
    inline constexpr uintptr_t geographic_latitude = 0x124;
    inline constexpr uintptr_t shadow_softness = 0x12c;
    inline constexpr uintptr_t global_shadows = 0x134;
    inline constexpr uintptr_t outlines = 0x135;
    inline constexpr uintptr_t prioritize_lighting_quality = 0x136;
    inline constexpr uintptr_t sky_ambient = 0x140;
    inline constexpr uintptr_t light_color = 0x14c;
    inline constexpr uintptr_t light_direction = 0x158;
    inline constexpr uintptr_t sky = 0x1b8;
    inline constexpr uintptr_t atmosphere = 0x1c8;
}

namespace material_service
{
    inline constexpr uintptr_t use_2022_materials = 0xd0;
}

namespace sky
{
    inline constexpr uintptr_t moon_texture = 0xb8;
    inline constexpr uintptr_t skybox_bk = 0xe8;
    inline constexpr uintptr_t skybox_dn = 0x118;
    inline constexpr uintptr_t skybox_ft = 0x148;
    inline constexpr uintptr_t skybox_lf = 0x178;
    inline constexpr uintptr_t skybox_rt = 0x1a8;
    inline constexpr uintptr_t skybox_up = 0x1d8;
    inline constexpr uintptr_t sun_texture = 0x208;
    inline constexpr uintptr_t skybox_orientation = 0x228;
    inline constexpr uintptr_t sun_angular_size = 0x22c;
    inline constexpr uintptr_t moon_angular_size = 0x234;
    inline constexpr uintptr_t star_count = 0x238;
    inline constexpr uintptr_t celestial_bodies_shown = 0x240;
}

namespace team
{
    inline constexpr uintptr_t brick_color = 0xa8;
}

namespace tool
{
    inline constexpr uintptr_t texture_id = 0x350;
    inline constexpr uintptr_t tooltip = 0x458;
    inline constexpr uintptr_t grip = 0x49c;
    inline constexpr uintptr_t can_be_dropped = 0x4a8;
    inline constexpr uintptr_t requires_handle = 0x4ab;
}

namespace animation
{
    inline constexpr uintptr_t animation_id = 0xb0;
}

namespace animation_track
{
    inline constexpr uintptr_t animation = 0xa8;
    inline constexpr uintptr_t animator = 0x100;
    inline constexpr uintptr_t is_playing = 0x522;
    inline constexpr uintptr_t looped = 0xd5;
    inline constexpr uintptr_t speed = 0xc4;
    inline constexpr uintptr_t time_position = 0xc8;
}

namespace mouse_service
{

    inline constexpr uintptr_t input_object = 0xe0;
    inline constexpr uintptr_t input_object_alt = 0xe0;
    inline constexpr uintptr_t input_object_2 = 0xf0;
    inline constexpr uintptr_t mouse_position = 0xc4;
}

namespace input_object
{
    inline constexpr uintptr_t position = 0xf4;
    inline constexpr uintptr_t position_alt = 0xec;
    inline constexpr uintptr_t delta = 0x100;
}

}
