#include "mc_assets.h"
#include "platform/platform.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    [MC_TEX_RED_WOOL]                         = "textures/block/red_wool.png",
    [MC_TEX_RED_BED]                          = "textures/entity/bed/red.png",
    [MC_TEX_WOLF]                             = "textures/entity/wolf/wolf_tame.png",
    [MC_TEX_SAND]                             = "textures/block/sand.png",
    [MC_TEX_BEEF]                             = "textures/item/beef.png",
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

static PlatMutex g_init_mu = PLAT_MUTEX_INIT;
static _Atomic int g_ready = 0;

static McTexture g_textures[MC_TEX_COUNT];
static McSound g_sounds[MC_SND_COUNT];
static uint8_t g_font_widths[256];
static int g_font_widths_loaded = 0;
static McLangEntry g_en_us[MC_MAX_EN_US];
static int g_en_us_count = 0;
static char g_root_dir[1024];

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* =========================================================================
 * Self-contained RFC 1950/1951 DEFLATE + PNG Decoder (straight RGBA8888)
 * ========================================================================= */

typedef struct {
    const uint8_t *src;
    size_t len;
    size_t pos;
    uint32_t bitbuf;
    int bitcnt;
} BitStream;

static int bs_ensure(BitStream *bs, int n) {
    while (bs->bitcnt < n) {
        if (bs->pos >= bs->len) return 0;
        bs->bitbuf |= (uint32_t)bs->src[bs->pos++] << bs->bitcnt;
        bs->bitcnt += 8;
    }
    return 1;
}

static int bs_read(BitStream *bs, int n, uint32_t *out) {
    if (n == 0) { *out = 0; return 1; }
    if (!bs_ensure(bs, n)) return 0;
    *out = bs->bitbuf & ((1u << n) - 1u);
    bs->bitbuf >>= n;
    bs->bitcnt -= n;
    return 1;
}

static void bs_align(BitStream *bs) {
    int drop = bs->bitcnt & 7;
    bs->bitbuf >>= drop;
    bs->bitcnt -= drop;
}

#define HUFF_MAX_BITS 15
#define HUFF_MAX_SYMS 320

typedef struct {
    uint16_t counts[HUFF_MAX_BITS + 1];
    uint16_t symbols[HUFF_MAX_SYMS];
} HuffTable;

static int huff_build(HuffTable *t, const uint8_t *lengths, int nsyms) {
    if (nsyms > HUFF_MAX_SYMS) return 0;
    memset(t->counts, 0, sizeof t->counts);
    for (int i = 0; i < nsyms; i++) {
        if (lengths[i] > HUFF_MAX_BITS) return 0;
        t->counts[lengths[i]]++;
    }
    if (t->counts[0] == nsyms) return 1;

    int left = 1;
    for (int b = 1; b <= HUFF_MAX_BITS; b++) {
        left <<= 1;
        left -= (int)t->counts[b];
        if (left < 0) return 0;
    }

    uint16_t offs[HUFF_MAX_BITS + 1];
    offs[1] = 0;
    for (int b = 1; b < HUFF_MAX_BITS; b++) {
        offs[b + 1] = (uint16_t)(offs[b] + t->counts[b]);
    }
    for (int i = 0; i < nsyms; i++) {
        if (lengths[i] != 0) {
            t->symbols[offs[lengths[i]]++] = (uint16_t)i;
        }
    }
    return 1;
}

static int huff_decode(BitStream *bs, const HuffTable *t, int *out_sym) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= HUFF_MAX_BITS; len++) {
        uint32_t bit = 0;
        if (!bs_read(bs, 1, &bit)) return 0;
        code |= (int)bit;
        int count = (int)t->counts[len];
        if (code - count < first) {
            *out_sym = (int)t->symbols[index + (code - first)];
            return 1;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return 0;
}

static int zlib_inflate(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len) {
    if (src_len < 6) return 0;
    uint8_t cmf = src[0], flg = src[1];
    if ((cmf & 0x0Fu) != 8u || (cmf >> 4) > 7u) return 0;
    if ((((uint16_t)cmf << 8) | flg) % 31u != 0u) return 0;
    if (flg & 0x20u) return 0; /* preset dict not used in PNG */

    BitStream bs = {src + 2, src_len - 2, 0, 0, 0};
    size_t out_pos = 0;

    static const uint16_t len_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const uint8_t len_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static const uint16_t dist_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    static const uint8_t dist_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static const uint8_t cl_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    uint32_t bfinal = 0;
    do {
        uint32_t btype = 0;
        if (!bs_read(&bs, 1, &bfinal) || !bs_read(&bs, 2, &btype)) return 0;

        if (btype == 0) {
            bs_align(&bs);
            if (bs.pos + 4 > bs.len) return 0;
            uint16_t blen = read_le16(bs.src + bs.pos);
            uint16_t nlen = read_le16(bs.src + bs.pos + 2);
            bs.pos += 4;
            if ((uint16_t)(blen ^ 0xFFFFu) != nlen) return 0;
            if (bs.pos + blen > bs.len || out_pos + blen > dst_len) return 0;
            memcpy(dst + out_pos, bs.src + bs.pos, blen);
            bs.pos += blen;
            out_pos += blen;
        } else if (btype == 1 || btype == 2) {
            HuffTable lit_tab, dist_tab;
            if (btype == 1) {
                uint8_t ll[288], dl[32];
                for (int i = 0; i <= 143; i++) ll[i] = 8;
                for (int i = 144; i <= 255; i++) ll[i] = 9;
                for (int i = 256; i <= 279; i++) ll[i] = 7;
                for (int i = 280; i <= 287; i++) ll[i] = 8;
                memset(dl, 5, sizeof dl);
                if (!huff_build(&lit_tab, ll, 288) || !huff_build(&dist_tab, dl, 32)) return 0;
            } else {
                uint32_t hlit = 0, hdist = 0, hclen = 0;
                if (!bs_read(&bs, 5, &hlit) || !bs_read(&bs, 5, &hdist) || !bs_read(&bs, 4, &hclen)) return 0;
                hlit += 257;
                hdist += 1;
                hclen += 4;
                if (hlit > 286 || hdist > 30) return 0;

                uint8_t cl_lens[19];
                memset(cl_lens, 0, sizeof cl_lens);
                for (uint32_t i = 0; i < hclen; i++) {
                    uint32_t v = 0;
                    if (!bs_read(&bs, 3, &v)) return 0;
                    cl_lens[cl_order[i]] = (uint8_t)v;
                }
                HuffTable cl_tab;
                if (!huff_build(&cl_tab, cl_lens, 19)) return 0;

                uint8_t lens[286 + 30];
                uint32_t total = hlit + hdist;
                uint32_t idx = 0;
                while (idx < total) {
                    int sym = 0;
                    if (!huff_decode(&bs, &cl_tab, &sym)) return 0;
                    if (sym < 16) {
                        lens[idx++] = (uint8_t)sym;
                    } else {
                        uint32_t rep = 0, extra = 0;
                        uint8_t val = 0;
                        if (sym == 16) {
                            if (idx == 0 || !bs_read(&bs, 2, &extra)) return 0;
                            val = lens[idx - 1];
                            rep = 3u + extra;
                        } else if (sym == 17) {
                            if (!bs_read(&bs, 3, &extra)) return 0;
                            rep = 3u + extra;
                        } else if (sym == 18) {
                            if (!bs_read(&bs, 7, &extra)) return 0;
                            rep = 11u + extra;
                        } else {
                            return 0;
                        }
                        if (idx + rep > total) return 0;
                        while (rep--) lens[idx++] = val;
                    }
                }
                if (lens[256] == 0) return 0;
                if (!huff_build(&lit_tab, lens, (int)hlit) ||
                    !huff_build(&dist_tab, lens + hlit, (int)hdist)) return 0;
            }

            for (;;) {
                int sym = 0;
                if (!huff_decode(&bs, &lit_tab, &sym)) return 0;
                if (sym < 256) {
                    if (out_pos >= dst_len) return 0;
                    dst[out_pos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else {
                    int lidx = sym - 257;
                    if (lidx < 0 || lidx >= 29) return 0;
                    uint32_t lextra = 0;
                    if (!bs_read(&bs, len_extra[lidx], &lextra)) return 0;
                    size_t match_len = (size_t)len_base[lidx] + (size_t)lextra;

                    int dsym = 0;
                    if (!huff_decode(&bs, &dist_tab, &dsym)) return 0;
                    if (dsym < 0 || dsym >= 30) return 0;
                    uint32_t dextra = 0;
                    if (!bs_read(&bs, dist_extra[dsym], &dextra)) return 0;
                    size_t dist = (size_t)dist_base[dsym] + (size_t)dextra;
                    if (dist == 0 || dist > out_pos || out_pos + match_len > dst_len) return 0;

                    for (size_t k = 0; k < match_len; k++) {
                        dst[out_pos] = dst[out_pos - dist];
                        out_pos++;
                    }
                }
            }
        } else {
            return 0;
        }
    } while (!bfinal);

    return out_pos == dst_len;
}

static uint8_t paeth_predictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

static int unfilter_rows(uint8_t *raw, uint32_t w, uint32_t h, uint32_t bpp, uint32_t stride) {
    (void)w;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1u + (size_t)stride);
        uint8_t f = row[0];
        uint8_t *cur = row + 1;
        const uint8_t *prev = (y > 0) ? (cur - (1u + (size_t)stride)) : NULL;
        switch (f) {
            case 0:
                break;
            case 1:
                for (uint32_t i = bpp; i < stride; i++) {
                    cur[i] = (uint8_t)(cur[i] + cur[i - bpp]);
                }
                break;
            case 2:
                if (prev) {
                    for (uint32_t i = 0; i < stride; i++) {
                        cur[i] = (uint8_t)(cur[i] + prev[i]);
                    }
                }
                break;
            case 3:
                for (uint32_t i = 0; i < stride; i++) {
                    uint32_t a = (i >= bpp) ? cur[i - bpp] : 0u;
                    uint32_t b = prev ? prev[i] : 0u;
                    cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1));
                }
                break;
            case 4:
                for (uint32_t i = 0; i < stride; i++) {
                    int a = (i >= bpp) ? cur[i - bpp] : 0;
                    int b = prev ? prev[i] : 0;
                    int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                    cur[i] = (uint8_t)(cur[i] + paeth_predictor(a, b, c));
                }
                break;
            default:
                return 0;
        }
    }
    return 1;
}

static uint32_t png_sample_bits(const uint8_t *row, uint32_t bit_offset, uint8_t depth) {
    if (depth == 8) return row[bit_offset >> 3];
    if (depth == 16) return row[bit_offset >> 3]; /* MSB gives exact 8-bit quantized value */
    uint32_t byte = row[bit_offset >> 3];
    int shift = 8 - (int)depth - (int)(bit_offset & 7u);
    uint32_t mask = (1u << depth) - 1u;
    uint32_t v = (byte >> shift) & mask;
    if (depth == 1) return v ? 255u : 0u;
    if (depth == 2) return v * 85u;
    if (depth == 4) return v * 17u;
    return v;
}

static void png_decode_pixel(const uint8_t *row, uint32_t px, uint8_t color_type, uint8_t depth,
                             const uint8_t *plte, uint32_t plte_entries,
                             const uint8_t *trns, uint32_t trns_len, uint8_t out_rgba[4]) {
    uint32_t spp = 1u;
    switch (color_type) {
        case 0: spp = 1u; break;
        case 2: spp = 3u; break;
        case 3: spp = 1u; break;
        case 4: spp = 2u; break;
        case 6: spp = 4u; break;
        default: memset(out_rgba, 0, 4); return;
    }
    uint32_t bit_base = px * spp * depth;
    if (color_type == 3) {
        uint32_t idx;
        if (depth == 8) idx = row[px];
        else {
            uint32_t byte = row[bit_base >> 3];
            int shift = 8 - (int)depth - (int)(bit_base & 7u);
            idx = (byte >> shift) & ((1u << depth) - 1u);
        }
        if (idx < plte_entries) {
            out_rgba[0] = plte[idx * 3u + 0u];
            out_rgba[1] = plte[idx * 3u + 1u];
            out_rgba[2] = plte[idx * 3u + 2u];
            out_rgba[3] = (idx < trns_len) ? trns[idx] : 255u;
        } else {
            memset(out_rgba, 0, 4);
        }
        return;
    }

    if (color_type == 0) {
        uint8_t g = (uint8_t)png_sample_bits(row, bit_base, depth);
        out_rgba[0] = g;
        out_rgba[1] = g;
        out_rgba[2] = g;
        if (trns && trns_len >= 2) {
            uint16_t raw_g = (depth == 16)
                ? (uint16_t)(((uint16_t)row[px * 2u] << 8) | row[px * 2u + 1u])
                : (uint16_t)((row[bit_base >> 3] >> (depth < 8 ? (8 - depth - (int)(bit_base & 7u)) : 0)) & ((1u << depth) - 1u));
            uint16_t key = (uint16_t)(((uint16_t)trns[0] << 8) | trns[1]);
            out_rgba[3] = (raw_g == key) ? 0u : 255u;
        } else {
            out_rgba[3] = 255u;
        }
    } else if (color_type == 2) {
        uint32_t step = depth;
        out_rgba[0] = (uint8_t)png_sample_bits(row, bit_base, depth);
        out_rgba[1] = (uint8_t)png_sample_bits(row, bit_base + step, depth);
        out_rgba[2] = (uint8_t)png_sample_bits(row, bit_base + step * 2u, depth);
        if (trns && trns_len >= 6) {
            uint16_t kr = (uint16_t)(((uint16_t)trns[0] << 8) | trns[1]);
            uint16_t kg = (uint16_t)(((uint16_t)trns[2] << 8) | trns[3]);
            uint16_t kb = (uint16_t)(((uint16_t)trns[4] << 8) | trns[5]);
            uint16_t rr, rg, rb;
            if (depth == 16) {
                rr = (uint16_t)(((uint16_t)row[px * 6u] << 8) | row[px * 6u + 1u]);
                rg = (uint16_t)(((uint16_t)row[px * 6u + 2u] << 8) | row[px * 6u + 3u]);
                rb = (uint16_t)(((uint16_t)row[px * 6u + 4u] << 8) | row[px * 6u + 5u]);
            } else {
                rr = out_rgba[0];
                rg = out_rgba[1];
                rb = out_rgba[2];
            }
            out_rgba[3] = (rr == kr && rg == kg && rb == kb) ? 0u : 255u;
        } else {
            out_rgba[3] = 255u;
        }
    } else if (color_type == 4) {
        uint32_t step = depth;
        uint8_t g = (uint8_t)png_sample_bits(row, bit_base, depth);
        uint8_t a = (uint8_t)png_sample_bits(row, bit_base + step, depth);
        out_rgba[0] = g;
        out_rgba[1] = g;
        out_rgba[2] = g;
        out_rgba[3] = a;
    } else if (color_type == 6) {
        uint32_t step = depth;
        out_rgba[0] = (uint8_t)png_sample_bits(row, bit_base, depth);
        out_rgba[1] = (uint8_t)png_sample_bits(row, bit_base + step, depth);
        out_rgba[2] = (uint8_t)png_sample_bits(row, bit_base + step * 2u, depth);
        out_rgba[3] = (uint8_t)png_sample_bits(row, bit_base + step * 3u, depth);
    }
}

static int load_png_rgba8(const char *path, McTexture *out) {
    uint64_t fsize = 0;
    uint8_t *file = plat_file_read(path, &fsize);
    if (!file || fsize < 33) {
        free(file);
        return 0;
    }

    static const uint8_t k_png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (memcmp(file, k_png_sig, 8) != 0) {
        free(file);
        return 0;
    }

    uint32_t w = 0, h = 0;
    uint8_t depth = 0, color_type = 0, comp = 0, filt = 0, interlace = 0;
    int have_ihdr = 0, have_iend = 0;

    uint8_t plte[256 * 3];
    uint32_t plte_entries = 0;
    uint8_t trns[256];
    uint32_t trns_len = 0;

    uint8_t *idat = NULL;
    size_t idat_len = 0, idat_cap = 0;

    size_t pos = 8;
    while (pos + 12 <= (size_t)fsize) {
        uint32_t clen = read_be32(file + pos);
        const uint8_t *ctype = file + pos + 4;
        pos += 8;
        if (pos + (size_t)clen + 4 > (size_t)fsize) break;
        const uint8_t *cdata = file + pos;

        if (memcmp(ctype, "IHDR", 4) == 0) {
            if (clen != 13 || have_ihdr) { free(idat); free(file); return 0; }
            w = read_be32(cdata);
            h = read_be32(cdata + 4);
            depth = cdata[8];
            color_type = cdata[9];
            comp = cdata[10];
            filt = cdata[11];
            interlace = cdata[12];
            have_ihdr = 1;
        } else if (memcmp(ctype, "PLTE", 4) == 0) {
            if ((clen % 3u) != 0u || clen > sizeof plte) { free(idat); free(file); return 0; }
            memcpy(plte, cdata, clen);
            plte_entries = clen / 3u;
        } else if (memcmp(ctype, "tRNS", 4) == 0) {
            uint32_t cp = clen < (uint32_t)sizeof trns ? clen : (uint32_t)sizeof trns;
            memcpy(trns, cdata, cp);
            trns_len = cp;
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            if (clen > 0) {
                if (idat_len + clen > idat_cap) {
                    size_t ncap = idat_cap ? idat_cap * 2u : 4096u;
                    while (ncap < idat_len + clen) ncap *= 2u;
                    if (ncap > 64u * 1024u * 1024u) { free(idat); free(file); return 0; }
                    uint8_t *nb = (uint8_t *)realloc(idat, ncap);
                    if (!nb) { free(idat); free(file); return 0; }
                    idat = nb;
                    idat_cap = ncap;
                }
                memcpy(idat + idat_len, cdata, clen);
                idat_len += clen;
            }
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            have_iend = 1;
            pos += (size_t)clen + 4;
            break;
        }
        pos += (size_t)clen + 4;
    }
    free(file);

    if (!have_ihdr || !have_iend || !idat || idat_len == 0 ||
        w == 0 || h == 0 || w > 4096 || h > 4096 || comp != 0 || filt != 0 ||
        (interlace != 0 && interlace != 1)) {
        free(idat);
        return 0;
    }

    uint32_t spp = 0;
    switch (color_type) {
        case 0: spp = 1u; if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) { free(idat); return 0; } break;
        case 2: spp = 3u; if (depth != 8 && depth != 16) { free(idat); return 0; } break;
        case 3: spp = 1u; if (depth != 1 && depth != 2 && depth != 4 && depth != 8) { free(idat); return 0; } break;
        case 4: spp = 2u; if (depth != 8 && depth != 16) { free(idat); return 0; } break;
        case 6: spp = 4u; if (depth != 8 && depth != 16) { free(idat); return 0; } break;
        default: free(idat); return 0;
    }

    uint32_t bits_per_pixel = spp * depth;
    uint32_t bpp = (bits_per_pixel + 7u) / 8u;
    size_t raw_size = 0;
    if (interlace == 0) {
        uint32_t stride = (w * bits_per_pixel + 7u) / 8u;
        raw_size = (size_t)h * (1u + (size_t)stride);
    } else {
        static const uint8_t x0[7] = {0, 4, 0, 2, 0, 1, 0};
        static const uint8_t y0[7] = {0, 0, 4, 0, 2, 0, 1};
        static const uint8_t dx[7] = {8, 8, 4, 4, 2, 2, 1};
        static const uint8_t dy[7] = {8, 8, 8, 4, 4, 2, 2};
        for (int pass = 0; pass < 7; pass++) {
            if (w <= x0[pass] || h <= y0[pass]) continue;
            uint32_t pw = (w - x0[pass] + dx[pass] - 1u) / dx[pass];
            uint32_t ph = (h - y0[pass] + dy[pass] - 1u) / dy[pass];
            uint32_t stride = (pw * bits_per_pixel + 7u) / 8u;
            raw_size += (size_t)ph * (1u + (size_t)stride);
        }
    }

    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) {
        free(idat);
        return 0;
    }
    int ok = zlib_inflate(idat, idat_len, raw, raw_size);
    free(idat);
    if (!ok) {
        free(raw);
        return 0;
    }

    uint8_t *rgba = (uint8_t *)calloc(1u, (size_t)w * (size_t)h * 4u);
    if (!rgba) {
        free(raw);
        return 0;
    }

    if (interlace == 0) {
        uint32_t stride = (w * bits_per_pixel + 7u) / 8u;
        if (!unfilter_rows(raw, w, h, bpp, stride)) {
            free(rgba);
            free(raw);
            return 0;
        }
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *row = raw + (size_t)y * (1u + (size_t)stride) + 1u;
            for (uint32_t x = 0; x < w; x++) {
                png_decode_pixel(row, x, color_type, depth, plte, plte_entries, trns, trns_len,
                                 rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u);
            }
        }
    } else {
        static const uint8_t x0[7] = {0, 4, 0, 2, 0, 1, 0};
        static const uint8_t y0[7] = {0, 0, 4, 0, 2, 0, 1};
        static const uint8_t dx[7] = {8, 8, 4, 4, 2, 2, 1};
        static const uint8_t dy[7] = {8, 8, 8, 4, 4, 2, 2};
        size_t off = 0;
        for (int pass = 0; pass < 7; pass++) {
            if (w <= x0[pass] || h <= y0[pass]) continue;
            uint32_t pw = (w - x0[pass] + dx[pass] - 1u) / dx[pass];
            uint32_t ph = (h - y0[pass] + dy[pass] - 1u) / dy[pass];
            uint32_t stride = (pw * bits_per_pixel + 7u) / 8u;
            uint8_t *praw = raw + off;
            if (!unfilter_rows(praw, pw, ph, bpp, stride)) {
                free(rgba);
                free(raw);
                return 0;
            }
            for (uint32_t py = 0; py < ph; py++) {
                const uint8_t *row = praw + (size_t)py * (1u + (size_t)stride) + 1u;
                uint32_t dst_y = y0[pass] + py * dy[pass];
                for (uint32_t px = 0; px < pw; px++) {
                    uint32_t dst_x = x0[pass] + px * dx[pass];
                    png_decode_pixel(row, px, color_type, depth, plte, plte_entries, trns, trns_len,
                                     rgba + ((size_t)dst_y * (size_t)w + (size_t)dst_x) * 4u);
                }
            }
            off += (size_t)ph * (1u + (size_t)stride);
        }
    }

    free(raw);
    out->rgba = rgba;
    out->width = (int)w;
    out->height = (int)h;
    out->loaded = 1;
    return 1;
}

/* =========================================================================
 * Asset Root Resolution & Loaders
 * ========================================================================= */

static int dir_has_core_assets(const char *dir) {
    if (!dir || !dir[0]) return 0;
    char p1[1200], p2[1200];
    if (snprintf(p1, sizeof p1, "%s/textures/font/ascii.png", dir) >= (int)sizeof p1) return 0;
    if (snprintf(p2, sizeof p2, "%s/sounds/random/explode1.wav", dir) >= (int)sizeof p2) return 0;
    return plat_file_exists(p1) && plat_file_exists(p2);
}

static int try_candidate_root(const char *candidate, char *out_root, size_t cap) {
    if (!candidate || !candidate[0]) return 0;
    if (!dir_has_core_assets(candidate)) return 0;
    char resolved[PLAT_PATH_MAX];
    if (plat_full_path(candidate, resolved, sizeof resolved) && dir_has_core_assets(resolved)) {
        if (snprintf(out_root, cap, "%s", resolved) < (int)cap) return 1;
    }
    if (snprintf(out_root, cap, "%s", candidate) < (int)cap) return 1;
    return 0;
}

static int resolve_assets_root(const char *override_dir, char *out_root, size_t cap) {
    if (try_candidate_root(override_dir, out_root, cap)) return 1;

    char env_dir[PLAT_PATH_MAX];
    if (plat_getenv("TOTK_MC_ASSETS", env_dir, sizeof env_dir) > 0 &&
        try_candidate_root(env_dir, out_root, cap)) {
        return 1;
    }

    char exe_buf[PLAT_PATH_MAX];
    if (plat_exe_path(exe_buf, sizeof exe_buf)) {
        char dir[PLAT_PATH_MAX];
        snprintf(dir, sizeof dir, "%s", exe_buf);
        char *sep = plat_last_sep(dir);
        if (sep) {
            *sep = '\0';
            char cand[PLAT_PATH_MAX];
            if (snprintf(cand, sizeof cand, "%s/assets/mc", dir) < (int)sizeof cand &&
                try_candidate_root(cand, out_root, cap)) {
                return 1;
            }
            if (snprintf(cand, sizeof cand, "%s/../assets/mc", dir) < (int)sizeof cand &&
                try_candidate_root(cand, out_root, cap)) {
                return 1;
            }
            if (snprintf(cand, sizeof cand, "%s/../../assets/mc", dir) < (int)sizeof cand &&
                try_candidate_root(cand, out_root, cap)) {
                return 1;
            }
        }
    }

    char appdata[PLAT_PATH_MAX];
    if (plat_getenv("APPDATA", appdata, sizeof appdata) > 0) {
        char cand[PLAT_PATH_MAX];
        if (snprintf(cand, sizeof cand, "%s/totk-pc-recomp/assets/mc", appdata) < (int)sizeof cand &&
            try_candidate_root(cand, out_root, cap)) {
            return 1;
        }
        if (snprintf(cand, sizeof cand, "%s/TOTK Recomp/assets/mc", appdata) < (int)sizeof cand &&
            try_candidate_root(cand, out_root, cap)) {
            return 1;
        }
    }

    if (try_candidate_root("assets/mc", out_root, cap)) return 1;
    if (try_candidate_root("../assets/mc", out_root, cap)) return 1;
    return 0;
}

static int load_wav_pcm16(const char *path, McSound *out) {
    uint64_t fsize = 0;
    uint8_t *file = plat_file_read(path, &fsize);
    if (!file || fsize < 12) {
        free(file);
        return 0;
    }

    if (memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) {
        free(file);
        return 0;
    }

    int have_fmt = 0;
    uint16_t audio_fmt = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    int16_t *pcm = NULL;
    uint32_t data_bytes = 0;

    size_t pos = 12;
    while (pos + 8 <= (size_t)fsize) {
        const uint8_t *chdr = file + pos;
        uint32_t csize = read_le32(chdr + 4);
        pos += 8;
        if (pos + (size_t)csize > (size_t)fsize) break;

        if (memcmp(chdr, "fmt ", 4) == 0) {
            if (csize < 16 || csize > 4096) break;
            const uint8_t *fbuf = file + pos;
            audio_fmt = read_le16(fbuf);
            channels = read_le16(fbuf + 2);
            sample_rate = read_le32(fbuf + 4);
            bits = read_le16(fbuf + 14);
            have_fmt = 1;
        } else if (memcmp(chdr, "data", 4) == 0) {
            if (csize == 0 || csize > 64u * 1024u * 1024u) break;
            free(pcm);
            pcm = NULL;
            data_bytes = 0;
            pcm = (int16_t *)malloc(csize);
            if (!pcm) break;
            memcpy(pcm, file + pos, csize);
            data_bytes = csize;
            if (have_fmt) break;
        }
        pos += (size_t)csize + ((size_t)csize & 1u);
    }
    free(file);

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
        uint64_t sz = 0;
        uint8_t *buf = plat_file_read(path, &sz);
        if (buf && sz >= 256) {
            memcpy(g_font_widths, buf, 256);
            free(buf);
            g_font_widths_loaded = 1;
            return;
        }
        free(buf);
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
    uint64_t sz = 0;
    uint8_t *raw = plat_file_read(path, &sz);
    if (!raw || sz == 0 || sz > 1024u * 1024u) {
        free(raw);
        return;
    }
    char *buf = (char *)raw;
    buf[sz] = '\0';

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
    atomic_store_explicit(&g_ready, 0, memory_order_release);
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
}

int mc_assets_init(const char *override_dir) {
    if (!override_dir && atomic_load_explicit(&g_ready, memory_order_acquire)) {
        return 1;
    }

    plat_mutex_lock(&g_init_mu);
    if (atomic_load_explicit(&g_ready, memory_order_relaxed)) {
        if (!override_dir || strcmp(override_dir, g_root_dir) == 0) {
            plat_mutex_unlock(&g_init_mu);
            return 1;
        }
        free_all_locked();
    }

    char root[1024];
    if (!resolve_assets_root(override_dir, root, sizeof root)) {
        plat_mutex_unlock(&g_init_mu);
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
    plat_mutex_unlock(&g_init_mu);
    return ok;
}

int mc_assets_ready(void) {
    return atomic_load_explicit(&g_ready, memory_order_acquire);
}

void mc_assets_shutdown(void) {
    plat_mutex_lock(&g_init_mu);
    free_all_locked();
    plat_mutex_unlock(&g_init_mu);
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
