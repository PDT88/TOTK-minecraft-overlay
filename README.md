# TOTK Minecraft Overlay (`TOTK-minecraft-overlay`) — Windows

A **Minecraft-in-Hyrule** runtime overlay mod embedded directly into *The Legend of Zelda: Tears of the Kingdom* (**v1.2.1** — `totk-pc-recomp` on Windows x86-64 / Vulkan, and `totk-native` on macOS ARM64 / Metal 4).

| Platform | Branch | Renderer / Runtime | Easiest Way to Install |
| :--- | :--- | :--- | :--- |
| **Windows (x86-64)** | [`windows`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/windows) | Vulkan / `totk-pc-recomp` | Download `TOTK-minecraft-overlay-windows-setup.zip` from [Releases](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest) and run `TotK-Setup.exe` |
| **macOS (Apple Silicon)** | [`main`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/main) | Metal 4 / `totk-native` | Download macOS source ZIP from [Releases](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest) and run `Setup TOTK Minecraft Overlay.command` |

Rather than replacing Link's model with a static skin, this mod embeds a live Minecraft voxel, entity, inventory, HUD, and audio engine inside the running TOTK process—letting you build, fight, craft, and explore Hyrule with Minecraft mechanics synchronized in real time with Zelda's world, physics, and combat.

> **Note on Copyrighted Assets:** This repository contains **only** original source code, shaders, build scripts, and documentation. No Nintendo game files or Mojang/Minecraft assets are included. You must supply your own legally obtained copies of *Tears of the Kingdom* (base + **v1.2.1** update + `prod.keys`) and *Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`) to build and run locally.

---

## Quick Start

- **On Windows?** Download `TOTK-minecraft-overlay-windows-setup.zip` from [**GitHub Releases**](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest), unzip it, and run `TotK-Setup.exe` (no separate build toolchain or `ffmpeg` required). Jump to [**Setup & Building (Windows)**](#setup--building-windows).
- **On macOS?** See the [**macOS README**](https://github.com/PDT88/TOTK-minecraft-overlay/tree/main#setup--usage-macos) for the guided `Setup TOTK Minecraft Overlay.command` flow.

---

## Requirements (Windows)

- **OS & Hardware**: Windows 10 or later (x86-64 CPU with SSE4.2+, Vulkan 1.3 GPU, **~42 GB free disk space** for first-time setup).
- ***Tears of the Kingdom* (v1.2.1)**: Your own legally dumped *Tears of the Kingdom* base image (`.xci` or `.nsp`), **v1.2.1 update** (`.nsp`), and `prod.keys` *(plus an unpacked Switch firmware NCA directory if using Switch shared fonts)*.
- ***Minecraft: Java Edition***: Version `1.21+` (including `1.21.x` and `26.x+`) installed locally and **launched at least once** via the launcher so textures, fonts, localization, and audio assets are present under `%APPDATA%\.minecraft\`.
- **Build & Asset Tools *(Option B from source only)***: Toolchain listed below and in [`docs/building.md`](docs/building.md) (Python 3.12 + 3.14 are managed automatically via `uv`; `ffmpeg.exe` on `PATH` or `soundfile` + `numpy` is used for audio decoding). **Option A (`TotK-Setup.exe`) bundles everything needed and requires no separate toolchain install.**

---

## Setup & Building (Windows)

### Option A: `TotK-Setup` GUI Installer *(Recommended — No Build Toolchain Required)*

1. Download `TOTK-minecraft-overlay-windows-setup.zip` from [**GitHub Releases**](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest) *(or build it via the **Build Windows Installer** workflow in [`.github/workflows/build-windows-release.yml`](.github/workflows/build-windows-release.yml))*.
2. Unpack the ZIP and double-click `TotK-Setup.exe` *(if Windows SmartScreen prompts because the executable is unsigned, click **More info → Run anyway**)*.
3. Select your local TOTK base game, **v1.2.1** update, `prod.keys`, and an empty install folder (~42 GB free), then click **Start**.
4. Wait ~10–15 minutes while `TotK-Setup.exe` extracts game files, builds `totk.exe`, pre-warms shaders, and extracts your local Minecraft Java `1.21+` / `26.x+` assets into `assets\mc\` automatically (no `ffmpeg` or build toolchain required).
5. Launch `totk.exe` inside your chosen install folder.

---

### Option B: Quick Setup from Source (PowerShell)

1. **Clone the `windows` branch:**
   ```powershell
   git clone -b windows https://github.com/PDT88/TOTK-minecraft-overlay.git
   cd TOTK-minecraft-overlay
   ```

2. **Install Windows build dependencies in one command (run in an Administrator PowerShell window):**
   Right-click PowerShell → **Run as administrator**, then run:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\install_windows_deps.ps1
   ```
   This installs `uv`, `.NET 10 SDK`, `Vulkan SDK`, `FFmpeg`, `Visual Studio Build Tools` (VCTools), and `MSYS2 UCRT64` packages, and adds `Visual Studio\Installer` to your User `PATH`. Then open a **new** PowerShell window in this repo.

   <details>
   <summary>Manual dependency install commands (if you prefer running step by step)</summary>

   ```powershell
   winget install -e --id astral-sh.uv
   winget install -e --id Microsoft.DotNet.SDK.10
   winget install -e --id KhronosGroup.VulkanSDK
   winget install -e --id Gyan.FFmpeg
   winget install -e --id Microsoft.VisualStudio.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
   - Ensure `C:\Program Files (x86)\Microsoft Visual Studio\Installer` is on your user `PATH` so `.NET` NativeAOT (`tools\build_xlate.py`) can locate `vswhere.exe`.
   - Install [MSYS2](https://www.msys2.org/) to `C:\msys64`, open **MSYS2 UCRT64**, run `pacman -Syuu` (twice if the core updates and exits), then run:
     ```sh
     pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-{clang,lld,ninja,gcc,make,pkgconf,sdl3,opus,libvpx,zstd,vulkan-devel,vulkan-memory-allocator,vulkan-validation-layers,shaderc,spirv-tools} base-devel git unzip
     ```

   </details>

3. **Run the automated setup (pointing to your local TOTK v1.2.1 files):**
   ```powershell
   uv run --python 3.12 tools\setup_mc_windows.py `
     --base C:\path\to\totk_base.xci `
     --update C:\path\to\totk_update_1.2.1.nsp `
     --keys C:\path\to\prod.keys
   ```
   *(Or you can copy `config.example.toml` to `config.local.toml` with your local paths and run `uv run --python 3.12 tools\setup_mc_windows.py` without path flags. Add `--firmware C:\path\to\firmware` for Switch shared fonts, `--cpu v2` for CPUs without AVX2/FMA, or `--prepare-shaders` to fetch shader refs and pre-warm the shader cache. Run `--check-only` anytime to inspect prerequisite/stage status.)*

   This creates `.venv` / `.venv-py314`, extracts local Minecraft Java `1.21+` / `26.x+` assets into `assets\mc\`, builds `hactool.exe` (`tools\build_hactool.py`), extracts TOTK v1.2.1 (and shared fonts if `firmware` is set), builds `totk_xlate.exe`, lifts `v121`, and compiles `build\v121\totk.exe`, skipping any stage that is already complete.

4. **Launch:**
   ```powershell
   build\v121\totk.exe
   ```

### Manual Steps

1. **Extract Minecraft assets (local, gitignored)**:
   ```powershell
   .venv\Scripts\python tools\extract_mc_assets.py
   ```
   This auto-discovers your newest installed Minecraft: Java Edition `1.21+` / `26.x+` JAR under `%APPDATA%\.minecraft\versions\` (preferring stable releases over snapshots; overrideable via `--jar`, `--assets-dir`, `--index`, or `MINECRAFT_JAR`) and populates `assets\mc\`.

2. **Build `hactool`, extract, and compile `totk-pc-recomp`**:
   Follow [`docs/building.md`](docs/building.md) (`tools\build_hactool.py`, `tools\extract.py`, `tools\extract_fonts.py`, `tools\build_xlate.py`, `recomp\gen.py`, `tools\make_romfs_patches.py`, `tools\build_game.py`, and optionally `tools\prepare_shaders.py`).

---

## Controls

| Input | Action |
| :--- | :--- |
| `` ` `` / `~` or `F6` | Toggle **Minecraft Mode** / **Zelda Mode** |
| `F5` or `V` | Cycle camera view (1st-person → 3rd-person back → 3rd-person front) |
| `E` / `Esc` | Open / close Creative & Survival Inventory |
| `1`–`9` or Scroll | Select hotbar slot |
| `Left Click` | Attack / hold to mine block (sword or flint ignites TNT) |
| `Right Click` | Place block / use item / eat / draw bow / open Crafting Table or Furnace / sleep in bed / raise Shield |
| `W` (double-tap) | Sprint |
| `F` | Swap mainhand and offhand |
| `G` | Quick-toggle Elytra + Totem of Undying vs. Diamond Chestplate + Shield |
| `Space` | Jump (press again in midair with Elytra to glide; right-click Firework Rocket to boost) |
| `Q` (`Shift+Q`) | Drop 1 item (or full stack) |
| `N` | Spawn a Minecraft mob nearby |
| `F1` | Open / close `totk-pc-recomp` options menu (Minecraft Overlay settings) |

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

## Repository Hygiene & License

- [`.gitignore`](.gitignore) and [`tools/check_content.py`](tools/check_content.py) enforce that no Nintendo game files, binaries (`*.exe`, `*.dll`), or extracted Minecraft assets (`assets/mc/`) are committed.
- Project code is under the [MIT License](LICENSE); third-party components keep their respective licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

---

## Upstream & References

- **Base TOTK Native Runtime (`totk-pc-recomp`)**: https://github.com/valentderah/totk-pc-recomp
