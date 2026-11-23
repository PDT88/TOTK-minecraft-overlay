#include "minecraft_overlay.h"
#include "mc_assets.h"
#include "mc_audio.h"
#include "mc_engine.h"
#include "mc_totk_bridge.h"

#include <TargetConditionals.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

static pthread_mutex_t g_mc_mu = PTHREAD_MUTEX_INITIALIZER;
static McEngine g_mc_eng;
static McRenderSnapshot g_mc_snap;

static atomic_int g_mc_initialized = 0;
static int g_bridge_inited = 0;
#if defined(TOTK_GAME_V121) && !TARGET_OS_IPHONE
void runtime_mc_publish_projection(float radians, int first_person);
static atomic_int options_open;
int runtime_mc_options_open(void) { return atomic_load(&options_open); }
#endif
static bool g_mc_use_held;
static bool g_mc_mouse_use_down, g_mc_mouse_use_pressed;
static atomic_int g_mc_active_flag = 0;
static atomic_int g_mc_wants_tick_flag = 0;
static atomic_int g_mc_mode_flag = 1;
static atomic_int g_mc_view_flag = 0;
static atomic_int g_mc_mouse_capture_flag = 0;
static atomic_int g_mc_sprint_allowed = 0;

static void sync_atomic_flags_locked(void) {
    int gp = (g_mc_eng.initialized && g_mc_eng.active && g_mc_eng.gameplay_active) ? 1 : 0;
    atomic_store_explicit(&g_mc_wants_tick_flag, (g_mc_eng.initialized && g_mc_eng.active) ? 1 : 0, memory_order_release);
    atomic_store_explicit(&g_mc_active_flag, (gp && g_mc_eng.mode == 1) ? 1 : 0, memory_order_release);
    atomic_store_explicit(&g_mc_mode_flag, gp ? g_mc_eng.mode : 0, memory_order_release);
    atomic_store_explicit(&g_mc_view_flag, g_mc_eng.view_mode, memory_order_release);
    const bool in_world = mc_engine_wants_mouse_capture(&g_mc_eng);
    if (!in_world) {
        g_mc_mouse_use_down = g_mc_mouse_use_pressed = g_mc_use_held = false;
    }
    atomic_store_explicit(&g_mc_mouse_capture_flag, in_world, memory_order_release);
    atomic_store_explicit(&g_mc_sprint_allowed, in_world && !g_mc_eng.elytra_gliding, memory_order_release);
}

void runtime_mc_save_base(void) {
    pthread_mutex_lock(&g_mc_mu);
    mc_engine_save_base(&g_mc_eng);
    pthread_mutex_unlock(&g_mc_mu);
}

static void ensure_initialized_locked(Module *main_module) {
    if (main_module && !g_bridge_inited) {
        mc_totk_bridge_init(main_module);
        g_bridge_inited = 1;
    }
    if (!g_mc_eng.initialized) {
        mc_audio_init();
        mc_assets_init(NULL);
        mc_engine_init(&g_mc_eng);
        mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
        sync_atomic_flags_locked();
        atomic_store_explicit(&g_mc_initialized, 1, memory_order_release);
    }
}

#if defined(TOTK_GAME_V121) && !TARGET_OS_IPHONE
typedef void (*SetSceneCamera)(void *, unsigned, const float *, const void *, const float *);
static SetSceneCamera g_set_scene_camera;

static void set_minecraft_scene_camera(void *view_set, unsigned index, const float *view,
                                    const void *projection, const float *reference_view) {
    if (atomic_load_explicit(&g_mc_active_flag, memory_order_acquire)) {
        _Alignas(16) float flight_view[12];
        int use_flight_view = 0;
        pthread_mutex_lock(&g_mc_mu);
        if (g_mc_eng.mode == 1 && g_mc_eng.gameplay_active && g_mc_eng.player_pos_valid) {
            float distance_squared = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                float camera = -view[axis] * view[3] - view[4 + axis] * view[7]
                             - view[8 + axis] * view[11];
                float delta = camera - g_mc_eng.player_pos[axis];
                distance_squared += delta * delta;
            }
            if (distance_squared < 16.0f * 16.0f) {
                if (g_mc_eng.view_mode != MC_VIEW_FIRST_PERSON && g_mc_eng.elytra_gliding && g_mc_eng.flight_view_ready) {
                    memcpy(flight_view, g_mc_eng.flight_view, sizeof(flight_view));
                    use_flight_view = 1;
                } else if (g_mc_eng.view_mode == MC_VIEW_FIRST_PERSON ||
                           (!g_mc_eng.elytra_gliding && g_mc_eng.ground_camera_distance > 0)) {
                    float eye[3];
                    float distance = 0.0f;
                    if (g_mc_eng.view_mode == MC_VIEW_FIRST_PERSON) {
                        mc_engine_first_person_eye(&g_mc_eng, eye);
                    } else {
                        memcpy(eye, g_mc_eng.player_pos, sizeof(eye));
                        eye[1] += 1.5f;
                        distance = g_mc_eng.ground_camera_distance;
                    }
                    memcpy(flight_view, view, sizeof(flight_view));
                    for (int row = 0; row < 3; ++row) {
                        const float *basis = view + 4 * row;
                        flight_view[4 * row + 3] = -(basis[0] * eye[0] + basis[1] * eye[1] + basis[2] * eye[2]);
                    }
                    flight_view[11] -= distance;
                    use_flight_view = 1;
                }
            }
        }
        pthread_mutex_unlock(&g_mc_mu);
        if (use_flight_view) {
            g_set_scene_camera(view_set, index, flight_view, projection, flight_view);
            return;
        }
    }
    g_set_scene_camera(view_set, index, view, projection, reference_view);
}

static void install_scene_camera(Module *module) {
    /* Replace the three scene-view inputs before Zelda builds the frustum and render uniforms. */
    static const uint64_t calls[] = {0x974fb8, 0x975008, 0x975058};
    static const uint32_t expected[] = {0x9400264c, 0x94002638, 0x94002624};
    const uint64_t setter = 0x97e8e8;
    const uint64_t stub = ((module->text_end + 15) & ~UINT64_C(15)) + 0x400;
    const uint64_t text_page_end = (module->text_end + 0x3fff) & ~UINT64_C(0x3fff);
    const uint32_t expected_setter = 0xa9bb7bfd;
    const uint8_t empty[16] = {0};
    if (setter + 4 > module->text_end || stub + sizeof(empty) > text_page_end ||
        memcmp(module->base + setter, &expected_setter, 4) ||
        memcmp(module->base + stub, empty, sizeof(empty))) {
        fprintf(stderr, "[mc] Flight camera hook unavailable for this game image\n");
        return;
    }
    for (size_t i = 0; i < sizeof(calls) / sizeof(*calls); ++i) {
        if (memcmp(module->base + calls[i], &expected[i], sizeof(expected[i]))) {
            fprintf(stderr, "[mc] Flight camera call site did not match this game image\n");
            return;
        }
    }
    g_set_scene_camera = (SetSceneCamera)(void *)(module->base + setter);
    uint64_t target = (uintptr_t)runtime_hle_wrap_named((void *)set_minecraft_scene_camera, "Minecraft scene camera");
    /* ldr x16, +8; br x16; .quad wrapper. The original BL supplies the return address. */
    const uint32_t veneer[] = {0x58000050, 0xd61f0200};
    memcpy(module->base + stub, veneer, sizeof(veneer));
    memcpy(module->base + stub + sizeof(veneer), &target, sizeof(target));
    for (size_t i = 0; i < sizeof(calls) / sizeof(*calls); ++i) {
        uint32_t branch = 0x94000000u | ((uint32_t)((stub - calls[i]) / 4) & 0x03ffffffu);
        memcpy(module->base + calls[i], &branch, sizeof(branch));
    }
    g_mc_eng.flight_camera_available = 1;
}
#endif

void runtime_mc_init(Module *main_module) {
    pthread_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(main_module);
#if defined(TOTK_GAME_V121) && !TARGET_OS_IPHONE
    if (main_module && !g_set_scene_camera) install_scene_camera(main_module);
#endif
    pthread_mutex_unlock(&g_mc_mu);
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
    /* Link 3D visibility is managed cleanly in guest memory via mc_totk_bridge. */
    return 0;
}

void runtime_mc_note_camera(const float *cam84, int count) {
    if (!cam84 || count < 74) return;
    pthread_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(NULL);
    mc_engine_note_camera(&g_mc_eng, cam84, count);
    mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
    sync_atomic_flags_locked();
    pthread_mutex_unlock(&g_mc_mu);
}

void runtime_mc_apply_settings(float distance, float sensitivity, float fov, int random_mob_spawns,
                               float gui_scale, float mainhand_scale, float offhand_scale) {
    pthread_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(NULL);
    g_mc_eng.random_mob_spawns = random_mob_spawns != 0;
    g_mc_eng.camera_distance = distance;
    g_mc_eng.mouse_scale = sensitivity;
    g_mc_eng.gui_scale = gui_scale;
    g_mc_eng.mainhand_scale = mainhand_scale;
    g_mc_eng.offhand_scale = offhand_scale;
    g_mc_eng.fov_radians = fov * (float)M_PI / 180.0f;
    pthread_mutex_unlock(&g_mc_mu);
}

void runtime_mc_game_tick(uint64_t frame) {
    pthread_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(NULL);
    bool bow_pressed, bow_down, jump, sprint;
    runtime_input_mc_controls(&bow_pressed, &bow_down, &jump, &sprint, &g_mc_eng.move_right, &g_mc_eng.move_forward);
    const bool in_world = mc_engine_wants_mouse_capture(&g_mc_eng);
    g_mc_eng.sprinting = in_world && sprint && !g_mc_eng.elytra_gliding;
    const bool down = in_world && (bow_down || g_mc_mouse_use_down);
    const bool pressed = in_world && (bow_pressed || g_mc_mouse_use_pressed);
    if (in_world) {
        if (jump) g_mc_eng.pending_jump_press = 1;
        if (!g_mc_use_held && (pressed || down)) mc_engine_mouse_button(&g_mc_eng, 1, 1);
        if (!down && (g_mc_use_held || pressed)) mc_engine_mouse_button(&g_mc_eng, 1, 0);
    }
    g_mc_mouse_use_pressed = false;
    g_mc_use_held = down;
    mc_engine_game_tick(&g_mc_eng, frame);
#if defined(TOTK_GAME_V121) && !TARGET_OS_IPHONE
    if ((g_mc_eng.tick_count % 8) == 0) atomic_store(&options_open, mc_totk_options_open());
    int mc_camera = g_mc_eng.gameplay_active && g_mc_eng.active && g_mc_eng.mode == 1;
    runtime_mc_publish_projection(mc_camera ? g_mc_eng.fov_radians : 0,
                                  mc_camera && g_mc_eng.flight_camera_available &&
                                  g_mc_eng.view_mode == MC_VIEW_FIRST_PERSON);
#endif
    mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
    sync_atomic_flags_locked();
    pthread_mutex_unlock(&g_mc_mu);
}

int runtime_mc_key(unsigned code, int down) {
    pthread_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(NULL);
    int consumed = mc_engine_key(&g_mc_eng, code, down);
    mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
    sync_atomic_flags_locked();
    pthread_mutex_unlock(&g_mc_mu);
    return consumed;
}

int runtime_mc_command(const char *line) {
    pthread_mutex_lock(&g_mc_mu);
    ensure_initialized_locked(NULL);
    int handled = mc_engine_command(&g_mc_eng, line);
    if (handled) {
        mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
        sync_atomic_flags_locked();
    }
    pthread_mutex_unlock(&g_mc_mu);
    return handled;
}

int runtime_mc_wants_mouse_capture(void) {
    return atomic_load_explicit(&g_mc_mouse_capture_flag, memory_order_acquire);
}

int runtime_mc_allows_sprint(void) {
    return atomic_load_explicit(&g_mc_sprint_allowed, memory_order_acquire);
}

void runtime_mc_mouse_move(float dx, float dy) {
    pthread_mutex_lock(&g_mc_mu);
    if (g_mc_eng.initialized) {
        mc_engine_mouse_move(&g_mc_eng, dx, dy);
    }
    pthread_mutex_unlock(&g_mc_mu);
}

void runtime_mc_cursor_pos(float nx, float ny) {
    pthread_mutex_lock(&g_mc_mu);
    if (g_mc_eng.initialized) {
        mc_engine_cursor_pos(&g_mc_eng, nx, ny);
        if (g_mc_eng.inv_open != MC_INV_CLOSED) {
            mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
        }
    }
    pthread_mutex_unlock(&g_mc_mu);
}

void runtime_mc_mouse_button(int button, int down) {
    pthread_mutex_lock(&g_mc_mu);
    if (g_mc_eng.initialized) {
        if (mc_engine_wants_mouse_capture(&g_mc_eng) && button == 1) {
            if (down && !g_mc_mouse_use_down) g_mc_mouse_use_pressed = true;
            g_mc_mouse_use_down = down;
        } else {
            mc_engine_mouse_button(&g_mc_eng, button, down);
        }
        mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
        sync_atomic_flags_locked();
    }
    pthread_mutex_unlock(&g_mc_mu);
}

void runtime_mc_scroll(float delta_y) {
    pthread_mutex_lock(&g_mc_mu);
    if (g_mc_eng.initialized) {
        mc_engine_scroll(&g_mc_eng, delta_y);
        mc_engine_export_snapshot(&g_mc_eng, &g_mc_snap);
        sync_atomic_flags_locked();
    }
    pthread_mutex_unlock(&g_mc_mu);
}

void mc_snapshot_copy(McRenderSnapshot *out) {
    if (!out) return;
    pthread_mutex_lock(&g_mc_mu);
    if (g_mc_eng.initialized) {
        memcpy(out, &g_mc_snap, sizeof(*out));
    } else {
        memset(out, 0, sizeof(*out));
    }
    pthread_mutex_unlock(&g_mc_mu);
}

int runtime_mc_render_frame(uint8_t *rgba_out, int w, int h) {
    (void)rgba_out;
    (void)w;
    (void)h;
    /* Rendering is handled directly in Metal by mc_metal_render.m via mc_snapshot_copy(). */
    return 0;
}

void runtime_mc_audio_mix(float *stereo_io, int frames, int rate) {
    mc_audio_mix(stereo_io, frames, rate);
}
