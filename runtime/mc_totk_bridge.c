/*
 * Guest-side TOTK v1.2.1 runtime bridge for Minecraft overlay integration
 * (ported to totk-pc-recomp Windows x86-64).
 * Runs guest memory/function operations strictly on the guest frame thread.
 */
#include "mc_totk_bridge.h"
#include "layout.h"
#include "platform/platform.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Cross-thread Mouse-Look -> Switch Right-Stick Accumulator (all versions)
 * -------------------------------------------------------------------------- */
static PlatMutex g_cam_lock = PLAT_MUTEX_INIT;
static double g_cam_dx = 0.0;
static double g_cam_dy = 0.0;
static int32_t g_cam_hold_rx = 0;
static int32_t g_cam_hold_ry = 0;
static uint64_t g_cam_hold_ns = 0;

void mc_totk_set_camera_look(float dx, float dy) {
    if (!isfinite(dx) || !isfinite(dy)) return;
    plat_mutex_lock(&g_cam_lock);
    g_cam_dx += (double)dx;
    g_cam_dy += (double)dy;
    /* Cap queued look delta to prevent unbounded accumulation. */
    if (g_cam_dx > 512.0) g_cam_dx = 512.0;
    if (g_cam_dx < -512.0) g_cam_dx = -512.0;
    if (g_cam_dy > 512.0) g_cam_dy = 512.0;
    if (g_cam_dy < -512.0) g_cam_dy = -512.0;
    plat_mutex_unlock(&g_cam_lock);
}

void mc_totk_clear_camera_look(void) {
    plat_mutex_lock(&g_cam_lock);
    g_cam_dx = 0.0;
    g_cam_dy = 0.0;
    g_cam_hold_rx = 0;
    g_cam_hold_ry = 0;
    g_cam_hold_ns = 0;
    plat_mutex_unlock(&g_cam_lock);
}

static int32_t mouse_delta_to_stick(double d) {
    const double deadzone = 5400.0;
    const double scale = 1250.0;
    double ad = fabs(d);
    double mag;
    if (ad < 0.08) return 0;
    mag = deadzone + ad * scale;
    if (mag > 32767.0) mag = 32767.0;
    return (d > 0.0) ? (int32_t)mag : -(int32_t)mag;
}

void mc_totk_sample_camera_stick(int32_t *out_rx, int32_t *out_ry) {
    uint64_t now_ns = plat_now_ns();
    int32_t rx, ry;

    plat_mutex_lock(&g_cam_lock);
    if (g_cam_hold_ns == 0 || (now_ns - g_cam_hold_ns) >= 12000000ull ||
        (g_cam_hold_rx == 0 && g_cam_hold_ry == 0 && (g_cam_dx != 0.0 || g_cam_dy != 0.0))) {
        /* Mouse deltaY > 0 is downward; Switch right-stick +ry looks upward. */
        g_cam_hold_rx = mouse_delta_to_stick(g_cam_dx);
        g_cam_hold_ry = mouse_delta_to_stick(-g_cam_dy);
        /* Each sample consumes all input so stick saturation cannot leave a turn queued. */
        g_cam_dx = 0.0;
        g_cam_dy = 0.0;
        g_cam_hold_ns = now_ns ? now_ns : 1ull;
    }
    rx = g_cam_hold_rx;
    ry = g_cam_hold_ry;
    plat_mutex_unlock(&g_cam_lock);

    if (out_rx) *out_rx = rx;
    if (out_ry) *out_ry = ry;
}

/* --------------------------------------------------------------------------
 * TOTK v1.2.1 Offsets, Constants & State
 * -------------------------------------------------------------------------- */
enum {
    /* Globals: SCENE_SLOT is BSS (single deref); GOT entries require double deref. */
    OFF_SCENE_SLOT            = 0x4728538u,
    OFF_GOT_PHYSICS_MGR       = 0x462e038u,
    OFF_GOT_RAY_VT            = 0x462e158u,
    OFF_GOT_ACTORMGR          = 0x462e3c8u,
    OFF_GOT_STAMINA_PARAM     = 0x462df08u,
    OFF_GOT_PLAYER_STATUS     = 0x462fe78u,
    OFF_GOT_UI_MGR            = 0x462fec8u,

    /* Verified TOTK v1.2.1 functions */
    FN_LIFECOMP_SYNC          = 0x064bf28u,
    FN_DEATH_REQUEST         = 0x064cc14u,
    FN_FORCE_SET_MATRIX       = 0x06ba86cu,
    FN_RAY_EXEC               = 0x07d0884u,
    FN_CCT_IS_ON_GROUND       = 0x084e6b4u,
    FN_RB_SET_VEL             = 0x0acc874u,
    FN_UI_FIND_LOC            = 0x0b66ef0u,
    FN_RAY_CTOR               = 0x0b7f7a4u,
    FN_RAY_MASK               = 0x0cb8710u,
    FN_UI_FIND_MAP            = 0x1085a18u,
    FN_UI_FIND_STAM           = 0x11b851cu,
    FN_UI_FIND_CTRL_GUIDE     = 0x11eb06cu,
    FN_STAMINA_ADD            = 0x1202684u,
    FN_DAMAGE_INFO_CTOR       = 0x133e520u,
    FN_ADD_DAMAGE_ENTRY       = 0x133e5c4u,
    FN_UI_FIND_HEART          = 0x1587a70u,
    FN_CCT_SET_VEL            = 0x160bba4u,
    FN_CCT_GET_VERT_VEL       = 0x19241a4u,
    FN_UI_FIND_DOCMD          = 0x19a4aecu,

    HASH_UI_HEART             = 0x5c7f1c3cu,
    HASH_UI_STAMINA           = 0xb5576be1u,
    HASH_UI_MAP               = 0x69aa084fu,
    HASH_UI_DOCMD             = 0x2d9798a8u,
    HASH_UI_CTRL_GUIDE        = 0x5528d3d9u,
    HASH_UI_LOC               = 0x62fedc9bu,

    VT_UI_ACTION_GUIDE        = 0x43d9250u,
    VT_UI_ENV                 = 0x43de938u,
    VT_UI_HORSE_GAUGE         = 0x43e0ce8u,
    VT_UI_PICKUP              = 0x43e4e80u,
    VT_UI_SHORTCUT_BG         = 0x43e7ca8u,
    VT_UI_SHORTCUT_MAIN       = 0x43e7fe0u,
    VT_UI_SHORTCUT            = 0x43e8330u,
    VT_UI_TIME                = 0x43eaaf0u,

    /* Blocking / non-gameplay UI screen primary vtables (verified via .rela.dyn in main.elf) */
    /* Group A: Title / Logo / GameOver / Ending / Save-Load */
    VT_UI_TITLE                    = 0x43eae28u,
    VT_UI_GAME_TITLE_LOGO          = 0x43dfca8u,
    VT_UI_GAME_OVER                = 0x43df970u,
    VT_UI_STAFF_ROLL               = 0x43e9088u,
    VT_UI_FINISH                   = 0x43df638u,
    VT_UI_SYSTEM_SAVE_LOAD         = 0x43ea0c0u,
    VT_UI_SYSTEM_LOAD_LIST         = 0x43e9a38u,

    /* Group B: Fade transitions (blocking when opening or RootPane visible with alpha > 0) */
    VT_UI_LOADING_FADE             = 0x43e1d40u,
    VT_UI_EVENT_FADE               = 0x43dec70u,
    VT_UI_EVENT_FADE_WIPE          = 0x43defc8u,

    /* Group C: Pause / Pouch / Fullscreen AppMenu / System / Cutscene / Dialog overlays */
    VT_UI_PAUSE_MENU_BG            = 0x43e4810u,
    VT_UI_PAUSE_MENU_OVERLAY       = 0x43e4b48u,
    VT_UI_APP_MENU_OVERLAY         = 0x43da0d8u,
    VT_UI_POUCH                    = 0x43e6550u,
    VT_UI_ALBUM                    = 0x43d9928u,
    VT_UI_PICTURE_BOOK             = 0x43e51b8u,
    VT_UI_SYSTEM_OPTION            = 0x43e9d70u,
    VT_UI_SYSTEM_WINDOW            = 0x43ea400u,
    VT_UI_SYSTEM_ACTION_GUIDE      = 0x43e96f8u,
    VT_UI_SHOP_MENU                = 0x43e78f0u,
    VT_UI_DYE_SHOP_MENU            = 0x43de2c8u,
    VT_UI_HOUSE_SHOP_MENU          = 0x43e1020u,
    VT_UI_OWNED_HORSE_LIST         = 0x43e40c8u,
    VT_UI_PARASAIL_PATTERN_CHANGE  = 0x43e44b0u,
    VT_UI_AUTO_BUILDER             = 0x43db338u,
    VT_UI_AMIIBO_WINDOW            = 0x43d9d20u,
    VT_UI_CAMERA_SYSTEM_WINDOW     = 0x43dc448u,
    VT_UI_CHALLENGE                = 0x43dcdf0u,
    VT_UI_EVENT_SKIP               = 0x43df300u,
    VT_UI_MESSAGE_DEMO             = 0x43e2c00u,
    VT_UI_MESSAGE_DIALOG           = 0x43e2f38u,
    VT_UI_MESSAGE_DUNGEON          = 0x43e32f8u,
    VT_UI_MESSAGE_LOG_01           = 0x43e3630u,
    VT_UI_OPENING_TEXT             = 0x43e3d90u,
    VT_UI_DEMO_NICKNAME            = 0x43ddb50u,
    VT_UI_APP_OPEN_DEMO_WINDOW     = 0x43da598u,
    VT_UI_GET_WINDOW               = 0x43dffe0u,
    VT_UI_SPECIAL_POWER_GET_WINDOW = 0x43e8d08u,
    VT_UI_HEART_GAUGE_DEMO         = 0x43e0978u,
    VT_UI_STAMINA_GAUGE_DEMO       = 0x43e93c0u,
    VT_UI_INSTANT_TIPS             = 0x43e1698u,
    VT_UI_INSTANT_TIPS_WITH_PICT   = 0x43e19d8u,
    VT_UI_INSTANT_TIPS_APP_MENU    = 0x43e1358u,
    VT_UI_ACTION_GUIDE_WITH_PICT   = 0x43d9588u,
    VT_UI_RECIPE_CARD              = 0x43e6bc0u,
    VT_UI_CHARA_DIRECTORY          = 0x43dd130u,

    /* Actor & component field offsets */
    ACT_STATE_OFF             = 0x024u,
    ACT_NAME_OFF              = 0x218u,
    ACT_COMP_OFF              = 0x228u,
    COMP_DEATH_OFF           = 0x1c8u,
    COMP_DEATH_AUX_OFF       = 0x208u,
    ACT_POS_OFF               = 0x2b4u,
    ACT_ROT_OFF               = 0x2c0u,
    ACT_SCALE_OFF             = 0x2e4u,
    ACT_AABB_MIN_OFF          = 0x350u,
    ACT_AABB_MAX_OFF          = 0x35cu,
    ACT_VIS_FLAGS_OFF         = 0x444u,

    COMP_MODEL_WRAP_OFF       = 0x008u,
    COMP_ACTION_CHAIN_OFF     = 0x040u,
    COMP_PHYSICS_OFF          = 0x050u,
    COMP_PARASAIL_CHAIN_OFF   = 0x158u,
    COMP_LIFE_OFF             = 0x2f8u,
    COMP_STAMINA_OFF          = 0x3a8u,
    COMP_DMG_RECEIVER_OFF     = 0x448u,

    CCT_HOLDER_OFF            = 0x020u,
    CCT_FROM_HOLDER_OFF       = 0x010u,
    RB_FROM_HOLDER_OFF        = 0x150u,

    LIFE_CUR_HOLDER_OFF       = 0x1830u,
    LIFE_MAX_HOLDER_OFF       = 0x1838u,
    LIFE_EXTRA_HOLDER_OFF     = 0x1840u,
    LIFE_GLOOM_HOLDER_OFF     = 0x1848u,

    STAM_EXHAUSTED_OFF        = 0x13e0u,
    STAM_CUR_A_OFF            = 0x13e4u,
    STAM_CUR_B_OFF            = 0x13e8u,
    STAM_MAX_A_OFF            = 0x13fcu,

    VIS_BIT_ACTOR_23          = 0x00800000u
};

/* Guest-visible kernel scratch buffer layout (below 2^39) for guest function calls. */
#define SCRATCH_SIZE        0x2000u
#define SCRATCH_RAY_OFF     0x0000u  /* 0x240 bytes */
#define SCRATCH_MAT_OFF     0x0240u  /* 12 floats = 0x30 bytes */
#define SCRATCH_VEL_OFF     0x0280u  /* 3 floats = 0x0c bytes */
#define SCRATCH_POS_OFF     0x0290u  /* 3 floats = 0x0c bytes */
#define SCRATCH_DIR_OFF     0x02a0u  /* 3 floats = 0x0c bytes */
#define SCRATCH_DINFO_OFF   0x0300u  /* 0x1a00 bytes (ends at 0x1d00) */

#define VIS_ENTRY_CAP     64
#define HUD_BYTE_CAP      512

typedef struct {
    int       hidden;
    int       bit23_saved;
    uintptr_t actor;
    int       had_bit23;
    int       entry_count;
    uintptr_t entry_ptr[VIS_ENTRY_CAP];
    uintptr_t sub_ptr[VIS_ENTRY_CAP];
    uint8_t   orig_bit20[VIS_ENTRY_CAP];
    int32_t   orig_vis_i32[VIS_ENTRY_CAP];
} SavedPlayerVis;

typedef struct {
    uintptr_t addr;
    uintptr_t screen;
    uint8_t   orig_val;
    uint8_t   clear_mask;
} SavedHudByte;

typedef struct {
    int          hidden;
    uintptr_t    status_byte_addr;
    uint8_t      orig_status_bits;
    int          byte_count;
    SavedHudByte bytes[HUD_BYTE_CAP];
} SavedHudVis;

static uintptr_t      g_main_base;
static size_t         g_main_size;
static uintptr_t      g_text_end_abs;
static int            g_initialized;
static int            g_force_matrix_verified;
static uint8_t       *g_scratch;

static atomic_int     g_guest_tid_set = 0;
static uint32_t       g_guest_tid = 0;
static atomic_int     g_offthread_warned = 0;

static float          g_seen_max_stamina = 1000.0f;
static uintptr_t      g_stamina_player;
static int            g_expected_hp_quarters = -1;
static uintptr_t      g_hp_player;
static atomic_uintptr_t g_mc_player_life;
static PlatMutex      g_player_health_lock = PLAT_MUTEX_INIT;
static int            g_pending_player_damage;
static int            g_health_hook_available;

static SavedPlayerVis g_player_vis;
static SavedHudVis    g_hud_vis;

/* --------------------------------------------------------------------------
 * Safe Guest Memory, Code Validation & Guest Function Call Helpers
 * -------------------------------------------------------------------------- */
static int read_mem(uintptr_t addr, void *out, size_t size) {
    if (!out || !size) return 0;
    if (!addr || addr < 0x10000u || (uint64_t)addr > UINT64_MAX - (uint64_t)size) return 0;
    if ((uint64_t)addr >= RT_ADDRESS_LIMIT || (uint64_t)addr + (uint64_t)size > RT_ADDRESS_LIMIT) return 0;
    if (!mem_guest_readable((uint64_t)addr, (uint64_t)size)) return 0;
    memcpy(out, (const void *)addr, size);
    return 1;
}

static int read_only_module(uint64_t a, uint64_t n) {
    for (int i = 0; i < ldr_count(); i++) {
        const RtLoaded *m = ldr_module(i);
        if (a >= m->base && a < m->base + m->size) {
            return (a - m->base < m->data_off) || (a + n > m->base + m->size);
        }
    }
    return 0;
}

static int is_writable_guest(uintptr_t addr, size_t size) {
    if (!addr || addr < 0x100000u || (uint64_t)addr > UINT64_MAX - (uint64_t)size) return 0;
    if ((uint64_t)addr + (uint64_t)size > RT_ADDRESS_LIMIT) return 0;
    if (!mem_guest_readable((uint64_t)addr, (uint64_t)size)) return 0;
    return !read_only_module((uint64_t)addr, (uint64_t)size);
}

static uintptr_t read_ptr(uintptr_t addr) {
    uintptr_t p = 0;
    return read_mem(addr, &p, sizeof(p)) ? p : 0;
}

static int read_u8(uintptr_t addr, uint8_t *out) {
    return read_mem(addr, out, sizeof(*out));
}

static int read_u32(uintptr_t addr, uint32_t *out) {
    return read_mem(addr, out, sizeof(*out));
}

static int read_i32(uintptr_t addr, int32_t *out) {
    return read_mem(addr, out, sizeof(*out));
}

static int read_u64(uintptr_t addr, uint64_t *out) {
    return read_mem(addr, out, sizeof(*out));
}

static int read_f32(uintptr_t addr, float *out) {
    return read_mem(addr, out, sizeof(*out));
}

static uintptr_t read_got_instance(uint32_t got_rva) {
    uintptr_t slot;
    if (!g_initialized || !g_main_base || (size_t)got_rva + 8u > g_main_size) return 0;
    slot = read_ptr(g_main_base + got_rva);
    return slot ? read_ptr(slot) : 0;
}

static int is_text_ptr(uintptr_t addr) {
    return g_initialized && addr >= g_main_base && addr + 4u <= g_text_end_abs && (addr & 3u) == 0;
}

static uintptr_t text_addr(uint32_t rva) {
    uintptr_t p;
    if (!g_initialized || !g_main_base) return 0;
    if ((uintptr_t)rva > g_text_end_abs - g_main_base) return 0;
    p = g_main_base + rva;
    return is_text_ptr(p) ? p : 0;
}

static uintptr_t main_data_addr(uint32_t rva) {
    if (!g_initialized || !g_main_base) return 0;
    if ((size_t)rva + 8u > g_main_size) return 0;
    return g_main_base + rva;
}

static int is_obj_ptr(uintptr_t addr) {
    uintptr_t vt;
    if (!g_initialized || !g_main_base || addr < 0x100000u) return 0;
    vt = read_ptr(addr);
    return vt >= g_main_base && vt + 8u <= g_main_base + g_main_size;
}

static void latch_guest_thread(void) {
    if (!atomic_load_explicit(&g_guest_tid_set, memory_order_acquire)) {
        if (!kern_cpu()) return;
        g_guest_tid = plat_thread_id();
        atomic_store_explicit(&g_guest_tid_set, 1, memory_order_release);
    }
}

static int require_guest_thread(const char *fn_name) {
    int set = atomic_load_explicit(&g_guest_tid_set, memory_order_acquire);
    uint32_t tid = plat_thread_id();
    Cpu *c = kern_cpu();
    if (!set || tid != g_guest_tid || !c) {
        if (!atomic_exchange_explicit(&g_offthread_warned, 1, memory_order_relaxed)) {
            rt_log("[mc_totk_bridge] %s rejected: off guest frame thread (latched=%d, tid=%u, expected=%u, cpu=%p)",
                   fn_name, set, (unsigned)tid, (unsigned)g_guest_tid, (void *)c);
        }
        return 0;
    }
    return 1;
}

/* Safely invoke a lifted guest function on the active guest thread, preserving Cpu state. */
static int bridge_rt_call_full(uint64_t fn, const float *opt_s0, float *out_s0,
                               uint64_t *out_x0, int n,
                               uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    Cpu *c = kern_cpu();
    if (!c || !fn || !rt_dispatch_lookup(fn)) return 0;
    Cpu keep = *c;
    if (opt_s0) {
        uint32_t bits = 0;
        memcpy(&bits, opt_s0, sizeof(bits));
        c->v[0] = (uint64_t)bits;
    }
    uint64_t r = 0;
    switch (n) {
    case 0: r = rt_call(c, fn, 0); break;
    case 1: r = rt_call(c, fn, 1, a0); break;
    case 2: r = rt_call(c, fn, 2, a0, a1); break;
    case 3: r = rt_call(c, fn, 3, a0, a1, a2); break;
    case 4: r = rt_call(c, fn, 4, a0, a1, a2, a3); break;
    default:
        *c = keep;
        return 0;
    }
    if (out_x0) *out_x0 = r;
    if (out_s0) {
        uint32_t bits = (uint32_t)c->v[0];
        memcpy(out_s0, &bits, sizeof(*out_s0));
    }
    *c = keep;
    return 1;
}

static int valid_xyz(const float xyz[3]) {
    return isfinite(xyz[0]) && isfinite(xyz[1]) && isfinite(xyz[2]) &&
           fabsf(xyz[0]) <= 5500.0f && xyz[1] >= -1500.0f &&
           xyz[1] <= 3500.0f && fabsf(xyz[2]) <= 5000.0f;
}

static int actor_is_live(uintptr_t actor) {
    uint32_t st = 0;
    if (!is_obj_ptr(actor)) return 0;
    if (!read_u32(actor + ACT_STATE_OFF, &st)) return 0;
    return st < 7u || st >= 12u;
}

static int read_actor_name(uintptr_t actor, char *out, size_t out_sz) {
    uintptr_t np = read_ptr(actor + ACT_NAME_OFF);
    if (!np || !out || out_sz < 2) return 0;
    memset(out, 0, out_sz);
    return read_mem(np, out, out_sz - 1);
}

static int read_life_holder(uintptr_t life, uint32_t holder_off, int *out_val) {
    uintptr_t holder = read_ptr(life + holder_off);
    int32_t val = 0;
    if (!holder || !read_i32(holder + 8u, &val)) return 0;
    *out_val = (int)val;
    return 1;
}

static int read_actor_life(uintptr_t actor, int *hp, int *max_hp,
                           int *extra_hp, int *gloom_hp, uintptr_t *out_life) {
    uintptr_t comp = read_ptr(actor + ACT_COMP_OFF);
    uintptr_t life = comp ? read_ptr(comp + COMP_LIFE_OFF) : 0;
    int cur = -1, mx = -1, ext = 0, glm = 0;
    if (out_life) *out_life = life;
    if (!life || !read_life_holder(life, LIFE_CUR_HOLDER_OFF, &cur)) return 0;
    (void)read_life_holder(life, LIFE_MAX_HOLDER_OFF, &mx);
    (void)read_life_holder(life, LIFE_EXTRA_HOLDER_OFF, &ext);
    (void)read_life_holder(life, LIFE_GLOOM_HOLDER_OFF, &glm);
    if (hp) *hp = cur;
    if (max_hp) *max_hp = mx;
    if (extra_hp) *extra_hp = ext;
    if (gloom_hp) *gloom_hp = glm;
    return 1;
}

static void resolve_player_physics(uintptr_t player, uintptr_t *out_cct_holder,
                                   uintptr_t *out_cct, uintptr_t *out_cct_inner,
                                   uintptr_t *out_rb) {
    uintptr_t comp = player ? read_ptr(player + ACT_COMP_OFF) : 0;
    uintptr_t phys = comp ? read_ptr(comp + COMP_PHYSICS_OFF) : 0;
    uintptr_t holder = phys ? read_ptr(phys + CCT_HOLDER_OFF) : 0;
    uintptr_t cct = holder ? read_ptr(holder + CCT_FROM_HOLDER_OFF) : 0;
    uintptr_t inner = cct ? read_ptr(cct + 0x08u) : 0;
    uintptr_t rb = holder ? read_ptr(holder + RB_FROM_HOLDER_OFF) : 0;
    if (out_cct_holder) *out_cct_holder = holder;
    if (out_cct) *out_cct = cct;
    if (out_cct_inner) *out_cct_inner = inner;
    if (out_rb) *out_rb = rb;
}

static uintptr_t find_player_actor(void) {
    uintptr_t module, scene, components, resident, links;
    int count = 0, i;
    if (!g_initialized || !g_main_base || (size_t)OFF_SCENE_SLOT + 8u > g_main_size) return 0;
    module = read_ptr(g_main_base + OFF_SCENE_SLOT);
    scene = module ? read_ptr(module + 0x1e8u) : 0;
    if (!scene || !read_i32(scene + 0x50u, &count) || count < 14 || count > 256) return 0;
    components = read_ptr(scene + 0x58u);
    resident = components ? read_ptr(components + 13u * 8u) : 0;
    if (!resident || !read_i32(resident + 0x20u, &count) || count < 1 || count > 64) return 0;
    links = read_ptr(resident + 0x28u);
    if (!links) return 0;
    for (i = 0; i < count; ++i) {
        uintptr_t data = read_ptr(links + (uintptr_t)i * 0x70u + 8u);
        uintptr_t actor = data ? read_ptr(data + 0x40u) : 0;
        char text[7];
        float xyz[3];
        uintptr_t np = actor ? read_ptr(actor + ACT_NAME_OFF) : 0;
        if (np && read_mem(np, text, sizeof(text)) && memcmp(text, "Player", 7) == 0 &&
            actor_is_live(actor) && read_mem(actor + ACT_POS_OFF, xyz, sizeof(xyz)) &&
            valid_xyz(xyz)) {
            return actor;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Optional Static-Patch Player Health Hook (when patches-v121 has mc_health)
 * -------------------------------------------------------------------------- */
#define MC_PATCH_PAGE (((MOD_MAIN_BASE + MOD_MAIN_SIZE) + 0xFFFull) & ~0xFFFull)
#define HOST_MC_HEALTH (MC_PATCH_PAGE + 0xf50ull)

static int intercept_player_damage(uintptr_t life, int damage) {
    if (damage <= 0 || life != atomic_load_explicit(&g_mc_player_life, memory_order_acquire)) {
        return damage;
    }
    plat_mutex_lock(&g_player_health_lock);
    if (life == atomic_load_explicit(&g_mc_player_life, memory_order_relaxed)) {
        uint32_t kind = 0, key = 0;
        /* The native fall hit uses height lost even if Steve was gliding.
         * Minecraft handles fall damage from its own airborne state and landing. */
        if (!read_u32(life + 0x124c, &kind) || !read_u32(life + 0x1240, &key) ||
            kind != 3u || key != 0x40000u) {
            g_pending_player_damage += damage;
        }
        damage = 0;
    }
    plat_mutex_unlock(&g_player_health_lock);
    return damage;
}

static void host_mc_health(Cpu *c) {
    c->x[30] = LD64(c->sp);
    c->sp += 16u;
    int32_t dmg = (int32_t)(uint32_t)c->x[8];
    if (dmg > 0) {
        c->x[8] = (uint32_t)intercept_player_damage((uintptr_t)c->x[19], dmg);
    }
    /* Displaced instruction at main+0x64b4e8: ldr w10, [x9, #8]! */
    c->x[9] += 8u;
    c->x[10] = (uint32_t)LD32(c->x[9]);
}

__attribute__((constructor)) static void mc_bridge_register_host(void) {
    rt_register_host(HOST_MC_HEALTH, host_mc_health);
}

/* --------------------------------------------------------------------------
 * Visibility & HUD Restoration (internal helpers also safe during cleanup)
 * -------------------------------------------------------------------------- */
static uintptr_t resolve_model_holder(uintptr_t actor) {
    uintptr_t comp = read_ptr(actor + ACT_COMP_OFF);
    uintptr_t mcomp = comp ? read_ptr(comp + COMP_MODEL_WRAP_OFF) : 0;
    uintptr_t vt = mcomp ? read_ptr(mcomp) : 0;
    uintptr_t fn = vt ? read_ptr(vt + 0xe8u) : 0;
    uint64_t holder = 0;
    if (!mcomp || !is_text_ptr(fn)) return 0;
    if (!bridge_rt_call_full((uint64_t)fn, NULL, NULL, &holder, 1, (uint64_t)mcomp, 0, 0, 0)) {
        return 0;
    }
    return (uintptr_t)holder;
}

static void restore_saved_player_vis(uintptr_t current_player) {
    int i;
    if (!g_player_vis.hidden && !g_player_vis.bit23_saved && g_player_vis.entry_count == 0) {
        return;
    }
    if (g_player_vis.actor && g_player_vis.actor == current_player &&
        actor_is_live(current_player)) {
        uint32_t vis_flags = 0;
        if (g_player_vis.bit23_saved &&
            read_u32(current_player + ACT_VIS_FLAGS_OFF, &vis_flags) &&
            is_writable_guest(current_player + ACT_VIS_FLAGS_OFF, 4u)) {
            if (g_player_vis.had_bit23) {
                __atomic_or_fetch((uint32_t *)(current_player + ACT_VIS_FLAGS_OFF),
                                  VIS_BIT_ACTOR_23, __ATOMIC_RELAXED);
            } else {
                __atomic_and_fetch((uint32_t *)(current_player + ACT_VIS_FLAGS_OFF),
                                   ~VIS_BIT_ACTOR_23, __ATOMIC_RELAXED);
            }
        }
        for (i = 0; i < g_player_vis.entry_count; ++i) {
            uintptr_t m = g_player_vis.entry_ptr[i];
            if (!is_obj_ptr(m)) continue;
            uintptr_t sub = read_ptr(m);
            uint8_t b1e = 0;
            int32_t dummy = 0;
            if (!sub || sub != g_player_vis.sub_ptr[i] || !is_obj_ptr(sub)) continue;
            if (read_u8(m + 0x1eu, &b1e) && is_writable_guest(m + 0x1eu, 1u)) {
                *(uint8_t *)(m + 0x1eu) = (uint8_t)((b1e & ~0x20u) | g_player_vis.orig_bit20[i]);
            }
            if (read_i32(sub + 8u, &dummy) && is_writable_guest(sub + 8u, 4u)) {
                *(int32_t *)(sub + 8u) = g_player_vis.orig_vis_i32[i];
            }
        }
    }
    memset(&g_player_vis, 0, sizeof(g_player_vis));
}

static int is_valid_ui_mgr(uintptr_t ui_mgr) {
    uintptr_t map, map_vt;
    if (!g_initialized || !g_main_base || ui_mgr < 0x100000u) return 0;
    map = read_ptr(ui_mgr + 0x28u);
    if (!is_obj_ptr(map)) return 0;
    map_vt = read_ptr(map);
    return map_vt && is_text_ptr(read_ptr(map_vt + 0x48u));
}

static int screen_in_ui_mgr(uintptr_t screen, const uintptr_t *slots, uint32_t slot_count) {
    for (uint32_t i = 0; i < slot_count; ++i) {
        if (slots[i] == screen) return 1;
    }
    return 0;
}

static void restore_saved_hud_vis(void) {
    uintptr_t ui_mgr;
    uintptr_t live_slots[1024];
    uint32_t live_count = 0;
    uint8_t b = 0;
    int i;

    if (!g_initialized) return;
    if (!g_hud_vis.hidden && g_hud_vis.byte_count == 0) {
        return;
    }

    ui_mgr = read_got_instance(OFF_GOT_UI_MGR);
    if (is_valid_ui_mgr(ui_mgr)) {
        uint32_t cnt = 0;
        uintptr_t arr = read_ptr(ui_mgr + 0x10u);
        if (arr && read_u32(ui_mgr + 0x08u, &cnt)) {
            if (cnt > 1024u) cnt = 1024u;
            for (uint32_t s = 0; s < cnt; ++s) {
                uintptr_t scr = read_ptr(arr + (uintptr_t)s * 8u);
                if (is_obj_ptr(scr) && live_count < 1024u) {
                    live_slots[live_count++] = scr;
                }
            }
        }
    }

    for (i = g_hud_vis.byte_count - 1; i >= 0; --i) {
        SavedHudByte sb = g_hud_vis.bytes[i];
        if (!sb.addr) continue;
        if (sb.screen) {
            if (!ui_mgr || live_count == 0 ||
                !screen_in_ui_mgr(sb.screen, live_slots, live_count) ||
                !is_obj_ptr(sb.screen)) {
                continue;
            }
        }
        {
            uintptr_t pane_off = (sb.clear_mask == 0x01u) ? 0x58u : 0x59u;
            uintptr_t pane;
            uintptr_t vt;
            if (sb.addr <= 0x100000u + pane_off) continue;
            pane = sb.addr - pane_off;
            if (!is_obj_ptr(pane) || !mem_guest_readable((uint64_t)pane, 0x100u)) continue;
            vt = read_ptr(pane);
            if (!vt || !is_text_ptr(read_ptr(vt))) continue;
        }
        if (read_u8(sb.addr, &b) && is_writable_guest(sb.addr, 1u)) {
            *(uint8_t *)sb.addr = (uint8_t)((b & ~sb.clear_mask) |
                                            (sb.orig_val & sb.clear_mask));
        }
    }
    memset(&g_hud_vis, 0, sizeof(g_hud_vis));
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */
void mc_totk_bridge_init(const RtLoaded *main_module) {
    static const uint32_t expected_fm[4] = {
        0xa9bd7bfdU, 0xf9000bf5U, 0x910003fdU, 0xa9024ff4U
    };
    g_initialized = 0;
    g_force_matrix_verified = 0;
    g_main_base = 0;
    g_main_size = 0;
    g_text_end_abs = 0;
    atomic_store_explicit(&g_guest_tid_set, 0, memory_order_release);
    g_guest_tid = 0;
    atomic_store_explicit(&g_offthread_warned, 0, memory_order_release);
    g_seen_max_stamina = 1000.0f;
    g_stamina_player = 0;
    g_expected_hp_quarters = -1;
    g_hp_player = 0;
    g_health_hook_available = 0;
    atomic_store_explicit(&g_mc_player_life, 0, memory_order_release);
    g_pending_player_damage = 0;
    memset(&g_player_vis, 0, sizeof(g_player_vis));
    memset(&g_hud_vis, 0, sizeof(g_hud_vis));

    if (rt_game_version != 121) {
        rt_log("[mc_totk_bridge] inactive: game version %d != 121", rt_game_version);
        return;
    }

    if (!main_module) {
        for (int i = 0; i < ldr_count(); ++i) {
            const RtLoaded *m = ldr_module(i);
            if (m && m->name && strcmp(m->name, "main") == 0) {
                main_module = m;
                break;
            }
        }
    }
    if (!main_module || !main_module->name || strcmp(main_module->name, "main") != 0 ||
        !main_module->base || !main_module->text_end || main_module->size < main_module->text_end) {
        return;
    }

    if (!g_scratch) {
        g_scratch = (uint8_t *)mem_kernel_alloc(SCRATCH_SIZE, 16);
        if (!g_scratch) {
            rt_log("[mc_totk_bridge] mem_kernel_alloc failed");
            return;
        }
    } else {
        memset(g_scratch, 0, SCRATCH_SIZE);
    }

    g_main_base = (uintptr_t)main_module->base;
    g_main_size = (size_t)main_module->size;
    g_text_end_abs = g_main_base + (uintptr_t)main_module->text_end;
    g_initialized = 1;

    int health_set = rt_patch_set("mc_health");
    if (health_set >= 0 && rt_patch_enabled(health_set)) {
        g_health_hook_available = 1;
    } else {
        g_health_hook_available = 0;
    }

    if ((uintptr_t)FN_FORCE_SET_MATRIX + sizeof(expected_fm) <= (uintptr_t)main_module->text_end) {
        uint32_t words[4];
        if (read_mem(g_main_base + FN_FORCE_SET_MATRIX, words, sizeof(words)) &&
            memcmp(words, expected_fm, sizeof(words)) == 0) {
            g_force_matrix_verified = 1;
        }
    }
    rt_log("[mc_totk_bridge] initialized v1.2.1 base=%#llx force_matrix=%d health_hook=%d",
           (unsigned long long)g_main_base, g_force_matrix_verified, g_health_hook_available);
}

void mc_totk_bridge_cleanup(void) {
    mc_totk_clear_camera_look();
    if (g_initialized) {
        if (g_player_vis.hidden || g_player_vis.bit23_saved) {
            restore_saved_player_vis(find_player_actor());
        }
        if (g_hud_vis.hidden || g_hud_vis.byte_count > 0 || g_hud_vis.status_byte_addr) {
            restore_saved_hud_vis();
        }
    }
    if (g_scratch) {
        mem_kernel_free(g_scratch, SCRATCH_SIZE);
        g_scratch = NULL;
    }
    atomic_store_explicit(&g_guest_tid_set, 0, memory_order_release);
    g_guest_tid = 0;
    atomic_store_explicit(&g_offthread_warned, 0, memory_order_release);
    g_initialized = 0;
}

/* --------------------------------------------------------------------------
 * Player State Readers & Mutators
 * -------------------------------------------------------------------------- */
static void read_action_flags(uintptr_t comp, int *out_blocking, int *out_attacking) {
    uintptr_t a1 = read_ptr(comp + COMP_ACTION_CHAIN_OFF);
    uintptr_t a2 = a1 ? read_ptr(a1 + 0x18u) : 0;
    uintptr_t mgr = a2 ? read_ptr(a2 + 0x18u) : 0;
    int32_t cnt = 0;
    uintptr_t slots;
    int i;
    *out_blocking = 0;
    *out_attacking = 0;
    if (!mgr || !read_i32(mgr + 0x18u, &cnt) || cnt <= 0) return;
    if (cnt > 32) cnt = 32;
    slots = read_ptr(mgr + 0x20u);
    if (!slots) return;
    for (i = 0; i < cnt; ++i) {
        uintptr_t obj = read_ptr(slots + (uintptr_t)i * 0x48u);
        uintptr_t state = obj ? read_ptr(obj + 0x60u) : 0;
        uint32_t flags = 0;
        int32_t sub_cnt = 0;
        if (!state || !read_u32(state + 0x40u, &flags)) continue;
        (void)read_i32(state + 0xa0u, &sub_cnt);
        if ((flags & 0x20u) != 0 && sub_cnt >= 1) *out_blocking = 1;
        if ((flags & 0x90u) == 0x10u && sub_cnt >= 1) *out_attacking = 1;
    }
}

static int read_rb_velocity(uintptr_t rb, float out_vel[3]) {
    uint32_t flags = 0;
    float v[3];
    if (!rb || !read_u32(rb + 0xd4u, &flags)) return 0;
    if ((flags & 0x80u) == 0u) {
        if (!read_mem(rb + 0xf8u, v, sizeof(v))) return 0;
    } else {
        uintptr_t p1 = read_ptr(rb + 0x60u);
        uintptr_t p2 = p1 ? read_ptr(p1) : 0;
        if (!p2 || !read_mem(p2 + 0x44u, v, sizeof(v))) return 0;
    }
    if (!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2])) return 0;
    memcpy(out_vel, v, sizeof(v));
    return 1;
}

int mc_totk_read_player(McTotkPlayerState *out) {
    uintptr_t player, comp, cct, cct_inner, rb;
    uintptr_t fn_on_ground, fn_vert_vel;
    uintptr_t stam;
    int i;

    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!g_initialized) return 0;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_read_player")) return 0;

    player = find_player_actor();
    if (!player) return 0;

    if (!read_mem(player + ACT_POS_OFF, out->pos, sizeof(out->pos)) || !valid_xyz(out->pos)) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    if (!read_mem(player + ACT_ROT_OFF, out->rot, sizeof(out->rot))) {
        out->rot[0] = 1.0f; out->rot[4] = 1.0f; out->rot[8] = 1.0f;
    } else {
        for (i = 0; i < 9; ++i) {
            if (!isfinite(out->rot[i])) out->rot[i] = (i % 4 == 0) ? 1.0f : 0.0f;
        }
    }
    out->fwd[0] = out->rot[2];
    out->fwd[1] = out->rot[5];
    out->fwd[2] = out->rot[8];
    {
        float flen = sqrtf(out->fwd[0] * out->fwd[0] + out->fwd[1] * out->fwd[1] +
                           out->fwd[2] * out->fwd[2]);
        if (flen > 1e-4f) {
            out->fwd[0] /= flen;
            out->fwd[1] /= flen;
            out->fwd[2] /= flen;
        } else {
            out->fwd[0] = 0.0f; out->fwd[1] = 0.0f; out->fwd[2] = 1.0f;
        }
    }
    out->yaw = atan2f(out->fwd[0], out->fwd[2]);

    comp = read_ptr(player + ACT_COMP_OFF);
    resolve_player_physics(player, NULL, &cct, &cct_inner, &rb);

    fn_on_ground = text_addr(FN_CCT_IS_ON_GROUND);
    if (cct && cct_inner && read_ptr(cct_inner + 0x40u) && fn_on_ground) {
        uint64_t r = 1;
        if (bridge_rt_call_full((uint64_t)fn_on_ground, NULL, NULL, &r, 1, (uint64_t)cct, 0, 0, 0)) {
            /* CharacterStateType: 0 = OnGround (Supported), 1 = InAir (Unsupported). */
            out->grounded = ((uint32_t)r == 0u);
        }
    }

    fn_vert_vel = text_addr(FN_CCT_GET_VERT_VEL);
    if (cct_inner && read_ptr(cct_inner + 0x40u) && fn_vert_vel) {
        float vv = 0.0f;
        if (bridge_rt_call_full((uint64_t)fn_vert_vel, NULL, &vv, NULL, 1, (uint64_t)cct_inner, 0, 0, 0)) {
            out->vert_vel = isfinite(vv) ? vv : 0.0f;
        }
    }

    if (!read_rb_velocity(rb, out->vel)) {
        out->vel[1] = out->vert_vel;
    }
    out->falling = (!out->grounded && out->vert_vel < -0.35f) ? 1 : 0;

    if (comp) {
        out->gliding_paraglider = 0;
        read_action_flags(comp, &out->blocking, &out->attacking);
    }

    out->hp_quarters = -1;
    (void)read_actor_life(player, &out->hp_quarters, &out->max_hp_quarters,
                          &out->extra_hp_quarters, &out->gloom_quarters, NULL);

    if (player != g_stamina_player) {
        g_stamina_player = player;
        g_seen_max_stamina = 1000.0f;
    }
    stam = comp ? read_ptr(comp + COMP_STAMINA_OFF) : 0;
    if (stam) {
        float sa = 0.0f, sb = 0.0f;
        uint8_t exh = 0;
        if (read_f32(stam + STAM_CUR_A_OFF, &sa) && read_f32(stam + STAM_CUR_B_OFF, &sb)) {
            float total = (isfinite(sa) ? sa : 0.0f) + (isfinite(sb) ? sb : 0.0f);
            out->stamina = total > 0.0f ? total : 0.0f;
            if (out->stamina > g_seen_max_stamina) g_seen_max_stamina = out->stamina;
        }
        if (read_u8(stam + STAM_EXHAUSTED_OFF, &exh)) {
            out->stamina_exhausted = (exh & 1u) != 0;
        }
    }
    out->max_stamina = g_seen_max_stamina;
    out->actor = (void *)player;
    out->valid = 1;
    return 1;
}

int mc_totk_set_player_pos(const float pos[3], const float *opt_yaw_rad) {
    uintptr_t player, fn;
    float *m;

    if (!g_initialized || !g_scratch || !pos || !valid_xyz(pos) || !g_force_matrix_verified) return 0;
    if (!require_guest_thread("mc_totk_set_player_pos")) return 0;

    fn = text_addr(FN_FORCE_SET_MATRIX);
    player = find_player_actor();
    if (!fn || !player) return 0;

    m = (float *)(g_scratch + SCRATCH_MAT_OFF);
    if (opt_yaw_rad && isfinite(*opt_yaw_rad)) {
        float c = cosf(*opt_yaw_rad), s = sinf(*opt_yaw_rad);
        m[0] =  c;    m[1] = 0.0f; m[2] = s;    m[3] = pos[0];
        m[4] = 0.0f;  m[5] = 1.0f; m[6] = 0.0f; m[7] = pos[1];
        m[8] = -s;    m[9] = 0.0f; m[10] = c;   m[11] = pos[2];
    } else {
        float rot[9];
        if (!read_mem(player + ACT_ROT_OFF, rot, sizeof(rot))) {
            m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
            m[4] = 0.0f; m[5] = 1.0f; m[6] = 0.0f;
            m[8] = 0.0f; m[9] = 0.0f; m[10] = 1.0f;
        } else {
            m[0] = rot[0]; m[1] = rot[1]; m[2]  = rot[2];
            m[4] = rot[3]; m[5] = rot[4]; m[6]  = rot[5];
            m[8] = rot[6]; m[9] = rot[7]; m[10] = rot[8];
        }
        m[3] = pos[0];
        m[7] = pos[1];
        m[11] = pos[2];
    }

    return bridge_rt_call_full((uint64_t)fn, NULL, NULL, NULL, 3,
                               (uint64_t)player, (uint64_t)(uintptr_t)m, 0ull, 0);
}

int mc_totk_set_player_yaw(float yaw_rad) {
    uintptr_t player;
    float pos[3];
    if (!g_initialized || !isfinite(yaw_rad)) return 0;
    if (!require_guest_thread("mc_totk_set_player_yaw")) return 0;
    player = find_player_actor();
    if (!player || !read_mem(player + ACT_POS_OFF, pos, sizeof(pos))) return 0;
    return mc_totk_set_player_pos(pos, &yaw_rad);
}

int mc_totk_set_player_vel(const float vel[3]) {
    uintptr_t player, cct, cct_inner, rb;
    uintptr_t fn_cct_vel, fn_rb_vel;
    float *gvel;
    int applied = 0;

    if (!g_initialized || !g_scratch || !vel ||
        !isfinite(vel[0]) || !isfinite(vel[1]) || !isfinite(vel[2])) return 0;
    if (!require_guest_thread("mc_totk_set_player_vel")) return 0;

    player = find_player_actor();
    if (!player) return 0;
    resolve_player_physics(player, NULL, &cct, &cct_inner, &rb);

    gvel = (float *)(g_scratch + SCRATCH_VEL_OFF);
    memcpy(gvel, vel, 3u * sizeof(float));

    fn_cct_vel = text_addr(FN_CCT_SET_VEL);
    if (cct && cct_inner && read_ptr(cct_inner) && fn_cct_vel) {
        if (bridge_rt_call_full((uint64_t)fn_cct_vel, NULL, NULL, NULL, 2,
                                (uint64_t)cct, (uint64_t)(uintptr_t)gvel, 0, 0)) {
            applied = 1;
        }
    }

    fn_rb_vel = text_addr(FN_RB_SET_VEL);
    if (rb && read_ptr(rb) && fn_rb_vel) {
        if (bridge_rt_call_full((uint64_t)fn_rb_vel, NULL, NULL, NULL, 2,
                                (uint64_t)rb, (uint64_t)(uintptr_t)gvel, 0, 0)) {
            applied = 1;
        }
    }
    return applied;
}

int mc_totk_set_player_hp(int hp_quarters) {
    uintptr_t player, life = 0, cur_holder, fn_sync;
    int cur = 0, max_hp = 0, ext = 0, glm = 0, cap, target;

    if (!g_initialized) return 0;
    if (!require_guest_thread("mc_totk_set_player_hp")) return 0;

    player = find_player_actor();
    if (!player || !read_actor_life(player, &cur, &max_hp, &ext, &glm, &life)) return 0;
    cur_holder = read_ptr(life + LIFE_CUR_HOLDER_OFF);
    if (!cur_holder || !is_writable_guest(cur_holder + 8u, 4u)) return 0;

    cap = (max_hp > 0) ? (max_hp - (glm > 0 ? glm : 0)) : 120;
    if (cap < 1) cap = 1;
    target = hp_quarters;
    if (target < 0) target = 0;
    if (max_hp > 0 && target > cap) target = cap;

    if (target <= 0) {
        plat_mutex_lock(&g_player_health_lock);
        atomic_store_explicit(&g_mc_player_life, 0, memory_order_release);
        g_pending_player_damage = 0;
        plat_mutex_unlock(&g_player_health_lock);
    }

    __atomic_store_n((int32_t *)(cur_holder + 8u), (int32_t)target, __ATOMIC_RELAXED);
    fn_sync = text_addr(FN_LIFECOMP_SYNC);
    if (fn_sync) {
        (void)bridge_rt_call_full((uint64_t)fn_sync, NULL, NULL, NULL, 1, (uint64_t)life, 0, 0, 0);
    }

    g_hp_player = player;
    g_expected_hp_quarters = target;
    return 1;
}

int mc_totk_add_player_hp(int delta_quarters) {
    uintptr_t player, life = 0;
    int cur = 0, mx = 0, ext = 0, glm = 0;
    if (!g_initialized) return 0;
    if (!require_guest_thread("mc_totk_add_player_hp")) return 0;
    player = find_player_actor();
    if (!player || !read_actor_life(player, &cur, &mx, &ext, &glm, &life)) return 0;
    return mc_totk_set_player_hp(cur + delta_quarters);
}

int mc_totk_add_player_stamina(float delta) {
    uintptr_t player, comp, stam, fn_stam;
    if (!g_initialized || !isfinite(delta)) return 0;
    if (!require_guest_thread("mc_totk_add_player_stamina")) return 0;
    player = find_player_actor();
    comp = player ? read_ptr(player + ACT_COMP_OFF) : 0;
    stam = comp ? read_ptr(comp + COMP_STAMINA_OFF) : 0;
    fn_stam = text_addr(FN_STAMINA_ADD);
    if (!stam || !fn_stam || !read_got_instance(OFF_GOT_STAMINA_PARAM)) return 0;
    return bridge_rt_call_full((uint64_t)fn_stam, &delta, NULL, NULL, 1, (uint64_t)stam, 0, 0, 0);
}

void mc_totk_refill_player_stamina(void *player) {
    if (!g_initialized || !player || !require_guest_thread("mc_totk_refill_player_stamina")) return;
    uintptr_t actor = (uintptr_t)player;
    if (!actor_is_live(actor)) return;
    uintptr_t comp = read_ptr(actor + ACT_COMP_OFF);
    uintptr_t stamina = comp ? read_ptr(comp + COMP_STAMINA_OFF) : 0;
    if (!stamina) return;

    float current, capacity;
    uintptr_t current_addr = stamina + STAM_CUR_A_OFF;
    uintptr_t exhausted_addr = stamina + STAM_EXHAUSTED_OFF;
    if (!read_f32(current_addr, &current) || !isfinite(current) ||
        !read_f32(stamina + STAM_MAX_A_OFF, &capacity) || !isfinite(capacity) || capacity <= 0.0f ||
        !is_writable_guest(current_addr, sizeof(float)) || !is_writable_guest(exhausted_addr, 1u)) return;

    // Refill the normal wheel to the game's current capacity without altering bonus stamina.
    if (current < capacity) {
        uint32_t bits;
        memcpy(&bits, &capacity, sizeof(bits));
        __atomic_store_n((uint32_t *)current_addr, bits, __ATOMIC_RELAXED);
    }
    uint8_t exhausted = __atomic_load_n((uint8_t *)exhausted_addr, __ATOMIC_RELAXED);
    if (exhausted & 1u) __atomic_fetch_and((uint8_t *)exhausted_addr, (uint8_t)~1u, __ATOMIC_RELAXED);
}

/* --------------------------------------------------------------------------
 * Visibility & HUD Toggles
 * -------------------------------------------------------------------------- */
static int player_vis_has_entry(uintptr_t m, uintptr_t sub) {
    for (int i = 0; i < g_player_vis.entry_count; ++i) {
        if (g_player_vis.entry_ptr[i] == m && g_player_vis.sub_ptr[i] == sub) return 1;
    }
    return 0;
}

void mc_totk_set_player_visible(int visible) {
    uintptr_t player;
    uintptr_t holder, arr;
    int32_t cnt = 0, i;

    if (!g_initialized) return;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_set_player_visible")) return;
    player = find_player_actor();

    if (visible || !mc_totk_is_gameplay_active()) {
        restore_saved_player_vis(player);
        return;
    }

    if (!player) return;
    if ((g_player_vis.hidden || g_player_vis.bit23_saved) && g_player_vis.actor != player) {
        memset(&g_player_vis, 0, sizeof(g_player_vis));
    }

    g_player_vis.actor = player;
    if (!g_player_vis.bit23_saved) {
        uint32_t flags = 0;
        if (!read_u32(player + ACT_VIS_FLAGS_OFF, &flags)) return;
        g_player_vis.bit23_saved = 1;
        g_player_vis.had_bit23 = (flags & VIS_BIT_ACTOR_23) != 0;
    }

    /* Dynamically capture any Link/equipment model entries loaded after boot or weapon swap. */
    holder = resolve_model_holder(player);
    if (holder && read_i32(holder + 0x20u, &cnt) && cnt > 0) {
        if (cnt > VIS_ENTRY_CAP) cnt = VIS_ENTRY_CAP;
        arr = read_ptr(holder + 0x28u);
        if (arr) {
            for (i = 0; i < cnt && g_player_vis.entry_count < VIS_ENTRY_CAP; ++i) {
                uintptr_t m = read_ptr(arr + (uintptr_t)i * 8u);
                uintptr_t sub = m ? read_ptr(m) : 0;
                uint8_t b1e = 0;
                int32_t vis_val = 0;
                int idx;
                if (!sub || player_vis_has_entry(m, sub)) continue;
                if (!read_u8(m + 0x1eu, &b1e) || !read_i32(sub + 8u, &vis_val)) continue;
                idx = g_player_vis.entry_count++;
                g_player_vis.entry_ptr[idx] = m;
                g_player_vis.sub_ptr[idx] = sub;
                g_player_vis.orig_bit20[idx] = b1e & 0x20u;
                g_player_vis.orig_vis_i32[idx] = vis_val;
            }
        }
    }
    if (g_player_vis.entry_count > 0) {
        g_player_vis.hidden = 1;
    }

    if (is_writable_guest(player + ACT_VIS_FLAGS_OFF, 4u)) {
        __atomic_and_fetch((uint32_t *)(player + ACT_VIS_FLAGS_OFF),
                           ~VIS_BIT_ACTOR_23, __ATOMIC_RELAXED);
    }
    for (i = 0; i < g_player_vis.entry_count; ++i) {
        uintptr_t m = g_player_vis.entry_ptr[i];
        if (!is_obj_ptr(m)) continue;
        uintptr_t sub = read_ptr(m);
        uint8_t b1e = 0;
        if (!sub || sub != g_player_vis.sub_ptr[i] || !is_obj_ptr(sub)) continue;
        if (read_u8(m + 0x1eu, &b1e) && is_writable_guest(m + 0x1eu, 1u)) {
            *(uint8_t *)(m + 0x1eu) = (uint8_t)(b1e & ~0x20u);
        }
        if (is_writable_guest(sub + 8u, 4u)) {
            *(int32_t *)(sub + 8u) = 0;
        }
    }
}

static int uiscreen_fade_is_opaque(uintptr_t scr) {
    uintptr_t layout = read_ptr(scr + 0x28u);
    uintptr_t root_pane;
    uintptr_t vt;
    uint8_t vis = 0, alpha = 0;
    if (layout < 0x100000u) return 0;
    root_pane = read_ptr(layout + 0x18u);
    if (!is_obj_ptr(root_pane)) return 0;
    vt = read_ptr(root_pane);
    if (!vt || !is_text_ptr(read_ptr(vt))) return 0;
    if (!read_u8(root_pane + 0x58u, &vis) || !read_u8(root_pane + 0x59u, &alpha)) return 0;
    return (vis & 0x01u) != 0 && alpha > 0u;
}

static int check_is_gameplay_active_impl(void) {
    static const uint32_t group_a_vt[] = {
        VT_UI_TITLE, VT_UI_GAME_TITLE_LOGO, VT_UI_GAME_OVER,
        VT_UI_STAFF_ROLL, VT_UI_FINISH,
        VT_UI_SYSTEM_SAVE_LOAD, VT_UI_SYSTEM_LOAD_LIST
    };
    static const uint32_t group_b_vt[] = {
        VT_UI_LOADING_FADE, VT_UI_EVENT_FADE, VT_UI_EVENT_FADE_WIPE
    };
    static const uint32_t group_c_vt[] = {
        VT_UI_PAUSE_MENU_BG, VT_UI_PAUSE_MENU_OVERLAY, VT_UI_APP_MENU_OVERLAY,
        VT_UI_POUCH, VT_UI_ALBUM, VT_UI_PICTURE_BOOK,
        VT_UI_SYSTEM_OPTION, VT_UI_SYSTEM_WINDOW, VT_UI_SYSTEM_ACTION_GUIDE,
        VT_UI_SHOP_MENU, VT_UI_DYE_SHOP_MENU, VT_UI_HOUSE_SHOP_MENU,
        VT_UI_OWNED_HORSE_LIST, VT_UI_PARASAIL_PATTERN_CHANGE,
        VT_UI_AUTO_BUILDER, VT_UI_AMIIBO_WINDOW, VT_UI_CAMERA_SYSTEM_WINDOW,
        VT_UI_CHALLENGE, VT_UI_EVENT_SKIP,
        VT_UI_MESSAGE_DEMO, VT_UI_MESSAGE_DIALOG, VT_UI_MESSAGE_DUNGEON,
        VT_UI_MESSAGE_LOG_01, VT_UI_OPENING_TEXT, VT_UI_DEMO_NICKNAME,
        VT_UI_APP_OPEN_DEMO_WINDOW, VT_UI_GET_WINDOW,
        VT_UI_SPECIAL_POWER_GET_WINDOW, VT_UI_HEART_GAUGE_DEMO,
        VT_UI_STAMINA_GAUGE_DEMO, VT_UI_INSTANT_TIPS,
        VT_UI_INSTANT_TIPS_WITH_PICT, VT_UI_INSTANT_TIPS_APP_MENU,
        VT_UI_ACTION_GUIDE_WITH_PICT, VT_UI_RECIPE_CARD,
        VT_UI_CHARA_DIRECTORY
    };
    uintptr_t module, scene, player, phys_mgr, phys_scene, ui_mgr, slots;
    int32_t scene_cnt = 0;
    uint32_t ui_cnt = 0;

    if (!g_initialized || !g_main_base || (size_t)OFF_SCENE_SLOT + 8u > g_main_size) {
        return 0;
    }

    module = read_ptr(g_main_base + OFF_SCENE_SLOT);
    scene = (module >= 0x100000u) ? read_ptr(module + 0x1e8u) : 0;
    if (scene < 0x100000u || !read_i32(scene + 0x50u, &scene_cnt) ||
        scene_cnt < 14 || scene_cnt > 256) {
        return 0;
    }

    ui_mgr = read_got_instance(OFF_GOT_UI_MGR);
    if (!is_valid_ui_mgr(ui_mgr)) return 0;
    slots = read_ptr(ui_mgr + 0x10u);
    if (slots < 0x100000u || !read_u32(ui_mgr + 0x08u, &ui_cnt) ||
        ui_cnt == 0u || ui_cnt > 2048u) {
        return 0;
    }

    for (uint32_t s = 0; s < ui_cnt; ++s) {
        uintptr_t scr = read_ptr(slots + (uintptr_t)s * 8u);
        uintptr_t vt;
        uint32_t vt_rva;
        uint8_t state = 0, hide_u8 = 0, susp = 0;
        int8_t hide_cnt;

        if (!is_obj_ptr(scr)) continue;
        vt = read_ptr(scr);
        if (vt <= g_main_base || (size_t)(vt - g_main_base) + 8u > g_main_size) continue;
        vt_rva = (uint32_t)(vt - g_main_base);

        if (!read_u8(scr + 0x16eu, &state) || !read_u8(scr + 0x172u, &susp)) continue;
        if (susp != 0u || state == 0u) continue;
        (void)read_u8(scr + 0x16du, &hide_u8);
        hide_cnt = (int8_t)hide_u8;

        for (size_t k = 0; k < sizeof(group_a_vt) / sizeof(group_a_vt[0]); ++k) {
            if (vt_rva == group_a_vt[k]) return 0;
        }
        for (size_t k = 0; k < sizeof(group_b_vt) / sizeof(group_b_vt[0]); ++k) {
            if (vt_rva == group_b_vt[k]) {
                if (state == 1u || ((state == 2u || state == 3u) && uiscreen_fade_is_opaque(scr))) {
                    return 0;
                }
                break;
            }
        }
        for (size_t k = 0; k < sizeof(group_c_vt) / sizeof(group_c_vt[0]); ++k) {
            if (vt_rva == group_c_vt[k]) {
                if (state == 1u || ((state == 2u || state == 3u) && hide_cnt >= 0)) {
                    return 0;
                }
                break;
            }
        }
    }

    player = find_player_actor();
    if (!player) return 0;

    phys_mgr = read_got_instance(OFF_GOT_PHYSICS_MGR);
    phys_scene = (phys_mgr >= 0x100000u) ? read_ptr(phys_mgr + 0xc8u) : 0;
    return phys_scene >= 0x100000u && read_ptr(phys_scene) != 0;
}

/* The Options screen can sit behind the pause screen in the UI manager's slot order. */
int mc_totk_options_open(void) {
    uintptr_t ui, slots;
    uint32_t count = 0;
    if (!g_initialized) return 0;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_options_open")) return 0;

    ui = read_got_instance(OFF_GOT_UI_MGR);
    if (!is_valid_ui_mgr(ui) || !read_u32(ui + 8, &count) || count > 2048) return 0;
    slots = read_ptr(ui + 0x10);
    for (uint32_t i = 0; slots && i < count; ++i) {
        uintptr_t screen = read_ptr(slots + i * sizeof(uintptr_t));
        if (screen && read_ptr(screen) == g_main_base + VT_UI_SYSTEM_OPTION) {
            uint8_t state = 0, suspended = 1, hidden = 0;
            if (!read_u8(screen + 0x16e, &state) || !read_u8(screen + 0x172, &suspended) || suspended) continue;
            if (state == 1 || ((state == 2 || state == 3) &&
                read_u8(screen + 0x16d, &hidden) && (int8_t)hidden >= 0)) return 1;
        }
    }
    return 0;
}

int mc_totk_is_gameplay_active(void) {
    if (!g_initialized) return 0;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_is_gameplay_active")) return 0;
    return check_is_gameplay_active_impl();
}

static uintptr_t resolve_ui_screen(uintptr_t ui_mgr, uint32_t fn_rva, uint32_t hash) {
    uintptr_t fn = text_addr(fn_rva);
    uintptr_t slots_arr;
    uint64_t scr = 0;
    uint32_t cnt = 0;
    if (!is_valid_ui_mgr(ui_mgr) || !is_text_ptr(fn)) return 0;
    slots_arr = read_ptr(ui_mgr + 0x10u);
    if (!slots_arr || !read_u32(ui_mgr + 0x08u, &cnt) || cnt == 0 || cnt > 2048u) return 0;
    if (!bridge_rt_call_full((uint64_t)fn, NULL, NULL, &scr, 2, (uint64_t)ui_mgr, (uint64_t)hash, 0, 0)) {
        return 0;
    }
    return is_obj_ptr((uintptr_t)scr) ? (uintptr_t)scr : 0;
}

static void hud_save_and_mask_byte(uintptr_t addr, uintptr_t screen, uint8_t clear_mask) {
    uint8_t cur = 0;
    int i;
    if (!addr || !read_u8(addr, &cur) || !is_writable_guest(addr, 1u)) return;
    for (i = 0; i < g_hud_vis.byte_count; ++i) {
        if (g_hud_vis.bytes[i].addr == addr) {
            g_hud_vis.bytes[i].screen = screen;
            *(uint8_t *)addr = (uint8_t)(cur & ~clear_mask);
            return;
        }
    }
    if (g_hud_vis.byte_count < HUD_BYTE_CAP) {
        i = g_hud_vis.byte_count++;
        g_hud_vis.bytes[i].addr = addr;
        g_hud_vis.bytes[i].screen = screen;
        g_hud_vis.bytes[i].orig_val = cur;
        g_hud_vis.bytes[i].clear_mask = clear_mask;
    }
    *(uint8_t *)addr = (uint8_t)(cur & ~clear_mask);
}

static void hud_hide_pane(uintptr_t pane, uintptr_t screen) {
    uint8_t tmp = 0;
    uintptr_t vt;
    if (!is_obj_ptr(pane)) return;
    vt = read_ptr(pane);
    if (!vt || !is_text_ptr(read_ptr(vt))) return;
    /* Clear Visible bit 0 (+0x58) and zero mAlpha (+0x59) on verified nn::ui2d::Pane*. */
    if (read_u8(pane + 0x58u, &tmp)) {
        hud_save_and_mask_byte(pane + 0x58u, screen, 0x01u);
    }
    if (read_u8(pane + 0x59u, &tmp)) {
        hud_save_and_mask_byte(pane + 0x59u, screen, 0xffu);
    }
}

static void hud_hide_layout(uintptr_t layout, uintptr_t screen) {
    uintptr_t root_pane;
    if (layout < 0x100000u) return;
    root_pane = read_ptr(layout + 0x18u);
    if (is_obj_ptr(root_pane)) hud_hide_pane(root_pane, screen);
}

static void hud_hide_screen_root(uintptr_t screen) {
    if (!is_obj_ptr(screen)) return;
    hud_hide_layout(read_ptr(screen + 0x28u), screen);
}

static void hud_hide_wrapper_slot(uintptr_t screen, uint32_t slot_off) {
    uintptr_t p1;
    if (!is_obj_ptr(screen)) return;
    p1 = read_ptr(screen + slot_off);
    if (!is_obj_ptr(p1)) return;
    hud_hide_layout(read_ptr(p1 + 0x20u), screen);
}

void mc_totk_set_hud_visible(int visible) {
    uintptr_t ui_mgr;

    if (!g_initialized) return;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_set_hud_visible")) return;

    if (visible || !check_is_gameplay_active_impl()) {
        restore_saved_hud_vis();
        return;
    }

    ui_mgr = read_got_instance(OFF_GOT_UI_MGR);

    /* Hide gameplay HUD screens via verified eui::LayoutEx (+0x18 -> RootPane) mAlpha (+0x59). */
    if (is_valid_ui_mgr(ui_mgr)) {
        uintptr_t pstatus_scr = resolve_ui_screen(ui_mgr, FN_UI_FIND_HEART, HASH_UI_HEART);
        uintptr_t stam_scr = resolve_ui_screen(ui_mgr, FN_UI_FIND_STAM, HASH_UI_STAMINA);
        uintptr_t map_scr = resolve_ui_screen(ui_mgr, FN_UI_FIND_MAP, HASH_UI_MAP);
        uintptr_t docmd_scr = resolve_ui_screen(ui_mgr, FN_UI_FIND_DOCMD, HASH_UI_DOCMD);
        uintptr_t ctrl_scr = resolve_ui_screen(ui_mgr, FN_UI_FIND_CTRL_GUIDE, HASH_UI_CTRL_GUIDE);
        uintptr_t loc_scr = resolve_ui_screen(ui_mgr, FN_UI_FIND_LOC, HASH_UI_LOC);
        uintptr_t vt_action_guide = main_data_addr(VT_UI_ACTION_GUIDE);
        uintptr_t vt_env = main_data_addr(VT_UI_ENV);
        uintptr_t vt_horse = main_data_addr(VT_UI_HORSE_GAUGE);
        uintptr_t vt_pickup = main_data_addr(VT_UI_PICKUP);
        uintptr_t vt_shortcut_bg = main_data_addr(VT_UI_SHORTCUT_BG);
        uintptr_t vt_shortcut_main = main_data_addr(VT_UI_SHORTCUT_MAIN);
        uintptr_t vt_shortcut = main_data_addr(VT_UI_SHORTCUT);
        uintptr_t vt_time = main_data_addr(VT_UI_TIME);
        uint32_t cnt = 0;
        uintptr_t arr = read_ptr(ui_mgr + 0x10u);

        if (pstatus_scr) {
            static const uint32_t ps_slots[] = {
                0x1e8u, 0x1f0u, 0x1f8u, 0x200u, 0x220u, 0x228u,
                0x280u, 0x288u, 0x290u, 0x298u
            };
            for (size_t k = 0; k < sizeof(ps_slots) / sizeof(ps_slots[0]); ++k) {
                hud_hide_wrapper_slot(pstatus_scr, ps_slots[k]);
            }
            hud_hide_screen_root(pstatus_scr);
        }
        if (stam_scr) {
            hud_hide_wrapper_slot(stam_scr, 0x1b0u);
            hud_hide_screen_root(stam_scr);
        }
        if (map_scr) {
            uintptr_t mp268 = read_ptr(map_scr + 0x268u);
            hud_hide_wrapper_slot(map_scr, 0x220u);
            hud_hide_wrapper_slot(map_scr, 0x288u);
            hud_hide_wrapper_slot(map_scr, 0x290u);
            hud_hide_layout(read_ptr(map_scr + 0x2a0u), map_scr);
            if (mp268 >= 0x100000u) {
                hud_hide_pane(read_ptr(mp268 + 0x88u), map_scr);
            }
            hud_hide_screen_root(map_scr);
        }
        if (docmd_scr) hud_hide_screen_root(docmd_scr);
        if (ctrl_scr) hud_hide_screen_root(ctrl_scr);
        if (loc_scr) hud_hide_screen_root(loc_scr);

        if (arr && read_u32(ui_mgr + 0x08u, &cnt)) {
            if (cnt > 1024u) cnt = 1024u;
            for (uint32_t s = 0; s < cnt; ++s) {
                uintptr_t scr = read_ptr(arr + (uintptr_t)s * 8u);
                uintptr_t vt = is_obj_ptr(scr) ? read_ptr(scr) : 0;
                if (vt && (vt == vt_shortcut || vt == vt_shortcut_bg ||
                           vt == vt_shortcut_main || vt == vt_time ||
                           vt == vt_env || vt == vt_horse ||
                           vt == vt_action_guide || vt == vt_pickup)) {
                    if (vt == vt_env) {
                        hud_hide_wrapper_slot(scr, 0x280u);
                    }
                    hud_hide_screen_root(scr);
                }
            }
        }
    }

    if (g_hud_vis.byte_count > 0) {
        g_hud_vis.hidden = 1;
    }
}

/* --------------------------------------------------------------------------
 * Hyrule Physics Raycast, Ground Probe & Wall Sweep
 * -------------------------------------------------------------------------- */
static int raycast_raw_masked(const float origin[3], const float end[3],
                              uint32_t mask_idx, int ignore_start_inside,
                              float out_hit[3], float out_normal[3],
                              float *out_dist, uintptr_t *out_hit_rb,
                              float *out_frac, uint32_t *out_cfi) {
    uintptr_t phys_mgr, scene, fn_ctor, fn_mask, fn_exec;
    uint8_t *ray;
    uint64_t hit_x0 = 0;
    int hit;

    if (!g_initialized || !g_scratch) return 0;
    phys_mgr = read_got_instance(OFF_GOT_PHYSICS_MGR);
    scene = phys_mgr ? read_ptr(phys_mgr + 0xc8u) : 0;
    if (!scene || !read_ptr(scene) || !read_ptr(g_main_base + OFF_GOT_RAY_VT)) return 0;

    fn_ctor = text_addr(FN_RAY_CTOR);
    fn_mask = text_addr(FN_RAY_MASK);
    fn_exec = text_addr(FN_RAY_EXEC);
    if (!fn_ctor || !fn_mask || !fn_exec) return 0;

    ray = g_scratch + SCRATCH_RAY_OFF;
    memset(ray, 0, 0x240u);
    if (!bridge_rt_call_full((uint64_t)fn_ctor, NULL, NULL, NULL, 4,
                             (uint64_t)(uintptr_t)ray, 0x11ull, 0ull, 0ull)) {
        return 0;
    }
    if (ignore_start_inside >= 1) {
        /* Clear QueryImp start-in-shape reporting flag: main+0x01182a28 computes
         * `8 & ~(ray[0x118] >> 4)`, so bit 7 (0x80) shifts to bit 3 (0x08) and clears it. */
        ray[0x118u] |= 0x80u;
    }
    if (ignore_start_inside >= 2) {
        /* Fallback mode used by TOTK callers:
         * main+0x01182a30..0x01182a64 ORs ray[0x84]==1 into hknpRayCastQuery.m_flags bit 0. */
        uint32_t flag84 = 1u;
        memcpy(ray + 0x84u, &flag84, sizeof(flag84));
    }
    memcpy(ray + 0x08u, origin, 12u);
    memcpy(ray + 0x14u, end, 12u);

    /* 0x25 = HitOnlyGroundAndObjectWithoutSmallObject; 0x2f = GroundForCharacterController. */
    if (!bridge_rt_call_full((uint64_t)fn_mask, NULL, NULL, NULL, 3,
                             (uint64_t)(uintptr_t)(ray + 0x128u), (uint64_t)mask_idx, 1ull, 0)) {
        return 0;
    }
    {
        uint32_t *m = (uint32_t *)(ray + 0x134u);
        *m &= ~0x30000u;
    }

    if (!bridge_rt_call_full((uint64_t)fn_exec, NULL, NULL, &hit_x0, 1,
                             (uint64_t)(uintptr_t)ray, 0, 0, 0)) {
        return 0;
    }
    hit = (int)(hit_x0 & 1ull);
    if (!hit || !ray[0x20u]) return 0;

    {
        float hpos[3], norm[3], frac = -1.0f;
        uintptr_t hit_rb = 0;
        uint32_t cfi = 0;
        float dx, dy, dz, d;
        memcpy(hpos, ray + 0x24u, sizeof(hpos));
        memcpy(norm, ray + 0x30u, sizeof(norm));
        memcpy(&frac, ray + 0x3cu, sizeof(frac));
        memcpy(&cfi, ray + 0x58u, sizeof(cfi));
        memcpy(&hit_rb, ray + 0x170u, sizeof(hit_rb));
        if (!valid_xyz(hpos) || !isfinite(norm[0]) || !isfinite(norm[1]) || !isfinite(norm[2])) {
            return 0;
        }
        dx = hpos[0] - origin[0];
        dy = hpos[1] - origin[1];
        dz = hpos[2] - origin[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (out_hit) memcpy(out_hit, hpos, sizeof(hpos));
        if (out_normal) memcpy(out_normal, norm, sizeof(norm));
        if (out_dist) *out_dist = d;
        if (out_hit_rb) *out_hit_rb = hit_rb;
        if (out_frac) *out_frac = frac;
        if (out_cfi) *out_cfi = cfi;
    }
    return 1;
}

float mc_totk_camera_clearance(const float origin[3], const float dir[3], float distance) {
    if (!g_initialized) return distance;
    if (!require_guest_thread("mc_totk_camera_clearance")) return distance;
    if (!origin || !dir || !isfinite(distance)) return distance;
    float end[3] = {origin[0] + dir[0] * distance, origin[1] + dir[1] * distance,
                    origin[2] + dir[2] * distance};
    float hit = distance;
    if (raycast_raw_masked(origin, end, 0x25u, 1, NULL, NULL, &hit, NULL, NULL, NULL)) {
        return fmaxf(0.15f, fminf(distance, hit - 0.20f));
    }
    return distance;
}

static int raycast_filtered(const float origin[3], const float udir[3], float max_dist,
                            uint32_t mask_idx, int log_close, int apply_player_cyl,
                            float min_up_norm_y, float max_hit_y,
                            float out_hit[3], float out_normal[3], float *out_dist) {
    static int s_close_hit_logs = 0;
    uintptr_t player, player_rb = 0;
    float ppos[3] = { 0.0f, 0.0f, 0.0f };
    int has_ppos = 0;
    float cur_orig[3], rem = max_dist, total_d = 0.0f;
    int ignore_start_inside = 0;
    int is_vertical = (fabsf(udir[1]) >= 0.90f);

    player = find_player_actor();
    if (player) {
        resolve_player_physics(player, NULL, NULL, NULL, &player_rb);
        if (apply_player_cyl && !is_vertical &&
            read_mem(player + ACT_POS_OFF, ppos, sizeof(ppos)) && valid_xyz(ppos)) {
            has_ppos = 1;
        }
    }

    memcpy(cur_orig, origin, sizeof(cur_orig));
    for (int attempt = 0; attempt < 12 && rem > 0.05f; ++attempt) {
        float end[3] = {
            cur_orig[0] + udir[0] * rem,
            cur_orig[1] + udir[1] * rem,
            cur_orig[2] + udir[2] * rem
        };
        float hpos[3], hnorm[3], step_d = 0.0f, frac = -1.0f;
        uintptr_t hit_rb = 0;
        uint32_t shape_key = 0;
        int is_self_rb, is_player_body = 0, is_start_inside;
        int skip_steep_or_high = 0;

        if (!raycast_raw_masked(cur_orig, end, mask_idx, ignore_start_inside,
                                hpos, hnorm, &step_d, &hit_rb, &frac, &shape_key)) {
            return 0;
        }
        is_self_rb = (player_rb && hit_rb == player_rb);
        is_start_inside = (isfinite(frac) && frac >= 0.0f && frac <= 1e-4f);
        /* Only skip the ~1.35m cylindrical guard around Link when apply_player_cyl is requested;
         * straight-downward ground checks and wall sweeps must never hide floors/walls near Link. */
        if (has_ppos && apply_player_cyl && !is_vertical) {
            float hdx = hpos[0] - ppos[0];
            float hy = hpos[1] - ppos[1];
            float hdz = hpos[2] - ppos[2];
            if ((hdx * hdx + hdz * hdz) < (1.35f * 1.35f) && hy > 0.15f && hy < 2.35f) {
                is_player_body = 1;
            }
        }
        if (isfinite(min_up_norm_y) && hnorm[1] < min_up_norm_y) skip_steep_or_high = 1;
        if (isfinite(max_hit_y) && hpos[1] > max_hit_y) skip_steep_or_high = 1;

        if (is_start_inside && ignore_start_inside < 2) {
            ++ignore_start_inside;
            if (log_close && s_close_hit_logs < 12) {
                ++s_close_hit_logs;
                rt_log("[mc-totk-ray] retry_no_start_in_shape mode=%d try=%d orig=(%.2f,%.2f,%.2f) "
                       "shape=%#x rb=%#llx",
                       ignore_start_inside, attempt, cur_orig[0], cur_orig[1], cur_orig[2],
                       shape_key, (unsigned long long)hit_rb);
            }
            continue;
        }
        if (is_self_rb || is_player_body || is_start_inside || skip_steep_or_high) {
            float skip = step_d + (is_start_inside ? 0.25f : 0.15f);
            if (log_close && is_start_inside && s_close_hit_logs < 12) {
                ++s_close_hit_logs;
                rt_log("[mc-totk-ray] skip_inside try=%d orig=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) "
                       "hit=(%.2f,%.2f,%.2f) d=%.3f frac=%.5f shape=%#x rb=%#llx",
                       attempt, cur_orig[0], cur_orig[1], cur_orig[2],
                       udir[0], udir[1], udir[2],
                       hpos[0], hpos[1], hpos[2], step_d, frac, shape_key,
                       (unsigned long long)hit_rb);
            }
            cur_orig[0] += udir[0] * skip;
            cur_orig[1] += udir[1] * skip;
            cur_orig[2] += udir[2] * skip;
            total_d += skip;
            rem -= skip;
            continue;
        }
        if (log_close && fabsf(udir[1]) < 0.99f && step_d < 0.35f && s_close_hit_logs < 12) {
            ++s_close_hit_logs;
            rt_log("[mc-totk-ray] close_hit try=%d orig=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) "
                   "hit=(%.2f,%.2f,%.2f) n=(%.2f,%.2f,%.2f) d=%.3f frac=%.5f shape=%#x rb=%#llx",
                   attempt, cur_orig[0], cur_orig[1], cur_orig[2],
                   udir[0], udir[1], udir[2],
                   hpos[0], hpos[1], hpos[2], hnorm[0], hnorm[1], hnorm[2],
                   step_d, frac, shape_key, (unsigned long long)hit_rb);
        }
        if (out_hit) memcpy(out_hit, hpos, sizeof(hpos));
        if (out_normal) memcpy(out_normal, hnorm, sizeof(hnorm));
        if (out_dist) *out_dist = total_d + step_d;
        return 1;
    }
    return 0;
}

static int raycast_world(const float origin[3], const float dir[3], float max_dist,
                         int skip_player_cylinder, float out_hit[3],
                         float out_normal[3], float *out_dist) {
    float dlen, udir[3];

    if (!g_initialized) return 0;
    if (!require_guest_thread("mc_totk_raycast")) return 0;
    if (!origin || !dir || !isfinite(max_dist) || max_dist <= 0.01f) return 0;
    if (!isfinite(origin[0]) || !isfinite(origin[1]) || !isfinite(origin[2])) return 0;
    dlen = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!isfinite(dlen) || dlen < 1e-5f) return 0;
    udir[0] = dir[0] / dlen;
    udir[1] = dir[1] / dlen;
    udir[2] = dir[2] / dlen;

    return raycast_filtered(origin, udir, max_dist, 0x25u, 1, skip_player_cylinder,
                            NAN, NAN, out_hit, out_normal, out_dist);
}

int mc_totk_raycast(const float origin[3], const float dir[3], float max_dist,
                    float out_hit[3], float out_normal[3], float *out_dist) {
    return raycast_world(origin, dir, max_dist, 1, out_hit, out_normal, out_dist);
}

int mc_totk_raycast_from_eye(const float origin[3], const float dir[3], float max_dist,
                             float out_hit[3], float out_normal[3], float *out_dist) {
    return raycast_world(origin, dir, max_dist, 0, out_hit, out_normal, out_dist);
}

static int probe_down_walkable(float x, float start_y, float z, float max_dist,
                               float max_valid_y, float min_norm_y,
                               float *out_y, float *out_norm_y) {
    static const float down[3] = {0.0f, -1.0f, 0.0f};
    static const uint32_t masks[2] = {0x25u, 0x2fu}; /* Ground+Obj, then GroundForCharacterController */
    float orig[3] = {x, start_y, z};
    float hit[3], norm[3], dist = 0.0f;

    for (int m = 0; m < 2; ++m) {
        if (raycast_filtered(orig, down, max_dist, masks[m], 0, 0,
                             min_norm_y, max_valid_y, hit, norm, &dist) &&
            norm[1] >= min_norm_y && hit[1] <= max_valid_y) {
            if (out_y) *out_y = hit[1];
            if (out_norm_y) *out_norm_y = norm[1];
            return 1;
        }
    }
    return 0;
}

int mc_totk_ground_height_with_normal(float x, float y_hint, float z,
                                      float *out_y, float *out_norm_y) {
    float hy = 0.0f, ny = 0.0f;

    if (!out_y || !isfinite(x) || !isfinite(y_hint) || !isfinite(z)) return 0;
    if (!g_initialized || !require_guest_thread("mc_totk_ground_height")) {
        *out_y = y_hint;
        if (out_norm_y) *out_norm_y = 0.0f;
        return 0;
    }

    if (probe_down_walkable(x, y_hint + 1.8f, z, 96.0f, y_hint + 1.8f, 0.30f, &hy, &ny) ||
        probe_down_walkable(x, y_hint + 4.5f, z, 160.0f, y_hint + 4.0f, 0.30f, &hy, &ny)) {
        *out_y = hy;
        if (out_norm_y) *out_norm_y = ny;
        return 1;
    }

    *out_y = y_hint;
    if (out_norm_y) *out_norm_y = 0.0f;
    return 0;
}

int mc_totk_ground_height(float x, float y_hint, float z, float *out_y) {
    return mc_totk_ground_height_with_normal(x, y_hint, z, out_y, NULL);
}

static int sweep_horz_ray(const float orig[3], const float dir[3], float cast_len,
                          float *out_d, float out_norm[3]) {
    static const uint32_t masks[2] = {0x25u, 0x2fu};
    for (int m = 0; m < 2; ++m) {
        float hpos[3], hnorm[3], hd = 0.0f;
        if (raycast_filtered(orig, dir, cast_len, masks[m], 0, 0,
                             NAN, NAN, hpos, hnorm, &hd)) {
            if (fabsf(hnorm[1]) < 0.65f) {
                *out_d = hd;
                memcpy(out_norm, hnorm, 12u);
                return 1;
            }
            return 0; /* gentle walkable slope (< ~49 deg), not a wall */
        }
    }
    return 0;
}

static int plane_allow_along_dir(float px, float pz, float fx, float fz, float move_dist,
                                 float hx, float hz, const float hnorm[3], float radius,
                                 float *out_allow) {
    float wn_x = hnorm[0], wn_z = hnorm[2], wn_len = hypotf(wn_x, wn_z);
    float clearance = radius + 0.02f;
    if (wn_len > 1e-4f) {
        float into, signed_d, allow;
        wn_x /= wn_len;
        wn_z /= wn_len;
        into = -(fx * wn_x + fz * wn_z);
        signed_d = (px - hx) * wn_x + (pz - hz) * wn_z;
        if (into > 0.05f) {
            allow = fmaxf(0.0f, (signed_d - clearance) / into);
            if (allow < move_dist || signed_d < clearance) {
                *out_allow = fminf(allow, move_dist);
                return 1;
            }
            return 0;
        }
    }
    {
        float proj = (hx - px) * fx + (hz - pz) * fz;
        float allow = fmaxf(0.0f, proj - clearance);
        if (allow < move_dist) {
            *out_allow = allow;
            return 1;
        }
    }
    return 0;
}

static void resolve_wall_slide(const float pos[3], float fx, float fz, float dist,
                               float radius, float h_mid,
                               float allow, const float wall_norm[3],
                               float out_pos_xz[2], float out_wall_norm_xz[2]) {
    float rem, rem_x, rem_z, wn_x, wn_z, wn_len, dot_n, sx, sz, s_len;
    if (allow < 0.0f) allow = 0.0f;
    if (allow > dist) allow = dist;
    rem = dist - allow;
    rem_x = fx * rem;
    rem_z = fz * rem;

    wn_x = wall_norm[0];
    wn_z = wall_norm[2];
    wn_len = hypotf(wn_x, wn_z);
    if (wn_len > 1e-4f) {
        wn_x /= wn_len;
        wn_z /= wn_len;
    } else {
        wn_x = -fx;
        wn_z = -fz;
    }

    dot_n = rem_x * wn_x + rem_z * wn_z;
    if (dot_n < 0.0f) {
        sx = rem_x - dot_n * wn_x;
        sz = rem_z - dot_n * wn_z;
    } else {
        sx = 0.0f;
        sz = 0.0f;
    }

    s_len = hypotf(sx, sz);
    if (s_len > 1e-4f) {
        float bx = pos[0] + fx * allow, bz = pos[2] + fz * allow;
        float sfx = sx / s_len, sfz = sz / s_len;
        float sorig[3] = {bx, h_mid, bz};
        float sdir[3] = {sfx, 0.0f, sfz};
        float sd = 0.0f, snorm[3] = {0.0f, 0.0f, 0.0f}, s_allow = s_len;
        if (sweep_horz_ray(sorig, sdir, s_len + radius * 1.6f + 0.08f, &sd, snorm) &&
            plane_allow_along_dir(bx, bz, sfx, sfz, s_len,
                                  sorig[0] + sdir[0] * sd, sorig[2] + sdir[2] * sd,
                                  snorm, radius, &s_allow)) {
            float scale = fminf(1.0f, fmaxf(0.0f, s_allow / s_len));
            sx *= scale;
            sz *= scale;
        }
    }

    out_pos_xz[0] = pos[0] + fx * allow + sx;
    out_pos_xz[1] = pos[2] + fz * allow + sz;
    if (out_wall_norm_xz) {
        out_wall_norm_xz[0] = wn_x;
        out_wall_norm_xz[1] = wn_z;
    }
}

int mc_totk_check_wall_step(const float pos[3], float radius, float height,
                            const float delta_xz[2], float out_pos_xz[2],
                            float out_wall_norm_xz[2], float *out_step_y) {
    float dx, dz, dist, fx, fz, rx, rz;
    float h_low, h_mid, h_high;
    float best_allow = 1e9f, best_norm[3] = {0.0f, 0.0f, 0.0f};
    float low_allow = 1e9f, low_norm[3] = {0.0f, 0.0f, 0.0f};
    int wall_hit = 0, low_hit = 0;
    const float back = 0.12f;

    if (!pos || !delta_xz || !out_pos_xz) return 0;
    if (!valid_xyz(pos) || !isfinite(delta_xz[0]) || !isfinite(delta_xz[1])) {
        out_pos_xz[0] = isfinite(pos[0]) ? pos[0] : 0.0f;
        out_pos_xz[1] = isfinite(pos[2]) ? pos[2] : 0.0f;
        if (out_wall_norm_xz) { out_wall_norm_xz[0] = 0.0f; out_wall_norm_xz[1] = 0.0f; }
        if (out_step_y) *out_step_y = isfinite(pos[1]) ? pos[1] : 0.0f;
        return 0;
    }

    dx = delta_xz[0];
    dz = delta_xz[1];
    out_pos_xz[0] = pos[0] + dx;
    out_pos_xz[1] = pos[2] + dz;
    if (out_wall_norm_xz) { out_wall_norm_xz[0] = 0.0f; out_wall_norm_xz[1] = 0.0f; }
    if (out_step_y) *out_step_y = pos[1];

    if (!g_initialized || !require_guest_thread("mc_totk_check_wall_step")) return 0;
    dist = hypotf(dx, dz);
    if (!isfinite(dist) || dist < 1e-5f) {
        out_pos_xz[0] = pos[0];
        out_pos_xz[1] = pos[2];
        return 0;
    }

    if (!isfinite(radius) || radius < 0.20f) radius = 0.20f;
    else if (radius > 1.25f) radius = 1.25f;
    if (!isfinite(height) || height < 0.60f) height = 0.60f;
    else if (height > 3.00f) height = 3.00f;

    fx = dx / dist;
    fz = dz / dist;
    rx = -fz;
    rz = fx;
    h_low  = pos[1] + fminf(0.45f, height * 0.25f);
    h_mid  = pos[1] + fminf(1.05f, height * 0.55f);
    h_high = pos[1] + fmaxf(0.85f, height - 0.15f);

    {
        const float lats[3] = {0.0f, -0.65f * radius, 0.65f * radius};
        const float heights[2] = {h_mid, h_high};
        float fwd_reach = back + dist + radius * 1.6f + 0.08f;

        for (int hi = 0; hi < 2; ++hi) {
            float orig[3] = {pos[0] - fx * back, heights[hi], pos[2] - fz * back};
            for (int li = 0; li < 3; ++li) {
                float tx = fx * fwd_reach + rx * lats[li];
                float tz = fz * fwd_reach + rz * lats[li];
                float cast_len = hypotf(tx, tz);
                float dir[3] = {tx / cast_len, 0.0f, tz / cast_len};
                float hd = 0.0f, hnorm[3] = {0.0f, 0.0f, 0.0f}, allow = dist;
                if (sweep_horz_ray(orig, dir, cast_len, &hd, hnorm) &&
                    plane_allow_along_dir(pos[0], pos[2], fx, fz, dist,
                                          orig[0] + dir[0] * hd, orig[2] + dir[2] * hd,
                                          hnorm, radius, &allow)) {
                    if (!wall_hit || allow < best_allow) {
                        best_allow = allow;
                        memcpy(best_norm, hnorm, sizeof(best_norm));
                        wall_hit = 1;
                    }
                }
            }
        }
        if (!wall_hit) {
            float lorig[3] = {pos[0] - fx * back, h_low, pos[2] - fz * back};
            float dir[3] = {fx, 0.0f, fz};
            float hd = 0.0f, hnorm[3] = {0.0f, 0.0f, 0.0f}, allow = dist;
            if (sweep_horz_ray(lorig, dir, fwd_reach, &hd, hnorm) &&
                plane_allow_along_dir(pos[0], pos[2], fx, fz, dist,
                                      lorig[0] + dir[0] * hd, lorig[2] + dir[2] * hd,
                                      hnorm, radius, &allow)) {
                low_allow = allow;
                memcpy(low_norm, hnorm, sizeof(low_norm));
                low_hit = 1;
            }
        }
    }

    if (wall_hit) {
        resolve_wall_slide(pos, fx, fz, dist, radius, h_mid,
                           best_allow, best_norm, out_pos_xz, out_wall_norm_xz);
        return 1;
    }

    if (low_hit) {
        float sy = 0.0f, sny = 0.0f;
        if (mc_totk_ground_height_with_normal(out_pos_xz[0], pos[1] + 1.25f, out_pos_xz[1],
                                              &sy, &sny) &&
            sy > pos[1] + 0.05f && sy <= pos[1] + 1.15f && sny >= 0.45f) {
            if (out_step_y) *out_step_y = sy;
            return 2;
        }
        resolve_wall_slide(pos, fx, fz, dist, radius, h_low,
                           low_allow, low_norm, out_pos_xz, out_wall_norm_xz);
        return 1;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Active Actor Enumeration, Damage Dealing, & Incoming Damage Monitor
 * -------------------------------------------------------------------------- */
static int is_bomb_target(const char *name) {
    // These actor packs use the game's CrackStoneObject or explosive-object damage parameters.
    return strncmp(name, "Obj_RockBroken_", sizeof("Obj_RockBroken_") - 1u) == 0 ||
           strncmp(name, "DgnObj_Small_BarrelBomb_", sizeof("DgnObj_Small_BarrelBomb_") - 1u) == 0 ||
           strcmp(name, "Obj_SecretBaseRockBroken_A_01") == 0 ||
           strcmp(name, "BlockCrackFixed4x4x4") == 0 ||
           strcmp(name, "DgnObj_Small_BlockCrackFixed_A_01") == 0 ||
           strcmp(name, "DgnObj_ElectricBlockCrack_A_01") == 0 ||
           strcmp(name, "WallCrack") == 0 ||
           strcmp(name, "TwnObj_HyruleCastleObject_Wallcrack_Before_A_01") == 0 ||
           strcmp(name, "TwnObj_HyruleCity_Tower_WallCrack_A_01") == 0 ||
           strcmp(name, "BarrelBomb") == 0 || strcmp(name, "BarrelBomb2") == 0 ||
           strcmp(name, "BarrelBomb3") == 0 || strcmp(name, "TimerBarrelBomb") == 0;
}

static McTotkActorKind classify_actor_name(const char *name) {
    static const char *const enemy_substrings[] = {
        "Bokoblin", "Moriblin", "Moblin", "Lizalfos", "Lynel",
        "Hinox", "Talus", "Golem", "PhantomGanon", "Ganondorf",
        "Gibdo", "Chuchu", "Keese", "LikeLike", "Octorok",
        "Wizzrobe", "Aerocuda", "Horroblin", "Horriblin",
        "Frox", "Gleeok", "ZonauGolem", "Phantom"
    };
    if (!name || !name[0]) return MC_TOTK_ACTOR_OTHER;
    if (is_bomb_target(name)) return MC_TOTK_ACTOR_BOMB_TARGET;
    if (strncmp(name, "Item_", 5) == 0 || strncmp(name, "Weapon_", 7) == 0 ||
        strncmp(name, "Obj_Drop", 8) == 0) {
        return MC_TOTK_ACTOR_ITEM;
    }
    if (strncmp(name, "Enemy_", 6) == 0 || strncmp(name, "Boss_", 5) == 0 ||
        strncmp(name, "DungeonBoss_", 12) == 0 || strncmp(name, "Npc_Assassin", 12) == 0) {
        return MC_TOTK_ACTOR_ENEMY;
    }
    if (strncmp(name, "Armor_", 6) != 0 && strncmp(name, "Obj_", 4) != 0) {
        for (size_t i = 0; i < sizeof(enemy_substrings) / sizeof(enemy_substrings[0]); ++i) {
            if (strstr(name, enemy_substrings[i]) != NULL) {
                return MC_TOTK_ACTOR_ENEMY;
            }
        }
    }
    if (strncmp(name, "Npc_", 4) == 0) return MC_TOTK_ACTOR_NPC;
    return MC_TOTK_ACTOR_OTHER;
}

static void read_actor_bounds(uintptr_t actor, const float pos[3], McTotkActorKind kind,
                              float out_min[3], float out_max[3], float *out_radius) {
    float bmin[3], bmax[3];
    if (read_mem(actor + ACT_AABB_MIN_OFF, bmin, sizeof(bmin)) &&
        read_mem(actor + ACT_AABB_MAX_OFF, bmax, sizeof(bmax))) {
        int k, ok = 1;
        float span_sum = 0.0f;
        for (k = 0; k < 3; ++k) {
            float span = bmax[k] - bmin[k];
            if (!isfinite(bmin[k]) || !isfinite(bmax[k]) || bmin[k] > bmax[k] || span > 200.0f) {
                ok = 0;
                break;
            }
            span_sum += span;
        }
        /* Reject degenerate (0,0,0)..(0,0,0) AABBs cleanly into the capsule fallback. */
        if (ok && span_sum > 0.05f) {
            float scale[3] = { 1.0f, 1.0f, 1.0f };
            float wmin[3], wmax[3];
            int far_from_origin = (fabsf(pos[0]) >= 8.0f || fabsf(pos[1]) >= 8.0f ||
                                   fabsf(pos[2]) >= 8.0f);
            int already_world = far_from_origin &&
                pos[0] >= bmin[0] - 2.0f && pos[0] <= bmax[0] + 2.0f &&
                pos[1] >= bmin[1] - 2.0f && pos[1] <= bmax[1] + 2.0f &&
                pos[2] >= bmin[2] - 2.0f && pos[2] <= bmax[2] + 2.0f;

            if (already_world) {
                memcpy(wmin, bmin, sizeof(wmin));
                memcpy(wmax, bmax, sizeof(wmax));
            } else {
                /* Verified at main+0x8f2aa0: [actor + 0x350..0x364] is local-space ModelAABB
                 * scaled by [actor + 0x2e4..0x2ec] and translated by actor world pos. */
                float raw_scale[3];
                if (read_mem(actor + ACT_SCALE_OFF, raw_scale, sizeof(raw_scale))) {
                    for (k = 0; k < 3; ++k) {
                        if (isfinite(raw_scale[k]) && raw_scale[k] >= 0.1f && raw_scale[k] <= 25.0f) {
                            scale[k] = raw_scale[k];
                        }
                    }
                }
                for (k = 0; k < 3; ++k) {
                    wmin[k] = pos[k] + bmin[k] * scale[k];
                    wmax[k] = pos[k] + bmax[k] * scale[k];
                    if (wmax[k] - wmin[k] > 200.0f || !isfinite(wmin[k]) || !isfinite(wmax[k])) {
                        ok = 0;
                        break;
                    }
                }
            }

            if (ok) {
                float dx = wmax[0] - wmin[0];
                float dz = wmax[2] - wmin[2];
                float r = 0.5f * sqrtf(dx * dx + dz * dz);
                if (r < 0.35f) r = 0.35f;
                if (r > 50.0f) r = 50.0f;
                memcpy(out_min, wmin, sizeof(wmin));
                memcpy(out_max, wmax, sizeof(wmax));
                *out_radius = r;
                return;
            }
        }
    }
    {
        float r = (kind == MC_TOTK_ACTOR_ENEMY) ? 1.1f : 0.6f;
        out_min[0] = pos[0] - r; out_min[1] = pos[1];        out_min[2] = pos[2] - r;
        out_max[0] = pos[0] + r; out_max[1] = pos[1] + 2.0f; out_max[2] = pos[2] + r;
        *out_radius = r;
    }
}

typedef struct {
    uintptr_t player;
    const float *center;
    float max_r2;
    McTotkActorInfo *out;
    float *dist2;
    int max_out;
    int written;
    uint32_t chunks_seen;
    uint32_t slots_seen;
    uint32_t bit4_hits;
    uint32_t live_hits;
    uint32_t sample_flags;
} ActorCollectCtx;

static void insert_actor_candidate(ActorCollectCtx *ctx, const McTotkActorInfo *info, float d2) {
    int i, victim = -1;
    for (i = 0; i < ctx->written; ++i) {
        if (ctx->out[i].actor == info->actor) return;
    }
    if (ctx->written < ctx->max_out) {
        int idx = ctx->written++;
        ctx->out[idx] = *info;
        ctx->dist2[idx] = d2;
        return;
    }
    /* When full, prefer Enemies over non-Enemies, then keep closest distance. */
    for (i = 0; i < ctx->written; ++i) {
        if (info->kind == MC_TOTK_ACTOR_ENEMY && ctx->out[i].kind != MC_TOTK_ACTOR_ENEMY) {
            if (victim < 0 || ctx->dist2[i] > ctx->dist2[victim]) victim = i;
        }
    }
    if (victim < 0) {
        float worst_d2 = d2;
        for (i = 0; i < ctx->written; ++i) {
            if (info->kind != MC_TOTK_ACTOR_ENEMY && ctx->out[i].kind == MC_TOTK_ACTOR_ENEMY) continue;
            if (ctx->dist2[i] > worst_d2) {
                worst_d2 = ctx->dist2[i];
                victim = i;
            }
        }
    }
    if (victim >= 0) {
        ctx->out[victim] = *info;
        ctx->dist2[victim] = d2;
    }
}

static void scan_actor_bank(uintptr_t mgr, uint32_t count_off, uint32_t chunks_off,
                            ActorCollectCtx *ctx) {
    uint32_t chunk_count = 0, ci, si;
    uintptr_t chunks;
    if (!read_u32(mgr + count_off, &chunk_count) || chunk_count == 0) return;
    if (chunk_count > 128u) chunk_count = 128u;
    chunks = read_ptr(mgr + chunks_off);
    if (chunks < 0x100000u) return;

    for (ci = 0; ci < chunk_count; ++ci) {
        uintptr_t chunk = chunks + (uintptr_t)ci * 0x68u;
        uint32_t slot_count = 0;
        uintptr_t slots;
        if (!read_u32(chunk, &slot_count) || slot_count == 0) continue;
        if (slot_count > 2048u) slot_count = 2048u;
        slots = read_ptr(chunk + 0x08u);
        if (slots < 0x100000u) continue;
        ctx->chunks_seen++;

        /* ActorLink slots are contiguous. Batch their reads because player
         * arrows query nearby actors on successive frames. */
        uint8_t batch[32 * 0x100];
        int have_batch = 0;
        for (si = 0; si < slot_count; ++si) {
            uintptr_t slot = slots + (uintptr_t)si * 0x100u;
            uint32_t flags = 0;
            uint64_t raw_actor = 0;
            uintptr_t actor;
            McTotkActorInfo info;
            float d2 = 0.0f;

            unsigned slot_in_batch = si % 32u;
            if (slot_in_batch == 0u) {
                unsigned remaining = slot_count - si;
                have_batch = read_mem(slot, batch, (remaining < 32u ? remaining : 32u) * 0x100u);
            }
            ctx->slots_seen++;
            if (have_batch) {
                const uint8_t *entry = batch + slot_in_batch * 0x100u;
                memcpy(&flags, entry + 0x98, sizeof(flags));
                memcpy(&raw_actor, entry + 0x20, sizeof(raw_actor));
            } else if (!read_u32(slot + 0x98u, &flags) || !read_u64(slot + 0x20u, &raw_actor)) {
                continue;
            }
            if (ctx->sample_flags == 0u && flags != 0u) ctx->sample_flags = flags;
            if ((flags & 0x10u) != 0u) ctx->bit4_hits++;
            if (raw_actor & 1ull) continue;
            actor = (uintptr_t)(raw_actor & ~3ull);
            if (actor < 0x100000u || actor == ctx->player || !actor_is_live(actor)) continue;
            if (!(flags & 0x10u) && !is_text_ptr(read_ptr(read_ptr(actor)))) continue;
            ctx->live_hits++;

            memset(&info, 0, sizeof(info));
            if (!read_actor_name(actor, info.name, sizeof(info.name)) || !info.name[0]) continue;
            if (strcmp(info.name, "Player") == 0) continue;
            if (!read_mem(actor + ACT_POS_OFF, info.pos, sizeof(info.pos)) || !valid_xyz(info.pos)) {
                continue;
            }
            if (ctx->center) {
                float dx = info.pos[0] - ctx->center[0];
                float dy = info.pos[1] - ctx->center[1];
                float dz = info.pos[2] - ctx->center[2];
                d2 = dx * dx + dy * dy + dz * dz;
                if (ctx->max_r2 > 0.0f && d2 > ctx->max_r2) continue;
            }

            info.actor = (void *)actor;
            info.kind = classify_actor_name(info.name);
            info.hp = -1;
            info.max_hp = -1;
            int has_life = read_actor_life(actor, &info.hp, &info.max_hp, NULL, NULL, NULL);
            /* Some live enemies and destructible objects lack the normal linked-object flag. */
            if (!(flags & 0x10u) &&
                !(has_life && (info.kind == MC_TOTK_ACTOR_BOMB_TARGET ||
                               (info.kind == MC_TOTK_ACTOR_ENEMY && info.hp > 0)))) continue;
            read_actor_bounds(actor, info.pos, info.kind, info.aabb_min, info.aabb_max, &info.radius);
            insert_actor_candidate(ctx, &info, d2);
        }
    }
}

static int enumerate_actors_impl(const float center[3], float max_radius,
                                 McTotkActorInfo *out, int max_out) {
    static uint32_t s_empty_logs = 0;
    uintptr_t mgr;
    float *dist2;
    ActorCollectCtx ctx;

    if (!g_initialized || !out || max_out <= 0) return 0;
    if (max_out > 2048) max_out = 2048;
    mgr = read_got_instance(OFF_GOT_ACTORMGR);
    if (!mgr) return 0;

    dist2 = (float *)calloc((size_t)max_out, sizeof(float));
    if (!dist2) return 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.player = find_player_actor();
    ctx.center = (center && isfinite(center[0]) && isfinite(center[1]) && isfinite(center[2]))
                 ? center : NULL;
    ctx.max_r2 = (ctx.center && isfinite(max_radius) && max_radius > 0.0f)
                 ? (max_radius * max_radius) : 0.0f;
    ctx.out = out;
    ctx.dist2 = dist2;
    ctx.max_out = max_out;

    scan_actor_bank(mgr, 0xa8u, 0xb0u, &ctx);
    scan_actor_bank(mgr, 0xb8u, 0xc0u, &ctx);

    if (ctx.written == 0 && s_empty_logs < 8u && (s_empty_logs == 0u || ctx.slots_seen > 0u)) {
        ++s_empty_logs;
        rt_log("[mc-totk-enum] mgr=%p chunks=%u slots=%u bit4=%u live=%u sample_flags=0x%x",
               (void *)mgr, ctx.chunks_seen, ctx.slots_seen, ctx.bit4_hits,
               ctx.live_hits, ctx.sample_flags);
    }

    free(dist2);
    return ctx.written;
}

int mc_totk_enumerate_actors(const float center[3], float max_radius,
                             McTotkActorInfo *out, int max_out) {
    if (!g_initialized || !out || max_out <= 0) return 0;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_enumerate_actors")) return 0;
    return enumerate_actors_impl(center, max_radius, out, max_out);
}

static int dmg_table_has_key(uintptr_t arr, uint32_t cap, uint32_t stride, uint32_t key) {
    if (arr < 0x100000u || cap == 0u || key == 0u) return 0;
    if (cap > 256u) cap = 256u;
    for (uint32_t i = 0; i < cap; ++i) {
        uint32_t k = 0;
        if (read_u32(arr + (uintptr_t)i * stride, &k) && k == key) return 1;
    }
    return 0;
}

static uint32_t find_damage_receiver_key(uintptr_t dmg_recv) {
    uintptr_t arr30 = read_ptr(dmg_recv + 0x30u);
    uintptr_t arr20 = read_ptr(dmg_recv + 0x20u);
    uint32_t cap30 = 0, cap20 = 0;

    if (!read_u32(dmg_recv + 0x3cu, &cap30) || !read_u32(dmg_recv + 0x2cu, &cap20)) {
        return 0u;
    }
    if (arr30 < 0x100000u || arr20 < 0x100000u || cap30 == 0u || cap20 == 0u) return 0u;
    if (cap30 > 256u) cap30 = 256u;
    if (cap20 > 256u) cap20 = 256u;

    /* Prefer canonical body key 0xd111dbe1 if registered in both DamageReceiver maps. */
    if (dmg_table_has_key(arr30, cap30, 0x128u, 0xd111dbe1u) &&
        dmg_table_has_key(arr20, cap20, 0x38u, 0xd111dbe1u)) {
        return 0xd111dbe1u;
    }
    for (uint32_t i = 0; i < cap30; ++i) {
        uint32_t k = 0;
        if (read_u32(arr30 + (uintptr_t)i * 0x128u, &k) && k != 0u &&
            dmg_table_has_key(arr20, cap20, 0x38u, k)) {
            return k;
        }
    }
    return 0u;
}

void *mc_totk_trace_actor(const float origin[3], const float direction[3], float distance,
                          float hit[3]) {
    if (!g_initialized || !origin || !direction || distance <= 0.0f) return NULL;
    if (!require_guest_thread("mc_totk_trace_actor")) return NULL;

    float end[3], point[3];
    for (int axis = 0; axis < 3; ++axis) end[axis] = origin[axis] + direction[axis] * distance;
    uintptr_t body = 0;
    /* HitToCharacterAndObject is index 1 in the native collision mask table. */
    if (!raycast_raw_masked(origin, end, 1u, 2, point, NULL, NULL, &body, NULL, NULL) || !body)
        return NULL;

    /* PhysicsBody's owner handle: verified in the native ray consumer
     * at 0x21b1fa0 and the handle validity check at 0x9ee934. */
    uintptr_t owner = read_ptr(body + 0x110u);
    if (!owner) return NULL;
    uintptr_t link = read_ptr(owner + 0x20u);
    uint32_t owner_id = 0, link_id = 0;
    if (!link || !read_u32(owner + 0x28u, &owner_id) || owner_id == UINT32_MAX ||
        !read_u32(link + 0x48u, &link_id) || owner_id != link_id) return NULL;
    uintptr_t actor = read_ptr(link + 0x40u);
    char name[64];
    if (!is_obj_ptr(actor) || !actor_is_live(actor) ||
        !read_actor_name(actor, name, sizeof(name)) ||
        classify_actor_name(name) != MC_TOTK_ACTOR_ENEMY) return NULL;
    if (hit) memcpy(hit, point, sizeof(point));
    return (void *)actor;
}

static int request_zero_life_death(uintptr_t actor, uintptr_t comp) {
    uintptr_t death = read_ptr(comp + COMP_DEATH_OFF);
    uintptr_t fn = text_addr(FN_DEATH_REQUEST);
    if (!death || !fn || read_ptr(death + 0x18u) != actor) return 0;

    // LifeComp uses reason 0x10 at main+0x64b5e8 when HP reaches zero.
    uint64_t accepted = 0;
    uint64_t check_death_procedure = read_ptr(comp + COMP_DEATH_AUX_OFF) == 0;
    return bridge_rt_call_full(fn, NULL, NULL, &accepted, 4,
                               (uint64_t)death, 0x10u, 0, check_death_procedure) && (accepted & 1u);
}

static int damage_actor(void *actor_ptr, int damage, const float impulse_dir[3], const float blast_center[3]) {
    uintptr_t actor = (uintptr_t)actor_ptr;
    uintptr_t player, comp, dmg_recv, fn_ctor, fn_add;
    float pos[3], dir[3];
    char name[96];
    int hp_before = -1, hp_after = -1;
    uintptr_t life = 0, cur_holder = 0;
    uint32_t dmg_key = 0u;
    int dmg_recv_ok = 0, life_ok = 0;

    if (!g_initialized || !g_scratch || !actor || damage <= 0) return 0;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_damage_actor")) return 0;

    player = find_player_actor();
    if (actor == player || !actor_is_live(actor)) {
        rt_log("[mc-totk-dmg] fail actor=%p player_or_dead dmg=%d", actor_ptr, damage);
        return 0;
    }
    if (!read_mem(actor + ACT_POS_OFF, pos, sizeof(pos)) || !valid_xyz(pos)) {
        rt_log("[mc-totk-dmg] fail actor=%p bad_pos dmg=%d", actor_ptr, damage);
        return 0;
    }

    name[0] = '\0';
    (void)read_actor_name(actor, name, sizeof(name));
    if (blast_center && !is_bomb_target(name)) return 0;
    if (read_actor_life(actor, &hp_before, NULL, NULL, NULL, &life) && hp_before <= 0 && !blast_center) return 0;
    hp_after = hp_before;

    if (impulse_dir && isfinite(impulse_dir[0]) && isfinite(impulse_dir[1]) &&
        isfinite(impulse_dir[2])) {
        float l = sqrtf(impulse_dir[0] * impulse_dir[0] + impulse_dir[1] * impulse_dir[1] +
                        impulse_dir[2] * impulse_dir[2]);
        if (l > 1e-4f) {
            dir[0] = impulse_dir[0] / l;
            dir[1] = impulse_dir[1] / l;
            dir[2] = impulse_dir[2] / l;
        } else {
            dir[0] = 0.0f; dir[1] = 0.25f; dir[2] = 0.968f;
        }
    } else {
        dir[0] = 0.0f; dir[1] = 0.25f; dir[2] = 0.968f;
    }

    comp = read_ptr(actor + ACT_COMP_OFF);
    dmg_recv = (comp >= 0x100000u) ? read_ptr(comp + COMP_DMG_RECEIVER_OFF) : 0;
    fn_ctor = text_addr(FN_DAMAGE_INFO_CTOR);
    fn_add = text_addr(FN_ADD_DAMAGE_ENTRY);

    if (hp_before != 0 && dmg_recv >= 0x100000u && read_ptr(dmg_recv) && fn_ctor && fn_add) {
        dmg_key = find_damage_receiver_key(dmg_recv);
        if (dmg_key != 0u) {
            float *gpos = (float *)(g_scratch + SCRATCH_POS_OFF);
            float *gdir = (float *)(g_scratch + SCRATCH_DIR_OFF);
            uint8_t *dinfo = g_scratch + SCRATCH_DINFO_OFF;
            uint64_t add_ret = 0;
            memcpy(gpos, blast_center ? blast_center : pos, sizeof(pos));
            memcpy(gdir, dir, sizeof(dir));
            memset(dinfo, 0, 0x1a00u);
            if (bridge_rt_call_full((uint64_t)fn_ctor, NULL, NULL, NULL, 4,
                                    (uint64_t)(uintptr_t)dinfo, (uint64_t)dmg_key,
                                    (uint64_t)(uintptr_t)gpos, (uint64_t)(uintptr_t)gdir)) {
                /* Verified at main+0x16491b0..0x1649228 & 0x15d0708: +0x1920 damage type,
                 * +0x1924 hit level, +0x1930 int32 damage, +0x1934 float impulse scale,
                 * +0x1938/+0x1939 field presence flags (keep bit0 of +0x1939 clear). */
                // DamageType names in 1.2.1 start at main+0x43007d0, with Bomb at index 7.
                *(int32_t *)(dinfo + 0x1920u) = blast_center ? 7 : 0x0a;
                *(int32_t *)(dinfo + 0x1924u) = (damage >= 40) ? 2 : 1;
                *(int32_t *)(dinfo + 0x1930u) = (int32_t)damage;
                *(float *)(dinfo + 0x1934u) = 1.0f;
                dinfo[0x1938u] |= 0x04u;
                dinfo[0x1939u] |= 0x16u;
                if (bridge_rt_call_full((uint64_t)fn_add, NULL, NULL, &add_ret, 2,
                                        (uint64_t)dmg_recv, (uint64_t)(uintptr_t)dinfo, 0, 0)) {
                    dmg_recv_ok = ((int32_t)(uint32_t)add_ret != 0) ? 1 : 0;
                }
            }
        }
    }

    /* Guarantee deterministic LifeComp HP reduction (and FN_LIFECOMP_SYNC) alongside hit reaction. */
    if ((!blast_center || !dmg_recv_ok) &&
        life && (cur_holder = read_ptr(life + LIFE_CUR_HOLDER_OFF)) >= 0x100000u) {
        int32_t hp_now = 0;
        if (read_i32(cur_holder + 8u, &hp_now)) {
            if (hp_before >= 0 && hp_now < hp_before) {
                hp_after = hp_now;
                life_ok = 1;
            } else if (is_writable_guest(cur_holder + 8u, 4u)) {
                int next = hp_now - damage;
                uintptr_t fn_sync = text_addr(FN_LIFECOMP_SYNC);
                if (next < 0) next = 0;
                __atomic_store_n((int32_t *)(cur_holder + 8u), (int32_t)next, __ATOMIC_RELAXED);
                if (fn_sync) {
                    (void)bridge_rt_call_full((uint64_t)fn_sync, NULL, NULL, NULL, 1,
                                              (uint64_t)life, 0, 0, 0);
                }
                hp_after = next;
                life_ok = 1;
            }
        }
    }

    int death_requested = 0;
    if (blast_center && !dmg_recv_ok && hp_after == 0) {
        // Syncing the HP holder alone leaves some static objects with their collision intact.
        death_requested = request_zero_life_death(actor, comp);
        rt_log("[mc-totk-blast] %s native destruction request accepted=%d", name, death_requested);
    }
    rt_log("[mc-totk-dmg] actor=%p (%s) key=0x%08x dmg=%d dmg_recv_ok=%d life_ok=%d hp=%d->%d",
           actor_ptr, name[0] ? name : "?", dmg_key, damage,
           dmg_recv_ok, life_ok, hp_before, hp_after);
    return dmg_recv_ok || (blast_center && hp_after == 0 ? death_requested : life_ok);
}

int mc_totk_damage_actor(void *actor, int damage, const float impulse_dir[3]) {
    return damage_actor(actor, damage, impulse_dir, NULL);
}

int mc_totk_blast_object(void *actor, const float center[3], const float impulse_dir[3]) {
    if (!center || !valid_xyz(center)) return 0;
    // The Bomb Flower AttackParam uses 40 base damage.
    return damage_actor(actor, 40, impulse_dir, center);
}

int mc_totk_poll_incoming_damage(int minecraft_active, McTotkIncomingDamage *out) {
    uintptr_t player = 0, life = 0;
    int cur_hp = 0;
    float ppos[3];

    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!g_initialized) return 0;
    latch_guest_thread();
    if (!require_guest_thread("mc_totk_poll_incoming_damage")) return 0;

    if (minecraft_active) {
        player = find_player_actor();
        if (!player || !read_actor_life(player, &cur_hp, NULL, NULL, NULL, &life)) {
            player = life = 0;
        }
    }

    uintptr_t target = (g_health_hook_available && cur_hp > 0) ? life : 0;
    plat_mutex_lock(&g_player_health_lock);
    uintptr_t previous = atomic_exchange_explicit(&g_mc_player_life, target, memory_order_release);
    int pending = (previous == life) ? g_pending_player_damage : 0;
    g_pending_player_damage = 0;
    plat_mutex_unlock(&g_player_health_lock);

    int diff = (player && player == g_hp_player) ? (g_expected_hp_quarters - cur_hp) : 0;
    g_hp_player = player;
    g_expected_hp_quarters = player ? cur_hp : -1;
    if (diff < 0) diff = 0;
    if (!life || (pending <= 0 && diff == 0)) return 0;

    out->took_damage = 1;
    out->hp_before = cur_hp + diff;
    out->raw_damage_quarters = diff + pending;

    if (read_mem(player + ACT_POS_OFF, ppos, sizeof(ppos)) && valid_xyz(ppos)) {
        McTotkActorInfo nearby[32];
        int n = enumerate_actors_impl(ppos, 24.0f, nearby, 32);
        float best_d2 = 1e9f;
        int i, best = -1;
        for (i = 0; i < n; ++i) {
            float dx, dy, dz, d2;
            if (nearby[i].kind != MC_TOTK_ACTOR_ENEMY) continue;
            dx = nearby[i].pos[0] - ppos[0];
            dy = nearby[i].pos[1] - ppos[1];
            dz = nearby[i].pos[2] - ppos[2];
            d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best_d2) {
                best_d2 = d2;
                best = i;
            }
        }
        if (best >= 0) {
            out->attacker_actor = nearby[best].actor;
            memcpy(out->attacker_name, nearby[best].name, sizeof(out->attacker_name));
            memcpy(out->attacker_pos, nearby[best].pos, sizeof(out->attacker_pos));
            out->has_attacker_pos = 1;
        }
    }
    return 1;
}
