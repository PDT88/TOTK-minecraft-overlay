# TOTK Minecraft Overlay (`TOTK-minecraft-overlay`)

A **Minecraft-in-Hyrule** runtime overlay mod embedded directly into a native macOS ARM64 port of *The Legend of Zelda: Tears of the Kingdom* (`totk-native`).

Rather than replacing Link's model with a static skin, this mod embeds a live Minecraft voxel, entity, inventory, HUD, and audio engine inside the running TOTK process—letting you build, fight, craft, and explore Hyrule with Minecraft mechanics synchronized in real time with Zelda's world, physics, and combat.

> **Note on Copyrighted Assets:** This repository contains **only** original source code, build scripts, and documentation. No Nintendo game files, Switch keys, or Mojang/Minecraft assets are included. You must supply your own legally obtained copies of *Tears of the Kingdom* and *Minecraft: Java Edition* (`1.21` or newer, including `1.21.x` and `26.x+`) to extract assets and build/run locally.

---

## Features

- **Seamless Mode & Camera Switching**: Toggle between **Minecraft Mode** and **Zelda Mode** on the fly, with first-person, third-person back, and third-person front views, mouse-look camera capture, and automatic hiding/restoring of Link's 3D body and Zelda's native HUD.
- **Depth-Tested 3D World in Hyrule**: Place and mine blocks (wood, stone, obsidian, glass, torches, water, lava, crafting tables, furnaces, beds, TNT, and more) directly in Hyrule, depth-tested against Hyrule's G-buffer depth and terrain raycasts so blocks, mobs, dropped items, projectiles, and particles occlude naturally behind hills and walls.
- **First-Person Viewmodel**: Dedicated depth-buffered 1st-person arm, held items, tools, weapons, bows, and shields that never clip into nearby Hyrule geometry.
- **Live Combat, Mobs & Physics Bridge**:
  - Syncs Link's position, velocity, HP (quarter-hearts), and stamina with TOTK's guest state.
  - Attacks from Minecraft swords, axes, bows, arrows, and primed TNT damage active TOTK enemies; incoming damage to Link flows through Minecraft armor, directional shield blocking, and Totem of Undying.
  - Spawns Minecraft mobs with AI, animations, and Hyrule terrain/wall-sweep collision.
- **Full Minecraft HUD & Menus**: Crisp nearest-neighbor 2D HUD (hotbar, hearts, hunger, armor, XP bar, crosshair + attack cooldown indicator, status effects), Survival inventory with live 3D paperdoll preview, 3x3 Crafting Table, Furnace smelting, Creative tabbed inventory, tooltips, and chat/debug commands.
- **Authentic Assets & 48 kHz Audio**: Extracts textures, fonts, `en_us.json`, and 48 kHz PCM WAVs directly from any local Minecraft: Java Edition `1.21+` installation (including `1.21.x` and `26.x+` releases) and mixes footsteps, digging, combat, eating, TNT, fireworks, XP, and hurt SFX into the runtime audio stream.

---

## Architecture

The overlay runs a self-contained Minecraft voxel, entity, inventory, HUD, and audio engine directly inside the TOTK runtime, synchronized with Hyrule's world, physics, camera, and combat systems:

| Component | Files | Purpose |
| :--- | :--- | :--- |
| **Overlay Controller** | [`runtime/minecraft_overlay.h`](runtime/minecraft_overlay.h), [`runtime/minecraft_overlay.c`](runtime/minecraft_overlay.c) | Entrypoints for lifecycle, input/mouse capture, chat/debug commands, and frame coordination. |
| **Core Simulation** | [`runtime/mc_engine.h`](runtime/mc_engine.h), [`runtime/mc_engine.c`](runtime/mc_engine.c), [`runtime/mc_snapshot.h`](runtime/mc_snapshot.h) | Voxel blocks, mobs, primed TNT, dropped items, projectiles, particles, crafting/smelting, HUD layout, and thread-safe POD `McRenderSnapshot` emission. |
| **TOTK Runtime Bridge** | [`runtime/mc_totk_bridge.h`](runtime/mc_totk_bridge.h), [`runtime/mc_totk_bridge.c`](runtime/mc_totk_bridge.c) | Reads/writes Link's pose, HP, stamina, camera matrix, terrain raycasts, wall-sweep collision, active actors/enemies, and incoming damage on the guest frame thread. |
| **Metal Renderer** | [`runtime/mc_metal_render.h`](runtime/mc_metal_render.h), [`runtime/mc_metal_render.m`](runtime/mc_metal_render.m) | Multi-pass Metal rendering: 3D world depth-tested against Hyrule's G-buffer, 1st-person viewmodel, offscreen 3D inventory paperdoll, and nearest-neighbor 2D HUD/text. |
| **Asset Loader & Extractor** | [`runtime/mc_assets.h`](runtime/mc_assets.h), [`runtime/mc_assets.c`](runtime/mc_assets.c), [`tools/extract_mc_assets.py`](tools/extract_mc_assets.py) | Auto-discovers installed Minecraft Java `1.21+` versions and extracts textures, glyph widths, `en_us.json`, and 48 kHz WAVs into `assets/mc/` (gitignored). |
| **Audio Mixer** | [`runtime/mc_audio.h`](runtime/mc_audio.h), [`runtime/mc_audio.c`](runtime/mc_audio.c), [`runtime/mc_link_audio_ids.inc`](runtime/mc_link_audio_ids.inc) | Mixes 48 kHz stereo float PCM Minecraft sound effects into the runtime audio stream. |

---

## Setup & Usage (macOS)

1. **Install Homebrew & Python dependencies:**
   ```bash
   brew install opus libvpx ffmpeg
   make dependencies
   ```
   `opus` and `libvpx` are linked by [`Makefile`](Makefile) from `/opt/homebrew/opt/{opus,libvpx}`, and `ffmpeg` is used to decode Minecraft `.ogg` audio to 48 kHz `.wav`.

2. **Extract Minecraft assets (local, gitignored):**
   ```bash
   .venv/bin/python tools/extract_mc_assets.py
   ```
   This auto-discovers your newest installed Minecraft: Java Edition `1.21+` version (including `1.21.x` and `26.x+`, preferring stable releases over snapshots) and populates `assets/mc/`. You can also set `MINECRAFT_DIR` or pass `--jar` / `--index` to target a specific version or directory.

3. **Provide TOTK v1.2.1 base files (local, gitignored):**
   The TOTK bridge (`runtime/mc_totk_bridge.c`), camera hooks, and Minecraft Settings UI require **TOTK v1.2.1** (`TOTK_GAME_V121=1`). Place your legally dumped TOTK v1.2.1 image/update and `prod.keys` as configured in [`Makefile`](Makefile), then prepare the v1.2.1 extracted/rewritten runtime inputs.

4. **Build the native v1.2.1 runtime:**
   ```bash
   make runtime-v121
   ```
   *(Or `make build/totk-native-v121` for just the binary. Note that `make build/totk-native` targets v1.0.0, where the Minecraft overlay bridge stubs out and will not activate.)*

---

## Repository Hygiene

The [`.gitignore`](.gitignore) file explicitly excludes all copyrighted, extracted, or generated artifacts:
- `assets/mc/` (extracted Minecraft textures, sounds, strings, and manifest)
- `extracted/`, `rewritten/`, `build/`, `user-data/`, `*.keys`, `analysis/`, `tooling/`, `.venv/`

---

## Upstream & References

- **Base TOTK Native Runtime (`MacNativeGames`)**: https://github.com/ly05010419/MacNativeGames
