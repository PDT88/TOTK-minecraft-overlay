#pragma once

#include "mc_snapshot.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal simulation structures owned exclusively by McEngine (under g_mc_mu). */

typedef struct {
    uint8_t  active;
    uint16_t type;        /* McBlockType */
    int32_t  ix, iy, iz;
    uint8_t  light;
    uint8_t  facing;
    uint16_t smelt_output;
    uint8_t fuel_uses;
    float cook_left;
    float    tnt_fuse;    /* -1.0f idle */
    uint8_t  flash_white;
} McWorldBlock;

typedef struct {
    uint8_t  active;
    uint8_t  kind;        /* McMobType */
    uint8_t  grounded;
    uint8_t  hurt_flash;
    float    x, y, z;
    float    vx, vy, vz;
    float    yaw, pitch;
    float    walk_phase;
    float    walk_amp;
    float    swing_arm;
    float    swell;
    float    hp, max_hp;
    float    hurt_timer;
    float    attack_cd;
    uint8_t natural, stay;
    float    ai_timer;
    float    say_timer;
    float    step_accum;
    float    strafe_dir;
} McMob;

typedef struct {
    uint8_t  active;
    uint8_t  flash_white;
    float    x, y, z;
    float    vx, vy, vz;
    float    fuse_left;
    float    fuse_total;
} McPrimedTnt;

typedef struct {
    uint8_t  active;
    uint8_t  enchanted;
    uint16_t item_type;   /* McItemType */
    uint8_t  count;
    float    x, y, z;
    float    vx, vy, vz;
    float    spin_yaw;
    float    age;
    float    pickup_delay;
} McDroppedItem;

typedef struct {
    uint8_t  active;
    uint8_t  kind;        /* McProjType */
    uint8_t  from_player;
    uint8_t  stuck;
    uint8_t  crit;
    uint8_t  firework_style;
    float    x, y, z;
    float    vx, vy, vz;
    float    age;
    float    max_age;
    float    power;
} McProjectile;

typedef struct {
    uint8_t  active;
    uint8_t  kind;        /* McParticleType */
    uint8_t  frame_count;
    uint8_t  _pad;
    uint32_t rgba;
    float    x, y, z;
    float    vx, vy, vz;
    float    size;
    float    age;
    float    max_age;
    float    gravity;
} McParticle;

typedef struct {
    int32_t  initialized;
    int32_t  active;
    int32_t  mode;        /* 1 = Minecraft Mode, 0 = Zelda Mode */
    int32_t  prev_mode;
    int32_t  view_mode;   /* McViewMode */
    int32_t  gameplay_active;
    int32_t  in_hyrule_world;

    uint64_t tick_count;
    uint64_t last_tick_ns;
    float    time_sec;
    uint32_t rng;

    /* Camera (updated from NVN / note_camera) */
    int32_t  cam_valid;
    float    camera_orbit_until;
    float camera_distance, ground_camera_distance;
    float mouse_scale, fov_radians, camera_shake;
    float gui_scale, mainhand_scale, offhand_scale;
    int32_t  flight_camera_available;
    int32_t  flight_look_active;
    int32_t  flight_view_ready;
    float    flight_view[12];    /* Row-major 3x4 view matrix for Zelda's scene renderer. */
    float    last_third_person_dist;
    float    eye_height;         /* Smoothed Minecraft eye height (1.62 stand / 1.27 crouch / 0.55 glide) */
    float    totk_cam_yaw;       /* Pre-override TotK scene camera yaw (radians, 0=+Z, +pi/2=+X) */
    float    totk_cam_pitch;     /* Pre-override TotK scene camera pitch (radians) */
    int32_t  totk_cam_valid;
    float    cam_vp[16];
    float    cam_pos[3];
    float    cam_right[3];
    float    cam_up[3];
    float    cam_fwd[3];
    float    cam_near;
    float    cam_far;
    float    cam_fov_rad;
    float    cam_aspect;
    float    render_w;
    float    render_h;

    /* Mouse look / cursor */
    float    mouse_look_dx;
    float    mouse_look_dy;
    float    move_right, move_forward;
    uint64_t forward_tap_ns;
    uint8_t  sprint_held;
    bool     sprint_requested;
    float    look_yaw;
    float    look_pitch;
    int32_t  look_angles_init;
    float    cursor_nx;
    float    cursor_ny;
    uint8_t  mouse_down[3];
    uint8_t  mouse_clicked[3];
    uint8_t  pending_left_press;
    uint8_t  pending_right_press;
    uint8_t  pending_jump_press;
    uint8_t  pending_bow_fire;
    float    pending_bow_charge_sec;
    int16_t  pending_spawns[16];
    int      pending_spawn_count;
    uint8_t  pending_break_target;
    uint8_t  pending_ignite_tnt;
    uint8_t  pending_log_actors;
    uint8_t  pending_log_mobs;
    uint8_t  key_down[512];
    uint8_t  consume_key_up[512];

    /* Player kinematics & state */
    float    player_pos[3];
    float    player_vel[3];
    float    player_prev_pos[3];
    float    player_totk_yaw;
    int32_t  player_totk_yaw_valid;
    float    body_yaw;
    float    head_yaw;
    int32_t  body_yaw_init;
    int32_t  player_pos_valid;
    uint8_t  player_grounded;
    uint8_t  player_was_grounded;
    uint8_t  player_on_mc_block;
    uint8_t  player_crouching;
    float    fall_peak_y;
    float    jump_ascent_speed;
    float    airborne_time;
    float    walk_phase;
    float    walk_amp;
    float    step_accum;

    /* Elytra */
    uint8_t  elytra_gliding;
    float    elytra_open;
    float    elytra_pitch;
    float    elytra_bank;
    float    elytra_yaw;
    float    elytra_prev_yaw;
    float    elytra_flight_time;
    float    elytra_fov_mult;
    float    elytra_vel[3];
    float    elytra_jump_until;
    float    elytra_takeoff_until;
    float    elytra_boost_timer;
    float    elytra_boost_trail_accum;
    uint8_t  prev_jump_down;

    /* Health, hunger, armor, XP, buffs */
    int32_t  hearts_half;
    int32_t  max_hearts_half;
    int32_t  absorption_half;
    int32_t  hunger;
    float    saturation;
    float exhaustion, food_timer, encounter_timer, day_seconds;
    uint8_t random_mob_spawns;
    uint8_t shaders_enabled;
    int station_x, station_y, station_z;
    int pending_recipe;
    uint8_t base_dirty, base_played, base_read_error;
    float base_save_timer;
    struct PlatThread *base_save_thread;
    struct McBaseSaveJob *base_save_job;
    float    regen_accum;
    int32_t  armor;
    int32_t  xp_level;
    float    xp_prog;
    int32_t  totk_hp_q;
    int32_t  totk_max_hp_q;
    float    invuln_timer;

    int32_t  effect_count;
    McEffectSnap effects[MC_SNAP_MAX_EFFECTS];

    /* Inventory & hotbar */
    int32_t  selected_slot;
    McItemStack hotbar[9];
    McItemStack offhand;
    McItemStack armor_slots[4];
    McItemStack inv_slots[27];
    McItemStack craft_slots[4];
    McItemStack craft_result;

    int32_t  inv_open;       /* McInvMode */
    int32_t  creative_tab;   /* McCreativeTab */
    float    creative_scroll;
    McItemStack cursor_stack;
    char     search_text[48];
    int32_t  search_len;
    char     hover_text[96];

    /* Combat & use animations */
    float    swing_timer;
    float    swing_dur;
    float    mining_swing_timer;
    float    attack_cd_timer;
    float    attack_cd_dur;
    float    equip_drop;
    int32_t  last_held_signature;

    uint8_t  bow_drawing;
    float    bow_charge_sec;
    float    bow_raise, bow_release;
    uint8_t  eating;
    float    eat_timer;
    float    eat_snd_accum;
    float    eat_raise;
    float    eat_crumb_accum;
    float    shield_block;
    float    totem_pop_timer;
    float    hurt_flash_timer;
    float    tooltip_timer;
    char     tooltip_text[64];
    float    crosshair_hit_timer;
    float    left_click_repeat;

    /* Mining progress */
    int32_t  has_target;
    int32_t  target_ix, target_iy, target_iz;
    int32_t  target_face;
    float    mining_progress;
    float    mining_snd_accum;
    float    mining_crumb_accum;
    int32_t  mining_ix, mining_iy, mining_iz;
    int32_t  has_hyrule_target;
    float    hyrule_hit[3];
    float    hyrule_normal[3];

    /* World entities */
    int32_t  block_count;
    uint8_t block_face_masks[MC_SNAP_MAX_BLOCKS];
    uint8_t block_faces_dirty;
    McWorldBlock blocks[MC_SNAP_MAX_BLOCKS];
    McMob    mobs[MC_SNAP_MAX_MOBS];
    McPrimedTnt primed_tnt[MC_SNAP_MAX_PRIMED_TNT];
    McDroppedItem dropped[MC_SNAP_MAX_DROPPED];
    McProjectile projectiles[MC_SNAP_MAX_PROJECTILES];
    McParticle particles[MC_SNAP_MAX_PARTICLES];
} McEngine;

void mc_engine_save_base(McEngine *eng);
void mc_engine_init(McEngine *eng);
void mc_engine_first_person_eye(const McEngine *eng, float eye[3]);
void mc_engine_refresh_view(McEngine *eng);
void mc_engine_note_camera(McEngine *eng, const float *cam84, int count);
void mc_engine_game_tick(McEngine *eng, uint64_t frame);

int  mc_engine_key(McEngine *eng, unsigned code, int down);
int  mc_engine_command(McEngine *eng, const char *line);

int  mc_engine_wants_mouse_capture(const McEngine *eng);
void mc_engine_mouse_move(McEngine *eng, float dx, float dy);
void mc_engine_cursor_pos(McEngine *eng, float nx, float ny);
void mc_engine_mouse_button(McEngine *eng, int button, int down);
void mc_engine_scroll(McEngine *eng, float delta_y);

void mc_engine_export_snapshot(McEngine *eng, McRenderSnapshot *out);

#ifdef __cplusplus
}
#endif
