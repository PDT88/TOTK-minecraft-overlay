# TOTK Minecraft Overlay (`TOTK-minecraft-overlay`)

A **Minecraft-in-Hyrule** runtime overlay mod embedded directly into *The Legend of Zelda: Tears of the Kingdom* (`totk-native` on macOS ARM64 / Metal, and `totk-pc-recomp` on Windows x86-64 / Vulkan).

| Platform | Branch | Renderer / Runtime |
| :--- | :--- | :--- |
| **macOS (Apple Silicon)** | [`main`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/main) | Metal / `totk-native` |
| **Windows (x86-64)** | [`windows`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/windows) | Vulkan / `totk-pc-recomp` |

Rather than replacing Link's model with a static skin, this mod embeds a live Minecraft voxel, entity, inventory, HUD, and audio engine inside the running TOTK process—letting you build, fight, craft, and explore Hyrule with Minecraft mechanics synchronized in real time with Zelda's world, physics, and combat.

> **Note on Copyrighted Assets:** This repository contains **only** original source code, build scripts, and documentation. No Nintendo game files or Mojang/Minecraft assets are included. You must supply your own legally obtained copies of *Tears of the Kingdom* (base + **v1.2.1** update + `prod.keys`) and *Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`) to set up and run locally.

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

- **Apple Silicon Mac** (`arm64`) running **macOS 26 (Tahoe)** / **Metal 4**.
- **Xcode 26 Command Line Tools** (`xcode-select --install`) and **Homebrew**.
- **Python 3.14+** (`python@3.14` via Homebrew, providing `compression.zstd` for building v1.2.1 RomFS overlays), plus Homebrew packages `opus`, `libvpx`, `ffmpeg`, `llvm`, and `spirv-cross` *(installed automatically by Option A or `make install-deps`)*.
- ***The Legend of Zelda: Tears of the Kingdom* (v1.2.1)**: your own legally dumped base game (`.xci`, `.nsp`, `.xcz`, or `.nsz`), **v1.2.1 update** (`.nsp` or `.nsz`), and `prod.keys` *(plus Switch firmware NCAs as an unpacked directory or `.zip` archive if your base image is `.nsp` or `.nsz` rather than `.xci`/`.xcz`)*.
- ***Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`)** installed locally.

---

## Setup & Usage (macOS)

The TOTK bridge (`runtime/mc_totk_bridge.c`), camera hooks, and Minecraft Settings UI require **TOTK v1.2.1** (`TOTK_GAME_V121=1`). *(For Windows x86-64 / Vulkan builds and the `TotK-Setup` GUI installer, see the [`windows`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/windows) branch.)*

First, clone the repository:
```bash
git clone https://github.com/PDT88/TOTK-minecraft-overlay.git
cd TOTK-minecraft-overlay
```

### Option A: Double-click Guided Setup (Finder)

1. In Finder, double-click [`Setup TOTK Minecraft Overlay.command`](Setup%20TOTK%20Minecraft%20Overlay.command) *(if macOS Gatekeeper warns on first launch, Right-click → **Open**)*.
2. The script verifies Apple Silicon (`arm64`), macOS 26+, Xcode Command Line Tools, and Homebrew, then installs any missing Homebrew dependencies (`python3 scripts/setup_v121.py --install-deps`).
3. Pick your TOTK base dump (`.xci`, `.nsp`, `.xcz`, or `.nsz`), **v1.2.1** update (`.nsp` or `.nsz`), `prod.keys` (auto-detected from `~/Library/Application Support/Ryujinx/system/prod.keys` when present), optional Switch firmware (`.zip` or unpacked NCA folder, required when base is `.nsp`/`.nsz`), and optional Minecraft `1.21+` client JAR (or auto-detect `~/Library/Application Support/minecraft`) via native Finder dialogs.
4. Optionally enable startup shader seed pre-compilation (`--prepare-shaders`) and standalone `.app` bundle creation (`--build-app`, enabled by default).
5. When setup completes, Finder reveals `build/TOTK Minecraft Overlay.app` — double-click it to play. *(Launching the `.app` stores saves and shader caches under `~/Library/Application Support/MacNativeGames/TOTK/`.)*

### Option B: Terminal / Makefile

1. **Install Homebrew dependencies and check prerequisites:**
   ```bash
   make install-deps
   python3 scripts/setup_v121.py --check-only
   ```
   - `python@3.14` provides `compression.zstd` for building v1.2.1 RomFS overlays.
   - `opus` and `libvpx` are linked by [`Makefile`](Makefile) from `/opt/homebrew/opt/{opus,libvpx}`.
   - `ffmpeg` decodes Minecraft `.ogg` audio to 48 kHz `.wav`.
   - `llvm` is used during NSO rewrite verification.
   - `spirv-cross` is used by the runtime shader translation service.

2. **Run the automated setup:**
   ```bash
   make setup-v121 \
     BASE_ROM=/path/to/totk_base.xci \
     UPDATE_NSP=/path/to/totk_update_1.2.1.nsp \
     KEYS=/path/to/prod.keys
   ```
   To also pre-compile the startup shader seed (`build/shader-seed`) and package the standalone `.app` bundle in one pass, add `PREPARE_SHADERS=1` and `BUILD_APP=1`:
   ```bash
   make setup-v121 \
     BASE_ROM=/path/to/totk_base.xcz \
     UPDATE_NSP=/path/to/totk_update_1.2.1.nsz \
     KEYS=/path/to/prod.keys \
     PREPARE_SHADERS=1 \
     BUILD_APP=1
   ```
   *(Both `BASE_ROM` and `UPDATE_NSP` accept compressed `.xcz` / `.nsz` dumps in addition to `.xci` / `.nsp`. If `BASE_ROM` is `.nsp` or `.nsz`, also pass `FIRMWARE=/path/to/firmware_ncas_or_zip` for Switch shared fonts. `KEYS` defaults to `~/Library/Application Support/Ryujinx/system/prod.keys` if omitted. Optional variables: `INSTALL_DEPS=1`, `PREPARE_SHADERS=1`, `BUILD_APP=1`, `APP_OUT=...`, `MC_DIR=...`, `MC_JAR=...`, `HACTOOL=...`, `JOBS=...`. Once extracted/rewritten, bare `make setup-v121` skips completed stages and rebuilds.)*

   [`scripts/setup_v121.py`](scripts/setup_v121.py) automatically creates `.venv` (Python 3.14+), builds patched `hactool` under `tooling/hactool/` if missing, decompresses `.xcz`/`.nsz` dumps in a temporary directory, extracts TOTK v1.2.1 ExeFS/RomFS/shared fonts, prepares v1.2.1 module ELF inventories, builds v1.2.1 RomFS overlays, rewrites & verifies NSOs, auto-discovers your newest Minecraft: Java Edition `1.21+` / `26.x+` installation to populate `assets/mc/`, stages `prebuilt/macos-arm64/totk_xlate` + `spirv-cross` into `build/`, and builds `build/totk-native-v121` (`make runtime-v121`). *(Run `python3 scripts/setup_v121.py --help` for all flags.)*

3. **Package `.app` bundle (if not already built above) and launch:**
   ```bash
   make app-v121
   open "build/TOTK Minecraft Overlay.app"
   ```
   *(Or run directly from the terminal without packaging using `make run-v121`, which stores saves and `minecraft-base.v1` in `user-data/` by default — override with `PROFILE_DIR=/path/to/profile`. `make app-v121` invokes [`scripts/build_app_v121.py`](scripts/build_app_v121.py) using APFS copy-on-write clones and accepts optional `APP_OUT=...`, `BASE_ROM=...`, and `KEYS=...` for `AppIcon.icns` extraction.)*

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
