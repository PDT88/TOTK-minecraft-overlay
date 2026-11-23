#pragma once

#include "mc_assets.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Shared Immutable Frame Snapshot (mc_engine <-> mc_metal_render)
 * 100% POD, zero pointers, safe to memcpy under mutex via mc_snapshot_copy().
 * Coordinate conventions:
 *   - World space matches TOTK Hyrule meters (+Y up).
 *   - cam_vp[16] is row-major OpenGL View-Projection from cam84[12..27]:
 *       clip_gl = M_row_major * float4(world_pos, 1.0)
 *       Metal depth remap in vertex shader:
 *         out.pos = float4(c.x, c.y, 0.5 * c.z + 0.5 * c.w, c.w)
 *   - Mob/player yaw=0 faces +Z: fwd = (sin(yaw), 0, cos(yaw)), right = (cos(yaw), 0, -sin(yaw)).
 *   - Color uint32_t rgba_tint is packed little-endian RGBA8:
 *       r | (g << 8) | (b << 16) | (a << 24).
 * ========================================================================= */

#define MC_SNAP_VERSION         1u
#define MC_SNAP_MAX_BLOCKS      1024
#define MC_SNAP_MAX_MOBS        48
#define MC_SNAP_MAX_PRIMED_TNT  32
#define MC_SNAP_MAX_DROPPED     64
#define MC_SNAP_MAX_PROJECTILES 64
#define MC_SNAP_MAX_PARTICLES   512
#define MC_SNAP_MAX_EFFECTS     8
#define MC_SNAP_MAX_GUI_QUADS   1024
#define MC_SNAP_MAX_GUI_TEXTS   160

typedef enum {
    MC_VIEW_FIRST_PERSON = 0,
    MC_VIEW_THIRD_BACK   = 1,
    MC_VIEW_THIRD_FRONT  = 2
} McViewMode;

typedef enum {
    MC_BLK_AIR = 0,
    MC_BLK_TNT,
    MC_BLK_OAK_LOG,
    MC_BLK_OAK_PLANKS,
    MC_BLK_GRASS,
    MC_BLK_DIRT,
    MC_BLK_STONE,
    MC_BLK_COBBLESTONE,
    MC_BLK_OBSIDIAN,
    MC_BLK_BEDROCK,
    MC_BLK_TORCH,
    MC_BLK_DIAMOND_BLOCK,
    MC_BLK_GOLD_BLOCK,
    MC_BLK_GLASS,
    MC_BLK_GLOWSTONE,
    MC_BLK_CRAFTING_TABLE,
    MC_BLK_FURNACE,
    MC_BLK_WATER,
    MC_BLK_LAVA,
    MC_BLK_RED_WOOL, MC_BLK_BED_FOOT, MC_BLK_BED_HEAD,
    MC_BLK_SAND,
    MC_BLK_GLASS_PANE,
    MC_BLK_COUNT
} McBlockType;

typedef enum {
    MC_ITM_NONE = 0,
    MC_ITM_COOKED_BEEF,
    MC_ITM_DIAMOND_SWORD,
    MC_ITM_IRON_SWORD,
    MC_ITM_NETHERITE_SWORD,
    MC_ITM_DIAMOND_AXE,
    MC_ITM_DIAMOND_PICKAXE,
    MC_ITM_BOW,
    MC_ITM_CROSSBOW,
    MC_ITM_ARROW,
    MC_ITM_SHIELD,
    MC_ITM_ELYTRA,
    MC_ITM_TOTEM_OF_UNDYING,
    MC_ITM_GOLDEN_APPLE,
    MC_ITM_ENCHANTED_GOLDEN_APPLE,
    MC_ITM_APPLE,
    MC_ITM_BREAD,
    MC_ITM_FIREWORK_ROCKET,
    MC_ITM_FLINT_AND_STEEL,
    MC_ITM_BONE,
    MC_ITM_ENDER_PEARL,
    MC_ITM_WATER_BUCKET,
    MC_ITM_LAVA_BUCKET,
    MC_ITM_EXP_BOTTLE,
    MC_ITM_DIAMOND_HELMET,
    MC_ITM_DIAMOND_CHESTPLATE,
    MC_ITM_DIAMOND_LEGGINGS,
    MC_ITM_DIAMOND_BOOTS,
    MC_ITM_DIAMOND,
    MC_ITM_EMERALD,
    MC_ITM_IRON_INGOT,
    MC_ITM_GOLD_INGOT,
    MC_ITM_REDSTONE,
    MC_ITM_COAL,
    MC_ITM_CHARCOAL,
    MC_ITM_STICK,
    MC_ITM_GUNPOWDER,
    MC_ITM_ROTTEN_FLESH,
    MC_ITM_SPIDER_EYE,
    MC_ITM_STRING,
    MC_ITM_SLIME_BALL,
    MC_ITM_BARRIER,
    /* Placeable block items */
    MC_ITM_TNT,
    MC_ITM_OAK_LOG,
    MC_ITM_OAK_PLANKS,
    MC_ITM_GRASS_BLOCK,
    MC_ITM_DIRT,
    MC_ITM_STONE,
    MC_ITM_COBBLESTONE,
    MC_ITM_OBSIDIAN,
    MC_ITM_BEDROCK,
    MC_ITM_TORCH,
    MC_ITM_DIAMOND_BLOCK,
    MC_ITM_GOLD_BLOCK,
    MC_ITM_GLASS,
    MC_ITM_GLOWSTONE,
    MC_ITM_CRAFTING_TABLE,
    MC_ITM_FURNACE,
    MC_ITM_RED_WOOL, MC_ITM_RED_BED,
    MC_ITM_SAND, MC_ITM_RAW_BEEF, MC_ITM_GLASS_PANE,
    MC_ITM_COUNT
} McItemType;

typedef enum {
    MC_MOB_CREEPER = 0,
    MC_MOB_ZOMBIE,
    MC_MOB_SKELETON,
    MC_MOB_WOLF,
    MC_MOB_COUNT
} McMobType;

typedef enum {
    MC_PROJ_ARROW = 0,
    MC_PROJ_FIREWORK = 1
} McProjType;

typedef enum {
    MC_PART_EXPLOSION = 0,  /* textures/particle/explosion_0..15.png */
    MC_PART_SMOKE,          /* textures/particle/big_smoke_0..11.png */
    MC_PART_FLAME,          /* textures/particle/flame.png */
    MC_PART_CRIT,           /* textures/particle/critical_hit.png */
    MC_PART_SWEEP,          /* textures/particle/sweep_0..7.png */
    MC_PART_GLINT,          /* textures/particle/glint.png */
    MC_PART_GENERIC,        /* textures/particle/generic_0..7.png (totem/eat/splashes) */
    MC_PART_BLOCK_CRUMB     /* colored/textured block break & eat crumbs */
} McParticleType;

typedef enum {
    MC_EFF_NONE = 0,
    MC_EFF_RESISTANCE,
    MC_EFF_FIRE_RESISTANCE,
    MC_EFF_ABSORPTION,
    MC_EFF_REGENERATION,
    MC_EFF_SPEED
} McEffectType;

typedef enum {
    MC_INV_CLOSED = 0,
    MC_INV_SURVIVAL = 1,
    MC_INV_CREATIVE = 2,
    MC_INV_STATION
} McInvMode;

typedef enum {
    MC_CTAB_BLOCKS = 0,
    MC_CTAB_COMBAT = 1,
    MC_CTAB_TOOLS  = 2,
    MC_CTAB_FOOD   = 3,
    MC_CTAB_MISC   = 4,
    MC_CTAB_SEARCH = 5,
    MC_CTAB_SURV_INV = 6,
    MC_CTAB_COUNT  = 7
} McCreativeTab;

typedef enum {
    MC_GUI_QUAD_TEX       = 0,
    MC_GUI_QUAD_SOLID     = 1,
    MC_GUI_QUAD_ISO_BLOCK = 2
} McGuiQuadKind;

typedef enum {
    MC_GUI_FLAG_GLINT      = 1u << 0,
    MC_GUI_FLAG_NINE_SLICE = 1u << 1,
    MC_GUI_FLAG_CROSSHAIR  = 1u << 2
} McGuiQuadFlags;

typedef enum {
    MC_GUI_TEXT_LEFT     = 0,
    MC_GUI_TEXT_CENTER   = 1,
    MC_GUI_TEXT_RIGHT    = 2,
    MC_GUI_TEXT_SHADOW   = 1u << 2,
    MC_GUI_TEXT_OUTLINE4 = 1u << 3
} McGuiTextFlags;

typedef struct {
    int32_t  ix, iy, iz;     /* Logical voxel coordinates */
    float    x, y, z;        /* World center of 1x1x1 block */
    uint16_t block_type;     /* McBlockType */
    uint8_t  light;          /* 0..15 light level */
    int8_t   mining_stage;   /* -1 = none, 0..9 = destroy_stage_0..9 */
    float    tnt_fuse;       /* -1.0f = idle, 0..1 = primed fuse progress */
    uint8_t  face_mask;      /* Bits 0..5 = -X,+X,-Y,+Y,-Z,+Z visible flags (0x3f = all) */
    uint8_t  facing;         /* 0=N(-Z), 1=S(+Z), 2=W(-X), 3=E(+X) */
    uint8_t  flash_white;    /* 1 = white pulse frame */
    uint8_t  _pad;
} McBlockSnap;

typedef struct {
    uint8_t  active;
    uint8_t  kind;           /* McMobType */
    uint8_t  hurt_flash;     /* 1 = red damage overlay */
    uint8_t  fuse_flash;     /* 1 = white Creeper swell flash */
    float    x, y, z;        /* Feet position in world */
    float    yaw, pitch;     /* Radians; yaw=0 faces +Z */
    float    walk_phase;
    float    walk_amp;       /* 0..1 */
    float    swing_arm;      /* 0..1 */
    float    swell;          /* 0..1 Creeper swell */
    float    hp, max_hp;
} McMobSnap;

typedef struct {
    uint8_t  active;
    uint8_t  flash_white;
    uint8_t  _pad[2];
    float    x, y, z;        /* World center */
    float    fuse_progress;  /* 0..1 */
} McPrimedTntSnap;

typedef struct {
    uint8_t  active;
    uint8_t  enchanted;
    uint16_t item_type;      /* McItemType */
    uint8_t  count;
    uint8_t  is_block;       /* 1 if rendered as rotating 3D mini cube */
    uint16_t block_type;     /* McBlockType when is_block */
    float    x, y, z;
    float    spin_yaw;
} McDroppedItemSnap;

typedef struct {
    uint8_t  active;
    uint8_t  kind;           /* McProjType */
    uint8_t  stuck;
    uint8_t  crit;
    float    x, y, z;
    float    vx, vy, vz;
} McProjectileSnap;

typedef struct {
    uint8_t  active;
    uint8_t  kind;           /* McParticleType */
    uint8_t  frame;          /* Animation frame index */
    uint8_t  _pad;
    uint32_t rgba;           /* Tint / crumb color (r | g<<8 | b<<16 | a<<24) */
    float    x, y, z;
    float    size;
    float    age_norm;       /* 0..1 */
} McParticleSnap;

typedef struct {
    uint16_t item_id;        /* McItemType (0 = empty) */
    uint8_t  count;          /* 0..64 */
    uint8_t  enchanted;      /* 1 = purple glint */
    uint8_t  has_durability;
    uint8_t  _pad[3];
    float    durability;     /* 0..1 */
} McItemStack;

typedef struct {
    uint8_t  effect_type;    /* McEffectType */
    uint8_t  amplifier;      /* 0 = I, 1 = II, etc. */
    uint8_t  ambient;
    uint8_t  _pad;
    float    seconds_left;
} McEffectSnap;

typedef struct {
    uint16_t mainhand_item;      /* McItemType (0 = empty Steve arm) */
    uint8_t  mainhand_enchanted;
    uint8_t  bow_pull_stage;     /* 0 = idle, 1..3 = bow_pulling_0..2 */
    float    bow_pull;           /* 0..1 */
    float    bow_raise, bow_release;
    float    attack_swing;       /* 0..1 */
    float    attack_cooldown;    /* 0..1 */
    float    equip_drop;         /* 0..1 */
    float    eat_bob;            /* 0..1 */
    float    walk_bob_x;
    float    walk_bob_y;
    uint16_t offhand_item;       /* McItemType */
    uint8_t  offhand_enchanted;
    uint8_t  _pad;
    float    shield_block;       /* 0..1 */
    float    totem_pop;          /* 0..1 (center-screen Totem activation animation) */
    float    hurt_flash;         /* 0..1 */
} McViewmodelSnap;

typedef struct {
    int32_t  valid;
    float    pos[3];             /* Feet pos */
    float    yaw, pitch;         /* Radians; yaw=0 faces +Z, pitch>0 up */
    float    walk_phase;
    float    walk_amp;           /* 0..1 */
    float    swing_arm;          /* 0..1 */
    float    shield_block;       /* 0..1 */
    float    bow_pull;           /* 0..1 */
    float    bow_raise, bow_release;
    float    elytra_open;        /* 0..1 (0 = folded, 1 = flight wings spread) */
    float    elytra_pitch;       /* Flight path elevation in radians */
    float    elytra_bank;
    uint8_t  crouching;
    uint8_t  hurt_flash;
    uint8_t  has_elytra;
    uint8_t  on_fire;
    uint16_t mainhand_item;
    uint8_t  mainhand_enchanted;
    uint8_t  bow_pull_stage;
    uint16_t offhand_item;
    uint8_t  has_helmet;
    uint8_t  has_chestplate;
    uint8_t  has_leggings;
    uint8_t  has_boots;
    uint8_t  _pad1[2];
} McPlayerSnap;

typedef struct {
    float    x, y, w, h;         /* Virtual GUI space (0..gui_w, 0..gui_h), origin top-left */
    float    u0, v0, u1, v1;     /* Normalized texture UVs [0..1] */
    uint16_t tex_id;             /* McTextureId */
    uint16_t aux;                /* McBlockType for ISO_BLOCK, or border px for NINE_SLICE */
    uint8_t  kind;               /* McGuiQuadKind */
    uint8_t  flags;              /* McGuiQuadFlags */
    uint8_t  layer;              /* Draw order layer 0..7 */
    uint8_t  _pad;
    uint32_t rgba_tint;          /* Packed RGBA8 */
} McGuiQuad;

typedef struct {
    float    x, y;               /* Virtual GUI coords */
    float    scale;              /* 1.0 = 8px Mojangles glyph height */
    uint32_t rgba;               /* Packed RGBA8 */
    uint8_t  flags;              /* McGuiTextFlags */
    uint8_t  layer;              /* Draw order layer 0..7 */
    uint8_t  _pad[2];
    char     text[96];           /* Pure English ASCII string */
} McGuiText;

typedef struct {
    uint32_t version;
    uint64_t tick;
    float    time_sec;

    /* Mode & View */
    int32_t  active;             /* 1 = MC overlay/mod enabled */
    int32_t  mode;               /* 1 = Minecraft Mode, 0 = Zelda Mode */
    int32_t  view_mode;          /* McViewMode: 0=1st, 1=3rd back, 2=3rd front */

    /* Camera (from TOTK cam84) */
    int32_t  cam_valid;
    float    cam_vp[16];         /* Row-major OpenGL VP matrix (cam[12..27]) */
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

    /* Target Block Outline & Hyrule Raycast Hit */
    int32_t  has_target;
    int32_t  target_ix, target_iy, target_iz;
    float    target_x, target_y, target_z;
    int32_t  target_face;        /* 0..5: -X,+X,-Y,+Y,-Z,+Z */
    int32_t  target_mining_stage;/* -1 = none, 0..9 */
    int32_t  has_hyrule_target;
    float    hyrule_hit[3];
    float    hyrule_normal[3];

    /* 3D World Counts & Arrays */
    int32_t  block_count;
    McBlockSnap blocks[MC_SNAP_MAX_BLOCKS];

    int32_t  mob_count;
    McMobSnap mobs[MC_SNAP_MAX_MOBS];

    int32_t  primed_tnt_count;
    McPrimedTntSnap primed_tnt[MC_SNAP_MAX_PRIMED_TNT];

    int32_t  dropped_count;
    McDroppedItemSnap dropped[MC_SNAP_MAX_DROPPED];

    int32_t  projectile_count;
    McProjectileSnap projectiles[MC_SNAP_MAX_PROJECTILES];

    int32_t  particle_count;
    McParticleSnap particles[MC_SNAP_MAX_PARTICLES];

    /* 1st-Person Viewmodel & 3rd-Person Player */
    McViewmodelSnap viewmodel;
    McPlayerSnap    player;

    /* High-level HUD & Inventory State */
    int32_t  hearts_half;        /* 0..20 */
    int32_t  max_hearts_half;    /* 20 */
    int32_t  absorption_half;    /* 0..16 */
    int32_t  hunger;             /* 0..20 */
    int32_t  armor;              /* 0..20 */
    int32_t  xp_level;
    float    xp_prog;            /* 0..1 */
    int32_t  selected_slot;      /* 0..8 */
    McItemStack hotbar[9];
    McItemStack offhand;
    McItemStack armor_slots[4];  /* 0=helmet, 1=chest/elytra, 2=legs, 3=boots */
    McItemStack inv_slots[27];   /* 3x9 main backpack */
    McItemStack craft_slots[4];  /* 2x2 crafting input */
    McItemStack craft_result;

    int32_t  effect_count;
    McEffectSnap active_effects[MC_SNAP_MAX_EFFECTS];

    float    cooldown_prog;      /* 0..1 (1.0 = full) */
    char     tooltip_text[64];   /* Official English en_us held item name */
    float    tooltip_alpha;      /* 0..1 */
    int32_t  crosshair_hit_flash;

    /* Inventory / Creative Picker State */
    int32_t  inv_open;           /* McInvMode */
    int32_t  creative_tab;       /* McCreativeTab */
    float    creative_scroll;    /* 0..1 */
    McItemStack cursor_stack;
    float    cursor_nx;          /* Normalized window cursor X [0..1] */
    float    cursor_ny;          /* Normalized window cursor Y [0..1] */
    char     hover_text[96];
    float    inv_paperdoll_x;    /* Paperdoll rect inside gui_w x gui_h */
    float    inv_paperdoll_y;
    float    inv_paperdoll_w;
    float    inv_paperdoll_h;

    /* Pre-laid-out 2D GUI Draw Commands (virtual gui_w x gui_h space) */
    float    mainhand_scale, offhand_scale;
    float    gui_w;              /* e.g. 480.0f */
    float    gui_h;              /* e.g. 270.0f */
    int32_t  gui_quad_count;
    McGuiQuad gui_quads[MC_SNAP_MAX_GUI_QUADS];
    int32_t  gui_text_count;
    McGuiText gui_texts[MC_SNAP_MAX_GUI_TEXTS];
} McRenderSnapshot;

#ifdef __cplusplus
}
#endif
