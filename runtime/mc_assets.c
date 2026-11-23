#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "mc_assets.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MC_MAX_EN_US 128

static const char *const k_tex_paths[MC_TEX_COUNT] = {
    [MC_TEX_GUI_HOTBAR]                       = "textures/gui/hotbar.png",
    [MC_TEX_GUI_HOTBAR_SELECTION]             = "textures/gui/hotbar_selection.png",
    [MC_TEX_GUI_HOTBAR_OFFHAND_LEFT]          = "textures/gui/hotbar_offhand_left.png",
    [MC_TEX_GUI_HOTBAR_OFFHAND_RIGHT]         = "textures/gui/hotbar_offhand_right.png",
    [MC_TEX_GUI_CROSSHAIR]                    = "textures/gui/crosshair.png",
    [MC_TEX_GUI_CROSSHAIR_ATTACK_BG]          = "textures/gui/crosshair_attack_indicator_background.png",
    [MC_TEX_GUI_CROSSHAIR_ATTACK_FULL]        = "textures/gui/crosshair_attack_indicator_full.png",
    [MC_TEX_GUI_CROSSHAIR_ATTACK_PROGRESS]    = "textures/gui/crosshair_attack_indicator_progress.png",
    [MC_TEX_GUI_XP_BG]                        = "textures/gui/experience_bar_background.png",
    [MC_TEX_GUI_XP_PROGRESS]                  = "textures/gui/experience_bar_progress.png",
    [MC_TEX_GUI_HEART_CONTAINER]              = "textures/gui/heart_container.png",
    [MC_TEX_GUI_HEART_CONTAINER_BLINK]        = "textures/gui/heart_container_blinking.png",
    [MC_TEX_GUI_HEART_FULL]                   = "textures/gui/heart_full.png",
    [MC_TEX_GUI_HEART_FULL_BLINK]             = "textures/gui/heart_full_blinking.png",
    [MC_TEX_GUI_HEART_HALF]                   = "textures/gui/heart_half.png",
    [MC_TEX_GUI_HEART_HALF_BLINK]             = "textures/gui/heart_half_blinking.png",
    [MC_TEX_GUI_HEART_ABSORB_FULL]            = "textures/gui/heart_absorbing_full.png",
    [MC_TEX_GUI_HEART_ABSORB_HALF]            = "textures/gui/heart_absorbing_half.png",
    [MC_TEX_GUI_FOOD_EMPTY]                   = "textures/gui/food_empty.png",
    [MC_TEX_GUI_FOOD_HALF]                    = "textures/gui/food_half.png",
    [MC_TEX_GUI_FOOD_FULL]                    = "textures/gui/food_full.png",
    [MC_TEX_GUI_ARMOR_EMPTY]                  = "textures/gui/armor_empty.png",
    [MC_TEX_GUI_ARMOR_HALF]                   = "textures/gui/armor_half.png",
    [MC_TEX_GUI_ARMOR_FULL]                   = "textures/gui/armor_full.png",
    [MC_TEX_GUI_EFFECT_BG]                    = "textures/gui/effect_background.png",
    [MC_TEX_GUI_EFFECT_BG_AMBIENT]            = "textures/gui/effect_background_ambient.png",
    [MC_TEX_GUI_TOOLTIP_BG]                   = "textures/gui/tooltip_background.png",
    [MC_TEX_GUI_TOOLTIP_FRAME]                = "textures/gui/tooltip_frame.png",
    [MC_TEX_RED_WOOL] = "textures/block/red_wool.png",
    [MC_TEX_RED_BED] = "textures/entity/bed/red.png",
    [MC_TEX_WOLF] = "textures/entity/wolf/wolf_tame.png",
    [MC_TEX_SAND] = "textures/block/sand.png",
    [MC_TEX_BEEF] = "textures/item/beef.png",
    [MC_TEX_GUI_INVENTORY]                    = "textures/gui/inventory.png",
    [MC_TEX_GUI_CREATIVE_TAB_ITEMS]           = "textures/gui/creative_tab_items.png",
    [MC_TEX_GUI_CREATIVE_TAB_SEARCH]          = "textures/gui/creative_tab_item_search.png",
    [MC_TEX_GUI_CREATIVE_TAB_INVENTORY]       = "textures/gui/creative_tab_inventory.png",
    [MC_TEX_GUI_CREATIVE_SCROLLER]            = "textures/gui/creative_scroller.png",
    [MC_TEX_GUI_CREATIVE_SCROLLER_DISABLED]   = "textures/gui/creative_scroller_disabled.png",
    [MC_TEX_GUI_CREATIVE_TAB_TOP_SEL_1]       = "textures/gui/creative_tab_top_selected_1.png",
    [MC_TEX_GUI_CREATIVE_TAB_TOP_SEL_2]       = "textures/gui/creative_tab_top_selected_2.png",
    [MC_TEX_GUI_CREATIVE_TAB_TOP_UNSEL_1]     = "textures/gui/creative_tab_top_unselected_1.png",
    [MC_TEX_GUI_CREATIVE_TAB_TOP_UNSEL_2]     = "textures/gui/creative_tab_top_unselected_2.png",
    [MC_TEX_GUI_CREATIVE_TAB_BOT_SEL_1]       = "textures/gui/creative_tab_bottom_selected_1.png",
    [MC_TEX_GUI_CREATIVE_TAB_BOT_UNSEL_1]     = "textures/gui/creative_tab_bottom_unselected_1.png",
    [MC_TEX_GUI_SLOT_SHIELD]                  = "textures/gui/slot_shield.png",
    [MC_TEX_GUI_SLOT_HIGHLIGHT_BACK]          = "textures/gui/slot_highlight_back.png",
    [MC_TEX_GUI_SLOT_HIGHLIGHT_FRONT]         = "textures/gui/slot_highlight_front.png",
    [MC_TEX_GUI_RECIPE_BOOK_BTN]              = "textures/gui/recipe_book_button.png",
    [MC_TEX_GUI_RECIPE_BOOK_BTN_HI]           = "textures/gui/recipe_book_button_highlighted.png",

    [MC_TEX_EFFECT_RESISTANCE]                = "textures/mob_effect/resistance.png",
    [MC_TEX_EFFECT_FIRE_RESISTANCE]           = "textures/mob_effect/fire_resistance.png",
    [MC_TEX_EFFECT_ABSORPTION]                = "textures/mob_effect/absorption.png",
    [MC_TEX_EFFECT_REGENERATION]              = "textures/mob_effect/regeneration.png",
    [MC_TEX_EFFECT_SPEED]                     = "textures/mob_effect/speed.png",

    [MC_TEX_FONT_ASCII]                       = "textures/font/ascii.png",

    [MC_TEX_ITEM_COOKED_BEEF]                 = "textures/item/cooked_beef.png",
    [MC_TEX_ITEM_DIAMOND_SWORD]               = "textures/item/diamond_sword.png",
    [MC_TEX_ITEM_BOW]                         = "textures/item/bow.png",
    [MC_TEX_ITEM_BOW_PULLING_0]               = "textures/item/bow_pulling_0.png",
    [MC_TEX_ITEM_BOW_PULLING_1]               = "textures/item/bow_pulling_1.png",
    [MC_TEX_ITEM_BOW_PULLING_2]               = "textures/item/bow_pulling_2.png",
    [MC_TEX_ITEM_DIAMOND_AXE]                 = "textures/item/diamond_axe.png",
    [MC_TEX_ITEM_GOLDEN_APPLE]                = "textures/item/golden_apple.png",
    [MC_TEX_ITEM_DIAMOND_PICKAXE]             = "textures/item/diamond_pickaxe.png",
    [MC_TEX_ITEM_FIREWORK_ROCKET]             = "textures/item/firework_rocket.png",
    [MC_TEX_ITEM_BONE]                        = "textures/item/bone.png",
    [MC_TEX_ITEM_ARROW]                       = "textures/item/arrow.png",
    [MC_TEX_ITEM_TOTEM_OF_UNDYING]            = "textures/item/totem_of_undying.png",
    [MC_TEX_ITEM_ELYTRA]                      = "textures/item/elytra.png",
    [MC_TEX_ITEM_ELYTRA_BROKEN]               = "textures/item/elytra_broken.png",
    [MC_TEX_ITEM_FLINT_AND_STEEL]             = "textures/item/flint_and_steel.png",
    [MC_TEX_ITEM_WATER_BUCKET]                = "textures/item/water_bucket.png",
    [MC_TEX_ITEM_LAVA_BUCKET]                 = "textures/item/lava_bucket.png",
    [MC_TEX_ITEM_ENDER_PEARL]                 = "textures/item/ender_pearl.png",
    [MC_TEX_ITEM_BREAD]                       = "textures/item/bread.png",
    [MC_TEX_ITEM_APPLE]                       = "textures/item/apple.png",
    [MC_TEX_ITEM_CHARCOAL]                    = "textures/item/charcoal.png",
    [MC_TEX_ITEM_COAL]                        = "textures/item/coal.png",
    [MC_TEX_ITEM_GUNPOWDER]                   = "textures/item/gunpowder.png",
    [MC_TEX_ITEM_ROTTEN_FLESH]                = "textures/item/rotten_flesh.png",
    [MC_TEX_ITEM_SPIDER_EYE]                  = "textures/item/spider_eye.png",
    [MC_TEX_ITEM_STRING]                      = "textures/item/string.png",
    [MC_TEX_ITEM_SLIME_BALL]                  = "textures/item/slime_ball.png",
    [MC_TEX_ITEM_EXPERIENCE_BOTTLE]           = "textures/item/experience_bottle.png",
    [MC_TEX_ITEM_STICK]                       = "textures/item/stick.png",
    [MC_TEX_ITEM_IRON_INGOT]                  = "textures/item/iron_ingot.png",
    [MC_TEX_ITEM_GOLD_INGOT]                  = "textures/item/gold_ingot.png",
    [MC_TEX_ITEM_DIAMOND]                     = "textures/item/diamond.png",
    [MC_TEX_ITEM_EMERALD]                     = "textures/item/emerald.png",
    [MC_TEX_ITEM_REDSTONE]                    = "textures/item/redstone.png",
    [MC_TEX_ITEM_CROSSBOW_STANDBY]            = "textures/item/crossbow_standby.png",
    [MC_TEX_ITEM_CROSSBOW_ARROW]              = "textures/item/crossbow_arrow.png",
    [MC_TEX_ITEM_IRON_SWORD]                  = "textures/item/iron_sword.png",
    [MC_TEX_ITEM_NETHERITE_SWORD]             = "textures/item/netherite_sword.png",
    [MC_TEX_ITEM_DIAMOND_HELMET]              = "textures/item/diamond_helmet.png",
    [MC_TEX_ITEM_DIAMOND_CHESTPLATE]          = "textures/item/diamond_chestplate.png",
    [MC_TEX_ITEM_DIAMOND_LEGGINGS]            = "textures/item/diamond_leggings.png",
    [MC_TEX_ITEM_DIAMOND_BOOTS]               = "textures/item/diamond_boots.png",
    [MC_TEX_ITEM_BARRIER]                     = "textures/item/barrier.png",

    [MC_TEX_BLOCK_TNT_TOP]                    = "textures/block/tnt_top.png",
    [MC_TEX_BLOCK_TNT_BOTTOM]                 = "textures/block/tnt_bottom.png",
    [MC_TEX_BLOCK_TNT_SIDE]                   = "textures/block/tnt_side.png",
    [MC_TEX_BLOCK_OAK_LOG]                    = "textures/block/oak_log.png",
    [MC_TEX_BLOCK_OAK_LOG_TOP]                = "textures/block/oak_log_top.png",
    [MC_TEX_BLOCK_OAK_PLANKS]                 = "textures/block/oak_planks.png",
    [MC_TEX_BLOCK_GRASS_TOP]                  = "textures/block/grass_block_top.png",
    [MC_TEX_BLOCK_GRASS_SIDE]                 = "textures/block/grass_block_side.png",
    [MC_TEX_BLOCK_GRASS_SIDE_OVERLAY]         = "textures/block/grass_block_side_overlay.png",
    [MC_TEX_BLOCK_DIRT]                       = "textures/block/dirt.png",
    [MC_TEX_BLOCK_STONE]                      = "textures/block/stone.png",
    [MC_TEX_BLOCK_COBBLESTONE]                = "textures/block/cobblestone.png",
    [MC_TEX_BLOCK_OBSIDIAN]                   = "textures/block/obsidian.png",
    [MC_TEX_BLOCK_BEDROCK]                    = "textures/block/bedrock.png",
    [MC_TEX_BLOCK_TORCH]                      = "textures/block/torch.png",
    [MC_TEX_BLOCK_DIAMOND_BLOCK]              = "textures/block/diamond_block.png",
    [MC_TEX_BLOCK_GOLD_BLOCK]                 = "textures/block/gold_block.png",
    [MC_TEX_BLOCK_GLASS]                      = "textures/block/glass.png",
    [MC_TEX_BLOCK_GLOWSTONE]                  = "textures/block/glowstone.png",
    [MC_TEX_BLOCK_CRAFTING_TABLE_TOP]         = "textures/block/crafting_table_top.png",
    [MC_TEX_BLOCK_CRAFTING_TABLE_SIDE]        = "textures/block/crafting_table_side.png",
    [MC_TEX_BLOCK_CRAFTING_TABLE_FRONT]       = "textures/block/crafting_table_front.png",
    [MC_TEX_BLOCK_FURNACE_TOP]                = "textures/block/furnace_top.png",
    [MC_TEX_BLOCK_FURNACE_SIDE]               = "textures/block/furnace_side.png",
    [MC_TEX_BLOCK_FURNACE_FRONT]              = "textures/block/furnace_front.png",
    [MC_TEX_BLOCK_WATER_STILL]                = "textures/block/water_still.png",
    [MC_TEX_BLOCK_LAVA_STILL]                 = "textures/block/lava_still.png",
    [MC_TEX_BLOCK_DESTROY_0]                  = "textures/block/destroy_stage_0.png",
    [MC_TEX_BLOCK_DESTROY_1]                  = "textures/block/destroy_stage_1.png",
    [MC_TEX_BLOCK_DESTROY_2]                  = "textures/block/destroy_stage_2.png",
    [MC_TEX_BLOCK_DESTROY_3]                  = "textures/block/destroy_stage_3.png",
    [MC_TEX_BLOCK_DESTROY_4]                  = "textures/block/destroy_stage_4.png",
    [MC_TEX_BLOCK_DESTROY_5]                  = "textures/block/destroy_stage_5.png",
    [MC_TEX_BLOCK_DESTROY_6]                  = "textures/block/destroy_stage_6.png",
    [MC_TEX_BLOCK_DESTROY_7]                  = "textures/block/destroy_stage_7.png",
    [MC_TEX_BLOCK_DESTROY_8]                  = "textures/block/destroy_stage_8.png",
    [MC_TEX_BLOCK_DESTROY_9]                  = "textures/block/destroy_stage_9.png",

    [MC_TEX_COLORMAP_GRASS]                   = "textures/colormap/grass.png",
    [MC_TEX_ENTITY_SHIELD_NOPATTERN]          = "textures/entity/shield_base_nopattern.png",
    [MC_TEX_ENTITY_SHIELD_BASE]               = "textures/entity/shield_base.png",
    [MC_TEX_ENTITY_ELYTRA]                    = "textures/entity/elytra.png",
    [MC_TEX_ENTITY_STEVE]                     = "textures/entity/steve.png",
    [MC_TEX_ENTITY_CREEPER]                   = "textures/entity/creeper.png",
    [MC_TEX_ENTITY_ZOMBIE]                    = "textures/entity/zombie.png",
    [MC_TEX_ENTITY_SKELETON]                  = "textures/entity/skeleton.png",
    [MC_TEX_ENTITY_ARROW]                     = "textures/entity/arrow.png",
    [MC_TEX_MISC_GLINT_ITEM]                  = "textures/misc/enchanted_glint_item.png",
    [MC_TEX_MISC_GLINT_ARMOR]                 = "textures/misc/enchanted_glint_armor.png",
    [MC_TEX_PARTICLE_FLAME]                   = "textures/particle/flame.png",
    [MC_TEX_PARTICLE_GLINT]                   = "textures/particle/glint.png",
    [MC_TEX_PARTICLE_CRITICAL_HIT]            = "textures/particle/critical_hit.png",
    [MC_TEX_PARTICLE_EXPLOSION_0]             = "textures/particle/explosion_0.png",
    [MC_TEX_PARTICLE_EXPLOSION_1]             = "textures/particle/explosion_1.png",
    [MC_TEX_PARTICLE_EXPLOSION_2]             = "textures/particle/explosion_2.png",
    [MC_TEX_PARTICLE_EXPLOSION_3]             = "textures/particle/explosion_3.png",
    [MC_TEX_PARTICLE_EXPLOSION_4]             = "textures/particle/explosion_4.png",
    [MC_TEX_PARTICLE_EXPLOSION_5]             = "textures/particle/explosion_5.png",
    [MC_TEX_PARTICLE_EXPLOSION_6]             = "textures/particle/explosion_6.png",
    [MC_TEX_PARTICLE_EXPLOSION_7]             = "textures/particle/explosion_7.png",
    [MC_TEX_PARTICLE_EXPLOSION_8]             = "textures/particle/explosion_8.png",
    [MC_TEX_PARTICLE_EXPLOSION_9]             = "textures/particle/explosion_9.png",
    [MC_TEX_PARTICLE_EXPLOSION_10]            = "textures/particle/explosion_10.png",
    [MC_TEX_PARTICLE_EXPLOSION_11]            = "textures/particle/explosion_11.png",
    [MC_TEX_PARTICLE_EXPLOSION_12]            = "textures/particle/explosion_12.png",
    [MC_TEX_PARTICLE_EXPLOSION_13]            = "textures/particle/explosion_13.png",
    [MC_TEX_PARTICLE_EXPLOSION_14]            = "textures/particle/explosion_14.png",
    [MC_TEX_PARTICLE_EXPLOSION_15]            = "textures/particle/explosion_15.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_0]             = "textures/particle/big_smoke_0.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_1]             = "textures/particle/big_smoke_1.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_2]             = "textures/particle/big_smoke_2.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_3]             = "textures/particle/big_smoke_3.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_4]             = "textures/particle/big_smoke_4.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_5]             = "textures/particle/big_smoke_5.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_6]             = "textures/particle/big_smoke_6.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_7]             = "textures/particle/big_smoke_7.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_8]             = "textures/particle/big_smoke_8.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_9]             = "textures/particle/big_smoke_9.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_10]            = "textures/particle/big_smoke_10.png",
    [MC_TEX_PARTICLE_BIG_SMOKE_11]            = "textures/particle/big_smoke_11.png",
    [MC_TEX_PARTICLE_SWEEP_0]                 = "textures/particle/sweep_0.png",
    [MC_TEX_PARTICLE_SWEEP_1]                 = "textures/particle/sweep_1.png",
    [MC_TEX_PARTICLE_SWEEP_2]                 = "textures/particle/sweep_2.png",
    [MC_TEX_PARTICLE_SWEEP_3]                 = "textures/particle/sweep_3.png",
    [MC_TEX_PARTICLE_SWEEP_4]                 = "textures/particle/sweep_4.png",
    [MC_TEX_PARTICLE_SWEEP_5]                 = "textures/particle/sweep_5.png",
    [MC_TEX_PARTICLE_SWEEP_6]                 = "textures/particle/sweep_6.png",
    [MC_TEX_PARTICLE_SWEEP_7]                 = "textures/particle/sweep_7.png",
    [MC_TEX_PARTICLE_GENERIC_0]               = "textures/particle/generic_0.png",
    [MC_TEX_PARTICLE_GENERIC_1]               = "textures/particle/generic_1.png",
    [MC_TEX_PARTICLE_GENERIC_2]               = "textures/particle/generic_2.png",
    [MC_TEX_PARTICLE_GENERIC_3]               = "textures/particle/generic_3.png",
    [MC_TEX_PARTICLE_GENERIC_4]               = "textures/particle/generic_4.png",
    [MC_TEX_PARTICLE_GENERIC_5]               = "textures/particle/generic_5.png",
    [MC_TEX_PARTICLE_GENERIC_6]               = "textures/particle/generic_6.png",
    [MC_TEX_PARTICLE_GENERIC_7]               = "textures/particle/generic_7.png",

    [MC_TEX_ARMOR_DIAMOND_1]                  = "textures/entity/armor_diamond_1.png",
    [MC_TEX_ARMOR_DIAMOND_2]                  = "textures/entity/armor_diamond_2.png",
    [MC_TEX_ARMOR_NETHERITE_1]                = "textures/entity/armor_netherite_1.png",
    [MC_TEX_ARMOR_NETHERITE_2]                = "textures/entity/armor_netherite_2.png",
    [MC_TEX_GUI_SLOT_HELMET]                  = "textures/gui/slot_helmet.png",
    [MC_TEX_GUI_SLOT_CHESTPLATE]              = "textures/gui/slot_chestplate.png",
    [MC_TEX_GUI_SLOT_LEGGINGS]                = "textures/gui/slot_leggings.png",
    [MC_TEX_GUI_SLOT_BOOTS]                   = "textures/gui/slot_boots.png",
    [MC_TEX_BLOCK_GLASS_PANE_TOP]             = "textures/block/glass_pane_top.png",
};

static const char *const k_sound_paths[MC_SND_COUNT] = {
    [MC_SND_DIG_WOOD_1]             = "sounds/dig/wood1.wav",
    [MC_SND_DIG_WOOD_2]             = "sounds/dig/wood2.wav",
    [MC_SND_DIG_WOOD_3]             = "sounds/dig/wood3.wav",
    [MC_SND_DIG_WOOD_4]             = "sounds/dig/wood4.wav",
    [MC_SND_DIG_STONE_1]            = "sounds/dig/stone1.wav",
    [MC_SND_DIG_STONE_2]            = "sounds/dig/stone2.wav",
    [MC_SND_DIG_STONE_3]            = "sounds/dig/stone3.wav",
    [MC_SND_DIG_STONE_4]            = "sounds/dig/stone4.wav",
    [MC_SND_DIG_GRASS_1]            = "sounds/dig/grass1.wav",
    [MC_SND_DIG_GRASS_2]            = "sounds/dig/grass2.wav",
    [MC_SND_DIG_GRASS_3]            = "sounds/dig/grass3.wav",
    [MC_SND_DIG_GRASS_4]            = "sounds/dig/grass4.wav",
    [MC_SND_DIG_GRAVEL_1]           = "sounds/dig/gravel1.wav",
    [MC_SND_DIG_GRAVEL_2]           = "sounds/dig/gravel2.wav",
    [MC_SND_DIG_GRAVEL_3]           = "sounds/dig/gravel3.wav",
    [MC_SND_DIG_GRAVEL_4]           = "sounds/dig/gravel4.wav",
    [MC_SND_DIG_SAND_1]             = "sounds/dig/sand1.wav",
    [MC_SND_DIG_SAND_2]             = "sounds/dig/sand2.wav",
    [MC_SND_DIG_SAND_3]             = "sounds/dig/sand3.wav",
    [MC_SND_DIG_SAND_4]             = "sounds/dig/sand4.wav",

    [MC_SND_STEP_WOOD_1]            = "sounds/step/wood1.wav",
    [MC_SND_STEP_WOOD_2]            = "sounds/step/wood2.wav",
    [MC_SND_STEP_WOOD_3]            = "sounds/step/wood3.wav",
    [MC_SND_STEP_WOOD_4]            = "sounds/step/wood4.wav",
    [MC_SND_STEP_WOOD_5]            = "sounds/step/wood5.wav",
    [MC_SND_STEP_WOOD_6]            = "sounds/step/wood6.wav",
    [MC_SND_STEP_STONE_1]           = "sounds/step/stone1.wav",
    [MC_SND_STEP_STONE_2]           = "sounds/step/stone2.wav",
    [MC_SND_STEP_STONE_3]           = "sounds/step/stone3.wav",
    [MC_SND_STEP_STONE_4]           = "sounds/step/stone4.wav",
    [MC_SND_STEP_STONE_5]           = "sounds/step/stone5.wav",
    [MC_SND_STEP_STONE_6]           = "sounds/step/stone6.wav",
    [MC_SND_STEP_GRASS_1]           = "sounds/step/grass1.wav",
    [MC_SND_STEP_GRASS_2]           = "sounds/step/grass2.wav",
    [MC_SND_STEP_GRASS_3]           = "sounds/step/grass3.wav",
    [MC_SND_STEP_GRASS_4]           = "sounds/step/grass4.wav",
    [MC_SND_STEP_GRASS_5]           = "sounds/step/grass5.wav",
    [MC_SND_STEP_GRASS_6]           = "sounds/step/grass6.wav",
    [MC_SND_STEP_GRAVEL_1]          = "sounds/step/gravel1.wav",
    [MC_SND_STEP_GRAVEL_2]          = "sounds/step/gravel2.wav",
    [MC_SND_STEP_GRAVEL_3]          = "sounds/step/gravel3.wav",
    [MC_SND_STEP_GRAVEL_4]          = "sounds/step/gravel4.wav",
    [MC_SND_STEP_SAND_1]            = "sounds/step/sand1.wav",
    [MC_SND_STEP_SAND_2]            = "sounds/step/sand2.wav",
    [MC_SND_STEP_SAND_3]            = "sounds/step/sand3.wav",
    [MC_SND_STEP_SAND_4]            = "sounds/step/sand4.wav",
    [MC_SND_STEP_SAND_5]            = "sounds/step/sand5.wav",

    [MC_SND_RANDOM_BOW]             = "sounds/random/bow.wav",
    [MC_SND_RANDOM_BOWHIT_1]        = "sounds/random/bowhit1.wav",
    [MC_SND_RANDOM_BOWHIT_2]        = "sounds/random/bowhit2.wav",
    [MC_SND_RANDOM_BOWHIT_3]        = "sounds/random/bowhit3.wav",
    [MC_SND_RANDOM_BOWHIT_4]        = "sounds/random/bowhit4.wav",
    [MC_SND_RANDOM_SUCCESSFUL_HIT]  = "sounds/random/successful_hit.wav",
    [MC_SND_ATTACK_STRONG_1]        = "sounds/entity/player/attack/strong1.wav",
    [MC_SND_ATTACK_STRONG_2]        = "sounds/entity/player/attack/strong2.wav",
    [MC_SND_ATTACK_STRONG_3]        = "sounds/entity/player/attack/strong3.wav",
    [MC_SND_ATTACK_STRONG_4]        = "sounds/entity/player/attack/strong4.wav",
    [MC_SND_ATTACK_STRONG_5]        = "sounds/entity/player/attack/strong5.wav",
    [MC_SND_ATTACK_STRONG_6]        = "sounds/entity/player/attack/strong6.wav",
    [MC_SND_ATTACK_SWEEP_1]         = "sounds/entity/player/attack/sweep1.wav",
    [MC_SND_ATTACK_SWEEP_2]         = "sounds/entity/player/attack/sweep2.wav",
    [MC_SND_ATTACK_SWEEP_3]         = "sounds/entity/player/attack/sweep3.wav",
    [MC_SND_ATTACK_SWEEP_4]         = "sounds/entity/player/attack/sweep4.wav",
    [MC_SND_ATTACK_SWEEP_5]         = "sounds/entity/player/attack/sweep5.wav",
    [MC_SND_ATTACK_SWEEP_6]         = "sounds/entity/player/attack/sweep6.wav",
    [MC_SND_ATTACK_SWEEP_7]         = "sounds/entity/player/attack/sweep7.wav",
    [MC_SND_ATTACK_CRIT_1]          = "sounds/entity/player/attack/crit1.wav",
    [MC_SND_ATTACK_CRIT_2]          = "sounds/entity/player/attack/crit2.wav",
    [MC_SND_ATTACK_CRIT_3]          = "sounds/entity/player/attack/crit3.wav",
    [MC_SND_ATTACK_WEAK_1]          = "sounds/entity/player/attack/weak1.wav",
    [MC_SND_ATTACK_WEAK_2]          = "sounds/entity/player/attack/weak2.wav",
    [MC_SND_ATTACK_WEAK_3]          = "sounds/entity/player/attack/weak3.wav",
    [MC_SND_ATTACK_WEAK_4]          = "sounds/entity/player/attack/weak4.wav",
    [MC_SND_ATTACK_KNOCKBACK_1]     = "sounds/entity/player/attack/knockback1.wav",
    [MC_SND_ATTACK_KNOCKBACK_2]     = "sounds/entity/player/attack/knockback2.wav",
    [MC_SND_ATTACK_KNOCKBACK_3]     = "sounds/entity/player/attack/knockback3.wav",
    [MC_SND_ATTACK_KNOCKBACK_4]     = "sounds/entity/player/attack/knockback4.wav",
    [MC_SND_SHIELD_BLOCK_1]         = "sounds/item/shield/block1.wav",
    [MC_SND_SHIELD_BLOCK_2]         = "sounds/item/shield/block2.wav",
    [MC_SND_SHIELD_BLOCK_3]         = "sounds/item/shield/block3.wav",
    [MC_SND_SHIELD_BLOCK_4]         = "sounds/item/shield/block4.wav",
    [MC_SND_SHIELD_BLOCK_5]         = "sounds/item/shield/block5.wav",
    [MC_SND_RANDOM_BREAK]           = "sounds/random/break.wav",

    [MC_SND_RANDOM_EAT_1]           = "sounds/random/eat1.wav",
    [MC_SND_RANDOM_EAT_2]           = "sounds/random/eat2.wav",
    [MC_SND_RANDOM_EAT_3]           = "sounds/random/eat3.wav",
    [MC_SND_RANDOM_BURP]            = "sounds/random/burp.wav",
    [MC_SND_RANDOM_DRINK]           = "sounds/random/drink.wav",
    [MC_SND_RANDOM_ORB]             = "sounds/random/orb.wav",
    [MC_SND_RANDOM_LEVELUP]         = "sounds/random/levelup.wav",
    [MC_SND_TOTEM_USE]              = "sounds/item/totem/use_totem.wav",
    [MC_SND_RANDOM_CLICK]           = "sounds/random/click.wav",
    [MC_SND_RANDOM_CLICK_STEREO]    = "sounds/random/click_stereo.wav",
    [MC_SND_FIRE_IGNITE]            = "sounds/fire/ignite.wav",

    [MC_SND_RANDOM_FUSE]            = "sounds/random/fuse.wav",
    [MC_SND_RANDOM_EXPLODE_1]       = "sounds/random/explode1.wav",
    [MC_SND_RANDOM_EXPLODE_2]       = "sounds/random/explode2.wav",
    [MC_SND_RANDOM_EXPLODE_3]       = "sounds/random/explode3.wav",
    [MC_SND_RANDOM_EXPLODE_4]       = "sounds/random/explode4.wav",
    [MC_SND_CREEPER_SAY_1]          = "sounds/entity/creeper/say1.wav",
    [MC_SND_CREEPER_SAY_2]          = "sounds/entity/creeper/say2.wav",
    [MC_SND_CREEPER_SAY_3]          = "sounds/entity/creeper/say3.wav",
    [MC_SND_CREEPER_SAY_4]          = "sounds/entity/creeper/say4.wav",
    [MC_SND_CREEPER_DEATH]          = "sounds/entity/creeper/death.wav",
    [MC_SND_FIREWORK_LAUNCH_1]      = "sounds/item/firework/launch1.wav",
    [MC_SND_FIREWORK_BLAST_1]       = "sounds/item/firework/blast1.wav",
    [MC_SND_FIREWORK_LARGE_BLAST_1] = "sounds/item/firework/largeblast1.wav",
    [MC_SND_FIREWORK_TWINKLE_1]     = "sounds/item/firework/twinkle1.wav",
    [MC_SND_ELYTRA_LOOP]            = "sounds/item/elytra/elytra_loop.wav",

    [MC_SND_DAMAGE_HIT_1]           = "sounds/damage/hit1.wav",
    [MC_SND_DAMAGE_HIT_2]           = "sounds/damage/hit2.wav",
    [MC_SND_DAMAGE_HIT_3]           = "sounds/damage/hit3.wav",
    [MC_SND_CLASSIC_HURT]           = "sounds/random/classic_hurt.wav",
    [MC_SND_DAMAGE_FALL_SMALL]      = "sounds/damage/fallsmall.wav",
    [MC_SND_DAMAGE_FALL_BIG]        = "sounds/damage/fallbig.wav",
    [MC_SND_ZOMBIE_SAY_1]           = "sounds/entity/zombie/say1.wav",
    [MC_SND_ZOMBIE_SAY_2]           = "sounds/entity/zombie/say2.wav",
    [MC_SND_ZOMBIE_SAY_3]           = "sounds/entity/zombie/say3.wav",
    [MC_SND_ZOMBIE_HURT_1]          = "sounds/entity/zombie/hurt1.wav",
    [MC_SND_ZOMBIE_HURT_2]          = "sounds/entity/zombie/hurt2.wav",
    [MC_SND_ZOMBIE_DEATH]           = "sounds/entity/zombie/death.wav",
    [MC_SND_SKELETON_SAY_1]         = "sounds/entity/skeleton/say1.wav",
    [MC_SND_SKELETON_SAY_2]         = "sounds/entity/skeleton/say2.wav",
    [MC_SND_SKELETON_SAY_3]         = "sounds/entity/skeleton/say3.wav",
    [MC_SND_SKELETON_HURT_1]        = "sounds/entity/skeleton/hurt1.wav",
    [MC_SND_SKELETON_HURT_2]        = "sounds/entity/skeleton/hurt2.wav",
    [MC_SND_SKELETON_HURT_3]        = "sounds/entity/skeleton/hurt3.wav",
    [MC_SND_SKELETON_HURT_4]        = "sounds/entity/skeleton/hurt4.wav",
    [MC_SND_SKELETON_DEATH]         = "sounds/entity/skeleton/death.wav",
    [MC_SND_RANDOM_GLASS_1]         = "sounds/random/glass1.wav",
    [MC_SND_RANDOM_GLASS_2]         = "sounds/random/glass2.wav",
    [MC_SND_RANDOM_GLASS_3]         = "sounds/random/glass3.wav",
    [MC_SND_RANDOM_POP]             = "sounds/random/pop.wav",
    [MC_SND_RANDOM_SPLASH]          = "sounds/random/splash.wav",
};

typedef struct {
    McSoundId first;
    int count;
} McSoundGroupSpan;

static const McSoundGroupSpan k_sound_groups[MC_SNDGRP_COUNT] = {
    [MC_SNDGRP_DIG_WOOD]             = {MC_SND_DIG_WOOD_1, 4},
    [MC_SNDGRP_DIG_STONE]            = {MC_SND_DIG_STONE_1, 4},
    [MC_SNDGRP_DIG_GRASS]            = {MC_SND_DIG_GRASS_1, 4},
    [MC_SNDGRP_DIG_GRAVEL]           = {MC_SND_DIG_GRAVEL_1, 4},
    [MC_SNDGRP_DIG_SAND]             = {MC_SND_DIG_SAND_1, 4},
    [MC_SNDGRP_STEP_WOOD]            = {MC_SND_STEP_WOOD_1, 6},
    [MC_SNDGRP_STEP_STONE]           = {MC_SND_STEP_STONE_1, 6},
    [MC_SNDGRP_STEP_GRASS]           = {MC_SND_STEP_GRASS_1, 6},
    [MC_SNDGRP_STEP_GRAVEL]          = {MC_SND_STEP_GRAVEL_1, 4},
    [MC_SNDGRP_STEP_SAND]            = {MC_SND_STEP_SAND_1, 5},
    [MC_SNDGRP_BOW_SHOOT]            = {MC_SND_RANDOM_BOW, 1},
    [MC_SNDGRP_BOW_HIT]              = {MC_SND_RANDOM_BOWHIT_1, 4},
    [MC_SNDGRP_SUCCESSFUL_HIT]       = {MC_SND_RANDOM_SUCCESSFUL_HIT, 1},
    [MC_SNDGRP_ATTACK_STRONG]        = {MC_SND_ATTACK_STRONG_1, 6},
    [MC_SNDGRP_ATTACK_SWEEP]         = {MC_SND_ATTACK_SWEEP_1, 7},
    [MC_SNDGRP_ATTACK_CRIT]          = {MC_SND_ATTACK_CRIT_1, 3},
    [MC_SNDGRP_ATTACK_WEAK]          = {MC_SND_ATTACK_WEAK_1, 4},
    [MC_SNDGRP_ATTACK_KNOCKBACK]     = {MC_SND_ATTACK_KNOCKBACK_1, 4},
    [MC_SNDGRP_SHIELD_BLOCK]         = {MC_SND_SHIELD_BLOCK_1, 5},
    [MC_SNDGRP_SHIELD_BREAK]         = {MC_SND_RANDOM_BREAK, 1},
    [MC_SNDGRP_EAT]                  = {MC_SND_RANDOM_EAT_1, 3},
    [MC_SNDGRP_BURP]                 = {MC_SND_RANDOM_BURP, 1},
    [MC_SNDGRP_DRINK]                = {MC_SND_RANDOM_DRINK, 1},
    [MC_SNDGRP_ORB]                  = {MC_SND_RANDOM_ORB, 1},
    [MC_SNDGRP_LEVELUP]              = {MC_SND_RANDOM_LEVELUP, 1},
    [MC_SNDGRP_TOTEM_USE]            = {MC_SND_TOTEM_USE, 1},
    [MC_SNDGRP_CLICK]                = {MC_SND_RANDOM_CLICK, 1},
    [MC_SNDGRP_CLICK_STEREO]         = {MC_SND_RANDOM_CLICK_STEREO, 1},
    [MC_SNDGRP_IGNITE]               = {MC_SND_FIRE_IGNITE, 1},
    [MC_SNDGRP_FUSE]                 = {MC_SND_RANDOM_FUSE, 1},
    [MC_SNDGRP_EXPLODE]              = {MC_SND_RANDOM_EXPLODE_1, 4},
    [MC_SNDGRP_CREEPER_HURT]         = {MC_SND_CREEPER_SAY_1, 4},
    [MC_SNDGRP_CREEPER_DEATH]        = {MC_SND_CREEPER_DEATH, 1},
    [MC_SNDGRP_FIREWORK_LAUNCH]      = {MC_SND_FIREWORK_LAUNCH_1, 1},
    [MC_SNDGRP_FIREWORK_BLAST]       = {MC_SND_FIREWORK_BLAST_1, 1},
    [MC_SNDGRP_FIREWORK_LARGE_BLAST] = {MC_SND_FIREWORK_LARGE_BLAST_1, 1},
    [MC_SNDGRP_FIREWORK_TWINKLE]     = {MC_SND_FIREWORK_TWINKLE_1, 1},
    [MC_SNDGRP_ELYTRA_LOOP]          = {MC_SND_ELYTRA_LOOP, 1},
    [MC_SNDGRP_PLAYER_HURT]          = {MC_SND_DAMAGE_HIT_1, 3},
    [MC_SNDGRP_CLASSIC_HURT]         = {MC_SND_CLASSIC_HURT, 1},
    [MC_SNDGRP_FALL_SMALL]           = {MC_SND_DAMAGE_FALL_SMALL, 1},
    [MC_SNDGRP_FALL_BIG]             = {MC_SND_DAMAGE_FALL_BIG, 1},
    [MC_SNDGRP_ZOMBIE_SAY]           = {MC_SND_ZOMBIE_SAY_1, 3},
    [MC_SNDGRP_ZOMBIE_HURT]          = {MC_SND_ZOMBIE_HURT_1, 2},
    [MC_SNDGRP_ZOMBIE_DEATH]         = {MC_SND_ZOMBIE_DEATH, 1},
    [MC_SNDGRP_SKELETON_SAY]         = {MC_SND_SKELETON_SAY_1, 3},
    [MC_SNDGRP_SKELETON_HURT]        = {MC_SND_SKELETON_HURT_1, 4},
    [MC_SNDGRP_SKELETON_DEATH]       = {MC_SND_SKELETON_DEATH, 1},
    [MC_SNDGRP_GLASS_BREAK]          = {MC_SND_RANDOM_GLASS_1, 3},
    [MC_SNDGRP_ITEM_POP]             = {MC_SND_RANDOM_POP, 1},
    [MC_SNDGRP_SPLASH]               = {MC_SND_RANDOM_SPLASH, 1},
};

typedef struct {
    char *key;
    char *val;
} McLangEntry;

static pthread_mutex_t g_init_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_ready = 0;

static McTexture g_textures[MC_TEX_COUNT];
static McSound g_sounds[MC_SND_COUNT];
static uint8_t g_font_widths[256];
static int g_font_widths_loaded = 0;
static McLangEntry g_en_us[MC_MAX_EN_US];
static int g_en_us_count = 0;
static char g_root_dir[1024];

static int dir_has_core_assets(const char *dir) {
    if (!dir || !dir[0]) return 0;
    char p1[1200], p2[1200];
    if (snprintf(p1, sizeof p1, "%s/textures/font/ascii.png", dir) >= (int)sizeof p1) return 0;
    if (snprintf(p2, sizeof p2, "%s/sounds/random/explode1.wav", dir) >= (int)sizeof p2) return 0;
    return access(p1, R_OK) == 0 && access(p2, R_OK) == 0;
}

static int try_candidate_root(const char *candidate, char *out_root, size_t cap) {
    if (!candidate || !candidate[0]) return 0;
    if (!dir_has_core_assets(candidate)) return 0;
    char resolved[PATH_MAX];
    if (realpath(candidate, resolved) && dir_has_core_assets(resolved)) {
        if (snprintf(out_root, cap, "%s", resolved) < (int)cap) return 1;
    }
    if (snprintf(out_root, cap, "%s", candidate) < (int)cap) return 1;
    return 0;
}

static int resolve_assets_root(const char *override_dir, char *out_root, size_t cap) {
    if (try_candidate_root(override_dir, out_root, cap)) return 1;

    const char *env_dir = getenv("TOTK_MC_ASSETS");
    if (try_candidate_root(env_dir, out_root, cap)) return 1;

    char exe_buf[PATH_MAX];
    uint32_t exe_sz = (uint32_t)sizeof exe_buf;
    if (_NSGetExecutablePath(exe_buf, &exe_sz) == 0) {
        char exe_real[PATH_MAX];
        const char *exe = realpath(exe_buf, exe_real) ? exe_real : exe_buf;
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", exe);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            char cand[PATH_MAX];
            if (snprintf(cand, sizeof cand, "%s/../assets/mc", dir) < (int)sizeof cand &&
                try_candidate_root(cand, out_root, cap)) {
                return 1;
            }
            if (snprintf(cand, sizeof cand, "%s/assets/mc", dir) < (int)sizeof cand &&
                try_candidate_root(cand, out_root, cap)) {
                return 1;
            }
            if (snprintf(cand, sizeof cand, "%s/../Resources/totk/assets/mc", dir) < (int)sizeof cand &&
                try_candidate_root(cand, out_root, cap)) {
                return 1;
            }
        }
    }

    CFBundleRef bundle = CFBundleGetMainBundle();
    if (bundle) {
        CFURLRef res_url = CFBundleCopyResourcesDirectoryURL(bundle);
        if (res_url) {
            char res_path[PATH_MAX];
            if (CFURLGetFileSystemRepresentation(res_url, true, (UInt8 *)res_path, (CFIndex)sizeof res_path)) {
                char cand[PATH_MAX];
                if (snprintf(cand, sizeof cand, "%s/totk/assets/mc", res_path) < (int)sizeof cand &&
                    try_candidate_root(cand, out_root, cap)) {
                    CFRelease(res_url);
                    return 1;
                }
                if (snprintf(cand, sizeof cand, "%s/assets/mc", res_path) < (int)sizeof cand &&
                    try_candidate_root(cand, out_root, cap)) {
                    CFRelease(res_url);
                    return 1;
                }
            }
            CFRelease(res_url);
        }
    }

    if (try_candidate_root("assets/mc", out_root, cap)) return 1;
    if (try_candidate_root("/Users/pdt/Games/TOTK_build/totk/assets/mc", out_root, cap)) return 1;
    return 0;
}

static int load_png_rgba8(const char *path, McTexture *out) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    if (!url) return 0;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, NULL);
    CFRelease(url);
    if (!src) return 0;

    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    if (!img) return 0;

    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);
    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        CGImageRelease(img);
        return 0;
    }

    size_t nbytes = w * h * 4u;
    uint8_t *rgba = (uint8_t *)calloc(1u, nbytes);
    if (!rgba) {
        CGImageRelease(img);
        return 0;
    }

    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!cs) cs = CGColorSpaceCreateDeviceRGB();
    if (!cs) {
        free(rgba);
        CGImageRelease(img);
        return 0;
    }

    CGBitmapInfo bi = (CGBitmapInfo)kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big;
    CGContextRef ctx = CGBitmapContextCreate(rgba, w, h, 8, w * 4u, cs, bi);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        free(rgba);
        CGImageRelease(img);
        return 0;
    }

    CGContextSetBlendMode(ctx, kCGBlendModeCopy);
    CGContextDrawImage(ctx, CGRectMake(0.0, 0.0, (CGFloat)w, (CGFloat)h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    /* Unpremultiply to preserve exact straight RGBA8 values for shaders and CPU overlays. */
    size_t npix = w * h;
    for (size_t i = 0; i < npix; i++) {
        uint8_t *p = rgba + i * 4u;
        uint8_t a = p[3];
        if (a > 0 && a < 255) {
            uint32_t half_a = (uint32_t)a / 2u;
            uint32_t r = ((uint32_t)p[0] * 255u + half_a) / (uint32_t)a;
            uint32_t g = ((uint32_t)p[1] * 255u + half_a) / (uint32_t)a;
            uint32_t b = ((uint32_t)p[2] * 255u + half_a) / (uint32_t)a;
            p[0] = (uint8_t)(r > 255u ? 255u : r);
            p[1] = (uint8_t)(g > 255u ? 255u : g);
            p[2] = (uint8_t)(b > 255u ? 255u : b);
        }
    }

    out->rgba = rgba;
    out->width = (int)w;
    out->height = (int)h;
    out->loaded = 1;
    return 1;
}

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int load_wav_pcm16(const char *path, McSound *out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    uint8_t hdr[12];
    if (fread(hdr, 1, sizeof hdr, fp) != sizeof hdr ||
        memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(fp);
        return 0;
    }

    int have_fmt = 0;
    uint16_t audio_fmt = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    int16_t *pcm = NULL;
    uint32_t data_bytes = 0;

    uint8_t chdr[8];
    while (fread(chdr, 1, sizeof chdr, fp) == sizeof chdr) {
        uint32_t csize = read_le32(chdr + 4);
        if (memcmp(chdr, "fmt ", 4) == 0) {
            if (csize < 16 || csize > 4096) break;
            uint8_t fbuf[64];
            uint32_t to_read = csize < (uint32_t)sizeof fbuf ? csize : (uint32_t)sizeof fbuf;
            if (fread(fbuf, 1, to_read, fp) != to_read) break;
            if (csize > to_read && fseek(fp, (long)(csize - to_read), SEEK_CUR) != 0) break;
            audio_fmt = read_le16(fbuf);
            channels = read_le16(fbuf + 2);
            sample_rate = read_le32(fbuf + 4);
            bits = read_le16(fbuf + 14);
            have_fmt = 1;
        } else if (memcmp(chdr, "data", 4) == 0) {
            if (csize == 0 || csize > 64u * 1024u * 1024u) break;
            pcm = (int16_t *)malloc(csize);
            if (!pcm) break;
            if (fread(pcm, 1, csize, fp) != csize) {
                free(pcm);
                pcm = NULL;
                break;
            }
            data_bytes = csize;
            if (have_fmt) break;
        } else {
            if (fseek(fp, (long)csize, SEEK_CUR) != 0) break;
        }
        if (csize & 1u) {
            if (fseek(fp, 1L, SEEK_CUR) != 0) break;
        }
    }
    fclose(fp);

    if (!have_fmt || !pcm || audio_fmt != 1 || (channels != 1 && channels != 2) ||
        bits != 16 || sample_rate == 0) {
        free(pcm);
        return 0;
    }

    uint32_t frame_bytes = (uint32_t)channels * 2u;
    uint32_t frames = data_bytes / frame_bytes;
    if (frames == 0) {
        free(pcm);
        return 0;
    }

    out->pcm = pcm;
    out->frames = frames;
    out->sample_rate = sample_rate;
    out->channels = channels;
    out->loaded = 1;
    return 1;
}

static void load_font_widths(const char *root_dir) {
    memset(g_font_widths, 4, sizeof g_font_widths);
    char path[1200];
    if (snprintf(path, sizeof path, "%s/textures/font/ascii_widths.bin", root_dir) < (int)sizeof path) {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            size_t got = fread(g_font_widths, 1, 256, fp);
            fclose(fp);
            if (got == 256) {
                g_font_widths_loaded = 1;
                return;
            }
        }
    }

    /* Fallback: derive directly from loaded ascii.png RGBA texture if binary table is absent. */
    const McTexture *font = &g_textures[MC_TEX_FONT_ASCII];
    if (font->loaded && font->rgba && font->width >= 16 && font->height >= 16) {
        int cw = font->width / 16;
        int ch = font->height / 16;
        int scale_x = cw >= 8 ? cw / 8 : 1;
        for (int code = 0; code < 256; code++) {
            if (code == 32) {
                g_font_widths[code] = 4;
                continue;
            }
            int col = code % 16;
            int row = code / 16;
            int max_x = -1;
            for (int py = 0; py < ch; py++) {
                for (int px = 0; px < cw; px++) {
                    size_t idx = (size_t)(row * ch + py) * (size_t)font->width + (size_t)(col * cw + px);
                    if (font->rgba[idx * 4u + 3u] > 0 && px > max_x) {
                        max_x = px;
                    }
                }
            }
            g_font_widths[code] = (uint8_t)(max_x >= 0 ? (max_x / scale_x) + 2 : 4);
        }
        g_font_widths_loaded = 1;
    }
}

static char *parse_json_string(const char **pp) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return NULL;
    p++;

    size_t cap = 64, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;

    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\' && *p) {
            char esc = *p++;
            switch (esc) {
                case '"':  c = '"'; break;
                case '\\': c = '\\'; break;
                case '/':  c = '/'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                default:   c = (unsigned char)esc; break;
            }
        }
        if (len + 2 >= cap) {
            cap *= 2u;
            char *nb = (char *)realloc(out, cap);
            if (!nb) {
                free(out);
                return NULL;
            }
            out = nb;
        }
        out[len++] = (char)c;
    }
    if (*p != '"') {
        free(out);
        return NULL;
    }
    out[len] = '\0';
    *pp = p + 1;
    return out;
}

static void load_en_us_json(const char *root_dir) {
    char path[1200];
    if (snprintf(path, sizeof path, "%s/en_us.json", root_dir) >= (int)sizeof path) return;
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return; }
    long sz = ftell(fp);
    if (sz <= 0 || sz > 1024L * 1024L) { fclose(fp); return; }
    rewind(fp);
    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) { fclose(fp); return; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';

    const char *p = buf;
    while (*p && *p != '{') p++;
    if (*p == '{') p++;

    while (*p && g_en_us_count < MC_MAX_EN_US) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
        if (!*p || *p == '}') break;
        char *key = parse_json_string(&p);
        if (!key) break;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p != ':') { free(key); break; }
        p++;
        char *val = parse_json_string(&p);
        if (!val) { free(key); break; }
        g_en_us[g_en_us_count].key = key;
        g_en_us[g_en_us_count].val = val;
        g_en_us_count++;
    }
    free(buf);
}

static void free_all_locked(void) {
    for (int i = 0; i < MC_TEX_COUNT; i++) {
        free(g_textures[i].rgba);
        memset(&g_textures[i], 0, sizeof g_textures[i]);
    }
    for (int i = 0; i < MC_SND_COUNT; i++) {
        free(g_sounds[i].pcm);
        memset(&g_sounds[i], 0, sizeof g_sounds[i]);
    }
    for (int i = 0; i < g_en_us_count; i++) {
        free(g_en_us[i].key);
        free(g_en_us[i].val);
        g_en_us[i].key = NULL;
        g_en_us[i].val = NULL;
    }
    g_en_us_count = 0;
    g_font_widths_loaded = 0;
    memset(g_font_widths, 4, sizeof g_font_widths);
    g_root_dir[0] = '\0';
    atomic_store_explicit(&g_ready, 0, memory_order_release);
}

int mc_assets_init(const char *override_dir) {
    if (!override_dir && atomic_load_explicit(&g_ready, memory_order_acquire)) {
        return 1;
    }

    pthread_mutex_lock(&g_init_mu);
    if (atomic_load_explicit(&g_ready, memory_order_relaxed)) {
        if (!override_dir || strcmp(override_dir, g_root_dir) == 0) {
            pthread_mutex_unlock(&g_init_mu);
            return 1;
        }
        free_all_locked();
    }

    char root[1024];
    if (!resolve_assets_root(override_dir, root, sizeof root)) {
        pthread_mutex_unlock(&g_init_mu);
        fprintf(stderr, "[totk-mc-assets] Failed to locate Minecraft assets directory (assets/mc)\n");
        return 0;
    }
    snprintf(g_root_dir, sizeof g_root_dir, "%s", root);

    int tex_ok = 0;
    for (int i = 0; i < MC_TEX_COUNT; i++) {
        const char *rel = k_tex_paths[i];
        g_textures[i].rel_path = rel;
        if (!rel) continue;
        char full[1400];
        if (snprintf(full, sizeof full, "%s/%s", g_root_dir, rel) < (int)sizeof full) {
            if (load_png_rgba8(full, &g_textures[i])) tex_ok++;
        }
    }

    int snd_ok = 0;
    for (int i = 0; i < MC_SND_COUNT; i++) {
        const char *rel = k_sound_paths[i];
        g_sounds[i].rel_path = rel;
        if (!rel) continue;
        char full[1400];
        if (snprintf(full, sizeof full, "%s/%s", g_root_dir, rel) < (int)sizeof full) {
            if (load_wav_pcm16(full, &g_sounds[i])) snd_ok++;
        }
    }

    load_font_widths(g_root_dir);
    load_en_us_json(g_root_dir);

    int ok = (tex_ok > 0 && g_textures[MC_TEX_FONT_ASCII].loaded && snd_ok > 0);
    if (ok) {
        atomic_store_explicit(&g_ready, 1, memory_order_release);
        fprintf(stderr,
                "[totk-mc-assets] Loaded %d/%d PNGs, %d/%d WAVs, %d en_us strings from %s\n",
                tex_ok, MC_TEX_COUNT, snd_ok, MC_SND_COUNT, g_en_us_count, g_root_dir);
    } else {
        free_all_locked();
    }
    pthread_mutex_unlock(&g_init_mu);
    return ok;
}

int mc_assets_ready(void) {
    return atomic_load_explicit(&g_ready, memory_order_acquire);
}

void mc_assets_shutdown(void) {
    pthread_mutex_lock(&g_init_mu);
    free_all_locked();
    pthread_mutex_unlock(&g_init_mu);
}

const char *mc_assets_root_dir(void) {
    return g_root_dir;
}

void mc_assets_get_stats(McAssetStats *out_stats) {
    if (!out_stats) return;
    memset(out_stats, 0, sizeof *out_stats);
    out_stats->textures_total = MC_TEX_COUNT;
    out_stats->sounds_total = MC_SND_COUNT;
    for (int i = 0; i < MC_TEX_COUNT; i++) {
        if (g_textures[i].loaded) out_stats->textures_loaded++;
    }
    for (int i = 0; i < MC_SND_COUNT; i++) {
        if (g_sounds[i].loaded) out_stats->sounds_loaded++;
    }
    out_stats->en_us_loaded = g_en_us_count;
    out_stats->font_widths_loaded = g_font_widths_loaded;
    snprintf(out_stats->root_dir, sizeof out_stats->root_dir, "%s", g_root_dir);
}

const McTexture *mc_asset_tex(McTextureId id) {
    if (!atomic_load_explicit(&g_ready, memory_order_acquire)) return NULL;
    if ((unsigned)id >= (unsigned)MC_TEX_COUNT) return NULL;
    return g_textures[id].loaded ? &g_textures[id] : NULL;
}

const McTexture *mc_asset_tex_by_path(const char *rel_path) {
    if (!rel_path || !atomic_load_explicit(&g_ready, memory_order_acquire)) return NULL;
    for (int i = 0; i < MC_TEX_COUNT; i++) {
        const char *p = k_tex_paths[i];
        if (!p) continue;
        if (strcmp(p, rel_path) == 0 ||
            (strncmp(p, "textures/", 9) == 0 && strcmp(p + 9, rel_path) == 0)) {
            return g_textures[i].loaded ? &g_textures[i] : NULL;
        }
    }
    return NULL;
}

const McSound *mc_asset_sound(McSoundId id) {
    if (!atomic_load_explicit(&g_ready, memory_order_acquire)) return NULL;
    if ((unsigned)id >= (unsigned)MC_SND_COUNT) return NULL;
    return g_sounds[id].loaded ? &g_sounds[id] : NULL;
}

int mc_asset_sound_group_count(McSoundGroupId group) {
    if ((unsigned)group >= (unsigned)MC_SNDGRP_COUNT) return 0;
    return k_sound_groups[group].count;
}

const McSound *mc_asset_sound_group_get(McSoundGroupId group, int index) {
    if (!atomic_load_explicit(&g_ready, memory_order_acquire)) return NULL;
    if ((unsigned)group >= (unsigned)MC_SNDGRP_COUNT) return NULL;
    McSoundGroupSpan span = k_sound_groups[group];
    if (span.count <= 0 || (unsigned)index >= (unsigned)span.count) return NULL;
    McSoundId id = (McSoundId)((int)span.first + index);
    return mc_asset_sound(id);
}

const McSound *mc_asset_sound_group_pick(McSoundGroupId group, uint32_t seed) {
    if ((unsigned)group >= (unsigned)MC_SNDGRP_COUNT) return NULL;
    McSoundGroupSpan span = k_sound_groups[group];
    if (span.count <= 0) return NULL;
    int idx = (int)(seed % (uint32_t)span.count);
    return mc_asset_sound_group_get(group, idx);
}

const uint8_t *mc_asset_font_widths(void) {
    return g_font_widths;
}

int mc_asset_measure_text(const char *text) {
    if (!text) return 0;
    int width = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned char c = *p++;
        /* Skip Minecraft section formatting codes (§x) if present. */
        if (c == 0xC2u && *p == 0xA7u && p[1]) {
            p += 2;
            continue;
        }
        /* Basic UTF-8 skip to single byte fallback for non-ASCII */
        if (c >= 0x80u) {
            while ((*p & 0xC0u) == 0x80u) p++;
            width += 6;
            continue;
        }
        width += (int)g_font_widths[c];
    }
    return width;
}

const char *mc_asset_en_us(const char *lang_key) {
    if (!lang_key || !atomic_load_explicit(&g_ready, memory_order_acquire)) return NULL;
    for (int i = 0; i < g_en_us_count; i++) {
        if (strcmp(g_en_us[i].key, lang_key) == 0) {
            return g_en_us[i].val;
        }
    }
    return NULL;
}
