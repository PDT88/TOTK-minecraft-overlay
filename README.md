# TOTK Minecraft Overlay (`TOTK-minecraft-overlay`)

A **Minecraft-in-Hyrule** runtime overlay mod embedded into a native macOS ARM64 port of *The Legend of Zelda: Tears of the Kingdom* (`totk-native`), along with a full specification for porting the overlay to Windows (`totk-pc-recomp`).

> **Note on Copyrighted Assets:** This repository contains **only** original source code, build scripts, and documentation. No Nintendo game files, Switch keys, or Mojang/Minecraft assets are included. You must supply your own legally obtained copies of *Tears of the Kingdom* and *Minecraft: Java Edition* (`1.21.11`) to extract assets and build/run locally.

---

## Architecture

The overlay runs a self-contained Minecraft voxel, entity, inventory, HUD, and audio engine directly inside the TOTK runtime, synchronized with Hyrule's world, physics, camera, and combat systems:

| Component | Files | Purpose |
| :--- | :--- | :--- |
| **Overlay Controller** | [`runtime/minecraft_overlay.h`](runtime/minecraft_overlay.h), [`runtime/minecraft_overlay.c`](runtime/minecraft_overlay.c) | Entrypoints for lifecycle, input/mouse capture, chat/debug commands, and frame coordination. |
| **Core Simulation** | [`runtime/mc_engine.h`](runtime/mc_engine.h), [`runtime/mc_engine.c`](runtime/mc_engine.c), [`runtime/mc_snapshot.h`](runtime/mc_snapshot.h) | Voxel blocks, mobs, primed TNT, dropped items, projectiles, particles, crafting/smelting, HUD layout, and thread-safe POD `McRenderSnapshot` emission. |
| **TOTK Runtime Bridge** | [`runtime/mc_totk_bridge.h`](runtime/mc_totk_bridge.h), [`runtime/mc_totk_bridge.c`](runtime/mc_totk_bridge.c) | Reads/writes Link's pose, HP, stamina, camera matrix, terrain raycasts, wall-sweep collision, active actors/enemies, and incoming damage on the guest frame thread. |
| **Metal Renderer** | [`runtime/mc_metal_render.h`](runtime/mc_metal_render.h), [`runtime/mc_metal_render.m`](runtime/mc_metal_render.m) | Multi-pass Metal rendering: 3D world depth-tested against Hyrule's G-buffer, 1st-person viewmodel, offscreen 3D inventory paperdoll, and nearest-neighbor 2D HUD/text. |
| **Asset Loader & Extractor** | [`runtime/mc_assets.h`](runtime/mc_assets.h), [`runtime/mc_assets.c`](runtime/mc_assets.c), [`tools/extract_mc_assets.py`](tools/extract_mc_assets.py) | Extracts textures, glyph widths, `en_us.json`, and 48 kHz WAVs from a local Minecraft Java `1.21.11` installation into `assets/mc/` (gitignored). |
| **Audio Mixer** | [`runtime/mc_audio.h`](runtime/mc_audio.h), [`runtime/mc_audio.c`](runtime/mc_audio.c), [`runtime/mc_link_audio_ids.inc`](runtime/mc_link_audio_ids.inc) | Mixes 48 kHz stereo float PCM Minecraft sound effects into the runtime audio stream. |
| **Windows Port Spec** | [`WINDOWS_MC_MOD_SPEC.md`](WINDOWS_MC_MOD_SPEC.md) | Full specification for porting the overlay to Windows (`valentderah/totk-pc-recomp` + Vulkan). |

---

## Setup & Usage (macOS)

1. **Install Python dependencies & tools:**
   ```bash
   make dependencies
   ```
   Ensure `ffmpeg` is available on `PATH` for decoding Minecraft `.ogg` audio to 48 kHz `.wav`.

2. **Extract Minecraft assets (local, gitignored):**
   ```bash
   .venv/bin/python tools/extract_mc_assets.py
   ```
   This populates `assets/mc/` from your local Minecraft Java `1.21.11` installation (`~/Library/Application Support/minecraft/`).

3. **Provide TOTK base files (local, gitignored):**
   Place your legally dumped TOTK image/update and `prod.keys` as configured in [`Makefile`](Makefile), then run the extraction/rewrite targets (`make extract`, `make modules`, `make rewrite-sdk`, `make rewrite-main`).

4. **Build the native runtime:**
   ```bash
   make build/totk-native
   ```

---

## Repository Hygiene

The [`.gitignore`](.gitignore) file explicitly excludes all copyrighted, extracted, or generated artifacts:
- `assets/mc/` (extracted Minecraft textures, sounds, strings, and manifest)
- `extracted/`, `rewritten/`, `build/`, `user-data/`, `*.keys`, `analysis/`, `tooling/`, `.venv/`
