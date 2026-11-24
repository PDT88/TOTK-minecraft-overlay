#!/usr/bin/env python3
"""End-to-end macOS ARM64 setup for TOTK v1.2.1 + Minecraft Overlay.

Runs every local extraction, rewrite, verification, Minecraft asset extraction,
optional shader seed preparation, runtime build, and optional .app bundle build
step in order, skipping stages whose verified outputs already exist so
re-running picks up where it left off.

Example:
  python3 scripts/setup_v121.py \\
    --base /path/to/totk_base.xci \\
    --update /path/to/totk_update_1.2.1.nsp \\
    --keys /path/to/prod.keys
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENV_PY = ROOT / ".venv" / "bin" / "python"
VENV_NSZ = ROOT / ".venv" / "bin" / "nsz"
HACTOOL_COMMIT = "1d64a83450e025622f3468c28fc4164dad2c5ef6"
HACTOOL_PATCH = ROOT / "tools" / "hactool-mng-macos.patch"
HACTOOL_PATCH_MARKER = b"MNG_BASE_RANGE_OFFSET"
MIN_PYTHON = (3, 14)

COMPRESSED_OUTPUT_EXT = {".nsz": ".nsp", ".xcz": ".xci"}
ALLOWED_BASE_SUFFIXES = (".xci", ".nsp", ".xcz", ".nsz")
ALLOWED_UPDATE_SUFFIXES = (".nsp", ".nsz")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run_step(label: str, cmd: list[str], env: dict[str, str] | None = None) -> None:
    print(f"\n==> {label}")
    print("    " + " ".join(cmd))
    res = subprocess.run(cmd, cwd=ROOT, env=env)
    if res.returncode != 0:
        raise SystemExit(f"ERROR: {label} failed (exit code {res.returncode})")


def is_patched_hactool(path: Path) -> bool:
    if not path.is_file() or not os.access(path, os.X_OK):
        return False
    try:
        return HACTOOL_PATCH_MARKER in path.read_bytes()
    except OSError:
        return False


def find_hactool(explicit: Path | None) -> Path | None:
    if explicit is not None:
        if not explicit.is_file():
            return None
        if not os.access(explicit, os.X_OK):
            raise ValueError(f"--hactool binary is not executable: {explicit}")
        try:
            data = explicit.read_bytes()
        except OSError as exc:
            raise ValueError(f"Unable to read --hactool binary {explicit}: {exc}") from exc
        if HACTOOL_PATCH_MARKER not in data:
            raise ValueError(
                f"--hactool binary at {explicit} is unpatched (missing {HACTOOL_PATCH_MARKER.decode('ascii')}); "
                "omit --hactool to auto-build patched hactool under tooling/hactool/hactool"
            )
        return explicit

    found = shutil.which("hactool")
    if found and is_patched_hactool(Path(found)):
        return Path(found)
    for cand in (
        ROOT / "tooling" / "hactool" / "hactool",
        ROOT / "tooling" / "hactool",
        ROOT.parent / "src" / "hactool" / "hactool",
        Path("/opt/homebrew/bin/hactool"),
        Path("/usr/local/bin/hactool"),
    ):
        if is_patched_hactool(cand):
            return cand
    return None


def find_spirv_cross() -> Path | None:
    built = ROOT / "build" / "spirv-cross" / "spirv-cross"
    if built.is_file() and os.access(built, os.X_OK):
        return built
    found = shutil.which("spirv-cross")
    if found:
        return Path(found)
    for cand in (Path("/opt/homebrew/bin/spirv-cross"), Path("/usr/local/bin/spirv-cross")):
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand
    return None


def find_ffmpeg() -> Path | None:
    found = shutil.which("ffmpeg")
    if found:
        return Path(found)
    for cand in (Path("/opt/homebrew/bin/ffmpeg"), Path("/usr/local/bin/ffmpeg")):
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand
    return None


def find_brew() -> str | None:
    found = shutil.which("brew")
    if found:
        return found
    for cand in ("/opt/homebrew/bin/brew", "/usr/local/bin/brew"):
        if Path(cand).is_file() and os.access(cand, os.X_OK):
            return cand
    return None


def python_version(executable: str | Path) -> tuple[int, int] | None:
    path = Path(executable)
    if not path.is_file() or not os.access(path, os.X_OK):
        return None
    try:
        if path.resolve() == Path(sys.executable).resolve():
            return (sys.version_info[0], sys.version_info[1])
    except OSError:
        pass
    try:
        res = subprocess.run(
            [str(path), "-c", "import sys; print('%d.%d' % (sys.version_info[0], sys.version_info[1]))"],
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if res.returncode != 0:
        return None
    parts = res.stdout.strip().split(".")
    if len(parts) != 2:
        return None
    try:
        return (int(parts[0]), int(parts[1]))
    except ValueError:
        return None


def find_python314() -> str | None:
    candidates: list[str] = []
    if sys.version_info[:2] >= MIN_PYTHON:
        candidates.append(sys.executable)
    for item in (
        shutil.which("python3.14"),
        shutil.which("python3"),
        "/opt/homebrew/opt/python@3.14/bin/python3.14",
        "/opt/homebrew/bin/python3.14",
        "/usr/local/bin/python3.14",
        "/Library/Frameworks/Python.framework/Versions/3.14/bin/python3.14",
    ):
        if item:
            candidates.append(item)

    seen: set[str] = set()
    for cand in candidates:
        if cand in seen:
            continue
        seen.add(cand)
        ver = python_version(cand)
        if ver is not None and ver >= MIN_PYTHON:
            return cand
    return None


def build_hactool() -> Path:
    """Clone pinned SciresM/hactool, apply MNG macOS BKTR/range patch, and build into tooling/hactool."""
    hactool_dir = ROOT / "tooling" / "hactool"
    hactool_bin = hactool_dir / "hactool"
    if is_patched_hactool(hactool_bin):
        return hactool_bin
    if hactool_dir.exists():
        shutil.rmtree(hactool_dir)
    hactool_dir.mkdir(parents=True, exist_ok=True)
    run_step("Initializing hactool repo", ["git", "-C", str(hactool_dir), "init"])
    run_step(
        "Fetching pinned SciresM/hactool",
        ["git", "-C", str(hactool_dir), "fetch", "--depth", "1", "https://github.com/SciresM/hactool.git", HACTOOL_COMMIT],
    )
    run_step("Checking out pinned hactool commit", ["git", "-C", str(hactool_dir), "checkout", "FETCH_HEAD"])
    if not HACTOOL_PATCH.is_file():
        raise SystemExit(f"ERROR: Missing required hactool patch: {HACTOOL_PATCH}")
    run_step("Applying MNG macOS hactool patch", ["git", "-C", str(hactool_dir), "apply", str(HACTOOL_PATCH)])
    shutil.copyfile(hactool_dir / "config.mk.template", hactool_dir / "config.mk")
    jobs = str(os.cpu_count() or 4)
    run_step("Building hactool", ["make", "-C", str(hactool_dir), f"-j{jobs}"])
    if not is_patched_hactool(hactool_bin):
        raise SystemExit(f"ERROR: hactool build succeeded but patched binary missing at {hactool_bin}")
    return hactool_bin


def stage_shader_tools() -> None:
    """Stage prebuilt totk_xlate and spirv-cross into build/ where totk_shader_rt.m expects them."""
    dst_xlate = ROOT / "build" / "shader-service" / "totk_xlate"
    src_xlate = ROOT / "prebuilt" / "macos-arm64" / "totk_xlate"
    sha_file = ROOT / "prebuilt" / "macos-arm64" / "totk_xlate.sha256"
    if not (dst_xlate.is_file() and os.access(dst_xlate, os.X_OK)):
        if not src_xlate.is_file():
            raise SystemExit(f"ERROR: Missing prebuilt shader translator: {src_xlate}")
        if sha_file.is_file():
            expected_sha = sha_file.read_text(encoding="utf-8").split()[0]
            actual_sha = sha256_file(src_xlate)
            if actual_sha != expected_sha:
                raise SystemExit(f"ERROR: SHA-256 mismatch for {src_xlate} ({actual_sha} != {expected_sha})")
        dst_xlate.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_xlate, dst_xlate)
        dst_xlate.chmod(0o755)
        print(f"==> Staged {dst_xlate.relative_to(ROOT)} from {src_xlate.relative_to(ROOT)}")

    dst_sc = ROOT / "build" / "spirv-cross" / "spirv-cross"
    if not (dst_sc.is_file() and os.access(dst_sc, os.X_OK)):
        src_sc = find_spirv_cross()
        if not src_sc:
            raise SystemExit("ERROR: spirv-cross not found (`brew install spirv-cross`)")
        dst_sc.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_sc, dst_sc)
        dst_sc.chmod(0o755)
        print(f"==> Staged {dst_sc.relative_to(ROOT)} from {src_sc}")


def missing_brew_formulas(need_mc: bool, need_llvm: bool, need_spirv_cross: bool) -> list[str]:
    formulas: list[str] = []
    if find_python314() is None:
        formulas.append("python@3.14")
    if not Path("/opt/homebrew/opt/opus/lib/libopus.a").is_file():
        formulas.append("opus")
    if not Path("/opt/homebrew/opt/libvpx/lib/libvpx.a").is_file():
        formulas.append("libvpx")
    llvm_bin = Path(os.environ.get("LLVM_BIN", "/opt/homebrew/opt/llvm/bin"))
    if need_llvm and not llvm_bin.is_dir():
        formulas.append("llvm")
    if need_spirv_cross and not find_spirv_cross():
        formulas.append("spirv-cross")
    if need_mc and not find_ffmpeg():
        formulas.append("ffmpeg")
    return formulas


def install_homebrew_deps(need_mc: bool, need_llvm: bool, need_spirv_cross: bool) -> None:
    formulas = missing_brew_formulas(need_mc=need_mc, need_llvm=need_llvm, need_spirv_cross=need_spirv_cross)
    if not formulas:
        print("==> [skip] Required Homebrew dependencies already installed")
        return
    brew = find_brew()
    if not brew:
        raise SystemExit(
            "ERROR: Homebrew (`brew`) not found. Install Homebrew from https://brew.sh, "
            f"then run: brew install {' '.join(formulas)}"
        )
    run_step(f"Installing Homebrew dependencies ({', '.join(formulas)})", [brew, "install", *formulas])


def check_prereqs(
    need_hactool: bool, hactool_arg: Path | None, need_mc: bool, need_llvm: bool, need_spirv_cross: bool
) -> tuple[list[str], Path | None]:
    missing: list[str] = []
    if find_python314() is None:
        missing.append("Python >= 3.14 (`brew install python@3.14` or pass `--install-deps`)")
    if not Path("/opt/homebrew/opt/opus/lib/libopus.a").is_file():
        missing.append("Homebrew opus (`brew install opus` or pass `--install-deps`)")
    if not Path("/opt/homebrew/opt/libvpx/lib/libvpx.a").is_file():
        missing.append("Homebrew libvpx (`brew install libvpx` or pass `--install-deps`)")
    llvm_bin = Path(os.environ.get("LLVM_BIN", "/opt/homebrew/opt/llvm/bin"))
    if need_llvm and not llvm_bin.is_dir():
        missing.append("Homebrew llvm (`brew install llvm`, pass `--install-deps`, or set LLVM_BIN)")
    if need_spirv_cross and not find_spirv_cross():
        missing.append("spirv-cross (`brew install spirv-cross` or pass `--install-deps`)")
    if need_mc and not find_ffmpeg():
        missing.append("ffmpeg (`brew install ffmpeg` or pass `--install-deps`)")

    hactool: Path | None = None
    try:
        hactool = find_hactool(hactool_arg)
    except ValueError as exc:
        missing.append(str(exc))
    else:
        if hactool_arg is not None and not hactool and need_hactool:
            missing.append(f"hactool not found at {hactool_arg}")
    return missing, hactool


def is_compressed_dump(path: Path) -> bool:
    return path.suffix.lower() in COMPRESSED_OUTPUT_EXT


def find_nsz_cmd(py_exe: str) -> list[str]:
    if VENV_NSZ.is_file() and os.access(VENV_NSZ, os.X_OK):
        return [str(VENV_NSZ)]
    run_step("Installing Python requirements (nsz)", [py_exe, "-m", "pip", "install", "-r", "requirements.txt"])
    if VENV_NSZ.is_file() and os.access(VENV_NSZ, os.X_OK):
        return [str(VENV_NSZ)]
    return [py_exe, "-m", "nsz"]


def decompress_dump(src: Path, role: str, tmp_root: Path, py_exe: str, keys: Path) -> Path:
    suffix_lower = src.suffix.lower()
    if suffix_lower not in COMPRESSED_OUTPUT_EXT:
        return src
    if not keys.is_file():
        raise SystemExit(
            f"ERROR: prod.keys not found at {keys} (required to decompress {src.name}; pass --keys)."
        )
    target_ext = COMPRESSED_OUTPUT_EXT[suffix_lower]
    work_dir = tmp_root / role
    work_dir.mkdir(parents=True, exist_ok=True)
    # nsz resolves symlinks and compares extensions case-sensitively against '.nsz'/'.xcz'.
    # If the resolved filename uses uppercase/mixed-case suffix, expose a lowercase alias via hardlink/APFS clone.
    src_resolved = src.resolve()
    if src_resolved.suffix != suffix_lower:
        in_path = work_dir / f"{src_resolved.stem}{suffix_lower}"
        if not in_path.exists():
            try:
                os.link(src_resolved, in_path)
            except OSError:
                if subprocess.run(["cp", "-c", str(src_resolved), str(in_path)]).returncode != 0:
                    shutil.copyfile(src_resolved, in_path)
    else:
        in_path = src_resolved

    nsz_cmd = find_nsz_cmd(py_exe)
    run_step(
        f"Decompressing {role} ({src.name} -> {target_ext})",
        [*nsz_cmd, "-D", "-w", "-o", str(work_dir), "--keys", str(keys), str(in_path)],
    )
    expected = work_dir / f"{in_path.stem}{target_ext}"
    if expected.is_file():
        return expected
    found = sorted(work_dir.glob(f"*{target_ext}"))
    if found:
        return found[0]
    raise SystemExit(f"ERROR: nsz finished but decompressed {target_ext} was not found in {work_dir}")


def resolve_firmware_dir(firmware: Path, tmp_root: Path) -> Path:
    if firmware.is_dir():
        return firmware
    if firmware.is_file() and firmware.suffix.lower() == ".zip":
        fw_dir = tmp_root / "firmware"
        fw_dir.mkdir(parents=True, exist_ok=True)
        fw_root = fw_dir.resolve()
        with zipfile.ZipFile(firmware, "r") as zf:
            for member in zf.infolist():
                target = (fw_dir / member.filename).resolve()
                if target != fw_root and fw_root not in target.parents:
                    raise SystemExit(f"ERROR: Unsafe path in firmware archive: {member.filename}")
            zf.extractall(fw_dir)
        has_nca = any(
            (p.is_dir() and (p / "00").is_file()) or (p.is_file() and p.suffix.lower() == ".nca")
            for p in fw_dir.iterdir()
        )
        if not has_nca:
            subdirs = [p for p in fw_dir.iterdir() if p.is_dir() and not p.name.startswith("__MACOSX")]
            if len(subdirs) == 1:
                return subdirs[0]
        return fw_dir
    raise SystemExit(f"ERROR: --firmware must be an unpacked NCA directory or .zip archive: {firmware}")


def ensure_venv() -> str:
    venv_dir = ROOT / ".venv"
    if VENV_PY.is_file():
        ver = python_version(VENV_PY)
        if ver is not None and ver >= MIN_PYTHON:
            return str(VENV_PY)
        print(f"==> Recreating .venv (found Python {ver}, require >= {MIN_PYTHON[0]}.{MIN_PYTHON[1]} for compression.zstd)")
        if venv_dir.is_symlink():
            venv_dir.unlink()
        elif venv_dir.exists():
            shutil.rmtree(venv_dir)

    py_base = find_python314()
    if py_base is None:
        raise SystemExit("ERROR: Python >= 3.14 not found (`brew install python@3.14` or pass `--install-deps`).")
    run_step(f"Creating .venv ({py_base})", [py_base, "-m", "venv", str(venv_dir)])
    run_step("Installing Python requirements", [str(VENV_PY), "-m", "pip", "install", "-r", "requirements.txt"])
    return str(VENV_PY)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", type=Path, help="TOTK base 1.0.0 image (.xci, .nsp, .xcz, or .nsz)")
    parser.add_argument("--update", type=Path, help="TOTK v1.2.1 update package (.nsp or .nsz)")
    parser.add_argument(
        "--keys",
        type=Path,
        default=Path.home() / "Library" / "Application Support" / "Ryujinx" / "system" / "prod.keys",
        help="Path to prod.keys",
    )
    parser.add_argument(
        "--firmware",
        type=Path,
        help="Unpacked Switch firmware NCA directory or .zip (required for shared fonts when --base is .nsp/.nsz)",
    )
    parser.add_argument("--hactool", type=Path, help="Path to patched hactool binary (auto-detected/built if omitted)")
    parser.add_argument("--mc-dir", type=Path, help="Optional Minecraft directory (overrides MINECRAFT_DIR)")
    parser.add_argument("--mc-jar", type=Path, help="Optional explicit Minecraft 1.21+ client JAR")
    parser.add_argument("--jobs", type=int, default=2, help="Parallel jobs for verify_rewrites.py (default: 2)")
    parser.add_argument("--skip-mc", action="store_true", help="Skip Minecraft asset extraction")
    parser.add_argument("--skip-build", action="store_true", help="Skip final `make runtime-v121` build")
    parser.add_argument("--install-deps", action="store_true", help="Install missing Homebrew dependencies before checking prerequisites")
    parser.add_argument("--prepare-shaders", action="store_true", help="Prepare startup shader seed into build/shader-seed")
    parser.add_argument("--build-app", action="store_true", help="Build standalone macOS .app bundle via scripts/build_app_v121.py")
    parser.add_argument("--app-out", type=Path, help="Output .app bundle path when --build-app is set")
    parser.add_argument("--check-only", action="store_true", help="Check prerequisites and stage status without running")
    args = parser.parse_args()

    exefs_dir = ROOT / "extracted" / "v121" / "exefs"
    exefs_manifest = ROOT / "extracted" / "v121" / "extraction-manifest.json"
    elf_dir = ROOT / "extracted" / "v121" / "elf"
    romfs_index = ROOT / "extracted" / "v121" / "romfs" / "index.json"
    romfs_patches_manifest = ROOT / "extracted" / "v121" / "romfs-patches" / "manifest.tsv"
    res_overlay_manifest = ROOT / "extracted" / "v121" / "romfs-patches-out" / "2560x1440" / "manifest.tsv"
    fonts_manifest = ROOT / "extracted" / "firmware" / "shared-fonts" / "manifest.json"
    rewritten_dir = ROOT / "rewritten" / "v121"
    build_src_sha = rewritten_dir / "build_src.sha256"
    patch_nso = ROOT / "third_party" / "d2r" / "patch_nso.py"
    phase12_report = ROOT / "analysis" / "v121-port" / "phase12-validation.json"
    shader_xlate = ROOT / "build" / "shader-service" / "totk_xlate"
    shader_sc = ROOT / "build" / "spirv-cross" / "spirv-cross"
    shader_seed_direct = ROOT / "build" / "shader-seed" / "seed-manifest.json"
    shader_seed_nested = ROOT / "build" / "shader-seed" / "v121" / "seed-manifest.json"
    mc_manifest = ROOT / "assets" / "mc" / "manifest.json"
    binary = ROOT / "build" / "totk-native-v121"

    have_exefs = exefs_manifest.is_file() and all((exefs_dir / m).is_file() for m in ("main", "sdk", "subsdk0"))
    have_elf = all((elf_dir / f"{m}-inventory.json").is_file() for m in ("main", "sdk", "subsdk0"))
    have_romfs = romfs_index.is_file()
    have_overlays = romfs_patches_manifest.is_file() and res_overlay_manifest.is_file()
    have_fonts = fonts_manifest.is_file()
    expected_patch_sha = sha256_file(patch_nso)
    have_rewrites = (
        build_src_sha.is_file()
        and build_src_sha.read_text(encoding="utf-8").split()[0] == expected_patch_sha
        and all((rewritten_dir / f"{m}.patched.bin").is_file() for m in ("main", "sdk", "subsdk0"))
    )
    have_verified = phase12_report.is_file() and have_rewrites
    have_shader_tools = (
        shader_xlate.is_file() and os.access(shader_xlate, os.X_OK)
        and shader_sc.is_file() and os.access(shader_sc, os.X_OK)
    )
    have_shader_seed = shader_seed_direct.is_file() or shader_seed_nested.is_file()
    have_mc = mc_manifest.is_file()

    standalone_install = (
        args.install_deps
        and not args.check_only
        and args.base is None
        and args.update is None
        and not args.prepare_shaders
        and not args.build_app
    )

    if args.install_deps:
        install_homebrew_deps(
            need_mc=not args.skip_mc and (not have_mc or standalone_install),
            need_llvm=not have_verified or standalone_install,
            need_spirv_cross=(not (shader_sc.is_file() and os.access(shader_sc, os.X_OK))) or standalone_install,
        )

    missing, hactool = check_prereqs(
        need_hactool=not have_romfs,
        hactool_arg=args.hactool,
        need_mc=not args.skip_mc and (not have_mc or standalone_install),
        need_llvm=not have_verified or standalone_install,
        need_spirv_cross=(not (shader_sc.is_file() and os.access(shader_sc, os.X_OK))) or standalone_install,
    )

    if args.check_only or standalone_install:
        if args.check_only:
            stages = [
                ("v1.2.1 ExeFS", have_exefs),
                ("v1.2.1 ELF inventory", have_elf),
                ("v1.2.1 RomFS", have_romfs),
                ("v1.2.1 RomFS overlays", have_overlays),
                ("Shared fonts", have_fonts),
                ("Rewritten NSOs", have_rewrites),
                ("Phase 1+2 verification", have_verified),
                ("Runtime shader tools", have_shader_tools),
                ("Startup shader seed (optional)", have_shader_seed),
                ("Minecraft assets", have_mc),
                ("build/totk-native-v121", binary.is_file()),
            ]
            for name, done in stages:
                print(f"  [{'ok' if done else 'pending'}] {name}")
        if missing:
            print("\nMissing prerequisites:" if args.check_only else "Missing prerequisites:", file=sys.stdout if args.check_only else sys.stderr)
            for item in missing:
                print(f"  - {item}", file=sys.stdout if args.check_only else sys.stderr)
            return 1
        print("\nPrerequisites OK." if args.check_only else "Prerequisites OK.")
        return 0

    if missing:
        print("Missing prerequisites:", file=sys.stderr)
        for item in missing:
            print(f"  - {item}", file=sys.stderr)
        return 1

    if args.base is not None:
        if not args.base.is_file():
            raise SystemExit(f"ERROR: --base file not found: {args.base}")
        if args.base.suffix.lower() not in ALLOWED_BASE_SUFFIXES:
            raise SystemExit(
                f"ERROR: --base must be an {', '.join(ALLOWED_BASE_SUFFIXES)} file (got {args.base.name})."
            )
    if args.update is not None:
        if not args.update.is_file():
            raise SystemExit(f"ERROR: --update file not found: {args.update}")
        if args.update.suffix.lower() not in ALLOWED_UPDATE_SUFFIXES:
            raise SystemExit(
                f"ERROR: --update must be an {', '.join(ALLOWED_UPDATE_SUFFIXES)} file (got {args.update.name})."
            )

    # 0. Python venv + dependencies (require Python >= 3.14 for compression.zstd)
    py = ensure_venv()

    with tempfile.TemporaryDirectory(prefix="totk-setup-") as tmp_dir:
        tmp_root = Path(tmp_dir)
        effective_base: Path | None = None
        effective_update: Path | None = None

        def get_effective_base() -> Path:
            nonlocal effective_base
            if effective_base is not None:
                return effective_base
            if not args.base or not args.base.is_file():
                raise SystemExit("ERROR: --base <totk_base.xci|.nsp|.xcz|.nsz> is required.")
            effective_base = decompress_dump(args.base, "base", tmp_root, py, args.keys)
            return effective_base

        def get_effective_update() -> Path:
            nonlocal effective_update
            if effective_update is not None:
                return effective_update
            if not args.update or not args.update.is_file():
                raise SystemExit("ERROR: --update <totk_update_1.2.1.nsp|.nsz> is required.")
            effective_update = decompress_dump(args.update, "update", tmp_root, py, args.keys)
            return effective_update

        # 1. Extract v1.2.1 ExeFS
        if not have_exefs:
            if not args.update or not args.update.is_file():
                raise SystemExit("ERROR: --update <totk_update_1.2.1.nsp|.nsz> is required to extract v1.2.1 ExeFS.")
            if not args.keys.is_file():
                raise SystemExit(f"ERROR: prod.keys not found at {args.keys} (pass --keys).")
            update_nsp = get_effective_update()
            if exefs_dir.exists():
                shutil.rmtree(exefs_dir)
            run_step(
                "Extracting TOTK v1.2.1 ExeFS",
                [py, "scripts/extract_exefs.py", str(update_nsp), "--update", "--keys", str(args.keys), "--output", str(exefs_dir)],
            )
        else:
            print("==> [skip] v1.2.1 ExeFS already extracted")

        # 1b. Prepare v1.2.1 ELF inventories (needed by prepare_runtime.py)
        if not have_elf:
            run_step(
                "Preparing TOTK v1.2.1 module inventories",
                [py, "scripts/prepare_modules.py", str(exefs_dir), "--output", str(elf_dir)],
            )
        else:
            print("==> [skip] v1.2.1 ELF inventory already prepared")

        # 2. Extract & merge v1.2.1 RomFS
        if not have_romfs:
            if not args.base or not args.base.is_file():
                raise SystemExit("ERROR: --base <totk_base.xci|.nsp|.xcz|.nsz> is required to extract v1.2.1 RomFS.")
            if not args.update or not args.update.is_file():
                raise SystemExit("ERROR: --update <totk_update_1.2.1.nsp|.nsz> is required to extract v1.2.1 RomFS.")
            if not args.keys.is_file():
                raise SystemExit(f"ERROR: prod.keys not found at {args.keys} (pass --keys).")
            base_img = get_effective_base()
            update_nsp = get_effective_update()
            if hactool is None:
                hactool = build_hactool()
            run_step(
                "Extracting and merging TOTK v1.2.1 RomFS",
                [
                    py,
                    "scripts/extract_update_romfs.py",
                    str(base_img),
                    str(update_nsp),
                    "--keys",
                    str(args.keys),
                    "--hactool",
                    str(hactool),
                    "--output",
                    str(ROOT / "extracted" / "v121" / "romfs"),
                ],
            )
        else:
            print("==> [skip] v1.2.1 RomFS already extracted")

        # 2b. Build v1.2.1 RomFS patches & 1440p heap overlay
        if not romfs_patches_manifest.is_file():
            run_step("Building v1.2.1 RomFS patches", [py, "scripts/make_romfs_patches.py", "--version", "v121"])
        else:
            print("==> [skip] v1.2.1 RomFS patches already built")

        if not res_overlay_manifest.is_file():
            run_step(
                "Building v1.2.1 2560x1440 heap overlay",
                [py, "scripts/make_output_res_overlay.py", "2560x1440", "--version", "v121"],
            )
        else:
            print("==> [skip] v1.2.1 2560x1440 heap overlay already built")

        # 3. Extract Switch shared fonts
        if not have_fonts:
            if not args.keys.is_file():
                raise SystemExit(f"ERROR: prod.keys not found at {args.keys} (pass --keys).")
            font_cmd = [py, "scripts/extract_shared_fonts.py", "--keys", str(args.keys)]
            if args.firmware:
                fw_dir = resolve_firmware_dir(args.firmware, tmp_root)
                font_cmd += ["--firmware", str(fw_dir)]
            elif args.base and args.base.is_file() and args.base.suffix.lower() in (".xci", ".xcz"):
                base_img = get_effective_base()
                if base_img.suffix.lower() != ".xci":
                    raise SystemExit(
                        "ERROR: Shared fonts require either an .xci/.xcz --base image or --firmware <dir|zip>."
                    )
                font_cmd += ["--xci", str(base_img)]
            else:
                raise SystemExit(
                    "ERROR: Shared fonts require either an .xci/.xcz --base image or --firmware <unpacked_firmware_nca_dir|zip>."
                )
            run_step("Extracting Switch shared fonts", font_cmd)
        else:
            print("==> [skip] Switch shared fonts already extracted")

        # 4. Rewrite NSOs into rewritten/v121
        if not have_rewrites:
            rewritten_dir.mkdir(parents=True, exist_ok=True)
            rw_env = os.environ.copy()
            rw_env["DM_RULESET"] = "native"
            rw_env["DM_PRIVATE_GATES"] = "0"
            for mod in ("sdk", "subsdk0", "main"):
                run_step(
                    f"Rewriting v1.2.1 {mod}",
                    [py, "third_party/d2r/patch_nso.py", str(exefs_dir / mod), str(rewritten_dir), mod],
                    env=rw_env,
                )
            build_src_sha.write_text(f"{expected_patch_sha}  third_party/d2r/patch_nso.py\n", encoding="utf-8")
        else:
            print("==> [skip] v1.2.1 modules already rewritten")

        # 5. Verify rewrites (generates analysis/v121-port/phase12-validation.json)
        if not have_verified:
            run_step(
                "Verifying v1.2.1 rewrites (phase 1+2 validation)",
                [py, "scripts/verify_rewrites.py", "--version", "v121", "--jobs", str(args.jobs)],
            )
        else:
            print("==> [skip] v1.2.1 phase 1+2 verification already complete")

        # 6. Extract Minecraft Java 1.21+ assets
        if not args.skip_mc and not have_mc:
            mc_env = os.environ.copy()
            if args.mc_dir:
                mc_env["MINECRAFT_DIR"] = str(args.mc_dir)
            mc_cmd = [py, "tools/extract_mc_assets.py"]
            if args.mc_jar:
                mc_cmd += ["--jar", str(args.mc_jar)]
            run_step("Extracting Minecraft Java assets", mc_cmd, env=mc_env)
        elif args.skip_mc:
            print("==> [skip] Minecraft asset extraction (--skip-mc)")
        else:
            print("==> [skip] Minecraft assets already extracted")

        # 6b. Stage runtime shader tools (totk_xlate + spirv-cross)
        if not have_shader_tools:
            stage_shader_tools()
        else:
            print("==> [skip] Runtime shader tools already staged")

        # 6c. Optional startup shader seed preparation
        if args.prepare_shaders:
            if not romfs_index.is_file():
                raise SystemExit(f"ERROR: Missing verified RomFS index for shader preparation: {romfs_index}")
            if not (rewritten_dir / "main.patched.bin").is_file():
                raise SystemExit(f"ERROR: Missing rewritten modules in {rewritten_dir} for shader preparation")
            if not (shader_xlate.is_file() and shader_sc.is_file()):
                raise SystemExit("ERROR: Missing runtime shader tools (totk_xlate / spirv-cross)")
            shader_seed_out = (
                ROOT / "build" / "shader-seed" / "v121"
                if shader_seed_nested.is_file() and not shader_seed_direct.is_file()
                else ROOT / "build" / "shader-seed"
            )
            run_step(
                "Preparing v1.2.1 startup shader seed",
                [
                    py,
                    "scripts/prepare_shader_seed.py",
                    "--romfs",
                    str(ROOT / "extracted" / "v121" / "romfs"),
                    "--output",
                    str(shader_seed_out),
                    "--translator",
                    str(shader_xlate),
                    "--spirv-cross",
                    str(shader_sc),
                    "--version",
                    "v121",
                    "--modules",
                    str(rewritten_dir),
                    "--game",
                    "totk",
                ],
            )

        # 7. Build runtime-v121
        if not args.skip_build:
            run_step("Building v1.2.1 native runtime", ["make", "runtime-v121"])

        # 8. Optional standalone macOS .app bundle
        if args.build_app:
            icon_cache = ROOT / "build" / "AppIcon.icns"
            if args.base and not icon_cache.is_file():
                if not is_compressed_dump(args.base):
                    effective_base = args.base
                elif args.keys.is_file():
                    effective_base = get_effective_base()
            elif args.base and effective_base is None and not is_compressed_dump(args.base):
                effective_base = args.base

            app_cmd = [py, "scripts/build_app_v121.py"]
            if args.app_out:
                app_cmd += ["--output", str(args.app_out)]
            if effective_base:
                app_cmd += ["--base", str(effective_base)]
            if args.keys.is_file():
                app_cmd += ["--keys", str(args.keys)]
            run_step("Building macOS .app bundle", app_cmd)

    if args.build_app:
        app_path = args.app_out or (ROOT / "build" / "TOTK Minecraft Overlay.app")
        print(f"\nSetup complete. Launch:\n  open \"{app_path}\"\n  (or: make run-v121)")
    else:
        print("\nSetup complete. Launch with:\n  make run-v121")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
