# TOTK Minecraft Overlay (`TOTK-minecraft-overlay`)

A **Minecraft-in-Hyrule** runtime overlay mod embedded directly into *The Legend of Zelda: Tears of the Kingdom* (**v1.2.1** — `totk-native` on macOS ARM64 / Metal 4, and `totk-pc-recomp` on Windows x86-64 / Vulkan).

| Platform | Branch | Renderer / Runtime | Easiest Way to Install |
| :--- | :--- | :--- | :--- |
| **Windows (x86-64)** | [`main`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/main) | Vulkan / `totk-pc-recomp` | Download `TOTK-minecraft-overlay-windows-setup.zip` from [Releases](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest) and run `TotK-Setup.exe` |
| **macOS (Apple Silicon)** | [`macos`](https://github.com/PDT88/TOTK-minecraft-overlay/tree/macos) | Metal 4 / `totk-native` | Download macOS source ZIP from [Releases](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest) and run `Setup TOTK Minecraft Overlay.command` |

Rather than replacing Link's model with a static skin, this mod embeds a live Minecraft voxel, entity, inventory, HUD, and audio engine inside the running TOTK process—letting you build, fight, craft, and explore Hyrule with Minecraft mechanics synchronized in real time with Zelda's world, physics, and combat.

> **Note on Copyrighted Assets:** This repository contains **only** original source code, build scripts, and documentation. No Nintendo game files or Mojang/Minecraft assets are included. You must supply your own legally obtained copies of *Tears of the Kingdom* (base + **v1.2.1** update + `prod.keys`) and *Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`) to set up and run locally.

---

## Quick Start

- **On macOS?** Download `TOTK-minecraft-overlay-v1.0.0-macos-source.zip` from [**GitHub Releases**](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest) (or clone `macos`), install Xcode Command Line Tools + Homebrew once, then double-click [`Setup TOTK Minecraft Overlay.command`](Setup%20TOTK%20Minecraft%20Overlay.command) to build **`TOTK Minecraft Overlay.app`**. Jump to [**Setup & Usage (macOS)**](#setup--usage-macos).
- **On Windows?** Download `TOTK-minecraft-overlay-windows-setup.zip` from [**GitHub Releases**](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest), unzip it, and run `TotK-Setup.exe` (no build toolchain required). See the [**Windows README**](https://github.com/PDT88/TOTK-minecraft-overlay/tree/main#setup--building-windows).

---

## Requirements & What You Need (macOS)

### System & One-Time Tools

- **Mac & OS**: Apple Silicon Mac (`arm64`) running **macOS 26 (Tahoe)** / **Metal 4**.
- **Free Disk Space**: **~25–30 GB** free *(plus ~18 GB of temporary free space during setup if decompressing `.xcz` / `.nsz` dumps)*.
- **Xcode 26 Command Line Tools** *(one-time install in Terminal)*:
  ```bash
  xcode-select --install
  ```
- **Homebrew** *(one-time install if you don't have it yet — see [brew.sh](https://brew.sh))*:
  ```bash
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  ```
- *(Python 3.14+ and native build/audio tools — `python@3.14`, `opus`, `libvpx`, `ffmpeg`, `llvm`, and `spirv-cross` — are installed automatically by [`Setup TOTK Minecraft Overlay.command`](Setup%20TOTK%20Minecraft%20Overlay.command) or `make install-deps`.)*

### Files You Need to Supply

Have these legal files ready on your Mac before starting:

1. ***The Legend of Zelda: Tears of the Kingdom* base dump**: `.xci`, `.nsp`, `.xcz`, or `.nsz`
2. ***Tears of the Kingdom* v1.2.1 update**: `.nsp` or `.nsz` *(must be **v1.2.1** specifically)*
3. **`prod.keys`**: Automatically detected if already at `~/Library/Application Support/Ryujinx/system/prod.keys`
4. **Switch firmware** *(only needed if your base dump is `.nsp` or `.nsz`, optional for `.xci`/`.xcz`)*: `.zip` archive or unpacked NCA folder
5. ***Minecraft: Java Edition* (`1.21+`, including `1.21.x` and `26.x+`)**: Installed via the Minecraft Launcher and **launched at least once** so textures, sounds, and language assets exist under `~/Library/Application Support/minecraft` *(or if you use Prism / another custom launcher, you can point directly to the client `.jar` during setup)*

---

## Setup & Usage (macOS)

### Option A: Double-Click Guided Setup (Recommended)

1. **Get the files**: Download and unzip `TOTK-minecraft-overlay-v1.0.0-macos-source.zip` from [**GitHub Releases**](https://github.com/PDT88/TOTK-minecraft-overlay/releases/latest) *(or `git clone -b macos https://github.com/PDT88/TOTK-minecraft-overlay.git`)*.
2. **Start the guided setup**: Open the unpacked folder in Finder and double-click [`Setup TOTK Minecraft Overlay.command`](Setup%20TOTK%20Minecraft%20Overlay.command).
   - *If macOS Gatekeeper blocks it on first launch*, open **System Settings → Privacy & Security**, scroll down and click **Open Anyway** (or run `xattr -dr com.apple.quarantine .` inside the folder in Terminal, then double-click again).
3. **Install dependencies**: The script checks Apple Silicon, macOS 26+, Xcode CLT, and Homebrew, then automatically installs any missing Homebrew packages via `python3 scripts/setup_v121.py --install-deps`.
4. **Pick your files using the Finder dialogs**:
   - **TOTK Base ROM**: select your `.xci`, `.nsp`, `.xcz`, or `.nsz` file
   - **TOTK v1.2.1 Update**: select your `.nsp` or `.nsz` v1.2.1 update file
   - **`prod.keys`**: confirm the auto-detected Ryujinx `prod.keys` or browse to yours
   - **Switch Firmware**: pick a firmware `.zip` or unpacked NCA folder *(required for `.nsp`/`.nsz` base; click **Skip** if using an `.xci`/`.xcz` with an update partition)*
   - **Minecraft Assets**: keep **Auto-Detect** for `~/Library/Application Support/minecraft`, or click **Choose JAR...** to pick a specific `1.21+` client `.jar`
   - **Shader Pre-compilation** *(optional)*: click **Yes** to pre-compile startup shaders (`--prepare-shaders`) for smoother initial gameplay, or **No** for a faster first-time setup
   - **App Bundle**: keep **Yes** (default) to build a standalone `build/TOTK Minecraft Overlay.app`
5. **Wait ~10–20 minutes** on first setup while game files are extracted, rewritten/verified, and compiled in Terminal. *(If anything stops mid-way, just run it again—completed stages are automatically skipped.)*
6. **Play**: When finished, Finder opens `build/TOTK Minecraft Overlay.app` — double-click it to launch!
   - Packaging uses APFS copy-on-write cloning so `.app` creation adds minimal extra disk space, and dragging `TOTK Minecraft Overlay.app` into `/Applications` on the same APFS volume stays space-efficient.
   - Saves, profile data, and shader caches are stored under `~/Library/Application Support/MacNativeGames/TOTK/`.

### Option B: Terminal / Makefile

1. **Clone the repository and install dependencies:**
   ```bash
   git clone -b macos https://github.com/PDT88/TOTK-minecraft-overlay.git
   cd TOTK-minecraft-overlay
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
   To also pre-compile startup shaders (`build/shader-seed`) and package `build/TOTK Minecraft Overlay.app` in one pass, add `PREPARE_SHADERS=1` and `BUILD_APP=1`:
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

3. **Package `.app` bundle (if not built above) and launch:**
   ```bash
   make app-v121
   open "build/TOTK Minecraft Overlay.app"
   ```
   *(Or run directly from the terminal without packaging using `make run-v121`, which stores saves and `minecraft-base.v1` in `user-data/` by default — override with `PROFILE_DIR=/path/to/profile`. `make app-v121` invokes [`scripts/build_app_v121.py`](scripts/build_app_v121.py) and accepts optional `APP_OUT=...`, `BASE_ROM=...`, and `KEYS=...` for `AppIcon.icns` extraction.)*

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

## Troubleshooting (macOS)

- **"Apple could not verify `Setup TOTK Minecraft Overlay.command`" (Gatekeeper)**: Open **System Settings → Privacy & Security**, click **Open Anyway**, and run it again (or run `xattr -dr com.apple.quarantine .` in Terminal inside the unpacked folder).
- **ExeFS / RomFS mismatch or missing v1.2.1 error**: Make sure the update file is specifically **TOTK v1.2.1** (`--update`) and matches your base dump (`--base`) + `prod.keys`.
- **Minecraft assets not found**: Launch Minecraft: Java Edition `1.21+` at least once so the official launcher downloads `assets/indexes`, `assets/objects`, and the version `.jar`, or choose your custom launcher's `.jar` (with `MC_DIR` / **Choose JAR...**).
- **Setup was interrupted mid-way**: Re-run [`Setup TOTK Minecraft Overlay.command`](Setup%20TOTK%20Minecraft%20Overlay.command) or `make setup-v121` — already-verified stages are skipped automatically. Run `python3 scripts/setup_v121.py --check-only` to see which stages are done.
- **Where are saves and logs?**: `.app` launches store saves and shader caches in `~/Library/Application Support/MacNativeGames/TOTK/` and write runtime logs to `~/Library/Logs/MacNativeGames/totk.log`.

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

## Architecture (macOS / Metal 4)

The TOTK bridge (`runtime/mc_totk_bridge.c`), camera hooks, and Minecraft Settings UI target **TOTK v1.2.1** (`TOTK_GAME_V121=1`):

| Component | Files | Purpose |
| :--- | :--- | :--- |
| **Overlay Controller** | [`runtime/minecraft_overlay.h`](runtime/minecraft_overlay.h), [`runtime/minecraft_overlay.c`](runtime/minecraft_overlay.c) | Entrypoints for lifecycle, input/mouse capture, chat/debug commands, and frame coordination. |
| **Core Simulation** | [`runtime/mc_engine.h`](runtime/mc_engine.h), [`runtime/mc_engine.c`](runtime/mc_engine.c), [`runtime/mc_snapshot.h`](runtime/mc_snapshot.h) | Voxel blocks, mobs, primed TNT, dropped items, projectiles, particles, crafting/smelting, HUD layout, and thread-safe POD `McRenderSnapshot` emission. |
| **TOTK Runtime Bridge** | [`runtime/mc_totk_bridge.h`](runtime/mc_totk_bridge.h), [`runtime/mc_totk_bridge.c`](runtime/mc_totk_bridge.c) | Reads/writes Link's pose, HP, stamina, camera matrix, terrain raycasts, wall-sweep collision, active actors/enemies, and incoming damage on the guest frame thread. |
| **Metal Renderer** | [`runtime/mc_metal_render.h`](runtime/mc_metal_render.h), [`runtime/mc_metal_render.m`](runtime/mc_metal_render.m) | Multi-pass Metal rendering: 3D world depth-tested against Hyrule's G-buffer, 1st-person viewmodel, offscreen 3D inventory paperdoll, and nearest-neighbor 2D HUD/text. |
| **Asset Loader & Extractor** | [`runtime/mc_assets.h`](runtime/mc_assets.h), [`runtime/mc_assets.c`](runtime/mc_assets.c), [`tools/extract_mc_assets.py`](tools/extract_mc_assets.py) | Auto-discovers installed Minecraft Java `1.21+` versions and extracts textures, glyph widths, `en_us.json`, and 48 kHz WAVs into `assets/mc/` (gitignored). |
| **Audio Mixer** | [`runtime/mc_audio.h`](runtime/mc_audio.h), [`runtime/mc_audio.c`](runtime/mc_audio.c), [`runtime/mc_link_audio_ids.inc`](runtime/mc_link_audio_ids.inc) | Mixes 48 kHz stereo float PCM Minecraft sound effects into the runtime audio stream. |

---

## Repository Hygiene

The [`.gitignore`](.gitignore) file explicitly excludes all copyrighted, extracted, or generated artifacts:
- `assets/mc/` (extracted Minecraft textures, sounds, strings, and manifest)
- `extracted/`, `rewritten/`, `build/`, `user-data/`, `analysis/`, `tooling/`, `.venv/`

---

## Upstream & References

- **Base TOTK Native Runtime (`MacNativeGames`)**: https://github.com/ly05010419/MacNativeGames
