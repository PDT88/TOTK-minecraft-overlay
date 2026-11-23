#!/usr/bin/env python3
"""Extract 100% authentic Minecraft Java 1.21.11 textures, font widths,
en_us strings, and decoded 48 kHz PCM WAV sounds into assets/mc/ and sync
to the packaged Tears of the Kingdom.app bundle.

Zero procedural/synthetic textures, fonts, or audio are generated.
Zero non-English (Spanish) strings are emitted.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import wave
import zipfile
import zlib

DEFAULT_JAR = Path("/Users/pdt/Library/Application Support/minecraft/versions/1.21.11/1.21.11.jar")
DEFAULT_INDEX = Path("/Users/pdt/Library/Application Support/minecraft/assets/indexes/29.json")
DEFAULT_OBJECTS = Path("/Users/pdt/Library/Application Support/minecraft/assets/objects")
DEFAULT_OUT = Path("/Users/pdt/Games/TOTK_build/totk/assets/mc")
DEFAULT_SYNC_APP = Path("/Users/pdt/Games/Tears of the Kingdom.app/Contents/Resources/totk/assets/mc")
DEFAULT_FFMPEG = Path("/opt/homebrew/bin/ffmpeg")

# Mapping: (output relpath under assets/mc/textures/, path inside 1.21.11.jar)
TEXTURE_MAP: list[tuple[str, str]] = [
    # HUD sprites & widgets
    ("gui/hotbar.png", "assets/minecraft/textures/gui/sprites/hud/hotbar.png"),
    ("gui/hotbar_selection.png", "assets/minecraft/textures/gui/sprites/hud/hotbar_selection.png"),
    ("gui/hotbar_offhand_left.png", "assets/minecraft/textures/gui/sprites/hud/hotbar_offhand_left.png"),
    ("gui/hotbar_offhand_right.png", "assets/minecraft/textures/gui/sprites/hud/hotbar_offhand_right.png"),
    ("gui/crosshair.png", "assets/minecraft/textures/gui/sprites/hud/crosshair.png"),
    ("gui/crosshair_attack_indicator_background.png", "assets/minecraft/textures/gui/sprites/hud/crosshair_attack_indicator_background.png"),
    ("gui/crosshair_attack_indicator_full.png", "assets/minecraft/textures/gui/sprites/hud/crosshair_attack_indicator_full.png"),
    ("gui/crosshair_attack_indicator_progress.png", "assets/minecraft/textures/gui/sprites/hud/crosshair_attack_indicator_progress.png"),
    ("gui/experience_bar_background.png", "assets/minecraft/textures/gui/sprites/hud/experience_bar_background.png"),
    ("gui/experience_bar_progress.png", "assets/minecraft/textures/gui/sprites/hud/experience_bar_progress.png"),
    ("gui/heart_container.png", "assets/minecraft/textures/gui/sprites/hud/heart/container.png"),
    ("gui/heart_container_blinking.png", "assets/minecraft/textures/gui/sprites/hud/heart/container_blinking.png"),
    ("gui/heart_full.png", "assets/minecraft/textures/gui/sprites/hud/heart/full.png"),
    ("gui/heart_full_blinking.png", "assets/minecraft/textures/gui/sprites/hud/heart/full_blinking.png"),
    ("gui/heart_half.png", "assets/minecraft/textures/gui/sprites/hud/heart/half.png"),
    ("gui/heart_half_blinking.png", "assets/minecraft/textures/gui/sprites/hud/heart/half_blinking.png"),
    ("gui/heart_absorbing_full.png", "assets/minecraft/textures/gui/sprites/hud/heart/absorbing_full.png"),
    ("gui/heart_absorbing_half.png", "assets/minecraft/textures/gui/sprites/hud/heart/absorbing_half.png"),
    ("gui/food_empty.png", "assets/minecraft/textures/gui/sprites/hud/food_empty.png"),
    ("gui/food_half.png", "assets/minecraft/textures/gui/sprites/hud/food_half.png"),
    ("gui/food_full.png", "assets/minecraft/textures/gui/sprites/hud/food_full.png"),
    ("gui/armor_empty.png", "assets/minecraft/textures/gui/sprites/hud/armor_empty.png"),
    ("gui/armor_half.png", "assets/minecraft/textures/gui/sprites/hud/armor_half.png"),
    ("gui/armor_full.png", "assets/minecraft/textures/gui/sprites/hud/armor_full.png"),
    ("gui/effect_background.png", "assets/minecraft/textures/gui/sprites/hud/effect_background.png"),
    ("gui/effect_background_ambient.png", "assets/minecraft/textures/gui/sprites/hud/effect_background_ambient.png"),
    ("gui/tooltip_background.png", "assets/minecraft/textures/gui/sprites/tooltip/background.png"),
    ("gui/tooltip_frame.png", "assets/minecraft/textures/gui/sprites/tooltip/frame.png"),
    ("gui/inventory.png", "assets/minecraft/textures/gui/container/inventory.png"),
    ("gui/creative_tab_items.png", "assets/minecraft/textures/gui/container/creative_inventory/tab_items.png"),
    ("gui/creative_tab_item_search.png", "assets/minecraft/textures/gui/container/creative_inventory/tab_item_search.png"),
    ("gui/creative_tab_inventory.png", "assets/minecraft/textures/gui/container/creative_inventory/tab_inventory.png"),
    ("gui/creative_scroller.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/scroller.png"),
    ("gui/creative_scroller_disabled.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/scroller_disabled.png"),
    ("gui/creative_tab_top_selected_1.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_selected_1.png"),
    ("gui/creative_tab_top_selected_2.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_selected_2.png"),
    ("gui/creative_tab_top_unselected_1.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_unselected_1.png"),
    ("gui/creative_tab_top_unselected_2.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_top_unselected_2.png"),
    ("gui/creative_tab_bottom_selected_1.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_bottom_selected_1.png"),
    ("gui/creative_tab_bottom_unselected_1.png", "assets/minecraft/textures/gui/sprites/container/creative_inventory/tab_bottom_unselected_1.png"),
    ("gui/slot_shield.png", "assets/minecraft/textures/gui/sprites/container/slot/shield.png"),
    ("gui/slot_highlight_back.png", "assets/minecraft/textures/gui/sprites/container/slot_highlight_back.png"),
    ("gui/slot_highlight_front.png", "assets/minecraft/textures/gui/sprites/container/slot_highlight_front.png"),
    ("gui/recipe_book_button.png", "assets/minecraft/textures/gui/sprites/recipe_book/button.png"),
    ("gui/recipe_book_button_highlighted.png", "assets/minecraft/textures/gui/sprites/recipe_book/button_highlighted.png"),

    # Mob effects
    ("mob_effect/resistance.png", "assets/minecraft/textures/mob_effect/resistance.png"),
    ("mob_effect/fire_resistance.png", "assets/minecraft/textures/mob_effect/fire_resistance.png"),
    ("mob_effect/absorption.png", "assets/minecraft/textures/mob_effect/absorption.png"),
    ("mob_effect/regeneration.png", "assets/minecraft/textures/mob_effect/regeneration.png"),
    ("mob_effect/speed.png", "assets/minecraft/textures/mob_effect/speed.png"),

    # Font
    ("font/ascii.png", "assets/minecraft/textures/font/ascii.png"),

    # Items
    ("item/cooked_beef.png", "assets/minecraft/textures/item/cooked_beef.png"),
    ("item/diamond_sword.png", "assets/minecraft/textures/item/diamond_sword.png"),
    ("item/bow.png", "assets/minecraft/textures/item/bow.png"),
    ("item/bow_pulling_0.png", "assets/minecraft/textures/item/bow_pulling_0.png"),
    ("item/bow_pulling_1.png", "assets/minecraft/textures/item/bow_pulling_1.png"),
    ("item/bow_pulling_2.png", "assets/minecraft/textures/item/bow_pulling_2.png"),
    ("item/diamond_axe.png", "assets/minecraft/textures/item/diamond_axe.png"),
    ("item/golden_apple.png", "assets/minecraft/textures/item/golden_apple.png"),
    ("item/diamond_pickaxe.png", "assets/minecraft/textures/item/diamond_pickaxe.png"),
    ("item/firework_rocket.png", "assets/minecraft/textures/item/firework_rocket.png"),
    ("item/bone.png", "assets/minecraft/textures/item/bone.png"),
    ("item/arrow.png", "assets/minecraft/textures/item/arrow.png"),
    ("item/totem_of_undying.png", "assets/minecraft/textures/item/totem_of_undying.png"),
    ("item/elytra.png", "assets/minecraft/textures/item/elytra.png"),
    ("item/elytra_broken.png", "assets/minecraft/textures/item/elytra_broken.png"),
    ("item/flint_and_steel.png", "assets/minecraft/textures/item/flint_and_steel.png"),
    ("item/water_bucket.png", "assets/minecraft/textures/item/water_bucket.png"),
    ("item/lava_bucket.png", "assets/minecraft/textures/item/lava_bucket.png"),
    ("item/ender_pearl.png", "assets/minecraft/textures/item/ender_pearl.png"),
    ("item/bread.png", "assets/minecraft/textures/item/bread.png"),
    ("item/apple.png", "assets/minecraft/textures/item/apple.png"),
    ("item/charcoal.png", "assets/minecraft/textures/item/charcoal.png"),
    ("item/coal.png", "assets/minecraft/textures/item/coal.png"),
    ("item/gunpowder.png", "assets/minecraft/textures/item/gunpowder.png"),
    ("item/rotten_flesh.png", "assets/minecraft/textures/item/rotten_flesh.png"),
    ("item/spider_eye.png", "assets/minecraft/textures/item/spider_eye.png"),
    ("item/string.png", "assets/minecraft/textures/item/string.png"),
    ("item/slime_ball.png", "assets/minecraft/textures/item/slime_ball.png"),
    ("item/experience_bottle.png", "assets/minecraft/textures/item/experience_bottle.png"),
    ("item/stick.png", "assets/minecraft/textures/item/stick.png"),
    ("item/iron_ingot.png", "assets/minecraft/textures/item/iron_ingot.png"),
    ("item/gold_ingot.png", "assets/minecraft/textures/item/gold_ingot.png"),
    ("item/diamond.png", "assets/minecraft/textures/item/diamond.png"),
    ("item/emerald.png", "assets/minecraft/textures/item/emerald.png"),
    ("item/redstone.png", "assets/minecraft/textures/item/redstone.png"),
    ("item/crossbow_standby.png", "assets/minecraft/textures/item/crossbow_standby.png"),
    ("item/crossbow_arrow.png", "assets/minecraft/textures/item/crossbow_arrow.png"),
    ("item/iron_sword.png", "assets/minecraft/textures/item/iron_sword.png"),
    ("item/netherite_sword.png", "assets/minecraft/textures/item/netherite_sword.png"),
    ("item/diamond_helmet.png", "assets/minecraft/textures/item/diamond_helmet.png"),
    ("item/diamond_chestplate.png", "assets/minecraft/textures/item/diamond_chestplate.png"),
    ("item/diamond_leggings.png", "assets/minecraft/textures/item/diamond_leggings.png"),
    ("item/diamond_boots.png", "assets/minecraft/textures/item/diamond_boots.png"),
    ("item/barrier.png", "assets/minecraft/textures/item/barrier.png"),

    # Blocks
    ("block/tnt_top.png", "assets/minecraft/textures/block/tnt_top.png"),
    ("block/tnt_bottom.png", "assets/minecraft/textures/block/tnt_bottom.png"),
    ("block/tnt_side.png", "assets/minecraft/textures/block/tnt_side.png"),
    ("block/oak_log.png", "assets/minecraft/textures/block/oak_log.png"),
    ("block/oak_log_top.png", "assets/minecraft/textures/block/oak_log_top.png"),
    ("block/oak_planks.png", "assets/minecraft/textures/block/oak_planks.png"),
    ("block/grass_block_top.png", "assets/minecraft/textures/block/grass_block_top.png"),
    ("block/grass_block_side.png", "assets/minecraft/textures/block/grass_block_side.png"),
    ("block/grass_block_side_overlay.png", "assets/minecraft/textures/block/grass_block_side_overlay.png"),
    ("block/dirt.png", "assets/minecraft/textures/block/dirt.png"),
    ("block/stone.png", "assets/minecraft/textures/block/stone.png"),
    ("block/cobblestone.png", "assets/minecraft/textures/block/cobblestone.png"),
    ("block/obsidian.png", "assets/minecraft/textures/block/obsidian.png"),
    ("block/bedrock.png", "assets/minecraft/textures/block/bedrock.png"),
    ("block/torch.png", "assets/minecraft/textures/block/torch.png"),
    ("block/diamond_block.png", "assets/minecraft/textures/block/diamond_block.png"),
    ("block/gold_block.png", "assets/minecraft/textures/block/gold_block.png"),
    ("block/glass.png", "assets/minecraft/textures/block/glass.png"),
    ("block/glowstone.png", "assets/minecraft/textures/block/glowstone.png"),
    ("block/crafting_table_top.png", "assets/minecraft/textures/block/crafting_table_top.png"),
    ("block/crafting_table_side.png", "assets/minecraft/textures/block/crafting_table_side.png"),
    ("block/crafting_table_front.png", "assets/minecraft/textures/block/crafting_table_front.png"),
    ("block/furnace_top.png", "assets/minecraft/textures/block/furnace_top.png"),
    ("block/furnace_side.png", "assets/minecraft/textures/block/furnace_side.png"),
    ("block/furnace_front.png", "assets/minecraft/textures/block/furnace_front.png"),
    ("block/water_still.png", "assets/minecraft/textures/block/water_still.png"),
    ("block/lava_still.png", "assets/minecraft/textures/block/lava_still.png"),
    *[(f"block/destroy_stage_{i}.png", f"assets/minecraft/textures/block/destroy_stage_{i}.png") for i in range(10)],

    # Colormaps, Entities, Misc, Particles
    ("colormap/grass.png", "assets/minecraft/textures/colormap/grass.png"),
    ("entity/shield_base_nopattern.png", "assets/minecraft/textures/entity/shield_base_nopattern.png"),
    ("entity/shield_base.png", "assets/minecraft/textures/entity/shield_base.png"),
    ("entity/elytra.png", "assets/minecraft/textures/entity/equipment/wings/elytra.png"),
    ("entity/steve.png", "assets/minecraft/textures/entity/player/wide/steve.png"),
    ("entity/creeper.png", "assets/minecraft/textures/entity/creeper/creeper.png"),
    ("entity/zombie.png", "assets/minecraft/textures/entity/zombie/zombie.png"),
    ("entity/skeleton.png", "assets/minecraft/textures/entity/skeleton/skeleton.png"),
    ("entity/arrow.png", "assets/minecraft/textures/entity/projectiles/arrow.png"),
    ("misc/enchanted_glint_item.png", "assets/minecraft/textures/misc/enchanted_glint_item.png"),
    ("misc/enchanted_glint_armor.png", "assets/minecraft/textures/misc/enchanted_glint_armor.png"),
    ("particle/flame.png", "assets/minecraft/textures/particle/flame.png"),
    ("particle/glint.png", "assets/minecraft/textures/particle/glint.png"),
    ("particle/critical_hit.png", "assets/minecraft/textures/particle/critical_hit.png"),
    *[(f"particle/explosion_{i}.png", f"assets/minecraft/textures/particle/explosion_{i}.png") for i in range(16)],
    *[(f"particle/big_smoke_{i}.png", f"assets/minecraft/textures/particle/big_smoke_{i}.png") for i in range(12)],
    *[(f"particle/sweep_{i}.png", f"assets/minecraft/textures/particle/sweep_{i}.png") for i in range(8)],
    *[(f"particle/generic_{i}.png", f"assets/minecraft/textures/particle/generic_{i}.png") for i in range(8)],
    ("entity/armor_diamond_1.png", "assets/minecraft/textures/entity/equipment/humanoid/diamond.png"),
    ("entity/armor_diamond_2.png", "assets/minecraft/textures/entity/equipment/humanoid_leggings/diamond.png"),
    ("entity/armor_netherite_1.png", "assets/minecraft/textures/entity/equipment/humanoid/netherite.png"),
    ("entity/armor_netherite_2.png", "assets/minecraft/textures/entity/equipment/humanoid_leggings/netherite.png"),
    ("gui/slot_helmet.png", "assets/minecraft/textures/gui/sprites/container/slot/helmet.png"),
    ("gui/slot_chestplate.png", "assets/minecraft/textures/gui/sprites/container/slot/chestplate.png"),
    ("gui/slot_leggings.png", "assets/minecraft/textures/gui/sprites/container/slot/leggings.png"),
    ("block/red_wool.png", "assets/minecraft/textures/block/red_wool.png"),
    ("entity/bed/red.png", "assets/minecraft/textures/entity/bed/red.png"),
    ("entity/wolf/wolf_tame.png", "assets/minecraft/textures/entity/wolf/wolf_tame.png"),
    ("block/sand.png", "assets/minecraft/textures/block/sand.png"),
    ("block/glass_pane_top.png", "assets/minecraft/textures/block/glass_pane_top.png"),
    ("item/beef.png", "assets/minecraft/textures/item/beef.png"),
    ("gui/slot_boots.png", "assets/minecraft/textures/gui/sprites/container/slot/boots.png"),
]

# Mapping: (output relpath under assets/mc/sounds/, asset key in 29.json)
SOUND_MAP: list[tuple[str, str]] = [
    *[(f"dig/wood{i}.wav", f"minecraft/sounds/dig/wood{i}.ogg") for i in range(1, 5)],
    *[(f"dig/stone{i}.wav", f"minecraft/sounds/dig/stone{i}.ogg") for i in range(1, 5)],
    *[(f"dig/grass{i}.wav", f"minecraft/sounds/dig/grass{i}.ogg") for i in range(1, 5)],
    *[(f"dig/gravel{i}.wav", f"minecraft/sounds/dig/gravel{i}.ogg") for i in range(1, 5)],
    *[(f"dig/sand{i}.wav", f"minecraft/sounds/dig/sand{i}.ogg") for i in range(1, 5)],
    *[(f"step/wood{i}.wav", f"minecraft/sounds/step/wood{i}.ogg") for i in range(1, 7)],
    *[(f"step/stone{i}.wav", f"minecraft/sounds/step/stone{i}.ogg") for i in range(1, 7)],
    *[(f"step/grass{i}.wav", f"minecraft/sounds/step/grass{i}.ogg") for i in range(1, 7)],
    *[(f"step/gravel{i}.wav", f"minecraft/sounds/step/gravel{i}.ogg") for i in range(1, 5)],
    *[(f"step/sand{i}.wav", f"minecraft/sounds/step/sand{i}.ogg") for i in range(1, 6)],

    ("random/bow.wav", "minecraft/sounds/random/bow.ogg"),
    *[(f"random/bowhit{i}.wav", f"minecraft/sounds/random/bowhit{i}.ogg") for i in range(1, 5)],
    ("random/successful_hit.wav", "minecraft/sounds/random/successful_hit.ogg"),
    *[(f"entity/player/attack/strong{i}.wav", f"minecraft/sounds/entity/player/attack/strong{i}.ogg") for i in range(1, 7)],
    *[(f"entity/player/attack/sweep{i}.wav", f"minecraft/sounds/entity/player/attack/sweep{i}.ogg") for i in range(1, 8)],
    *[(f"entity/player/attack/crit{i}.wav", f"minecraft/sounds/entity/player/attack/crit{i}.ogg") for i in range(1, 4)],
    *[(f"entity/player/attack/weak{i}.wav", f"minecraft/sounds/entity/player/attack/weak{i}.ogg") for i in range(1, 5)],
    *[(f"entity/player/attack/knockback{i}.wav", f"minecraft/sounds/entity/player/attack/knockback{i}.ogg") for i in range(1, 5)],
    *[(f"item/shield/block{i}.wav", f"minecraft/sounds/item/shield/block{i}.ogg") for i in range(1, 6)],
    ("random/break.wav", "minecraft/sounds/random/break.ogg"),

    *[(f"random/eat{i}.wav", f"minecraft/sounds/random/eat{i}.ogg") for i in range(1, 4)],
    ("random/burp.wav", "minecraft/sounds/random/burp.ogg"),
    ("random/drink.wav", "minecraft/sounds/random/drink.ogg"),
    ("random/orb.wav", "minecraft/sounds/random/orb.ogg"),
    ("random/levelup.wav", "minecraft/sounds/random/levelup.ogg"),
    ("item/totem/use_totem.wav", "minecraft/sounds/item/totem/use_totem.ogg"),
    ("random/click.wav", "minecraft/sounds/random/click.ogg"),
    ("random/click_stereo.wav", "minecraft/sounds/random/click_stereo.ogg"),
    ("fire/ignite.wav", "minecraft/sounds/fire/ignite.ogg"),

    ("random/fuse.wav", "minecraft/sounds/random/fuse.ogg"),
    *[(f"random/explode{i}.wav", f"minecraft/sounds/random/explode{i}.ogg") for i in range(1, 5)],
    *[(f"entity/creeper/say{i}.wav", f"minecraft/sounds/mob/creeper/say{i}.ogg") for i in range(1, 5)],
    ("entity/creeper/death.wav", "minecraft/sounds/mob/creeper/death.ogg"),
    ("item/firework/launch1.wav", "minecraft/sounds/fireworks/launch1.ogg"),
    ("item/firework/blast1.wav", "minecraft/sounds/fireworks/blast1.ogg"),
    ("item/firework/largeblast1.wav", "minecraft/sounds/fireworks/largeblast1.ogg"),
    ("item/firework/twinkle1.wav", "minecraft/sounds/fireworks/twinkle1.ogg"),
    ("item/elytra/elytra_loop.wav", "minecraft/sounds/item/elytra/elytra_loop.ogg"),

    *[(f"damage/hit{i}.wav", f"minecraft/sounds/damage/hit{i}.ogg") for i in range(1, 4)],
    ("random/classic_hurt.wav", "minecraft/sounds/random/classic_hurt.ogg"),
    ("damage/fallsmall.wav", "minecraft/sounds/damage/fallsmall.ogg"),
    ("damage/fallbig.wav", "minecraft/sounds/damage/fallbig.ogg"),
    *[(f"entity/zombie/say{i}.wav", f"minecraft/sounds/mob/zombie/say{i}.ogg") for i in range(1, 4)],
    *[(f"entity/zombie/hurt{i}.wav", f"minecraft/sounds/mob/zombie/hurt{i}.ogg") for i in range(1, 3)],
    ("entity/zombie/death.wav", "minecraft/sounds/mob/zombie/death.ogg"),
    *[(f"entity/skeleton/say{i}.wav", f"minecraft/sounds/mob/skeleton/say{i}.ogg") for i in range(1, 4)],
    *[(f"entity/skeleton/hurt{i}.wav", f"minecraft/sounds/mob/skeleton/hurt{i}.ogg") for i in range(1, 5)],
    ("entity/skeleton/death.wav", "minecraft/sounds/mob/skeleton/death.ogg"),
    *[(f"random/glass{i}.wav", f"minecraft/sounds/random/glass{i}.ogg") for i in range(1, 4)],
    ("random/pop.wav", "minecraft/sounds/random/pop.ogg"),
    ("random/splash.wav", "minecraft/sounds/random/splash.ogg"),
]

EN_US_KEYS = [
    "item.minecraft.cooked_beef",
    "item.minecraft.diamond_sword",
    "block.minecraft.oak_log",
    "item.minecraft.bow",
    "item.minecraft.diamond_axe",
    "item.minecraft.enchanted_golden_apple",
    "item.minecraft.golden_apple",
    "item.minecraft.diamond_pickaxe",
    "item.minecraft.firework_rocket",
    "block.minecraft.tnt",
    "item.minecraft.bone",
    "item.minecraft.shield",
    "item.minecraft.arrow",
    "item.minecraft.totem_of_undying",
    "item.minecraft.elytra",
    "item.minecraft.flint_and_steel",
    "item.minecraft.water_bucket",
    "item.minecraft.lava_bucket",
    "item.minecraft.ender_pearl",
    "item.minecraft.bread",
    "item.minecraft.apple",
    "item.minecraft.charcoal",
    "item.minecraft.coal",
    "item.minecraft.gunpowder",
    "item.minecraft.rotten_flesh",
    "item.minecraft.spider_eye",
    "item.minecraft.string",
    "item.minecraft.slime_ball",
    "item.minecraft.experience_bottle",
    "item.minecraft.stick",
    "item.minecraft.iron_ingot",
    "item.minecraft.gold_ingot",
    "item.minecraft.diamond",
    "item.minecraft.emerald",
    "item.minecraft.redstone",
    "item.minecraft.crossbow",
    "item.minecraft.iron_sword",
    "item.minecraft.netherite_sword",
    "item.minecraft.diamond_helmet",
    "item.minecraft.diamond_chestplate",
    "item.minecraft.diamond_leggings",
    "item.minecraft.diamond_boots",
    "block.minecraft.barrier",
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
    "block.minecraft.glass_pane",
    "block.minecraft.glowstone",
    "block.minecraft.crafting_table",
    "block.minecraft.furnace",
    "effect.minecraft.resistance",
    "effect.minecraft.fire_resistance",
    "effect.minecraft.absorption",
    "effect.minecraft.regeneration",
    "effect.minecraft.speed",
    "container.crafting",
    "container.inventory",
    "itemGroup.buildingBlocks",
    "itemGroup.combat",
    "itemGroup.tools",
    "itemGroup.foodAndDrink",
    "itemGroup.search",
    "itemGroup.inventory",
    "entity.minecraft.creeper",
    "entity.minecraft.zombie",
    "entity.minecraft.skeleton",
    "death.attack.explosion",
    "death.fell.accident.generic",
]

FORBIDDEN_SPANISH_SUBSTRINGS = (
    "Cohete",
    "fuegos artificiales",
    "Pico de diamante",
    "Hacha de diamante",
    "Espada de diamante",
    "Fabricación",
    "Manzana",
    "Filete",
    "Escudo",
    "Paravela",
)


def decode_png_rgba(data: bytes) -> tuple[int, int, bytes]:
    """Minimal lossless PNG decoder using stdlib struct+zlib to extract exact RGBA8 pixels."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Not a PNG stream")
    pos = 8
    idat = bytearray()
    plte: list[tuple[int, int, int]] | None = None
    trns: bytes | None = None
    ihdr: tuple[int, int, int, int, int, int, int] | None = None
    while pos < len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        ctype = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"PLTE":
            plte = [(chunk[i], chunk[i + 1], chunk[i + 2]) for i in range(0, len(chunk), 3)]
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break
    if ihdr is None:
        raise ValueError("Missing IHDR")
    w, h, bd, ct, comp, filt, interlace = ihdr
    if comp != 0 or filt != 0 or interlace != 0:
        raise ValueError(f"Unsupported PNG flags comp={comp} filt={filt} interlace={interlace}")
    raw = zlib.decompress(bytes(idat))
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    if ct in (0, 3) and bd < 8:
        stride = (w * bd + 7) // 8
        fbpp = 1
    else:
        stride = w * bpp * (bd // 8)
        fbpp = bpp * (bd // 8)
    recon = bytearray(h * stride)
    p = 0

    def paeth(a: int, b: int, c: int) -> int:
        pa = abs(b - c)
        pb = abs(a - c)
        pc = abs(a + b - 2 * c)
        if pa <= pb and pa <= pc:
            return a
        return b if pb <= pc else c

    for y in range(h):
        ft = raw[p]
        p += 1
        row_start = y * stride
        prev_start = (y - 1) * stride
        for x in range(stride):
            val = raw[p]
            p += 1
            a = recon[row_start + x - fbpp] if x >= fbpp else 0
            b = recon[prev_start + x] if y > 0 else 0
            c = recon[prev_start + x - fbpp] if (y > 0 and x >= fbpp) else 0
            if ft == 0:
                out = val
            elif ft == 1:
                out = (val + a) & 0xFF
            elif ft == 2:
                out = (val + b) & 0xFF
            elif ft == 3:
                out = (val + ((a + b) >> 1)) & 0xFF
            elif ft == 4:
                out = (val + paeth(a, b, c)) & 0xFF
            else:
                raise ValueError(f"Invalid PNG filter {ft}")
            recon[row_start + x] = out

    rgba = bytearray(w * h * 4)
    for y in range(h):
        rs = y * stride
        for x in range(w):
            dst = (y * w + x) * 4
            if ct == 3:
                assert plte is not None
                if bd == 8:
                    idx = recon[rs + x]
                elif bd == 4:
                    idx = (recon[rs + (x >> 1)] >> (4 if (x & 1) == 0 else 0)) & 0xF
                elif bd == 2:
                    idx = (recon[rs + (x >> 2)] >> (6 - 2 * (x & 3))) & 0x3
                elif bd == 1:
                    idx = (recon[rs + (x >> 3)] >> (7 - (x & 7))) & 0x1
                else:
                    raise ValueError(f"Unsupported indexed bit depth {bd}")
                r, g, b = plte[idx]
                a = trns[idx] if (trns is not None and idx < len(trns)) else 255
            elif ct == 6 and bd == 8:
                i = rs + x * 4
                r, g, b, a = recon[i], recon[i + 1], recon[i + 2], recon[i + 3]
            elif ct == 2 and bd == 8:
                i = rs + x * 3
                r, g, b, a = recon[i], recon[i + 1], recon[i + 2], 255
            elif ct == 4 and bd == 8:
                i = rs + x * 2
                v, a = recon[i], recon[i + 1]
                r = g = b = v
            elif ct == 0 and bd == 8:
                v = recon[rs + x]
                r = g = b = v
                a = 255
            elif ct == 0 and bd == 1:
                v = 255 if ((recon[rs + (x >> 3)] >> (7 - (x & 7))) & 1) else 0
                r = g = b = v
                a = 255
            else:
                raise ValueError(f"Unsupported PNG format ct={ct} bd={bd}")
            rgba[dst : dst + 4] = bytes((r, g, b, a))
    return w, h, bytes(rgba)


def compute_ascii_widths(ascii_png: bytes, space_json: bytes) -> list[int]:
    """Compute Mojangles 256-entry advance width table directly from ascii.png + space.json."""
    w, h, px = decode_png_rgba(ascii_png)
    cw, ch = w // 16, h // 16
    space_data = json.loads(space_json.decode("utf-8"))
    space_adv = 4
    for prov in space_data.get("providers", []):
        if prov.get("type") == "space":
            adv_map = prov.get("advances", {})
            if " " in adv_map:
                space_adv = int(adv_map[" "])

    widths = [space_adv] * 256
    for code in range(256):
        if code == 32:
            widths[code] = space_adv
            continue
        col = code % 16
        row = code // 16
        max_x = -1
        for py in range(ch):
            for px_x in range(cw):
                idx = (row * ch + py) * w + (col * cw + px_x)
                if px[idx * 4 + 3] > 0 and px_x > max_x:
                    max_x = px_x
        if max_x >= 0:
            # Minecraft bitmap font adds 1px inter-glyph gap scaled to 8px cell height
            scale_x = cw // 8 if cw >= 8 else 1
            widths[code] = (max_x // scale_x) + 2
        else:
            widths[code] = space_adv
    return widths


def decode_ogg_to_wav(
    ffmpeg_bin: Path, src_ogg: Path, dst_wav: Path, force: bool = False
) -> tuple[int, int, int]:
    """Decode one .ogg object to 48kHz signed 16-bit PCM WAV via ffmpeg."""
    dst_wav.parent.mkdir(parents=True, exist_ok=True)
    if not force and dst_wav.is_file() and dst_wav.stat().st_size > 44:
        with wave.open(str(dst_wav), "rb") as wf:
            if wf.getframerate() == 48000 and wf.getsampwidth() == 2 and wf.getnframes() > 0:
                return wf.getframerate(), wf.getnchannels(), wf.getnframes()

    tmp_wav = dst_wav.with_suffix(".wav.tmp")
    cmd = [
        str(ffmpeg_bin),
        "-v",
        "error",
        "-y",
        "-i",
        str(src_ogg),
        "-ar",
        "48000",
        "-c:a",
        "pcm_s16le",
        "-bitexact",
        "-f",
        "wav",
        str(tmp_wav),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        if tmp_wav.exists():
            tmp_wav.unlink()
        raise RuntimeError(f"ffmpeg failed for {src_ogg} -> {dst_wav}: {res.stderr.strip()}")
    tmp_wav.replace(dst_wav)
    with wave.open(str(dst_wav), "rb") as wf:
        rate = wf.getframerate()
        ch = wf.getnchannels()
        frames = wf.getnframes()
        sw = wf.getsampwidth()
        if rate != 48000 or sw != 2 or frames <= 0 or ch not in (1, 2):
            raise RuntimeError(f"Unexpected WAV format in {dst_wav}: rate={rate} ch={ch} sw={sw} frames={frames}")
        return rate, ch, frames


def sync_tree(src_dir: Path, dst_dir: Path) -> int:
    """Mirror src_dir to dst_dir cleanly, removing stale files and verifying byte counts."""
    src_files = {p.relative_to(src_dir): p for p in src_dir.rglob("*") if p.is_file()}
    dst_dir.mkdir(parents=True, exist_ok=True)
    if dst_dir.exists():
        for existing in list(dst_dir.rglob("*")):
            if existing.is_file():
                rel = existing.relative_to(dst_dir)
                if rel not in src_files:
                    existing.unlink()
    copied = 0
    for rel, src in sorted(src_files.items()):
        dst = dst_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        src_bytes = src.read_bytes()
        if not dst.is_file() or dst.read_bytes() != src_bytes:
            dst.write_bytes(src_bytes)
        copied += 1
    return copied


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jar", type=Path, default=DEFAULT_JAR)
    parser.add_argument("--index", type=Path, default=DEFAULT_INDEX)
    parser.add_argument("--objects", type=Path, default=DEFAULT_OBJECTS)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--sync-app", type=Path, default=DEFAULT_SYNC_APP)
    parser.add_argument("--ffmpeg", type=Path, default=DEFAULT_FFMPEG)
    parser.add_argument("--force", action="store_true", help="Re-decode WAVs even if valid 48kHz PCM exists")
    parser.add_argument("--no-sync", action="store_true", help="Do not sync to .app bundle")
    args = parser.parse_args()

    if not args.jar.is_file():
        print(f"ERROR: Missing Minecraft jar: {args.jar}", file=sys.stderr)
        return 1
    if not args.index.is_file():
        print(f"ERROR: Missing asset index: {args.index}", file=sys.stderr)
        return 1
    if not args.objects.is_dir():
        print(f"ERROR: Missing objects dir: {args.objects}", file=sys.stderr)
        return 1
    if not args.ffmpeg.is_file():
        print(f"ERROR: Missing ffmpeg binary: {args.ffmpeg}", file=sys.stderr)
        return 1

    out_tex = args.out / "textures"
    out_snd = args.out / "sounds"
    args.out.mkdir(parents=True, exist_ok=True)

    tex_entries = []
    expected_tex_paths: set[Path] = set()
    sprite_mcmeta: dict[str, dict] = {}

    with zipfile.ZipFile(args.jar, "r") as zf:
        jar_names = set(zf.namelist())
        for rel_dst, jar_src in TEXTURE_MAP:
            if jar_src not in jar_names:
                raise FileNotFoundError(f"Missing inside jar: {jar_src}")
            png_bytes = zf.read(jar_src)
            if png_bytes[:8] != b"\x89PNG\r\n\x1a\n":
                raise ValueError(f"Corrupt PNG header in {jar_src}")
            w, h = struct.unpack(">II", png_bytes[16:24])
            dst_path = out_tex / rel_dst
            dst_path.parent.mkdir(parents=True, exist_ok=True)
            dst_path.write_bytes(png_bytes)
            expected_tex_paths.add(dst_path.resolve())
            tex_entries.append({
                "rel_path": f"textures/{rel_dst}",
                "jar_source": jar_src,
                "width": w,
                "height": h,
                "bytes": len(png_bytes),
                "sha256": hashlib.sha256(png_bytes).hexdigest(),
            })
            mcmeta_path = jar_src + ".mcmeta"
            if mcmeta_path in jar_names:
                sprite_mcmeta[f"textures/{rel_dst}"] = json.loads(zf.read(mcmeta_path).decode("utf-8"))

        # Font glyph advance widths
        ascii_bytes = zf.read("assets/minecraft/textures/font/ascii.png")
        space_bytes = zf.read("assets/minecraft/font/include/space.json")
        widths = compute_ascii_widths(ascii_bytes, space_bytes)
        widths_path = out_tex / "font" / "ascii_widths.bin"
        widths_path.parent.mkdir(parents=True, exist_ok=True)
        widths_path.write_bytes(bytes(widths))
        expected_tex_paths.add(widths_path.resolve())

        # Official English en_us translations (strictly 100% English)
        raw_lang = json.loads(zf.read("assets/minecraft/lang/en_us.json").decode("utf-8"))
        en_us: dict[str, str] = {}
        for k in EN_US_KEYS:
            if k not in raw_lang:
                raise KeyError(f"Missing en_us key in jar: {k}")
            val = str(raw_lang[k])
            for bad in FORBIDDEN_SPANISH_SUBSTRINGS:
                if bad.lower() in val.lower():
                    raise ValueError(f"Forbidden non-English text in {k}: {val}")
            en_us[k] = val
        (args.out / "en_us.json").write_text(json.dumps(en_us, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    # Clean stale texture files in out_tex
    for existing in list(out_tex.rglob("*")):
        if existing.is_file() and existing.resolve() not in expected_tex_paths:
            existing.unlink()

    # Decode sounds from 29.json + assets/objects/
    index_objs = json.loads(args.index.read_text(encoding="utf-8"))["objects"]
    sound_tasks: list[tuple[str, str, str, Path, Path]] = []
    expected_snd_paths: set[Path] = set()
    for rel_dst, asset_key in SOUND_MAP:
        if asset_key not in index_objs:
            raise KeyError(f"Missing sound key in 29.json: {asset_key}")
        h = index_objs[asset_key]["hash"]
        src_ogg = args.objects / h[:2] / h
        if not src_ogg.is_file():
            raise FileNotFoundError(f"Missing object blob for {asset_key}: {src_ogg}")
        dst_wav = out_snd / rel_dst
        expected_snd_paths.add(dst_wav.resolve())
        sound_tasks.append((rel_dst, asset_key, h, src_ogg, dst_wav))

    # Clean stale sound files
    if out_snd.exists():
        for existing in list(out_snd.rglob("*")):
            if existing.is_file() and existing.resolve() not in expected_snd_paths:
                existing.unlink()

    sound_entries: list[dict] = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        fut_map = {
            pool.submit(decode_ogg_to_wav, args.ffmpeg, src_ogg, dst_wav, args.force): (
                rel_dst,
                asset_key,
                h,
                dst_wav,
            )
            for (rel_dst, asset_key, h, src_ogg, dst_wav) in sound_tasks
        }
        for fut in as_completed(fut_map):
            rel_dst, asset_key, h, dst_wav = fut_map[fut]
            rate, ch, frames = fut.result()
            sound_entries.append({
                "rel_path": f"sounds/{rel_dst}",
                "index_key": asset_key,
                "object_sha1": h,
                "sample_rate": rate,
                "channels": ch,
                "frames": frames,
                "bytes": dst_wav.stat().st_size,
            })
    sound_entries.sort(key=lambda e: e["rel_path"])

    manifest = {
        "source_jar": str(args.jar),
        "source_index": str(args.index),
        "language": "en_us",
        "texture_count": len(tex_entries),
        "sound_count": len(sound_entries),
        "ascii_widths": widths,
        "sprite_mcmeta": sprite_mcmeta,
        "textures": tex_entries,
        "sounds": sound_entries,
    }
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    synced_count = 0
    if not args.no_sync and args.sync_app:
        synced_count = sync_tree(args.out, args.sync_app)

    print(
        f"Extracted {len(tex_entries)} PNG textures, 1 ascii_widths.bin, "
        f"{len(en_us)} en_us strings, and {len(sound_entries)} 48kHz PCM WAV sounds "
        f"into {args.out}"
        + (f" (synced {synced_count} files to {args.sync_app})" if not args.no_sync else "")
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
