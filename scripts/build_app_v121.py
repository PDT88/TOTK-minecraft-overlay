#!/usr/bin/env python3
"""Package TOTK v1.2.1 + Minecraft Overlay into a standalone macOS .app bundle.

Assembles `<app>/Contents/` around `app/launcher.m` (compiled with
`-DTOTK_GAME_V121=1`), stages `build/totk-native-v121` plus runtime assets via
APFS copy-on-write clones (`cp -cR`, falling back to `cp -R`), optionally
extracts `AppIcon.icns`, writes `Info.plist` / `PkgInfo`, and ad-hoc signs the
bundle with `codesign`.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = ROOT / "build" / "TOTK Minecraft Overlay.app"
DEFAULT_KEYS = Path.home() / "Library" / "Application Support" / "Ryujinx" / "system" / "prod.keys"
COMPRESSED_EXTS = {".xcz", ".nsz"}
UNCOMPRESSED_EXTS = {".xci", ".nsp"}


def run_cmd(cmd: list[str], label: str, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    print(f"==> {label}")
    res = subprocess.run(cmd, cwd=cwd or ROOT, text=True)
    if res.returncode != 0:
        raise SystemExit(f"ERROR: {label} failed (exit code {res.returncode})")
    return res


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def clone_path(src: Path, dst: Path) -> None:
    """Clone src to dst using APFS copy-on-write, falling back to regular copy."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    remove_path(dst)
    if src.is_dir():
        res = subprocess.run(["cp", "-cR", str(src), str(dst)], capture_output=True, text=True)
        if res.returncode == 0:
            return
        res_fb = subprocess.run(["cp", "-R", str(src), str(dst)], capture_output=True, text=True)
        if res_fb.returncode != 0:
            raise SystemExit(f"ERROR: Failed to copy directory {src} -> {dst}: {res_fb.stderr.strip()}")
        return

    res = subprocess.run(["cp", "-cp", str(src), str(dst)], capture_output=True, text=True)
    if res.returncode == 0:
        return
    shutil.copy2(src, dst)


def find_python_for_tools() -> str:
    venv_py = ROOT / ".venv" / "bin" / "python"
    if venv_py.is_file() and os.access(venv_py, os.X_OK):
        return str(venv_py)
    return sys.executable


def find_nsz() -> str | None:
    venv_nsz = ROOT / ".venv" / "bin" / "nsz"
    if venv_nsz.is_file() and os.access(venv_nsz, os.X_OK):
        return str(venv_nsz)
    return shutil.which("nsz")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_required_inputs() -> None:
    """Fail clearly before staging if any required v1.2.1 runtime input is missing."""
    errors: list[str] = []

    native_bin = ROOT / "build" / "totk-native-v121"
    if not (native_bin.is_file() and os.access(native_bin, os.X_OK)):
        errors.append(f"Missing executable {native_bin} (run `make runtime-v121` or `python3 scripts/setup_v121.py`).")

    extracted_v121 = ROOT / "extracted" / "v121"
    if not extracted_v121.is_dir():
        errors.append(f"Missing directory {extracted_v121} (run `python3 scripts/setup_v121.py`).")
    elif not (extracted_v121 / "romfs").is_dir():
        errors.append(f"Incomplete {extracted_v121}: missing `romfs/` directory.")

    shared_fonts = ROOT / "extracted" / "firmware" / "shared-fonts"
    if not (shared_fonts.is_dir() and (shared_fonts / "manifest.json").is_file()):
        errors.append(f"Missing Switch shared fonts at {shared_fonts} (required by launcher TOTK_SHARED_FONTS).")

    rewritten_v121 = ROOT / "rewritten" / "v121"
    if not rewritten_v121.is_dir():
        errors.append(f"Missing directory {rewritten_v121} (run `python3 scripts/setup_v121.py`).")
    else:
        for mod in ("main", "sdk", "subsdk0"):
            if not (rewritten_v121 / f"{mod}.patched.bin").is_file():
                errors.append(f"Incomplete {rewritten_v121}: missing `{mod}.patched.bin`.")

    assets_mc = ROOT / "assets" / "mc"
    if not (assets_mc.is_dir() and (assets_mc / "manifest.json").is_file()):
        errors.append(f"Missing Minecraft overlay assets at {assets_mc} (run `tools/extract_mc_assets.py`).")

    launcher_src = ROOT / "app" / "launcher.m"
    if not launcher_src.is_file():
        errors.append(f"Missing launcher source {launcher_src}.")

    if errors:
        raise SystemExit("ERROR: Cannot build .app bundle:\n  - " + "\n  - ".join(errors))


def resolve_app_icon(base: Path | None, keys: Path | None) -> Path | None:
    """Return path to AppIcon.icns, reusing build/AppIcon.icns or extracting from --base."""
    cached_icon = ROOT / "build" / "AppIcon.icns"
    if cached_icon.is_file():
        print(f"==> Reusing cached icon: {cached_icon.relative_to(ROOT)}")
        return cached_icon

    if not base or not base.is_file():
        print("==> Skipping AppIcon.icns (no build/AppIcon.icns and no --base image provided)")
        return None
    if not keys or not keys.is_file():
        print(f"==> Skipping AppIcon.icns (prod.keys not found at {keys})")
        return None

    ext = base.suffix.lower()
    py = find_python_for_tools()
    extract_script = ROOT / "scripts" / "extract_app_icon.py"
    if not extract_script.is_file():
        print("==> Skipping AppIcon.icns (scripts/extract_app_icon.py not found)")
        return None

    with tempfile.TemporaryDirectory(prefix="totk-icon-") as tmp:
        tmp_dir = Path(tmp)
        game_image = base
        if ext in COMPRESSED_EXTS:
            nsz_bin = find_nsz()
            if not nsz_bin:
                print(f"==> Skipping AppIcon.icns (`nsz` not found to decompress {base.name})")
                return None
            print(f"==> Decompressing {base.name} for AppIcon extraction")
            src_resolved = base.resolve()
            if src_resolved.suffix != ext:
                in_path = tmp_dir / f"{src_resolved.stem}{ext}"
                try:
                    os.link(src_resolved, in_path)
                except OSError:
                    clone_path(src_resolved, in_path)
            else:
                in_path = src_resolved
            nsz_cmd = [nsz_bin, "-D", "-w", "-o", str(tmp_dir), "--keys", str(keys), str(in_path)]
            res = subprocess.run(nsz_cmd, cwd=ROOT, capture_output=True, text=True)
            if res.returncode != 0:
                print(f"WARN: Failed to decompress {base.name} for icon ({res.stderr.strip()}); skipping icon")
                return None
            candidates = sorted(
                p for p in tmp_dir.iterdir() if p.is_file() and p.suffix.lower() in UNCOMPRESSED_EXTS
            )
            if not candidates:
                print(f"WARN: No decompressed .xci/.nsp produced from {base.name}; skipping icon")
                return None
            game_image = candidates[0]
        elif ext not in UNCOMPRESSED_EXTS:
            print(f"WARN: Unsupported --base extension '{base.suffix}' for icon extraction; skipping icon")
            return None

        print(f"==> Extracting AppIcon.icns from {game_image.name}")
        res = subprocess.run(
            [py, str(extract_script), str(game_image), "--keys", str(keys), "--output", str(cached_icon)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if res.returncode != 0 or not cached_icon.is_file():
            detail = (res.stderr or res.stdout or "").strip()
            print(f"WARN: Icon extraction failed ({detail or 'unknown error'}); continuing without AppIcon.icns")
            return None
        return cached_icon


def compile_launcher(dst_binary: Path) -> None:
    dst_binary.parent.mkdir(parents=True, exist_ok=True)
    clang = shutil.which("xcrun")
    compiler = [clang, "clang"] if clang else ["clang"]
    cmd = [
        *compiler,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fobjc-arc",
        "-DTOTK_GAME_V121=1",
        f"-I{ROOT / 'runtime'}",
        f"-I{ROOT / 'third_party' / 'd2r_gfx' / 'gfx' / 'nvn'}",
        str(ROOT / "app" / "launcher.m"),
        "-framework",
        "AppKit",
        "-framework",
        "Foundation",
        "-o",
        str(dst_binary),
    ]
    run_cmd(cmd, f"Compiling app/launcher.m -> {dst_binary.name}")
    dst_binary.chmod(0o755)


def stage_shader_artifacts(res_build: Path) -> None:
    for tsv_name in ("pso_manifest.tsv", "pso_title.tsv"):
        src = ROOT / "build" / tsv_name
        if src.is_file():
            print(f"==> Staging build/{tsv_name}")
            clone_path(src, res_build / tsv_name)

    seed_v121 = ROOT / "build" / "shader-seed" / "v121"
    seed_root = ROOT / "build" / "shader-seed"
    if (seed_v121 / "seed-manifest.json").is_file():
        print("==> Cloning build/shader-seed/v121 -> Contents/Resources/totk/build/shader-seed")
        clone_path(seed_v121, res_build / "shader-seed")
    elif (seed_root / "seed-manifest.json").is_file():
        print("==> Cloning build/shader-seed -> Contents/Resources/totk/build/shader-seed")
        clone_path(seed_root, res_build / "shader-seed")
    elif seed_v121.is_dir() and any(seed_v121.iterdir()):
        print("==> Cloning build/shader-seed/v121 -> Contents/Resources/totk/build/shader-seed")
        clone_path(seed_v121, res_build / "shader-seed")

    shader_service = ROOT / "build" / "shader-service"
    prebuilt_xlate = ROOT / "prebuilt" / "macos-arm64" / "totk_xlate"
    if (shader_service / "totk_xlate").is_file():
        print("==> Staging build/shader-service")
        clone_path(shader_service, res_build / "shader-service")
    elif prebuilt_xlate.is_file():
        sha_file = prebuilt_xlate.with_suffix(".sha256")
        if sha_file.is_file():
            expected_sha = sha_file.read_text(encoding="utf-8").split()[0]
            actual_sha = sha256_file(prebuilt_xlate)
            if actual_sha != expected_sha:
                raise SystemExit(
                    f"ERROR: SHA-256 mismatch for {prebuilt_xlate} ({actual_sha} != {expected_sha})"
                )
        print("==> Staging prebuilt/macos-arm64/totk_xlate -> Contents/Resources/totk/build/shader-service/totk_xlate")
        dst_xlate = res_build / "shader-service" / "totk_xlate"
        clone_path(prebuilt_xlate, dst_xlate)
        dst_xlate.chmod(0o755)

    spirv_bin = ROOT / "build" / "spirv-cross" / "spirv-cross"
    spirv_dir = ROOT / "build" / "spirv-cross"
    if spirv_bin.is_file():
        print("==> Staging build/spirv-cross/spirv-cross")
        dst_sc = res_build / "spirv-cross" / "spirv-cross"
        clone_path(spirv_bin, dst_sc)
        dst_sc.chmod(0o755)
    elif spirv_dir.is_dir():
        print("==> Staging build/spirv-cross")
        clone_path(spirv_dir, res_build / "spirv-cross")


def write_bundle_metadata(contents: Path, has_icon: bool) -> None:
    plist_data: dict[str, object] = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": "TOTK Minecraft Overlay",
        "CFBundleExecutable": "TOTK",
        "CFBundleIdentifier": "org.macnativegames.totk-minecraft-overlay",
        "CFBundleName": "TOTK Minecraft Overlay",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "1.2.1",
        "CFBundleVersion": "393217",
        "GCSupportsControllerUserInteraction": True,
        "LSApplicationCategoryType": "public.app-category.action-games",
        "LSMinimumSystemVersion": "26.0",
        "LSSupportsGameMode": True,
        "NSHighResolutionCapable": True,
    }
    if has_icon:
        plist_data["CFBundleIconFile"] = "AppIcon"

    with (contents / "Info.plist").open("wb") as f:
        plistlib.dump(plist_data, f, sort_keys=True)
    (contents / "PkgInfo").write_bytes(b"APPL????")


def build_bundle(output: Path, base: Path | None, keys: Path | None) -> Path:
    verify_required_inputs()
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    icon_path = resolve_app_icon(base, keys)

    staging = output.parent / f".{output.name}.staging-{os.getpid()}"
    backup = output.parent / f".{output.name}.backup-{os.getpid()}"
    remove_path(staging)
    remove_path(backup)

    try:
        contents = staging / "Contents"
        macos_dir = contents / "MacOS"
        res_dir = contents / "Resources"
        res_totk = res_dir / "totk"
        res_build = res_totk / "build"

        macos_dir.mkdir(parents=True, exist_ok=True)
        res_build.mkdir(parents=True, exist_ok=True)

        compile_launcher(macos_dir / "TOTK")

        print("==> Staging build/totk-native-v121 -> Contents/Resources/totk/build/totk-native")
        dst_native = res_build / "totk-native"
        clone_path(ROOT / "build" / "totk-native-v121", dst_native)
        dst_native.chmod(0o755)

        print("==> Cloning assets/mc")
        clone_path(ROOT / "assets" / "mc", res_totk / "assets" / "mc")

        print("==> Cloning extracted/v121 (APFS CoW)")
        clone_path(ROOT / "extracted" / "v121", res_totk / "extracted" / "v121")

        print("==> Cloning extracted/firmware/shared-fonts")
        clone_path(ROOT / "extracted" / "firmware" / "shared-fonts", res_totk / "extracted" / "firmware" / "shared-fonts")

        print("==> Cloning rewritten/v121")
        clone_path(ROOT / "rewritten" / "v121", res_totk / "rewritten" / "v121")

        stage_shader_artifacts(res_build)

        if icon_path and icon_path.is_file():
            clone_path(icon_path, res_dir / "AppIcon.icns")

        write_bundle_metadata(contents, has_icon=bool(icon_path and icon_path.is_file()))

        if output.exists() or output.is_symlink():
            output.rename(backup)
        try:
            staging.rename(output)
            run_cmd(["codesign", "--force", "--deep", "--sign", "-", str(output)], f"Ad-hoc signing {output.name}")
            run_cmd(["codesign", "--verify", "--deep", "--strict", str(output)], f"Verifying signature of {output.name}")
            run_cmd([str(output / "Contents" / "MacOS" / "TOTK"), "--check"], f"Running launcher self-check on {output.name}")
        except BaseException:
            remove_path(output)
            if backup.exists():
                backup.rename(output)
            raise
        remove_path(backup)
    finally:
        remove_path(staging)

    print(f"\nApp bundle ready: {output}")
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--output",
        "--app-out",
        dest="output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Output .app bundle path (default: {DEFAULT_OUTPUT.relative_to(ROOT)})",
    )
    parser.add_argument(
        "--base",
        type=Path,
        default=None,
        help="Optional TOTK base image (.xci, .nsp, .xcz, or .nsz) for AppIcon extraction",
    )
    parser.add_argument(
        "--keys",
        type=Path,
        default=DEFAULT_KEYS,
        help="Path to prod.keys (default: ~/Library/Application Support/Ryujinx/system/prod.keys)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_bundle(args.output, args.base, args.keys)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
