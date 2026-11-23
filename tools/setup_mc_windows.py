#!/usr/bin/env python3
"""Automated Windows setup & preflight helper for TOTK v1.2.1 + Minecraft Overlay.

Checks Windows toolchain prerequisites, sets up Python environments (.venv and
.venv-py314), extracts local Minecraft Java 1.21+ / 26.x+ assets into assets/mc/,
and builds build/v121/totk.exe — skipping stages whose outputs already exist.

Examples:
  python tools/setup_mc_windows.py --check-only
  python tools/setup_mc_windows.py
  python tools/setup_mc_windows.py --cpu v2 --mc-jar C:\\path\\to\\1.21.11.jar
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MSYS2_BIN = Path(r"C:\msys64\ucrt64\bin")


def venv_python(venv_dir: Path) -> Path:
    if os.name == "nt":
        return venv_dir / "Scripts" / "python.exe"
    return venv_dir / "bin" / "python"


def find_tool(name: str, extra_paths: tuple[Path, ...] = ()) -> Path | None:
    exe = f"{name}.exe" if os.name == "nt" and not name.endswith(".exe") else name
    for cand in extra_paths:
        if cand.is_file():
            return cand
    found = shutil.which(exe) or shutil.which(name)
    return Path(found) if found else None


def find_ffmpeg() -> Path | None:
    candidates: list[Path] = []
    local_app = os.environ.get("LOCALAPPDATA")
    if local_app:
        candidates.append(Path(local_app) / "Microsoft" / "WinGet" / "Links" / "ffmpeg.exe")
        pkgs = Path(local_app) / "Microsoft" / "WinGet" / "Packages"
        if pkgs.is_dir():
            for pkg in sorted(pkgs.glob("Gyan.FFmpeg*")):
                candidates.extend(pkg.glob("*/bin/ffmpeg.exe"))
                candidates.extend(pkg.glob("bin/ffmpeg.exe"))
    return find_tool("ffmpeg", tuple(candidates))


def run_step(label: str, cmd: list[str]) -> None:
    print(f"\n==> {label}")
    print("    " + " ".join(cmd))
    res = subprocess.run(cmd, cwd=ROOT)
    if res.returncode != 0:
        raise SystemExit(f"ERROR: {label} failed (exit code {res.returncode})")


def check_prereqs(need_mc: bool, need_build: bool) -> list[str]:
    missing: list[str] = []
    if not find_tool("uv"):
        missing.append("uv (https://docs.astral.sh/uv/)")
    if need_build:
        if not find_tool("clang", (MSYS2_BIN / "clang.exe",)):
            missing.append("MSYS2 UCRT64 clang (see docs/building.md)")
        if not find_tool("ninja", (MSYS2_BIN / "ninja.exe",)):
            missing.append("MSYS2 UCRT64 ninja (see docs/building.md)")
        if not find_tool("dotnet"):
            missing.append(".NET 10 SDK (`winget install Microsoft.DotNet.SDK.10`)")
    if need_mc and not find_ffmpeg():
        missing.append("ffmpeg (`winget install -e --id Gyan.FFmpeg`, or install `soundfile` + `numpy`)")
    return missing


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="v121", help="TOTK version (default: v121)")
    ap.add_argument("--cpu", choices=("v2", "v3"), default="v3", help="x86-64 target level for build_game.py (default: v3)")
    ap.add_argument("--mc-jar", type=Path, help="Optional explicit Minecraft 1.21+ / 26.x+ client JAR")
    ap.add_argument("--mc-assets-dir", type=Path, help="Optional explicit .minecraft/assets directory")
    ap.add_argument("--mc-index", type=Path, help="Optional explicit Minecraft asset index JSON")
    ap.add_argument("--prepare-shaders", action="store_true", help="Also run tools/prepare_shaders.py after building")
    ap.add_argument("--skip-mc", action="store_true", help="Skip Minecraft asset extraction")
    ap.add_argument("--skip-build", action="store_true", help="Skip lifting and compiling totk.exe")
    ap.add_argument("--check-only", action="store_true", help="Check prerequisites and stage status without running")
    args = ap.parse_args()

    ver = args.version
    exe_name = "totk.exe" if os.name == "nt" else "totk"
    xlate_name = "totk_xlate.exe" if os.name == "nt" else "totk_xlate"

    venv312 = ROOT / ".venv"
    venv314 = ROOT / ".venv-py314"
    py312 = venv_python(venv312)
    py314 = venv_python(venv314)
    cfg_local = ROOT / "config.local.toml"
    exefs_dir = ROOT / "build" / "extracted" / ver / "exefs"
    xlate_bin = ROOT / "build" / "xlate" / xlate_name
    mc_manifest = ROOT / "assets" / "mc" / "manifest.json"
    romfs_patches = ROOT / "build" / ver / "romfs-patches"
    totk_bin = ROOT / "build" / ver / exe_name

    have_venv312 = py312.is_file()
    have_venv314 = py314.is_file()
    have_cfg = cfg_local.is_file()
    have_exefs = exefs_dir.is_dir() and any(exefs_dir.iterdir())
    have_xlate = xlate_bin.is_file()
    have_mc = mc_manifest.is_file()
    have_romfs_patches = romfs_patches.is_dir()
    have_totk = totk_bin.is_file()

    missing = check_prereqs(
        need_mc=not args.skip_mc and not have_mc,
        need_build=not args.skip_build and not have_totk,
    )

    if args.check_only:
        stages = [
            (".venv (Python 3.12)", have_venv312),
            (".venv-py314 (Python 3.14)", have_venv314),
            ("config.local.toml", have_cfg),
            (f"build/extracted/{ver}/exefs", have_exefs),
            (f"build/xlate/{xlate_name}", have_xlate),
            ("assets/mc (Minecraft assets)", have_mc),
            (f"build/{ver}/romfs-patches", have_romfs_patches),
            (f"build/{ver}/{exe_name}", have_totk),
        ]
        for name, done in stages:
            print(f"  [{'ok' if done else 'pending'}] {name}")
        if missing:
            print("\nMissing prerequisites:")
            for item in missing:
                print(f"  - {item}")
            return 1
        print("\nPrerequisites OK.")
        return 0

    if missing:
        print("Missing prerequisites:", file=sys.stderr)
        for item in missing:
            print(f"  - {item}", file=sys.stderr)
        return 1

    # 1. Python environments
    if not have_venv312:
        run_step("Creating .venv (Python 3.12)", ["uv", "venv", "--python", "3.12", ".venv"])
        run_step(
            "Installing .venv dependencies",
            ["uv", "pip", "install", "--python", str(py312), "-r", "tools/requirements.txt"],
        )
    else:
        print("==> [skip] .venv already exists")

    if not have_venv314:
        run_step("Creating .venv-py314 (Python 3.14)", ["uv", "venv", "--python", "3.14", ".venv-py314"])
        run_step(
            "Installing .venv-py314 dependencies",
            ["uv", "pip", "install", "--python", str(py314), "-r", "tools/requirements-py314.txt"],
        )
    else:
        print("==> [skip] .venv-py314 already exists")

    # 2. Extract Minecraft Java assets
    if not args.skip_mc and not have_mc:
        mc_cmd = [str(py312), "tools/extract_mc_assets.py"]
        if args.mc_jar:
            mc_cmd += ["--jar", str(args.mc_jar)]
        if args.mc_assets_dir:
            mc_cmd += ["--assets-dir", str(args.mc_assets_dir)]
        if args.mc_index:
            mc_cmd += ["--index", str(args.mc_index)]
        run_step("Extracting Minecraft Java assets", mc_cmd)
    elif args.skip_mc:
        print("==> [skip] Minecraft asset extraction (--skip-mc)")
    else:
        print("==> [skip] assets/mc already extracted")

    if args.skip_build:
        return 0

    # 3. Extract TOTK files if needed
    if not have_exefs:
        if not have_cfg:
            raise SystemExit(
                "ERROR: config.local.toml not found. Copy config.example.toml to config.local.toml "
                "and configure your local TOTK v1.2.1 paths (see docs/building.md)."
            )
        run_step(f"Extracting TOTK {ver} files", [str(py314), "tools/extract.py"])
    else:
        print(f"==> [skip] build/extracted/{ver}/exefs already present")

    # 4. Shader translator
    if not have_xlate:
        run_step("Building shader translator (totk_xlate)", [str(py312), "tools/build_xlate.py"])
    else:
        print(f"==> [skip] build/xlate/{xlate_name} already built")

    # 5. Lift, RomFS patches, and compile
    run_step(
        f"Lifting {ver} modules",
        [
            str(py312),
            "recomp/gen.py",
            "--exefs",
            f"build/extracted/{ver}/exefs",
            "--out",
            f"generated/{ver}",
            "--patches",
            f"recomp/patches-{ver}.txt",
        ],
    )
    run_step(f"Building {ver} RomFS patches", [str(py314), "tools/make_romfs_patches.py", "--version", ver])

    build_cmd = [str(py312), "tools/build_game.py", "--version", ver]
    if args.cpu == "v2":
        build_cmd += ["--cpu", "v2"]
    run_step(f"Building build/{ver}/{exe_name}", build_cmd)

    if args.prepare_shaders:
        run_step(
            f"Preparing shaders for build/{ver}",
            [str(py314), "tools/prepare_shaders.py", f"build/{ver}", "--version", ver],
        )

    print(f"\nSetup complete. Launch with:\n  build\\{ver}\\{exe_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
