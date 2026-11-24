# TOTK Minecraft Overlay (`TOTK-minecraft-overlay`)

A **Minecraft-in-Hyrule** runtime overlay mod embedded directly into *The Legend of Zelda: Tears of the Kingdom* (`totk-native` on macOS ARM64 / Metal, and `totk-pc-recomp` on Windows x86-64 / Vulkan).

| Platform | Branch | Renderer / Runtime |
| :--- | :--- | :--- |
| **macOS (Apple Silicon)** | [`main`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/main) | Metal / `totk-native` |
| **Windows (x86-64)** | [`windows`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/windows) | Vulkan / `totk-pc-recomp` |

Rather than replacing Link's model with a static skin, this mod embeds a live Minecraft voxel, entity, inventory, HUD, and audio engine inside the running TOTK process—letting you build, fight, craft, and explore Hyrule with Minecraft mechanics synchronized in real time with Zelda's world, physics, and combat.

> **Note on Copyrighted Assets:** This repository contains **only** original source code, build scripts, and documentation. No Nintendo game files or Mojang/Minecraft assets are included. You must already have your own legally obtained copies of *Tears of the Kingdom* (**v1.2.1**) running natively and *Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`) installed locally.

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

## Requirements

- **Apple Silicon Mac** running macOS with Xcode Command Line Tools (`xcode-select --install`) and Homebrew.
- ***The Legend of Zelda: Tears of the Kingdom* (v1.2.1)**: your own legally dumped base game (`.xci` or `.nsp`), **v1.2.1 update** (`.nsp`), and `prod.keys` *(plus unpacked Switch firmware NCAs if your base image is `.nsp` rather than `.xci`)*.
- ***Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`)** installed locally.

---

## Setup & Usage (macOS)

The TOTK bridge (`runtime/mc_totk_bridge.c`), camera hooks, and Minecraft Settings UI require **TOTK v1.2.1** (`TOTK_GAME_V121=1`).

1. **Clone the repository and install Homebrew dependencies:**
   ```bash
   git clone https://github.com/PDT88/TOTK-minecraft-overlay.git
   cd TOTK-minecraft-overlay
   brew install opus libvpx ffmpeg llvm
   ```
   - `opus` and `libvpx` are linked by [`Makefile`](Makefile) from `/opt/homebrew/opt/{opus,libvpx}`.
   - `ffmpeg` decodes Minecraft `.ogg` audio to 48 kHz `.wav`.
   - `llvm` is used during NSO rewrite verification.

2. **Check prerequisites and run the automated setup:**
   ```bash
   python3 scripts/setup_v121.py --check-only
   make setup-v121 \
     BASE_ROM=/path/to/totk_base.xci \
     UPDATE_NSP=/path/to/totk_update_1.2.1.nsp \
     KEYS=/path/to/prod.keys
   ```
   *(If `BASE_ROM` is an `.nsp` instead of `.xci`, also pass `FIRMWARE=/path/to/firmware_ncas` for Switch shared fonts. `KEYS` defaults to `~/Library/Application Support/Ryujinx/system/prod.keys` if omitted. You can also pass `MC_DIR=...` or `MC_JAR=...` to override the auto-discovered Minecraft `1.21+` / `26.x+` installation. Once extracted/rewritten, bare `make setup-v121` skips completed stages and rebuilds.)*

   [`scripts/setup_v121.py`](scripts/setup_v121.py) automatically creates `.venv`, clones/builds `hactool` if missing, extracts TOTK v1.2.1 ExeFS/RomFS/shared fonts, rewrites & verifies NSOs, auto-discovers your newest Minecraft: Java Edition `1.21+` / `26.x+` installation to populate `assets/mc/`, and builds `build/totk-native-v121` (`make runtime-v121`). *(Run `python3 scripts/setup_v121.py --help` for all flags.)*

3. **Launch:**
   ```bash
   make run-v121
   ```
   Saves and `minecraft-base.v1` are stored in `user-data/` by default (override with `PROFILE_DIR=/path/to/profile`).

---

## Controls

| Key / Input | Action |
| :--- | :--- |
| `Tab` or `F6` | Toggle **Minecraft Mode** / **Zelda Mode** |
| `F5` or `V` | Cycle camera view (1st-person → 3rd-person back → 3rd-person front) |
| `E` / `Esc` | Open / close Creative & Survival Inventory |
| `1`–`9` or Scroll | Select hotbar slot |
| `Left Click` | Attack / hold to mine block (sword or flint ignites TNT) |
| `Right Click` | Place block / use item / eat / draw bow / open Crafting Table or Furnace / sleep in bed / raise Shield |
| `F` | Swap mainhand and offhand |
| `G` | Quick-toggle Elytra + Totem of Undying vs. Diamond Chestplate + Shield |
| `Space` | Jump (press again in midair with Elytra to glide; right-click Firework Rocket to boost) |
| `Q` (`Shift+Q`) | Drop 1 item (or full stack) |
| `N` | Spawn a Minecraft mob nearby |
| `Cmd + ,` | Open **Minecraft Settings** (FOV, 3rd-person distance, sensitivity, mob spawns, GUI/hand scale, shaders) |

---

## Repository Hygiene

The [`.gitignore`](.gitignore) file explicitly excludes all copyrighted, extracted, or generated artifacts:
- `assets/mc/` (extracted Minecraft textures, sounds, strings, and manifest)
- `extracted/`, `rewritten/`, `build/`, `user-data/`, `analysis/`, `tooling/`, `.venv/`

---

## Upstream & References

- **Base TOTK Native Runtime (`MacNativeGames`)**: https://github.com/ly05010419/MacNativeGames
