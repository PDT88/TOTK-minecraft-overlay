#!/usr/bin/env python3
"""Extract authentic Minecraft Java 1.21.11 textures, font widths, en_us strings,
and 48 kHz 16-bit PCM WAV sounds into assets/mc/ for totk-pc-recomp.

Reads:
  - Client JAR: %APPDATA%\\.minecraft\\versions\\1.21.11\\1.21.11.jar
    (configurable via --jar or MINECRAFT_JAR)
  - Asset objects & index JSON: %APPDATA%\\.minecraft\\assets\\objects + indexes\\<id>.json
    (auto-resolved from <jar_dir>\\<version>.json or configurable via --assets-dir / --index)

Uses ffmpeg.exe (or soundfile+numpy fallback) to decode .ogg audio objects into
48000 Hz 16-bit PCM .wav files matching mc_assets.c.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import struct
import subprocess
import sys
import wave
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT_DIR = REPO_ROOT / "assets" / "mc"
DEFAULT_VERSION = "1.21.11"

# Every texture required by mc_assets.h / mc_assets.c (k_tex_paths, 204 entries).
# Value is the path inside the 1.21.11 client JAR if it differs from "assets/minecraft/<rel_path>".
TEX_OVERRIDES: Dict[str, str] = {
    # HUD & GUI sprites (relocated into textures/gui/sprites/ and containers in 1.21.x)
    "textures/gui/hotbar.png": "assets/minecraft/textures/gui/sprites/hud/hotbar.png",
    "textures/gui/hotbar_selection.png": "assets/minecraft/textures/gui/sprites/hud/hotbar_selection.png",
    "textures/gui/hotbar_offhand_left.png": "assets/minecraft/textures/gui/sprites/hud/hotbar_offhand_left.png",
    "textures/gui/hotbar_offhand_right.png": "assets/minecraft/textures/gui/sprites/hud/hotbar_offhand_right.png",
    "textures/gui/crosshair.png": "assets/minecraft/textures/gui/sprites/hud/crosshair.png",
    "textures/gui/crosshair_attack_indicator_background.png": "assets/minecraft/textures/gui/sprites/hud/crosshair_attack_indicator_background.png",
    "textures/gui/crosshair_attack_indicator_full.png": "assets/minecraft/textures/gui/sprites/hud/crosshair_attack_indicator_full.png",
    "textures/gui/crosshair_attack_indicator_progress.png": "assets/minecraft/textures/gui/sprites/hud/crosshair_attack_indicator_progress.png",
    "textures/gui/experience_bar_background.png": "assets/minecraft/textures/gui/sprites/hud/experience_bar_background.png",
    "textures/gui/experience_bar_progress.png": "assets/minecraft/textures/gui/sprites/hud/experience_bar_progress.png",
    "textures/gui/heart_container.png": "assets/minecraft/textures/gui/sprites/hud/heart/container.png",
    "textures/gui/heart_container_blinking.png": "assets/minecraft/textures/gui/sprites/hud/heart/container_blinking.png",
    "textures/gui/heart_full.png": "assets/minecraft/textures/gui/sprites/hud/heart/full.png",
    "textures/gui/heart_full_blinking.png": "assets/minecraft/textures/gui/sprites/hud/heart/full_blinking.png",
    "textures/gui/heart_half.png": "assets/minecraft/textures/gui/sprites/hud/heart/half.png",
    "textures/gui/heart_half_blinking.png": "assets/minecraft/textures/gui/sprites/hud/heart/half_blinking.png",
    "textures/gui/heart_absorbing_full.png": "assets/minecraft/textures/gui/sprites/hud/heart/absorbing_full.png",
    "textures/gui/heart_absorbing_half.png": "assets/minecraft/textures/gui/sprites/hud/heart/absorbing_half.png",
    "textures/gui/food_empty.png": "assets/minecraft/textures/gui/sprites/hud/food_empty.png",
    "textures/gui/food_half.png": "assets/minecraft/textures/gui/sprites/hud/food_half.png",
    "textures/gui/food_full.png": "assets/minecraft/textures/gui/sprites/hud/food_full.png",
    "textures/gui/armor_empty.png": "assets/minecraft/textures/gui/sprites/hud/armor_empty.png",
    "textures/gui/armor_half.png": "assets/minecraft/textures/gui/sprites/hud/armor_half.png",
    "textures/gui/armor_full.png": "assets/minecraft/textures/gui/sprites/hud/armor_full.png",
    "textures/gui/effect_background.png": "assets/minecraft/textures/gui/sprites/hud/effect_background.png",
    "textures/gui/effect_background_ambient.png": "assets/minecraft/textures/gui/sprites/hud/effect_background_ambient.png",
    "textures/gui/tooltip_background.png": "assets/minecraft/textures/gui/sprites/tooltip/background.png",
    "textures/gui/tooltip_frame.png": "assets/minecraft/textures/gui/sprites/tooltip/frame.png",
    "textures/gui/inventory.png": "assets/minecraft/textures/gui/container/inventory.png",
    "textures/gui/creative_tab_items.png": "assets/minecraft/textures/gui/container/creative_inventory/tab_items.png",
    "textures/gui/creative_tab_item_search.png": "assets/minecraft/textures/gui/container/creative_inventory/tab_item_search.png",
    "textures/gui/creative_tab_inventory.png": "assets/minecraft/textures/gui/container/creative_inventory/tab_inventory.png",
    "textures/gui/creative_scroller.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/scroller.png",
    "textures/gui/creative_scroller_disabled.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/scroller_disabled.png",
    "textures/gui/creative_tab_top_selected_1.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_selected_1.png",
    "textures/gui/creative_tab_top_selected_2.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_selected_2.png",
    "textures/gui/creative_tab_top_unselected_1.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_unselected_1.png",
    "textures/gui/creative_tab_top_unselected_2.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_unselected_2.png",
    "textures/gui/creative_tab_bottom_selected_1.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_bottom_selected_1.png",
    "textures/gui/creative_tab_bottom_unselected_1.png": "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_bottom_unselected_1.png",
    "textures/gui/slot_shield.png": "assets/minecraft/textures/gui/sprites/container/slot/shield.png",
    "textures/gui/slot_highlight_back.png": "assets/minecraft/textures/gui/sprites/container/bundle/slot_highlight_back.png",
    "textures/gui/slot_highlight_front.png": "assets/minecraft/textures/gui/sprites/container/bundle/slot_highlight_front.png",
    "textures/gui/recipe_book_button.png": "assets/minecraft/textures/gui/sprites/recipe_book/button.png",
    "textures/gui/recipe_book_button_highlighted.png": "assets/minecraft/textures/gui/sprites/recipe_book/button_highlighted.png",
    "textures/gui/slot_helmet.png": "assets/minecraft/textures/gui/sprites/container/slot/helmet.png",
    "textures/gui/slot_chestplate.png": "assets/minecraft/textures/gui/sprites/container/slot/chestplate.png",
    "textures/gui/slot_leggings.png": "assets/minecraft/textures/gui/sprites/container/slot/leggings.png",
    "textures/gui/slot_boots.png": "assets/minecraft/textures/gui/sprites/container/slot/boots.png",
    # Entity & armor equipment layers (relocated in 1.21.x)
    "textures/entity/elytra.png": "assets/minecraft/textures/entity/equipment/wings/elytra.png",
    "textures/entity/steve.png": "assets/minecraft/textures/entity/player/wide/steve.png",
    "textures/entity/creeper.png": "assets/minecraft/textures/entity/creeper/creeper.png",
    "textures/entity/zombie.png": "assets/minecraft/textures/entity/zombie/zombie.png",
    "textures/entity/skeleton.png": "assets/minecraft/textures/entity/skeleton/skeleton.png",
    "textures/entity/arrow.png": "assets/minecraft/textures/entity/projectiles/arrow.png",
    "textures/entity/armor_diamond_1.png": "assets/minecraft/textures/entity/equipment/humanoid/diamond.png",
    "textures/entity/armor_diamond_2.png": "assets/minecraft/textures/entity/equipment/humanoid_leggings/diamond.png",
    "textures/entity/armor_netherite_1.png": "assets/minecraft/textures/entity/equipment/humanoid/netherite.png",
    "textures/entity/armor_netherite_2.png": "assets/minecraft/textures/entity/equipment/humanoid_leggings/netherite.png",
}

REQUIRED_TEXTURES: List[str] = [
    # HUD sprites & widgets
    "textures/gui/hotbar.png",
    "textures/gui/hotbar_selection.png",
    "textures/gui/hotbar_offhand_left.png",
    "textures/gui/hotbar_offhand_right.png",
    "textures/gui/crosshair.png",
    "textures/gui/crosshair_attack_indicator_background.png",
    "textures/gui/crosshair_attack_indicator_full.png",
    "textures/gui/crosshair_attack_indicator_progress.png",
    "textures/gui/experience_bar_background.png",
    "textures/gui/experience_bar_progress.png",
    "textures/gui/heart_container.png",
    "textures/gui/heart_container_blinking.png",
    "textures/gui/heart_full.png",
    "textures/gui/heart_full_blinking.png",
    "textures/gui/heart_half.png",
    "textures/gui/heart_half_blinking.png",
    "textures/gui/heart_absorbing_full.png",
    "textures/gui/heart_absorbing_half.png",
    "textures/gui/food_empty.png",
    "textures/gui/food_half.png",
    "textures/gui/food_full.png",
    "textures/gui/armor_empty.png",
    "textures/gui/armor_half.png",
    "textures/gui/armor_full.png",
    "textures/gui/effect_background.png",
    "textures/gui/effect_background_ambient.png",
    "textures/gui/tooltip_background.png",
    "textures/gui/tooltip_frame.png",
    "textures/gui/inventory.png",
    "textures/gui/creative_tab_items.png",
    "textures/gui/creative_tab_item_search.png",
    "textures/gui/creative_tab_inventory.png",
    "textures/gui/creative_scroller.png",
    "textures/gui/creative_scroller_disabled.png",
    "textures/gui/creative_tab_top_selected_1.png",
    "textures/gui/creative_tab_top_selected_2.png",
    "textures/gui/creative_tab_top_unselected_1.png",
    "textures/gui/creative_tab_top_unselected_2.png",
    "textures/gui/creative_tab_bottom_selected_1.png",
    "textures/gui/creative_tab_bottom_unselected_1.png",
    "textures/gui/slot_shield.png",
    "textures/gui/slot_highlight_back.png",
    "textures/gui/slot_highlight_front.png",
    "textures/gui/recipe_book_button.png",
    "textures/gui/recipe_book_button_highlighted.png",
    # Status effect icons
    "textures/mob_effect/resistance.png",
    "textures/mob_effect/fire_resistance.png",
    "textures/mob_effect/absorption.png",
    "textures/mob_effect/regeneration.png",
    "textures/mob_effect/speed.png",
    # Font
    "textures/font/ascii.png",
    # Items
    "textures/item/cooked_beef.png",
    "textures/item/diamond_sword.png",
    "textures/item/bow.png",
    "textures/item/bow_pulling_0.png",
    "textures/item/bow_pulling_1.png",
    "textures/item/bow_pulling_2.png",
    "textures/item/diamond_axe.png",
    "textures/item/golden_apple.png",
    "textures/item/diamond_pickaxe.png",
    "textures/item/firework_rocket.png",
    "textures/item/bone.png",
    "textures/item/arrow.png",
    "textures/item/totem_of_undying.png",
    "textures/item/elytra.png",
    "textures/item/elytra_broken.png",
    "textures/item/flint_and_steel.png",
    "textures/item/water_bucket.png",
    "textures/item/lava_bucket.png",
    "textures/item/ender_pearl.png",
    "textures/item/bread.png",
    "textures/item/apple.png",
    "textures/item/charcoal.png",
    "textures/item/coal.png",
    "textures/item/gunpowder.png",
    "textures/item/rotten_flesh.png",
    "textures/item/spider_eye.png",
    "textures/item/string.png",
    "textures/item/slime_ball.png",
    "textures/item/experience_bottle.png",
    "textures/item/stick.png",
    "textures/item/iron_ingot.png",
    "textures/item/gold_ingot.png",
    "textures/item/diamond.png",
    "textures/item/emerald.png",
    "textures/item/redstone.png",
    "textures/item/crossbow_standby.png",
    "textures/item/crossbow_arrow.png",
    "textures/item/iron_sword.png",
    "textures/item/netherite_sword.png",
    "textures/item/diamond_helmet.png",
    "textures/item/diamond_chestplate.png",
    "textures/item/diamond_leggings.png",
    "textures/item/diamond_boots.png",
    "textures/item/barrier.png",
    "textures/item/beef.png",
    # Blocks
    "textures/block/tnt_top.png",
    "textures/block/tnt_bottom.png",
    "textures/block/tnt_side.png",
    "textures/block/oak_log.png",
    "textures/block/oak_log_top.png",
    "textures/block/oak_planks.png",
    "textures/block/grass_block_top.png",
    "textures/block/grass_block_side.png",
    "textures/block/grass_block_side_overlay.png",
    "textures/block/dirt.png",
    "textures/block/stone.png",
    "textures/block/cobblestone.png",
    "textures/block/obsidian.png",
    "textures/block/bedrock.png",
    "textures/block/torch.png",
    "textures/block/diamond_block.png",
    "textures/block/gold_block.png",
    "textures/block/glass.png",
    "textures/block/glowstone.png",
    "textures/block/crafting_table_top.png",
    "textures/block/crafting_table_side.png",
    "textures/block/crafting_table_front.png",
    "textures/block/furnace_top.png",
    "textures/block/furnace_side.png",
    "textures/block/furnace_front.png",
    "textures/block/water_still.png",
    "textures/block/lava_still.png",
    "textures/block/destroy_stage_0.png",
    "textures/block/destroy_stage_1.png",
    "textures/block/destroy_stage_2.png",
    "textures/block/destroy_stage_3.png",
    "textures/block/destroy_stage_4.png",
    "textures/block/destroy_stage_5.png",
    "textures/block/destroy_stage_6.png",
    "textures/block/destroy_stage_7.png",
    "textures/block/destroy_stage_8.png",
    "textures/block/destroy_stage_9.png",
    "textures/block/red_wool.png",
    "textures/block/sand.png",
    "textures/block/glass_pane_top.png",
    # Colormap, entities, misc, particles
    "textures/colormap/grass.png",
    "textures/entity/shield_base_nopattern.png",
    "textures/entity/shield_base.png",
    "textures/entity/elytra.png",
    "textures/entity/steve.png",
    "textures/entity/creeper.png",
    "textures/entity/zombie.png",
    "textures/entity/skeleton.png",
    "textures/entity/arrow.png",
    "textures/entity/bed/red.png",
    "textures/entity/wolf/wolf_tame.png",
    "textures/misc/enchanted_glint_item.png",
    "textures/misc/enchanted_glint_armor.png",
    "textures/particle/flame.png",
    "textures/particle/glint.png",
    "textures/particle/critical_hit.png",
    "textures/particle/explosion_0.png",
    "textures/particle/explosion_1.png",
    "textures/particle/explosion_2.png",
    "textures/particle/explosion_3.png",
    "textures/particle/explosion_4.png",
    "textures/particle/explosion_5.png",
    "textures/particle/explosion_6.png",
    "textures/particle/explosion_7.png",
    "textures/particle/explosion_8.png",
    "textures/particle/explosion_9.png",
    "textures/particle/explosion_10.png",
    "textures/particle/explosion_11.png",
    "textures/particle/explosion_12.png",
    "textures/particle/explosion_13.png",
    "textures/particle/explosion_14.png",
    "textures/particle/explosion_15.png",
    "textures/particle/big_smoke_0.png",
    "textures/particle/big_smoke_1.png",
    "textures/particle/big_smoke_2.png",
    "textures/particle/big_smoke_3.png",
    "textures/particle/big_smoke_4.png",
    "textures/particle/big_smoke_5.png",
    "textures/particle/big_smoke_6.png",
    "textures/particle/big_smoke_7.png",
    "textures/particle/big_smoke_8.png",
    "textures/particle/big_smoke_9.png",
    "textures/particle/big_smoke_10.png",
    "textures/particle/big_smoke_11.png",
    "textures/particle/sweep_0.png",
    "textures/particle/sweep_1.png",
    "textures/particle/sweep_2.png",
    "textures/particle/sweep_3.png",
    "textures/particle/sweep_4.png",
    "textures/particle/sweep_5.png",
    "textures/particle/sweep_6.png",
    "textures/particle/sweep_7.png",
    "textures/particle/generic_0.png",
    "textures/particle/generic_1.png",
    "textures/particle/generic_2.png",
    "textures/particle/generic_3.png",
    "textures/particle/generic_4.png",
    "textures/particle/generic_5.png",
    "textures/particle/generic_6.png",
    "textures/particle/generic_7.png",
    # Armor layers & armor slot outlines
    "textures/entity/armor_diamond_1.png",
    "textures/entity/armor_diamond_2.png",
    "textures/entity/armor_netherite_1.png",
    "textures/entity/armor_netherite_2.png",
    "textures/gui/slot_helmet.png",
    "textures/gui/slot_chestplate.png",
    "textures/gui/slot_leggings.png",
    "textures/gui/slot_boots.png",
]

# Sound index overrides where mc_assets.c uses sounds/entity/* or sounds/item/firework/*
# while Minecraft's asset index stores them under minecraft/sounds/mob/* or minecraft/sounds/fireworks/*.
SOUND_OVERRIDES: Dict[str, str] = {
    "sounds/entity/creeper/say1.wav": "minecraft/sounds/mob/creeper/say1.ogg",
    "sounds/entity/creeper/say2.wav": "minecraft/sounds/mob/creeper/say2.ogg",
    "sounds/entity/creeper/say3.wav": "minecraft/sounds/mob/creeper/say3.ogg",
    "sounds/entity/creeper/say4.wav": "minecraft/sounds/mob/creeper/say4.ogg",
    "sounds/entity/creeper/death.wav": "minecraft/sounds/mob/creeper/death.ogg",
    "sounds/item/firework/launch1.wav": "minecraft/sounds/fireworks/launch1.ogg",
    "sounds/item/firework/blast1.wav": "minecraft/sounds/fireworks/blast1.ogg",
    "sounds/item/firework/largeblast1.wav": "minecraft/sounds/fireworks/largeblast1.ogg",
    "sounds/item/firework/twinkle1.wav": "minecraft/sounds/fireworks/twinkle1.ogg",
    "sounds/entity/zombie/say1.wav": "minecraft/sounds/mob/zombie/say1.ogg",
    "sounds/entity/zombie/say2.wav": "minecraft/sounds/mob/zombie/say2.ogg",
    "sounds/entity/zombie/say3.wav": "minecraft/sounds/mob/zombie/say3.ogg",
    "sounds/entity/zombie/hurt1.wav": "minecraft/sounds/mob/zombie/hurt1.ogg",
    "sounds/entity/zombie/hurt2.wav": "minecraft/sounds/mob/zombie/hurt2.ogg",
    "sounds/entity/zombie/death.wav": "minecraft/sounds/mob/zombie/death.ogg",
    "sounds/entity/skeleton/say1.wav": "minecraft/sounds/mob/skeleton/say1.ogg",
    "sounds/entity/skeleton/say2.wav": "minecraft/sounds/mob/skeleton/say2.ogg",
    "sounds/entity/skeleton/say3.wav": "minecraft/sounds/mob/skeleton/say3.ogg",
    "sounds/entity/skeleton/hurt1.wav": "minecraft/sounds/mob/skeleton/hurt1.ogg",
    "sounds/entity/skeleton/hurt2.wav": "minecraft/sounds/mob/skeleton/hurt2.ogg",
    "sounds/entity/skeleton/hurt3.wav": "minecraft/sounds/mob/skeleton/hurt3.ogg",
    "sounds/entity/skeleton/hurt4.wav": "minecraft/sounds/mob/skeleton/hurt4.ogg",
    "sounds/entity/skeleton/death.wav": "minecraft/sounds/mob/skeleton/death.ogg",
}

REQUIRED_SOUNDS: List[str] = [
    "sounds/dig/wood1.wav",
    "sounds/dig/wood2.wav",
    "sounds/dig/wood3.wav",
    "sounds/dig/wood4.wav",
    "sounds/dig/stone1.wav",
    "sounds/dig/stone2.wav",
    "sounds/dig/stone3.wav",
    "sounds/dig/stone4.wav",
    "sounds/dig/grass1.wav",
    "sounds/dig/grass2.wav",
    "sounds/dig/grass3.wav",
    "sounds/dig/grass4.wav",
    "sounds/dig/gravel1.wav",
    "sounds/dig/gravel2.wav",
    "sounds/dig/gravel3.wav",
    "sounds/dig/gravel4.wav",
    "sounds/dig/sand1.wav",
    "sounds/dig/sand2.wav",
    "sounds/dig/sand3.wav",
    "sounds/dig/sand4.wav",
    "sounds/step/wood1.wav",
    "sounds/step/wood2.wav",
    "sounds/step/wood3.wav",
    "sounds/step/wood4.wav",
    "sounds/step/wood5.wav",
    "sounds/step/wood6.wav",
    "sounds/step/stone1.wav",
    "sounds/step/stone2.wav",
    "sounds/step/stone3.wav",
    "sounds/step/stone4.wav",
    "sounds/step/stone5.wav",
    "sounds/step/stone6.wav",
    "sounds/step/grass1.wav",
    "sounds/step/grass2.wav",
    "sounds/step/grass3.wav",
    "sounds/step/grass4.wav",
    "sounds/step/grass5.wav",
    "sounds/step/grass6.wav",
    "sounds/step/gravel1.wav",
    "sounds/step/gravel2.wav",
    "sounds/step/gravel3.wav",
    "sounds/step/gravel4.wav",
    "sounds/step/sand1.wav",
    "sounds/step/sand2.wav",
    "sounds/step/sand3.wav",
    "sounds/step/sand4.wav",
    "sounds/step/sand5.wav",
    "sounds/random/bow.wav",
    "sounds/random/bowhit1.wav",
    "sounds/random/bowhit2.wav",
    "sounds/random/bowhit3.wav",
    "sounds/random/bowhit4.wav",
    "sounds/random/successful_hit.wav",
    "sounds/entity/player/attack/strong1.wav",
    "sounds/entity/player/attack/strong2.wav",
    "sounds/entity/player/attack/strong3.wav",
    "sounds/entity/player/attack/strong4.wav",
    "sounds/entity/player/attack/strong5.wav",
    "sounds/entity/player/attack/strong6.wav",
    "sounds/entity/player/attack/sweep1.wav",
    "sounds/entity/player/attack/sweep2.wav",
    "sounds/entity/player/attack/sweep3.wav",
    "sounds/entity/player/attack/sweep4.wav",
    "sounds/entity/player/attack/sweep5.wav",
    "sounds/entity/player/attack/sweep6.wav",
    "sounds/entity/player/attack/sweep7.wav",
    "sounds/entity/player/attack/crit1.wav",
    "sounds/entity/player/attack/crit2.wav",
    "sounds/entity/player/attack/crit3.wav",
    "sounds/entity/player/attack/weak1.wav",
    "sounds/entity/player/attack/weak2.wav",
    "sounds/entity/player/attack/weak3.wav",
    "sounds/entity/player/attack/weak4.wav",
    "sounds/entity/player/attack/knockback1.wav",
    "sounds/entity/player/attack/knockback2.wav",
    "sounds/entity/player/attack/knockback3.wav",
    "sounds/entity/player/attack/knockback4.wav",
    "sounds/item/shield/block1.wav",
    "sounds/item/shield/block2.wav",
    "sounds/item/shield/block3.wav",
    "sounds/item/shield/block4.wav",
    "sounds/item/shield/block5.wav",
    "sounds/random/break.wav",
    "sounds/random/eat1.wav",
    "sounds/random/eat2.wav",
    "sounds/random/eat3.wav",
    "sounds/random/burp.wav",
    "sounds/random/drink.wav",
    "sounds/random/orb.wav",
    "sounds/random/levelup.wav",
    "sounds/item/totem/use_totem.wav",
    "sounds/random/click.wav",
    "sounds/random/click_stereo.wav",
    "sounds/fire/ignite.wav",
    "sounds/random/fuse.wav",
    "sounds/random/explode1.wav",
    "sounds/random/explode2.wav",
    "sounds/random/explode3.wav",
    "sounds/random/explode4.wav",
    "sounds/entity/creeper/say1.wav",
    "sounds/entity/creeper/say2.wav",
    "sounds/entity/creeper/say3.wav",
    "sounds/entity/creeper/say4.wav",
    "sounds/entity/creeper/death.wav",
    "sounds/item/firework/launch1.wav",
    "sounds/item/firework/blast1.wav",
    "sounds/item/firework/largeblast1.wav",
    "sounds/item/firework/twinkle1.wav",
    "sounds/item/elytra/elytra_loop.wav",
    "sounds/damage/hit1.wav",
    "sounds/damage/hit2.wav",
    "sounds/damage/hit3.wav",
    "sounds/random/classic_hurt.wav",
    "sounds/damage/fallsmall.wav",
    "sounds/damage/fallbig.wav",
    "sounds/entity/zombie/say1.wav",
    "sounds/entity/zombie/say2.wav",
    "sounds/entity/zombie/say3.wav",
    "sounds/entity/zombie/hurt1.wav",
    "sounds/entity/zombie/hurt2.wav",
    "sounds/entity/zombie/death.wav",
    "sounds/entity/skeleton/say1.wav",
    "sounds/entity/skeleton/say2.wav",
    "sounds/entity/skeleton/say3.wav",
    "sounds/entity/skeleton/hurt1.wav",
    "sounds/entity/skeleton/hurt2.wav",
    "sounds/entity/skeleton/hurt3.wav",
    "sounds/entity/skeleton/hurt4.wav",
    "sounds/entity/skeleton/death.wav",
    "sounds/random/glass1.wav",
    "sounds/random/glass2.wav",
    "sounds/random/glass3.wav",
    "sounds/random/pop.wav",
    "sounds/random/splash.wav",
]

# English block/item/UI strings loaded by mc_assets.c (keep <= MC_MAX_EN_US = 128).
REQUIRED_EN_US_KEYS: List[str] = [
    # Blocks
    "block.minecraft.tnt",
    "block.minecraft.oak_log",
    "block.minecraft.oak_planks",
    "block.minecraft.grass_block",
    "block.minecraft.dirt",
    "block.minecraft.stone",
    "block.minecraft.cobblestone",
    "block.minecraft.obsidian",
    "block.minecraft.bedrock",
    "block.minecraft.torch",
    "block.minecraft.diamond_block",
    "block.minecraft.gold_block",
    "block.minecraft.glass",
    "block.minecraft.glowstone",
    "block.minecraft.crafting_table",
    "block.minecraft.furnace",
    "block.minecraft.water",
    "block.minecraft.lava",
    "block.minecraft.sand",
    "block.minecraft.red_wool",
    "block.minecraft.red_bed",
    "block.minecraft.glass_pane",
    "block.minecraft.barrier",
    # Items
    "item.minecraft.cooked_beef",
    "item.minecraft.beef",
    "item.minecraft.diamond_sword",
    "item.minecraft.iron_sword",
    "item.minecraft.netherite_sword",
    "item.minecraft.diamond_axe",
    "item.minecraft.diamond_pickaxe",
    "item.minecraft.bow",
    "item.minecraft.crossbow",
    "item.minecraft.arrow",
    "item.minecraft.shield",
    "item.minecraft.elytra",
    "item.minecraft.totem_of_undying",
    "item.minecraft.golden_apple",
    "item.minecraft.enchanted_golden_apple",
    "item.minecraft.apple",
    "item.minecraft.bread",
    "item.minecraft.firework_rocket",
    "item.minecraft.flint_and_steel",
    "item.minecraft.bone",
    "item.minecraft.ender_pearl",
    "item.minecraft.water_bucket",
    "item.minecraft.lava_bucket",
    "item.minecraft.experience_bottle",
    "item.minecraft.diamond_helmet",
    "item.minecraft.diamond_chestplate",
    "item.minecraft.diamond_leggings",
    "item.minecraft.diamond_boots",
    "item.minecraft.diamond",
    "item.minecraft.emerald",
    "item.minecraft.iron_ingot",
    "item.minecraft.gold_ingot",
    "item.minecraft.redstone",
    "item.minecraft.coal",
    "item.minecraft.charcoal",
    "item.minecraft.stick",
    "item.minecraft.gunpowder",
    "item.minecraft.rotten_flesh",
    "item.minecraft.spider_eye",
    "item.minecraft.string",
    "item.minecraft.slime_ball",
    # UI & Creative Tabs
    "container.crafting",
    "container.inventory",
    "container.furnace",
    "itemGroup.buildingBlocks",
    "itemGroup.combat",
    "itemGroup.tools",
    "itemGroup.foodAndDrink",
    "itemGroup.search",
    # Status Effects
    "effect.minecraft.resistance",
    "effect.minecraft.fire_resistance",
    "effect.minecraft.absorption",
    "effect.minecraft.regeneration",
    "effect.minecraft.speed",
]


def default_minecraft_dir() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / ".minecraft"
    return Path.home() / "AppData" / "Roaming" / ".minecraft"


def resolve_jar_path(cli_jar: Optional[Path]) -> Path:
    if cli_jar:
        return cli_jar
    env_jar = os.environ.get("MINECRAFT_JAR")
    if env_jar:
        return Path(env_jar)
    return default_minecraft_dir() / "versions" / DEFAULT_VERSION / f"{DEFAULT_VERSION}.jar"


def resolve_assets_and_index(
    jar_path: Path, cli_assets: Optional[Path], cli_index: Optional[Path]
) -> Tuple[Path, Path, str]:
    mc_dir = default_minecraft_dir()
    if cli_assets:
        assets_dir = cli_assets
    else:
        # Walk up from versions/<ver>/<ver>.jar -> .minecraft/assets if present.
        cand = jar_path.parent.parent.parent / "assets"
        assets_dir = cand if cand.is_dir() else (mc_dir / "assets")

    index_id = "29"
    ver_json = jar_path.with_suffix(".json")
    if ver_json.is_file():
        try:
            ver_meta = json.loads(ver_json.read_text(encoding="utf-8"))
            index_id = str(ver_meta.get("assetIndex", {}).get("id") or ver_meta.get("assets") or index_id)
        except Exception:
            pass

    if cli_index:
        index_path = cli_index
        index_id = index_path.stem
    else:
        index_path = assets_dir / "indexes" / f"{index_id}.json"

    return assets_dir, index_path, index_id


def find_ffmpeg(cli_ffmpeg: Optional[Path]) -> Optional[Path]:
    if cli_ffmpeg:
        return cli_ffmpeg if cli_ffmpeg.is_file() else None

    which = shutil.which("ffmpeg") or shutil.which("ffmpeg.exe")
    if which:
        return Path(which)

    # Check current Windows User/Machine PATH from registry (handles fresh winget installs
    # before the current shell reloads PATH) and standard WinGet package directories.
    local_app = os.environ.get("LOCALAPPDATA")
    candidates: List[Path] = []
    if local_app:
        winget_links = Path(local_app) / "Microsoft" / "WinGet" / "Links" / "ffmpeg.exe"
        candidates.append(winget_links)
        winget_pkgs = Path(local_app) / "Microsoft" / "WinGet" / "Packages"
        if winget_pkgs.is_dir():
            for pkg in sorted(winget_pkgs.glob("Gyan.FFmpeg*")):
                candidates.extend(pkg.glob("*/bin/ffmpeg.exe"))
                candidates.extend(pkg.glob("bin/ffmpeg.exe"))

    if os.name == "nt":
        try:
            import winreg

            for hive, subkey in [
                (winreg.HKEY_CURRENT_USER, r"Environment"),
                (winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment"),
            ]:
                try:
                    with winreg.OpenKey(hive, subkey) as k:
                        val, _ = winreg.QueryValueEx(k, "Path")
                        for entry in val.split(";"):
                            entry = os.path.expandvars(entry.strip().strip('"'))
                            if entry:
                                candidates.append(Path(entry) / "ffmpeg.exe")
                except OSError:
                    pass
        except ImportError:
            pass

    for cand in candidates:
        if cand.is_file():
            return cand
    return None


def compute_ascii_widths(ascii_png: Path) -> bytes:
    """Compute Mojangles 256-entry advance width table matching mc_assets.c."""
    with Image.open(ascii_png) as im:
        rgba = im.convert("RGBA")
        w, h = rgba.size
        px = rgba.load()

    cw = max(1, w // 16)
    ch = max(1, h // 16)
    scale_x = max(1, cw // 8)
    widths = bytearray(256)

    for code in range(256):
        if code == 32:
            widths[code] = 4
            continue
        col = code % 16
        row = code // 16
        max_x = -1
        x0 = col * cw
        y0 = row * ch
        for py in range(ch):
            for ix in range(cw):
                if px[x0 + ix, y0 + py][3] > 0 and ix > max_x:
                    max_x = ix
        widths[code] = (max_x // scale_x) + 2 if max_x >= 0 else 4

    return bytes(widths)


def decode_ogg_with_ffmpeg(ffmpeg_exe: Path, src_ogg: Path, dst_wav: Path) -> None:
    dst_wav.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(ffmpeg_exe),
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(src_ogg),
        "-ar",
        "48000",
        "-acodec",
        "pcm_s16le",
        "-fflags",
        "+bitexact",
        "-f",
        "wav",
        str(dst_wav),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"ffmpeg failed on {src_ogg}: {res.stderr.strip()}")


def decode_ogg_with_soundfile(src_ogg: Path, dst_wav: Path) -> None:
    import numpy as np
    import soundfile as sf

    data, sr = sf.read(str(src_ogg), dtype="float32", always_2d=True)
    if data.shape[1] > 2:
        data = data[:, :2]
    if sr != 48000 and data.shape[0] > 0:
        out_len = max(1, int(round(data.shape[0] * (48000.0 / float(sr)))))
        xp = np.linspace(0.0, float(data.shape[0] - 1), num=data.shape[0], dtype=np.float64)
        x_new = np.linspace(0.0, float(data.shape[0] - 1), num=out_len, dtype=np.float64)
        resampled = np.empty((out_len, data.shape[1]), dtype=np.float32)
        for c in range(data.shape[1]):
            resampled[:, c] = np.interp(x_new, xp, data[:, c]).astype(np.float32)
        data = resampled

    pcm16 = np.clip(np.round(data * 32767.0), -32768.0, 32767.0).astype("<i2")
    dst_wav.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(dst_wav), "wb") as wf:
        wf.setnchannels(int(pcm16.shape[1]))
        wf.setsampwidth(2)
        wf.setframerate(48000)
        wf.writeframes(pcm16.tobytes())


def inspect_wav(wav_path: Path) -> Dict[str, int]:
    """Validate WAV is standard RIFF PCM 16-bit 48000 Hz mono/stereo (matching mc_assets.c)."""
    raw = wav_path.read_bytes()
    if len(raw) < 44 or raw[0:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"Invalid RIFF/WAVE header in {wav_path}")

    pos = 12
    audio_fmt = channels = bits = 0
    sample_rate = data_bytes = 0
    have_fmt = have_data = False
    while pos + 8 <= len(raw):
        cid = raw[pos : pos + 4]
        csize = struct.unpack_from("<I", raw, pos + 4)[0]
        pos += 8
        if cid == b"fmt " and csize >= 16 and pos + csize <= len(raw):
            audio_fmt, channels, sample_rate, _, _, bits = struct.unpack_from("<HHIIHH", raw, pos)
            have_fmt = True
        elif cid == b"data" and pos + csize <= len(raw):
            data_bytes = csize
            have_data = True
        pos += csize + (csize & 1)

    if not (have_fmt and have_data):
        raise ValueError(f"Missing fmt/data chunk in {wav_path}")
    if audio_fmt != 1 or bits != 16 or sample_rate != 48000 or channels not in (1, 2):
        raise ValueError(
            f"Unsupported WAV format in {wav_path}: fmt={audio_fmt} ch={channels} rate={sample_rate} bits={bits}"
        )
    frames = data_bytes // (channels * 2)
    if frames <= 0:
        raise ValueError(f"Empty audio data in {wav_path}")
    return {"sample_rate": sample_rate, "channels": channels, "bits": bits, "frames": frames}


def verify_output(out_dir: Path) -> Dict[str, int]:
    missing_tex = [rel for rel in REQUIRED_TEXTURES if not (out_dir / rel).is_file()]
    if missing_tex:
        raise RuntimeError(f"Missing {len(missing_tex)} textures: {missing_tex[:5]}")

    for rel in REQUIRED_TEXTURES:
        with Image.open(out_dir / rel) as im:
            im.verify()

    widths_path = out_dir / "textures" / "font" / "ascii_widths.bin"
    if not widths_path.is_file() or widths_path.stat().st_size != 256:
        raise RuntimeError(f"Invalid or missing {widths_path}")

    missing_snd = [rel for rel in REQUIRED_SOUNDS if not (out_dir / rel).is_file()]
    if missing_snd:
        raise RuntimeError(f"Missing {len(missing_snd)} sounds: {missing_snd[:5]}")

    for rel in REQUIRED_SOUNDS:
        inspect_wav(out_dir / rel)

    en_us_path = out_dir / "en_us.json"
    if not en_us_path.is_file():
        raise RuntimeError(f"Missing {en_us_path}")
    en_us = json.loads(en_us_path.read_text(encoding="utf-8"))
    if len(en_us) > 128:
        raise RuntimeError(f"en_us.json has {len(en_us)} keys (> MC_MAX_EN_US=128)")
    missing_keys = [k for k in REQUIRED_EN_US_KEYS if k not in en_us]
    if missing_keys:
        raise RuntimeError(f"Missing {len(missing_keys)} en_us keys: {missing_keys[:5]}")

    manifest_path = out_dir / "manifest.json"
    if not manifest_path.is_file():
        raise RuntimeError(f"Missing {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if len(manifest.get("textures", [])) != len(REQUIRED_TEXTURES):
        raise RuntimeError("manifest.json texture count mismatch")
    if len(manifest.get("sounds", [])) != len(REQUIRED_SOUNDS):
        raise RuntimeError("manifest.json sound count mismatch")

    return {
        "textures": len(REQUIRED_TEXTURES),
        "sounds": len(REQUIRED_SOUNDS),
        "en_us_keys": len(en_us),
    }


def extract_assets(
    jar_path: Path,
    assets_dir: Path,
    index_path: Path,
    index_id: str,
    out_dir: Path,
    ffmpeg_exe: Optional[Path],
) -> None:
    if not jar_path.is_file():
        raise FileNotFoundError(f"Minecraft JAR not found: {jar_path}")
    if not index_path.is_file():
        raise FileNotFoundError(f"Minecraft asset index JSON not found: {index_path}")
    objects_dir = assets_dir / "objects"
    if not objects_dir.is_dir():
        raise FileNotFoundError(f"Minecraft assets/objects directory not found: {objects_dir}")

    out_dir.mkdir(parents=True, exist_ok=True)
    tex_manifest: List[Dict[str, object]] = []
    en_us_out: Dict[str, str] = {}

    with zipfile.ZipFile(jar_path, "r") as zf:
        jar_names = set(zf.namelist())

        for rel_path in REQUIRED_TEXTURES:
            jar_member = TEX_OVERRIDES.get(rel_path, f"assets/minecraft/{rel_path}")
            if jar_member not in jar_names:
                raise FileNotFoundError(f"Missing texture in JAR: {jar_member} (for {rel_path})")
            dst = out_dir / rel_path
            dst.parent.mkdir(parents=True, exist_ok=True)
            data = zf.read(jar_member)
            dst.write_bytes(data)
            with Image.open(dst) as im:
                w, h = im.size
            tex_manifest.append(
                {
                    "rel_path": rel_path,
                    "source": jar_member,
                    "width": w,
                    "height": h,
                }
            )

        lang_member = "assets/minecraft/lang/en_us.json"
        if lang_member not in jar_names:
            raise FileNotFoundError(f"Missing {lang_member} in JAR")
        full_en_us = json.loads(zf.read(lang_member).decode("utf-8"))
        for key in REQUIRED_EN_US_KEYS:
            if key not in full_en_us:
                raise KeyError(f"Missing required en_us key in JAR: {key}")
            en_us_out[key] = str(full_en_us[key])

    # Compute Mojangles glyph advance widths (256 bytes)
    widths_bytes = compute_ascii_widths(out_dir / "textures" / "font" / "ascii.png")
    (out_dir / "textures" / "font" / "ascii_widths.bin").write_bytes(widths_bytes)

    # Write trimmed en_us.json (ASCII/UTF-8 JSON, <= MC_MAX_EN_US entries)
    (out_dir / "en_us.json").write_text(
        json.dumps(en_us_out, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    # Extract and decode sounds
    idx_objects = json.loads(index_path.read_text(encoding="utf-8")).get("objects", {})
    sound_jobs: List[Tuple[str, str, Path, Path]] = []
    for rel_path in REQUIRED_SOUNDS:
        idx_key = SOUND_OVERRIDES.get(rel_path, f"minecraft/{rel_path[:-4]}.ogg")
        entry = idx_objects.get(idx_key)
        if not entry or "hash" not in entry:
            raise KeyError(f"Missing sound in asset index {index_path}: {idx_key} (for {rel_path})")
        h = entry["hash"]
        src_ogg = objects_dir / h[:2] / h
        if not src_ogg.is_file():
            raise FileNotFoundError(f"Missing audio object {src_ogg} for {idx_key}")
        sound_jobs.append((rel_path, idx_key, src_ogg, out_dir / rel_path))

    def convert_one(job: Tuple[str, str, Path, Path]) -> Dict[str, object]:
        rel_path, idx_key, src_ogg, dst_wav = job
        if ffmpeg_exe:
            decode_ogg_with_ffmpeg(ffmpeg_exe, src_ogg, dst_wav)
        else:
            decode_ogg_with_soundfile(src_ogg, dst_wav)
        info = inspect_wav(dst_wav)
        return {
            "rel_path": rel_path,
            "source": idx_key,
            "sample_rate": info["sample_rate"],
            "channels": info["channels"],
            "bits": info["bits"],
            "frames": info["frames"],
        }

    workers = min(8, os.cpu_count() or 4)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        snd_manifest = list(pool.map(convert_one, sound_jobs))

    manifest = {
        "format_version": 1,
        "minecraft_version": DEFAULT_VERSION,
        "asset_index": index_id,
        "jar_path": str(jar_path),
        "audio_decoder": str(ffmpeg_exe) if ffmpeg_exe else "soundfile+numpy",
        "counts": {
            "textures": len(tex_manifest),
            "sounds": len(snd_manifest),
            "en_us_keys": len(en_us_out),
            "font_widths": len(widths_bytes),
        },
        "font_widths": "textures/font/ascii_widths.bin",
        "en_us": "en_us.json",
        "textures": tex_manifest,
        "sounds": snd_manifest,
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def mirror_directory(src: Path, dst: Path) -> None:
    if src.resolve() == dst.resolve():
        return
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jar", type=Path, help="Path to Minecraft 1.21.11 client JAR (or set MINECRAFT_JAR)")
    parser.add_argument("--assets-dir", type=Path, help="Path to .minecraft/assets directory")
    parser.add_argument("--index", type=Path, help="Path to Minecraft asset index JSON (e.g. 29.json)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT_DIR, help="Output assets/mc directory")
    parser.add_argument("--copy-to", type=Path, action="append", default=[], help="Additional destination dir(s) to mirror assets/mc into")
    parser.add_argument("--ffmpeg", type=Path, help="Explicit path to ffmpeg.exe")
    parser.add_argument("--verify-only", action="store_true", help="Only verify an existing assets/mc output directory")
    args = parser.parse_args()

    if args.verify_only:
        stats = verify_output(args.out)
        print(
            f"[extract_mc_assets] Verified {args.out}: "
            f"{stats['textures']} textures, {stats['sounds']} WAVs (48kHz PCM16), "
            f"{stats['en_us_keys']} en_us keys, ascii_widths.bin + manifest.json OK"
        )
        return 0

    jar_path = resolve_jar_path(args.jar)
    assets_dir, index_path, index_id = resolve_assets_and_index(jar_path, args.assets_dir, args.index)
    ffmpeg_exe = find_ffmpeg(args.ffmpeg)
    if not ffmpeg_exe:
        try:
            import numpy  # noqa: F401
            import soundfile  # noqa: F401
            print("[extract_mc_assets] Warning: ffmpeg.exe not found; using soundfile+numpy fallback", file=sys.stderr)
        except ImportError:
            print(
                "[extract_mc_assets] ERROR: ffmpeg.exe not found on PATH (install with: winget install -e --id Gyan.FFmpeg)",
                file=sys.stderr,
            )
            return 1

    extract_assets(jar_path, assets_dir, index_path, index_id, args.out, ffmpeg_exe)
    stats = verify_output(args.out)

    for extra in args.copy_to:
        mirror_directory(args.out, extra)
        verify_output(extra)

    print(
        f"[extract_mc_assets] Extracted & verified {args.out}: "
        f"{stats['textures']}/{len(REQUIRED_TEXTURES)} textures, "
        f"{stats['sounds']}/{len(REQUIRED_SOUNDS)} WAVs (48kHz PCM16 via "
        f"{ffmpeg_exe.name if ffmpeg_exe else 'soundfile'}), "
        f"{stats['en_us_keys']} en_us keys, ascii_widths.bin, manifest.json"
    )
    for extra in args.copy_to:
        print(f"[extract_mc_assets] Mirrored & verified -> {extra}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
