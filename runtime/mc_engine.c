#include "mc_engine.h"
#include "mc_assets.h"
#include "mc_audio.h"
#include "mc_totk_bridge.h"
#include "minecraft_overlay.h"
#include "platform/platform.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================================
 * Constants, Keycodes & Colors
 * ========================================================================= */

#define MC_GUI_W 480.0f
#define MC_GUI_H 270.0f

#ifdef _WIN32
/* Win32 virtual keycodes (VK_*) passed by input_win.c */
#define KVK_A          0x41u
#define KVK_S          0x53u
#define KVK_D          0x44u
#define KVK_F          0x46u
#define KVK_H          0x48u
#define KVK_G          0x47u
#define KVK_Z          0x5Au
#define KVK_C          0x43u
#define KVK_V          0x56u
#define KVK_Q          0x51u
#define KVK_W          0x57u
#define KVK_E          0x45u
#define KVK_K          0x4Bu
#define KVK_B          0x42u
#define KVK_N          0x4Eu
#define KVK_M          0x4Du
#define KVK_GRAVE      0xC0u /* VK_OEM_3 (` / ~) */
#define KVK_SPACE      0x20u
#define KVK_BACKSPACE  0x08u
#define KVK_ESCAPE     0x1Bu
#define KVK_SHIFT      0x10u
#define KVK_CONTROL    0x11u
#define KVK_F5         0x74u
#define KVK_F6         0x75u
#else
/* macOS HIToolbox virtual keycodes */
#define KVK_A          0x00u
#define KVK_S          0x01u
#define KVK_D          0x02u
#define KVK_F          0x03u
#define KVK_H          0x04u
#define KVK_G          0x05u
#define KVK_Z          0x06u
#define KVK_C          0x08u
#define KVK_V          0x09u
#define KVK_Q          0x0Cu
#define KVK_W          0x0Du
#define KVK_E          0x0Eu
#define KVK_K          0x28u
#define KVK_B          0x0Bu
#define KVK_N          0x2Du
#define KVK_M          0x2Eu
#define KVK_GRAVE      0x32u
#define KVK_SPACE      0x31u
#define KVK_BACKSPACE  0x33u
#define KVK_ESCAPE     0x35u
#define KVK_SHIFT      0x38u
#define KVK_CONTROL    0x3Bu
#define KVK_F5         0x60u
#define KVK_F6         0x61u
#endif

#define RGBA_PACK(r, g, b, a) \
    ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

#define COL_WHITE        RGBA_PACK(255, 255, 255, 255)
#define COL_SHADOW_DIM   RGBA_PACK(16, 16, 16, 175)
#define COL_DARK_TEXT    RGBA_PACK(64, 64, 64, 255)
#define COL_XP_GREEN     RGBA_PACK(128, 255, 32, 255)
#define COL_AQUA         RGBA_PACK(85, 255, 255, 255)
#define COL_YELLOW       RGBA_PACK(255, 255, 85, 255)
#define COL_GOLD         RGBA_PACK(255, 170, 0, 255)
#define COL_LIGHT_PURPLE RGBA_PACK(255, 85, 255, 255)

#ifdef _WIN32
static const unsigned k_hotbar_keys[9] = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

static const uint8_t k_key_to_ascii[128] = {
    [0x20] = ' ',
    ['0'] = '0', ['1'] = '1', ['2'] = '2', ['3'] = '3', ['4'] = '4',
    ['5'] = '5', ['6'] = '6', ['7'] = '7', ['8'] = '8', ['9'] = '9',
    ['A'] = 'a', ['B'] = 'b', ['C'] = 'c', ['D'] = 'd', ['E'] = 'e',
    ['F'] = 'f', ['G'] = 'g', ['H'] = 'h', ['I'] = 'i', ['J'] = 'j',
    ['K'] = 'k', ['L'] = 'l', ['M'] = 'm', ['N'] = 'n', ['O'] = 'o',
    ['P'] = 'p', ['Q'] = 'q', ['R'] = 'r', ['S'] = 's', ['T'] = 't',
    ['U'] = 'u', ['V'] = 'v', ['W'] = 'w', ['X'] = 'x', ['Y'] = 'y',
    ['Z'] = 'z',
};
#else
static const unsigned k_hotbar_keys[9] = {
    0x12u, 0x13u, 0x14u, 0x15u, 0x17u, 0x16u, 0x1Au, 0x1Cu, 0x19u
};

static const uint8_t k_key_to_ascii[128] = {
    [0x00] = 'a', [0x01] = 's', [0x02] = 'd', [0x03] = 'f', [0x04] = 'h',
    [0x05] = 'g', [0x06] = 'z', [0x07] = 'x', [0x08] = 'c', [0x09] = 'v',
    [0x0B] = 'b', [0x0C] = 'q', [0x0D] = 'w', [0x0E] = 'e', [0x0F] = 'r',
    [0x10] = 'y', [0x11] = 't', [0x1F] = 'o', [0x20] = 'u', [0x22] = 'i',
    [0x23] = 'p', [0x25] = 'l', [0x26] = 'j', [0x28] = 'k', [0x2D] = 'n',
    [0x2E] = 'm', [0x31] = ' '
};
#endif

/* =========================================================================
 * External Input & TOTK Bridge Helpers
 * ========================================================================= */

static inline float mc_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void totk_enemy_world_aabb(const McTotkActorInfo *a, float expand,
                                  float mn[3], float mx[3]) {
    int valid_box = (a->aabb_max[0] > a->aabb_min[0] + 0.05f &&
                     a->aabb_max[1] > a->aabb_min[1] + 0.05f &&
                     a->aabb_max[2] > a->aabb_min[2] + 0.05f &&
                     a->aabb_min[0] <= a->pos[0] + 15.0f &&
                     a->aabb_max[0] >= a->pos[0] - 15.0f &&
                     a->aabb_min[2] <= a->pos[2] + 15.0f &&
                     a->aabb_max[2] >= a->pos[2] - 15.0f &&
                     a->aabb_max[1] >= a->pos[1] - 2.0f &&
                     a->aabb_min[1] <= a->pos[1] + 20.0f);
    if (valid_box) {
        mn[0] = a->aabb_min[0] - expand;
        mn[1] = a->aabb_min[1] - expand;
        mn[2] = a->aabb_min[2] - expand;
        mx[0] = a->aabb_max[0] + expand;
        mx[1] = a->aabb_max[1] + expand;
        mx[2] = a->aabb_max[2] + expand;
    } else {
        float r = (a->radius > 0.35f ? a->radius : 0.9f) + expand;
        mn[0] = a->pos[0] - r; mx[0] = a->pos[0] + r;
        mn[1] = a->pos[1] - expand; mx[1] = a->pos[1] + 2.1f + expand;
        mn[2] = a->pos[2] - r; mx[2] = a->pos[2] + r;
    }
}

static int totk_damage_nearest_enemy(const float orig[3], const float dir[3],
                                     float max_dist, float expand, int allow_cone,
                                     int damage, const float knock_dir[3],
                                     float out_hit[3]) {
    if (damage < 1) damage = 1;
    void *direct = mc_totk_trace_actor(orig, dir, max_dist, out_hit);
    if (direct && mc_totk_damage_actor(direct, damage, knock_dir)) return 1;
    McTotkActorInfo actors[64];
    int n = mc_totk_enumerate_actors(orig, max_dist + 6.0f, actors, 64);
    if (n <= 0) return 0;

    int best_i = -1;
    float best_t = max_dist;
    for (int i = 0; i < n; i++) {
        const McTotkActorInfo *a = &actors[i];
        if (!a->actor || a->kind != MC_TOTK_ACTOR_ENEMY) continue;
        if (a->hp == 0) continue;

        float mn[3], mx[3];
        totk_enemy_world_aabb(a, expand, mn, mx);

        float entry = 0.0f, exit = best_t;
        for (int axis = 0; axis < 3; ++axis) {
            if (fabsf(dir[axis]) < 1e-6f) {
                if (orig[axis] < mn[axis] || orig[axis] > mx[axis]) {
                    entry = INFINITY;
                    break;
                }
                continue;
            }
            float near = (mn[axis] - orig[axis]) / dir[axis];
            float far = (mx[axis] - orig[axis]) / dir[axis];
            entry = fmaxf(entry, fminf(near, far));
            exit = fminf(exit, fmaxf(near, far));
            if (entry > exit) break;
        }
        if (entry <= exit && entry < best_t) {
            best_t = entry;
            best_i = i;
        }
    }

    int used_cone = 0;
    if (best_i < 0 && allow_cone) {
        float best_score = 1e9f;
        for (int i = 0; i < n; i++) {
            const McTotkActorInfo *a = &actors[i];
            if (!a->actor || a->kind != MC_TOTK_ACTOR_ENEMY) continue;
            if (a->hp == 0) continue;

            float mn[3], mx[3];
            totk_enemy_world_aabb(a, 0.0f, mn, mx);
            float cx = 0.5f * (mn[0] + mx[0]);
            float cy = 0.5f * (mn[1] + mx[1]);
            float cz = 0.5f * (mn[2] + mx[2]);
            float vx = cx - orig[0], vy = cy - orig[1], vz = cz - orig[2];
            float dist = sqrtf(vx * vx + vy * vy + vz * vz);
            if (dist < 1e-4f) dist = 1e-4f;
            float eff_r = mc_clampf(0.5f * fmaxf(mx[0] - mn[0], mx[2] - mn[2]), 0.6f, 4.0f);
            float surf_d = fmaxf(0.0f, dist - eff_r);
            if (surf_d > max_dist || fabsf(vy) > 2.6f) continue;

            float dot = (vx * dir[0] + vy * dir[1] + vz * dir[2]) / dist;
            if (dot < (surf_d <= 1.8f ? 0.0f : 0.35f)) continue;
            float score = surf_d * (1.35f - dot);
            if (score < best_score) {
                best_score = score;
                best_t = surf_d;
                best_i = i;
                used_cone = 1;
            }
        }
    }

    if (best_i < 0) return 0;
    const McTotkActorInfo *hit_a = &actors[best_i];
    int applied = mc_totk_damage_actor(hit_a->actor, damage, knock_dir);
    fprintf(stderr, "[mc] hit %s dmg=%d hp_before=%d/%d applied=%d dist=%.2f%s\n",
            hit_a->name[0] ? hit_a->name : "Enemy",
            damage, hit_a->hp, hit_a->max_hp, applied, best_t,
            used_cone ? " (cone)" : "");
    if (!applied) return 0;
    if (out_hit) {
        if (used_cone) {
            float mn[3], mx[3];
            totk_enemy_world_aabb(hit_a, 0.0f, mn, mx);
            out_hit[0] = 0.5f * (mn[0] + mx[0]);
            out_hit[1] = 0.5f * (mn[1] + mx[1]);
            out_hit[2] = 0.5f * (mn[2] + mx[2]);
        } else {
            out_hit[0] = orig[0] + dir[0] * best_t;
            out_hit[1] = orig[1] + dir[1] * best_t;
            out_hit[2] = orig[2] + dir[2] * best_t;
        }
    }
    return 1;
}

static void totk_damage_blast_targets(const float center[3], float radius, int max_damage, int is_tnt) {
    McTotkActorInfo actors[64];
    void *damaged[64];
    int damaged_count = 0;
    int n = mc_totk_enumerate_actors(center, radius + 4.0f, actors, 64);
    for (int i = 0; i < n; i++) {
        const McTotkActorInfo *a = &actors[i];
        int bomb_target = is_tnt && a->kind == MC_TOTK_ACTOR_BOMB_TARGET;
        if (!a->actor || (a->kind != MC_TOTK_ACTOR_ENEMY && !bomb_target)) continue;
        if (a->hp == 0 && !bomb_target) continue;
        float mn[3], mx[3];
        totk_enemy_world_aabb(a, 0.0f, mn, mx);
        float qx = mc_clampf(center[0], mn[0], mx[0]);
        float qy = mc_clampf(center[1], mn[1], mx[1]);
        float qz = mc_clampf(center[2], mn[2], mx[2]);
        float cdx = qx - center[0], cdy = qy - center[1], cdz = qz - center[2];
        float dist = sqrtf(cdx * cdx + cdy * cdy + cdz * cdz);
        if (dist >= radius) continue;
        if (bomb_target) {
            const float dir[3] = {-cdx, -cdy, -cdz};
            mc_totk_blast_object(a->actor, center, dir);
            continue;
        }
        float falloff = 1.0f - dist / radius;
        int dmg = (int)roundf((float)max_damage * falloff);
        if (dmg <= 0) dmg = 1;
        float dx = a->pos[0] - center[0];
        float dz = a->pos[2] - center[2];
        float hlen = sqrtf(dx * dx + dz * dz);
        float imp[3] = {
            hlen > 0.05f ? dx / hlen : 0.0f,
            0.35f,
            hlen > 0.05f ? dz / hlen : 1.0f
        };
        int applied = mc_totk_damage_actor(a->actor, dmg, imp);
        if (applied) damaged[damaged_count++] = a->actor;
        fprintf(stderr, "[mc] tnt hit %s dmg=%d hp_before=%d/%d applied=%d dist=%.2f\n",
                a->name[0] ? a->name : "Enemy", dmg, a->hp, a->max_hp, applied, dist);
    }
    /* Include enemies whose physics bodies are present but whose actors
     * aren't discoverable through the current ActorMgr snapshot. */
    for (int ring = -1; ring <= 1 && damaged_count < 64; ++ring) {
        for (int i = 0; i < 16 && damaged_count < 64; ++i) {
            float angle = (i + 0.5f * ring) * (2.0f * (float)M_PI / 16.0f);
            float direction[3] = {cosf(angle) * (ring ? 0.8f : 1.0f),
                                  ring * 0.6f, sinf(angle) * (ring ? 0.8f : 1.0f)};
            float hit[3];
            void *actor = mc_totk_trace_actor(center, direction, radius, hit);
            if (!actor) continue;
            int seen = 0;
            for (int j = 0; j < damaged_count; ++j) {
                if (damaged[j] == actor) { seen = 1; break; }
            }
            if (seen) continue;
            float dx = hit[0] - center[0], dy = hit[1] - center[1], dz = hit[2] - center[2];
            float distance = sqrtf(dx * dx + dy * dy + dz * dz);
            int damage = (int)roundf(max_damage * fmaxf(0.0f, 1.0f - distance / radius));
            if (mc_totk_damage_actor(actor, damage > 0 ? damage : 1, direction))
                damaged[damaged_count++] = actor;
        }
    }
}

/* =========================================================================
 * Block & Item Registry Tables
 * ========================================================================= */

typedef enum {
    TAB_NONE   = 0,
    TAB_BLOCKS = 1u << 0,
    TAB_COMBAT = 1u << 1,
    TAB_TOOLS  = 1u << 2,
    TAB_FOOD   = 1u << 3,
    TAB_MISC   = 1u << 4
} McItemTabMask;

typedef enum {
    MAT_STONE = 0,
    MAT_WOOD,
    MAT_GRASS,
    MAT_GRAVEL,
    MAT_GLASS
} McMatSound;

typedef struct {
    const char *lang_key;
    const char *fallback;
    float       hardness;
    uint8_t     solid;
    uint8_t     opaque;
    uint8_t     mat;
    uint16_t    drop_item;
} McBlockMeta;

typedef struct {
    const char *lang_key;
    const char *fallback;
    uint16_t    tex_id;
    float       u0, v0, u1, v1;
    uint16_t    place_block;
    uint8_t     max_stack;
    uint8_t     tab_mask;
    uint8_t     default_glint;
    uint8_t     is_food;
    uint8_t     food_hunger;
    float       food_sat;
    float       melee_dmg;
    float       atk_speed; /* seconds cooldown */
    uint8_t     tool_kind; /* 0=none, 1=sword, 2=axe, 3=pickaxe */
} McItemMeta;

static const McBlockMeta k_blocks[MC_BLK_COUNT] = {
    [MC_BLK_AIR]            = {"", "Air", 0.0f, 0, 0, MAT_GRASS, MC_ITM_NONE},
    [MC_BLK_TNT]            = {"block.minecraft.tnt", "TNT", 0.2f, 1, 1, MAT_GRASS, MC_ITM_TNT},
    [MC_BLK_OAK_LOG]        = {"block.minecraft.oak_log", "Oak Log", 1.2f, 1, 1, MAT_WOOD, MC_ITM_OAK_LOG},
    [MC_BLK_OAK_PLANKS]     = {"block.minecraft.oak_planks", "Oak Planks", 1.2f, 1, 1, MAT_WOOD, MC_ITM_OAK_PLANKS},
    [MC_BLK_GRASS]          = {"block.minecraft.grass_block", "Grass Block", 0.6f, 1, 1, MAT_GRASS, MC_ITM_DIRT},
    [MC_BLK_DIRT]           = {"block.minecraft.dirt", "Dirt", 0.5f, 1, 1, MAT_GRAVEL, MC_ITM_DIRT},
    [MC_BLK_STONE]          = {"block.minecraft.stone", "Stone", 1.8f, 1, 1, MAT_STONE, MC_ITM_COBBLESTONE},
    [MC_BLK_COBBLESTONE]    = {"block.minecraft.cobblestone", "Cobblestone", 2.0f, 1, 1, MAT_STONE, MC_ITM_COBBLESTONE},
    [MC_BLK_OBSIDIAN]       = {"block.minecraft.obsidian", "Obsidian", 12.0f, 1, 1, MAT_STONE, MC_ITM_OBSIDIAN},
    [MC_BLK_BEDROCK]        = {"block.minecraft.bedrock", "Bedrock", -1.0f, 1, 1, MAT_STONE, MC_ITM_NONE},
    [MC_BLK_TORCH]          = {"block.minecraft.torch", "Torch", 0.1f, 0, 0, MAT_WOOD, MC_ITM_TORCH},
    [MC_BLK_DIAMOND_BLOCK]  = {"block.minecraft.diamond_block", "Block of Diamond", 3.0f, 1, 1, MAT_STONE, MC_ITM_DIAMOND_BLOCK},
    [MC_BLK_GOLD_BLOCK]     = {"block.minecraft.gold_block", "Block of Gold", 2.5f, 1, 1, MAT_STONE, MC_ITM_GOLD_BLOCK},
    [MC_BLK_GLASS]          = {"block.minecraft.glass", "Glass", 0.4f, 1, 0, MAT_GLASS, MC_ITM_NONE},
    [MC_BLK_GLOWSTONE]      = {"block.minecraft.glowstone", "Glowstone", 0.4f, 1, 0, MAT_GLASS, MC_ITM_GLOWSTONE},
    [MC_BLK_CRAFTING_TABLE] = {"block.minecraft.crafting_table", "Crafting Table", 1.4f, 1, 1, MAT_WOOD, MC_ITM_CRAFTING_TABLE},
    [MC_BLK_FURNACE]        = {"block.minecraft.furnace", "Furnace", 2.5f, 1, 1, MAT_STONE, MC_ITM_FURNACE},
    [MC_BLK_WATER]          = {"block.minecraft.water", "Water", -1.0f, 0, 0, MAT_GRASS, MC_ITM_NONE},
    [MC_BLK_SAND] = {"block.minecraft.sand", "Sand", 0.5f, 1, 1, MAT_GRAVEL, MC_ITM_SAND},
    [MC_BLK_RED_WOOL] = {"block.minecraft.red_wool", "Red Wool", 0.8f, 1, 1, MAT_GRASS, MC_ITM_RED_WOOL},
    [MC_BLK_BED_FOOT] = {"block.minecraft.red_bed", "Red Bed", 0.4f, 1, 0, MAT_WOOD, MC_ITM_RED_BED},
    [MC_BLK_BED_HEAD] = {"block.minecraft.red_bed", "Red Bed", 0.4f, 1, 0, MAT_WOOD, MC_ITM_RED_BED},
    [MC_BLK_LAVA]           = {"block.minecraft.lava", "Lava", -1.0f, 0, 0, MAT_GRASS, MC_ITM_NONE},
    [MC_BLK_GLASS_PANE]     = {"block.minecraft.glass_pane", "Glass Pane", 0.4f, 1, 0, MAT_GLASS, MC_ITM_NONE},
};

#define FULL_UV 0.0f, 0.0f, 1.0f, 1.0f
#define BLK_ITEM(key, fb, tex, blk, tab) \
    {key, fb, tex, FULL_UV, blk, 64, tab, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0}

static const McItemMeta k_items[MC_ITM_COUNT] = {
    [MC_ITM_NONE]                  = {"", "", 0, FULL_UV, 0, 0, 0, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_COOKED_BEEF]           = {"item.minecraft.cooked_beef", "Steak", MC_TEX_ITEM_COOKED_BEEF, FULL_UV, 0, 64, TAB_FOOD, 0, 1, 8, 12.8f, 1.0f, 0.25f, 0},
    [MC_ITM_DIAMOND_SWORD]         = {"item.minecraft.diamond_sword", "Diamond Sword", MC_TEX_ITEM_DIAMOND_SWORD, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 7.0f, 0.625f, 1},
    [MC_ITM_IRON_SWORD]            = {"item.minecraft.iron_sword", "Iron Sword", MC_TEX_ITEM_IRON_SWORD, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 6.0f, 0.625f, 1},
    [MC_ITM_NETHERITE_SWORD]       = {"item.minecraft.netherite_sword", "Netherite Sword", MC_TEX_ITEM_NETHERITE_SWORD, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 8.0f, 0.625f, 1},
    [MC_ITM_DIAMOND_AXE]           = {"item.minecraft.diamond_axe", "Diamond Axe", MC_TEX_ITEM_DIAMOND_AXE, FULL_UV, 0, 1, TAB_TOOLS | TAB_COMBAT, 0, 0, 0, 0.0f, 9.0f, 1.0f, 2},
    [MC_ITM_DIAMOND_PICKAXE]       = {"item.minecraft.diamond_pickaxe", "Diamond Pickaxe", MC_TEX_ITEM_DIAMOND_PICKAXE, FULL_UV, 0, 1, TAB_TOOLS, 0, 0, 0, 0.0f, 5.0f, 0.833f, 3},
    [MC_ITM_BOW]                   = {"item.minecraft.bow", "Bow", MC_TEX_ITEM_BOW, FULL_UV, 0, 1, TAB_COMBAT, 1, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_CROSSBOW]              = {"item.minecraft.crossbow", "Crossbow", MC_TEX_ITEM_CROSSBOW_STANDBY, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_ARROW]                 = {"item.minecraft.arrow", "Arrow", MC_TEX_ITEM_ARROW, FULL_UV, 0, 64, TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_SHIELD]                = {"item.minecraft.shield", "Shield", MC_TEX_ENTITY_SHIELD_NOPATTERN, 1.0f/64.0f, 1.0f/64.0f, 13.0f/64.0f, 23.0f/64.0f, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_ELYTRA]                = {"item.minecraft.elytra", "Elytra", MC_TEX_ITEM_ELYTRA, FULL_UV, 0, 1, TAB_TOOLS | TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_TOTEM_OF_UNDYING]      = {"item.minecraft.totem_of_undying", "Totem of Undying", MC_TEX_ITEM_TOTEM_OF_UNDYING, FULL_UV, 0, 1, TAB_COMBAT | TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_GOLDEN_APPLE]          = {"item.minecraft.golden_apple", "Golden Apple", MC_TEX_ITEM_GOLDEN_APPLE, FULL_UV, 0, 64, TAB_FOOD, 0, 1, 4, 9.6f, 1.0f, 0.25f, 0},
    [MC_ITM_ENCHANTED_GOLDEN_APPLE]= {"item.minecraft.enchanted_golden_apple", "Enchanted Golden Apple", MC_TEX_ITEM_GOLDEN_APPLE, FULL_UV, 0, 64, TAB_FOOD, 1, 1, 4, 9.6f, 1.0f, 0.25f, 0},
    [MC_ITM_APPLE]                 = {"item.minecraft.apple", "Apple", MC_TEX_ITEM_APPLE, FULL_UV, 0, 64, TAB_FOOD, 0, 1, 4, 2.4f, 1.0f, 0.25f, 0},
    [MC_ITM_BREAD]                 = {"item.minecraft.bread", "Bread", MC_TEX_ITEM_BREAD, FULL_UV, 0, 64, TAB_FOOD, 0, 1, 5, 6.0f, 1.0f, 0.25f, 0},
    [MC_ITM_FIREWORK_ROCKET]       = {"item.minecraft.firework_rocket", "Firework Rocket", MC_TEX_ITEM_FIREWORK_ROCKET, FULL_UV, 0, 64, TAB_TOOLS | TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_FLINT_AND_STEEL]       = {"item.minecraft.flint_and_steel", "Flint and Steel", MC_TEX_ITEM_FLINT_AND_STEEL, FULL_UV, 0, 1, TAB_TOOLS | TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_BONE]                  = {"item.minecraft.bone", "Bone", MC_TEX_ITEM_BONE, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_ENDER_PEARL]           = {"item.minecraft.ender_pearl", "Ender Pearl", MC_TEX_ITEM_ENDER_PEARL, FULL_UV, 0, 16, TAB_TOOLS | TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_WATER_BUCKET]          = {"item.minecraft.water_bucket", "Water Bucket", MC_TEX_ITEM_WATER_BUCKET, FULL_UV, 0, 1, TAB_TOOLS, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_LAVA_BUCKET]           = {"item.minecraft.lava_bucket", "Lava Bucket", MC_TEX_ITEM_LAVA_BUCKET, FULL_UV, 0, 1, TAB_TOOLS, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_EXP_BOTTLE]            = {"item.minecraft.experience_bottle", "Bottle o' Enchanting", MC_TEX_ITEM_EXPERIENCE_BOTTLE, FULL_UV, 0, 64, TAB_MISC, 1, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_DIAMOND_HELMET]        = {"item.minecraft.diamond_helmet", "Diamond Helmet", MC_TEX_ITEM_DIAMOND_HELMET, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_DIAMOND_CHESTPLATE]    = {"item.minecraft.diamond_chestplate", "Diamond Chestplate", MC_TEX_ITEM_DIAMOND_CHESTPLATE, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_DIAMOND_LEGGINGS]      = {"item.minecraft.diamond_leggings", "Diamond Leggings", MC_TEX_ITEM_DIAMOND_LEGGINGS, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_DIAMOND_BOOTS]         = {"item.minecraft.diamond_boots", "Diamond Boots", MC_TEX_ITEM_DIAMOND_BOOTS, FULL_UV, 0, 1, TAB_COMBAT, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_DIAMOND]               = {"item.minecraft.diamond", "Diamond", MC_TEX_ITEM_DIAMOND, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_EMERALD]               = {"item.minecraft.emerald", "Emerald", MC_TEX_ITEM_EMERALD, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_IRON_INGOT]            = {"item.minecraft.iron_ingot", "Iron Ingot", MC_TEX_ITEM_IRON_INGOT, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_GOLD_INGOT]            = {"item.minecraft.gold_ingot", "Gold Ingot", MC_TEX_ITEM_GOLD_INGOT, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_REDSTONE]              = {"item.minecraft.redstone", "Redstone Dust", MC_TEX_ITEM_REDSTONE, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_COAL]                  = {"item.minecraft.coal", "Coal", MC_TEX_ITEM_COAL, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_CHARCOAL]              = {"item.minecraft.charcoal", "Charcoal", MC_TEX_ITEM_CHARCOAL, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_STICK]                 = {"item.minecraft.stick", "Stick", MC_TEX_ITEM_STICK, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_GUNPOWDER]             = {"item.minecraft.gunpowder", "Gunpowder", MC_TEX_ITEM_GUNPOWDER, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_ROTTEN_FLESH]          = {"item.minecraft.rotten_flesh", "Rotten Flesh", MC_TEX_ITEM_ROTTEN_FLESH, FULL_UV, 0, 64, TAB_FOOD | TAB_MISC, 0, 1, 4, 0.8f, 1.0f, 0.25f, 0},
    [MC_ITM_SPIDER_EYE]            = {"item.minecraft.spider_eye", "Spider Eye", MC_TEX_ITEM_SPIDER_EYE, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_STRING]                = {"item.minecraft.string", "String", MC_TEX_ITEM_STRING, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_SLIME_BALL]            = {"item.minecraft.slime_ball", "Slimeball", MC_TEX_ITEM_SLIME_BALL, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_BARRIER]               = {"block.minecraft.barrier", "Barrier", MC_TEX_ITEM_BARRIER, FULL_UV, 0, 64, TAB_MISC, 0, 0, 0, 0.0f, 1.0f, 0.25f, 0},
    [MC_ITM_TNT]                   = BLK_ITEM("block.minecraft.tnt", "TNT", MC_TEX_BLOCK_TNT_SIDE, MC_BLK_TNT, TAB_BLOCKS | TAB_COMBAT),
    [MC_ITM_OAK_LOG]               = BLK_ITEM("block.minecraft.oak_log", "Oak Log", MC_TEX_BLOCK_OAK_LOG, MC_BLK_OAK_LOG, TAB_BLOCKS),
    [MC_ITM_OAK_PLANKS]            = BLK_ITEM("block.minecraft.oak_planks", "Oak Planks", MC_TEX_BLOCK_OAK_PLANKS, MC_BLK_OAK_PLANKS, TAB_BLOCKS),
    [MC_ITM_GRASS_BLOCK]           = BLK_ITEM("block.minecraft.grass_block", "Grass Block", MC_TEX_BLOCK_GRASS_SIDE, MC_BLK_GRASS, TAB_BLOCKS),
    [MC_ITM_DIRT]                  = BLK_ITEM("block.minecraft.dirt", "Dirt", MC_TEX_BLOCK_DIRT, MC_BLK_DIRT, TAB_BLOCKS),
    [MC_ITM_STONE]                 = BLK_ITEM("block.minecraft.stone", "Stone", MC_TEX_BLOCK_STONE, MC_BLK_STONE, TAB_BLOCKS),
    [MC_ITM_COBBLESTONE]           = BLK_ITEM("block.minecraft.cobblestone", "Cobblestone", MC_TEX_BLOCK_COBBLESTONE, MC_BLK_COBBLESTONE, TAB_BLOCKS),
    [MC_ITM_OBSIDIAN]              = BLK_ITEM("block.minecraft.obsidian", "Obsidian", MC_TEX_BLOCK_OBSIDIAN, MC_BLK_OBSIDIAN, TAB_BLOCKS),
    [MC_ITM_BEDROCK]               = BLK_ITEM("block.minecraft.bedrock", "Bedrock", MC_TEX_BLOCK_BEDROCK, MC_BLK_BEDROCK, TAB_BLOCKS),
    [MC_ITM_TORCH]                 = BLK_ITEM("block.minecraft.torch", "Torch", MC_TEX_BLOCK_TORCH, MC_BLK_TORCH, TAB_BLOCKS | TAB_MISC),
    [MC_ITM_DIAMOND_BLOCK]         = BLK_ITEM("block.minecraft.diamond_block", "Block of Diamond", MC_TEX_BLOCK_DIAMOND_BLOCK, MC_BLK_DIAMOND_BLOCK, TAB_BLOCKS),
    [MC_ITM_GOLD_BLOCK]            = BLK_ITEM("block.minecraft.gold_block", "Block of Gold", MC_TEX_BLOCK_GOLD_BLOCK, MC_BLK_GOLD_BLOCK, TAB_BLOCKS),
    [MC_ITM_GLASS]                 = BLK_ITEM("block.minecraft.glass", "Glass", MC_TEX_BLOCK_GLASS, MC_BLK_GLASS, TAB_BLOCKS),
    [MC_ITM_GLOWSTONE]             = BLK_ITEM("block.minecraft.glowstone", "Glowstone", MC_TEX_BLOCK_GLOWSTONE, MC_BLK_GLOWSTONE, TAB_BLOCKS),
    [MC_ITM_CRAFTING_TABLE]        = BLK_ITEM("block.minecraft.crafting_table", "Crafting Table", MC_TEX_BLOCK_CRAFTING_TABLE_FRONT, MC_BLK_CRAFTING_TABLE, TAB_BLOCKS),
    [MC_ITM_SAND] = BLK_ITEM("block.minecraft.sand", "Sand", MC_TEX_SAND, MC_BLK_SAND, TAB_BLOCKS),
    [MC_ITM_RAW_BEEF] = {"item.minecraft.beef", "Raw Beef", MC_TEX_BEEF, FULL_UV, 0, 64, TAB_FOOD, 0, 1, 3, 1.8f, 1.0f, 0.25f, 0},
    [MC_ITM_RED_WOOL] = BLK_ITEM("block.minecraft.red_wool", "Red Wool", MC_TEX_RED_WOOL, MC_BLK_RED_WOOL, TAB_BLOCKS),
    [MC_ITM_RED_BED] = BLK_ITEM("block.minecraft.red_bed", "Red Bed", MC_TEX_RED_WOOL, MC_BLK_BED_FOOT, TAB_BLOCKS),
    [MC_ITM_FURNACE]               = BLK_ITEM("block.minecraft.furnace", "Furnace", MC_TEX_BLOCK_FURNACE_FRONT, MC_BLK_FURNACE, TAB_BLOCKS),
    [MC_ITM_GLASS_PANE]            = BLK_ITEM("block.minecraft.glass_pane", "Glass Pane", MC_TEX_BLOCK_GLASS, MC_BLK_GLASS_PANE, TAB_BLOCKS),
};

static const char *item_en_name(uint16_t item_id) {
    if (item_id == 0 || item_id >= MC_ITM_COUNT) return "";
    const McItemMeta *m = &k_items[item_id];
    const char *s = (m->lang_key && m->lang_key[0]) ? mc_asset_en_us(m->lang_key) : NULL;
    return (s && s[0]) ? s : (m->fallback ? m->fallback : "");
}

static const char *lang_str(const char *key, const char *fallback) {
    const char *s = mc_asset_en_us(key);
    return (s && s[0]) ? s : fallback;
}

static uint32_t item_tooltip_color(const McItemStack *st) {
    if (!st || st->item_id == 0) return COL_WHITE;
    if (st->item_id == MC_ITM_ENCHANTED_GOLDEN_APPLE) return COL_LIGHT_PURPLE;
    if (st->item_id == MC_ITM_TOTEM_OF_UNDYING) return COL_YELLOW;
    if (st->enchanted) return COL_AQUA;
    return COL_WHITE;
}

static McItemStack make_stack(uint16_t item_id, int count) {
    McItemStack st;
    memset(&st, 0, sizeof(st));
    if (item_id == 0 || item_id >= MC_ITM_COUNT || count <= 0) return st;
    int mx = k_items[item_id].max_stack;
    if (mx < 1) mx = 1;
    st.item_id = item_id;
    st.count = (uint8_t)(count > mx ? mx : count);
    st.enchanted = k_items[item_id].default_glint;
    st.has_durability = (k_items[item_id].max_stack == 1 &&
        (k_items[item_id].tool_kind > 0 || item_id == MC_ITM_BOW ||
         item_id == MC_ITM_SHIELD || item_id == MC_ITM_ELYTRA ||
         (item_id >= MC_ITM_DIAMOND_HELMET && item_id <= MC_ITM_DIAMOND_BOOTS))) ? 1 : 0;
    st.durability = 1.0f;
    return st;
}

/* =========================================================================
 * Small Math, PRNG & Audio Helpers
 * ========================================================================= */

static inline uint32_t mc_rand(McEngine *eng) {
    uint32_t x = eng->rng ? eng->rng : 0xA5A55A5Au;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    eng->rng = x;
    return x;
}

static inline float mc_randf(McEngine *eng) {
    return (float)(mc_rand(eng) & 0xFFFFFFu) / 16777216.0f;
}

static uint64_t mc_now_ns(void) {
    return plat_now_ns();
}

static void flight_look_direction(const McEngine *eng, float dir[3]) {
    float cp = cosf(eng->look_pitch);
    dir[0] = sinf(eng->look_yaw) * cp;
    dir[1] = sinf(eng->look_pitch);
    dir[2] = cosf(eng->look_yaw) * cp;
}

void mc_engine_first_person_eye(const McEngine *eng, float eye[3]) {
    memcpy(eye, eng->player_pos, sizeof(eng->player_pos));
    float eh = eng->eye_height > 0.1f ? eng->eye_height : 1.62f;
    eye[1] += eh;
}

/* Pure-math row-major 3x4 view matrix builder (thread-safe, zero guest physics calls).
 * Drives direct 1:1 mouse-look in ALL 3 views for both walking and Elytra flight. */
static void build_flight_view_matrix(McEngine *eng, float third_person_dist) {
    float fwd[3];
    flight_look_direction(eng, fwd);
    float sign = eng->view_mode == MC_VIEW_THIRD_FRONT ? -1.0f : 1.0f;
    float yaw = eng->look_yaw + (sign < 0.0f ? (float)M_PI : 0.0f);
    float right[3] = {-cosf(yaw), 0.0f, sinf(yaw)};
    for (int i = 0; i < 3; ++i) fwd[i] *= sign;
    float up[3] = {
        -right[2] * fwd[1],
        right[2] * fwd[0] - right[0] * fwd[2],
        right[0] * fwd[1]
    };

    float eye[3];
    if (eng->view_mode == MC_VIEW_FIRST_PERSON) {
        mc_engine_first_person_eye(eng, eye);
        /* Authentic Minecraft GameRenderer.bobView while walking on ground:
         * pendulum horizontal sway, footstep dip, and gentle roll scaled by camera_shake
         * (zero in flight/air/idle or when camera_shake == 0). */
        float shake = mc_clampf(eng->camera_shake, 0.0f, 1.0f);
        float bob_amp = (eng->elytra_gliding || shake <= 1e-4f) ? 0.0f : (eng->walk_amp * shake);
        if (bob_amp > 1e-4f) {
            float sway = sinf(eng->walk_phase) * bob_amp * 0.045f;
            float dip = -fabsf(cosf(eng->walk_phase)) * bob_amp * 0.055f;
            eye[0] += right[0] * sway + up[0] * dip;
            eye[1] += right[1] * sway + up[1] * dip;
            eye[2] += right[2] * sway + up[2] * dip;

            float roll = sinf(eng->walk_phase) * bob_amp * 0.014f;
            float cr = cosf(roll), sr = sinf(roll);
            float rr[3] = {
                right[0] * cr + up[0] * sr,
                right[1] * cr + up[1] * sr,
                right[2] * cr + up[2] * sr
            };
            float uu[3] = {
                -right[0] * sr + up[0] * cr,
                -right[1] * sr + up[1] * cr,
                -right[2] * sr + up[2] * cr
            };
            memcpy(right, rr, sizeof(right));
            memcpy(up, uu, sizeof(up));
        }
    } else {
        float open = mc_clampf(eng->elytra_open, 0.0f, 1.0f);
        float t = open * open * (3.0f - 2.0f * open);
        float eh = eng->eye_height > 0.1f ? eng->eye_height : 1.62f;
        /* Pivot at vanilla Minecraft eye height (1.62m standing, 1.27m crouch, ~0.65m gliding). */
        float pivot_y = eh + 0.10f * t;
        const float pivot[3] = {eng->player_pos[0], eng->player_pos[1] + pivot_y, eng->player_pos[2]};
        float behind[3] = {-fwd[0], -fwd[1], -fwd[2]};
        float dist = third_person_dist > 0.15f ? third_person_dist : 0.15f;
        eye[0] = pivot[0] + behind[0] * dist;
        eye[1] = pivot[1] + behind[1] * dist;
        eye[2] = pivot[2] + behind[2] * dist;
    }

    float view[3][4] = {
        {right[0], right[1], right[2], 0.0f},
        {up[0],    up[1],    up[2],    0.0f},
        {-fwd[0],  -fwd[1],  -fwd[2],  0.0f}
    };
    for (int row = 0; row < 3; ++row) {
        view[row][3] = -(view[row][0] * eye[0] + view[row][1] * eye[1] + view[row][2] * eye[2]);
    }
    memcpy(eng->flight_view, view, sizeof(view));
    eng->flight_view_ready = 1;
}

void mc_engine_refresh_view(McEngine *eng) {
    if (!eng || !eng->flight_look_active || !eng->player_pos_valid) return;
    float dist = eng->last_third_person_dist > 0.15f
        ? fminf(eng->last_third_person_dist, eng->camera_distance)
        : eng->camera_distance;
    build_flight_view_matrix(eng, dist);
}

static void get_view_origin_dir(const McEngine *eng, float orig[3], float dir[3]) {
    if (eng->flight_look_active) {
        flight_look_direction(eng, dir);
        mc_engine_first_person_eye(eng, orig);
        return;
    }
    int has_dir = 0;
    if (eng->cam_valid) {
        dir[0] = eng->cam_fwd[0];
        dir[1] = eng->cam_fwd[1];
        dir[2] = eng->cam_fwd[2];
        float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (len > 1e-5f) {
            dir[0] /= len; dir[1] /= len; dir[2] /= len;
            has_dir = 1;
        }
    }
    if (!has_dir) {
        float cp = cosf(eng->look_pitch);
        dir[0] = sinf(eng->look_yaw) * cp;
        dir[1] = sinf(eng->look_pitch);
        dir[2] = cosf(eng->look_yaw) * cp;
    }
    if (eng->view_mode == MC_VIEW_FIRST_PERSON && eng->flight_camera_available && eng->player_pos_valid) {
        mc_engine_first_person_eye(eng, orig);
        return;
    }
    if (eng->cam_valid && has_dir) {
        orig[0] = eng->cam_pos[0];
        orig[1] = eng->cam_pos[1];
        orig[2] = eng->cam_pos[2];
        if (eng->player_pos_valid) {
            /* Advance along exact screen crosshair ray to Link's depth without ever dipping below chest height */
            float eye_y = eng->player_pos[1] + (eng->player_crouching ? 1.27f : 1.55f);
            float dx = eng->player_pos[0] - orig[0];
            float dy = eye_y - orig[1];
            float dz = eng->player_pos[2] - orig[2];
            float t_eye = dx * dir[0] + dy * dir[1] + dz * dir[2];
            if (t_eye > 0.25f) {
                float adv = t_eye;
                if (dir[1] < -0.01f) {
                    float min_y = eng->player_pos[1] + 1.25f;
                    if (orig[1] + adv * dir[1] < min_y) {
                        adv = (min_y - orig[1]) / dir[1];
                    }
                }
                adv = mc_clampf(adv, 0.0f, 10.0f);
                orig[0] += dir[0] * adv;
                orig[1] += dir[1] * adv;
                orig[2] += dir[2] * adv;
            }
        }
    } else if (eng->player_pos_valid) {
        orig[0] = eng->player_pos[0];
        orig[1] = eng->player_pos[1] + (eng->player_crouching ? 1.27f : 1.55f);
        orig[2] = eng->player_pos[2];
    } else {
        orig[0] = 0.0f; orig[1] = 1.55f; orig[2] = 0.0f;
    }
}

static void play_sfx_2d(McSfxEvent ev, float gain, float pitch) {
    mc_audio_push(ev, gain, pitch, 0.0f);
}

static void play_sfx_at(const McEngine *eng, McSfxEvent ev, float gain, float pitch, const float pos[3]) {
    if (!eng->cam_valid) {
        mc_audio_push(ev, gain, pitch, 0.0f);
        return;
    }
    mc_audio_push_3d(ev, gain, pitch, eng->cam_pos, eng->cam_right, pos);
}

static McSfxEvent mat_dig_sfx(uint8_t mat) {
    switch (mat) {
    case MAT_WOOD:   return MC_SFX_DIG_WOOD;
    case MAT_GRASS:  return MC_SFX_DIG_GRASS;
    case MAT_GRAVEL: return MC_SFX_DIG_GRAVEL;
    case MAT_GLASS:  return MC_SFX_GLASS_BREAK;
    default:         return MC_SFX_DIG_STONE;
    }
}

static McSfxEvent mat_step_sfx(uint8_t mat) {
    switch (mat) {
    case MAT_WOOD:   return MC_SFX_STEP_WOOD;
    case MAT_GRASS:  return MC_SFX_STEP_GRASS;
    case MAT_GRAVEL: return MC_SFX_STEP_GRAVEL;
    default:         return MC_SFX_STEP_STONE;
    }
}

/* =========================================================================
 * Spawning Helpers (Particles, Drops, TNT, Projectiles, Mobs)
 * ========================================================================= */

static void spawn_particle(McEngine *eng, McParticleType kind, uint8_t frame_count,
                           uint32_t rgba, const float pos[3], const float vel[3],
                           float size, float max_age, float gravity) {
    int slot = -1;
    for (int i = 0; i < MC_SNAP_MAX_PARTICLES; i++) {
        if (!eng->particles[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = (int)(mc_rand(eng) % MC_SNAP_MAX_PARTICLES);
    McParticle *p = &eng->particles[slot];
    memset(p, 0, sizeof(*p));
    p->active = 1;
    p->kind = (uint8_t)kind;
    p->frame_count = frame_count ? frame_count : 1;
    p->rgba = rgba;
    p->x = pos[0]; p->y = pos[1]; p->z = pos[2];
    p->vx = vel[0]; p->vy = vel[1]; p->vz = vel[2];
    p->size = size;
    p->max_age = max_age > 0.05f ? max_age : 0.4f;
    p->gravity = gravity;
}

static void spawn_burst_particles(McEngine *eng, McParticleType kind, uint8_t frames,
                                  uint32_t rgba, const float center[3], int count,
                                  float spread, float speed, float size, float max_age, float grav) {
    for (int i = 0; i < count; i++) {
        float pos[3] = {
            center[0] + (mc_randf(eng) - 0.5f) * spread,
            center[1] + (mc_randf(eng) - 0.5f) * spread,
            center[2] + (mc_randf(eng) - 0.5f) * spread
        };
        float vel[3] = {
            (mc_randf(eng) - 0.5f) * speed * 2.0f,
            mc_randf(eng) * speed * 1.4f,
            (mc_randf(eng) - 0.5f) * speed * 2.0f
        };
        spawn_particle(eng, kind, frames, rgba, pos, vel, size, max_age * (0.7f + 0.6f * mc_randf(eng)), grav);
    }
}

static uint32_t block_crumb_color(uint16_t block_type) {
    switch (block_type) {
    case MC_BLK_TNT:            return RGBA_PACK(210, 48, 36, 255);
    case MC_BLK_OAK_LOG:        return RGBA_PACK(110, 82, 48, 255);
    case MC_BLK_OAK_PLANKS:     return RGBA_PACK(178, 138, 86, 255);
    case MC_BLK_GRASS:          return RGBA_PACK(92, 168, 62, 255);
    case MC_BLK_DIRT:           return RGBA_PACK(134, 96, 67, 255);
    case MC_BLK_OBSIDIAN:       return RGBA_PACK(36, 22, 54, 255);
    case MC_BLK_DIAMOND_BLOCK:  return RGBA_PACK(96, 236, 226, 255);
    case MC_BLK_GOLD_BLOCK:     return RGBA_PACK(246, 208, 62, 255);
    case MC_BLK_GLOWSTONE:      return RGBA_PACK(242, 198, 92, 255);
    case MC_BLK_GLASS:
    case MC_BLK_GLASS_PANE:     return RGBA_PACK(210, 240, 250, 220);
    default:                    return RGBA_PACK(136, 136, 136, 255);
    }
}

static void spawn_dropped_item(McEngine *eng, uint16_t item_id, int count,
                               const float pos[3], const float vel[3]) {
    if (item_id == 0 || item_id >= MC_ITM_COUNT || count <= 0) return;
    int slot = -1;
    for (int i = 0; i < MC_SNAP_MAX_DROPPED; i++) {
        if (!eng->dropped[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    McDroppedItem *d = &eng->dropped[slot];
    memset(d, 0, sizeof(*d));
    d->active = 1;
    d->item_type = item_id;
    d->count = (uint8_t)count;
    d->enchanted = k_items[item_id].default_glint;
    d->x = pos[0]; d->y = pos[1]; d->z = pos[2];
    if (vel) {
        d->vx = vel[0]; d->vy = vel[1]; d->vz = vel[2];
    } else {
        d->vx = (mc_randf(eng) - 0.5f) * 3.0f;
        d->vy = 3.2f;
        d->vz = (mc_randf(eng) - 0.5f) * 3.0f;
    }
    d->pickup_delay = 0.45f;
}

static void spawn_primed_tnt(McEngine *eng, float x, float y, float z, float fuse) {
    int slot = -1;
    for (int i = 0; i < MC_SNAP_MAX_PRIMED_TNT; i++) {
        if (!eng->primed_tnt[i].active) { slot = i; break; }
    }
    if (slot < 0) return;
    McPrimedTnt *t = &eng->primed_tnt[slot];
    memset(t, 0, sizeof(*t));
    t->active = 1;
    t->x = x; t->y = y; t->z = z;
    float a = mc_randf(eng) * 6.283185f;
    t->vx = cosf(a) * 1.1f;
    t->vy = 3.6f;
    t->vz = sinf(a) * 1.1f;
    t->fuse_left = fuse;
    t->fuse_total = fuse;
}

static int find_block_idx(const McEngine *eng, int32_t ix, int32_t iy, int32_t iz) {
    for (int i = 0; i < eng->block_count; i++) {
        if (eng->blocks[i].active &&
            eng->blocks[i].ix == ix &&
            eng->blocks[i].iy == iy &&
            eng->blocks[i].iz == iz) {
            return i;
        }
    }
    return -1;
}

static int bed_dx(int facing) { return (facing == 2) - (facing == 3); }
static int bed_dz(int facing) { return (facing == 0) - (facing == 1); }
static int is_bed(int type) { return type == MC_BLK_BED_HEAD || type == MC_BLK_BED_FOOT; }

static int furnace_input(uint16_t output);

static int set_block_at(McEngine *eng, int32_t ix, int32_t iy, int32_t iz,
                        uint16_t type, uint8_t facing) {
    int idx = find_block_idx(eng, ix, iy, iz);
    eng->base_dirty = 1;
    eng->block_faces_dirty = 1;
    if (type == MC_BLK_AIR) {
        if (idx >= 0 && is_bed(eng->blocks[idx].type)) {
            McWorldBlock b = eng->blocks[idx];
            int sign = b.type == MC_BLK_BED_FOOT ? 1 : -1;
            int pair = find_block_idx(eng, ix + sign * bed_dx(b.facing), iy, iz + sign * bed_dz(b.facing));
            if (pair >= 0 && is_bed(eng->blocks[pair].type)) {
                eng->blocks[pair] = eng->blocks[--eng->block_count];
                idx = find_block_idx(eng, ix, iy, iz);
            }
        }
        if (idx >= 0 && eng->blocks[idx].smelt_output) {
            McWorldBlock *b = &eng->blocks[idx];
            int drop = b->cook_left <= 0 ? b->smelt_output :
                       furnace_input(b->smelt_output);
            float pos[3] = {ix + 0.5f, iy + 0.5f, iz + 0.5f};
            spawn_dropped_item(eng, drop, 1, pos, NULL);
        }
        if (idx >= 0) {
            eng->blocks[idx] = eng->blocks[eng->block_count - 1];
            eng->block_count--;
        }
        return 1;
    }
    if (idx >= 0) {
        eng->blocks[idx].smelt_output = 0;
        eng->blocks[idx].cook_left = 0;
        eng->blocks[idx].fuel_uses = 0;
        eng->blocks[idx].type = type;
        eng->blocks[idx].facing = facing;
        eng->blocks[idx].tnt_fuse = -1.0f;
        return 1;
    }
    if (eng->block_count >= MC_SNAP_MAX_BLOCKS) return 0;
    McWorldBlock *b = &eng->blocks[eng->block_count++];
    memset(b, 0, sizeof(*b));
    b->active = 1;
    b->ix = ix; b->iy = iy; b->iz = iz;
    b->type = type;
    b->light = (type == MC_BLK_GLOWSTONE || type == MC_BLK_TORCH || type == MC_BLK_LAVA) ? 15 : 14;
    b->facing = facing;
    b->tnt_fuse = -1.0f;
    return 1;
}

static void show_tooltip(McEngine *eng, const char *txt) {
    if (!txt || !txt[0]) {
        eng->tooltip_timer = 0.0f;
        eng->tooltip_text[0] = '\0';
        return;
    }
    snprintf(eng->tooltip_text, sizeof(eng->tooltip_text), "%s", txt);
    eng->tooltip_timer = 2.6f;
}

static void recompute_armor(McEngine *eng) {
    int pts = 0;
    if (eng->armor_slots[0].item_id == MC_ITM_DIAMOND_HELMET) pts += 3;
    if (eng->armor_slots[1].item_id == MC_ITM_DIAMOND_CHESTPLATE) pts += 8;
    if (eng->armor_slots[2].item_id == MC_ITM_DIAMOND_LEGGINGS) pts += 6;
    if (eng->armor_slots[3].item_id == MC_ITM_DIAMOND_BOOTS) pts += 3;
    eng->armor = pts;
}

static void grant_effect(McEngine *eng, McEffectType type, uint8_t amp, float secs) {
    for (int i = 0; i < eng->effect_count; i++) {
        if (eng->effects[i].effect_type == type) {
            if (amp >= eng->effects[i].amplifier || secs > eng->effects[i].seconds_left) {
                eng->effects[i].amplifier = amp;
                eng->effects[i].seconds_left = secs;
            }
            return;
        }
    }
    if (eng->effect_count < MC_SNAP_MAX_EFFECTS) {
        McEffectSnap *e = &eng->effects[eng->effect_count++];
        memset(e, 0, sizeof(*e));
        e->effect_type = (uint8_t)type;
        e->amplifier = amp;
        e->seconds_left = secs;
    }
}

static int find_effect_amp(const McEngine *eng, McEffectType type) {
    for (int i = 0; i < eng->effect_count; i++) {
        if (eng->effects[i].effect_type == type) return (int)eng->effects[i].amplifier;
    }
    return -1;
}

static void add_xp(McEngine *eng, int orb_points) {
    float need = (float)(7 + eng->xp_level * 2);
    eng->xp_prog += (float)orb_points / need;
    if (eng->xp_prog >= 1.0f) {
        while (eng->xp_prog >= 1.0f) {
            eng->xp_prog -= 1.0f;
            eng->xp_level++;
        }
        play_sfx_2d(MC_SFX_LEVELUP, 0.8f, 1.0f);
    } else {
        play_sfx_2d(MC_SFX_XP_ORB, 0.5f, 0.85f + 0.3f * mc_randf(eng));
    }
}

/* =========================================================================
 * Inventory Management & 2x2 Crafting
 * ========================================================================= */

static int try_give_stack(McEngine *eng, McItemStack *st) {
    if (!st || st->item_id == 0 || st->count == 0) return 1;
    int max_st = k_items[st->item_id].max_stack;
    if (max_st > 1) {
        for (int i = 0; i < 9 && st->count > 0; i++) {
            McItemStack *dst = &eng->hotbar[i];
            if (dst->item_id == st->item_id && dst->enchanted == st->enchanted && dst->count < max_st) {
                int add = max_st - dst->count;
                if (add > st->count) add = st->count;
                dst->count += (uint8_t)add;
                st->count -= (uint8_t)add;
            }
        }
        for (int i = 0; i < 27 && st->count > 0; i++) {
            McItemStack *dst = &eng->inv_slots[i];
            if (dst->item_id == st->item_id && dst->enchanted == st->enchanted && dst->count < max_st) {
                int add = max_st - dst->count;
                if (add > st->count) add = st->count;
                dst->count += (uint8_t)add;
                st->count -= (uint8_t)add;
            }
        }
    }
    if (!st->count) {
        memset(st, 0, sizeof(*st));
        return 1;
    }
    if (st->count > 0) {
        for (int i = 0; i < 9; i++) {
            if (eng->hotbar[i].item_id == 0) {
                eng->hotbar[i] = *st;
                memset(st, 0, sizeof(*st));
                return 1;
            }
        }
        for (int i = 0; i < 27; i++) {
            if (eng->inv_slots[i].item_id == 0) {
                eng->inv_slots[i] = *st;
                memset(st, 0, sizeof(*st));
                return 1;
            }
        }
    }
    return st->count == 0;
}

static void update_craft_result(McEngine *eng) {
    uint16_t g[4];
    int non_empty = 0;
    uint16_t only_item = 0;
    for (int i = 0; i < 4; i++) {
        g[i] = eng->craft_slots[i].item_id;
        if (g[i]) { non_empty++; only_item = g[i]; }
    }
    memset(&eng->craft_result, 0, sizeof(eng->craft_result));
    if (non_empty == 1 && only_item == MC_ITM_OAK_LOG) {
        eng->craft_result = make_stack(MC_ITM_OAK_PLANKS, 4);
    } else if (non_empty == 4 &&
               g[0] == MC_ITM_OAK_PLANKS && g[1] == MC_ITM_OAK_PLANKS &&
               g[2] == MC_ITM_OAK_PLANKS && g[3] == MC_ITM_OAK_PLANKS) {
        eng->craft_result = make_stack(MC_ITM_CRAFTING_TABLE, 1);
    } else if (non_empty == 2 &&
               ((g[0] == MC_ITM_OAK_PLANKS && g[2] == MC_ITM_OAK_PLANKS) ||
                (g[1] == MC_ITM_OAK_PLANKS && g[3] == MC_ITM_OAK_PLANKS))) {
        eng->craft_result = make_stack(MC_ITM_STICK, 4);
    } else if (non_empty == 2 &&
               (((g[0] == MC_ITM_COAL || g[0] == MC_ITM_CHARCOAL) && g[2] == MC_ITM_STICK) ||
                ((g[1] == MC_ITM_COAL || g[1] == MC_ITM_CHARCOAL) && g[3] == MC_ITM_STICK))) {
        eng->craft_result = make_stack(MC_ITM_TORCH, 4);
    }
}

static void consume_craft_ingredients(McEngine *eng) {
    for (int i = 0; i < 4; i++) {
        if (eng->craft_slots[i].item_id != 0 && eng->craft_slots[i].count > 0) {
            eng->craft_slots[i].count--;
            if (eng->craft_slots[i].count == 0) memset(&eng->craft_slots[i], 0, sizeof(McItemStack));
        }
    }
    update_craft_result(eng);
}

static void toggle_elytra_and_totem(McEngine *eng) {
    if (eng->armor_slots[1].item_id != MC_ITM_ELYTRA) {
        McItemStack prev_chest = eng->armor_slots[1];
        eng->armor_slots[1] = make_stack(MC_ITM_ELYTRA, 1);
        if (prev_chest.item_id != 0) try_give_stack(eng, &prev_chest);

        McItemStack prev_off = eng->offhand;
        eng->offhand = make_stack(MC_ITM_TOTEM_OF_UNDYING, 1);
        if (prev_off.item_id != 0 && prev_off.item_id != MC_ITM_TOTEM_OF_UNDYING) {
            try_give_stack(eng, &prev_off);
        }
        show_tooltip(eng, "Elytra + Totem of Undying");
    } else {
        eng->armor_slots[1] = make_stack(MC_ITM_DIAMOND_CHESTPLATE, 1);
        eng->offhand = make_stack(MC_ITM_SHIELD, 1);
        show_tooltip(eng, "Diamond Chestplate + Shield");
    }
    recompute_armor(eng);
    play_sfx_2d(MC_SFX_UI_CLICK, 0.7f, 1.0f);
}

static void setup_default_loadout(McEngine *eng) {
    memset(eng->hotbar, 0, sizeof(eng->hotbar));
    memset(eng->inv_slots, 0, sizeof(eng->inv_slots));
    memset(eng->armor_slots, 0, sizeof(eng->armor_slots));

    /* Exact Hotbar matching reference frames:
     * Steak x60 | Diamond Sword | Oak Log x6 | Enchanted Bow | Diamond Axe |
     * Enchanted Golden Apple x32 | Diamond Pickaxe | Firework Rocket x64 | TNT x31 */
    eng->hotbar[0] = make_stack(MC_ITM_COOKED_BEEF, 60);
    eng->hotbar[1] = make_stack(MC_ITM_DIAMOND_SWORD, 1);
    eng->hotbar[2] = make_stack(MC_ITM_OAK_LOG, 6);
    eng->hotbar[3] = make_stack(MC_ITM_BOW, 1);
    eng->hotbar[4] = make_stack(MC_ITM_DIAMOND_AXE, 1);
    eng->hotbar[5] = make_stack(MC_ITM_ENCHANTED_GOLDEN_APPLE, 32);
    eng->hotbar[6] = make_stack(MC_ITM_DIAMOND_PICKAXE, 1);
    eng->hotbar[7] = make_stack(MC_ITM_FIREWORK_ROCKET, 64);
    eng->hotbar[8] = make_stack(MC_ITM_TNT, 31);

    eng->offhand = make_stack(MC_ITM_SHIELD, 1);
    eng->armor_slots[0] = make_stack(MC_ITM_DIAMOND_HELMET, 1);
    eng->armor_slots[1] = make_stack(MC_ITM_DIAMOND_CHESTPLATE, 1);
    eng->armor_slots[2] = make_stack(MC_ITM_DIAMOND_LEGGINGS, 1);
    eng->armor_slots[3] = make_stack(MC_ITM_DIAMOND_BOOTS, 1);

    /* Useful extras in main backpack */
    eng->inv_slots[0] = make_stack(MC_ITM_ELYTRA, 1);
    eng->inv_slots[1] = make_stack(MC_ITM_TOTEM_OF_UNDYING, 1);
    eng->inv_slots[2] = make_stack(MC_ITM_FLINT_AND_STEEL, 1);
    eng->inv_slots[3] = make_stack(MC_ITM_ARROW, 64);
    eng->inv_slots[4] = make_stack(MC_ITM_OAK_PLANKS, 64);
    eng->inv_slots[5] = make_stack(MC_ITM_COBBLESTONE, 64);
    eng->inv_slots[6] = make_stack(MC_ITM_OBSIDIAN, 16);
    eng->inv_slots[7] = make_stack(MC_ITM_TORCH, 32);
    eng->inv_slots[8] = make_stack(MC_ITM_EXP_BOTTLE, 16);
    eng->inv_slots[9] = make_stack(MC_ITM_ENDER_PEARL, 16);

    recompute_armor(eng);
    eng->selected_slot = 1;
    show_tooltip(eng, item_en_name(eng->hotbar[eng->selected_slot].item_id));
}

/* =========================================================================
 * Damage, Knockback & Totem of Undying
 * ========================================================================= */

static void update_player_hearts(McEngine *eng, int hp) {
    eng->totk_hp_q = hp;
    int capacity = eng->totk_max_hp_q;
    int visible_hp = hp > capacity ? capacity : hp;
    eng->hearts_half = (visible_hp * eng->max_hearts_half + capacity - 1) / capacity;
}

static void write_player_health(McEngine *eng, int hp) {
    if (hp < 0) hp = 0;
    if (hp > eng->totk_max_hp_q) hp = eng->totk_max_hp_q;
    if (hp == eng->totk_hp_q || mc_totk_set_player_hp(hp)) {
        update_player_hearts(eng, hp);
    }
}

static void apply_player_damage(McEngine *eng, int raw_quarters, int hp_before,
                                int can_block, const float attacker_pos[3]) {
    if (raw_quarters <= 0 || hp_before <= 0) return;
    if (eng->invuln_timer > 0.0f) {
        write_player_health(eng, hp_before);
        return;
    }

    if (can_block && eng->shield_block > 0.45f) {
        if (attacker_pos && eng->player_pos_valid) {
            float to_atk[3] = {
                attacker_pos[0] - eng->player_pos[0],
                0.0f,
                attacker_pos[2] - eng->player_pos[2]
            };
            float dummy_o[3], fwd[3];
            get_view_origin_dir(eng, dummy_o, fwd);
            float dot = to_atk[0] * fwd[0] + to_atk[2] * fwd[2];
            if (dot >= -0.25f) {
                play_sfx_2d(MC_SFX_SHIELD_BLOCK, 0.9f, 0.95f + 0.1f * mc_randf(eng));
                write_player_health(eng, hp_before);
                return;
            }
        } else {
            play_sfx_2d(MC_SFX_SHIELD_BLOCK, 0.9f, 1.0f);
            write_player_health(eng, hp_before);
            return;
        }
    }

    float dmg = (float)raw_quarters;
    /* Armor reduction (roughly vanilla: up to 60%) */
    float armor_red = (float)eng->armor * 0.03f;
    dmg *= (1.0f - mc_clampf(armor_red, 0.0f, 0.65f));
    int res_amp = find_effect_amp(eng, MC_EFF_RESISTANCE);
    if (res_amp >= 0) dmg *= mc_clampf(1.0f - 0.20f * (float)(res_amp + 1), 0.1f, 1.0f);

    int dmg_i = (int)ceilf(dmg);
    if (dmg_i < 1) dmg_i = 1;

    if (eng->absorption_half > 0) {
        int available = eng->absorption_half * 2;
        int absorb = dmg_i < available ? dmg_i : available;
        eng->absorption_half -= (absorb + 1) / 2;
        dmg_i -= absorb;
    }

    int hp_after = hp_before - dmg_i;
    eng->hurt_flash_timer = 0.45f;
    eng->invuln_timer = 0.5f;

    if (hp_after <= 0) {
        int triggered_totem = 0;
        if (eng->offhand.item_id == MC_ITM_TOTEM_OF_UNDYING) {
            eng->offhand.count--;
            if (eng->offhand.count == 0) memset(&eng->offhand, 0, sizeof(eng->offhand));
            triggered_totem = 1;
        } else if (eng->hotbar[eng->selected_slot].item_id == MC_ITM_TOTEM_OF_UNDYING) {
            eng->hotbar[eng->selected_slot].count--;
            if (eng->hotbar[eng->selected_slot].count == 0) {
                memset(&eng->hotbar[eng->selected_slot], 0, sizeof(McItemStack));
            }
            triggered_totem = 1;
        }

        if (triggered_totem) {
            eng->absorption_half = 4;
            eng->totem_pop_timer = 1.6f;
            eng->hurt_flash_timer = 0.0f;
            grant_effect(eng, MC_EFF_REGENERATION, 1, 45.0f);
            grant_effect(eng, MC_EFF_ABSORPTION, 1, 5.0f);
            grant_effect(eng, MC_EFF_FIRE_RESISTANCE, 0, 40.0f);
            play_sfx_2d(MC_SFX_TOTEM_USE, 1.0f, 1.0f);

            float pcenter[3] = {
                eng->player_pos[0], eng->player_pos[1] + 1.2f, eng->player_pos[2]
            };
            spawn_burst_particles(eng, MC_PART_GENERIC, 8, RGBA_PACK(255, 224, 60, 255),
                                  pcenter, 22, 0.8f, 3.8f, 0.24f, 1.1f, 3.0f);
            spawn_burst_particles(eng, MC_PART_GENERIC, 8, RGBA_PACK(80, 255, 96, 255),
                                  pcenter, 22, 0.8f, 3.5f, 0.24f, 1.1f, 3.0f);
            write_player_health(eng, 4);
            show_tooltip(eng, item_en_name(MC_ITM_TOTEM_OF_UNDYING));
            return;
        }
    }
    write_player_health(eng, hp_after);
    play_sfx_2d(MC_SFX_HURT, 0.85f, 0.92f + 0.16f * mc_randf(eng));
}

/* =========================================================================
 * Explosions & Mobs
 * ========================================================================= */

static void trigger_explosion(McEngine *eng, const float center[3], float radius, float max_dmg, int is_tnt) {
    play_sfx_at(eng, MC_SFX_EXPLODE, 1.35f, 0.90f + 0.20f * mc_randf(eng), center);
    spawn_burst_particles(eng, MC_PART_EXPLOSION, 16, COL_WHITE, center, 18,
                          radius * 0.7f, 1.2f, 1.35f, 0.45f, -0.4f);
    spawn_burst_particles(eng, MC_PART_SMOKE, 12, RGBA_PACK(210, 210, 210, 230), center, 24,
                          radius * 0.8f, 3.8f, 0.9f, 0.95f, -0.8f);

    /* Carve placed MC blocks (and chain-ignite nearby TNT) */
    int32_t minx = (int32_t)floorf(center[0] - radius);
    int32_t maxx = (int32_t)floorf(center[0] + radius);
    int32_t miny = (int32_t)floorf(center[1] - radius);
    int32_t maxy = (int32_t)floorf(center[1] + radius);
    int32_t minz = (int32_t)floorf(center[2] - radius);
    int32_t maxz = (int32_t)floorf(center[2] + radius);

    for (int i = eng->block_count - 1; i >= 0; i--) {
        if (i >= eng->block_count) continue;
        McWorldBlock *b = &eng->blocks[i];
        if (!b->active) continue;
        if (b->ix < minx || b->ix > maxx || b->iy < miny || b->iy > maxy || b->iz < minz || b->iz > maxz) {
            continue;
        }
        float dx = (float)b->ix + 0.5f - center[0];
        float dy = (float)b->iy + 0.5f - center[1];
        float dz = (float)b->iz + 0.5f - center[2];
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > radius * radius) continue;

        uint16_t bt = b->type;
        if (bt == MC_BLK_BEDROCK || bt == MC_BLK_OBSIDIAN) continue;
        if (bt == MC_BLK_TNT) {
            float tx = (float)b->ix + 0.5f, ty = (float)b->iy + 0.5f, tz = (float)b->iz + 0.5f;
            set_block_at(eng, b->ix, b->iy, b->iz, MC_BLK_AIR, 0);
            spawn_primed_tnt(eng, tx, ty, tz, 0.45f + 0.45f * mc_randf(eng));
            continue;
        }
        if ((mc_rand(eng) % 3u) == 0u && k_blocks[bt].drop_item != MC_ITM_NONE) {
            float p[3] = {(float)b->ix + 0.5f, (float)b->iy + 0.5f, (float)b->iz + 0.5f};
            spawn_dropped_item(eng, k_blocks[bt].drop_item, 1, p, NULL);
        }
        set_block_at(eng, b->ix, b->iy, b->iz, MC_BLK_AIR, 0);
    }

    /* Damage MC mobs */
    for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
        McMob *m = &eng->mobs[i];
        if (!m->active) continue;
        float dx = m->x - center[0];
        float dy = (m->y + 0.9f) - center[1];
        float dz = m->z - center[2];
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist >= radius * 1.4f) continue;
        float falloff = 1.0f - dist / (radius * 1.4f);
        m->hp -= max_dmg * falloff;
        m->hurt_timer = 0.4f;
        if (dist > 0.05f) {
            m->vx += (dx / dist) * 9.0f * falloff;
            m->vy += 5.5f * falloff;
            m->vz += (dz / dist) * 9.0f * falloff;
        }
        if (m->hp <= 0.0f) {
            m->active = 0;
            add_xp(eng, 5);
        }
    }

    totk_damage_blast_targets(center, radius * 1.6f, (int)(max_dmg * 4.5f), is_tnt);

    /* Damage Player */
    if (eng->player_pos_valid) {
        float dx = eng->player_pos[0] - center[0];
        float dy = (eng->player_pos[1] + 0.9f) - center[1];
        float dz = eng->player_pos[2] - center[2];
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < radius * 1.35f) {
            float falloff = 1.0f - dist / (radius * 1.35f);
            int dmg = (int)roundf(max_dmg * falloff);
            if (dmg > 0) apply_player_damage(eng, dmg * 2, eng->totk_hp_q, 0, NULL);
        }
    }
}

static McMob *spawn_mob(McEngine *eng, McMobType kind, float x, float y, float z) {
    int slot = -1;
    for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
        if (!eng->mobs[i].active) { slot = i; break; }
    }
    if (slot < 0) return NULL;
    McMob *m = &eng->mobs[slot];
    memset(m, 0, sizeof(*m));
    m->active = 1;
    m->kind = (uint8_t)kind;
    m->x = x; m->y = y; m->z = z;
    m->hp = 20.0f;
    m->max_hp = 20.0f;
    m->say_timer = 2.0f + 5.0f * mc_randf(eng);
    m->strafe_dir = (mc_rand(eng) & 1u) ? 1.0f : -1.0f;
    return m;
}

static void kill_mob(McEngine *eng, McMob *m) {
    if (m->kind == MC_MOB_WOLF) { m->active=0; eng->base_dirty=1; return; }
    float p[3] = {m->x, m->y + 0.9f, m->z};
    McSfxEvent d_ev = (m->kind == MC_MOB_CREEPER) ? MC_SFX_CREEPER_DEATH
                    : (m->kind == MC_MOB_ZOMBIE) ? MC_SFX_ZOMBIE_DEATH : MC_SFX_SKELETON_DEATH;
    play_sfx_at(eng, d_ev, 0.9f, 0.95f + 0.1f * mc_randf(eng), p);
    uint16_t drop = (m->kind == MC_MOB_CREEPER) ? MC_ITM_GUNPOWDER
                  : (m->kind == MC_MOB_ZOMBIE) ? MC_ITM_ROTTEN_FLESH : MC_ITM_BONE;
    spawn_dropped_item(eng, drop, 1 + (int)(mc_rand(eng) % 2u), p, NULL);
    if (m->kind == MC_MOB_SKELETON) spawn_dropped_item(eng, MC_ITM_ARROW, 1 + (int)(mc_rand(eng) % 2u), p, NULL);
    spawn_burst_particles(eng, MC_PART_SMOKE, 12, RGBA_PACK(230, 230, 230, 220), p, 10, 0.6f, 1.5f, 0.6f, 0.7f, -0.3f);
    m->active = 0;
    add_xp(eng, 5);
}

/* =========================================================================
 * Raycasting & Block Collision
 * ========================================================================= */

static int raycast_blocks(const McEngine *eng, const float orig[3], const float dir[3],
                          float max_dist, int32_t *out_ix, int32_t *out_iy, int32_t *out_iz,
                          int *out_face, float *out_dist) {
    float best_t = max_dist;
    int hit = 0;
    float invx = fabsf(dir[0]) > 1e-6f ? 1.0f / dir[0] : 1e30f;
    float invy = fabsf(dir[1]) > 1e-6f ? 1.0f / dir[1] : 1e30f;
    float invz = fabsf(dir[2]) > 1e-6f ? 1.0f / dir[2] : 1e30f;

    for (int i = 0; i < eng->block_count; i++) {
        const McWorldBlock *b = &eng->blocks[i];
        if (!b->active) continue;
        float bx0 = (float)b->ix, by0 = (float)b->iy, bz0 = (float)b->iz;
        float t1 = (bx0 - orig[0]) * invx, t2 = (bx0 + 1.0f - orig[0]) * invx;
        float height = is_bed(b->type) ? 0.5625f : 1.0f;
        float t3 = (by0 - orig[1]) * invy, t4 = (by0 + height - orig[1]) * invy;
        float t5 = (bz0 - orig[2]) * invz, t6 = (bz0 + 1.0f - orig[2]) * invz;

        float tminx = t1 < t2 ? t1 : t2, tmaxx = t1 > t2 ? t1 : t2;
        float tminy = t3 < t4 ? t3 : t4, tmaxy = t3 > t4 ? t3 : t4;
        float tminz = t5 < t6 ? t5 : t6, tmaxz = t5 > t6 ? t5 : t6;

        float tmin = tminx; int face = (dir[0] > 0.0f) ? 0 : 1;
        if (tminy > tmin) { tmin = tminy; face = (dir[1] > 0.0f) ? 2 : 3; }
        if (tminz > tmin) { tmin = tminz; face = (dir[2] > 0.0f) ? 4 : 5; }
        float tmax = tmaxx < tmaxy ? tmaxx : tmaxy;
        if (tmaxz < tmax) tmax = tmaxz;

        if (tmax >= tmin && tmin >= 0.02f && tmin < best_t) {
            best_t = tmin;
            *out_ix = b->ix; *out_iy = b->iy; *out_iz = b->iz;
            *out_face = face;
            *out_dist = best_t;
            hit = 1;
        }
    }
    return hit;
}

static int find_hit_mob(const McEngine *eng, const float orig[3], const float dir[3],
                        float max_dist, float expand, float *out_dist) {
    int best = -1;
    float best_t = max_dist;
    for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
        const McMob *m = &eng->mobs[i];
        if (!m->active) continue;
        float hw = 0.42f + expand;
        float h = 1.85f + expand;
        float px = m->x - orig[0], py = (m->y + h * 0.5f) - orig[1], pz = m->z - orig[2];
        float proj = px * dir[0] + py * dir[1] + pz * dir[2];
        if (proj < 0.0f || proj > best_t) continue;
        float rx = orig[0] + dir[0] * proj - m->x;
        float ry = orig[1] + dir[1] * proj - (m->y + h * 0.5f);
        float rz = orig[2] + dir[2] * proj - m->z;
        if (rx * rx + rz * rz <= hw * hw && fabsf(ry) <= h * 0.5f + hw * 0.4f) {
            best_t = proj;
            best = i;
        }
    }
    if (best >= 0 && out_dist) *out_dist = best_t;
    return best;
}

static float probe_ground_surface_raw(const McEngine *eng, float x, float hyrule_y_top,
                                      float mc_y_top, float z,
                                      int *out_is_mc, uint8_t *out_mat, float *out_norm_y) {
    float best_y = -10000.0f;
    float best_ny = 0.0f;
    if (out_is_mc) *out_is_mc = 0;
    if (out_mat) *out_mat = MAT_GRASS;
    if (out_norm_y) *out_norm_y = 0.0f;

    float hy = -10000.0f, hny = 1.0f;
    if (mc_totk_ground_height_with_normal(x, hyrule_y_top, z, &hy, &hny)) {
        if (hy > hyrule_y_top + 0.25f) {
            float hy2 = -10000.0f, hny2 = 1.0f;
            if (mc_totk_ground_height_with_normal(x, hyrule_y_top - 1.4f, z, &hy2, &hny2) &&
                hy2 <= hyrule_y_top + 0.25f) {
                hy = hy2;
                hny = hny2;
            }
        }
        best_y = hy;
        best_ny = hny;
    }

    int32_t ix = (int32_t)floorf(x), iz = (int32_t)floorf(z);
    for (int i = 0; i < eng->block_count; i++) {
        const McWorldBlock *b = &eng->blocks[i];
        if (!b->active || b->ix != ix || b->iz != iz) continue;
        if (b->type >= MC_BLK_COUNT || !k_blocks[b->type].solid) continue;
        float top_y = b->iy + (is_bed(b->type) ? 0.5625f : 1.0f);
        if (top_y <= mc_y_top && top_y > best_y) {
            best_y = top_y;
            best_ny = 1.0f;
            if (out_is_mc) *out_is_mc = 1;
            if (out_mat) *out_mat = k_blocks[b->type].mat;
        }
    }
    if (out_norm_y) *out_norm_y = best_ny;
    return best_y;
}

static float probe_ground_surface_ex(const McEngine *eng, float x, float y_top, float z,
                                     int *out_is_mc, uint8_t *out_mat, float *out_norm_y) {
    return probe_ground_surface_raw(eng, x, y_top, y_top + 0.05f, z,
                                    out_is_mc, out_mat, out_norm_y);
}

static float probe_ground_surface(const McEngine *eng, float x, float y_top, float z,
                                  int *out_is_mc, uint8_t *out_mat) {
    return probe_ground_surface_raw(eng, x, y_top + 1.0f, y_top + 0.65f, z,
                                    out_is_mc, out_mat, NULL);
}

/* Resolve the top face before walls so that a descending player cannot be
 * ejected sideways during the frame their feet cross a platform. */
static float player_block_floor(const McEngine *eng, float previous_y, int staying_supported, uint8_t *material) {
    const float *p = eng->player_pos;
    const float hw = 0.28f;
    float floor_y = -10000.0f;
    if (!staying_supported && p[1] > previous_y + 0.005f) return floor_y;
    const float min_top = staying_supported ? previous_y - 0.001f : p[1] - 0.025f;
    for (int i = 0; i < eng->block_count; i++) {
        const McWorldBlock *b = &eng->blocks[i];
        if (!b->active || b->type >= MC_BLK_COUNT || !k_blocks[b->type].solid) continue;
        if (p[0]+hw <= b->ix || p[0]-hw >= b->ix+1 ||
            p[2]+hw <= b->iz || p[2]-hw >= b->iz+1) continue;
        float top = b->iy + (is_bed(b->type) ? 0.5625f : 1.0f);
        if (top > previous_y + 0.04f || top < min_top || top <= floor_y) continue;
        floor_y = top;
        *material = k_blocks[b->type].mat;
    }
    return floor_y;
}

/* Use the same camera-relative movement on platforms and during jump ascent. */
static void set_input_walk_velocity(McEngine *eng) {
    float right = eng->move_right, forward = eng->move_forward;
    float scale = eng->inv_open == MC_INV_CLOSED
        ? 4.317f / fmaxf(1.0f, hypotf(right, forward)) : 0.0f;
    float yaw = eng->flight_look_active
        ? eng->look_yaw
        : (eng->cam_valid ? atan2f(eng->cam_fwd[0], eng->cam_fwd[2]) : eng->look_yaw);
    float s = sinf(yaw), c = cosf(yaw);
    eng->player_vel[0] = scale * (forward * s - right * c);
    eng->player_vel[2] = scale * (forward * c + right * s);
}

/* Push out of the side face and remove only motion into that face.
 * The caller restores the remaining velocity after force-setting position. */
static int resolve_player_block_walls(McEngine *eng) {
    float px = eng->player_pos[0], py = eng->player_pos[1], pz = eng->player_pos[2];
    const float hw = 0.28f;
    const float clearance = 0.002f;
    int pushed = 0;
    for (int i = 0; i < eng->block_count; i++) {
        const McWorldBlock *b = &eng->blocks[i];
        if (!b->active || b->type >= MC_BLK_COUNT || !k_blocks[b->type].solid) continue;
        if (b->iy + (is_bed(b->type) ? 0.5625f : 1.0f) <= py + 0.001f || (float)b->iy >= py + 1.68f) continue;
        float ox0 = (px + hw) - (float)b->ix, ox1 = (float)(b->ix + 1) - (px - hw);
        float oz0 = (pz + hw) - (float)b->iz, oz1 = (float)(b->iz + 1) - (pz - hw);
        if (ox0 <= 0.0f || ox1 <= 0.0f || oz0 <= 0.0f || oz1 <= 0.0f) continue;
        float min_o = ox0; int axis = 0;
        if (ox1 < min_o) { min_o = ox1; axis = 1; }
        if (oz0 < min_o) { min_o = oz0; axis = 2; }
        if (oz1 < min_o) { min_o = oz1; axis = 3; }
        const int coord = axis < 2 ? 0 : 2;
        const float outward = axis % 2 == 0 ? -1.0f : 1.0f;
        if (coord == 0) px += outward * (min_o + clearance);
        else pz += outward * (min_o + clearance);
        if (eng->player_vel[coord] * outward < 0.0f) eng->player_vel[coord] = 0.0f;
        pushed = 1;
    }
    eng->player_pos[0] = px;
    eng->player_pos[2] = pz;
    return pushed;
}

static int mob_has_los(const McEngine *eng, const float a[3], const float b[3]) {
    float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist < 0.05f) return 1;
    float dir[3] = {dx / dist, dy / dist, dz / dist};

    for (int i = 0; i < eng->block_count; i++) {
        const McWorldBlock *blk = &eng->blocks[i];
        if (!blk->active || blk->type >= MC_BLK_COUNT || !k_blocks[blk->type].solid) continue;
        float tmin = 0.02f, tmax = dist - 0.05f;
        float bmin[3] = {(float)blk->ix, (float)blk->iy, (float)blk->iz};
        int hit = 1;
        for (int ax = 0; ax < 3; ax++) {
            if (fabsf(dir[ax]) < 1e-6f) {
                if (a[ax] < bmin[ax] || a[ax] > bmin[ax] + 1.0f) { hit = 0; break; }
            } else {
                float inv = 1.0f / dir[ax];
                float t0 = (bmin[ax] - a[ax]) * inv;
                float t1 = (bmin[ax] + 1.0f - a[ax]) * inv;
                if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
                if (t0 > tmin) tmin = t0;
                if (t1 < tmax) tmax = t1;
                if (tmin > tmax) { hit = 0; break; }
            }
        }
        if (hit) return 0;
    }

    if (dist > 0.12f) {
        float hit[3], nrm[3], hd = dist;
        float start[3] = {a[0] + dir[0] * 0.05f, a[1] + dir[1] * 0.05f, a[2] + dir[2] * 0.05f};
        if (mc_totk_raycast(start, dir, dist - 0.10f, hit, nrm, &hd) && hd < dist - 0.15f) {
            return 0;
        }
    }
    return 1;
}

static int aabb_overlaps_solid_blocks(const McEngine *eng, float x, float y, float z,
                                      float half_w, float height) {
    int32_t x0 = (int32_t)floorf(x - half_w), x1 = (int32_t)floorf(x + half_w);
    int32_t y0 = (int32_t)floorf(y + 0.05f), y1 = (int32_t)floorf(y + height - 0.05f);
    int32_t z0 = (int32_t)floorf(z - half_w), z1 = (int32_t)floorf(z + half_w);
    for (int32_t iy = y0; iy <= y1; iy++) {
        for (int32_t ix = x0; ix <= x1; ix++) {
            for (int32_t iz = z0; iz <= z1; iz++) {
                int idx = find_block_idx(eng, ix, iy, iz);
                if (idx < 0) continue;
                int type = eng->blocks[idx].type;
                if (k_blocks[type].solid && (!is_bed(type) || y < iy + 0.5625f)) return 1;
            }
        }
    }
    return 0;
}

static int probe_mob_headroom(const McEngine *eng, float x, float gy, float z) {
    if (aabb_overlaps_solid_blocks(eng, x, gy + 0.05f, z, 0.34f, 1.85f)) {
        return 0;
    }
    static const float up[3] = {0.0f, 1.0f, 0.0f};
    float hit[3], nrm[3], hd = 2.0f;
    float orig[3] = {x, gy + 0.35f, z};
    if (mc_totk_raycast(orig, up, 1.65f, hit, nrm, &hd) && hd < 1.55f) {
        return 0;
    }
    return 1;
}

/* =========================================================================
 * Interactions: Combat, Bow, Food, Block Place/Mine, TNT, Rockets
 * ========================================================================= */

static void get_attack_aim(const McEngine *eng, float orig[3], float dir[3]) {
    get_view_origin_dir(eng, orig, dir);
    if (eng->view_mode != MC_VIEW_THIRD_FRONT || !eng->player_pos_valid || eng->flight_look_active) return;

    /* The front camera looks back at the avatar. Attack from the avatar
     * toward the world it faces, rather than toward the camera crosshair. */
    mc_engine_first_person_eye(eng, orig);
    dir[0] = sinf(eng->player_totk_yaw);
    dir[1] = 0.0f;
    dir[2] = cosf(eng->player_totk_yaw);
}

static void fire_bow_arrow(McEngine *eng, float charge_sec) {
    float orig[3], dir[3];
    get_attack_aim(eng, orig, dir);

    float pull = mc_clampf(charge_sec / 1.0f, 0.1f, 1.0f);
    int slot = -1;
    for (int i = 0; i < MC_SNAP_MAX_PROJECTILES; i++) {
        if (!eng->projectiles[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    float speed = 12.0f + 26.0f * pull;
    McProjectile *p = &eng->projectiles[slot];
    memset(p, 0, sizeof(*p));
    p->active = 1;
    p->kind = MC_PROJ_ARROW;
    p->from_player = 1;
    p->crit = pull >= 0.92f ? 1 : 0;
    p->x = orig[0] + dir[0] * 0.65f;
    p->y = orig[1] + dir[1] * 0.65f - 0.05f;
    p->z = orig[2] + dir[2] * 0.65f;
    p->vx = dir[0] * speed;
    p->vy = dir[1] * speed + 0.4f;
    p->vz = dir[2] * speed;
    p->max_age = 12.0f;
    p->power = 4.0f + 7.0f * pull;
    if (eng->hotbar[eng->selected_slot].item_id == MC_ITM_BOW) eng->bow_release = 1.0f;
    play_sfx_2d(MC_SFX_BOW_SHOOT, 0.9f, 0.95f + 0.2f * pull);
}

static void launch_firework_rocket(McEngine *eng, const float pos[3], const float dir[3]) {
    int slot = -1;
    for (int i = 0; i < MC_SNAP_MAX_PROJECTILES; i++) {
        if (!eng->projectiles[i].active) { slot = i; break; }
    }
    if (slot < 0) return;
    McProjectile *p = &eng->projectiles[slot];
    memset(p, 0, sizeof(*p));
    p->active = 1;
    p->kind = MC_PROJ_FIREWORK;
    p->from_player = 1;
    p->x = pos[0]; p->y = pos[1]; p->z = pos[2];
    p->vx = dir[0] * 4.0f;
    p->vy = 14.0f;
    p->vz = dir[2] * 4.0f;
    p->max_age = 0.85f + 0.25f * mc_randf(eng);
    p->firework_style = (uint8_t)(mc_rand(eng) % 4u);
    play_sfx_at(eng, MC_SFX_FIREWORK_LAUNCH, 1.0f, 0.95f + 0.1f * mc_randf(eng), pos);
}

static void perform_left_click_attack(McEngine *eng) {
    float orig[3], dir[3];
    get_attack_aim(eng, orig, dir);

    uint16_t held = eng->hotbar[eng->selected_slot].item_id;
    const McItemMeta *hm = (held < MC_ITM_COUNT) ? &k_items[held] : &k_items[0];
    float dmg_scale = mc_clampf(0.2f + 0.8f * (1.0f - (eng->attack_cd_timer / (hm->atk_speed > 0.0f ? hm->atk_speed : 0.25f))), 0.2f, 1.0f);
    float raw_dmg = hm->melee_dmg * dmg_scale;
    int is_crit = (!eng->player_grounded && eng->player_vel[1] < -0.5f && dmg_scale > 0.85f) ? 1 : 0;
    if (is_crit) raw_dmg *= 1.5f;

    eng->swing_timer = eng->swing_dur = 0.24f;
    eng->attack_cd_dur = hm->atk_speed > 0.05f ? hm->atk_speed : 0.25f;
    eng->attack_cd_timer = eng->attack_cd_dur;

    float mob_t = 999.0f;
    int mob_idx = find_hit_mob(eng, orig, dir, 3.6f, 0.35f, &mob_t);
    int hit_something = 0;

    if (mob_idx >= 0) {
        McMob *m = &eng->mobs[mob_idx];
        m->hp -= raw_dmg;
        m->hurt_timer = 0.4f;
        m->vx += dir[0] * 5.5f;
        m->vy += 3.8f;
        m->vz += dir[2] * 5.5f;
        eng->crosshair_hit_timer = 0.22f;
        hit_something = 1;

        float hp_pos[3] = {m->x, m->y + 1.0f, m->z};
        if (is_crit) {
            spawn_burst_particles(eng, MC_PART_CRIT, 1, COL_WHITE, hp_pos, 10, 0.6f, 2.6f, 0.22f, 0.45f, 2.0f);
            play_sfx_at(eng, MC_SFX_ATTACK_CRIT, 0.95f, 1.0f, hp_pos);
        } else if (hm->tool_kind == 1 && dmg_scale > 0.85f) {
            spawn_burst_particles(eng, MC_PART_SWEEP, 8, COL_WHITE, hp_pos, 1, 0.05f, 0.0f, 1.4f, 0.28f, 0.0f);
            play_sfx_at(eng, MC_SFX_ATTACK_SWEEP, 0.95f, 1.0f, hp_pos);
        } else {
            play_sfx_at(eng, dmg_scale > 0.8f ? MC_SFX_ATTACK_STRONG : MC_SFX_ATTACK_WEAK, 0.9f, 1.0f, hp_pos);
        }

        if (m->hp <= 0.0f) {
            kill_mob(eng, m);
        } else {
            McSfxEvent h_ev = (m->kind == MC_MOB_ZOMBIE) ? MC_SFX_ZOMBIE_HURT
                            : (m->kind == MC_MOB_SKELETON) ? MC_SFX_SKELETON_HURT : MC_SFX_CREEPER_SAY;
            play_sfx_at(eng, h_ev, 0.75f, 1.0f, hp_pos);
        }
    } else {
        float hit_p[3] = {0};
        int totk_dmg = (int)roundf(raw_dmg * 4.5f);
        if (totk_dmg < 4) totk_dmg = 4;
        if (totk_damage_nearest_enemy(orig, dir, 4.8f, 0.55f, 1, totk_dmg, dir, hit_p)) {
            eng->crosshair_hit_timer = 0.22f;
            hit_something = 1;
            if (is_crit) {
                spawn_burst_particles(eng, MC_PART_CRIT, 1, COL_WHITE, hit_p, 10, 0.6f, 2.4f, 0.24f, 0.45f, 2.0f);
                play_sfx_at(eng, MC_SFX_ATTACK_CRIT, 0.95f, 1.0f, hit_p);
            } else if (hm->tool_kind == 1 && dmg_scale > 0.85f) {
                spawn_burst_particles(eng, MC_PART_SWEEP, 8, COL_WHITE, hit_p, 1, 0.05f, 0.0f, 1.5f, 0.28f, 0.0f);
                play_sfx_at(eng, MC_SFX_ATTACK_SWEEP, 0.95f, 1.0f, hit_p);
            } else {
                play_sfx_at(eng, MC_SFX_ATTACK_STRONG, 0.9f, 1.0f, hit_p);
            }
        }
    }

    if (hit_something) eng->exhaustion += 0.1f;

    /* Sword/Flint click on TNT block ignites it immediately */
    if (!hit_something && eng->has_target && hm->tool_kind == 1) {
        int bidx = find_block_idx(eng, eng->target_ix, eng->target_iy, eng->target_iz);
        if (bidx >= 0 && eng->blocks[bidx].type == MC_BLK_TNT) {
            float tx = (float)eng->target_ix + 0.5f;
            float ty = (float)eng->target_iy + 0.5f;
            float tz = (float)eng->target_iz + 0.5f;
            set_block_at(eng, eng->target_ix, eng->target_iy, eng->target_iz, MC_BLK_AIR, 0);
            spawn_primed_tnt(eng, tx, ty, tz, 4.0f);
            float tpos[3] = {tx, ty, tz};
            play_sfx_at(eng, MC_SFX_TNT_FUSE, 1.0f, 1.0f, tpos);
            return;
        }
    }

    if (!hit_something && !eng->has_target) {
        play_sfx_2d(MC_SFX_ATTACK_WEAK, 0.45f, 1.0f);
    }
}

typedef struct {
    const char *label;
    uint16_t output, count, ingredient, needed, second, second_needed;
} StationRecipe;

static const StationRecipe table_recipes[] = {
    {"4 Planks   - 1 oak log", MC_ITM_OAK_PLANKS,4, MC_ITM_OAK_LOG,1,0,0},
    {"4 Sticks   - 2 planks", MC_ITM_STICK,4, MC_ITM_OAK_PLANKS,2,0,0},
    {"4 Torches  - coal + stick", MC_ITM_TORCH,4, MC_ITM_COAL,1,MC_ITM_STICK,1},
    {"Red wool   - 4 string (base recipe)", MC_ITM_RED_WOOL,1,MC_ITM_STRING,4,0,0},
    {"Red bed    - 3 red wool + 3 planks", MC_ITM_RED_BED,1,MC_ITM_RED_WOOL,3,MC_ITM_OAK_PLANKS,3},
    {"Furnace    - 8 cobblestone", MC_ITM_FURNACE,1,MC_ITM_COBBLESTONE,8,0,0},
    {"16 Glass panes - 6 glass", MC_ITM_GLASS_PANE,16,MC_ITM_GLASS,6,0,0}
};
static const StationRecipe furnace_recipes[] = {
    {"Smelt stone    - 1 cobblestone", MC_ITM_STONE,1,MC_ITM_COBBLESTONE,1,0,0},
    {"Make charcoal  - 1 oak log", MC_ITM_CHARCOAL,1,MC_ITM_OAK_LOG,1,0,0},
    {"Smelt glass    - 1 sand", MC_ITM_GLASS,1,MC_ITM_SAND,1,0,0},
    {"Cook steak     - 1 raw beef", MC_ITM_COOKED_BEEF,1,MC_ITM_RAW_BEEF,1,0,0}
};

static int furnace_input(uint16_t output) {
    for (size_t i=0;i<sizeof(furnace_recipes)/sizeof(*furnace_recipes);i++)
        if (furnace_recipes[i].output==output) return furnace_recipes[i].ingredient;
    return 0;
}

static int inventory_material(McEngine *eng, int item, int take) {
    int total = 0;
    for (int i = 0; i < 36; i++) {
        McItemStack *s = i < 9 ? &eng->hotbar[i] : &eng->inv_slots[i-9];
        if (s->item_id != item) continue;
        total += s->count;
        int used = s->count < take ? s->count : take;
        s->count -= used;
        take -= used;
        if (!s->count) memset(s,0,sizeof(*s));
    }
    return total;
}

static int station_index(const McEngine *eng) {
    int i = find_block_idx(eng,eng->station_x,eng->station_y,eng->station_z);
    if (i < 0 || (eng->blocks[i].type != MC_BLK_FURNACE && eng->blocks[i].type != MC_BLK_CRAFTING_TABLE)) return -1;
    return i;
}

static int night_time(const McEngine *eng) { return eng->day_seconds >= 600; }

static void station_action(McEngine *eng, int row) {
    int i = station_index(eng);
    if (i < 0) return;
    McWorldBlock *b = &eng->blocks[i];
    int furnace = b->type == MC_BLK_FURNACE;
    if (furnace && b->smelt_output) {
        if (row == 0 && b->cook_left <= 0) {
            McItemStack output = make_stack(b->smelt_output,1);
            if (try_give_stack(eng,&output)) {
                b->smelt_output = 0;
                eng->base_dirty = 1;
                show_tooltip(eng,"Collected");
            } else show_tooltip(eng,"Inventory full");
        }
        return;
    }
    if (row < 0 || row >= (int)(furnace ? sizeof(furnace_recipes)/sizeof(StationRecipe) : sizeof(table_recipes)/sizeof(StationRecipe))) return;
    const StationRecipe *recipe = furnace ? &furnace_recipes[row] : &table_recipes[row];
    int first = recipe->ingredient;
    if (first == MC_ITM_COAL && !inventory_material(eng,first,0)) first = MC_ITM_CHARCOAL;
    if (inventory_material(eng,first,0) < recipe->needed ||
        (recipe->second && inventory_material(eng,recipe->second,0) < recipe->second_needed)) {
        show_tooltip(eng,"Missing ingredients");
        return;
    }
    if (furnace && !b->fuel_uses) {
        int fuel = inventory_material(eng,MC_ITM_COAL,0) ? MC_ITM_COAL : MC_ITM_CHARCOAL;
        if (!inventory_material(eng,fuel,0)) { show_tooltip(eng,"Add coal or charcoal to your inventory"); return; }
        inventory_material(eng,fuel,1);
        b->fuel_uses = 8;
    }
    inventory_material(eng,first,recipe->needed);
    if (recipe->second) inventory_material(eng,recipe->second,recipe->second_needed);
    if (furnace) {
        b->fuel_uses--;
        b->smelt_output = recipe->output;
        b->cook_left = 10;
        eng->base_dirty = 1;
    } else {
        McItemStack output = make_stack(recipe->output,recipe->count);
        if (!try_give_stack(eng,&output)) spawn_dropped_item(eng,output.item_id,output.count,eng->player_pos,NULL);
        show_tooltip(eng,item_en_name(recipe->output));
    }
}

static void use_bed(McEngine *eng, McWorldBlock *bed, McItemStack *held) {
    if (held->item_id == MC_ITM_BONE) {
        int free_slot = 0;
        for (int i=0;i<MC_SNAP_MAX_MOBS;i++) {
            McMob *m = &eng->mobs[i];
            if (!m->active) free_slot = 1;
            else if (m->kind == MC_MOB_WOLF) {
                m->stay = !m->stay;
                show_tooltip(eng,m->stay ? "Wolf will stay" : "Wolf will follow");
                eng->base_dirty = 1;
                return;
            }
        }
        if (!free_slot) { show_tooltip(eng,"No room for a wolf"); return; }
        for (int dx=-1;dx<=1;dx++) for (int dz=-1;dz<=1;dz++) {
            float x=bed->ix+0.5f+dx, z=bed->iz+0.5f+dz;
            float y=probe_ground_surface_ex(eng,x,bed->iy+1.4f,z,NULL,NULL,NULL);
            if ((dx||dz) && fabsf(y-bed->iy)<1.5f && probe_mob_headroom(eng,x,y,z)) {
                spawn_mob(eng,MC_MOB_WOLF,x,y,z);
                if (--held->count == 0) memset(held,0,sizeof(*held));
                eng->base_dirty = 1;
                show_tooltip(eng,"Wolf joined you");
                return;
            }
        }
        show_tooltip(eng,"Clear a space beside the bed");
        return;
    }
    if (!night_time(eng)) { show_tooltip(eng,"You can sleep only at Minecraft night"); return; }
    for (int i=0;i<MC_SNAP_MAX_MOBS;i++) {
        McMob *m = &eng->mobs[i];
        if (m->active && m->kind != MC_MOB_WOLF && hypotf(m->x-bed->ix,m->z-bed->iz)<8 && fabsf(m->y-bed->iy)<5) {
            show_tooltip(eng,"Monsters nearby"); return;
        }
    }
    eng->day_seconds = 0;
    eng->encounter_timer = 90;
    eng->base_dirty = 1;
    show_tooltip(eng,"Slept until Minecraft morning");
}

static void handle_right_click_action(McEngine *eng, int on_press) {
    McItemStack *held = &eng->hotbar[eng->selected_slot];
    uint16_t item_id = held->item_id;
    float orig[3], dir[3];
    get_view_origin_dir(eng, orig, dir);

    if (on_press && eng->has_target && !eng->key_down[KVK_CONTROL]) {
        int i = find_block_idx(eng,eng->target_ix,eng->target_iy,eng->target_iz);
        if (i >= 0) {
            McWorldBlock *b = &eng->blocks[i];
            if (is_bed(b->type)) { use_bed(eng,b,held); return; }
            if (b->type == MC_BLK_CRAFTING_TABLE || b->type == MC_BLK_FURNACE) {
                eng->station_x=b->ix; eng->station_y=b->iy; eng->station_z=b->iz;
                eng->pending_recipe=0;
                eng->inv_open=MC_INV_STATION;
                eng->bow_drawing=eng->eating=0;
                eng->shield_block=0;
                eng->tooltip_timer=0;
                memset(eng->mouse_down,0,sizeof(eng->mouse_down));
                mc_totk_clear_camera_look();
                runtime_input_clear();
                return;
            }
        }
    }
    /* Elytra rockets are handled by the flight update after the jump input. */
    if (on_press && item_id == MC_ITM_FIREWORK_ROCKET) {
        float launch_pos[3] = {orig[0] + dir[0] * 1.2f, orig[1] + dir[1] * 1.2f, orig[2] + dir[2] * 1.2f};
        if (eng->has_target) {
            launch_pos[0] = (float)eng->target_ix + 0.5f;
            launch_pos[1] = (float)eng->target_iy + 1.1f;
            launch_pos[2] = (float)eng->target_iz + 0.5f;
        } else if (eng->has_hyrule_target) {
            memcpy(launch_pos, eng->hyrule_hit, sizeof(launch_pos));
            launch_pos[1] += 0.3f;
        }
        launch_firework_rocket(eng, launch_pos, dir);
        eng->swing_timer = eng->swing_dur = 0.22f;
        held->count--;
        if (held->count == 0) memset(held, 0, sizeof(*held));
        return;
    }

    /* 2. Flint and Steel on TNT */
    if (on_press && item_id == MC_ITM_FLINT_AND_STEEL && eng->has_target) {
        int bidx = find_block_idx(eng, eng->target_ix, eng->target_iy, eng->target_iz);
        if (bidx >= 0 && eng->blocks[bidx].type == MC_BLK_TNT) {
            float tx = (float)eng->target_ix + 0.5f;
            float ty = (float)eng->target_iy + 0.5f;
            float tz = (float)eng->target_iz + 0.5f;
            set_block_at(eng, eng->target_ix, eng->target_iy, eng->target_iz, MC_BLK_AIR, 0);
            spawn_primed_tnt(eng, tx, ty, tz, 4.0f);
            float tpos[3] = {tx, ty, tz};
            play_sfx_at(eng, MC_SFX_FIRE_IGNITE, 0.9f, 1.0f, tpos);
            play_sfx_at(eng, MC_SFX_TNT_FUSE, 1.0f, 1.0f, tpos);
            eng->swing_timer = eng->swing_dur = 0.2f;
            return;
        }
    }

    /* 3. Bow charging (hold Use Item) & Crossbow shot */
    if (item_id == MC_ITM_BOW) {
        eng->bow_drawing = eng->mouse_down[1];
        return;
    }
    if (on_press && item_id == MC_ITM_CROSSBOW) {
        fire_bow_arrow(eng, 1.0f);
        eng->swing_timer = eng->swing_dur = 0.2f;
        return;
    }

    /* 4. Eating food (Steak, Golden Apple, Enchanted Golden Apple, etc.) */
    if (item_id < MC_ITM_COUNT && k_items[item_id].is_food) {
        int always_eatable = (item_id == MC_ITM_GOLDEN_APPLE || item_id == MC_ITM_ENCHANTED_GOLDEN_APPLE);
        if (eng->hunger < 20 || always_eatable) {
            eng->eat_timer = 0.0f;
            eng->eat_snd_accum = 0.0f;
            eng->eating = eng->mouse_down[1];
            return;
        }
    }

    /* 5. Utility items (Bottle o' Enchanting, Ender Pearl) */
    if (on_press && item_id == MC_ITM_EXP_BOTTLE) {
        add_xp(eng, 8 + (int)(mc_rand(eng) % 6u));
        play_sfx_2d(MC_SFX_GLASS_BREAK, 0.7f, 1.15f);
        held->count--;
        if (held->count == 0) memset(held, 0, sizeof(*held));
        eng->swing_timer = eng->swing_dur = 0.2f;
        return;
    }
    if (on_press && item_id == MC_ITM_ENDER_PEARL) {
        if (eng->has_target || eng->has_hyrule_target) {
            float dest[3] = {
                eng->has_target ? (float)eng->target_ix + 0.5f : eng->hyrule_hit[0],
                (eng->has_target ? (float)eng->target_iy + 1.05f : eng->hyrule_hit[1] + 0.2f),
                eng->has_target ? (float)eng->target_iz + 0.5f : eng->hyrule_hit[2]
            };
            eng->player_pos[0] = dest[0]; eng->player_pos[1] = dest[1]; eng->player_pos[2] = dest[2];
            mc_totk_set_player_pos(dest, NULL);
            play_sfx_2d(MC_SFX_ITEM_POP, 1.0f, 0.75f);
            held->count--;
            if (held->count == 0) memset(held, 0, sizeof(*held));
            eng->swing_timer = eng->swing_dur = 0.2f;
            return;
        }
    }

    /* 6. Placing a block (or Water/Lava bucket) onto MC block face or Hyrule ground */
    uint16_t place_bt = (item_id < MC_ITM_COUNT) ? k_items[item_id].place_block : MC_BLK_AIR;
    int is_bucket = 0;
    if (item_id == MC_ITM_WATER_BUCKET) { place_bt = MC_BLK_WATER; is_bucket = 1; }
    else if (item_id == MC_ITM_LAVA_BUCKET) { place_bt = MC_BLK_LAVA; is_bucket = 1; }
    if (on_press && place_bt != MC_BLK_AIR) {
        int32_t px = 0, py = 0, pz = 0;
        int can_place = 0;
        static const int dx[6] = {-1, 1, 0, 0, 0, 0};
        static const int dy[6] = {0, 0, -1, 1, 0, 0};
        static const int dz[6] = {0, 0, 0, 0, -1, 1};
        if (eng->has_target) {
            int f = eng->target_face;
            if (f >= 0 && f < 6) {
                px = eng->target_ix + dx[f];
                py = eng->target_iy + dy[f];
                pz = eng->target_iz + dz[f];
                can_place = 1;
            }
        } else if (eng->has_hyrule_target) {
            px = (int32_t)floorf(eng->hyrule_hit[0] + eng->hyrule_normal[0] * 0.45f);
            py = (int32_t)floorf(eng->hyrule_hit[1] + eng->hyrule_normal[1] * 0.45f);
            pz = (int32_t)floorf(eng->hyrule_hit[2] + eng->hyrule_normal[2] * 0.45f);
            can_place = 1;
        } else if (!eng->cam_valid && eng->player_pos_valid && dir[1] < -0.05f) {
            float fy = floorf(eng->player_pos[1]);
            float t = (fy - orig[1]) / dir[1];
            if (t > 0.4f && t < 5.5f) {
                px = (int32_t)floorf(orig[0] + dir[0] * t);
                py = (int32_t)fy;
                pz = (int32_t)floorf(orig[2] + dir[2] * t);
                can_place = 1;
            }
        }

        int already_occupied = can_place ? (find_block_idx(eng, px, py, pz) >= 0) : 0;
        int overlaps_player = 0;
        if (can_place && !already_occupied) {
            uint16_t bt = place_bt;
            if (k_blocks[bt].solid && eng->player_pos_valid) {
                float pminx = eng->player_pos[0] - 0.24f, pmaxx = eng->player_pos[0] + 0.24f;
                float pminy = eng->player_pos[1],         pmaxy = eng->player_pos[1] + 1.75f;
                float pminz = eng->player_pos[2] - 0.24f, pmaxz = eng->player_pos[2] + 0.24f;
                if (pmaxx > (float)px && pminx < (float)(px + 1) &&
                    pmaxy > (float)py && pminy < (float)(py + 1) &&
                    pmaxz > (float)pz && pminz < (float)(pz + 1)) {
                    overlaps_player = 1;
                }
            }
            if (overlaps_player && !eng->has_target && eng->has_hyrule_target) {
                /* Nudge one horizontal voxel forward along gaze so placing near feet succeeds cleanly */
                int32_t sx = px + ((fabsf(dir[0]) >= fabsf(dir[2])) ? (dir[0] >= 0 ? 1 : -1) : 0);
                int32_t sz = pz + ((fabsf(dir[2]) > fabsf(dir[0])) ? (dir[2] >= 0 ? 1 : -1) : 0);
                if (find_block_idx(eng, sx, py, sz) < 0) {
                    px = sx; pz = sz;
                    overlaps_player = 0;
                }
            }
            if (!overlaps_player) {
                uint8_t facing = (fabsf(dir[0]) > fabsf(dir[2])) ? (dir[0] > 0 ? 2 : 3) : (dir[2] > 0 ? 0 : 1);
                if (bt == MC_BLK_BED_FOOT) {
                    int hx = px + bed_dx(facing), hz = pz + bed_dz(facing);
                    if (eng->block_count + 2 > MC_SNAP_MAX_BLOCKS || find_block_idx(eng,hx,py,hz) >= 0 ||
                        (fabsf(eng->player_pos[0]-hx-0.5f)<0.75f && fabsf(eng->player_pos[2]-hz-0.5f)<0.75f &&
                         eng->player_pos[1]<py+1 && eng->player_pos[1]+1.75f>py)) {
                        show_tooltip(eng,"The bed needs two clear spaces");
                        return;
                    }
                    set_block_at(eng, hx, py, hz, MC_BLK_BED_HEAD, facing);
                }
                if (set_block_at(eng, px, py, pz, bt, facing)) {
                    float bpos[3] = {(float)px + 0.5f, (float)py + 0.5f, (float)pz + 0.5f};
                    play_sfx_at(eng, is_bucket ? MC_SFX_SPLASH : mat_dig_sfx(k_blocks[bt].mat), 0.9f, 0.85f, bpos);
                    eng->swing_timer = eng->swing_dur = 0.22f;
                    if (!is_bucket) {
                        held->count--;
                        if (held->count == 0) memset(held, 0, sizeof(*held));
                    }
                    return;
                }
            }
        }
        fprintf(stderr,
                "[mc-engine] place skipped: bt=%u has_t=%d has_hy=%d can=%d occ=%d ovlp=%d blk=%d/%d dir=(%.2f,%.2f,%.2f) hy=(%.1f,%.1f,%.1f) p=(%.1f,%.1f,%.1f)\n",
                (unsigned)place_bt, eng->has_target, eng->has_hyrule_target,
                can_place, already_occupied, overlaps_player,
                eng->block_count, MC_SNAP_MAX_BLOCKS,
                dir[0], dir[1], dir[2],
                eng->hyrule_hit[0], eng->hyrule_hit[1], eng->hyrule_hit[2],
                eng->player_pos[0], eng->player_pos[1], eng->player_pos[2]);
    }

    /* 7. Otherwise offhand/mainhand Shield blocking */
    if (eng->offhand.item_id == MC_ITM_SHIELD || item_id == MC_ITM_SHIELD) {
        eng->shield_block = mc_clampf(eng->shield_block + 0.25f, 0.0f, 1.0f);
    }
}

#define MC_EAT_BITE_SECONDS 0.22f

static void finish_eating(McEngine *eng) {
    McItemStack *held = &eng->hotbar[eng->selected_slot];
    uint16_t id = held->item_id;
    if (id < MC_ITM_COUNT && k_items[id].is_food) {
        eng->hunger += (int)k_items[id].food_hunger;
        if (eng->hunger > 20) eng->hunger = 20;
        eng->saturation += k_items[id].food_sat;
        eng->saturation = fminf(eng->hunger,eng->saturation);

        if (id == MC_ITM_GOLDEN_APPLE) {
            grant_effect(eng, MC_EFF_REGENERATION, 1, 5.0f);
            grant_effect(eng, MC_EFF_ABSORPTION, 0, 120.0f);
            if (eng->absorption_half < 4) eng->absorption_half = 4;
        } else if (id == MC_ITM_ENCHANTED_GOLDEN_APPLE) {
            grant_effect(eng, MC_EFF_REGENERATION, 1, 20.0f);
            grant_effect(eng, MC_EFF_RESISTANCE, 0, 300.0f);
            grant_effect(eng, MC_EFF_FIRE_RESISTANCE, 0, 300.0f);
            grant_effect(eng, MC_EFF_ABSORPTION, 3, 120.0f);
            eng->absorption_half = 16;
        }
        held->count--;
        if (held->count == 0) memset(held, 0, sizeof(*held));
        play_sfx_2d(MC_SFX_BURP, 0.85f, 0.95f + 0.1f * mc_randf(eng));
    }
    eng->eating = 0;
    eng->eat_timer = 0.0f;
}

/* =========================================================================
 * Simulation Update Helpers
 * ========================================================================= */

static void update_mobs(McEngine *eng, float dt) {
    for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
        McMob *m = &eng->mobs[i];
        if (!m->active) continue;
        if (m->kind == MC_MOB_WOLF && eng->player_pos_valid &&
            (hypotf(eng->player_pos[0]-m->x,eng->player_pos[2]-m->z)>23 || fabsf(eng->player_pos[1]-m->y)>15)) {
            /* Freeze companions outside the loaded terrain. Rejoin on walkable ground. */
            if (!m->stay && eng->player_grounded && !eng->elytra_gliding) {
                float x=eng->player_pos[0]+2, z=eng->player_pos[2]+1;
                float y=probe_ground_surface_ex(eng,x,eng->player_pos[1]+1,z,NULL,NULL,NULL);
                if (fabsf(y-eng->player_pos[1])<2 && probe_mob_headroom(eng,x,y,z)) {
                    m->x=x; m->y=y; m->z=z; m->vx=m->vy=m->vz=0;
                }
            }
            continue;
        }
        if ((eng->player_pos_valid && m->y < eng->player_pos[1] - 80.0f) || m->y < -1200.0f) {
            fprintf(stderr, "[mc-mob] void despawn kind=%d at (%.1f, %.1f, %.1f)\n",
                    (int)m->kind, m->x, m->y, m->z);
            m->active = 0;
            continue;
        }
        if (m->hurt_timer > 0.0f) m->hurt_timer -= dt;
        if (m->attack_cd > 0.0f) m->attack_cd -= dt;
        if (m->swing_arm > 0.0f) m->swing_arm = mc_clampf(m->swing_arm - dt * 4.0f, 0.0f, 1.0f);

        float dx = eng->player_pos[0] - m->x;
        float dz = eng->player_pos[2] - m->z;
        float dist_xz = sqrtf(dx * dx + dz * dz);
        float dy = eng->player_pos[1] - m->y;

        if (m->natural && (dist_xz>80 || fabsf(dy)>60)) { m->active=0; continue; }
        m->say_timer -= dt;
        if (m->kind != MC_MOB_WOLF && m->say_timer <= 0.0f && dist_xz < 18.0f) {
            m->say_timer = 4.0f + 6.0f * mc_randf(eng);
            float mp[3] = {m->x, m->y + 1.4f, m->z};
            McSfxEvent s_ev = (m->kind == MC_MOB_CREEPER) ? MC_SFX_CREEPER_SAY
                            : (m->kind == MC_MOB_ZOMBIE) ? MC_SFX_ZOMBIE_SAY : MC_SFX_SKELETON_SAY;
            play_sfx_at(eng, s_ev, 0.45f, 0.9f + 0.2f * mc_randf(eng), mp);
        }

        float speed = (m->kind == MC_MOB_CREEPER) ? 2.6f : (m->kind == MC_MOB_ZOMBIE ? 2.5f : 2.3f);
        float target_vx = 0.0f, target_vz = 0.0f;
        if (eng->player_pos_valid && dist_xz < 24.0f && dist_xz > 0.05f) {
            m->yaw = atan2f(dx, dz);
            m->pitch = atan2f(-dy, dist_xz);
            float nx = dx / dist_xz, nz = dz / dist_xz;
            float m_eye[3] = {m->x, m->y + 1.45f, m->z};
            float p_eye[3] = {eng->player_pos[0], eng->player_pos[1] + 1.25f, eng->player_pos[2]};
            int has_los = mob_has_los(eng, m_eye, p_eye);

            if (m->kind == MC_MOB_WOLF) {
                if (!m->stay && dist_xz>2.5f) { target_vx=nx*3.4f; target_vz=nz*3.4f; }
            } else if (m->kind == MC_MOB_CREEPER) {
                if (has_los && dist_xz < 3.0f && fabsf(dy) < 2.2f) {
                    if (m->swell == 0.0f) {
                        float cp[3] = {m->x, m->y + 1.0f, m->z};
                        play_sfx_at(eng, MC_SFX_TNT_FUSE, 1.0f, 1.0f, cp);
                    }
                    m->swell += dt / 1.5f;
                    if (m->swell >= 1.0f) {
                        float cp[3] = {m->x, m->y + 0.8f, m->z};
                        m->active = 0;
                        trigger_explosion(eng, cp, 3.6f, 22.0f, 0);
                        continue;
                    }
                } else {
                    m->swell = mc_clampf(m->swell - dt * 1.2f, 0.0f, 1.0f);
                    if (dist_xz > 1.35f || !has_los) {
                        target_vx = nx * speed;
                        target_vz = nz * speed;
                    }
                }
            } else if (m->kind == MC_MOB_ZOMBIE) {
                if (dist_xz > 1.35f) {
                    target_vx = nx * speed;
                    target_vz = nz * speed;
                } else if (has_los && m->attack_cd <= 0.0f && fabsf(dy) < 1.8f) {
                    m->attack_cd = 1.0f;
                    m->swing_arm = 1.0f;
                    float zp[3] = {m->x, m->y + 1.2f, m->z};
                    apply_player_damage(eng, 8, eng->totk_hp_q, 1, zp);
                }
            } else { /* Skeleton */
                if (!has_los || dist_xz > 10.5f) {
                    target_vx = nx * speed;
                    target_vz = nz * speed;
                } else if (dist_xz < 5.0f) {
                    target_vx = -nx * speed * 0.7f;
                    target_vz = -nz * speed * 0.7f;
                } else {
                    target_vx = -nz * m->strafe_dir * speed * 0.6f;
                    target_vz = nx * m->strafe_dir * speed * 0.6f;
                }
                if (has_los && m->attack_cd <= 0.0f && dist_xz < 18.0f) {
                    m->attack_cd = 2.0f;
                    for (int p_i = 0; p_i < MC_SNAP_MAX_PROJECTILES; p_i++) {
                        if (!eng->projectiles[p_i].active) {
                            McProjectile *pr = &eng->projectiles[p_i];
                            memset(pr, 0, sizeof(*pr));
                            pr->active = 1;
                            pr->kind = MC_PROJ_ARROW;
                            pr->from_player = 0;
                            pr->x = m->x + nx * 0.6f;
                            pr->y = m->y + 1.55f;
                            pr->z = m->z + nz * 0.6f;
                            float invd = 1.0f / (dist_xz > 0.1f ? dist_xz : 1.0f);
                            pr->vx = dx * invd * 22.0f;
                            pr->vy = (dy + 0.12f * dist_xz) * invd * 22.0f + 1.2f;
                            pr->vz = dz * invd * 22.0f;
                            pr->max_age = 8.0f;
                            pr->power = 4.0f;
                            float sp[3] = {pr->x, pr->y, pr->z};
                            play_sfx_at(eng, MC_SFX_BOW_SHOOT, 0.85f, 1.0f, sp);
                            break;
                        }
                    }
                }
            }
        }

        /* Ledge avoidance: don't voluntarily walk off steep cliffs or into void */
        float tspd = sqrtf(target_vx * target_vx + target_vz * target_vz);
        if (m->grounded && m->hurt_timer <= 0.0f && tspd > 0.1f) {
            float lx = m->x + (target_vx / tspd) * 0.75f;
            float lz = m->z + (target_vz / tspd) * 0.75f;
            float lny = 1.0f;
            float lgy = probe_ground_surface_ex(eng, lx, m->y + 0.85f, lz, NULL, NULL, &lny);
            if (lgy < -9000.0f || lgy < m->y - 3.2f || lny < 0.35f) {
                target_vx = 0.0f;
                target_vz = 0.0f;
                m->vx = 0.0f;
                m->vz = 0.0f;
            }
        }

        m->vx += (target_vx - m->vx) * mc_clampf(dt * 8.0f, 0.0f, 1.0f);
        m->vz += (target_vz - m->vz) * mc_clampf(dt * 8.0f, 0.0f, 1.0f);
        m->vy -= 26.0f * dt;
        if (m->vy < -48.0f) m->vy = -48.0f;

        float cur_pos[3] = {m->x, m->y, m->z};
        float delta_xz[2] = {m->vx * dt, m->vz * dt};
        float hy_out_xz[2] = {m->x + delta_xz[0], m->z + delta_xz[1]};
        float wall_norm[2] = {0.0f, 0.0f};
        float step_y = 0.0f;
        if (fabsf(delta_xz[0]) > 1e-6f || fabsf(delta_xz[1]) > 1e-6f) {
            int wres = mc_totk_check_wall_step(cur_pos, 0.38f, 1.80f, delta_xz,
                                               hy_out_xz, wall_norm, &step_y);
            if (wres & 1) {
                float vn = m->vx * wall_norm[0] + m->vz * wall_norm[1];
                if (vn < 0.0f) {
                    m->vx -= vn * wall_norm[0];
                    m->vz -= vn * wall_norm[1];
                }
            }
            if ((wres & 2) && m->grounded) {
                m->vy = 8.2f;
                m->grounded = 0;
            }
        }

        float next_x = hy_out_xz[0], next_z = hy_out_xz[1];
        if (!aabb_overlaps_solid_blocks(eng, next_x, m->y + 0.05f, next_z, 0.32f, 1.75f)) {
            m->x = next_x;
            m->z = next_z;
        } else if (m->grounded && !aabb_overlaps_solid_blocks(eng, next_x, m->y + 1.05f, next_z, 0.32f, 1.75f)) {
            m->vy = 8.2f;
            m->grounded = 0;
        } else {
            int slid = 0;
            if (fabsf(next_x - m->x) > 1e-5f &&
                !aabb_overlaps_solid_blocks(eng, next_x, m->y + 0.05f, m->z, 0.32f, 1.75f)) {
                float sx_d[2] = {next_x - m->x, 0.0f}, sx_out[2] = {next_x, m->z}, swn[2] = {0};
                float ssy = 0.0f;
                float cpos[3] = {m->x, m->y, m->z};
                mc_totk_check_wall_step(cpos, 0.38f, 1.80f, sx_d, sx_out, swn, &ssy);
                if (!aabb_overlaps_solid_blocks(eng, sx_out[0], m->y + 0.05f, sx_out[1], 0.32f, 1.75f)) {
                    m->x = sx_out[0]; m->z = sx_out[1];
                    m->vz = 0.0f;
                    slid = 1;
                }
            }
            if (!slid && fabsf(next_z - m->z) > 1e-5f &&
                !aabb_overlaps_solid_blocks(eng, m->x, m->y + 0.05f, next_z, 0.32f, 1.75f)) {
                float sz_d[2] = {0.0f, next_z - m->z}, sz_out[2] = {m->x, next_z}, swn[2] = {0};
                float ssy = 0.0f;
                float cpos[3] = {m->x, m->y, m->z};
                mc_totk_check_wall_step(cpos, 0.38f, 1.80f, sz_d, sz_out, swn, &ssy);
                if (!aabb_overlaps_solid_blocks(eng, sz_out[0], m->y + 0.05f, sz_out[1], 0.32f, 1.75f)) {
                    m->x = sz_out[0]; m->z = sz_out[1];
                    m->vx = 0.0f;
                }
            }
        }

        /* Soft mob-mob and mob-player separation, wall- & block-safe */
        for (int j = 0; j < MC_SNAP_MAX_MOBS; j++) {
            if (j == i || !eng->mobs[j].active) continue;
            if (fabsf(m->y - eng->mobs[j].y) > 1.7f) continue;
            float sx = m->x - eng->mobs[j].x;
            float sz = m->z - eng->mobs[j].z;
            float d2 = sx * sx + sz * sz;
            const float min_sep = 0.70f;
            if (d2 < min_sep * min_sep) {
                float d = sqrtf(d2);
                float px_d, pz_d;
                if (d < 1e-4f) {
                    px_d = (i < j) ? 0.1f : -0.1f;
                    pz_d = 0.0f;
                } else {
                    float push = (min_sep - d) * 0.5f;
                    if (push > 0.12f) push = 0.12f;
                    px_d = (sx / d) * push;
                    pz_d = (sz / d) * push;
                }
                float sp_pos[3] = {m->x, m->y, m->z};
                float sp_del[2] = {px_d, pz_d};
                float sp_out[2] = {m->x + px_d, m->z + pz_d}, wn[2] = {0}, sy_s = 0.0f;
                mc_totk_check_wall_step(sp_pos, 0.38f, 1.80f, sp_del, sp_out, wn, &sy_s);
                if (!aabb_overlaps_solid_blocks(eng, sp_out[0], m->y + 0.05f, sp_out[1], 0.32f, 1.75f)) {
                    m->x = sp_out[0];
                    m->z = sp_out[1];
                }
            }
        }
        if (eng->player_pos_valid && fabsf(m->y - eng->player_pos[1]) < 1.8f) {
            float sx = m->x - eng->player_pos[0];
            float sz = m->z - eng->player_pos[2];
            float d2 = sx * sx + sz * sz;
            const float min_psep = 0.72f;
            if (d2 < min_psep * min_psep && d2 > 1e-6f) {
                float d = sqrtf(d2);
                float push = min_psep - d;
                if (push > 0.18f) push = 0.18f;
                float sp_pos[3] = {m->x, m->y, m->z};
                float sp_del[2] = {(sx / d) * push, (sz / d) * push};
                float sp_out[2] = {m->x + sp_del[0], m->z + sp_del[1]}, wn[2] = {0}, sy_s = 0.0f;
                mc_totk_check_wall_step(sp_pos, 0.38f, 1.80f, sp_del, sp_out, wn, &sy_s);
                if (!aabb_overlaps_solid_blocks(eng, sp_out[0], m->y + 0.05f, sp_out[1], 0.32f, 1.75f)) {
                    m->x = sp_out[0];
                    m->z = sp_out[1];
                }
            }
        }

        /* Anti-tunneling vertical sweep & upward bonk guard */
        float prev_y = m->y;
        m->y += m->vy * dt;
        if (m->vy > 0.0f) {
            if (aabb_overlaps_solid_blocks(eng, m->x, m->y + 0.05f, m->z, 0.32f, 1.75f)) {
                m->y = prev_y;
                m->vy = 0.0f;
            }
            m->grounded = 0;
        } else {
            float gy_norm = 0.0f;
            float sweep_top = fmaxf(prev_y, m->y) + 0.70f;
            float gy = probe_ground_surface_ex(eng, m->x, sweep_top, m->z, NULL, NULL, &gy_norm);
            if (gy > -9000.0f && gy <= prev_y + 0.85f && m->y <= gy + 0.04f && gy_norm >= 0.30f) {
                m->y = gy;
                m->vy = 0.0f;
                m->grounded = 1;
            } else {
                m->grounded = 0;
            }
        }

        float hspeed = sqrtf(m->vx * m->vx + m->vz * m->vz);
        m->walk_amp = mc_clampf(hspeed / speed, 0.0f, 1.0f);
        m->walk_phase += hspeed * dt * 2.2f;
        m->hurt_flash = (m->hurt_timer > 0.2f) ? 1 : 0;
    }
}

static void update_projectiles_and_particles(McEngine *eng, float dt) {
    for (int i = 0; i < MC_SNAP_MAX_PRIMED_TNT; i++) {
        McPrimedTnt *t = &eng->primed_tnt[i];
        if (!t->active) continue;
        t->fuse_left -= dt;
        t->flash_white = ((int)(t->fuse_left * 5.0f) & 1) == 0 ? 1 : 0;
        float prev_ty = t->y;
        t->vy -= 22.0f * dt;
        t->x += t->vx * dt; t->y += t->vy * dt; t->z += t->vz * dt;
        float sweep_top = fmaxf(prev_ty - 0.5f, t->y - 0.5f) + 0.75f;
        float gy = probe_ground_surface_ex(eng, t->x, sweep_top, t->z, NULL, NULL, NULL);
        if (gy > -9000.0f && gy <= prev_ty + 0.55f && t->y - 0.5f <= gy) {
            t->y = gy + 0.5f;
            t->vy = 0.0f;
            t->vx *= 0.6f; t->vz *= 0.6f;
        }
        if ((mc_rand(eng) % 3u) == 0u) {
            float sp[3] = {t->x, t->y + 0.55f, t->z};
            float sv[3] = {0.0f, 1.2f, 0.0f};
            spawn_particle(eng, MC_PART_SMOKE, 12, RGBA_PACK(180, 180, 180, 200), sp, sv, 0.25f, 0.35f, -0.5f);
        }
        if (t->fuse_left <= 0.0f) {
            float c[3] = {t->x, t->y, t->z};
            t->active = 0;
            trigger_explosion(eng, c, 4.2f, 36.0f, 1);
        }
    }

    for (int i = 0; i < MC_SNAP_MAX_PROJECTILES; i++) {
        McProjectile *p = &eng->projectiles[i];
        if (!p->active) continue;
        p->age += dt;
        if (p->age >= p->max_age) {
            if (p->kind == MC_PROJ_FIREWORK) {
                static const uint32_t fw_cols[4] = {
                    RGBA_PACK(255, 70, 70, 255), RGBA_PACK(80, 220, 255, 255),
                    RGBA_PACK(255, 225, 65, 255), RGBA_PACK(195, 95, 255, 255)
                };
                float pos[3] = {p->x, p->y, p->z};
                play_sfx_at(eng, (p->firework_style & 1) ? MC_SFX_FIREWORK_LARGE_BLAST : MC_SFX_FIREWORK_BLAST, 1.2f, 1.0f, pos);
                play_sfx_at(eng, MC_SFX_FIREWORK_TWINKLE, 0.9f, 1.0f, pos);
                spawn_burst_particles(eng, MC_PART_GENERIC, 8, fw_cols[p->firework_style & 3], pos, 44, 0.3f, 8.5f, 0.35f, 1.0f, 4.5f);
            }
            p->active = 0;
            continue;
        }
        if (p->stuck) continue;

        if (p->kind == MC_PROJ_FIREWORK) {
            p->vy += 5.5f * dt;
            if ((mc_rand(eng) % 2u) == 0u) {
                float sp[3] = {p->x, p->y, p->z}, sv[3] = {0.0f, -0.5f, 0.0f};
                spawn_particle(eng, MC_PART_FLAME, 1, COL_WHITE, sp, sv, 0.18f, 0.25f, 0.0f);
            }
        } else {
            p->vy -= 19.6f * dt;
            if (p->crit && (mc_rand(eng) % 2u) == 0u) {
                float sp[3] = {p->x, p->y, p->z}, sv[3] = {0.0f, 0.0f, 0.0f};
                spawn_particle(eng, MC_PART_CRIT, 1, COL_WHITE, sp, sv, 0.16f, 0.25f, 0.0f);
            }
        }

        float nx = p->x + p->vx * dt, ny = p->y + p->vy * dt, nz = p->z + p->vz * dt;
        if (p->kind == MC_PROJ_ARROW) {
            /* Hit MC block check */
            int32_t bix = (int32_t)floorf(nx), biy = (int32_t)floorf(ny), biz = (int32_t)floorf(nz);
            int bidx = find_block_idx(eng, bix, biy, biz);
            if (bidx >= 0 && eng->blocks[bidx].type < MC_BLK_COUNT && k_blocks[eng->blocks[bidx].type].solid) {
                if (eng->blocks[bidx].type == MC_BLK_TNT) {
                    set_block_at(eng, bix, biy, biz, MC_BLK_AIR, 0);
                    spawn_primed_tnt(eng, (float)bix + 0.5f, (float)biy + 0.5f, (float)biz + 0.5f, 4.0f);
                    p->active = 0;
                } else {
                    p->stuck = 1;
                    p->age = 0.0f; p->max_age = 6.0f;
                    float bp[3] = {nx, ny, nz};
                    play_sfx_at(eng, MC_SFX_BOW_HIT, 0.8f, 1.0f, bp);
                }
                continue;
            }
            /* Hit MC mob / player check */
            if (p->from_player) {
                int hit_m = 0;
                for (int mi = 0; mi < MC_SNAP_MAX_MOBS; mi++) {
                    McMob *m = &eng->mobs[mi];
                    if (!m->active) continue;
                    float dx = nx - m->x, dy = ny - (m->y + 0.9f), dz = nz - m->z;
                    if (dx * dx + dz * dz < 0.38f && fabsf(dy) < 0.95f) {
                        m->hp -= p->power;
                        m->hurt_timer = 0.4f;
                        eng->crosshair_hit_timer = 0.2f;
                        float mp[3] = {nx, ny, nz};
                        play_sfx_2d(MC_SFX_HIT_ARROW_DING, 0.75f, 1.0f);
                        play_sfx_at(eng, MC_SFX_BOW_HIT, 0.8f, 1.0f, mp);
                        if (m->hp <= 0.0f) kill_mob(eng, m);
                        p->active = 0;
                        hit_m = 1;
                        break;
                    }
                }
                if (hit_m) continue;
                float adir[3] = {p->vx, p->vy, p->vz};
                float dl = sqrtf(adir[0]*adir[0] + adir[1]*adir[1] + adir[2]*adir[2]);
                if (dl > 0.1f) {
                    adir[0] /= dl; adir[1] /= dl; adir[2] /= dl;
                    float po[3] = {p->x, p->y, p->z}, hp[3] = {0};
                    if (totk_damage_nearest_enemy(po, adir, dl * dt, 0.30f, 0,
                                                  (int)roundf(p->power * 4.5f), adir, hp)) {
                        play_sfx_2d(MC_SFX_HIT_ARROW_DING, 0.75f, 1.0f);
                        play_sfx_at(eng, MC_SFX_BOW_HIT, 0.85f, 1.0f, hp);
                        spawn_burst_particles(eng, MC_PART_CRIT, 1, COL_WHITE, hp, 8, 0.5f, 2.2f, 0.20f, 0.4f, 2.0f);
                        eng->crosshair_hit_timer = 0.22f;
                        p->active = 0;
                        continue;
                    }
                }
            } else if (eng->player_pos_valid) {
                float dx = nx - eng->player_pos[0];
                float dy = ny - (eng->player_pos[1] + 0.9f);
                float dz = nz - eng->player_pos[2];
                if (dx * dx + dz * dz < 0.35f && fabsf(dy) < 0.95f) {
                    float ap[3] = {p->x, p->y, p->z};
                    apply_player_damage(eng, (int)p->power * 2, eng->totk_hp_q, 1, ap);
                    p->active = 0;
                    continue;
                }
            }
            /* Hyrule surface check */
            float gy = probe_ground_surface(eng, nx, ny + 0.4f, nz, NULL, NULL);
            if (gy > -9000.0f && ny <= gy + 0.04f) {
                p->x = nx; p->y = gy + 0.04f; p->z = nz;
                p->stuck = 1; p->age = 0.0f; p->max_age = 6.0f;
                float gp[3] = {p->x, p->y, p->z};
                play_sfx_at(eng, MC_SFX_BOW_HIT, 0.75f, 1.0f, gp);
                continue;
            }
        }
        p->x = nx; p->y = ny; p->z = nz;
    }

    for (int i = 0; i < MC_SNAP_MAX_DROPPED; i++) {
        McDroppedItem *d = &eng->dropped[i];
        if (!d->active) continue;
        d->age += dt;
        if (d->pickup_delay > 0.0f) d->pickup_delay -= dt;
        d->spin_yaw += dt * 2.2f;
        d->vy -= 20.0f * dt;
        d->x += d->vx * dt; d->y += d->vy * dt; d->z += d->vz * dt;
        float gy = probe_ground_surface(eng, d->x, d->y + 0.5f, d->z, NULL, NULL);
        if (gy > -9000.0f && d->y <= gy + 0.15f) {
            d->y = gy + 0.15f; d->vy = 0.0f; d->vx *= 0.65f; d->vz *= 0.65f;
        }
        if (eng->player_pos_valid && d->pickup_delay <= 0.0f) {
            float dx = d->x - eng->player_pos[0];
            float dy = d->y - (eng->player_pos[1] + 0.8f);
            float dz = d->z - eng->player_pos[2];
            if (dx * dx + dy * dy + dz * dz < 1.6f * 1.6f) {
                McItemStack st = make_stack(d->item_type, d->count);
                st.enchanted = d->enchanted;
                if (try_give_stack(eng, &st)) {
                    d->active = 0;
                    play_sfx_2d(MC_SFX_ITEM_POP, 0.6f, 0.9f + 0.25f * mc_randf(eng));
                } else d->count = st.count;
            }
        }
        if (d->age > 120.0f) d->active = 0;
    }

    for (int i = 0; i < MC_SNAP_MAX_PARTICLES; i++) {
        McParticle *pt = &eng->particles[i];
        if (!pt->active) continue;
        pt->age += dt;
        if (pt->age >= pt->max_age) { pt->active = 0; continue; }
        pt->vy -= pt->gravity * dt;
        pt->x += pt->vx * dt; pt->y += pt->vy * dt; pt->z += pt->vz * dt;
    }
}

#define MC_MINING_SWING_SECONDS 0.30f

static void update_mining(McEngine *eng, float dt) {
    if (!eng->mouse_down[0] || !eng->has_target || eng->inv_open != MC_INV_CLOSED) {
        eng->mining_progress = 0.0f;
        eng->mining_snd_accum = 0.0f;
        eng->mining_crumb_accum = 0.0f;
        return;
    }
    int bidx = find_block_idx(eng, eng->target_ix, eng->target_iy, eng->target_iz);
    if (bidx < 0 || eng->blocks[bidx].type >= MC_BLK_COUNT) {
        eng->mining_progress = 0.0f;
        return;
    }
    if (eng->mining_ix != eng->target_ix || eng->mining_iy != eng->target_iy || eng->mining_iz != eng->target_iz) {
        eng->mining_ix = eng->target_ix; eng->mining_iy = eng->target_iy; eng->mining_iz = eng->target_iz;
        eng->mining_progress = 0.0f;
    }
    const McBlockMeta *bm = &k_blocks[eng->blocks[bidx].type];
    if (eng->mining_swing_timer <= 0.0f) eng->mining_swing_timer = MC_MINING_SWING_SECONDS;
    if (bm->hardness < 0.0f) return;

    uint16_t held = eng->hotbar[eng->selected_slot].item_id;
    uint8_t tk = (held < MC_ITM_COUNT) ? k_items[held].tool_kind : 0;
    float mult = 1.0f;
    if (bm->mat == MAT_STONE && tk == 3) mult = 5.5f;
    else if (bm->mat == MAT_WOOD && tk == 2) mult = 4.5f;
    else if (tk == 1 && eng->blocks[bidx].type == MC_BLK_TNT) mult = 5.0f;

    float break_time = bm->hardness / mult;
    if (break_time < 0.05f) break_time = 0.05f;
    eng->mining_progress += dt / break_time;

    float bpos[3] = {(float)eng->target_ix + 0.5f, (float)eng->target_iy + 0.5f, (float)eng->target_iz + 0.5f};
    eng->mining_snd_accum += dt;
    if (eng->mining_snd_accum >= 0.22f) {
        eng->mining_snd_accum = 0.0f;
        play_sfx_at(eng, mat_dig_sfx(bm->mat), 0.55f, 0.85f + 0.15f * mc_randf(eng), bpos);
    }
    eng->mining_crumb_accum += dt;
    if (eng->mining_crumb_accum >= 0.08f) {
        eng->mining_crumb_accum = 0.0f;
        spawn_burst_particles(eng, MC_PART_BLOCK_CRUMB, 1, block_crumb_color(eng->blocks[bidx].type),
                              bpos, 3, 0.7f, 1.5f, 0.14f, 0.35f, 14.0f);
    }

    if (eng->mining_progress >= 1.0f) {
        eng->exhaustion += 0.005f;
        uint16_t broken_bt = eng->blocks[bidx].type;
        play_sfx_at(eng, mat_dig_sfx(bm->mat), 0.9f, 1.0f, bpos);
        spawn_burst_particles(eng, MC_PART_BLOCK_CRUMB, 1, block_crumb_color(broken_bt),
                              bpos, 14, 0.8f, 2.8f, 0.18f, 0.55f, 15.0f);
        if (bm->drop_item != MC_ITM_NONE) {
            spawn_dropped_item(eng, bm->drop_item, 1, bpos, NULL);
        }
        set_block_at(eng, eng->target_ix, eng->target_iy, eng->target_iz, MC_BLK_AIR, 0);
        eng->mining_progress = 0.0f;
    }
}

/* =========================================================================
 * Public Engine Entrypoints
 * ========================================================================= */

/* Profile sidecar, independent of the native Zelda save format. Only persistent
 * state belongs here. Bump the format when changing its stored records. */
typedef struct {
    uint32_t version, block_count;
    float day, exhaustion, saturation;
    int hunger, selected;
    McItemStack items[46]; /* 9 hotbar, 27 backpack, 4 armor, offhand, 4 craft, cursor */
    McMob wolf;
    McWorldBlock blocks[MC_SNAP_MAX_BLOCKS];
} BaseSaveV1;

static McItemStack *saved_slot(McEngine *eng,int i) {
    if (i<9) return &eng->hotbar[i];
    if (i<36) return &eng->inv_slots[i-9];
    if (i<40) return &eng->armor_slots[i-36];
    if (i==40) return &eng->offhand;
    if (i<45) return &eng->craft_slots[i-41];
    return &eng->cursor_stack;
}

static int base_dir(char *dir, size_t size) {
    if (plat_getenv("TOTK_PROFILE_DIR", dir, size) > 0 && dir[0]) return 1;
    const RtConfig *cfg = rt_config();
    if (cfg && cfg->profile && cfg->profile[0]) {
        int n = snprintf(dir, size, "%s", cfg->profile);
        return n > 0 && (size_t)n < size;
    }
    char appdata[PLAT_PATH_MAX];
    if (plat_getenv("APPDATA", appdata, sizeof appdata) > 0 && appdata[0]) {
        int n = snprintf(dir, size, "%s/totk-pc-recomp", appdata);
        return n > 0 && (size_t)n < size;
    }
    return 0;
}

static int base_path(char *path, size_t size, const char *suffix) {
    char dir[PLAT_PATH_MAX];
    if (!base_dir(dir, sizeof dir)) return 0;
    int n = snprintf(path, size, "%s/minecraft-base.v1%s", dir, suffix);
    return n > 0 && (size_t)n < size;
}

typedef struct McBaseSaveJob {
    char dir[PLAT_PATH_MAX];
    char path[1200];
    BaseSaveV1 save;
    int ok;
} McBaseSaveJob;

static McBaseSaveJob *prepare_base_save(McEngine *eng) {
    if (!eng->base_played || eng->base_read_error) return NULL;
    McBaseSaveJob *job = calloc(1, sizeof(*job));
    if (!job) return NULL;
    if (!base_dir(job->dir, sizeof(job->dir)) || !base_path(job->path, sizeof(job->path), "")) {
        free(job);
        return NULL;
    }
    BaseSaveV1 *save = &job->save;
    save->version=1; save->block_count=eng->block_count;
    save->day=eng->day_seconds; save->exhaustion=eng->exhaustion;
    save->saturation=eng->saturation; save->hunger=eng->hunger; save->selected=eng->selected_slot;
    for (int i=0;i<46;i++) save->items[i]=*saved_slot(eng,i);
    memcpy(save->blocks,eng->blocks,eng->block_count*sizeof(McWorldBlock));
    for (int i=0;i<MC_SNAP_MAX_MOBS;i++)
        if (eng->mobs[i].active && eng->mobs[i].kind==MC_MOB_WOLF) { save->wolf=eng->mobs[i]; break; }
    return job;
}

static void write_base_save(void *arg) {
    McBaseSaveJob *job = arg;
    plat_file_mkdir_p(job->dir);
    job->ok = plat_file_write_atomic(job->path, &job->save, sizeof(job->save));
    if (!job->ok) fprintf(stderr, "[mc] saving base failed: %s\n", job->path);
}

static void finish_base_autosave(McEngine *eng) {
    if (!eng->base_save_job->ok) eng->base_dirty = 1;
    free(eng->base_save_job);
    eng->base_save_job = NULL;
    eng->base_save_thread = NULL;
}

static void tick_base_autosave(McEngine *eng, float dt) {
    eng->base_save_timer += dt;
    if (eng->base_save_thread) {
        if (!plat_thread_join_ns(eng->base_save_thread, 0)) return;
        finish_base_autosave(eng);
    }
    if (eng->base_save_timer < (eng->base_dirty ? 2.0f : 15.0f)) return;
    eng->base_save_timer = 0.0f;
    McBaseSaveJob *job = prepare_base_save(eng);
    if (!job) return;
    PlatThread *thread = plat_thread_new(write_base_save, job, "Minecraft autosave");
    if (!thread) {
        free(job);
        return;
    }
    eng->base_save_job = job;
    eng->base_save_thread = thread;
    eng->base_dirty = 0;
}

void mc_engine_save_base(McEngine *eng) {
    if (eng->base_save_thread) {
        plat_thread_join(eng->base_save_thread);
        finish_base_autosave(eng);
    }
    McBaseSaveJob *job = prepare_base_save(eng);
    if (!job) return;
    write_base_save(job);
    if (job->ok) eng->base_dirty = 0;
    free(job);
}

static void load_base(McEngine *eng) {
    char path[1200];
    if (!base_path(path, sizeof path, "")) return;
    if (!plat_file_exists(path)) return;
    uint64_t fsz = 0;
    uint8_t *raw = plat_file_read(path, &fsz);
    if (!raw) { eng->base_read_error=1; return; }
    BaseSaveV1 *s = (BaseSaveV1 *)raw;
    int ok = fsz == sizeof(*s) && s->version==1 &&
        s->block_count<=MC_SNAP_MAX_BLOCKS && s->selected>=0 && s->selected<9 &&
        s->hunger>=0 && s->hunger<=20 && isfinite(s->day) && s->day>=0 && s->day<1200 &&
        isfinite(s->exhaustion) && isfinite(s->saturation);
    for (int i=0;ok && i<46;i++) ok=s->items[i].item_id<MC_ITM_COUNT &&
        s->items[i].count<=k_items[s->items[i].item_id].max_stack;
    for (unsigned i=0;ok && i<s->block_count;i++) {
        McWorldBlock *b=&s->blocks[i];
        ok=b->active && b->type>0 && b->type<MC_BLK_COUNT && b->facing<4 &&
            (b->smelt_output==0 || furnace_input(b->smelt_output)) &&
            isfinite(b->cook_left) && b->fuel_uses<=8 &&
            b->ix>-100000 && b->ix<100000 && b->iy>-100000 && b->iy<100000 && b->iz>-100000 && b->iz<100000;
    }
    if (ok && s->wolf.active) ok=s->wolf.kind==MC_MOB_WOLF && isfinite(s->wolf.x) && isfinite(s->wolf.y) && isfinite(s->wolf.z);
    if (ok) {
        eng->block_count=s->block_count;
        eng->block_faces_dirty = 1;
        memcpy(eng->blocks,s->blocks,s->block_count*sizeof(McWorldBlock));
        eng->day_seconds=s->day; eng->exhaustion=s->exhaustion;
        eng->hunger=s->hunger; eng->saturation=s->saturation; eng->selected_slot=s->selected;
        for (int i=0;i<46;i++) *saved_slot(eng,i)=s->items[i];
        eng->mobs[0]=s->wolf;
        update_craft_result(eng);
        recompute_armor(eng);
    } else {
        eng->base_read_error=1;
        fprintf(stderr,"[mc] Unrecognized base save; preserving file\n");
    }
    free(s);
}

void mc_engine_init(McEngine *eng) {
    memset(eng, 0, sizeof(*eng));
    eng->active = 1;
    eng->mode = 1;
    eng->prev_mode = -1;
    eng->view_mode = MC_VIEW_FIRST_PERSON;
    eng->camera_distance = 4.0f;
    eng->last_third_person_dist = 4.0f;
    eng->eye_height = 1.62f;
    eng->camera_shake = 0.0f;
    eng->mouse_scale = 1.0f;
    eng->gui_scale = eng->mainhand_scale = eng->offhand_scale = 1.0f;
    eng->elytra_fov_mult = 1.0f;
    eng->rng = 0x1337BEEFu;
    eng->last_tick_ns = mc_now_ns();

    eng->hearts_half = 20;
    eng->max_hearts_half = 20;
    eng->hunger = 20;
    eng->saturation = 5.0f;
    eng->encounter_timer = 45;
    eng->random_mob_spawns = 1;
    eng->shaders_enabled = 1;
    eng->xp_level = 7;
    eng->xp_prog = 0.35f;

    setup_default_loadout(eng);
    load_base(eng);

    if (mc_assets_ready() == 0) {
        mc_assets_init(NULL);
    }
    eng->initialized = 1;
}

void mc_engine_note_camera(McEngine *eng, const float *cam84, int count) {
    if (!cam84 || count < 74) return;
    /* Some depth passes pass a shadow camera through the same NVN hook. */
    if (!(cam84[56] > 0.0f && cam84[70] > 0.1f && cam84[70] < (float)M_PI)) return;
    for (int i = 0; i < 56; i++) {
        if (!isfinite(cam84[i])) return;
    }
    memcpy(eng->cam_vp, cam84 + 12, sizeof(eng->cam_vp));
    eng->cam_pos[0] = cam84[47];
    eng->cam_pos[1] = cam84[51];
    eng->cam_pos[2] = cam84[55];
    eng->cam_right[0] = cam84[0]; eng->cam_right[1] = cam84[1]; eng->cam_right[2] = cam84[2];
    eng->cam_up[0]    = cam84[4]; eng->cam_up[1]    = cam84[5]; eng->cam_up[2]    = cam84[6];
    eng->cam_fwd[0]   = -cam84[8]; eng->cam_fwd[1]  = -cam84[9]; eng->cam_fwd[2]  = -cam84[10];
    eng->cam_near     = cam84[56];
    eng->cam_far      = cam84[57];
    eng->cam_aspect   = cam84[62];
    eng->cam_fov_rad  = cam84[70];
    eng->render_w     = cam84[72];
    eng->render_h     = cam84[73];
    eng->cam_valid    = 1;

    if (!eng->flight_look_active) {
        eng->look_yaw = atan2f(eng->cam_fwd[0], eng->cam_fwd[2]);
        eng->look_pitch = asinf(mc_clampf(eng->cam_fwd[1], -0.99f, 0.99f));
        eng->look_angles_init = 1;
    }
}

int mc_engine_wants_mouse_capture(const McEngine *eng) {
    return (eng->active && eng->gameplay_active && eng->mode == 1 && eng->inv_open == MC_INV_CLOSED) ? 1 : 0;
}

void mc_engine_mouse_move(McEngine *eng, float dx, float dy) {
    if (!mc_engine_wants_mouse_capture(eng) || !isfinite(dx) || !isfinite(dy)) return;
    dx *= eng->mouse_scale;
    dy *= eng->mouse_scale;
    if (eng->camera_orbit_until > 0.0f) {
        eng->camera_orbit_until = 0.0f;
        mc_totk_clear_camera_look();
    }
    if (eng->flight_look_active) {
        const float radians_per_dot = (float)M_PI / 1200.0f;
        eng->look_yaw = remainderf(eng->look_yaw - dx * radians_per_dot, 2.0f * (float)M_PI);
        eng->look_pitch = mc_clampf(eng->look_pitch - dy * radians_per_dot,
                                    -(float)M_PI * 0.495f, (float)M_PI * 0.495f);
        mc_engine_refresh_view(eng);
        return;
    }
    mc_totk_set_camera_look(dx, dy);
    eng->look_yaw -= dx * 0.0028f;
    eng->look_pitch = mc_clampf(eng->look_pitch - dy * 0.0028f, -1.52f, 1.52f);
}

void mc_engine_cursor_pos(McEngine *eng, float nx, float ny) {
    eng->cursor_nx = mc_clampf(nx, 0.0f, 1.0f);
    eng->cursor_ny = mc_clampf(ny, 0.0f, 1.0f);
}

/* =========================================================================
 * GUI Layout, Hit-Testing & Clicks (Survival Craft + Creative Picker)
 * ========================================================================= */

typedef struct {
    int   is_creative_catalog;
    int   catalog_index;
    McItemStack *slot_ptr;
    int   slot_kind; /* 0=normal, 1=armor(aux=0..3), 2=craft_out, 3=trash */
    int   armor_idx;
} GuiSlotRef;

static int collect_creative_catalog(const McEngine *eng, uint16_t *out_items, int max_out) {
    int count = 0;
    uint8_t tab_bit = 0;
    if (eng->creative_tab == MC_CTAB_BLOCKS) tab_bit = TAB_BLOCKS;
    else if (eng->creative_tab == MC_CTAB_COMBAT) tab_bit = TAB_COMBAT;
    else if (eng->creative_tab == MC_CTAB_TOOLS) tab_bit = TAB_TOOLS;
    else if (eng->creative_tab == MC_CTAB_FOOD) tab_bit = TAB_FOOD;
    else if (eng->creative_tab == MC_CTAB_MISC) tab_bit = TAB_MISC;

    for (int id = 1; id < MC_ITM_COUNT && count < max_out; id++) {
        if (eng->creative_tab == MC_CTAB_SEARCH) {
            if (eng->search_len > 0) {
                const char *nm = item_en_name((uint16_t)id);
                int match = 0;
                for (const char *p = nm; *p; p++) {
                    size_t j = 0;
                    while (j < (size_t)eng->search_len && p[j] &&
                           tolower((unsigned char)p[j]) == tolower((unsigned char)eng->search_text[j])) {
                        j++;
                    }
                    if (j == (size_t)eng->search_len) { match = 1; break; }
                }
                if (!match) continue;
            }
            out_items[count++] = (uint16_t)id;
        } else if (k_items[id].tab_mask & tab_bit) {
            out_items[count++] = (uint16_t)id;
        }
    }
    return count;
}

static int armor_slot_accepts(int armor_idx, uint16_t item_id) {
    if (item_id == 0) return 1;
    if (armor_idx == 0) return item_id == MC_ITM_DIAMOND_HELMET;
    if (armor_idx == 1) return item_id == MC_ITM_DIAMOND_CHESTPLATE || item_id == MC_ITM_ELYTRA;
    if (armor_idx == 2) return item_id == MC_ITM_DIAMOND_LEGGINGS;
    if (armor_idx == 3) return item_id == MC_ITM_DIAMOND_BOOTS;
    return 0;
}

static float gui_width(const McEngine *eng) {
    return MC_GUI_W / eng->gui_scale;
}

static float gui_height(const McEngine *eng) {
    return MC_GUI_H / eng->gui_scale;
}

static void station_panel_origin(const McEngine *eng, float *x, float *y) {
    *x = (gui_width(eng) - 232.0f) * 0.5f;
    *y = (gui_height(eng) - 196.0f) * 0.5f + 1.0f;
}

static int find_hovered_gui_target(McEngine *eng, float gx, float gy,
                                   GuiSlotRef *out_slot, float *out_sx, float *out_sy,
                                   int *out_clicked_tab, int *out_toggle_craft) {
    if (out_clicked_tab) *out_clicked_tab = -1;
    if (out_toggle_craft) *out_toggle_craft = 0;
    memset(out_slot, 0, sizeof(*out_slot));

    if (eng->inv_open == MC_INV_SURVIVAL) {
        float cx = (gui_width(eng) - 176.0f) * 0.5f;
        float cy = (gui_height(eng) - 166.0f) * 0.5f;
        /* Recipe / Creative Switch button at (cx + 104, cy + 61, 20, 18) */
        if (gx >= cx + 104 && gx < cx + 124 && gy >= cy + 61 && gy < cy + 79) {
            if (out_toggle_craft) *out_toggle_craft = 1;
            return 0;
        }
        /* Armor 4 */
        for (int a = 0; a < 4; a++) {
            float sx = cx + 8.0f, sy = cy + 8.0f + a * 18.0f;
            if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
                out_slot->slot_ptr = &eng->armor_slots[a];
                out_slot->slot_kind = 1; out_slot->armor_idx = a;
                *out_sx = sx; *out_sy = sy;
                return 1;
            }
        }
        /* Offhand */
        if (gx >= cx + 76 && gx < cx + 94 && gy >= cy + 61 && gy < cy + 79) {
            out_slot->slot_ptr = &eng->offhand;
            *out_sx = cx + 77.0f; *out_sy = cy + 62.0f;
            return 1;
        }
        /* 2x2 craft */
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 2; c++) {
                float sx = cx + 98.0f + c * 18.0f, sy = cy + 18.0f + r * 18.0f;
                if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
                    out_slot->slot_ptr = &eng->craft_slots[r * 2 + c];
                    *out_sx = sx; *out_sy = sy;
                    return 1;
                }
            }
        }
        /* Craft output */
        if (gx >= cx + 153 && gx < cx + 171 && gy >= cy + 27 && gy < cy + 45) {
            out_slot->slot_ptr = &eng->craft_result;
            out_slot->slot_kind = 2;
            *out_sx = cx + 154.0f; *out_sy = cy + 28.0f;
            return 1;
        }
        /* 3x9 backpack + 9 hotbar */
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 9; c++) {
                float sx = cx + 8.0f + c * 18.0f, sy = cy + 84.0f + r * 18.0f;
                if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
                    out_slot->slot_ptr = &eng->inv_slots[r * 9 + c];
                    *out_sx = sx; *out_sy = sy;
                    return 1;
                }
            }
        }
        for (int c = 0; c < 9; c++) {
            float sx = cx + 8.0f + c * 18.0f, sy = cy + 142.0f;
            if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
                out_slot->slot_ptr = &eng->hotbar[c];
                *out_sx = sx; *out_sy = sy;
                return 1;
            }
        }
        return 0;
    }

    /* MC_INV_CREATIVE (195x136) */
    float cx = (gui_width(eng) - 195.0f) * 0.5f;
    float cy = (gui_height(eng) - 136.0f) * 0.5f;

    /* Top tabs (0..4) + Search (5) */
    for (int t = 0; t < 5; t++) {
        float tx = cx + t * 27.0f, ty = cy - 28.0f;
        if (gx >= tx && gx < tx + 26.0f && gy >= ty && gy < ty + 28.0f) {
            if (out_clicked_tab) *out_clicked_tab = t;
            return 0;
        }
    }
    if (gx >= cx + 169.0f && gx < cx + 195.0f && gy >= cy - 28.0f && gy < cy) {
        if (out_clicked_tab) *out_clicked_tab = MC_CTAB_SEARCH;
        return 0;
    }
    /* Bottom-left Crafting switch tab + Bottom-right Survival Inventory tab */
    if (gx >= cx && gx < cx + 26.0f && gy >= cy + 132.0f && gy < cy + 164.0f) {
        if (out_toggle_craft) *out_toggle_craft = 1;
        return 0;
    }
    if (gx >= cx + 169.0f && gx < cx + 195.0f && gy >= cy + 132.0f && gy < cy + 164.0f) {
        if (out_clicked_tab) *out_clicked_tab = MC_CTAB_SURV_INV;
        return 0;
    }

    /* Hotbar (common at y + 112) */
    for (int c = 0; c < 9; c++) {
        float sx = cx + 9.0f + c * 18.0f, sy = cy + 112.0f;
        if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
            out_slot->slot_ptr = &eng->hotbar[c];
            *out_sx = sx; *out_sy = sy;
            return 1;
        }
    }

    if (eng->creative_tab == MC_CTAB_SURV_INV) {
        static const float ax[4] = {54.0f, 54.0f, 108.0f, 108.0f};
        static const float ay[4] = {6.0f, 33.0f, 6.0f, 33.0f};
        for (int a = 0; a < 4; a++) {
            float sx = cx + ax[a], sy = cy + ay[a];
            if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
                out_slot->slot_ptr = &eng->armor_slots[a];
                out_slot->slot_kind = 1; out_slot->armor_idx = a;
                *out_sx = sx; *out_sy = sy;
                return 1;
            }
        }
        if (gx >= cx + 34 && gx < cx + 52 && gy >= cy + 19 && gy < cy + 37) {
            out_slot->slot_ptr = &eng->offhand;
            *out_sx = cx + 35.0f; *out_sy = cy + 20.0f;
            return 1;
        }
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 9; c++) {
                float sx = cx + 9.0f + c * 18.0f, sy = cy + 54.0f + r * 18.0f;
                if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
                    out_slot->slot_ptr = &eng->inv_slots[r * 9 + c];
                    *out_sx = sx; *out_sy = sy;
                    return 1;
                }
            }
        }
        /* Destroy trash slot at (173, 112) */
        if (gx >= cx + 172 && gx < cx + 190 && gy >= cy + 111 && gy < cy + 129) {
            out_slot->slot_kind = 3;
            *out_sx = cx + 173.0f; *out_sy = cy + 112.0f;
            return 1;
        }
    } else {
        uint16_t cat[MC_ITM_COUNT];
        int total = collect_creative_catalog(eng, cat, MC_ITM_COUNT);
        int rows = (total + 8) / 9;
        int max_row = rows > 5 ? rows - 5 : 0;
        int start_row = (int)roundf(eng->creative_scroll * (float)max_row);
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 9; c++) {
                float sx = cx + 9.0f + c * 18.0f, sy = cy + 18.0f + r * 18.0f;
                if (gx >= sx - 1 && gx < sx + 17 && gy >= sy - 1 && gy < sy + 17) {
                    int idx = (start_row + r) * 9 + c;
                    if (idx < total) {
                        out_slot->is_creative_catalog = 1;
                        out_slot->catalog_index = cat[idx];
                        *out_sx = sx; *out_sy = sy;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

static void handle_inventory_click(McEngine *eng, int button) {
    float gx = eng->cursor_nx * gui_width(eng);
    float gy = eng->cursor_ny * gui_height(eng);
    if (eng->inv_open == MC_INV_STATION) {
        float x, y;
        station_panel_origin(eng, &x, &y);
        gx -= x;
        gy -= y;
        if (button == 0 && gx >= 6 && gx < 226 && gy >= 27 && gy < 167)
            eng->pending_recipe = 1 + (int)((gy - 27) / 20);
        return;
    }
    GuiSlotRef ref;
    float sx = 0, sy = 0;
    int clicked_tab = -1, toggle_craft = 0;

    int hit = find_hovered_gui_target(eng, gx, gy, &ref, &sx, &sy, &clicked_tab, &toggle_craft);
    if (toggle_craft) {
        eng->inv_open = (eng->inv_open == MC_INV_SURVIVAL) ? MC_INV_CREATIVE : MC_INV_SURVIVAL;
        play_sfx_2d(MC_SFX_UI_CLICK, 0.7f, 1.0f);
        return;
    }
    if (clicked_tab >= 0 && clicked_tab < MC_CTAB_COUNT) {
        eng->creative_tab = clicked_tab;
        eng->creative_scroll = 0.0f;
        play_sfx_2d(MC_SFX_UI_CLICK, 0.7f, 1.0f);
        return;
    }
    if (!hit) return;

    play_sfx_2d(MC_SFX_UI_CLICK, 0.65f, 1.05f);
    if (ref.slot_kind == 3) { /* Trash slot */
        memset(&eng->cursor_stack, 0, sizeof(eng->cursor_stack));
        return;
    }
    if (ref.is_creative_catalog) {
        uint16_t id = (uint16_t)ref.catalog_index;
        int take = (button == 1) ? 1 : (eng->key_down[KVK_SHIFT] ? k_items[id].max_stack : k_items[id].max_stack);
        if (eng->cursor_stack.item_id == id && eng->cursor_stack.count < k_items[id].max_stack) {
            int add = button == 1 ? 1 : (k_items[id].max_stack - eng->cursor_stack.count);
            eng->cursor_stack.count = (uint8_t)(eng->cursor_stack.count + add);
        } else if (eng->cursor_stack.item_id == 0) {
            eng->cursor_stack = make_stack(id, take);
        } else if (eng->cursor_stack.item_id == id) {
            eng->cursor_stack.count = (uint8_t)k_items[id].max_stack;
        } else {
            memset(&eng->cursor_stack, 0, sizeof(eng->cursor_stack));
        }
        return;
    }
    if (ref.slot_kind == 2) { /* 2x2 craft result */
        if (eng->craft_result.item_id == 0) return;
        if (eng->cursor_stack.item_id == 0) {
            eng->cursor_stack = eng->craft_result;
            consume_craft_ingredients(eng);
        } else if (eng->cursor_stack.item_id == eng->craft_result.item_id &&
                   eng->cursor_stack.count + eng->craft_result.count <= k_items[eng->craft_result.item_id].max_stack) {
            eng->cursor_stack.count += eng->craft_result.count;
            consume_craft_ingredients(eng);
        }
        return;
    }

    McItemStack *dst = ref.slot_ptr;
    if (!dst) return;
    if (ref.slot_kind == 1 && eng->cursor_stack.item_id != 0 &&
        !armor_slot_accepts(ref.armor_idx, eng->cursor_stack.item_id)) {
        return;
    }

    if (button == 1) { /* Right-click: split or place 1 */
        if (eng->cursor_stack.item_id == 0 && dst->item_id != 0) {
            int half = (dst->count + 1) / 2;
            eng->cursor_stack = *dst;
            eng->cursor_stack.count = (uint8_t)half;
            dst->count -= (uint8_t)half;
            if (dst->count == 0) memset(dst, 0, sizeof(*dst));
        } else if (eng->cursor_stack.item_id != 0) {
            if (dst->item_id == 0) {
                *dst = eng->cursor_stack;
                dst->count = 1;
                eng->cursor_stack.count--;
                if (eng->cursor_stack.count == 0) memset(&eng->cursor_stack, 0, sizeof(McItemStack));
            } else if (dst->item_id == eng->cursor_stack.item_id && dst->count < k_items[dst->item_id].max_stack) {
                dst->count++;
                eng->cursor_stack.count--;
                if (eng->cursor_stack.count == 0) memset(&eng->cursor_stack, 0, sizeof(McItemStack));
            }
        }
    } else { /* Left-click: pick up / merge / swap */
        if (eng->cursor_stack.item_id != 0 && dst->item_id == eng->cursor_stack.item_id &&
            dst->enchanted == eng->cursor_stack.enchanted &&
            dst->count < k_items[dst->item_id].max_stack) {
            int room = k_items[dst->item_id].max_stack - dst->count;
            int mv = eng->cursor_stack.count < room ? eng->cursor_stack.count : room;
            dst->count += (uint8_t)mv;
            eng->cursor_stack.count -= (uint8_t)mv;
            if (eng->cursor_stack.count == 0) memset(&eng->cursor_stack, 0, sizeof(McItemStack));
        } else {
            McItemStack tmp = *dst;
            *dst = eng->cursor_stack;
            eng->cursor_stack = tmp;
        }
    }
    if (ref.slot_kind == 1) recompute_armor(eng);
    if (eng->inv_open == MC_INV_SURVIVAL) update_craft_result(eng);
}

void mc_engine_mouse_button(McEngine *eng, int button, int down) {
    if (!eng->active || !eng->gameplay_active || eng->mode != 1 || button < 0 || button >= 3) return;

    if (eng->inv_open != MC_INV_CLOSED) {
        if (down && (button == 0 || button == 1)) {
            handle_inventory_click(eng, button);
        }
        return;
    }

    eng->mouse_down[button] = (uint8_t)(down ? 1 : 0);
    if (down) {
        eng->mouse_clicked[button] = 1;
        if (button == 0) eng->pending_left_press = 1;
        else if (button == 1) eng->pending_right_press = 1;
    } else {
        if (button == 1 && eng->bow_drawing) {
            if (eng->hotbar[eng->selected_slot].item_id == MC_ITM_BOW) {
                eng->pending_bow_fire = 1;
                eng->pending_bow_charge_sec = eng->bow_charge_sec;
            }
            eng->bow_drawing = 0;
            eng->bow_charge_sec = 0.0f;
        }
        if (button == 1) {
            eng->eating = 0;
            eng->eat_timer = 0.0f;
        }
    }
}

void mc_engine_scroll(McEngine *eng, float delta_y) {
    if (!eng->active || !eng->gameplay_active || eng->mode != 1) return;
    if (eng->inv_open == MC_INV_CREATIVE) {
        eng->creative_scroll = mc_clampf(eng->creative_scroll - delta_y * 0.12f, 0.0f, 1.0f);
        return;
    }
    if (eng->inv_open != MC_INV_CLOSED) return;
    static float accum = 0.0f;
    accum += delta_y;
    int step = 0;
    while (accum >= 1.0f) { step--; accum -= 1.0f; }
    while (accum <= -1.0f) { step++; accum += 1.0f; }
    if (step != 0) {
        eng->selected_slot = (eng->selected_slot + step % 9 + 9) % 9;
        eng->bow_drawing = 0; eng->bow_charge_sec = 0.0f;
        eng->eating = 0; eng->eat_timer = 0.0f;
        eng->equip_drop = 0.9f;
        show_tooltip(eng, item_en_name(eng->hotbar[eng->selected_slot].item_id));
    }
}

static void start_flight_look(McEngine *eng);

/* Direct view-matrix override (`flight_look_active`) is used ONLY during Elytra gliding
 * and 3rd-person front view. Normal ground walking in 1st-person and 3rd-person back
 * uses native 1:1 mouse_direct.c on TotK's PlayerCamera BEFORE CPU frustum culling. */
static int wants_flight_look(const McEngine *eng) {
    return eng->flight_camera_available &&
           (eng->elytra_gliding || eng->view_mode == MC_VIEW_THIRD_FRONT);
}

static void set_view_mode(McEngine *eng, int view_mode) {
    eng->view_mode = view_mode;
    eng->camera_orbit_until = 0.0f;
    if (wants_flight_look(eng)) {
        if (eng->player_pos_valid) {
            if (!eng->flight_look_active) start_flight_look(eng);
            mc_engine_refresh_view(eng);
        }
    } else {
        eng->flight_look_active = 0;
        eng->flight_view_ready = 0;
    }
    mc_totk_clear_camera_look();
}

static void set_mode_internal(McEngine *eng, int new_mode) {
    new_mode = new_mode ? 1 : 0;
    if (eng->mode == new_mode) return;
    eng->mode = new_mode;
    eng->camera_orbit_until = 0.0f;
    eng->inv_open = MC_INV_CLOSED;
    eng->bow_drawing = 0;
    eng->eating = 0;
    eng->shield_block = 0.0f;
    eng->elytra_gliding = 0;
    eng->flight_look_active = 0;
    eng->flight_view_ready = 0;
    eng->look_angles_init = 0;
    eng->body_yaw_init = 0;
    eng->totk_cam_valid = 0;
    eng->eye_height = 1.62f;
    eng->elytra_open = 0.0f;
    eng->elytra_pitch = 0.0f;
    eng->elytra_bank = 0.0f;
    eng->elytra_flight_time = 0.0f;
    eng->elytra_fov_mult = 1.0f;
    eng->elytra_boost_timer = 0.0f;
    eng->elytra_jump_until = 0.0f;
    eng->elytra_takeoff_until = 0.0f;
    eng->pending_left_press = 0;
    eng->pending_right_press = 0;
    eng->pending_jump_press = 0;
    eng->pending_bow_fire = 0;
    eng->bow_raise = eng->bow_release = 0.0f;
    eng->pending_spawn_count = 0;
    eng->pending_break_target = 0;
    eng->pending_ignite_tnt = 0;
    eng->pending_log_actors = 0;
    eng->pending_log_mobs = 0;
    memset(eng->mouse_down, 0, sizeof(eng->mouse_down));
    mc_audio_set_elytra_glide(0, 0.0f);
    mc_totk_clear_camera_look();
    runtime_input_clear();
    play_sfx_2d(MC_SFX_UI_CLICK, 0.7f, new_mode ? 1.1f : 0.85f);
}

int mc_engine_key(McEngine *eng, unsigned code, int down) {
    uint8_t *consume_up = eng->consume_key_up;
#ifdef _WIN32
    if (code == 0xA0u || code == 0xA1u) code = KVK_SHIFT;
    else if (code == 0xA2u || code == 0xA3u) code = KVK_CONTROL;
#endif
    if (!down) {
        if (code == KVK_W) eng->sprint_held = 0;
        if (code < 512) eng->key_down[code] = 0;
        if (code < 512 && consume_up[code]) {
            consume_up[code] = 0;
            return 1;
        }
        return (eng->active && eng->gameplay_active && eng->mode == 1 && eng->inv_open != MC_INV_CLOSED) ? 1 : 0;
    }

    if (!eng->active || !eng->gameplay_active) return 0;

    int prev = (code < 512) ? eng->key_down[code] : 0;
    if (code < 512) eng->key_down[code] = 1;

    int edge = !prev;
    if (code == KVK_GRAVE || code == KVK_F6) {
        if (edge) set_mode_internal(eng, !eng->mode);
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    if (eng->mode != 1) return 0;

    if (code == KVK_W && edge && eng->inv_open == MC_INV_CLOSED) {
        const uint64_t double_tap_ns = 350000000ull;
        uint64_t now = plat_now_ns();
        eng->sprint_held = eng->forward_tap_ns && now - eng->forward_tap_ns <= double_tap_ns;
        eng->forward_tap_ns = eng->sprint_held ? 0 : now;
    }

    if (eng->inv_open != MC_INV_CLOSED) {
        if (edge && code == KVK_ESCAPE) {
            if (eng->cursor_stack.item_id != 0) try_give_stack(eng, &eng->cursor_stack);
            eng->inv_open = MC_INV_CLOSED;
            play_sfx_2d(MC_SFX_UI_CLICK, 0.65f, 0.9f);
        } else if (edge && code == KVK_E &&
                   !(eng->inv_open == MC_INV_CREATIVE && eng->creative_tab == MC_CTAB_SEARCH)) {
            if (eng->cursor_stack.item_id != 0) try_give_stack(eng, &eng->cursor_stack);
            eng->inv_open = MC_INV_CLOSED;
            play_sfx_2d(MC_SFX_UI_CLICK, 0.65f, 0.9f);
        } else if (eng->inv_open == MC_INV_CREATIVE && eng->creative_tab == MC_CTAB_SEARCH) {
            if (code == KVK_BACKSPACE && eng->search_len > 0) {
                eng->search_text[--eng->search_len] = '\0';
                eng->creative_scroll = 0.0f;
            } else if (code < 128 && k_key_to_ascii[code] && eng->search_len < 44) {
                eng->search_text[eng->search_len++] = (char)k_key_to_ascii[code];
                eng->search_text[eng->search_len] = '\0';
                eng->creative_scroll = 0.0f;
            }
        }
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    if (edge && (code == KVK_F5 || code == KVK_V)) {
        set_view_mode(eng, (eng->view_mode + 1) % 3);
        play_sfx_2d(MC_SFX_UI_CLICK, 0.6f, 1.0f);
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    if (edge && code == KVK_E) {
        eng->inv_open = MC_INV_CREATIVE;
        eng->bow_drawing = 0; eng->eating = 0; eng->shield_block = 0.0f;
        memset(eng->mouse_down, 0, sizeof(eng->mouse_down));
        mc_totk_clear_camera_look();
        runtime_input_clear();
        play_sfx_2d(MC_SFX_UI_CLICK, 0.7f, 1.0f);
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    if (edge && code == KVK_F) {
        McItemStack tmp = eng->hotbar[eng->selected_slot];
        eng->hotbar[eng->selected_slot] = eng->offhand;
        eng->offhand = tmp;
        eng->equip_drop = 0.8f;
        show_tooltip(eng, item_en_name(eng->hotbar[eng->selected_slot].item_id));
        play_sfx_2d(MC_SFX_UI_CLICK, 0.55f, 1.15f);
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    if (edge && code == KVK_G) {
        toggle_elytra_and_totem(eng);
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    if (code == KVK_SPACE) {
        if (edge) eng->pending_jump_press = 1;
        consume_up[code] = 1;
        return 1;
    }

    if (edge && code == KVK_Q) {
        McItemStack *h = &eng->hotbar[eng->selected_slot];
        if (h->item_id != 0 && h->count > 0) {
            int drop_n = eng->key_down[KVK_SHIFT] ? h->count : 1;
            float orig[3], dir[3];
            get_view_origin_dir(eng, orig, dir);
            float dp[3] = {orig[0] + dir[0] * 0.8f, orig[1] - 0.1f, orig[2] + dir[2] * 0.8f};
            float dv[3] = {dir[0] * 5.0f, dir[1] * 5.0f + 1.5f, dir[2] * 5.0f};
            spawn_dropped_item(eng, h->item_id, drop_n, dp, dv);
            h->count -= (uint8_t)drop_n;
            if (h->count == 0) memset(h, 0, sizeof(*h));
            play_sfx_2d(MC_SFX_ITEM_POP, 0.6f, 0.9f);
        }
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    if (edge && code == KVK_N) {
        if (eng->pending_spawn_count < 16) {
            eng->pending_spawns[eng->pending_spawn_count++] = -1;
        }
        if (code < 512) consume_up[code] = 1;
        return 1;
    }

    for (int i = 0; i < 9; i++) {
        if (code == k_hotbar_keys[i]) {
            if (edge && eng->selected_slot != i) {
                eng->selected_slot = i;
                eng->equip_drop = 0.85f;
                eng->bow_drawing = 0; eng->bow_charge_sec = 0.0f;
                eng->eating = 0; eng->eat_timer = 0.0f;
            }
            show_tooltip(eng, item_en_name(eng->hotbar[eng->selected_slot].item_id));
            if (code < 512) consume_up[code] = 1;
            return 1;
        }
    }
    return 0;
}

int mc_engine_command(McEngine *eng, const char *line) {
    if (!line || !line[0]) return 0;
    while (*line == ' ' || *line == '\t') line++;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", line);
    size_t blen = strlen(buf);
    while (blen > 0 && (buf[blen - 1] == '\n' || buf[blen - 1] == '\r' ||
                        buf[blen - 1] == ' ' || buf[blen - 1] == '\t')) {
        buf[--blen] = '\0';
    }
    if (strncmp(buf, "mc ", 3) != 0 && strcmp(buf, "mc") != 0) return 0;
    const char *arg = buf + (buf[2] == ' ' ? 3 : 2);
    while (*arg == ' ') arg++;

    if (strcmp(arg, "on") == 0) { set_mode_internal(eng, 1); return 1; }
    if (strcmp(arg, "off") == 0) { set_mode_internal(eng, 0); return 1; }
    if (strcmp(arg, "mode") == 0) { set_mode_internal(eng, !eng->mode); return 1; }
    if (strncmp(arg, "view ", 5) == 0) {
        int view = atoi(arg + 5);
        if (view >= MC_VIEW_FIRST_PERSON && view <= MC_VIEW_THIRD_FRONT) set_view_mode(eng, view);
        return 1;
    }
    if (strncmp(arg, "slot ", 5) == 0) {
        int s = atoi(arg + 5);
        if (s >= 0 && s < 9 && eng->selected_slot != s) {
            eng->selected_slot = s;
            eng->equip_drop = 0.85f;
            eng->bow_drawing = 0; eng->bow_charge_sec = 0.0f;
            eng->eating = 0; eng->eat_timer = 0.0f;
        }
        show_tooltip(eng, item_en_name(eng->hotbar[eng->selected_slot].item_id));
        return 1;
    }
    if (strcmp(arg, "inv") == 0) {
        mc_engine_key(eng, KVK_E, 1);
        mc_engine_key(eng, KVK_E, 0);
        return 1;
    }
    if (strcmp(arg, "elytra") == 0) { toggle_elytra_and_totem(eng); return 1; }
    if (strncmp(arg, "look ", 5) == 0) {
        float dx = 0.0f, dy = 0.0f;
        if (sscanf(arg + 5, "%f %f", &dx, &dy) >= 1) {
            mc_engine_mouse_move(eng, dx, dy);
        }
        return 1;
    }
    if (strncmp(arg, "cursor ", 7) == 0) {
        float nx = 0.5f, ny = 0.5f;
        if (sscanf(arg + 7, "%f %f", &nx, &ny) == 2) {
            mc_engine_cursor_pos(eng, nx, ny);
        }
        return 1;
    }
    if (strcmp(arg, "click") == 0 || strncmp(arg, "click ", 6) == 0) {
        int btn = (arg[5] == ' ') ? atoi(arg + 6) : 0;
        if (btn < 0 || btn > 2) btn = 0;
        mc_engine_mouse_button(eng, btn, 1);
        mc_engine_mouse_button(eng, btn, 0);
        return 1;
    }
    if (strcmp(arg, "hold") == 0 || strncmp(arg, "hold ", 5) == 0) {
        int btn = (arg[4] == ' ') ? atoi(arg + 5) : 0;
        if (btn < 0 || btn > 2) btn = 0;
        mc_engine_mouse_button(eng, btn, 1);
        return 1;
    }
    if (strcmp(arg, "release") == 0 || strncmp(arg, "release ", 8) == 0) {
        int btn = (arg[7] == ' ') ? atoi(arg + 8) : 0;
        if (btn < 0 || btn > 2) btn = 0;
        mc_engine_mouse_button(eng, btn, 0);
        return 1;
    }
    if (strcmp(arg, "attack") == 0) {
        mc_engine_mouse_button(eng, 0, 1);
        mc_engine_mouse_button(eng, 0, 0);
        return 1;
    }
    if (strcmp(arg, "use") == 0 || strcmp(arg, "place") == 0) {
        mc_engine_mouse_button(eng, 1, 1);
        mc_engine_mouse_button(eng, 1, 0);
        return 1;
    }
    if (strcmp(arg, "break") == 0) {
        eng->pending_break_target = 1;
        return 1;
    }
    if (strcmp(arg, "ignite") == 0) {
        eng->pending_ignite_tnt = 1;
        return 1;
    }
    if (strcmp(arg, "actors") == 0) {
        eng->pending_log_actors = 1;
        return 1;
    }
    if (strcmp(arg, "mobs") == 0) {
        int active_cnt = 0;
        for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
            if (eng->mobs[i].active) active_cnt++;
        }
        fprintf(stderr, "[mc-mob] dump req active=%d player=(%.2f,%.2f,%.2f) mode=%d\n",
                active_cnt, eng->player_pos[0], eng->player_pos[1], eng->player_pos[2],
                (int)eng->mode);
        for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
            const McMob *m = &eng->mobs[i];
            if (!m->active) continue;
            fprintf(stderr, "  mob[%d] kind=%d pos=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f,%.2f) gr=%d swell=%.2f hp=%.1f\n",
                    i, (int)m->kind, m->x, m->y, m->z, m->vx, m->vy, m->vz,
                    (int)m->grounded, m->swell, m->hp);
        }
        eng->pending_log_mobs = 1;
        return 1;
    }
    if (strcmp(arg, "shoot") == 0 || strcmp(arg, "bow") == 0) {
        eng->pending_bow_fire = 1;
        eng->pending_bow_charge_sec = 1.0f;
        return 1;
    }
    if (strncmp(arg, "key ", 4) == 0) {
        unsigned code = (unsigned)strtoul(arg + 4, NULL, 0);
        mc_engine_key(eng, code, 1);
        mc_engine_key(eng, code, 0);
        return 1;
    }
    if (strcmp(arg, "clear") == 0) {
        eng->base_dirty = 1;
        eng->block_count = 0;
        eng->pending_spawn_count = 0;
        memset(eng->mobs, 0, sizeof(eng->mobs));
        memset(eng->primed_tnt, 0, sizeof(eng->primed_tnt));
        memset(eng->projectiles, 0, sizeof(eng->projectiles));
        return 1;
    }
    if (strcmp(arg, "spawn") == 0 || strncmp(arg, "spawn ", 6) == 0) {
        const char *what = (arg[5] == ' ') ? (arg + 6) : "";
        int16_t kind = -1;
        if (strstr(what, "creeper")) kind = MC_MOB_CREEPER;
        else if (strstr(what, "zombie")) kind = MC_MOB_ZOMBIE;
        else if (strstr(what, "skeleton")) kind = MC_MOB_SKELETON;
        if (eng->pending_spawn_count < 16) {
            eng->pending_spawns[eng->pending_spawn_count++] = kind;
        }
        return 1;
    }
    return 1;
}

/* =========================================================================
 * Main Guest-Thread Frame Tick
 * ========================================================================= */

static int consume_elytra_rocket(McEngine *eng) {
    if (!eng->pending_right_press || eng->inv_open != MC_INV_CLOSED) return 0;
    McItemStack *held = &eng->hotbar[eng->selected_slot];
    McItemStack *rocket = NULL;
    if (held->item_id == MC_ITM_FIREWORK_ROCKET && held->count > 0) {
        rocket = held;
    } else if (eng->offhand.item_id == MC_ITM_FIREWORK_ROCKET && eng->offhand.count > 0 &&
               (held->item_id == MC_ITM_NONE ||
                (!k_items[held->item_id].is_food && held->item_id != MC_ITM_BOW &&
                 held->item_id != MC_ITM_SHIELD && held->item_id != MC_ITM_ENDER_PEARL))) {
        rocket = &eng->offhand;
    }
    if (!rocket) return 0;

    eng->pending_right_press = 0;
    if (--rocket->count == 0) memset(rocket, 0, sizeof(*rocket));
    eng->elytra_boost_timer = 1.25f;
    eng->elytra_boost_trail_accum = 0.0f;
    eng->swing_timer = eng->swing_dur = 0.22f;
    play_sfx_2d(MC_SFX_FIREWORK_LAUNCH, 1.0f, 0.96f + 0.08f * mc_randf(eng));
    return 1;
}

/* Authentic Minecraft Java LivingEntity.travelFallFlying() + FireworkRocketEntity.tick()
 * integrated in fractional 20 Hz MC ticks (sub-stepped at <= 0.25 MC ticks) for clean
 * frame-rate independence across 30-180+ FPS.
 * Units: 1 MC block/tick = 20.0 m/s, MC gravity = 0.08 blocks/tick^2. */
static void step_elytra_physics(float vel_mps[3], const float aim[3],
                                int boosting, int takeoff, float dt) {
    float lx = aim[0], ly = aim[1], lz = aim[2];
    /* A level rocket fired immediately off a ground jump needs clearance from Havok CCT. */
    if (boosting && takeoff && ly >= -0.15f && ly < 0.25f) {
        ly = 0.25f;
    }
    float llen = hypotf(hypotf(lx, lz), ly);
    if (llen < 1e-5f) {
        lx = 0.0f; ly = 0.0f; lz = 1.0f;
    } else {
        lx /= llen; ly /= llen; lz /= llen;
    }

    const float look_h = hypotf(lx, lz);
    const float pitch = atan2f(ly, look_h); /* >0 climbing, <0 diving */
    const float f2 = look_h * look_h;       /* cos(pitch)^2 */
    const float sin_p = sinf(pitch);

    float vx = vel_mps[0] * 0.05f;
    float vy = vel_mps[1] * 0.05f;
    float vz = vel_mps[2] * 0.05f;

    float total_ticks = dt * 20.0f;
    int steps = (int)ceilf(total_ticks / 0.25f);
    if (steps < 1) steps = 1;
    if (steps > 8) steps = 8;
    float h = total_ticks / (float)steps;

    for (int s = 0; s < steps; s++) {
        if (boosting) {
            /* Exact fractional-tick integration of MC Java FireworkRocketEntity.tick():
             * v += look * 0.1 + (look * 1.5 - v) * 0.5  <=>  v += 0.5 * (1.7 * look - v). */
            float k = 1.0f - powf(0.5f, h);
            vx += k * (1.7f * lx - vx);
            vy += k * (1.7f * ly - vy);
            vz += k * (1.7f * lz - vz);
        }

        const float vel_h = hypotf(vx, vz);

        /* MC gravity (0.08 b/t^2) reduced by 75% * cos(pitch)^2 when gliding horizontally. */
        vy += 0.08f * (-1.0f + f2 * 0.75f) * h;

        /* Descending glide conversion: vertical fall speed converts into forward horizontal speed. */
        if (vy < 0.0f && look_h > 1e-4f) {
            float d2 = (-vy) * (1.0f - powf(1.0f - 0.1f * f2, h));
            vy += d2;
            vx += (lx / look_h) * d2;
            vz += (lz / look_h) * d2;
        }

        /* Pulling up / swoop climb: looking above horizon converts horizontal speed into 3.2x lift. */
        if (pitch > 0.0f && look_h > 1e-4f) {
            float d1 = vel_h * sin_p * 0.04f * h;
            vy += d1 * 3.2f;
            vx -= (lx / look_h) * d1;
            vz -= (lz / look_h) * d1;
        }

        /* Smooth aerodynamic turning: horizontal velocity curves smoothly toward look_xz. */
        if (look_h > 1e-4f) {
            float steer_h = 1.0f - powf(0.9f, h);
            vx += ((lx / look_h) * vel_h - vx) * steer_h;
            vz += ((lz / look_h) * vel_h - vz) * steer_h;
        }

        /* Authentic Minecraft air drag: 0.99 XZ, 0.98 Y per 20 Hz tick. */
        float drag_xz = powf(0.99f, h);
        float drag_y = powf(0.98f, h);
        vx *= drag_xz;
        vy *= drag_y;
        vz *= drag_xz;
    }

    vel_mps[0] = vx * 20.0f;
    vel_mps[1] = vy * 20.0f;
    vel_mps[2] = vz * 20.0f;

    /* Clamp extreme terminal dive speed to keep Hyrule terrain streaming & Havok CCT stable. */
    float spd = hypotf(hypotf(vel_mps[0], vel_mps[2]), vel_mps[1]);
    if (spd > 75.0f) {
        float sc = 75.0f / spd;
        vel_mps[0] *= sc;
        vel_mps[1] *= sc;
        vel_mps[2] *= sc;
    }
}

/* Activates direct 1:1 raw-mouse Minecraft camera control for ALL Minecraft gameplay
 * (walking, sprinting, jumping, and Elytra flight) across all 3 views, initializing
 * seamlessly from TotK's live pre-override camera so there is zero snap. */
static void start_flight_look(McEngine *eng) {
    if (!eng->flight_camera_available || eng->flight_look_active) return;
    float native_yaw, native_pitch;
    if (eng->totk_cam_valid) {
        native_yaw = eng->totk_cam_yaw;
        native_pitch = eng->totk_cam_pitch;
    } else if (eng->cam_valid) {
        native_yaw = atan2f(eng->cam_fwd[0], eng->cam_fwd[2]);
        native_pitch = asinf(mc_clampf(eng->cam_fwd[1], -1.0f, 1.0f));
    } else {
        native_yaw = eng->player_totk_yaw;
        native_pitch = 0.0f;
    }

    const float lim = (float)M_PI * 0.495f;
    eng->look_yaw = remainderf(native_yaw, 2.0f * (float)M_PI);
    eng->look_pitch = mc_clampf(native_pitch, -lim, lim);
    eng->look_angles_init = 1;
    eng->flight_look_active = 1;
    eng->camera_orbit_until = 0.0f;
    if (!eng->body_yaw_init) {
        eng->body_yaw = eng->player_totk_yaw_valid ? eng->player_totk_yaw : eng->look_yaw;
        eng->head_yaw = eng->look_yaw;
        eng->body_yaw_init = 1;
    }
    mc_totk_clear_camera_look();
}

static float third_person_distance(McEngine *eng, const float pivot[3], const float behind[3]) {
    float distance = mc_totk_camera_clearance(pivot, behind, eng->camera_distance);
    int32_t ix, iy, iz;
    int face;
    float hit;
    if (raycast_blocks(eng, pivot, behind, distance, &ix, &iy, &iz, &face, &hit)) {
        distance = fmaxf(0.15f, hit - 0.20f);
    }

    return distance;
}

/* Guest-frame-thread only: computes terrain+block-clipped 3rd-person clearance and
 * updates eng->flight_view for 1st-person, 3rd-person back, or 3rd-person front. */
static void update_flight_view(McEngine *eng) {
    float dist = eng->camera_distance;
    if (eng->view_mode != MC_VIEW_FIRST_PERSON) {
        float fwd[3];
        flight_look_direction(eng, fwd);
        float sign = eng->view_mode == MC_VIEW_THIRD_FRONT ? -1.0f : 1.0f;
        float behind[3] = {-fwd[0] * sign, -fwd[1] * sign, -fwd[2] * sign};
        float open = mc_clampf(eng->elytra_open, 0.0f, 1.0f);
        float t = open * open * (3.0f - 2.0f * open);
        float eh = eng->eye_height > 0.1f ? eng->eye_height : 1.62f;
        float pivot_y = eh + 0.10f * t;
        const float pivot[3] = {eng->player_pos[0], eng->player_pos[1] + pivot_y, eng->player_pos[2]};
        dist = third_person_distance(eng, pivot, behind);
        eng->last_third_person_dist = dist;
    }
    build_flight_view_matrix(eng, dist);
}

void mc_engine_game_tick(McEngine *eng, uint64_t frame) {
    if (!eng->initialized) mc_engine_init(eng);
    uint64_t now = mc_now_ns();
    float dt = (eng->last_tick_ns > 0) ? (float)(now - eng->last_tick_ns) * 1e-9f : (1.0f / 60.0f);
    eng->last_tick_ns = now;
    dt = mc_clampf(dt, 0.001f, 0.05f);
    eng->tick_count++;
    eng->time_sec += dt;

    McTotkPlayerState ps;
    memset(&ps, 0, sizeof(ps));
    int gp_ready = mc_totk_is_gameplay_active();
    int has_player = gp_ready && mc_totk_read_player(&ps) && ps.valid;
    int was_gp = eng->gameplay_active;
    eng->gameplay_active = has_player ? 1 : 0;
    eng->in_hyrule_world = eng->gameplay_active;
    if (!eng->active || !eng->gameplay_active) {
        eng->mining_swing_timer = 0.0f;
        McTotkIncomingDamage unused;
        mc_totk_poll_incoming_damage(0, &unused);
        if (was_gp && eng->prev_mode == 1) {
            fprintf(stderr, "[mc] pausing Minecraft gameplay at frame %llu (%s)\n",
                    (unsigned long long)frame, gp_ready ? "player state unavailable" : "gameplay gate closed");
            mc_totk_set_player_visible(1);
            mc_totk_set_hud_visible(1);
            fprintf(stderr, "[mc] visibility cleanup finished at frame %llu\n", (unsigned long long)frame);
        }
        eng->prev_mode = -1;
        eng->player_pos_valid = 0;
        eng->camera_orbit_until = 0.0f;
        eng->player_was_grounded = 0;
        eng->jump_ascent_speed = 0.0f;
        eng->airborne_time = 0.0f;
        eng->walk_amp = 0.0f;
        eng->fall_peak_y = 0.0f;
        eng->flight_look_active = 0;
        eng->flight_view_ready = 0;
        eng->look_angles_init = 0;
        eng->body_yaw_init = 0;
        eng->totk_cam_valid = 0;
        eng->eye_height = 1.62f;
        eng->elytra_open = 0.0f;
        eng->elytra_pitch = 0.0f;
        eng->elytra_bank = 0.0f;
        eng->elytra_flight_time = 0.0f;
        eng->elytra_fov_mult = 1.0f;
        eng->inv_open = MC_INV_CLOSED;
        eng->bow_drawing = 0;
        eng->bow_charge_sec = 0.0f;
        eng->eating = 0;
        eng->eat_timer = 0.0f;
        eng->eat_raise = 0.0f;
        eng->shield_block = 0.0f;
        eng->elytra_gliding = 0;
        eng->elytra_jump_until = 0.0f;
        eng->elytra_takeoff_until = 0.0f;
        eng->elytra_boost_timer = 0.0f;
        eng->pending_left_press = 0;
        eng->pending_right_press = 0;
        eng->pending_jump_press = 0;
        eng->pending_bow_fire = 0;
        eng->bow_raise = eng->bow_release = 0.0f;
        eng->pending_spawn_count = 0;
        eng->pending_break_target = 0;
        eng->pending_ignite_tnt = 0;
        eng->pending_log_actors = 0;
        eng->pending_log_mobs = 0;
        memset(eng->key_down, 0, sizeof(eng->key_down));
        memset(eng->mouse_down, 0, sizeof(eng->mouse_down));
        mc_audio_set_elytra_glide(0, 0.0f);
        mc_totk_clear_camera_look();
        return;
    }

    int entered_minecraft = eng->mode == 1 && eng->prev_mode != 1;
    if (eng->prev_mode != eng->mode) {
        eng->prev_mode = eng->mode;
        mc_totk_set_player_visible(eng->mode == 0);
        mc_totk_set_hud_visible(eng->mode == 0);
    } else if (eng->mode == 1) {
        mc_totk_set_player_visible(0);
        mc_totk_set_hud_visible(0);
    }

    /* Pull Link state */
    memcpy(eng->player_pos, ps.pos, sizeof(eng->player_pos));
    const float previous_yaw = eng->player_totk_yaw;
    eng->player_totk_yaw = ps.yaw;
    eng->player_totk_yaw_valid = 1;
    eng->player_pos_valid = 1;
    memcpy(eng->player_vel, ps.vel, sizeof(eng->player_vel));
    int totk_grounded = ps.grounded;
    McTotkIncomingDamage inc;
    int took_damage = mc_totk_poll_incoming_damage(eng->mode == 1, &inc);

    if (eng->mode != 1 || ps.hp_quarters < 0 || ps.max_hp_quarters <= 0) {
        eng->mining_swing_timer = 0.0f;
        eng->eat_raise = 0.0f;
        eng->flight_look_active = 0;
        eng->flight_view_ready = 0;
        eng->look_angles_init = 0;
        eng->body_yaw_init = 0;
        eng->totk_cam_valid = 0;
        eng->eye_height = 1.62f;
        eng->elytra_fov_mult = 1.0f;
        eng->player_was_grounded = 0;
        eng->jump_ascent_speed = 0.0f;
        eng->fall_peak_y = ps.pos[1];
        mc_audio_set_elytra_glide(0, 0.0f);
        return;
    }

    eng->totk_max_hp_q = ps.max_hp_quarters - (ps.gloom_quarters > 0 ? ps.gloom_quarters : 0);
    if (eng->totk_max_hp_q < 1) eng->totk_max_hp_q = 1;
    update_player_hearts(eng, ps.hp_quarters);
    if (was_gp && !entered_minecraft && took_damage) {
        apply_player_damage(eng, inc.raw_damage_quarters, inc.hp_before,
                            inc.has_attacker_pos, inc.has_attacker_pos ? inc.attacker_pos : NULL);
    }
    if (eng->totk_hp_q <= 0) {
        eng->flight_look_active = 0;
        eng->flight_view_ready = 0;
        mc_audio_set_elytra_glide(0, 0.0f);
        return;
    }

    mc_totk_refill_player_stamina(ps.actor);

    /* Use direct view-matrix override (`flight_look_active`) ONLY for Elytra gliding and
     * 3rd-person front view. During normal walking in 1st-person and 3rd-person back,
     * keep `flight_look_active = 0` so mouse_direct.c rotates TotK's actual PlayerCamera
     * 1:1 BEFORE CPU frustum culling (zero terrain/island culling pop-out). */
    if (wants_flight_look(eng)) {
        if (!eng->flight_look_active) start_flight_look(eng);
    } else if (eng->flight_look_active) {
        eng->flight_look_active = 0;
        eng->flight_view_ready = 0;
        eng->camera_orbit_until = 0.0f;
        mc_totk_clear_camera_look();
    }

    if (!eng->flight_look_active) {
        if (eng->totk_cam_valid) {
            eng->look_yaw = eng->totk_cam_yaw;
            eng->look_pitch = eng->totk_cam_pitch;
            eng->look_angles_init = 1;
        } else if (eng->cam_valid) {
            eng->look_yaw = atan2f(eng->cam_fwd[0], eng->cam_fwd[2]);
            eng->look_pitch = asinf(mc_clampf(eng->cam_fwd[1], -0.99f, 0.99f));
            eng->look_angles_init = 1;
        }
        eng->camera_orbit_until = 0.0f;
        mc_totk_clear_camera_look();
    } else if (eng->totk_cam_valid && mc_engine_wants_mouse_capture(eng)) {
        /* Gently align TotK's hidden internal camera toward look_yaw/look_pitch while in
         * Elytra flight or 3rd-person front view so culling and audio stay centered. */
        float yaw_err = remainderf(eng->look_yaw - eng->totk_cam_yaw, 2.0f * (float)M_PI);
        float target_pitch = mc_clampf(eng->look_pitch, -0.95f, 0.95f);
        float pitch_err = target_pitch - eng->totk_cam_pitch;
        if (fabsf(yaw_err) > 0.06f || fabsf(pitch_err) > 0.08f) {
            float step_scale = fminf(dt * 60.0f, 1.5f);
            float dx_cmd = fabsf(yaw_err) > 0.06f
                ? -mc_clampf(yaw_err * 16.0f, -22.0f, 22.0f) * step_scale
                : 0.0f;
            float dy_cmd = fabsf(pitch_err) > 0.08f
                ? -mc_clampf(pitch_err * 14.0f, -18.0f, 18.0f) * step_scale
                : 0.0f;
            mc_totk_set_camera_look(dx_cmd, dy_cmd);
        } else {
            mc_totk_clear_camera_look();
        }
    } else {
        mc_totk_clear_camera_look();
    }

    if (eng->inv_open == MC_INV_STATION) {
        if (station_index(eng)<0 || hypotf(eng->player_pos[0]-eng->station_x-.5f,eng->player_pos[2]-eng->station_z-.5f)>8 ||
            fabsf(eng->player_pos[1]-eng->station_y)>8) eng->inv_open=MC_INV_CLOSED;
        else if (eng->pending_recipe) station_action(eng,eng->pending_recipe-1);
    }
    eng->pending_recipe=0;
    for (int i=0;i<eng->block_count;i++) {
        McWorldBlock *b=&eng->blocks[i];
        if (b->cook_left>0) {
            b->cook_left=fmaxf(0,b->cook_left-dt);
            if (b->cook_left==0) eng->base_dirty=1;
        }
    }
    /* Execute queued mob spawns strictly on the guest HLE frame thread */
    if (eng->pending_log_actors) {
        eng->pending_log_actors = 0;
        McTotkActorInfo acts[64];
        int n = mc_totk_enumerate_actors(eng->player_pos, 32.0f, acts, 64);
        fprintf(stderr, "[mc] actors near=(%.1f,%.1f,%.1f) count=%d\n",
                eng->player_pos[0], eng->player_pos[1], eng->player_pos[2], n);
        for (int i = 0; i < n && i < 24; i++) {
            const McTotkActorInfo *a = &acts[i];
            float dx = a->pos[0] - eng->player_pos[0];
            float dy = a->pos[1] - eng->player_pos[1];
            float dz = a->pos[2] - eng->player_pos[2];
            fprintf(stderr, "  [%d] kind=%d name=%s hp=%d/%d d=%.2f pos=(%.2f,%.2f,%.2f) aabb=(%.1f..%.1f,%.1f..%.1f,%.1f..%.1f)\n",
                    i, (int)a->kind, a->name, a->hp, a->max_hp,
                    sqrtf(dx*dx + dy*dy + dz*dz),
                    a->pos[0], a->pos[1], a->pos[2],
                    a->aabb_min[0], a->aabb_max[0],
                    a->aabb_min[1], a->aabb_max[1],
                    a->aabb_min[2], a->aabb_max[2]);
        }
    }

    if (eng->pending_log_mobs) {
        eng->pending_log_mobs = 0;
        float pny = 0.0f;
        float pgy = probe_ground_surface_ex(eng, eng->player_pos[0], eng->player_pos[1] + 0.8f,
                                            eng->player_pos[2], NULL, NULL, &pny);
        int mcnt = 0;
        for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) if (eng->mobs[i].active) mcnt++;
        fprintf(stderr, "[mc-mob] tick dump active=%d player=(%.2f,%.2f,%.2f) p_ground=(%.2f,ny=%.2f)\n",
                mcnt, eng->player_pos[0], eng->player_pos[1], eng->player_pos[2], pgy, pny);
        for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
            const McMob *m = &eng->mobs[i];
            if (!m->active) continue;
            float mny = 0.0f;
            float mgy = probe_ground_surface_ex(eng, m->x, m->y + 0.8f, m->z, NULL, NULL, &mny);
            fprintf(stderr, "  [%d] kind=%d pos=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f,%.2f) gr=%d g=(%.2f,ny=%.2f) swell=%.2f hp=%.1f\n",
                    i, (int)m->kind, m->x, m->y, m->z, m->vx, m->vy, m->vz,
                    (int)m->grounded, mgy, mny, m->swell, m->hp);
        }
    }

    eng->day_seconds=fmodf(eng->day_seconds+dt,1200);
    if (eng->random_mob_spawns && eng->inv_open == MC_INV_CLOSED && !eng->elytra_gliding && totk_grounded) {
        eng->encounter_timer-=dt;
        if (eng->encounter_timer<=0 && eng->pending_spawn_count<16) {
            int hostiles=0;
            for (int i=0;i<MC_SNAP_MAX_MOBS;i++) if (eng->mobs[i].active && eng->mobs[i].kind != MC_MOB_WOLF) hostiles++;
            if (hostiles < (night_time(eng) ? 4 : 2)) eng->pending_spawns[eng->pending_spawn_count++]=-2;
            eng->encounter_timer=(night_time(eng) ? 40 : 85)+mc_randf(eng)*35;
        }
    }
    if (eng->pending_spawn_count > 0) {
        if (!eng->player_pos_valid) {
            fprintf(stderr, "[mc-mob] spawn skipped: player_pos invalid\n");
            eng->pending_spawn_count = 0;
        } else {
            static const float k_dists[] = {3.5f, 2.8f, 4.4f, 2.1f, 5.2f};
            static const float k_angles[] = {
                0.0f, 0.45f, -0.45f, 0.90f, -0.90f,
                1.45f, -1.45f, 2.15f, -2.15f, 3.14159265f
            };
            float orig[3], dir[3];
            get_view_origin_dir(eng, orig, dir);
            float fx = dir[0], fz = dir[2];
            float fl = sqrtf(fx * fx + fz * fz);
            if (fl < 1e-4f) { fx = 0.0f; fz = 1.0f; } else { fx /= fl; fz /= fl; }
            float rx = -fz, rz = fx;
            float p_chest[3] = {eng->player_pos[0], eng->player_pos[1] + 1.15f, eng->player_pos[2]};
            int cnt = eng->pending_spawn_count;

            for (int i = 0; i < cnt; i++) {
                int16_t rk = eng->pending_spawns[i];
                McMobType kind = (rk >= 0 && rk < MC_MOB_COUNT)
                                 ? (McMobType)rk
                                 : (McMobType)(mc_rand(eng) % MC_MOB_WOLF);
                float spread = ((float)i - 0.5f * (float)(cnt - 1)) * 1.4f;
                int found = 0;
                int fail_g = 0, fail_hr = 0, fail_los = 0, fail_wall = 0, fail_sep = 0;
                float best_x = 0.0f, best_y = 0.0f, best_z = 0.0f;

                int total_cands = (int)(sizeof(k_dists) / sizeof(k_dists[0])) *
                                  (int)(sizeof(k_angles) / sizeof(k_angles[0])) + 1;
                int natural = rk == -2;
                if (natural) total_cands = 6;
                for (int ci = 0; ci < total_cands; ci++) {
                    float cx, cz;
                    if (natural) {
                        float angle = 6.2831853f*mc_randf(eng), distance=16+7*mc_randf(eng);
                        cx=eng->player_pos[0]+cosf(angle)*distance;
                        cz=eng->player_pos[2]+sinf(angle)*distance;
                    } else if (ci == 0) {
                        cx = eng->player_pos[0] + fx * 3.5f + rx * spread;
                        cz = eng->player_pos[2] + fz * 3.5f + rz * spread;
                    } else {
                        int g = ci - 1;
                        int n_ang = (int)(sizeof(k_angles) / sizeof(k_angles[0]));
                        int d_idx = g / n_ang;
                        int a_idx = g % n_ang;
                        float d = k_dists[d_idx];
                        float ang = k_angles[a_idx];
                        float ca = cosf(ang), sa = sinf(ang);
                        float dx_r = fx * ca - fz * sa;
                        float dz_r = fx * sa + fz * ca;
                        cx = eng->player_pos[0] + dx_r * d;
                        cz = eng->player_pos[2] + dz_r * d;
                    }

                    float snorm_y = 0.0f;
                    float sy = probe_ground_surface_ex(eng, cx, eng->player_pos[1] + 0.8f, cz,
                                                       NULL, NULL, &snorm_y);
                    if (sy > eng->player_pos[1] + 1.8f ||
                        (sy > -9000.0f && !probe_mob_headroom(eng, cx, sy, cz))) {
                        float snorm_y2 = 0.0f;
                        float sy2 = probe_ground_surface_ex(eng, cx, eng->player_pos[1] - 0.5f, cz,
                                                            NULL, NULL, &snorm_y2);
                        if (sy2 > -9000.0f && sy2 < sy - 0.2f) {
                            sy = sy2;
                            snorm_y = snorm_y2;
                        }
                    }
                    if (sy <= -9000.0f ||
                        sy < eng->player_pos[1] - 4.5f ||
                        sy > eng->player_pos[1] + 4.0f ||
                        snorm_y < 0.45f) {
                        fail_g++;
                        continue;
                    }
                    if (!probe_mob_headroom(eng, cx, sy, cz)) {
                        fail_hr++;
                        continue;
                    }

                    if (natural) {
                        int lit=0;
                        for (int j=0;j<eng->block_count;j++) {
                            McWorldBlock *b=&eng->blocks[j];
                            if (b->light==15 && fabsf(b->ix+.5f-cx)+fabsf(b->iy+.5f-sy)+fabsf(b->iz+.5f-cz)<12) { lit=1; break; }
                        }
                        if (lit) continue;
                    }
                    float m_chest[3] = {cx, sy + 1.15f, cz};
                    if (!mob_has_los(eng, p_chest, m_chest)) {
                        fail_los++;
                        continue;
                    }

                    /* Verify horizontal wall clearance with 0.05m cardinal probes */
                    static const float k_wall_probes[4][2] = {
                        { 0.05f,  0.0f},
                        {-0.05f,  0.0f},
                        { 0.0f,  0.05f},
                        { 0.0f, -0.05f}
                    };
                    int wall_blocked = 0;
                    float cpos[3] = {cx, sy, cz};
                    for (int wi = 0; wi < 4; wi++) {
                        float out_xz[2] = {cx, cz}, wnorm[2] = {0}, step_y = 0.0f;
                        int wres = mc_totk_check_wall_step(cpos, 0.38f, 1.80f,
                                                           k_wall_probes[wi], out_xz, wnorm, &step_y);
                        if (wres & 1) {
                            wall_blocked = 1;
                            break;
                        }
                    }
                    if (wall_blocked) {
                        fail_wall++;
                        continue;
                    }

                    int overlap_mob = 0;
                    for (int m_i = 0; m_i < MC_SNAP_MAX_MOBS; m_i++) {
                        const McMob *em = &eng->mobs[m_i];
                        if (!em->active) continue;
                        if (fabsf(em->y - sy) > 1.8f) continue;
                        float mdx = em->x - cx, mdz = em->z - cz;
                        if (mdx * mdx + mdz * mdz < 0.85f * 0.85f) {
                            overlap_mob = 1;
                            break;
                        }
                    }
                    if (overlap_mob) {
                        fail_sep++;
                        continue;
                    }

                    best_x = cx; best_y = sy; best_z = cz;
                    found = 1;
                    break;
                }

                if (found) {
                    McMob *mob = spawn_mob(eng, kind, best_x, best_y, best_z);
                    if (mob) mob->natural=natural;
                } else {
                    if (natural) eng->encounter_timer = 10.0f + 5.0f * mc_randf(eng);
                    fprintf(stderr,
                            "[mc-mob] spawn skipped kind=%d: no walkable ground/clearance near (%.1f, %.1f, %.1f) [cands=%d g=%d hr=%d los=%d wall=%d sep=%d]\n",
                            (int)kind, eng->player_pos[0], eng->player_pos[1], eng->player_pos[2],
                            total_cands, fail_g, fail_hr, fail_los, fail_wall, fail_sep);
                }
            }
            eng->pending_spawn_count = 0;
        }
    }

    uint8_t step_mat = MAT_GRASS;
    if (fabsf(ps.vert_vel) > 0.05f) eng->player_vel[1] = ps.vert_vel;
    float prev_y = eng->player_prev_pos[1];
    int continuous = was_gp && !entered_minecraft && fabsf(prev_y - ps.pos[1]) < 8.0f;
    if (!continuous) prev_y = ps.pos[1];
    if (!continuous || eng->elytra_gliding) eng->jump_ascent_speed = 0.0f;
    const int ascending = eng->jump_ascent_speed > 0.0f;
    if (ascending) {
        /* The guest's run controller may replace its Y velocity mid-stride.
         * Finish the ascent from the last resolved height before handing
         * the fall back to its physics controller. */
        const float gravity = 30.0f;
        float rise_time = fminf(dt, eng->jump_ascent_speed / gravity);
        float rise = (eng->jump_ascent_speed - 0.5f * gravity * rise_time) * rise_time;
        float head[3] = {ps.pos[0], prev_y + 1.68f, ps.pos[2]};
        const float up[3] = {0, 1, 0};
        float distance = rise;
        if (aabb_overlaps_solid_blocks(eng, head[0], head[1], head[2], 0.26f, rise) ||
            mc_totk_raycast(head, up, rise, NULL, NULL, &distance)) {
            rise = 0.0f;
            eng->jump_ascent_speed = 0.0f;
        } else {
            eng->jump_ascent_speed = fmaxf(0.0f, eng->jump_ascent_speed - gravity * rise_time);
        }
        const float step_origin[3] = {eng->player_prev_pos[0], prev_y + rise, eng->player_prev_pos[2]};
        set_input_walk_velocity(eng);
        float delta[2] = {eng->player_vel[0] * dt, eng->player_vel[2] * dt};
        float next[2] = {step_origin[0] + delta[0], step_origin[2] + delta[1]};
        mc_totk_check_wall_step(step_origin, 0.28f, 1.68f, delta, next, NULL, NULL);
        eng->player_pos[0] = next[0];
        eng->player_pos[1] = step_origin[1];
        eng->player_pos[2] = next[1];
        eng->player_vel[0] = (next[0] - step_origin[0]) / dt;
        eng->player_vel[1] = eng->jump_ascent_speed;
        eng->player_vel[2] = (next[1] - step_origin[2]) / dt;
        totk_grounded = 0;
    }
    int was_supported = continuous && !ascending && eng->player_on_mc_block &&
        ps.pos[1] <= prev_y + 0.25f &&
        hypotf(ps.pos[0] - eng->player_prev_pos[0], ps.pos[2] - eng->player_prev_pos[2]) < 1.0f;

    /* The native controller still sees air here. Move using this frame's
     * input from the last resolved floor position, independent of its drift. */
    if (was_supported) {
        set_input_walk_velocity(eng);
        eng->player_vel[1] = 0.0f;
        eng->player_pos[0] = eng->player_prev_pos[0] + eng->player_vel[0] * dt;
        eng->player_pos[2] = eng->player_prev_pos[2] + eng->player_vel[2] * dt;
        eng->player_pos[1] = prev_y;
        resolve_player_block_walls(eng);
    }

    float gy = ascending ? -10000.0f : player_block_floor(eng, prev_y, was_supported, &step_mat);
    int on_mc = gy > -9000.0f;
    if (on_mc) {
        eng->player_pos[1] = gy;
        if (!was_supported) set_input_walk_velocity(eng);
        eng->player_vel[1] = 0.0f;
    } else if (was_supported) {
        eng->player_pos[1] = fminf(ps.pos[1], prev_y);
        eng->player_vel[1] = fminf(ps.vert_vel, 0.0f);
    }

    int hit_wall = resolve_player_block_walls(eng);
    const int walking_on_mc = on_mc && hypotf(eng->player_vel[0], eng->player_vel[2]) > 0.05f;
    if (walking_on_mc || (on_mc && was_supported)) {
        /* Virtual floors bypass the guest locomotion that normally turns the actor. */
        eng->player_totk_yaw = walking_on_mc ? atan2f(eng->player_vel[0], eng->player_vel[2]) : previous_yaw;
        mc_totk_set_player_pos(eng->player_pos, &eng->player_totk_yaw);
    } else if (ascending || hit_wall || was_supported || (on_mc && ps.pos[1] != gy)) {
        mc_totk_set_player_pos(eng->player_pos, NULL);
    }
    if (ascending || hit_wall || was_supported || on_mc) mc_totk_set_player_vel(eng->player_vel);

    /* Grounding over placed MC blocks + Elytra glide */
    if (eng->player_pos_valid) {
        int grounded = totk_grounded || on_mc;
        eng->player_grounded = (uint8_t)grounded;
        eng->player_on_mc_block = (uint8_t)(on_mc && grounded);

        int landed = grounded && !eng->player_was_grounded;
        int has_elytra = eng->armor_slots[1].item_id == MC_ITM_ELYTRA;
        if (landed && !eng->elytra_gliding && !entered_minecraft &&
            eng->fall_peak_y > eng->player_pos[1] + 3.6f) {
            int fdmg = (int)floorf(eng->fall_peak_y - eng->player_pos[1] - 3.0f);
            play_sfx_2d(fdmg >= 4 ? MC_SFX_FALL_BIG : MC_SFX_FALL_SMALL, 0.85f, 1.0f);
            apply_player_damage(eng, fdmg * 2, eng->totk_hp_q, 0, NULL);
            if (eng->totk_hp_q <= 0) {
                mc_audio_set_elytra_glide(0, 0.0f);
                return;
            }
        }

        if (grounded || eng->elytra_gliding || !was_gp || entered_minecraft) {
            eng->fall_peak_y = eng->player_pos[1];
        } else {
            eng->fall_peak_y = fmaxf(eng->fall_peak_y, eng->player_pos[1]);
        }
        int takeoff = eng->time_sec < eng->elytra_takeoff_until;
        int was_gliding = eng->elytra_gliding;
        if (!has_elytra || (grounded && !(takeoff && eng->elytra_vel[1] > 0.0f))) {
            if (was_gliding && grounded) {
                /* Clean landing from Elytra glide: zero bounce/snap, zero fall damage at gentle
                 * touchdown speeds, kinetic damage only on steep high-speed ground impact. */
                float land_vy = eng->elytra_vel[1];
                if (!entered_minecraft && eng->elytra_flight_time > 0.25f && land_vy < -13.0f) {
                    int kdmg = (int)floorf((-land_vy - 11.0f) * 0.6f);
                    if (kdmg > 0) {
                        play_sfx_2d(kdmg >= 4 ? MC_SFX_FALL_BIG : MC_SFX_FALL_SMALL, 0.85f, 1.0f);
                        apply_player_damage(eng, kdmg * 2, eng->totk_hp_q, 0, NULL);
                        if (eng->totk_hp_q <= 0) {
                            eng->elytra_gliding = 0;
                            eng->elytra_boost_timer = 0.0f;
                            mc_audio_set_elytra_glide(0, 0.0f);
                            return;
                        }
                    }
                }
                float land_vel[3] = {eng->elytra_vel[0] * 0.25f, 0.0f, eng->elytra_vel[2] * 0.25f};
                mc_totk_set_player_vel(land_vel);
                mc_totk_set_player_yaw(eng->elytra_yaw);
                eng->fall_peak_y = eng->player_pos[1];
            }
            eng->elytra_gliding = 0;
            eng->elytra_boost_timer = 0.0f;
            eng->elytra_flight_time = 0.0f;
        }
        if (landed) eng->elytra_jump_until = 0.0f;

        int jump_now = eng->key_down[KVK_SPACE];
        int jump_edge = (jump_now && !eng->prev_jump_down) || eng->pending_jump_press;
        eng->pending_jump_press = 0;
        eng->prev_jump_down = (uint8_t)jump_now;

        int recent_ground = continuous && eng->player_was_grounded && fabsf(ps.pos[1] - prev_y) <= 0.12f;
        if (jump_edge && !ascending && (grounded || recent_ground) && !eng->elytra_gliding) {
            jump_edge = 0;
            float y = eng->player_on_mc_block && gy > -9000.0f ? gy : eng->player_pos[1];
            float jp[3] = {eng->player_pos[0], y + 0.35f, eng->player_pos[2]};
            float jv[3] = {eng->player_vel[0], 9.2f, eng->player_vel[2]};
            eng->player_on_mc_block = 0;
            eng->player_pos[1] = jp[1];
            eng->player_vel[1] = jv[1];
            eng->jump_ascent_speed = jv[1];
            mc_totk_set_player_pos(jp, NULL);
            mc_totk_set_player_vel(jv);
            eng->elytra_jump_until = eng->time_sec + 0.45f;

        }

        int jump_launch = has_elytra && eng->time_sec < eng->elytra_jump_until;
        int rocket_started = has_elytra && (eng->elytra_gliding || jump_launch)
                          && consume_elytra_rocket(eng);
        if (!eng->elytra_gliding && has_elytra && ((jump_edge && !grounded) || rocket_started)) {
            eng->elytra_gliding = 1;
            eng->elytra_flight_time = 0.0f;
            eng->jump_ascent_speed = 0.0f;
            start_flight_look(eng);
            memcpy(eng->elytra_vel, eng->player_vel, sizeof(eng->elytra_vel));
            float init_h = hypotf(eng->elytra_vel[0], eng->elytra_vel[2]);
            float init_orig[3], init_dir[3];
            get_view_origin_dir(eng, init_orig, init_dir);
            float dh = hypotf(init_dir[0], init_dir[2]);
            float fx = dh > 0.02f ? init_dir[0] / dh : sinf(ps.yaw);
            float fz = dh > 0.02f ? init_dir[2] / dh : cosf(ps.yaw);
            if (init_h < 5.0f) {
                eng->elytra_vel[0] = fx * 5.0f;
                eng->elytra_vel[2] = fz * 5.0f;
            }
            eng->elytra_yaw = atan2f(eng->elytra_vel[0], eng->elytra_vel[2]);
            eng->elytra_prev_yaw = eng->elytra_yaw;
            if (eng->elytra_vel[1] > 0.0f) {
                eng->elytra_takeoff_until = eng->time_sec + 0.14f;
                takeoff = 1;
            }
        }
        if (rocket_started && jump_launch) {
            eng->elytra_jump_until = 0.0f;
            eng->elytra_takeoff_until = eng->time_sec + 0.22f;
            takeoff = 1;
        }

        if (eng->elytra_gliding) {
            start_flight_look(eng);
            eng->elytra_flight_time += dt;
            float orig[3], dir[3];
            get_view_origin_dir(eng, orig, dir);
            float *vel = eng->elytra_vel;
            float horizontal = hypotf(dir[0], dir[2]);
            float forward_x = horizontal > 0.02f ? dir[0] / horizontal : sinf(eng->elytra_yaw);
            float forward_z = horizontal > 0.02f ? dir[2] / horizontal : cosf(eng->elytra_yaw);
            float target_yaw = atan2f(forward_x, forward_z);
            int boosting = eng->elytra_boost_timer > 0.0f;

            /* Authentic Minecraft travelFallFlying + FireworkRocketEntity boost */
            float step_dt = (boosting && rocket_started) ? fmaxf(dt, 0.05f) : dt;
            step_elytra_physics(vel, dir, boosting, takeoff, step_dt);

            if (boosting) {
                eng->elytra_boost_timer -= dt;
                if (eng->elytra_boost_timer < 0.0f) eng->elytra_boost_timer = 0.0f;
                eng->elytra_boost_trail_accum += dt;
                if (rocket_started || eng->elytra_boost_trail_accum >= 0.032f) {
                    eng->elytra_boost_trail_accum = 0.0f;
                    float spd_b = hypotf(hypotf(vel[0], vel[2]), vel[1]);
                    float bx = spd_b > 0.5f ? -vel[0] / spd_b : -dir[0];
                    float by = spd_b > 0.5f ? -vel[1] / spd_b : -dir[1];
                    float bz = spd_b > 0.5f ? -vel[2] / spd_b : -dir[2];
                    float jx = (mc_randf(eng) - 0.5f) * 0.28f;
                    float jy = (mc_randf(eng) - 0.5f) * 0.22f;
                    float jz = (mc_randf(eng) - 0.5f) * 0.28f;
                    float sp[3] = {
                        eng->player_pos[0] + bx * 0.75f + jx,
                        eng->player_pos[1] + 0.55f + by * 0.75f + jy,
                        eng->player_pos[2] + bz * 0.75f + jz
                    };
                    float sv[3] = {
                        bx * 1.4f + (mc_randf(eng) - 0.5f) * 0.8f,
                        by * 1.4f - 0.3f,
                        bz * 1.4f + (mc_randf(eng) - 0.5f) * 0.8f
                    };
                    /* Crisp white Minecraft firework sparkle stars + soft smoke trail. */
                    spawn_particle(eng, MC_PART_GLINT, 1, COL_WHITE, sp, sv, 0.28f, 0.36f, 0.4f);
                    if (rocket_started || (eng->tick_count & 1u) == 0u) {
                        spawn_particle(eng, MC_PART_SMOKE, 12, COL_WHITE, sp, sv, 0.26f, 0.40f, 0.0f);
                    }
                }
            }

            /* Kinetic horizontal wall crash check (authentic MC wall collision damage) */
            float next_hspeed = hypotf(vel[0], vel[2]);
            if (eng->elytra_flight_time > 0.25f && next_hspeed > 18.0f) {
                float probe_orig[3] = {eng->player_pos[0], eng->player_pos[1] + 0.55f, eng->player_pos[2]};
                float hdir[3] = {vel[0] / next_hspeed, 0.0f, vel[2] / next_hspeed};
                float probe_dist = next_hspeed * dt + 0.55f;
                float hit_p[3], hit_n[3], hit_d = 0.0f;
                if (mc_totk_raycast(probe_orig, hdir, probe_dist, hit_p, hit_n, &hit_d) &&
                    fabsf(hit_n[1]) < 0.45f) {
                    float impact = -(vel[0] * hit_n[0] + vel[2] * hit_n[2]);
                    if (impact > 14.0f) {
                        int kdmg = (int)floorf((impact - 12.0f) * 0.5f);
                        if (kdmg > 0) {
                            play_sfx_2d(MC_SFX_HURT, 0.9f, 1.0f);
                            apply_player_damage(eng, kdmg * 2, eng->totk_hp_q, 0, NULL);
                        }
                        vel[0] *= 0.25f;
                        vel[2] *= 0.25f;
                        next_hspeed = hypotf(vel[0], vel[2]);
                        eng->elytra_boost_timer = 0.0f;
                    }
                }
            }

            if (!mc_totk_set_player_vel(vel)) eng->elytra_gliding = 0;

            float total_speed = hypotf(next_hspeed, vel[1]);
            eng->elytra_open = mc_clampf(eng->elytra_open + dt * 5.0f, 0.0f, 1.0f);

            /* Smooth heading yaw (tracks flight velocity, falling back to look_yaw in steep climbs) */
            float prev_yaw = eng->elytra_yaw;
            float desired_yaw = next_hspeed > 1.2f ? atan2f(vel[0], vel[2]) : target_yaw;
            float yaw_diff = remainderf(desired_yaw - prev_yaw, 2.0f * (float)M_PI);
            eng->elytra_yaw = remainderf(prev_yaw + yaw_diff * (1.0f - expf(-14.0f * dt)), 2.0f * (float)M_PI);
            float yaw_rate = remainderf(eng->elytra_yaw - eng->elytra_prev_yaw, 2.0f * (float)M_PI) / fmaxf(dt, 0.001f);
            eng->elytra_prev_yaw = eng->elytra_yaw;

            /* Body pitch (>0 nose up, <0 nose down) and turn bank (>0 right wing down, <0 left wing down) */
            float vel_pitch = atan2f(vel[1], fmaxf(next_hspeed, 1.5f));
            float look_pitch_rad = atan2f(dir[1], fmaxf(horizontal, 0.01f));
            float vel_weight = mc_clampf(total_speed / 8.0f, 0.2f, 1.0f);
            float target_pitch = mc_clampf(vel_pitch * vel_weight + look_pitch_rad * (1.0f - vel_weight),
                                           -1.35f, 1.25f);
            eng->elytra_pitch += (target_pitch - eng->elytra_pitch) * (1.0f - expf(-10.0f * dt));

            float turn = remainderf(target_yaw - eng->elytra_yaw, 2.0f * (float)M_PI);
            float air_factor = mc_clampf(total_speed / 18.0f, 0.25f, 1.0f);
            float target_bank = mc_clampf((-turn * 0.85f - yaw_rate * 0.18f) * air_factor, -0.60f, 0.60f);
            eng->elytra_bank += (target_bank - eng->elytra_bank) * (1.0f - expf(-7.5f * dt));

            /* Subtle speed-based FOV widening (authentic Minecraft flight feel) */
            float target_fov_mult = 1.0f + mc_clampf((total_speed - 12.0f) / 35.0f, 0.0f, 1.0f) * 0.12f
                                  + (boosting ? 0.04f : 0.0f);
            eng->elytra_fov_mult += (target_fov_mult - eng->elytra_fov_mult) * (1.0f - expf(-6.0f * dt));

            mc_audio_set_elytra_glide(eng->elytra_gliding, total_speed);
        } else {
            eng->elytra_open = mc_clampf(eng->elytra_open - dt * 5.0f, 0.0f, 1.0f);
            float relax = 1.0f - expf(-12.0f * dt);
            eng->elytra_pitch += (0.0f - eng->elytra_pitch) * relax;
            eng->elytra_bank += (0.0f - eng->elytra_bank) * relax;
            if (eng->elytra_open == 0.0f) {
                eng->elytra_pitch = 0.0f;
                eng->elytra_bank = 0.0f;
            }
            mc_audio_set_elytra_glide(0, 0.0f);
        }

        /* Footstep cadence & walk phase */
        float dx = eng->player_pos[0] - eng->player_prev_pos[0];
        float dz = eng->player_pos[2] - eng->player_prev_pos[2];
        float step_d = sqrtf(dx * dx + dz * dz);
        if (step_d > 5.0f) step_d = 0.0f;
        eng->player_prev_pos[0] = eng->player_pos[0];
        eng->player_prev_pos[1] = eng->player_pos[1];
        eng->player_prev_pos[2] = eng->player_pos[2];
        if (grounded) {
            eng->airborne_time = 0.0f;
        } else if (eng->elytra_gliding || eng->jump_ascent_speed > 0.0f) {
            eng->airborne_time = 1.0f;
        } else {
            eng->airborne_time += dt;
        }
        int bob_grounded = !eng->elytra_gliding && (grounded || (eng->airborne_time < 0.12f));
        float inst_speed = step_d / fmaxf(dt, 0.001f);
        if (step_d < 1e-4f) {
            float v_h = hypotf(eng->player_vel[0], eng->player_vel[2]);
            if (v_h > 0.25f) inst_speed = v_h;
        }
        float target_amp = bob_grounded ? mc_clampf(inst_speed / 5.5f, 0.0f, 1.0f) : 0.0f;
        float bob_alpha = 1.0f - expf(-8.5f * dt);
        if (eng->elytra_gliding) {
            eng->walk_amp = 0.0f;
        } else {
            eng->walk_amp += (target_amp - eng->walk_amp) * bob_alpha;
            if (eng->walk_amp < 0.001f && target_amp == 0.0f) eng->walk_amp = 0.0f;
        }
        if (grounded && !entered_minecraft && eng->inv_open == MC_INV_CLOSED)
            eng->exhaustion += step_d * (inst_speed > 4.5f ? 0.10f : 0.025f);
        eng->walk_phase += step_d * 2.1f;
        if (grounded && !eng->player_crouching) {
            eng->step_accum += step_d;
            if (eng->step_accum >= 1.9f) {
                eng->step_accum = 0.0f;
                play_sfx_2d(mat_step_sfx(step_mat), 0.32f, 0.92f + 0.16f * mc_randf(eng));
            }
        }
        eng->player_was_grounded = (uint8_t)grounded;

        /* Smooth Minecraft eye height: 1.62m standing, 1.27m crouching, 0.55m Elytra gliding. */
        float target_eye_h = eng->elytra_gliding ? 0.55f
                           : (eng->player_crouching ? 1.27f : 1.62f);
        if (eng->eye_height < 0.1f) eng->eye_height = target_eye_h;
        eng->eye_height += (target_eye_h - eng->eye_height) * (1.0f - expf(-14.0f * dt));

        /* Use sprint input for FOV. Position deltas and frame time can vary around
         * walking speed, making speed-based sprint detection visibly pulse. */
        if (!eng->elytra_gliding) {
            int sprinting = !eng->player_crouching && eng->inv_open == MC_INV_CLOSED &&
                            eng->move_forward > 0.25f && eng->sprint_requested;
            float target_fov = sprinting ? 1.08f : 1.0f;
            eng->elytra_fov_mult += (target_fov - eng->elytra_fov_mult) * (1.0f - expf(-8.0f * dt));
        }

        /* Vanilla Minecraft LivingEntity.tickHeadTurn: independent head_yaw and body_yaw. */
        float cur_head = eng->flight_look_active
            ? eng->look_yaw
            : (eng->cam_valid ? atan2f(eng->cam_fwd[0], eng->cam_fwd[2]) : eng->look_yaw);
        if (!eng->body_yaw_init) {
            eng->body_yaw = eng->player_totk_yaw_valid ? eng->player_totk_yaw : cur_head;
            eng->head_yaw = cur_head;
            eng->body_yaw_init = 1;
        }
        eng->head_yaw = cur_head;
        if (eng->elytra_gliding) {
            eng->body_yaw = eng->elytra_yaw;
        } else {
            float move_mag = hypotf(eng->move_right, eng->move_forward);
            int actively_moving = eng->inv_open == MC_INV_CLOSED && move_mag > 0.15f;
            if (actively_moving) {
                /* Movement angle relative to look_yaw (+angle is LEFT, -angle is RIGHT). */
                float rel_move = atan2f(-eng->move_right, eng->move_forward);
                /* When moving backward (|rel_move| > pi/2), flip torso 180 deg to backpedal facing forward. */
                if (rel_move > (float)M_PI * 0.5f + 0.05f) rel_move -= (float)M_PI;
                else if (rel_move < -(float)M_PI * 0.5f - 0.05f) rel_move += (float)M_PI;
                rel_move = mc_clampf(rel_move, -0.87266f, 0.87266f);
                float desired_body = remainderf(eng->head_yaw + rel_move, 2.0f * (float)M_PI);
                float bdiff = remainderf(desired_body - eng->body_yaw, 2.0f * (float)M_PI);
                eng->body_yaw = remainderf(eng->body_yaw + bdiff * (1.0f - expf(-12.0f * dt)),
                                           2.0f * (float)M_PI);
            } else if (eng->swing_timer > 0.0f || eng->mining_swing_timer > 0.0f || eng->bow_drawing) {
                float bdiff = remainderf(eng->head_yaw - eng->body_yaw, 2.0f * (float)M_PI);
                eng->body_yaw = remainderf(eng->body_yaw + bdiff * (1.0f - expf(-12.0f * dt)),
                                           2.0f * (float)M_PI);
            } else {
                float hdiff = remainderf(eng->head_yaw - eng->body_yaw, 2.0f * (float)M_PI);
                if (fabsf(hdiff) > 0.87266f) {
                    float target_b = remainderf(eng->head_yaw - copysignf(0.87266f, hdiff),
                                                2.0f * (float)M_PI);
                    float bdiff = remainderf(target_b - eng->body_yaw, 2.0f * (float)M_PI);
                    eng->body_yaw = remainderf(eng->body_yaw + bdiff * (1.0f - expf(-10.0f * dt)),
                                               2.0f * (float)M_PI);
                }
            }
            /* Hard neck clamp (~70 deg) so rapid mouse flicks never twist the neck backwards. */
            float neck = remainderf(eng->head_yaw - eng->body_yaw, 2.0f * (float)M_PI);
            if (neck > 1.22f) {
                eng->body_yaw = remainderf(eng->head_yaw - 1.22f, 2.0f * (float)M_PI);
            } else if (neck < -1.22f) {
                eng->body_yaw = remainderf(eng->head_yaw + 1.22f, 2.0f * (float)M_PI);
            }
        }

        /* Rebuild direct 1:1 view matrix only while Elytra gliding or 3rd-person front. */
        if (wants_flight_look(eng)) {
            if (!eng->flight_look_active) start_flight_look(eng);
            update_flight_view(eng);
        } else if (eng->flight_look_active) {
            eng->flight_look_active = 0;
            eng->flight_view_ready = 0;
            mc_totk_clear_camera_look();
        }
    }

    if (eng->flight_camera_available && !eng->flight_look_active && eng->cam_valid &&
        !eng->elytra_gliding && eng->view_mode != MC_VIEW_FIRST_PERSON) {
        float eh = eng->eye_height > 0.1f ? eng->eye_height : 1.62f;
        float pivot[3] = {ps.pos[0], ps.pos[1] + eh, ps.pos[2]};
        float behind[3] = {-eng->cam_fwd[0], -eng->cam_fwd[1], -eng->cam_fwd[2]};
        eng->ground_camera_distance = third_person_distance(eng, pivot, behind);
    } else {
        eng->ground_camera_distance = 0;
    }

    /* Update target raycast (MC blocks vs Hyrule world) */
    float orig[3], dir[3];
    get_view_origin_dir(eng, orig, dir);
    int32_t tix = 0, tiy = 0, tiz = 0; int tface = 0; float mc_dist = 99.0f;
    int hit_mc = raycast_blocks(eng, orig, dir, 24.0f, &tix, &tiy, &tiz, &tface, &mc_dist);
    eng->has_hyrule_target = 0;
    float hy_dist = 99.0f;
    int from_eye = eng->flight_look_active ||
                   (eng->view_mode == MC_VIEW_FIRST_PERSON && eng->flight_camera_available);
    if (from_eye) {
        eng->has_hyrule_target = mc_totk_raycast_from_eye(orig, dir, 24.0f, eng->hyrule_hit,
                                                          eng->hyrule_normal, &hy_dist);
    } else {
        float hy_off = eng->player_pos_valid ? 1.45f : 0.0f;
        float hy_orig[3] = {
            orig[0] + dir[0] * hy_off,
            orig[1] + dir[1] * hy_off,
            orig[2] + dir[2] * hy_off
        };
        float hy_rem = 24.0f - hy_off;
        for (int try_i = 0; try_i < 3 && hy_rem > 0.2f; try_i++) {
            float hhit[3], hnrm[3], hd = 0.0f;
            if (!mc_totk_raycast(hy_orig, dir, hy_rem, hhit, hnrm, &hd)) break;
            int is_self = 0;
            if (eng->player_pos_valid) {
                float dxp = hhit[0] - eng->player_pos[0];
                float dzp = hhit[2] - eng->player_pos[2];
                float dyp = hhit[1] - eng->player_pos[1];
                if (dxp * dxp + dzp * dzp < 1.35f * 1.35f && dyp > 0.15f && dyp < 2.35f) {
                    is_self = 1;
                }
            }
            if (!is_self) {
                memcpy(eng->hyrule_hit, hhit, sizeof(hhit));
                memcpy(eng->hyrule_normal, hnrm, sizeof(hnrm));
                hy_dist = hy_off + hd;
                eng->has_hyrule_target = 1;
                break;
            }
            float adv = hd + 0.35f;
            hy_orig[0] += dir[0] * adv;
            hy_orig[1] += dir[1] * adv;
            hy_orig[2] += dir[2] * adv;
            hy_off += adv;
            hy_rem -= adv;
        }
    }
    if (!eng->has_hyrule_target && dir[1] < -0.02f && eng->player_pos_valid) {
        /* Ground-height raymarch fallback when aiming down at terrain below eye level */
        float prev_ry = orig[1];
        for (float t = from_eye ? 0.05f : 1.5f; t <= 24.0f; t += 0.8f) {
            float rx = orig[0] + dir[0] * t;
            float ry = orig[1] + dir[1] * t;
            float rz = orig[2] + dir[2] * t;
            float gh = -10000.0f;
            if (mc_totk_ground_height(rx, orig[1], rz, &gh) &&
                gh > -9000.0f && gh <= orig[1] + 0.25f &&
                prev_ry > gh && ry <= gh + 0.08f) {
                eng->hyrule_hit[0] = rx;
                eng->hyrule_hit[1] = gh;
                eng->hyrule_hit[2] = rz;
                eng->hyrule_normal[0] = 0.0f;
                eng->hyrule_normal[1] = 1.0f;
                eng->hyrule_normal[2] = 0.0f;
                hy_dist = t;
                eng->has_hyrule_target = 1;
                break;
            }
            prev_ry = ry;
        }
    }
    if (hit_mc && (!eng->has_hyrule_target || mc_dist <= hy_dist + 0.05f)) {
        eng->has_target = 1;
        eng->target_ix = tix; eng->target_iy = tiy; eng->target_iz = tiz; eng->target_face = tface;
    } else {
        eng->has_target = 0;
    }

    /* Execute queued mouse actions safely on guest frame thread */
    if (eng->inv_open == MC_INV_CLOSED) {
        if (eng->pending_left_press) {
            eng->pending_left_press = 0;
            perform_left_click_attack(eng);
        }
        if (eng->pending_right_press) {
            eng->pending_right_press = 0;
            handle_right_click_action(eng, 1);
        }
        if (eng->pending_bow_fire) {
            eng->pending_bow_fire = 0;
            fire_bow_arrow(eng, eng->pending_bow_charge_sec);
        }
        if (eng->pending_break_target) {
            eng->pending_break_target = 0;
            if (eng->has_target) {
                int bidx = find_block_idx(eng, eng->target_ix, eng->target_iy, eng->target_iz);
                if (bidx >= 0) {
                    uint16_t bt = eng->blocks[bidx].type;
                    if (bt == MC_BLK_TNT) {
                        float tx = (float)eng->target_ix + 0.5f;
                        float ty = (float)eng->target_iy + 0.5f;
                        float tz = (float)eng->target_iz + 0.5f;
                        set_block_at(eng, eng->target_ix, eng->target_iy, eng->target_iz, MC_BLK_AIR, 0);
                        spawn_primed_tnt(eng, tx, ty, tz, 4.0f);
                        float tpos[3] = {tx, ty, tz};
                        play_sfx_at(eng, MC_SFX_TNT_FUSE, 1.0f, 1.0f, tpos);
                    } else if (bt < MC_BLK_COUNT && bt != MC_BLK_BEDROCK) {
                        const McBlockMeta *bm = &k_blocks[bt];
                        float bpos[3] = {
                            (float)eng->target_ix + 0.5f,
                            (float)eng->target_iy + 0.5f,
                            (float)eng->target_iz + 0.5f
                        };
                        play_sfx_at(eng, mat_dig_sfx(bm->mat), 0.9f, 1.0f, bpos);
                        spawn_burst_particles(eng, MC_PART_BLOCK_CRUMB, 1, block_crumb_color(bt),
                                              bpos, 14, 0.8f, 2.8f, 0.18f, 0.55f, 15.0f);
                        if (bm->drop_item != MC_ITM_NONE) {
                            spawn_dropped_item(eng, bm->drop_item, 1, bpos, NULL);
                        }
                        set_block_at(eng, eng->target_ix, eng->target_iy, eng->target_iz, MC_BLK_AIR, 0);
                        eng->mining_progress = 0.0f;
                    }
                    eng->swing_timer = eng->swing_dur = 0.25f;
                }
            }
        }
        if (eng->pending_ignite_tnt) {
            eng->pending_ignite_tnt = 0;
            int bidx = eng->has_target ? find_block_idx(eng, eng->target_ix, eng->target_iy, eng->target_iz) : -1;
            if (bidx < 0 || eng->blocks[bidx].type != MC_BLK_TNT) {
                float best_d2 = 676.0f;
                bidx = -1;
                for (int i = 0; i < eng->block_count; i++) {
                    if (eng->blocks[i].type != MC_BLK_TNT) continue;
                    float dx = (float)eng->blocks[i].ix + 0.5f - eng->player_pos[0];
                    float dy = (float)eng->blocks[i].iy + 0.5f - eng->player_pos[1];
                    float dz = (float)eng->blocks[i].iz + 0.5f - eng->player_pos[2];
                    float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < best_d2) { best_d2 = d2; bidx = i; }
                }
            }
            if (bidx >= 0) {
                int32_t tx_i = eng->blocks[bidx].ix, ty_i = eng->blocks[bidx].iy, tz_i = eng->blocks[bidx].iz;
                float tx = (float)tx_i + 0.5f, ty = (float)ty_i + 0.5f, tz = (float)tz_i + 0.5f;
                set_block_at(eng, tx_i, ty_i, tz_i, MC_BLK_AIR, 0);
                spawn_primed_tnt(eng, tx, ty, tz, 4.0f);
                float tpos[3] = {tx, ty, tz};
                play_sfx_at(eng, MC_SFX_TNT_FUSE, 1.0f, 1.0f, tpos);
            } else {
                float orig[3], dir[3];
                get_view_origin_dir(eng, orig, dir);
                float tx = eng->player_pos[0] + dir[0] * 3.0f;
                float tz = eng->player_pos[2] + dir[2] * 3.0f;
                float ty = probe_ground_surface(eng, tx, eng->player_pos[1] + 4.0f, tz, NULL, NULL);
                if (ty < -9000.0f) ty = eng->player_pos[1];
                spawn_primed_tnt(eng, tx, ty + 0.5f, tz, 4.0f);
                float tpos[3] = {tx, ty + 0.5f, tz};
                play_sfx_at(eng, MC_SFX_TNT_FUSE, 1.0f, 1.0f, tpos);
            }
            eng->swing_timer = eng->swing_dur = 0.25f;
        }
    } else {
        eng->pending_left_press = 0;
        eng->pending_right_press = 0;
        eng->pending_bow_fire = 0;
        eng->pending_break_target = 0;
        eng->pending_ignite_tnt = 0;
    }

    /* Timers & continuous use actions */
    if (eng->hotbar[eng->selected_slot].item_id == MC_ITM_BOW && eng->inv_open == MC_INV_CLOSED) {
        eng->bow_raise = mc_clampf(eng->bow_raise + (eng->bow_drawing ? 7.0f : -5.0f) * dt, 0.0f, 1.0f);
        eng->bow_release = mc_clampf(eng->bow_release - 6.5f * dt, 0.0f, 1.0f);
    } else {
        eng->bow_raise = eng->bow_release = 0.0f;
    }
    if (eng->swing_timer > 0.0f) eng->swing_timer = mc_clampf(eng->swing_timer - dt, 0.0f, 2.0f);
    eng->mining_swing_timer = fmaxf(0.0f, eng->mining_swing_timer - dt);
    if (eng->attack_cd_timer > 0.0f) eng->attack_cd_timer = mc_clampf(eng->attack_cd_timer - dt, 0.0f, 2.0f);
    if (eng->equip_drop > 0.0f) eng->equip_drop = mc_clampf(eng->equip_drop - dt * 5.0f, 0.0f, 1.0f);
    if (eng->totem_pop_timer > 0.0f) eng->totem_pop_timer = mc_clampf(eng->totem_pop_timer - dt, 0.0f, 3.0f);
    if (eng->hurt_flash_timer > 0.0f) eng->hurt_flash_timer = mc_clampf(eng->hurt_flash_timer - dt, 0.0f, 1.0f);
    if (eng->tooltip_timer > 0.0f) eng->tooltip_timer = mc_clampf(eng->tooltip_timer - dt, 0.0f, 5.0f);
    if (eng->crosshair_hit_timer > 0.0f) eng->crosshair_hit_timer = mc_clampf(eng->crosshair_hit_timer - dt, 0.0f, 1.0f);
    if (eng->invuln_timer > 0.0f) eng->invuln_timer = mc_clampf(eng->invuln_timer - dt, 0.0f, 2.0f);

    if (eng->inv_open == MC_INV_CLOSED) {
        if (eng->mouse_down[1]) {
            uint16_t hid = eng->hotbar[eng->selected_slot].item_id;
            if (eng->bow_drawing && hid == MC_ITM_BOW) {
                eng->bow_charge_sec = mc_clampf(eng->bow_charge_sec + dt, 0.0f, 1.2f);
            } else if (eng->eating && hid < MC_ITM_COUNT && k_items[hid].is_food) {
                eng->eat_timer += dt;
                eng->eat_snd_accum += dt;
                if (eng->eat_snd_accum >= MC_EAT_BITE_SECONDS) {
                    eng->eat_snd_accum -= MC_EAT_BITE_SECONDS;
                    play_sfx_2d(MC_SFX_EAT, 0.65f, 0.85f + 0.25f * mc_randf(eng));
                    float cp[3] = {orig[0] + dir[0] * 0.6f, orig[1] - 0.15f, orig[2] + dir[2] * 0.6f};
                    spawn_burst_particles(eng, MC_PART_BLOCK_CRUMB, 1, RGBA_PACK(185, 110, 62, 255),
                                          cp, 3, 0.15f, 1.1f, 0.09f, 0.35f, 12.0f);
                }
                if (eng->eat_timer >= 1.35f) finish_eating(eng);
            } else if (eng->offhand.item_id == MC_ITM_SHIELD || hid == MC_ITM_SHIELD) {
                eng->shield_block = mc_clampf(eng->shield_block + dt * 8.0f, 0.0f, 1.0f);
            }
        } else {
            eng->shield_block = mc_clampf(eng->shield_block - dt * 8.0f, 0.0f, 1.0f);
        }
        update_mining(eng, dt);
    } else {
        eng->shield_block = 0.0f;
    }

    int eating = eng->eating && eng->mouse_down[1] && eng->inv_open == MC_INV_CLOSED;
    eng->eat_raise = mc_clampf(eng->eat_raise + dt * (eating ? 7.0f : -6.0f), 0.0f, 1.0f);

    if (eng->inv_open==MC_INV_CLOSED) {
        if (eng->exhaustion>=4) {
            eng->exhaustion-=4;
            if (eng->saturation>0) eng->saturation=fmaxf(0,eng->saturation-1);
            else if (eng->hunger>0) eng->hunger--;
        }
        if ((eng->hunger>=18 && eng->totk_hp_q<eng->totk_max_hp_q && eng->totk_hp_q>0) ||
            (eng->hunger==0 && eng->totk_hp_q>4)) {
            eng->food_timer+=dt;
            if (eng->food_timer>=4) {
                eng->food_timer=0;
                if (eng->hunger) { write_player_health(eng,eng->totk_hp_q+2); eng->exhaustion+=6; }
                else write_player_health(eng,eng->totk_hp_q-2 < 4 ? 4 : eng->totk_hp_q-2);
            }
        } else eng->food_timer=0;
    }
    /* Status effect timers & Regeneration */
    for (int i = eng->effect_count - 1; i >= 0; i--) {
        eng->effects[i].seconds_left -= dt;
        if (eng->effects[i].seconds_left <= 0.0f) {
            if (eng->effects[i].effect_type == MC_EFF_ABSORPTION) eng->absorption_half = 0;
            eng->effects[i] = eng->effects[--eng->effect_count];
        }
    }
    int regen_amp = find_effect_amp(eng, MC_EFF_REGENERATION);
    if (regen_amp >= 0 && eng->totk_hp_q > 0 && eng->totk_hp_q < eng->totk_max_hp_q) {
        eng->regen_accum += dt;
        float interval = (regen_amp >= 1) ? 1.25f : 2.5f;
        if (eng->regen_accum >= interval) {
            eng->regen_accum = 0.0f;
            write_player_health(eng, eng->totk_hp_q + 2);
        }
    }

    eng->base_played=1;
    tick_base_autosave(eng, dt);
    update_mobs(eng, dt);
    update_projectiles_and_particles(eng, dt);
}

/* =========================================================================
 * GUI Quad & Text Emission Helpers
 * ========================================================================= */

static void emit_quad(McRenderSnapshot *s, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      uint16_t tex_id, uint16_t aux, uint8_t kind, uint8_t flags,
                      uint8_t layer, uint32_t rgba) {
    if (s->gui_quad_count >= MC_SNAP_MAX_GUI_QUADS) return;
    McGuiQuad *q = &s->gui_quads[s->gui_quad_count++];
    q->x = x; q->y = y; q->w = w; q->h = h;
    q->u0 = u0; q->v0 = v0; q->u1 = u1; q->v1 = v1;
    q->tex_id = tex_id; q->aux = aux; q->kind = kind;
    q->flags = flags; q->layer = layer; q->_pad = 0; q->rgba_tint = rgba;
}

static void emit_text(McRenderSnapshot *s, float x, float y, float scale,
                      uint32_t rgba, uint8_t flags, uint8_t layer, const char *txt) {
    if (!txt || !txt[0] || s->gui_text_count >= MC_SNAP_MAX_GUI_TEXTS) return;
    McGuiText *t = &s->gui_texts[s->gui_text_count++];
    t->x = x; t->y = y; t->scale = scale; t->rgba = rgba;
    t->flags = flags; t->layer = layer; t->_pad[0] = t->_pad[1] = 0;
    snprintf(t->text, sizeof(t->text), "%s", txt);
}

static void emit_stack(McRenderSnapshot *s, const McItemStack *st, float x, float y, uint8_t layer) {
    if (!st || st->item_id == 0 || st->item_id >= MC_ITM_COUNT) return;
    const McItemMeta *m = &k_items[st->item_id];
    uint8_t flags = st->enchanted ? MC_GUI_FLAG_GLINT : 0;

    if (m->place_block != MC_BLK_AIR && m->place_block != MC_BLK_TORCH && m->place_block != MC_BLK_GLASS_PANE) {
        emit_quad(s, x, y, 16.0f, 16.0f, 0, 0, 1, 1, m->tex_id, m->place_block,
                  MC_GUI_QUAD_ISO_BLOCK, flags, layer, COL_WHITE);
    } else if (st->item_id == MC_ITM_SHIELD) {
        emit_quad(s, x + 3.0f, y, 10.0f, 16.0f, m->u0, m->v0, m->u1, m->v1,
                  m->tex_id, 0, MC_GUI_QUAD_TEX, flags, layer, COL_WHITE);
    } else {
        emit_quad(s, x, y, 16.0f, 16.0f, m->u0, m->v0, m->u1, m->v1,
                  m->tex_id, 0, MC_GUI_QUAD_TEX, flags, layer, COL_WHITE);
    }

    if (st->has_durability && st->durability < 0.99f) {
        float bw = mc_clampf(st->durability, 0.0f, 1.0f) * 13.0f;
        emit_quad(s, x + 2.0f, y + 13.0f, 13.0f, 2.0f, 0, 0, 1, 1, 0, 0, MC_GUI_QUAD_SOLID, 0, layer + 1, RGBA_PACK(0, 0, 0, 255));
        emit_quad(s, x + 2.0f, y + 13.0f, bw, 1.0f, 0, 0, 1, 1, 0, 0, MC_GUI_QUAD_SOLID, 0, layer + 1, RGBA_PACK(64, 240, 64, 255));
    }
    if (st->count > 1) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)st->count);
        emit_text(s, x + 17.0f, y + 9.0f, 1.0f, COL_WHITE, MC_GUI_TEXT_RIGHT | MC_GUI_TEXT_SHADOW, layer + 1, buf);
    }
}

static void emit_hud(const McEngine *eng, McRenderSnapshot *s) {
    float cx = gui_width(eng) * 0.5f;
    float hx = cx - 91.0f;
    float hy = gui_height(eng) - 22.0f;

    /* Crosshair (1st person only, when GUI closed) */
    if (eng->inv_open == MC_INV_CLOSED && eng->view_mode == MC_VIEW_FIRST_PERSON) {
        emit_quad(s, cx - 7.5f, gui_height(eng) * 0.5f - 7.5f, 15.0f, 15.0f, 0, 0, 1, 1,
                  MC_TEX_GUI_CROSSHAIR, 0, MC_GUI_QUAD_TEX, MC_GUI_FLAG_CROSSHAIR, 0, COL_WHITE);
        if (s->cooldown_prog < 0.99f) {
            float ay = gui_height(eng) * 0.5f + 10.0f;
            emit_quad(s, cx - 8.0f, ay, 16.0f, 4.0f, 0, 0, 1, 1,
                      MC_TEX_GUI_CROSSHAIR_ATTACK_BG, 0, MC_GUI_QUAD_TEX, 0, 0, COL_WHITE);
            emit_quad(s, cx - 8.0f, ay, 16.0f * s->cooldown_prog, 4.0f, 0, 0, s->cooldown_prog, 1,
                      MC_TEX_GUI_CROSSHAIR_ATTACK_PROGRESS, 0, MC_GUI_QUAD_TEX, 0, 0, COL_WHITE);
        } else if (eng->crosshair_hit_timer > 0.0f) {
            emit_quad(s, cx - 8.0f, gui_height(eng) * 0.5f + 9.0f, 16.0f, 7.0f, 0, 0, 1, 7.0f / 16.0f,
                      MC_TEX_GUI_CROSSHAIR_ATTACK_FULL, 0, MC_GUI_QUAD_TEX, 0, 0, COL_WHITE);
        }
    }

    /* Hotbar + Offhand + Selection */
    emit_quad(s, hx, hy, 182.0f, 22.0f, 0, 0, 1, 1, MC_TEX_GUI_HOTBAR, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
    if (eng->offhand.item_id != 0) {
        emit_quad(s, hx - 29.0f, hy - 1.0f, 29.0f, 24.0f, 0, 0, 1, 1,
                  MC_TEX_GUI_HOTBAR_OFFHAND_LEFT, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        emit_stack(s, &eng->offhand, hx - 26.0f, hy + 3.0f, 2);
    }
    emit_quad(s, hx - 1.0f + eng->selected_slot * 20.0f, hy - 1.0f, 24.0f, 23.0f, 0, 0, 1, 1,
              MC_TEX_GUI_HOTBAR_SELECTION, 0, MC_GUI_QUAD_TEX, 0, 2, COL_WHITE);
    for (int i = 0; i < 9; i++) {
        emit_stack(s, &eng->hotbar[i], hx + 3.0f + i * 20.0f, hy + 3.0f, 2);
    }

    /* XP bar & Level */
    float xpy = hy - 7.0f;
    emit_quad(s, hx, xpy, 182.0f, 5.0f, 0, 0, 1, 1, MC_TEX_GUI_XP_BG, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
    if (eng->xp_prog > 0.0f) {
        emit_quad(s, hx, xpy, 182.0f * eng->xp_prog, 5.0f, 0, 0, eng->xp_prog, 1,
                  MC_TEX_GUI_XP_PROGRESS, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
    }
    if (eng->xp_level > 0) {
        char lvl[16];
        snprintf(lvl, sizeof(lvl), "%d", eng->xp_level);
        emit_text(s, cx, xpy - 6.0f, 1.0f, COL_XP_GREEN, MC_GUI_TEXT_CENTER | MC_GUI_TEXT_OUTLINE4, 2, lvl);
    }

    /* Hearts & Absorption */
    float hty = xpy - 10.0f;
    int blink = (eng->hearts_half <= 4 && ((int)(eng->time_sec * 6.0f) & 1)) ? 1 : 0;
    for (int i = 0; i < 10; i++) {
        float x = hx + i * 8.0f;
        emit_quad(s, x, hty, 9.0f, 9.0f, 0, 0, 1, 1,
                  blink ? MC_TEX_GUI_HEART_CONTAINER_BLINK : MC_TEX_GUI_HEART_CONTAINER,
                  0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        int rem = eng->hearts_half - i * 2;
        if (rem >= 2) {
            emit_quad(s, x, hty, 9.0f, 9.0f, 0, 0, 1, 1,
                      blink ? MC_TEX_GUI_HEART_FULL_BLINK : MC_TEX_GUI_HEART_FULL,
                      0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        } else if (rem == 1) {
            emit_quad(s, x, hty, 9.0f, 9.0f, 0, 0, 1, 1,
                      blink ? MC_TEX_GUI_HEART_HALF_BLINK : MC_TEX_GUI_HEART_HALF,
                      0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        }
    }
    float next_left_y = hty - 10.0f;
    if (eng->absorption_half > 0) {
        int ah_hearts = (eng->absorption_half + 1) / 2;
        for (int i = 0; i < ah_hearts && i < 10; i++) {
            float x = hx + i * 8.0f;
            emit_quad(s, x, next_left_y, 9.0f, 9.0f, 0, 0, 1, 1, MC_TEX_GUI_HEART_CONTAINER, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
            int rem = eng->absorption_half - i * 2;
            emit_quad(s, x, next_left_y, 9.0f, 9.0f, 0, 0, 1, 1,
                      rem >= 2 ? MC_TEX_GUI_HEART_ABSORB_FULL : MC_TEX_GUI_HEART_ABSORB_HALF,
                      0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        }
        next_left_y -= 10.0f;
    }

    /* Armor */
    if (eng->armor > 0) {
        for (int i = 0; i < 10; i++) {
            int rem = eng->armor - i * 2;
            uint16_t atex = rem >= 2 ? MC_TEX_GUI_ARMOR_FULL : (rem == 1 ? MC_TEX_GUI_ARMOR_HALF : MC_TEX_GUI_ARMOR_EMPTY);
            emit_quad(s, hx + i * 8.0f, next_left_y, 9.0f, 9.0f, 0, 0, 1, 1, atex, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        }
    }

    /* Hunger (right side) */
    for (int i = 0; i < 10; i++) {
        float x = hx + 182.0f - 9.0f - i * 8.0f;
        emit_quad(s, x, hty, 9.0f, 9.0f, 0, 0, 1, 1, MC_TEX_GUI_FOOD_EMPTY, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        int rem = eng->hunger - i * 2;
        if (rem >= 2) emit_quad(s, x, hty, 9.0f, 9.0f, 0, 0, 1, 1, MC_TEX_GUI_FOOD_FULL, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
        else if (rem == 1) emit_quad(s, x, hty, 9.0f, 9.0f, 0, 0, 1, 1, MC_TEX_GUI_FOOD_HALF, 0, MC_GUI_QUAD_TEX, 0, 1, COL_WHITE);
    }

    /* Top-right status effect HUD icons */
    for (int i = 0; i < eng->effect_count; i++) {
        const McEffectSnap *ef = &eng->effects[i];
        uint16_t etex = MC_TEX_EFFECT_RESISTANCE;
        if (ef->effect_type == MC_EFF_FIRE_RESISTANCE) etex = MC_TEX_EFFECT_FIRE_RESISTANCE;
        else if (ef->effect_type == MC_EFF_ABSORPTION) etex = MC_TEX_EFFECT_ABSORPTION;
        else if (ef->effect_type == MC_EFF_REGENERATION) etex = MC_TEX_EFFECT_REGENERATION;
        else if (ef->effect_type == MC_EFF_SPEED) etex = MC_TEX_EFFECT_SPEED;
        float ex = gui_width(eng) - 2.0f - (float)(i + 1) * 25.0f;
        uint8_t a = (ef->seconds_left < 5.0f && ((int)(eng->time_sec * 4.0f) & 1)) ? 130 : 255;
        uint32_t col = RGBA_PACK(255, 255, 255, a);
        emit_quad(s, ex, 2.0f, 24.0f, 24.0f, 0, 0, 1, 1, MC_TEX_GUI_EFFECT_BG, 0, MC_GUI_QUAD_TEX, 0, 1, col);
        emit_quad(s, ex + 3.0f, 5.0f, 18.0f, 18.0f, 0, 0, 1, 1, etex, 0, MC_GUI_QUAD_TEX, 0, 1, col);
    }

    /* Held item fading tooltip */
    if (s->tooltip_alpha > 0.01f && s->tooltip_text[0] && eng->inv_open == MC_INV_CLOSED) {
        uint8_t ta = (uint8_t)(s->tooltip_alpha * 255.0f);
        uint32_t base_c = item_tooltip_color(&eng->hotbar[eng->selected_slot]);
        uint32_t tc = (base_c & 0x00FFFFFFu) | ((uint32_t)ta << 24);
        float ty = (eng->absorption_half > 0 || eng->armor > 0) ? next_left_y - 12.0f : hty - 14.0f;
        emit_text(s, cx, ty, 1.0f, tc, MC_GUI_TEXT_CENTER | MC_GUI_TEXT_SHADOW, 2, s->tooltip_text);
    }
}

static void emit_creative_tab_widget(McRenderSnapshot *s, float x, float y, int top,
                                     int selected, int tab_idx, uint16_t icon_item) {
    uint16_t tex;
    float v0 = 0.0f, v1 = 1.0f, draw_h = 32.0f, draw_y = y;
    if (top) {
        if (selected) {
            tex = (tab_idx == 0) ? MC_TEX_GUI_CREATIVE_TAB_TOP_SEL_1 : MC_TEX_GUI_CREATIVE_TAB_TOP_SEL_2;
        } else {
            tex = (tab_idx == 0) ? MC_TEX_GUI_CREATIVE_TAB_TOP_UNSEL_1 : MC_TEX_GUI_CREATIVE_TAB_TOP_UNSEL_2;
            v0 = 2.0f / 32.0f; draw_h = 30.0f; draw_y += 2.0f;
        }
    } else {
        tex = selected ? MC_TEX_GUI_CREATIVE_TAB_BOT_SEL_1 : MC_TEX_GUI_CREATIVE_TAB_BOT_UNSEL_1;
        if (!selected) { v1 = 28.0f / 32.0f; draw_h = 28.0f; }
    }
    emit_quad(s, x, draw_y, 26.0f, draw_h, 0, v0, 1, v1, tex, 0, MC_GUI_QUAD_TEX, 0, selected ? 5 : 4, COL_WHITE);
    McItemStack icon = make_stack(icon_item, 1);
    emit_stack(s, &icon, x + 5.0f, top ? (y + 9.0f) : (y + 8.0f), selected ? 6 : 5);
}

static void emit_inventory_gui(const McEngine *eng, McRenderSnapshot *s) {
    if (eng->inv_open == MC_INV_CLOSED) return;
    emit_quad(s, 0, 0, gui_width(eng), gui_height(eng), 0, 0, 1, 1, 0, 0, MC_GUI_QUAD_SOLID, 0, 3, COL_SHADOW_DIM);

    float gx = eng->cursor_nx * gui_width(eng), gy = eng->cursor_ny * gui_height(eng);
    if (eng->inv_open == MC_INV_STATION) {
        int i = station_index(eng);
        if (i < 0) return;
        const McWorldBlock *b = &eng->blocks[i];
        int furnace = b->type == MC_BLK_FURNACE;
        float x, panel_y;
        station_panel_origin(eng, &x, &panel_y);
        emit_quad(s,x,panel_y,232,196,0,0,1,1,0,0,MC_GUI_QUAD_SOLID,0,4,RGBA_PACK(198,198,198,255));
        emit_text(s,x+6,panel_y+9,1,COL_DARK_TEXT,MC_GUI_TEXT_LEFT,5,furnace ? "Furnace" : "Crafting Table");
        int rows = furnace ? (b->smelt_output ? 1 : (int)(sizeof(furnace_recipes)/sizeof(*furnace_recipes))) : (int)(sizeof(table_recipes)/sizeof(StationRecipe));
        for (int row=0;row<rows;row++) {
            float y=panel_y+27+row*20;
            int hover=gx>=x+6 && gx<x+226 && gy>=y && gy<y+20;
            char label[64];
            const char *name=(furnace ? furnace_recipes : table_recipes)[row].label;
            if (furnace && b->smelt_output) {
                if (b->cook_left>0) snprintf(label,sizeof(label),"Smelting %s (%ds)",item_en_name(b->smelt_output),(int)ceilf(b->cook_left));
                else snprintf(label,sizeof(label),"Collect %s",item_en_name(b->smelt_output));
                name=label;
            }
            emit_quad(s,x+6,y,220,18,0,0,1,1,0,0,MC_GUI_QUAD_SOLID,0,4,hover ? RGBA_PACK(105,105,135,255) : RGBA_PACK(90,90,90,255));
            emit_text(s,x+10,y+5,1,COL_WHITE,MC_GUI_TEXT_LEFT,5,name);
        }
        if (furnace) {
            char fuel[64]; snprintf(fuel,sizeof(fuel),"Fuel: %u uses (coal or charcoal)",b->fuel_uses);
            emit_text(s,x+6,panel_y+117,1,COL_DARK_TEXT,MC_GUI_TEXT_LEFT,5,fuel);
        }
        emit_text(s,x+6,panel_y+179,1,COL_DARK_TEXT,MC_GUI_TEXT_LEFT,5,eng->tooltip_timer>0 ? eng->tooltip_text : "E / Esc to close");
        return;
    }
    GuiSlotRef hover_ref; float hx = 0, hy = 0; int htab = -1, hcraft = 0;
    int has_hover = find_hovered_gui_target((McEngine *)eng, gx, gy, &hover_ref, &hx, &hy, &htab, &hcraft);

    if (eng->inv_open == MC_INV_SURVIVAL) {
        float cx = (gui_width(eng) - 176.0f) * 0.5f, cy = (gui_height(eng) - 166.0f) * 0.5f;
        emit_quad(s, cx, cy, 176.0f, 166.0f, 0, 0, 176.0f / 256.0f, 166.0f / 256.0f,
                  MC_TEX_GUI_INVENTORY, 0, MC_GUI_QUAD_TEX, 0, 4, COL_WHITE);
        s->inv_paperdoll_x = cx + 26.0f; s->inv_paperdoll_y = cy + 8.0f;
        s->inv_paperdoll_w = 49.0f; s->inv_paperdoll_h = 70.0f;
        emit_quad(s, cx + 104.0f, cy + 61.0f, 20.0f, 18.0f, 0, 0, 1, 1,
                  hcraft ? MC_TEX_GUI_RECIPE_BOOK_BTN_HI : MC_TEX_GUI_RECIPE_BOOK_BTN, 0, MC_GUI_QUAD_TEX, 0, 5, COL_WHITE);
        emit_text(s, cx + 97.0f, cy + 6.0f, 1.0f, COL_DARK_TEXT, MC_GUI_TEXT_LEFT, 5,
                  lang_str("container.crafting", "Crafting"));

        for (int a = 0; a < 4; a++) emit_stack(s, &eng->armor_slots[a], cx + 8.0f, cy + 8.0f + a * 18.0f, 5);
        if (eng->offhand.item_id == 0) {
            emit_quad(s, cx + 77.0f, cy + 62.0f, 16.0f, 16.0f, 0, 0, 1, 1, MC_TEX_GUI_SLOT_SHIELD, 0, MC_GUI_QUAD_TEX, 0, 5, COL_WHITE);
        } else {
            emit_stack(s, &eng->offhand, cx + 77.0f, cy + 62.0f, 5);
        }
        for (int r = 0; r < 2; r++) for (int c = 0; c < 2; c++)
            emit_stack(s, &eng->craft_slots[r * 2 + c], cx + 98.0f + c * 18.0f, cy + 18.0f + r * 18.0f, 5);
        emit_stack(s, &eng->craft_result, cx + 154.0f, cy + 28.0f, 5);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 9; c++)
            emit_stack(s, &eng->inv_slots[r * 9 + c], cx + 8.0f + c * 18.0f, cy + 84.0f + r * 18.0f, 5);
        for (int c = 0; c < 9; c++) emit_stack(s, &eng->hotbar[c], cx + 8.0f + c * 18.0f, cy + 142.0f, 5);
    } else {
        float cx = (gui_width(eng) - 195.0f) * 0.5f, cy = (gui_height(eng) - 136.0f) * 0.5f;
        static const uint16_t top_icons[5] = {
            MC_ITM_OAK_LOG, MC_ITM_DIAMOND_SWORD, MC_ITM_DIAMOND_PICKAXE, MC_ITM_COOKED_BEEF, MC_ITM_REDSTONE
        };
        for (int t = 0; t < 5; t++) {
            emit_creative_tab_widget(s, cx + t * 27.0f, cy - 28.0f, 1, eng->creative_tab == t, t, top_icons[t]);
        }
        emit_creative_tab_widget(s, cx + 169.0f, cy - 28.0f, 1, eng->creative_tab == MC_CTAB_SEARCH, 1, MC_ITM_EMERALD);
        emit_creative_tab_widget(s, cx, cy + 132.0f, 0, 0, 0, MC_ITM_CRAFTING_TABLE);
        emit_creative_tab_widget(s, cx + 169.0f, cy + 132.0f, 0, eng->creative_tab == MC_CTAB_SURV_INV, 1, MC_ITM_DIAMOND_CHESTPLATE);

        uint16_t bg_tex = MC_TEX_GUI_CREATIVE_TAB_ITEMS;
        if (eng->creative_tab == MC_CTAB_SEARCH) bg_tex = MC_TEX_GUI_CREATIVE_TAB_SEARCH;
        else if (eng->creative_tab == MC_CTAB_SURV_INV) bg_tex = MC_TEX_GUI_CREATIVE_TAB_INVENTORY;
        emit_quad(s, cx, cy, 195.0f, 136.0f, 0, 0, 195.0f / 256.0f, 136.0f / 256.0f,
                  bg_tex, 0, MC_GUI_QUAD_TEX, 0, 4, COL_WHITE);

        for (int c = 0; c < 9; c++) emit_stack(s, &eng->hotbar[c], cx + 9.0f + c * 18.0f, cy + 112.0f, 5);

        if (eng->creative_tab == MC_CTAB_SURV_INV) {
            s->inv_paperdoll_x = cx + 73.0f; s->inv_paperdoll_y = cy + 6.0f;
            s->inv_paperdoll_w = 32.0f; s->inv_paperdoll_h = 43.0f;
            static const float ax[4] = {54.0f, 54.0f, 108.0f, 108.0f};
            static const float ay[4] = {6.0f, 33.0f, 6.0f, 33.0f};
            for (int a = 0; a < 4; a++) emit_stack(s, &eng->armor_slots[a], cx + ax[a], cy + ay[a], 5);
            if (eng->offhand.item_id == 0) {
                emit_quad(s, cx + 35.0f, cy + 20.0f, 16.0f, 16.0f, 0, 0, 1, 1, MC_TEX_GUI_SLOT_SHIELD, 0, MC_GUI_QUAD_TEX, 0, 5, COL_WHITE);
            } else {
                emit_stack(s, &eng->offhand, cx + 35.0f, cy + 20.0f, 5);
            }
            for (int r = 0; r < 3; r++) for (int c = 0; c < 9; c++)
                emit_stack(s, &eng->inv_slots[r * 9 + c], cx + 9.0f + c * 18.0f, cy + 54.0f + r * 18.0f, 5);
        } else {
            static const char *tab_keys[6] = {
                "itemGroup.buildingBlocks", "itemGroup.combat", "itemGroup.tools",
                "itemGroup.foodAndDrink", "container.inventory", "itemGroup.search"
            };
            static const char *tab_fbs[6] = {
                "Building Blocks", "Combat", "Tools & Utilities",
                "Food & Drinks", "Miscellaneous", "Search Items"
            };
            emit_text(s, cx + 8.0f, cy + 6.0f, 1.0f, COL_DARK_TEXT, MC_GUI_TEXT_LEFT, 5,
                      lang_str(tab_keys[eng->creative_tab], tab_fbs[eng->creative_tab]));
            if (eng->creative_tab == MC_CTAB_SEARCH) {
                emit_text(s, cx + 83.0f, cy + 6.0f, 1.0f, COL_WHITE, MC_GUI_TEXT_LEFT, 5, eng->search_text);
            }

            uint16_t cat[MC_ITM_COUNT];
            int total = collect_creative_catalog(eng, cat, MC_ITM_COUNT);
            int rows = (total + 8) / 9;
            int max_row = rows > 5 ? rows - 5 : 0;
            int start_row = (int)roundf(eng->creative_scroll * (float)max_row);
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 9; c++) {
                    int idx = (start_row + r) * 9 + c;
                    if (idx < total) {
                        McItemStack it = make_stack(cat[idx], 1);
                        emit_stack(s, &it, cx + 9.0f + c * 18.0f, cy + 18.0f + r * 18.0f, 5);
                    }
                }
            }
            float sc_y = cy + 18.0f + eng->creative_scroll * 95.0f;
            emit_quad(s, cx + 175.0f, sc_y, 12.0f, 15.0f, 0, 0, 1, 1,
                      max_row > 0 ? MC_TEX_GUI_CREATIVE_SCROLLER : MC_TEX_GUI_CREATIVE_SCROLLER_DISABLED,
                      0, MC_GUI_QUAD_TEX, 0, 5, COL_WHITE);
        }
    }

    /* Slot hover highlight & English tooltip box */
    const char *hover_name = NULL;
    uint32_t hover_col = COL_WHITE;
    if (has_hover) {
        emit_quad(s, hx - 4.0f, hy - 4.0f, 24.0f, 24.0f, 0, 0, 1, 1,
                  MC_TEX_GUI_SLOT_HIGHLIGHT_BACK, 0, MC_GUI_QUAD_TEX, 0, 4, COL_WHITE);
        emit_quad(s, hx - 4.0f, hy - 4.0f, 24.0f, 24.0f, 0, 0, 1, 1,
                  MC_TEX_GUI_SLOT_HIGHLIGHT_FRONT, 0, MC_GUI_QUAD_TEX, 0, 6, COL_WHITE);
        if (hover_ref.slot_kind == 3) {
            hover_name = "Destroy Item";
        } else if (hover_ref.is_creative_catalog) {
            McItemStack tmp = make_stack((uint16_t)hover_ref.catalog_index, 1);
            hover_name = item_en_name(tmp.item_id);
            hover_col = item_tooltip_color(&tmp);
        } else if (hover_ref.slot_ptr && hover_ref.slot_ptr->item_id != 0) {
            hover_name = item_en_name(hover_ref.slot_ptr->item_id);
            hover_col = item_tooltip_color(hover_ref.slot_ptr);
        }
    } else if (htab >= 0) {
        static const char *tnames[7] = {
            "Building Blocks", "Combat", "Tools & Utilities", "Food & Drinks",
            "Miscellaneous", "Search Items", "Survival Inventory"
        };
        hover_name = tnames[htab];
    } else if (hcraft) {
        hover_name = (eng->inv_open == MC_INV_SURVIVAL) ? "Creative Catalog" : "Crafting";
    }

    if (eng->cursor_stack.item_id != 0) {
        emit_stack(s, &eng->cursor_stack, gx - 8.0f, gy - 8.0f, 6);
    } else if (hover_name && hover_name[0]) {
        snprintf(s->hover_text, sizeof(s->hover_text), "%s", hover_name);
        int pw = mc_asset_measure_text(hover_name);
        if (pw < 12) pw = (int)strlen(hover_name) * 6;
        float box_w = (float)pw + 8.0f, box_h = 16.0f;
        float bx = gx + 10.0f, by = gy - 12.0f;
        if (bx + box_w > gui_width(eng) - 2.0f) bx = gx - box_w - 8.0f;
        if (by < 2.0f) by = 2.0f;
        emit_quad(s, bx - 8.0f, by - 8.0f, box_w + 16.0f, box_h + 16.0f, 0, 0, 1, 1,
                  MC_TEX_GUI_TOOLTIP_BG, 9, MC_GUI_QUAD_TEX, MC_GUI_FLAG_NINE_SLICE, 7, COL_WHITE);
        emit_quad(s, bx - 8.0f, by - 8.0f, box_w + 16.0f, box_h + 16.0f, 0, 0, 1, 1,
                  MC_TEX_GUI_TOOLTIP_FRAME, 10, MC_GUI_QUAD_TEX, MC_GUI_FLAG_NINE_SLICE, 7, COL_WHITE);
        emit_text(s, bx + 4.0f, by + 4.0f, 1.0f, hover_col, MC_GUI_TEXT_LEFT | MC_GUI_TEXT_SHADOW, 7, hover_name);
    }
}

/* =========================================================================
 * Immutable Snapshot Export
 * ========================================================================= */

static int pane_connects_to(uint16_t bt) {
    if (bt >= MC_BLK_COUNT) return 0;
    return k_blocks[bt].opaque || bt == MC_BLK_GLASS ||
           bt == MC_BLK_GLASS_PANE || bt == MC_BLK_GLOWSTONE;
}

static uint8_t block_face_mask(const McEngine *eng, const McWorldBlock *b) {
    uint8_t mask = 0x3fu;
    static const int ox[6] = {-1, 1, 0, 0, 0, 0};
    static const int oy[6] = {0, 0, -1, 1, 0, 0};
    static const int oz[6] = {0, 0, 0, 0, -1, 1};
    if (b->type == MC_BLK_GLASS_PANE) {
        mask = (1u << 2) | (1u << 3);
        for (int f = 0; f < 6; f++) {
            int ni = find_block_idx(eng, b->ix + ox[f], b->iy + oy[f], b->iz + oz[f]);
            if (ni < 0) continue;
            uint16_t nt = eng->blocks[ni].type;
            if (f == 2 || f == 3) {
                if (nt == MC_BLK_GLASS_PANE || (nt < MC_BLK_COUNT && k_blocks[nt].opaque)) {
                    mask &= (uint8_t)~(1u << f);
                }
            } else if (pane_connects_to(nt)) {
                mask |= (uint8_t)(1u << f);
            }
        }
    } else if (b->type < MC_BLK_COUNT && k_blocks[b->type].opaque) {
        for (int f = 0; f < 6; f++) {
            int ni = find_block_idx(eng, b->ix + ox[f], b->iy + oy[f], b->iz + oz[f]);
            if (ni >= 0 && eng->blocks[ni].type < MC_BLK_COUNT && k_blocks[eng->blocks[ni].type].opaque) {
                mask &= (uint8_t)~(1u << f);
            }
        }
    }
    return mask;
}

void mc_engine_export_snapshot(McEngine *eng, McRenderSnapshot *out) {
    memset(out, 0, sizeof(*out));
    out->version = MC_SNAP_VERSION;
    out->tick = eng->tick_count;
    out->time_sec = eng->time_sec;
    out->mainhand_scale = eng->mainhand_scale;
    out->offhand_scale = eng->offhand_scale;
    out->shaders_enabled = eng->shaders_enabled ? 1u : 0u;
    out->gui_w = gui_width(eng);
    out->gui_h = gui_height(eng);
    int gp_active = (eng->active && eng->gameplay_active) ? 1 : 0;
    if (!gp_active) {
        out->active = 0;
        out->mode = 0;
        out->view_mode = eng->view_mode;
        return;
    }
    out->active = 1;
    out->mode = eng->mode;
    out->view_mode = eng->view_mode;

    out->cam_valid = eng->cam_valid;
    memcpy(out->cam_vp, eng->cam_vp, sizeof(out->cam_vp));
    memcpy(out->cam_pos, eng->cam_pos, sizeof(out->cam_pos));
    memcpy(out->cam_right, eng->cam_right, sizeof(out->cam_right));
    memcpy(out->cam_up, eng->cam_up, sizeof(out->cam_up));
    memcpy(out->cam_fwd, eng->cam_fwd, sizeof(out->cam_fwd));
    out->cam_near = eng->cam_near; out->cam_far = eng->cam_far;
    out->cam_fov_rad = eng->cam_fov_rad; out->cam_aspect = eng->cam_aspect;
    out->render_w = eng->render_w; out->render_h = eng->render_h;

    out->has_target = eng->has_target;
    out->target_ix = eng->target_ix; out->target_iy = eng->target_iy; out->target_iz = eng->target_iz;
    out->target_x = (float)eng->target_ix + 0.5f;
    out->target_y = (float)eng->target_iy + 0.5f;
    out->target_z = (float)eng->target_iz + 0.5f;
    out->target_face = eng->target_face;
    int8_t mstage = (eng->has_target && eng->mining_progress > 0.01f)
                  ? (int8_t)mc_clampf(floorf(eng->mining_progress * 10.0f), 0.0f, 9.0f) : -1;
    out->target_mining_stage = mstage;
    out->has_hyrule_target = eng->has_hyrule_target;
    memcpy(out->hyrule_hit, eng->hyrule_hit, sizeof(out->hyrule_hit));
    memcpy(out->hyrule_normal, eng->hyrule_normal, sizeof(out->hyrule_normal));

    /* Blocks + face visibility mask */
    if (eng->block_faces_dirty) {
        for (int i = 0; i < eng->block_count; i++)
            eng->block_face_masks[i] = block_face_mask(eng, &eng->blocks[i]);
        eng->block_faces_dirty = 0;
    }
    int nb = 0;
    for (int i = 0; i < eng->block_count && nb < MC_SNAP_MAX_BLOCKS; i++) {
        const McWorldBlock *b = &eng->blocks[i];
        if (!b->active) continue;
        McBlockSnap *sb = &out->blocks[nb++];
        sb->ix = b->ix; sb->iy = b->iy; sb->iz = b->iz;
        sb->x = (float)b->ix + 0.5f; sb->y = (float)b->iy + 0.5f; sb->z = (float)b->iz + 0.5f;
        sb->block_type = b->type;
        sb->light = b->light;
        sb->facing = b->facing;
        sb->tnt_fuse = b->tnt_fuse;
        sb->flash_white = b->flash_white;
        sb->mining_stage = (eng->has_target && b->ix == eng->target_ix && b->iy == eng->target_iy &&
                            b->iz == eng->target_iz) ? mstage : -1;
        sb->face_mask = eng->block_face_masks[i];
    }
    out->block_count = nb;

    for (int i = 0; i < MC_SNAP_MAX_MOBS; i++) {
        const McMob *m = &eng->mobs[i];
        if (!m->active) continue;
        McMobSnap *sm = &out->mobs[out->mob_count++];
        sm->active = 1; sm->kind = m->kind;
        sm->hurt_flash = m->hurt_flash;
        sm->fuse_flash = (m->kind == MC_MOB_CREEPER && m->swell > 0.05f && ((int)(m->swell * 8.0f) & 1)) ? 1 : 0;
        sm->x = m->x; sm->y = m->y; sm->z = m->z;
        sm->yaw = m->yaw; sm->pitch = m->pitch;
        sm->walk_phase = m->walk_phase; sm->walk_amp = m->walk_amp;
        sm->swing_arm = m->swing_arm; sm->swell = m->swell;
        sm->hp = m->hp; sm->max_hp = m->max_hp;
    }

    for (int i = 0; i < MC_SNAP_MAX_PRIMED_TNT; i++) {
        const McPrimedTnt *t = &eng->primed_tnt[i];
        if (!t->active) continue;
        McPrimedTntSnap *st = &out->primed_tnt[out->primed_tnt_count++];
        st->active = 1; st->flash_white = t->flash_white;
        st->x = t->x; st->y = t->y; st->z = t->z;
        st->fuse_progress = 1.0f - mc_clampf(t->fuse_left / (t->fuse_total > 0.0f ? t->fuse_total : 4.0f), 0.0f, 1.0f);
    }

    for (int i = 0; i < MC_SNAP_MAX_DROPPED; i++) {
        const McDroppedItem *d = &eng->dropped[i];
        if (!d->active) continue;
        McDroppedItemSnap *sd = &out->dropped[out->dropped_count++];
        sd->active = 1; sd->enchanted = d->enchanted; sd->item_type = d->item_type; sd->count = d->count;
        uint16_t pb = (d->item_type < MC_ITM_COUNT) ? k_items[d->item_type].place_block : 0;
        sd->is_block = (pb != 0 && pb != MC_BLK_TORCH && pb != MC_BLK_GLASS_PANE) ? 1 : 0;
        sd->block_type = pb;
        sd->x = d->x; sd->y = d->y; sd->z = d->z; sd->spin_yaw = d->spin_yaw;
    }

    for (int i = 0; i < MC_SNAP_MAX_PROJECTILES; i++) {
        const McProjectile *p = &eng->projectiles[i];
        if (!p->active) continue;
        McProjectileSnap *sp = &out->projectiles[out->projectile_count++];
        sp->active = 1; sp->kind = p->kind; sp->stuck = p->stuck; sp->crit = p->crit;
        sp->x = p->x; sp->y = p->y; sp->z = p->z;
        sp->vx = p->vx; sp->vy = p->vy; sp->vz = p->vz;
    }

    for (int i = 0; i < MC_SNAP_MAX_PARTICLES; i++) {
        const McParticle *pt = &eng->particles[i];
        if (!pt->active) continue;
        McParticleSnap *spt = &out->particles[out->particle_count++];
        spt->active = 1; spt->kind = pt->kind; spt->rgba = pt->rgba;
        float an = mc_clampf(pt->age / pt->max_age, 0.0f, 0.999f);
        spt->age_norm = an;
        spt->frame = (uint8_t)(an * (float)pt->frame_count);
        spt->x = pt->x; spt->y = pt->y; spt->z = pt->z; spt->size = pt->size;
    }

    /* Viewmodel & 3rd-person player */
    const McItemStack *mh = &eng->hotbar[eng->selected_slot];
    out->viewmodel.mainhand_item = mh->item_id;
    out->viewmodel.mainhand_enchanted = mh->enchanted;
    float bow_p = eng->bow_drawing ? mc_clampf(eng->bow_charge_sec / 1.0f, 0.0f, 1.0f) : 0.0f;
    out->viewmodel.bow_pull = bow_p;
    if (mh->item_id == MC_ITM_BOW) {
        out->viewmodel.bow_raise = eng->bow_raise;
        out->viewmodel.bow_release = eng->bow_release;
    }
    out->viewmodel.bow_pull_stage = !eng->bow_drawing ? 0 : (bow_p >= 0.9f ? 3 : (bow_p >= 0.65f ? 2 : 1));
    out->viewmodel.attack_swing = (eng->swing_dur > 0.0f) ? (eng->swing_timer / eng->swing_dur) : 0.0f;
    if (eng->mining_swing_timer > 0.0f) {
        out->viewmodel.mining_swing = 1.0f - eng->mining_swing_timer / MC_MINING_SWING_SECONDS;
        out->viewmodel.attack_swing = 0.0f;
    }
    out->viewmodel.attack_cooldown = (eng->attack_cd_dur > 0.0f)
        ? (1.0f - mc_clampf(eng->attack_cd_timer / eng->attack_cd_dur, 0.0f, 1.0f)) : 1.0f;
    out->viewmodel.equip_drop = eng->equip_drop;
    if (mh->item_id < MC_ITM_COUNT && k_items[mh->item_id].is_food) {
        float raise = eng->eat_raise;
        out->viewmodel.eat_raise = raise * raise * (3.0f - 2.0f * raise);
        if (eng->eating) {
            float phase = eng->eat_snd_accum / MC_EAT_BITE_SECONDS;
            out->viewmodel.eat_bite = out->viewmodel.eat_raise *
                (0.5f + 0.5f * cosf(phase * 2.0f * (float)M_PI));
        }
    }
    float bob_amp = eng->elytra_gliding ? 0.0f : eng->walk_amp;
    out->viewmodel.walk_bob_x = sinf(eng->walk_phase) * bob_amp * 0.04f;
    out->viewmodel.walk_bob_y = fabsf(cosf(eng->walk_phase)) * bob_amp * 0.035f;
    out->viewmodel.offhand_item = eng->offhand.item_id;
    out->viewmodel.offhand_enchanted = eng->offhand.enchanted;
    out->viewmodel.shield_block = eng->shield_block;
    out->viewmodel.totem_pop = (eng->totem_pop_timer > 0.0f) ? (1.0f - eng->totem_pop_timer / 1.6f) : 0.0f;
    out->viewmodel.hurt_flash = mc_clampf(eng->hurt_flash_timer / 0.45f, 0.0f, 1.0f);

    out->player.valid = eng->player_pos_valid;
    memcpy(out->player.pos, eng->player_pos, sizeof(out->player.pos));
    if (eng->elytra_gliding) {
        out->player.yaw = eng->elytra_yaw;
        out->player.head_yaw = eng->look_yaw;
    } else if (eng->body_yaw_init) {
        out->player.yaw = eng->body_yaw;
        out->player.head_yaw = eng->head_yaw;
    } else {
        float fallback_yaw = eng->player_totk_yaw_valid
            ? eng->player_totk_yaw
            : (eng->cam_valid ? atan2f(eng->cam_fwd[0], eng->cam_fwd[2]) : eng->look_yaw);
        out->player.yaw = fallback_yaw;
        out->player.head_yaw = fallback_yaw;
    }
    out->player.pitch = eng->flight_look_active
        ? eng->look_pitch
        : (eng->cam_valid ? asinf(mc_clampf(eng->cam_fwd[1], -0.99f, 0.99f)) : eng->look_pitch);
    out->player.walk_phase = eng->walk_phase;
    out->player.walk_amp = bob_amp;
    out->player.swing_arm = eng->mining_swing_timer > 0.0f
        ? out->viewmodel.mining_swing : out->viewmodel.attack_swing;
    out->player.shield_block = eng->shield_block;
    out->player.eat_raise = out->viewmodel.eat_raise;
    out->player.eat_bite = out->viewmodel.eat_bite;
    out->player.bow_pull = bow_p;
    out->player.bow_pull_stage = out->viewmodel.bow_pull_stage;
    out->player.bow_raise = out->viewmodel.bow_raise;
    out->player.bow_release = out->viewmodel.bow_release;
    out->player.elytra_open = eng->elytra_open;
    out->player.elytra_pitch = eng->elytra_pitch;
    out->player.elytra_bank = eng->elytra_bank;
    out->player.crouching = eng->player_crouching;
    out->player.hurt_flash = (eng->hurt_flash_timer > 0.2f) ? 1 : 0;
    out->player.has_elytra = (eng->armor_slots[1].item_id == MC_ITM_ELYTRA) ? 1 : 0;
    out->player.mainhand_item = mh->item_id;
    out->player.mainhand_enchanted = mh->enchanted;
    out->player.offhand_item = eng->offhand.item_id;
    out->player.has_helmet = (eng->armor_slots[0].item_id != 0) ? 1 : 0;
    out->player.has_chestplate = (eng->armor_slots[1].item_id == MC_ITM_DIAMOND_CHESTPLATE) ? 1 : 0;
    out->player.has_leggings = (eng->armor_slots[2].item_id != 0) ? 1 : 0;
    out->player.has_boots = (eng->armor_slots[3].item_id != 0) ? 1 : 0;

    /* High-level HUD & Inv fields */
    out->hearts_half = eng->hearts_half;
    out->max_hearts_half = eng->max_hearts_half;
    out->absorption_half = eng->absorption_half;
    out->hunger = eng->hunger;
    out->armor = eng->armor;
    out->xp_level = eng->xp_level;
    out->xp_prog = eng->xp_prog;
    out->selected_slot = eng->selected_slot;
    memcpy(out->hotbar, eng->hotbar, sizeof(out->hotbar));
    out->offhand = eng->offhand;
    memcpy(out->armor_slots, eng->armor_slots, sizeof(out->armor_slots));
    memcpy(out->inv_slots, eng->inv_slots, sizeof(out->inv_slots));
    memcpy(out->craft_slots, eng->craft_slots, sizeof(out->craft_slots));
    out->craft_result = eng->craft_result;
    out->effect_count = eng->effect_count;
    memcpy(out->active_effects, eng->effects, sizeof(out->active_effects));
    out->cooldown_prog = out->viewmodel.attack_cooldown;
    snprintf(out->tooltip_text, sizeof(out->tooltip_text), "%s", eng->tooltip_text);
    out->tooltip_alpha = mc_clampf(eng->tooltip_timer * 1.2f, 0.0f, 1.0f);
    out->crosshair_hit_flash = (eng->crosshair_hit_timer > 0.0f) ? 1 : 0;
    out->inv_open = eng->inv_open;
    out->creative_tab = eng->creative_tab;
    out->creative_scroll = eng->creative_scroll;
    out->cursor_stack = eng->cursor_stack;
    out->cursor_nx = eng->cursor_nx;
    out->cursor_ny = eng->cursor_ny;

    if (out->active && eng->mode == 1) {
        if (eng->inv_open == MC_INV_CLOSED) {
            emit_hud(eng, out);
        }
        emit_inventory_gui(eng, out);
    }
}
