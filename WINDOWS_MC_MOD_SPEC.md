# Windows Port Specification: Minecraft-in-Hyrule Mod for `totk-pc-recomp` (v0.4.1+)

This specification instructs Antigravity on Windows to build a **100% functionally identical Windows version** of the macOS `totk-native` Minecraft-in-Hyrule mod inside **`valentderah/totk-pc-recomp`** (`v0.4.1`+, native x86-64 Windows + Vulkan renderer).

Nothing from the macOS runtime needs to be reused directly—implement everything natively for `totk-pc-recomp`'s Windows C/C++, Win32/SDL, audio, and Vulkan architecture while matching every feature, visual pass, input behavior, audio cue, and gameplay bridge described below.

---

## 1. High-Level Architecture

The mod embeds a full Minecraft voxel/gameplay/HUD/audio engine directly into the TOTK PC recompilation runtime, running alongside Hyrule in real time:

1. **Asset Extractor (`tools/extract_mc_assets.py`)**: Extracts authentic Minecraft Java `1.21.11` textures, font glyph widths, `en_us.json` strings, and 48 kHz PCM WAV sounds into `assets/mc/`.
2. **Core Simulation (`mc_engine`)**: Owns world blocks, mobs, primed TNT, dropped items, projectiles, particles, player inventory/crafting/smelting, HUD/UI layout, and emits a lock-free/mutex-copied POD `McRenderSnapshot` each frame.
3. **TOTK Runtime Bridge (`mc_totk_bridge`)**: Reads/writes TOTK v1.2.1 guest state (player pose, HP, stamina, camera matrix, terrain raycasts, wall-sweep collision, active actors/enemies, incoming damage) safely on the **guest frame thread**.
4. **Vulkan Renderer (`mc_vulkan_render`)**: Replaces the macOS Metal 4 renderer (`mc_metal_render.m`) with Vulkan passes integrated into `totk-pc-recomp`'s Vulkan framegraph/swapchain:
   - Pass (a): 3D Minecraft world depth-tested against Hyrule's G-buffer depth.
   - Pass (b): 1st-person viewmodel rendered with a private `mc_depth` buffer.
   - Pass (c-pre): Offscreen 3D inventory paperdoll render.
   - Pass (c): Crisp nearest-neighbor 2D HUD, inventory, tooltips, and text.
5. **Audio Mixer (`mc_audio`)**: Mixes 48 kHz stereo float PCM sound effects into `totk-pc-recomp`'s audio output stream.
6. **Input & Overlay Controller (`minecraft_overlay`)**: Handles keyboard, mouse capture, mouse-look camera bridge, scroll wheel, and debug/chat commands.

---

## 2. Asset Pipeline (Windows)

Port or re-implement `tools/extract_mc_assets.py` for Windows:
- **Input**:
  - Minecraft JAR: `%APPDATA%\.minecraft\versions\1.21.11\1.21.11.jar` (configurable via `--jar` or `MINECRAFT_JAR`).
  - Minecraft audio objects: `%APPDATA%\.minecraft\assets\objects` + asset index JSON.
  - Use `ffmpeg.exe` on `PATH` to decode `.ogg` sounds to 48 kHz PCM `.wav`.
- **Output (`assets/mc/`)**:
  - `textures/` (all block, item, entity/mob, particle, and GUI/HUD/creative/effect sprites; zero procedural/synthetic assets)
  - `sounds/` (48 kHz PCM WAVs)
  - `en_us.json` (English item/block/UI strings only)
  - `manifest.json` (index & metadata loaded by `mc_assets`)
- **Runtime Loader (`mc_assets`)**: Loads `assets/mc/manifest.json`, packs block/item/GUI textures into GPU-ready RGBA8 atlases, and loads WAV samples into memory.

---

## 3. Core Simulation & Snapshot Contract (`mc_engine` & `mc_snapshot`)

Implement a thread-safe, 100% POD `McRenderSnapshot` (no pointers; `memcpy` under mutex via `mc_snapshot_copy()`) shared between simulation and the Vulkan render thread:

- **Capacities**:
  - `MC_SNAP_MAX_BLOCKS = 1024`
  - `MC_SNAP_MAX_MOBS = 48`
  - `MC_SNAP_MAX_PRIMED_TNT = 32`
  - `MC_SNAP_MAX_DROPPED = 64`
  - `MC_SNAP_MAX_PROJECTILES = 64`
  - `MC_SNAP_MAX_PARTICLES = 512`
  - `MC_SNAP_MAX_EFFECTS = 8`
  - `MC_SNAP_MAX_GUI_QUADS = 1024`
  - `MC_SNAP_MAX_GUI_TEXTS = 160`
- **Coordinate System**:
  - World space matches TOTK Hyrule meters (`+Y` up).
  - Yaw `0` faces `+Z`: `fwd = (sin(yaw), 0, cos(yaw))`, `right = (cos(yaw), 0, -sin(yaw))`.
- **Modes & Views**:
  - Toggle between **Minecraft Mode** (`mode = 1`) and **Zelda Mode** (`mode = 0`).
  - Camera views: `MC_VIEW_FIRST_PERSON`, `MC_VIEW_THIRD_BACK`, `MC_VIEW_THIRD_FRONT`.
- **Blocks (`McBlockType`)**:
  - `AIR`, `TNT` (ignitable with fuse + white flash + explosion), `OAK_LOG`, `OAK_PLANKS`, `GRASS`, `DIRT`, `STONE`, `COBBLESTONE`, `OBSIDIAN`, `BEDROCK`, `TORCH` (emits light), `DIAMOND_BLOCK`, `GOLD_BLOCK`, `GLASS`, `GLOWSTONE`, `CRAFTING_TABLE` (opens 3x3 crafting), `FURNACE` (smelting with fuel + `cook_left`), `WATER`, `LAVA`, `RED_WOOL`, `BED_FOOT`, `BED_HEAD`, `SAND`, `GLASS_PANE`.
- **Items (`McItemType`)**:
  - Weapons/Tools: `DIAMOND_SWORD`, `IRON_SWORD`, `NETHERITE_SWORD`, `DIAMOND_AXE`, `DIAMOND_PICKAXE`, `BOW`, `CROSSBOW`, `ARROW`, `SHIELD`.
  - Armor/Equipment: `ELYTRA`, `DIAMOND_HELMET`, `DIAMOND_CHESTPLATE`, `DIAMOND_LEGGINGS`, `DIAMOND_BOOTS`, `TOTEM_OF_UNDYING`.
  - Consumables & Utility: `COOKED_BEEF`, `GOLDEN_APPLE`, `ENCHANTED_GOLDEN_APPLE`, `APPLE`, `BREAD`, `FIREWORK_ROCKET`, `FLINT_AND_STEEL`, `BONE`, `ENDER_PEARL`, `WATER_BUCKET`, `LAVA_BUCKET`, `EXP_BOTTLE`.
  - Materials & Placeables: `DIAMOND`, `EMERALD`, `IRON_INGOT`, `GOLD_INGOT`, `REDSTONE`, `COAL`, `CHARCOAL`, `STICK`, `GUNPOWDER`, `ROTTEN_FLESH`, `SPIDER_EYE`, `STRING`, `SLIME_BALL`, `BARRIER`, plus placeable forms of all blocks.
- **Entities**:
  - Mobs (`McMob`: AI timer, walk phase/amp, swing arm, creeper/swell state, hurt flash/timer, attack cooldown, strafe, step sound accumulator).
  - Primed TNT (`McPrimedTnt`), Dropped Items (`McDroppedItem` with spin/bob/pickup delay/enchant glint), Projectiles (`McProjectile`: arrows, fireworks, ender pearls, XP bottles), Particles (`McParticle`).

---

## 4. TOTK Runtime Bridge (`mc_totk_bridge`)

Hook into `totk-pc-recomp`'s recompiled guest memory and game loop.

**Strict Thread-Safety Contract**:
- All guest memory reads/writes, raycasts, actor scans, and visibility toggles MUST run **only on the guest frame thread** inside `runtime_mc_game_tick(frame)`.
- Never call guest memory functions from the audio thread or Vulkan render thread.
- Only `mc_totk_set_camera_look()`, `mc_totk_clear_camera_look()`, and `mc_totk_sample_camera_stick()` may be called cross-thread (via atomics/mutex).

**Required Bridge Capabilities**:
1. **Gameplay State Gate (`mc_totk_is_gameplay_active`, `mc_totk_options_open`)**:
   - Return `1` **only** during active, interactive Hyrule gameplay; return `0` on Title screen, Save/Loading, Scene Transitions/Fades, Cutscenes/Events, Game Over, or TOTK Fullscreen Menus.
2. **Player State Sync (`McTotkPlayerState`)**:
   - Read Link's world XYZ (`pos`), 3x3 rotation (`rot`), forward vector (`fwd`), `yaw`, linear velocity (`vel`), vertical velocity (`vert_vel`), `grounded`, `falling`, `gliding_paraglider`, `blocking`, `attacking`.
   - Read/write HP in **quarter-hearts** (`hp_quarters`, `max_hp_quarters`, `extra_hp_quarters`, `gloom_quarters`; 4 = 1 Zelda heart).
   - Read/add stamina (`1000.0f = 1 wheel`, `stamina_exhausted`).
   - Write player position/yaw/velocity (`mc_totk_set_player_pos`, `mc_totk_set_player_yaw`, `mc_totk_set_player_vel`).
3. **Visibility Toggles**:
   - `mc_totk_set_player_visible(int visible)`: Hide Link's 3D body in Minecraft first-person/Minecraft mode and restore saved visibility on exit/cleanup.
   - `mc_totk_set_hud_visible(int visible)`: Hide Zelda's native HUD when Minecraft HUD is active and restore on exit/cleanup.
4. **Hyrule Physics & Collision**:
   - `mc_totk_raycast` & `mc_totk_raycast_from_eye`: Raycast against Hyrule terrain/physics (`phive::RayCast` equivalent) for block placement, mining, arrows, and camera clearance.
   - `mc_totk_ground_height` / `mc_totk_ground_height_with_normal`: Ground Y + surface normal for mobs, dropped items, and explosions.
   - `mc_totk_check_wall_step`: Cylinder sweep returning bitmask (`bit 0`: hit/slid against wall with `out_wall_norm_xz`; `bit 1`: walkable step-up ledge `<= 1.15m`).
5. **Actor & Combat Bridge**:
   - Enumerate active TOTK actors (`McTotkActorInfo`: `ENEMY`, `NPC`, `ITEM`, `OTHER`) with world AABB, radius, and HP.
   - Apply Minecraft melee/projectile/TNT damage to TOTK enemies.
   - Capture incoming damage to Link (`McTotkIncomingDamage`: `raw_damage_quarters`, attacker actor/name/pos) so Minecraft armor, directional shield blocking, Totem of Undying, and `MC_SFX_HURT` process it consistently.
6. **Camera Bridge**:
   - Extract TOTK's active View-Projection matrix (`cam_vp[16]`) each frame and pass to `runtime_mc_note_camera()`.
   - Bridge mouse-look deltas (`mc_totk_set_camera_look(dx, dy)`) into TOTK's right-stick camera input (`mc_totk_sample_camera_stick`).

---

## 5. Vulkan Renderer (`mc_vulkan_render`)

Implement in `totk-pc-recomp`'s Vulkan backend, matching all 3 Metal render passes:

1. **Matrix & Depth Convention**:
   - `cam_vp[16]` is a row-major OpenGL-style View-Projection matrix (`clip_gl = M * float4(world_pos, 1.0)`).
   - In Vulkan vertex shaders, apply Vulkan NDC Y-flip (`c.y = -c.y` if needed for the target's viewport convention) and `[0, 1]` depth remap (`out.z = 0.5 * c.z + 0.5 * c.w`, matching Metal's `[0, 1]` NDC depth).
2. **Pass (a) — 3D Minecraft World**:
   - Render onto Hyrule's color image (`VK_ATTACHMENT_LOAD_OP_LOAD`) after Hyrule's 3D scene pass.
   - Sample Hyrule's G-buffer depth (`gbuf_depth`) in the fragment shader to depth-test Minecraft blocks, mobs, TNT, dropped items, projectiles, and particles against Hyrule terrain/structures.
   - Support alpha-tested cutouts (torches, glass, foliage) and translucent water/glass.
3. **Pass (b) — 1st-Person Viewmodel**:
   - Use a private `mc_depth` attachment cleared to `1.0` before drawing the 1st-person arm/held item/shield/bow so it never clips into nearby Hyrule walls or placed blocks.
4. **Pass (c-pre) — 3D Inventory Paperdoll**:
   - Before the final UI render pass, if the inventory is open, render the 3D player model (with equipped armor/held item, following mouse cursor look) to an offscreen `VkImage` sampled in Pass (c).
5. **Pass (c) — 2D HUD, Inventory & Text**:
   - Render with **nearest-neighbor (`VK_FILTER_NEAREST`)** sampling at native resolution onto the swapchain/present target:
     - Hotbar, selection box, offhand slot (left/right)
     - Crosshair + attack cooldown indicator (`BG`, `PROGRESS`, `FULL`)
     - XP bar + level number
     - Hearts (container, full, half, blink, absorption), hunger/food, armor, active status effect icons
     - Survival inventory, 3x3 Crafting Table UI, Furnace UI, Creative tabbed inventory with scrollbar, item tooltips, and recipe book button
     - Crisp bitmap/atlas text rendering (`MC_SNAP_MAX_GUI_TEXTS`)

---

## 6. Audio System (`mc_audio`)

Hook `runtime_mc_audio_mix(float *stereo_io, int frames, int rate)` into `totk-pc-recomp`'s Windows audio output (WASAPI / SDL audio callback):
- Support concurrent voice mixing at 48 kHz stereo float with pitch/gain variation.
- Implement all `MC_SFX_*` categories:
  - Digging & footsteps per material (`WOOD`, `STONE`, `GRASS`, `GRAVEL`, `SAND`)
  - Bow shoot / hit / arrow ding
  - Melee (`ATTACK_STRONG`, `ATTACK_SWEEP`, `ATTACK_CRIT`, `ATTACK_WEAK`, `ATTACK_KNOCKBACK`, `SHIELD_BLOCK`, `SHIELD_BREAK`)
  - Consumables & UI (`EAT`, `BURP`, `DRINK`, `XP_ORB`, `LEVELUP`, `UI_CLICK`)
  - Explosives & special (`TNT_FUSE`, `EXPLODE`, `FIRE_IGNITE`, `FIREWORK_LAUNCH`, `FIREWORK_BLAST`, `FIREWORK_LARGE_BLAST`, `FIREWORK_TWINKLE`, `TOTEM_USE`)
  - Player damage (`HURT`, `FALL_SMALL`, `FALL_BIG`)

---

## 7. Input, Windowing & Settings (Win32 / SDL2)

Wire `totk-pc-recomp`'s Windows input loop to `minecraft_overlay`:
- `runtime_mc_wants_mouse_capture()`: Lock/hide cursor (`ClipCursor` / `SDL_SetRelativeMouseMode`) during active first-person/gameplay when inventory/menus are closed.
- Forward keyboard (`runtime_mc_key(keycode, down)`), raw mouse deltas (`runtime_mc_mouse_move(dx, dy)`), cursor normalized coords (`runtime_mc_cursor_pos(nx, ny)`), mouse buttons (`runtime_mc_mouse_button(btn, down)`), and scroll wheel (`runtime_mc_scroll(delta_y)`).
- Support `runtime_mc_apply_settings(distance, sensitivity, fov, random_mob_spawns, gui_scale, mainhand_scale, offhand_scale)`.

---

## 8. Verification Checklist for Windows Antigravity

Before declaring the Windows port complete, verify all of the following in `totk-pc-recomp`:
1. **Build**: Compiles cleanly on Windows (MSVC/Clang) with zero warnings/errors in the new `mc_*` and Vulkan overlay files.
2. **Assets**: `extract_mc_assets.py` extracts `assets/mc/` from Windows `%APPDATA%\.minecraft\` with zero missing textures or sounds.
3. **3D Occlusion**: Placed blocks, mobs, and particles occlude properly behind Hyrule hills/walls via `gbuf_depth`.
4. **Viewmodel**: 1st-person hand/sword/blocks render cleanly with `mc_depth` without clipping into walls.
5. **HUD & Inventory**: Hotbar, hearts, hunger, armor, XP, crosshair, crafting/furnace/creative screens, tooltips, and 3D paperdoll render crisp with nearest-neighbor filtering.
6. **Gameplay Bridge**: Link's position, HP, stamina, raycasts, wall-sliding, enemy damage, shield blocking, and mouse-look work identically to macOS during active Hyrule gameplay, and cleanly pause/hide on Title, Cutscenes, and Zelda Menus.
7. **Audio**: Block break/step, combat, eating, TNT, fireworks, and hurt SFX mix cleanly at 48 kHz without glitches or thread-safety crashes.
