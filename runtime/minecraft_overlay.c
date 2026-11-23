#include "minecraft_overlay.h"
#include "cfg.h"
#include "input.h"
#include "input_wsys.h"
#include "mc_assets.h"
#include "mc_audio.h"
#include "mc_engine.h"
#include "mc_totk_bridge.h"
#include "platform/platform.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static PlatMutex g_mc_mu = PLAT_MUTEX_INIT;
static PlatMutex g_mc_snap_mu = PLAT_MUTEX_INIT;
static PlatMutex g_mc_input_mu = PLAT_MUTEX_INIT;

static McEngine g_mc_eng;
static McRenderSnapshot g_mc_snap_staging;
static McRenderSnapshot g_mc_snap;
static atomic_int g_mc_snap_ready = 0;

static atomic_int g_mc_initialized = 0;
static int g_bridge_inited = 0;
#define MC_FLIGHT_VIEW_SLOTS 16u
static float *g_flight_view_guest = NULL;
static atomic_uint g_flight_view_ring = 0;
static atomic_int g_flight_cam_seen = 0;

static atomic_int options_open = 0;
static bool g_mc_use_held = false;
static bool g_mc_mouse_use_down = false;
static bool g_mc_mouse_use_pressed = false;
static atomic_int g_mc_active_flag = 0;
static atomic_int g_mc_gameplay_flag = 0;
static atomic_int g_mc_wants_tick_flag = 0;
static atomic_int g_mc_mode_flag = 1;
static atomic_int g_mc_view_flag = 0;
static atomic_int g_mc_mouse_capture_flag = 0;
static atomic_int g_mc_sprint_flag = 0;
static atomic_int g_mc_flight_look_flag = 0;
static atomic_int g_mc_ground_move_active = 0;
static _Atomic float g_totk_cam_yaw = 0.0f;
static _Atomic float g_totk_cam_pitch = 0.0f;
static atomic_int g_totk_cam_valid = 0;

/* Lock-free seqlock state for runtime_mc_on_set_scene_camera (fnprof_hit). */
typedef struct {
    int   active_mode;
    int   view_mode;
    int   elytra_gliding;
    int   flight_look_active;
    int   flight_view_ready;
    float ground_camera_distance;
    float eye_height;
    float fp_bob_right;
    float fp_bob_up;
    float fp_bob_roll;
    float fov_radians;
    float player_pos[3];
    float first_person_eye[3];
    float flight_view[12];
} McCamHookState;

static atomic_uint g_cam_seq = 0;
static McCamHookState g_cam_state;
static _Thread_local uint64_t g_scene_projection;
static _Thread_local float g_scene_fov;

/* Non-blocking input & camera queue from Win32 wnd_proc and Vulkan replay thread. */
enum {
    MC_EV_KEY = 1,
    MC_EV_MOUSE_BTN = 2,
    MC_EV_SCROLL = 3,
    MC_EV_RELEASE = 4
};

typedef struct {
    int      kind;
    unsigned code;
    int      down;
    float    fval;
} McPendingEvent;

#define MC_MAX_PENDING_EVENTS 64
static McPendingEvent g_pending_ev[MC_MAX_PENDING_EVENTS];
static int g_pending_ev_count = 0;
static float g_pending_mouse_dx = 0.0f;
static float g_pending_mouse_dy = 0.0f;
static int g_pending_mouse_move = 0;
static float g_mc_yaw_synced = 0.0f; /* protected by g_mc_input_mu */
static _Atomic float g_mc_mouse_gain = 1.0f;
static float g_pending_cursor_nx = 0.5f;
static float g_pending_cursor_ny = 0.5f;
static int g_pending_cursor_dirty = 0;
static float g_pending_cam84[84];
static int g_pending_cam_valid = 0;
static uint8_t g_fast_consume_up[512];

int runtime_mc_options_open(void) {
    return atomic_load_explicit(&options_open, memory_order_acquire);
}

static float g_latest_cam_vp[4][4];
static float g_latest_cam_pos[3];
static float g_latest_cam_right[3];
static float g_latest_cam_up[3];
static float g_latest_cam_fwd[3];
static float g_latest_cam_near = 0.1f;
static float g_latest_cam_far = 1000.0f;
static float g_latest_cam_aspect = 16.0f / 9.0f;
static float g_latest_cam_fov_rad = 1.0f;
static int   g_latest_cam_valid = 0;

static void apply_latest_cam_to_snap_locked(McRenderSnapshot *snap) {
    if (!g_latest_cam_valid) return;
    memcpy(snap->cam_vp, g_latest_cam_vp, sizeof(snap->cam_vp));
    memcpy(snap->cam_pos, g_latest_cam_pos, sizeof(snap->cam_pos));
    memcpy(snap->cam_right, g_latest_cam_right, sizeof(snap->cam_right));
    memcpy(snap->cam_up, g_latest_cam_up, sizeof(snap->cam_up));
    memcpy(snap->cam_fwd, g_latest_cam_fwd, sizeof(snap->cam_fwd));
    snap->cam_near = g_latest_cam_near;
    snap->cam_far = g_latest_cam_far;
    snap->cam_aspect = g_latest_cam_aspect;
    snap->cam_fov_rad = g_latest_cam_fov_rad;
    snap->cam_valid = 1;
}

static void publish_snapshot_locked(void) {
    mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap_staging);
    plat_mutex_lock(&g_mc_snap_mu);
    memcpy(&g_mc_snap, &g_mc_snap_staging, sizeof(g_mc_snap));
    apply_latest_cam_to_snap_locked(&g_mc_snap);
    atomic_store_explicit(&g_mc_snap_ready, 1, memory_order_release);
    plat_mutex_unlock(&g_mc_snap_mu);
}

static int sync_atomic_flags_locked(void) {
    int prev_active = atomic_load_explicit(&g_mc_active_flag, memory_order_relaxed);
    int prev_cap = atomic_load_explicit(&g_mc_mouse_capture_flag, memory_order_relaxed);

    int gp = (g_mc_eng.initialized && g_mc_eng.active && g_mc_eng.gameplay_active) ? 1 : 0;
    int now_active = (gp && g_mc_eng.mode == 1) ? 1 : 0;
    const bool in_world = mc_engine_wants_mouse_capture(&g_mc_eng) != 0;
    int now_cap = in_world ? 1 : 0;

    atomic_store_explicit(&g_mc_gameplay_flag, gp, memory_order_release);
    atomic_store_explicit(&g_mc_wants_tick_flag, (g_mc_eng.initialized && g_mc_eng.active) ? 1 : 0, memory_order_release);
    atomic_store_explicit(&g_mc_active_flag, now_active, memory_order_release);
    atomic_store_explicit(&g_mc_mode_flag, gp ? g_mc_eng.mode : 0, memory_order_release);
    atomic_store_explicit(&g_mc_view_flag, g_mc_eng.view_mode, memory_order_release);
    atomic_store_explicit(&g_mc_flight_look_flag, g_mc_eng.flight_look_active ? 1 : 0, memory_order_release);
    if (!now_active) {
        atomic_store_explicit(&g_totk_cam_valid, 0, memory_order_release);
    }
    plat_mutex_lock(&g_mc_input_mu);
    g_mc_yaw_synced = g_mc_eng.look_yaw;
    plat_mutex_unlock(&g_mc_input_mu);
    atomic_store_explicit(&g_mc_ground_move_active,
                          (now_active && in_world && g_mc_eng.flight_look_active &&
                           !g_mc_eng.elytra_gliding) ? 1 : 0,
                          memory_order_release);
    if (!in_world) {
        g_mc_mouse_use_down = g_mc_mouse_use_pressed = g_mc_use_held = false;
        g_mc_eng.forward_tap_ns = 0;
        g_mc_eng.sprint_held = 0;
    }
    atomic_store_explicit(&g_mc_sprint_flag, g_mc_eng.sprint_held, memory_order_release);
    atomic_store_explicit(&g_mc_mouse_capture_flag, now_cap, memory_order_release);

    /* Publish lock-free camera state for runtime_mc_on_set_scene_camera. */
    McCamHookState cs;
    memset(&cs, 0, sizeof(cs));
    cs.active_mode = (now_active && g_mc_eng.player_pos_valid) ? 1 : 0;
    cs.view_mode = g_mc_eng.view_mode;
    cs.elytra_gliding = g_mc_eng.elytra_gliding;
    cs.flight_look_active = g_mc_eng.flight_look_active ? 1 : 0;
    cs.flight_view_ready = g_mc_eng.flight_view_ready;
    cs.ground_camera_distance = g_mc_eng.ground_camera_distance;
    cs.eye_height = g_mc_eng.eye_height > 0.1f ? g_mc_eng.eye_height : 1.62f;
    float shake = fmaxf(0.0f, fminf(1.0f, g_mc_eng.camera_shake));
    float bob_amp = (g_mc_eng.elytra_gliding || shake <= 1e-4f) ? 0.0f : (g_mc_eng.walk_amp * shake);
    if (bob_amp > 1e-4f) {
        cs.fp_bob_right = sinf(g_mc_eng.walk_phase) * bob_amp * 0.045f;
        cs.fp_bob_up = -fabsf(cosf(g_mc_eng.walk_phase)) * bob_amp * 0.055f;
        cs.fp_bob_roll = sinf(g_mc_eng.walk_phase) * bob_amp * 0.014f;
    }
    float fov_mult = g_mc_eng.elytra_fov_mult > 0.5f ? g_mc_eng.elytra_fov_mult : 1.0f;
    float eff_fov = g_mc_eng.fov_radians * fov_mult;
    const float max_fov = 122.0f * (float)M_PI / 180.0f;
    cs.fov_radians = eff_fov > max_fov ? max_fov : eff_fov;
    memcpy(cs.player_pos, g_mc_eng.player_pos, sizeof(cs.player_pos));
    mc_engine_first_person_eye(&g_mc_eng, cs.first_person_eye);
    memcpy(cs.flight_view, g_mc_eng.flight_view, sizeof(cs.flight_view));

    unsigned seq = atomic_load_explicit(&g_cam_seq, memory_order_relaxed);
    atomic_store_explicit(&g_cam_seq, seq + 1u, memory_order_release);
    atomic_thread_fence(memory_order_acq_rel);
    g_cam_state = cs;
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&g_cam_seq, seq + 2u, memory_order_release);

    return (now_active != prev_active || now_cap != prev_cap) ? 1 : 0;
}

static void release_input_locked(void) {
    g_mc_eng.forward_tap_ns = 0;
    g_mc_eng.sprint_held = 0;
    atomic_store_explicit(&g_mc_sprint_flag, 0, memory_order_release);
    memset(g_mc_eng.key_down, 0, sizeof(g_mc_eng.key_down));
    memset(g_mc_eng.consume_key_up, 0, sizeof(g_mc_eng.consume_key_up));
    memset(g_mc_eng.mouse_down, 0, sizeof(g_mc_eng.mouse_down));
    g_mc_eng.pending_left_press = g_mc_eng.pending_right_press = 0;
    g_mc_eng.pending_jump_press = g_mc_eng.pending_bow_fire = 0;
    g_mc_eng.bow_drawing = g_mc_eng.eating = 0;
    g_mc_eng.bow_charge_sec = g_mc_eng.shield_block = 0.0f;
    g_mc_mouse_use_down = g_mc_mouse_use_pressed = g_mc_use_held = false;
    runtime_input_clear();
}

static void drain_pending_inputs_locked(void) {
    float cam84[84];
    int has_cam = 0;
    float mdx = 0.0f, mdy = 0.0f;
    int has_mmove = 0;
    float cnx = 0.5f, cny = 0.5f;
    int has_cpos = 0;
    McPendingEvent evs[MC_MAX_PENDING_EVENTS];
    int nev = 0;

    int direct_look_moved = 0;
    plat_mutex_lock(&g_mc_input_mu);
    if (g_pending_cam_valid) {
        memcpy(cam84, g_pending_cam84, sizeof(cam84));
        g_pending_cam_valid = 0;
        has_cam = 1;
    }
    if (g_pending_mouse_move) {
        mdx = g_pending_mouse_dx;
        mdy = g_pending_mouse_dy;
        g_pending_mouse_dx = 0.0f;
        g_pending_mouse_dy = 0.0f;
        g_pending_mouse_move = 0;
        if (mc_engine_wants_mouse_capture(&g_mc_eng) && g_mc_eng.flight_look_active) {
            const float radians_per_dot = (float)M_PI / 1200.0f;  /* Java's 0.15 deg per count at 100%. */
            float sdx = mdx * g_mc_eng.mouse_scale;
            float sdy = mdy * g_mc_eng.mouse_scale;
            g_mc_eng.look_yaw = remainderf(g_mc_eng.look_yaw - sdx * radians_per_dot, 2.0f * (float)M_PI);
            float p = g_mc_eng.look_pitch - sdy * radians_per_dot;
            const float lim = (float)M_PI * 0.495f;
            g_mc_eng.look_pitch = p < -lim ? -lim : (p > lim ? lim : p);
            g_mc_yaw_synced = g_mc_eng.look_yaw;
            direct_look_moved = 1;
        } else {
            has_mmove = 1;
        }
    }
    if (g_pending_cursor_dirty) {
        cnx = g_pending_cursor_nx;
        cny = g_pending_cursor_ny;
        g_pending_cursor_dirty = 0;
        has_cpos = 1;
    }
    if (g_pending_ev_count > 0) {
        nev = g_pending_ev_count;
        memcpy(evs, g_pending_ev, (size_t)nev * sizeof(McPendingEvent));
        g_pending_ev_count = 0;
    }
    plat_mutex_unlock(&g_mc_input_mu);

    if (atomic_load_explicit(&g_flight_cam_seen, memory_order_acquire)) {
        g_mc_eng.flight_camera_available = 1;
    }
    if (has_cam) {
        mc_engine_note_camera(&g_mc_eng, cam84, 84);
    }
    if (has_cpos) {
        mc_engine_cursor_pos(&g_mc_eng, cnx, cny);
    }
    if (direct_look_moved) {
        if (g_mc_eng.camera_orbit_until > 0.0f) {
            g_mc_eng.camera_orbit_until = 0.0f;
            mc_totk_clear_camera_look();
        }
        mc_engine_refresh_view(&g_mc_eng);
    }
    if (has_mmove && mc_engine_wants_mouse_capture(&g_mc_eng)) {
        float sdx = mdx * g_mc_eng.mouse_scale;
        float sdy = mdy * g_mc_eng.mouse_scale;
        if (g_mc_eng.camera_orbit_until > 0.0f) {
            g_mc_eng.camera_orbit_until = 0.0f;
            mc_totk_clear_camera_look();
        }
        const float radians_per_dot = (float)M_PI / 1200.0f;  // Java's 0.15 degrees per count at 100%.
        if (g_mc_eng.flight_look_active) {
            g_mc_eng.look_yaw = remainderf(g_mc_eng.look_yaw - sdx * radians_per_dot, 2.0f * (float)M_PI);
            float p = g_mc_eng.look_pitch - sdy * radians_per_dot;
            const float lim = (float)M_PI * 0.495f;
            g_mc_eng.look_pitch = p < -lim ? -lim : (p > lim ? lim : p);
        } else {
            g_mc_eng.look_yaw -= sdx * radians_per_dot;
            float p = g_mc_eng.look_pitch - sdy * radians_per_dot;
            g_mc_eng.look_pitch = p < -1.52f ? -1.52f : (p > 1.52f ? 1.52f : p);
        }
    }
    for (int i = 0; i < nev; i++) {
        const McPendingEvent *e = &evs[i];
        if (e->kind == MC_EV_KEY) {
            (void)mc_engine_key(&g_mc_eng, e->code, e->down);
        } else if (e->kind == MC_EV_MOUSE_BTN) {
            int btn = (int)e->code;
            if (mc_engine_wants_mouse_capture(&g_mc_eng) && btn == 1) {
                if (e->down && !g_mc_mouse_use_down) g_mc_mouse_use_pressed = true;
                g_mc_mouse_use_down = e->down != 0;
            } else {
                mc_engine_mouse_button(&g_mc_eng, btn, e->down);
            }
        } else if (e->kind == MC_EV_SCROLL) {
            mc_engine_scroll(&g_mc_eng, e->fval);
        } else if (e->kind == MC_EV_RELEASE) {
            release_input_locked();
        }
    }
}

void runtime_mc_save_base(void) {
    plat_mutex_lock(&g_mc_mu);
    if (g_mc_eng.initialized) {
        mc_engine_save_base(&g_mc_eng);
    }
    plat_mutex_unlock(&g_mc_mu);
}

static void ensure_initialized_locked(const RtLoaded *main_module) {
    if (!g_bridge_inited && rt_game_version == 121) {
        const RtLoaded *mod = main_module ? main_module : ldr_module(0);
        if (mod) {
            mc_totk_bridge_init(mod);
            g_bridge_inited = 1;
        }
    }
    if (!g_flight_view_guest && rt_game_version == 121) {
        g_flight_view_guest = (float *)mem_kernel_alloc(sizeof(float) * 12u * MC_FLIGHT_VIEW_SLOTS, 16u);
    }
    if (!g_mc_eng.initialized) {
        mc_audio_init();
        mc_assets_init(NULL);
        mc_engine_init(&g_mc_eng);
        publish_snapshot_locked();
        (void)sync_atomic_flags_locked();
        atomic_store_explicit(&g_mc_initialized, 1, memory_order_release);
    }
}

/*
 * Lock-free hook called from main.c:fnprof_hit when guest enters f_700097e8e8 (SetSceneCamera, v1.2.1).
 * Never acquires g_mc_mu so it cannot deadlock with guest locks or runtime_mc_game_tick.
 */
void runtime_mc_on_set_scene_camera(Cpu *c) {
    g_scene_projection = 0;
    if (!c || rt_game_version != 121) return;
    if (!atomic_load_explicit(&g_mc_active_flag, memory_order_acquire)) return;
    if (!g_flight_view_guest) return;

    uint64_t view_addr = c->x[2];
    if (!view_addr || (view_addr & 3u) || !mem_guest_readable(view_addr, sizeof(float) * 12u)) return;

    float view[12];
    memcpy(view, (const void *)(uintptr_t)view_addr, sizeof(view));

    atomic_store_explicit(&g_flight_cam_seen, 1, memory_order_release);

    McCamHookState cs;
    for (int spin = 0; spin < 4; spin++) {
        unsigned s1 = atomic_load_explicit(&g_cam_seq, memory_order_acquire);
        if (s1 & 1u) continue;
        cs = g_cam_state;
        atomic_thread_fence(memory_order_acquire);
        unsigned s2 = atomic_load_explicit(&g_cam_seq, memory_order_acquire);
        if (s1 == s2) goto have_state;
    }
    return;

have_state:
    if (!cs.active_mode) return;

    float distance_squared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float camera = -view[axis] * view[3] - view[4 + axis] * view[7]
                     - view[8 + axis] * view[11];
        float delta = camera - cs.player_pos[axis];
        distance_squared += delta * delta;
    }
    float max_cam_dist = cs.elytra_gliding ? 64.0f : 24.0f;
    if (!(distance_squared < max_cam_dist * max_cam_dist)) return;

    /* Capture TotK's pre-override scene camera orientation (row 2 is -cam_fwd)
     * for 0 ms ground WASD stick rotation and gentle background camera alignment. */
    float fwd_len2 = view[8] * view[8] + view[9] * view[9] + view[10] * view[10];
    if (isfinite(fwd_len2) && fwd_len2 > 0.5f && fwd_len2 < 1.5f) {
        float inv_len = 1.0f / sqrtf(fwd_len2);
        float totk_yaw = atan2f(-view[8] * inv_len, -view[10] * inv_len);
        float totk_pitch = asinf(fmaxf(-1.0f, fminf(1.0f, -view[9] * inv_len)));
        if (isfinite(totk_yaw) && isfinite(totk_pitch)) {
            atomic_store_explicit(&g_totk_cam_yaw, totk_yaw, memory_order_relaxed);
            atomic_store_explicit(&g_totk_cam_pitch, totk_pitch, memory_order_relaxed);
            atomic_store_explicit(&g_totk_cam_valid, 1, memory_order_release);
        }
    }

    // SetSceneCamera selects this ViewInfo slot and builds its private projection copy.
    uint64_t view_set = c->x[0];
    if (cs.fov_radians > 0.0f && mem_guest_readable(view_set, 0x20u)) {
        uint32_t count;
        uint64_t slots;
        memcpy(&count, (const void *)(uintptr_t)(view_set + 0x10u), sizeof(count));
        memcpy(&slots, (const void *)(uintptr_t)(view_set + 0x18u), sizeof(slots));
        uint32_t index = (uint32_t)c->x[1];
        if (slots && index < count) {
            g_scene_projection = slots + (uint64_t)index * 0x430u + 0x160u;
            g_scene_fov = cs.fov_radians;
        }
    }

    float flight_view[12];
    int use_flight_view = 0;
    if (cs.flight_look_active && cs.flight_view_ready) {
        memcpy(flight_view, cs.flight_view, sizeof(flight_view));
        use_flight_view = 1;
    } else if (cs.view_mode == MC_VIEW_FIRST_PERSON ||
               (!cs.elytra_gliding && cs.ground_camera_distance > 0.0f)) {
        float eye[3];
        float distance = 0.0f;
        memcpy(flight_view, view, sizeof(flight_view));
        if (cs.view_mode == MC_VIEW_FIRST_PERSON) {
            memcpy(eye, cs.first_person_eye, sizeof(eye));
            if (fabsf(cs.fp_bob_right) > 1e-5f || fabsf(cs.fp_bob_up) > 1e-5f ||
                fabsf(cs.fp_bob_roll) > 1e-5f) {
                const float *r0 = view + 0;
                const float *u0 = view + 4;
                eye[0] += r0[0] * cs.fp_bob_right + u0[0] * cs.fp_bob_up;
                eye[1] += r0[1] * cs.fp_bob_right + u0[1] * cs.fp_bob_up;
                eye[2] += r0[2] * cs.fp_bob_right + u0[2] * cs.fp_bob_up;
                float cr = cosf(cs.fp_bob_roll), sr = sinf(cs.fp_bob_roll);
                for (int k = 0; k < 3; ++k) {
                    flight_view[k]     =  r0[k] * cr + u0[k] * sr;
                    flight_view[4 + k] = -r0[k] * sr + u0[k] * cr;
                }
            }
        } else {
            memcpy(eye, cs.player_pos, sizeof(eye));
            eye[1] += cs.eye_height > 0.1f ? cs.eye_height : 1.62f;
            distance = cs.ground_camera_distance;
        }
        for (int row = 0; row < 3; ++row) {
            const float *basis = flight_view + 4 * row;
            flight_view[4 * row + 3] = -(basis[0] * eye[0] + basis[1] * eye[1] + basis[2] * eye[2]);
        }
        flight_view[11] -= distance;
        use_flight_view = 1;
    }

    if (use_flight_view) {
        uint32_t cam_idx = (uint32_t)c->x[1] & 3u;
        uint32_t ring_idx = atomic_fetch_add_explicit(&g_flight_view_ring, 1u, memory_order_relaxed) & 3u;
        uint32_t slot = ((ring_idx << 2) | cam_idx) & (MC_FLIGHT_VIEW_SLOTS - 1u);
        float *dst = g_flight_view_guest + (size_t)slot * 12u;
        memcpy(dst, flight_view, sizeof(flight_view));
        uint64_t guest_ptr = (uint64_t)(uintptr_t)dst;
        c->x[2] = guest_ptr;
        c->x[4] = guest_ptr;
    }
}

void runtime_mc_on_scene_projection(Cpu *c) {
    if (!c || !g_scene_projection || c->x[1] != g_scene_projection) return;
    uint64_t addr = g_scene_projection;
    g_scene_projection = 0;
    if (!runtime_mc_active() || !mem_guest_readable(addr, 0x8cu)) return;

    // The game has populated this writable ViewInfo matrix and its vertical FOV.
    // Update both before it calculates the inverse projection and culling frustum.
    float *projection = (float *)(uintptr_t)addr;
    if (projection[15] != 0.0f || fabsf(projection[14]) != 1.0f ||
        !isfinite(projection[0]) || !isfinite(projection[5]) ||
        fabsf(projection[5]) < 0.001f) return;
    float scale = 1.0f / (tanf(g_scene_fov * 0.5f) * fabsf(projection[5]));
    projection[0] *= scale;
    projection[5] *= scale;
    projection[0x88u / sizeof(float)] = g_scene_fov;
}

static float mc_setting(const char *key, float lo, float hi) {
    return fminf(hi, fmaxf(lo, (float)cfg_double(key)));
}

static void sync_settings_locked(void) {
    static unsigned applied_version;
    static bool loaded;
    unsigned version = cfg_version();
    if (loaded && version == applied_version) return;
    applied_version = version;
    loaded = true;

    g_mc_eng.random_mob_spawns = cfg_bool("TOTK_MC_RANDOM_MOBS");
    g_mc_eng.shaders_enabled = cfg_bool("TOTK_MC_SHADERS") ? 1u : 0u;
    g_mc_eng.camera_distance = mc_setting("TOTK_MC_CAMERA_DISTANCE", 2.0f, 14.0f);
    float s = 0.2f + 0.6f * (mc_setting("TOTK_MC_MOUSE_SENS", 0.0f, 200.0f) / 200.0f);
    g_mc_eng.mouse_scale = 8.0f * s * s * s;
    atomic_store_explicit(&g_mc_mouse_gain, g_mc_eng.mouse_scale, memory_order_relaxed);
    g_mc_eng.gui_scale = mc_setting("TOTK_MC_GUI_SCALE", 0.5f, 2.0f);
    g_mc_eng.mainhand_scale = mc_setting("TOTK_MC_MAINHAND_SCALE", 0.4f, 2.0f);
    g_mc_eng.offhand_scale = mc_setting("TOTK_MC_OFFHAND_SCALE", 0.4f, 2.0f);
    g_mc_eng.fov_radians = mc_setting("TOTK_MC_FOV", 30.0f, 110.0f) * (float)M_PI / 180.0f;
    g_mc_eng.camera_shake = mc_setting("TOTK_MC_CAMERA_SHAKE", 0.0f, 100.0f) / 100.0f;
}

void runtime_mc_init(const RtLoaded *main_module) {
    plat_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(main_module);
    sync_settings_locked();
    if (rt_game_version == 121 && g_flight_view_guest) {
        g_mc_eng.flight_camera_available = 1;
        atomic_store_explicit(&g_flight_cam_seen, 1, memory_order_release);
        (void)sync_atomic_flags_locked();
    }
    plat_mutex_unlock(&g_mc_mu);
}

int runtime_mc_active(void) {
    return atomic_load_explicit(&g_mc_active_flag, memory_order_acquire);
}

int runtime_mc_wants_game_tick(void) {
    return atomic_load_explicit(&g_mc_wants_tick_flag, memory_order_acquire);
}

int runtime_mc_mode(void) {
    return atomic_load_explicit(&g_mc_mode_flag, memory_order_acquire);
}

int runtime_mc_view(void) {
    return atomic_load_explicit(&g_mc_view_flag, memory_order_acquire);
}

int runtime_mc_should_skip_player_draw(int is_gbuf, int index_count) {
    (void)is_gbuf;
    (void)index_count;
    return 0;
}

void runtime_mc_note_camera(const float *cam84, int count) {
    if (!cam84 || count < 74) return;
    if (!(cam84[56] > 0.0f && cam84[70] > 0.1f && cam84[70] < (float)M_PI)) return;
    for (int i = 0; i < 56; i++) {
        if (!isfinite(cam84[i])) return;
    }

    /* Always update the render snapshot's camera immediately under g_mc_snap_mu (~1 us) so
     * mc_vulkan_render_world never renders 3D blocks/mobs with a 1-frame-stale VP matrix. */
    plat_mutex_lock(&g_mc_snap_mu);
    memcpy(g_latest_cam_vp, cam84 + 12, sizeof(g_latest_cam_vp));
    g_latest_cam_pos[0]   = cam84[47]; g_latest_cam_pos[1]   = cam84[51]; g_latest_cam_pos[2]   = cam84[55];
    g_latest_cam_right[0] = cam84[0];  g_latest_cam_right[1] = cam84[1];  g_latest_cam_right[2] = cam84[2];
    g_latest_cam_up[0]    = cam84[4];  g_latest_cam_up[1]    = cam84[5];  g_latest_cam_up[2]    = cam84[6];
    g_latest_cam_fwd[0]   = -cam84[8]; g_latest_cam_fwd[1]   = -cam84[9]; g_latest_cam_fwd[2]   = -cam84[10];
    g_latest_cam_near     = cam84[56];
    g_latest_cam_far      = cam84[57];
    g_latest_cam_aspect   = cam84[62];
    g_latest_cam_fov_rad  = cam84[70];
    g_latest_cam_valid    = 1;
    if (atomic_load_explicit(&g_mc_snap_ready, memory_order_relaxed)) {
        apply_latest_cam_to_snap_locked(&g_mc_snap);
    }
    plat_mutex_unlock(&g_mc_snap_mu);

    if (plat_mutex_trylock(&g_mc_mu)) {
        ensure_initialized_locked(NULL);
        drain_pending_inputs_locked();
        mc_engine_note_camera(&g_mc_eng, cam84, count);
        publish_snapshot_locked();
        int changed = sync_atomic_flags_locked();
        plat_mutex_unlock(&g_mc_mu);
        if (changed) ikb_cursor_update();
        return;
    }
    plat_mutex_lock(&g_mc_input_mu);
    int n = count < 84 ? count : 84;
    memset(g_pending_cam84, 0, sizeof(g_pending_cam84));
    memcpy(g_pending_cam84, cam84, (size_t)n * sizeof(float));
    g_pending_cam_valid = 1;
    plat_mutex_unlock(&g_mc_input_mu);
}

void runtime_mc_game_tick(uint64_t frame) {
    plat_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(NULL);
    sync_settings_locked();
    drain_pending_inputs_locked();

    bool bow_pressed = false, bow_down = false, jump = false;
    runtime_input_mc_controls(&bow_pressed, &bow_down, &jump, &g_mc_eng.sprint_requested,
                               &g_mc_eng.move_right, &g_mc_eng.move_forward);
    const bool in_world = mc_engine_wants_mouse_capture(&g_mc_eng) != 0;
    const bool down = in_world && (bow_down || g_mc_mouse_use_down);
    const bool pressed = in_world && (bow_pressed || g_mc_mouse_use_pressed);
    if (in_world) {
        if (jump) g_mc_eng.pending_jump_press = 1;
        if (!g_mc_use_held && (pressed || down)) mc_engine_mouse_button(&g_mc_eng, 1, 1);
        if (!down && (g_mc_use_held || pressed)) mc_engine_mouse_button(&g_mc_eng, 1, 0);
    }
    g_mc_mouse_use_pressed = false;
    g_mc_use_held = down;
    if (atomic_load_explicit(&g_totk_cam_valid, memory_order_acquire)) {
        g_mc_eng.totk_cam_yaw = atomic_load_explicit(&g_totk_cam_yaw, memory_order_relaxed);
        g_mc_eng.totk_cam_pitch = atomic_load_explicit(&g_totk_cam_pitch, memory_order_relaxed);
        g_mc_eng.totk_cam_valid = 1;
    } else {
        g_mc_eng.totk_cam_valid = 0;
    }
    mc_engine_game_tick(&g_mc_eng, frame);
    if (rt_game_version == 121) {
        if ((g_mc_eng.tick_count % 8) == 0) {
            atomic_store_explicit(&options_open, mc_totk_options_open(), memory_order_release);
        }
    }
    drain_pending_inputs_locked();
    publish_snapshot_locked();
    int changed = sync_atomic_flags_locked();
    plat_mutex_unlock(&g_mc_mu);
    if (changed) {
        ikb_cursor_update();
    }
}

void runtime_mc_release_input(void) {
    atomic_store_explicit(&g_mc_sprint_flag, 0, memory_order_release);
    if (plat_mutex_trylock(&g_mc_mu)) {
        drain_pending_inputs_locked();
        release_input_locked();
        plat_mutex_unlock(&g_mc_mu);
        return;
    }
    plat_mutex_lock(&g_mc_input_mu);
    g_pending_ev_count = 0;
    g_pending_mouse_dx = g_pending_mouse_dy = 0.0f;
    g_pending_mouse_move = 0;
    g_pending_ev[g_pending_ev_count++] = (McPendingEvent){.kind = MC_EV_RELEASE};
    plat_mutex_unlock(&g_mc_input_mu);
    runtime_input_clear();
}

static int predict_key_consumed_fast(unsigned code, int down) {
#ifdef _WIN32
    if (code == 0xA0u || code == 0xA1u) code = 0x10u;
    else if (code == 0xA2u || code == 0xA3u) code = 0x11u;
#endif
    int active = atomic_load_explicit(&g_mc_active_flag, memory_order_acquire);
    int gp = atomic_load_explicit(&g_mc_gameplay_flag, memory_order_acquire);
    int cap = atomic_load_explicit(&g_mc_mouse_capture_flag, memory_order_acquire);
    if (!down) {
        if (code < 512u && g_fast_consume_up[code]) {
            g_fast_consume_up[code] = 0;
            return 1;
        }
        return (active && !cap) ? 1 : 0;
    }
    if (!gp) return 0;
    if (code == 0xC0u || code == 0x75u) { /* VK_OEM_3 (` / ~) / VK_F6 */
        if (code < 512u) g_fast_consume_up[code] = 1;
        return 1;
    }
    if (!active) return 0;
    if (!cap) {
        if (code < 512u) g_fast_consume_up[code] = 1;
        return 1;
    }
    if (code == 0x74u || code == 'V' || code == 'E' || code == 'F' ||
        code == 'G' || code == 0x20u || code == 'Q' || code == 'N' ||
        (code >= '1' && code <= '9')) {
        if (code < 512u) g_fast_consume_up[code] = 1;
        return 1;
    }
    return 0;
}

int runtime_mc_key(unsigned code, int down) {
    if (plat_mutex_trylock(&g_mc_mu)) {
        ensure_initialized_locked(NULL);
        drain_pending_inputs_locked();
        int consumed = mc_engine_key(&g_mc_eng, code, down);
        publish_snapshot_locked();
        int changed = sync_atomic_flags_locked();
        plat_mutex_unlock(&g_mc_mu);
        if (changed) {
            ikb_cursor_update();
        }
        return consumed;
    }
    plat_mutex_lock(&g_mc_input_mu);
    if (g_pending_ev_count < MC_MAX_PENDING_EVENTS) {
        g_pending_ev[g_pending_ev_count++] = (McPendingEvent){
            .kind = MC_EV_KEY, .code = code, .down = down
        };
    }
    int consumed = predict_key_consumed_fast(code, down);
    plat_mutex_unlock(&g_mc_input_mu);
    return consumed;
}

int runtime_mc_command(const char *line) {
    plat_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(NULL);
    drain_pending_inputs_locked();
    int handled = mc_engine_command(&g_mc_eng, line);
    int changed = 0;
    if (handled) {
        publish_snapshot_locked();
        changed = sync_atomic_flags_locked();
    }
    plat_mutex_unlock(&g_mc_mu);
    if (changed) ikb_cursor_update();
    return handled;
}

int runtime_mc_wants_mouse_capture(void) {
    return atomic_load_explicit(&g_mc_mouse_capture_flag, memory_order_acquire);
}

int runtime_mc_wants_sprint(void) {
    return atomic_load_explicit(&g_mc_sprint_flag, memory_order_acquire);
}

int runtime_mc_uses_native_mouse_look(void) {
    return runtime_mc_wants_mouse_capture() &&
           !atomic_load_explicit(&g_mc_flight_look_flag, memory_order_acquire);
}

int runtime_mc_ground_move_yaw_delta(float *out_delta_rad) {
    if (!out_delta_rad) return 0;
    if (!atomic_load_explicit(&g_mc_ground_move_active, memory_order_acquire)) return 0;
    if (!atomic_load_explicit(&g_totk_cam_valid, memory_order_acquire)) return 0;

    float totk_yaw = atomic_load_explicit(&g_totk_cam_yaw, memory_order_relaxed);
    float scale = atomic_load_explicit(&g_mc_mouse_gain, memory_order_relaxed);

    plat_mutex_lock(&g_mc_input_mu);
    float mc_yaw = g_mc_yaw_synced - g_pending_mouse_dx * scale * ((float)M_PI / 1200.0f);
    plat_mutex_unlock(&g_mc_input_mu);

    if (!isfinite(mc_yaw) || !isfinite(totk_yaw)) return 0;
    float delta = remainderf(mc_yaw - totk_yaw, 2.0f * (float)M_PI);
    if (!isfinite(delta)) return 0;
    *out_delta_rad = delta;
    return 1;
}

float runtime_mc_mouse_gain(void) {
    return atomic_load_explicit(&g_mc_mouse_gain, memory_order_relaxed);
}

void runtime_mc_mouse_move(float dx, float dy) {
    if (!atomic_load_explicit(&g_mc_mouse_capture_flag, memory_order_acquire) ||
        !isfinite(dx) || !isfinite(dy)) {
        return;
    }
    plat_mutex_lock(&g_mc_input_mu);
    g_pending_mouse_dx += dx;
    g_pending_mouse_dy += dy;
    g_pending_mouse_move = 1;
    plat_mutex_unlock(&g_mc_input_mu);

    if (runtime_mc_uses_native_mouse_look() && !input_direct_mouse_enabled()) {
        float scale = runtime_mc_mouse_gain();
        mc_totk_set_camera_look(dx * scale, dy * scale);
    }
}

void runtime_mc_cursor_pos(float nx, float ny) {
    if (!isfinite(nx) || !isfinite(ny)) return;
    if (nx < 0.0f) nx = 0.0f;
    else if (nx > 1.0f) nx = 1.0f;
    if (ny < 0.0f) ny = 0.0f;
    else if (ny > 1.0f) ny = 1.0f;

    if (plat_mutex_trylock(&g_mc_mu)) {
        if (g_mc_eng.initialized) {
            drain_pending_inputs_locked();
            mc_engine_cursor_pos(&g_mc_eng, nx, ny);
            if (g_mc_eng.inv_open != MC_INV_CLOSED) {
                publish_snapshot_locked();
            }
        }
        plat_mutex_unlock(&g_mc_mu);
        return;
    }
    plat_mutex_lock(&g_mc_input_mu);
    g_pending_cursor_nx = nx;
    g_pending_cursor_ny = ny;
    g_pending_cursor_dirty = 1;
    plat_mutex_unlock(&g_mc_input_mu);
}

void runtime_mc_mouse_button(int button, int down) {
    if (plat_mutex_trylock(&g_mc_mu)) {
        if (g_mc_eng.initialized) {
            drain_pending_inputs_locked();
            if (mc_engine_wants_mouse_capture(&g_mc_eng) && button == 1) {
                if (down && !g_mc_mouse_use_down) g_mc_mouse_use_pressed = true;
                g_mc_mouse_use_down = down != 0;
            } else {
                mc_engine_mouse_button(&g_mc_eng, button, down);
            }
            publish_snapshot_locked();
            int changed = sync_atomic_flags_locked();
            plat_mutex_unlock(&g_mc_mu);
            if (changed) ikb_cursor_update();
            return;
        }
        plat_mutex_unlock(&g_mc_mu);
        return;
    }
    plat_mutex_lock(&g_mc_input_mu);
    if (g_pending_ev_count < MC_MAX_PENDING_EVENTS) {
        g_pending_ev[g_pending_ev_count++] = (McPendingEvent){
            .kind = MC_EV_MOUSE_BTN, .code = (unsigned)button, .down = down
        };
    }
    plat_mutex_unlock(&g_mc_input_mu);
}

void runtime_mc_scroll(float delta_y) {
    if (plat_mutex_trylock(&g_mc_mu)) {
        if (g_mc_eng.initialized) {
            drain_pending_inputs_locked();
            mc_engine_scroll(&g_mc_eng, delta_y);
            publish_snapshot_locked();
            int changed = sync_atomic_flags_locked();
            plat_mutex_unlock(&g_mc_mu);
            if (changed) ikb_cursor_update();
            return;
        }
        plat_mutex_unlock(&g_mc_mu);
        return;
    }
    plat_mutex_lock(&g_mc_input_mu);
    if (g_pending_ev_count < MC_MAX_PENDING_EVENTS) {
        g_pending_ev[g_pending_ev_count++] = (McPendingEvent){
            .kind = MC_EV_SCROLL, .fval = delta_y
        };
    }
    plat_mutex_unlock(&g_mc_input_mu);
}

void mc_snapshot_copy(McRenderSnapshot *out) {
    if (!out) return;
    if (!atomic_load_explicit(&g_mc_snap_ready, memory_order_acquire)) {
        memset(out, 0, sizeof(*out));
        return;
    }
    plat_mutex_lock(&g_mc_snap_mu);
    memcpy(out, &g_mc_snap, sizeof(*out));
    plat_mutex_unlock(&g_mc_snap_mu);
}

int runtime_mc_render_frame(uint8_t *rgba_out, int w, int h) {
    (void)rgba_out;
    (void)w;
    (void)h;
    /* Rendering is handled directly in Vulkan by mc_vulkan_render.c via mc_snapshot_copy(). */
    return 0;
}

void runtime_mc_audio_mix(float *stereo_io, int frames, int rate) {
    mc_audio_mix(stereo_io, frames, rate);
}
