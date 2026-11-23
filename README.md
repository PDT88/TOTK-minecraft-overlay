# TOTK Minecraft Overlay (`TOTK-minecraft-overlay`) — Windows

A **Minecraft-in-Hyrule** runtime overlay mod embedded directly into *The Legend of Zelda: Tears of the Kingdom* (`totk-pc-recomp` on Windows x86-64 / Vulkan, and `totk-native` on macOS ARM64 / Metal).

| Platform | Branch | Renderer / Runtime |
| :--- | :--- | :--- |
| **macOS (Apple Silicon)** | [`main`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/main) | Metal / `totk-native` |
| **Windows (x86-64)** | [`windows`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/windows) *(this branch)* | Vulkan / `totk-pc-recomp` |

Rather than replacing Link's model with a static skin, this mod embeds a live Minecraft voxel, entity, inventory, HUD, and audio engine inside the running TOTK process—letting you build, fight, craft, and explore Hyrule with Minecraft mechanics synchronized in real time with Zelda's world, physics, and combat.

> **Note on Copyrighted Assets:** This repository contains **only** original source code, shaders, build scripts, and documentation. No Nintendo game files, Switch keys, or Mojang/Minecraft assets are included. You must supply your own legally obtained copies of *Tears of the Kingdom* (**v1.2.1**) and *Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`) to extract assets and build/run locally.

---

## Features

- **Seamless Mode & Camera Switching**: Toggle between **Minecraft Mode** and **Zelda Mode** on the fly, with first-person, third-person back, and third-person front views, raw mouse-look camera capture, and automatic hiding/restoring of Link's 3D body and Zelda's native HUD.
- **Depth-Tested 3D World in Hyrule**: Place and mine blocks (wood, stone, obsidian, glass, torches, water, lava, crafting tables, furnaces, beds, TNT, and more) directly in Hyrule, depth-tested against Hyrule's G-buffer depth via Vulkan so blocks, mobs, dropped items, projectiles, and particles occlude naturally behind hills and walls.
- **First-Person Viewmodel**: Dedicated depth-buffered 1st-person arm, held items, tools, weapons, bows, and shields that never clip into nearby Hyrule geometry.
- **Live Combat, Mobs & Physics Bridge**:
  - Syncs Link's position, velocity, HP (quarter-hearts), and stamina with TOTK's guest state.
  - Attacks from Minecraft swords, axes, bows, arrows, and primed TNT damage active TOTK enemies; incoming damage to Link flows through Minecraft armor, directional shield blocking, and Totem of Undying.
  - Spawns Minecraft mobs with AI, animations, and Hyrule terrain/wall-sweep collision.
- **Full Minecraft HUD & Menus**: Crisp nearest-neighbor 2D HUD (hotbar, hearts, hunger, armor, XP bar, crosshair + attack cooldown indicator, status effects), Survival inventory with live 3D paperdoll preview, 3x3 Crafting Table, Furnace smelting, Creative tabbed inventory, tooltips, and in-game settings integration.
- **Authentic Assets & 48 kHz Audio**: Extracts textures, fonts, `en_us.json`, and 48 kHz PCM WAVs directly from a local Minecraft: Java Edition installation and mixes footsteps, digging, combat, eating, TNT, fireworks, XP, and hurt SFX into the runtime audio stream.

---

## Architecture (Windows / Vulkan)

| Component | Files | Purpose |
| :--- | :--- | :--- |
| **Overlay Controller** | [`runtime/minecraft_overlay.h`](runtime/minecraft_overlay.h), [`runtime/minecraft_overlay.c`](runtime/minecraft_overlay.c) | Entrypoints for lifecycle, input/mouse capture, chat/debug commands, and frame coordination. |
| **Core Simulation** | [`runtime/mc_engine.h`](runtime/mc_engine.h), [`runtime/mc_engine.c`](runtime/mc_engine.c), [`runtime/mc_snapshot.h`](runtime/mc_snapshot.h) | Voxel blocks, mobs, primed TNT, dropped items, projectiles, particles, crafting/smelting, HUD layout, and thread-safe POD `McRenderSnapshot` emission. |
| **TOTK Runtime Bridge** | [`runtime/mc_totk_bridge.h`](runtime/mc_totk_bridge.h), [`runtime/mc_totk_bridge.c`](runtime/mc_totk_bridge.c) | Reads/writes Link's pose, HP, stamina, camera matrix, terrain raycasts, wall-sweep collision, active actors/enemies, and incoming damage on the guest frame thread (TOTK v1.2.1). |
| **Vulkan Renderer & Shaders** | [`runtime/mc_vulkan_render.h`](runtime/mc_vulkan_render.h), [`runtime/mc_vulkan_render.c`](runtime/mc_vulkan_render.c), [`runtime/shaders/`](runtime/shaders/) | Multi-pass Vulkan rendering (`mc_world_*`, `mc_gui_*`, `mc_probe_luma_cs`): 3D world depth-tested against Hyrule's G-buffer, 1st-person viewmodel, offscreen 3D inventory paperdoll, and nearest-neighbor 2D HUD/text. |
| **Asset Loader & Extractor** | [`runtime/mc_assets.h`](runtime/mc_assets.h), [`runtime/mc_assets.c`](runtime/mc_assets.c), [`tools/extract_mc_assets.py`](tools/extract_mc_assets.py) | Extracts textures, glyph widths, `en_us.json`, and 48 kHz WAVs from `%APPDATA%\.minecraft\` into `assets/mc/` (gitignored). |
| **Audio Mixer** | [`runtime/mc_audio.h`](runtime/mc_audio.h), [`runtime/mc_audio.c`](runtime/mc_audio.c), [`runtime/mc_link_audio_ids.inc`](runtime/mc_link_audio_ids.inc) | Mixes 48 kHz stereo float PCM Minecraft sound effects into the runtime audio stream. |

---

## Setup & Building (Windows)

1. **Prerequisites**:
   - Windows 10 or later (x86-64 CPU with SSE4.2+, Vulkan 1.3 GPU).
   - Build toolchain and dependencies as described in [`docs/building.md`](docs/building.md).
   - Python 3.10+ with `Pillow` (and `ffmpeg.exe` on `PATH`, or `soundfile` + `numpy`) for Minecraft asset extraction.

2. **Extract Minecraft assets (local, gitignored)**:
   ```powershell
   python tools\extract_mc_assets.py
   ```
   This auto-discovers your newest installed Minecraft: Java Edition `1.21+` / `26.x+` JAR under `%APPDATA%\.minecraft\versions\` (preferring stable releases over snapshots; overrideable via `--jar`, `--assets-dir`, `--index`, or `MINECRAFT_JAR`) and populates `assets\mc\`.

3. **Provide TOTK v1.2.1 base files & build**:
   Follow [`docs/building.md`](docs/building.md) to build `totk-pc-recomp` against your legally dumped **TOTK v1.2.1** game files.

---

## Repository Hygiene & License

- [`.gitignore`](.gitignore) and [`tools/check_content.py`](tools/check_content.py) enforce that no Nintendo game files, keys, binaries (`*.exe`, `*.dll`), or extracted Minecraft assets (`assets/mc/`) are committed.
- Project code is under the [MIT License](LICENSE); third-party components keep their respective licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
