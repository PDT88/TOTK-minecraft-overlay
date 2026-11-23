#!/usr/bin/env python3
"""End-to-end macOS ARM64 setup for TOTK v1.2.1 + Minecraft Overlay.

Runs every local extraction, rewrite, verification, Minecraft asset extraction,
and runtime build step in order, skipping stages whose verified outputs already
exist so re-running picks up where it left off.

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
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENV_PY = ROOT / ".venv" / "bin" / "python"


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


def find_hactool(explicit: Path | None) -> Path | None:
    if explicit:
        return explicit if explicit.is_file() else None
    found = shutil.which("hactool")
    if found:
        return Path(found)
    for cand in (
        ROOT / "tooling" / "hactool" / "hactool",
        ROOT / "tooling" / "hactool",
        ROOT.parent / "src" / "hactool" / "hactool",
        Path("/opt/homebrew/bin/hactool"),
        Path("/usr/local/bin/hactool"),
    ):
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand
    return None


def check_prereqs(
    need_hactool: bool, hactool_arg: Path | None, need_mc: bool, need_llvm: bool
) -> tuple[list[str], Path | None]:
    missing: list[str] = []
    if not Path("/opt/homebrew/opt/opus/lib/libopus.a").is_file():
        missing.append("Homebrew opus (`brew install opus`)")
    if not Path("/opt/homebrew/opt/libvpx/lib/libvpx.a").is_file():
        missing.append("Homebrew libvpx (`brew install libvpx`)")
    llvm_bin = Path(os.environ.get("LLVM_BIN", "/opt/homebrew/opt/llvm/bin"))
    if need_llvm and not llvm_bin.is_dir():
        missing.append("Homebrew llvm (`brew install llvm`, or set LLVM_BIN)")
    if need_mc and not (
        shutil.which("ffmpeg")
        or Path("/opt/homebrew/bin/ffmpeg").is_file()
        or Path("/usr/local/bin/ffmpeg").is_file()
    ):
        missing.append("ffmpeg (`brew install ffmpeg`)")
    hactool = find_hactool(hactool_arg)
    if need_hactool and not hactool:
        missing.append("hactool (install hactool on PATH or pass --hactool /path/to/hactool)")
    return missing, hactool


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", type=Path, help="TOTK base 1.0.0 image (.xci or .nsp)")
    parser.add_argument("--update", type=Path, help="TOTK v1.2.1 update package (.nsp)")
    parser.add_argument(
        "--keys",
        type=Path,
        default=Path.home() / "Library" / "Application Support" / "Ryujinx" / "system" / "prod.keys",
        help="Path to prod.keys",
    )
    parser.add_argument(
        "--firmware",
        type=Path,
        help="Unpacked Switch firmware NCA directory (required for shared fonts when --base is an .nsp)",
    )
    parser.add_argument("--hactool", type=Path, help="Path to hactool binary (auto-detected if omitted)")
    parser.add_argument("--mc-dir", type=Path, help="Optional Minecraft directory (overrides MINECRAFT_DIR)")
    parser.add_argument("--mc-jar", type=Path, help="Optional explicit Minecraft 1.21+ client JAR")
    parser.add_argument("--jobs", type=int, default=2, help="Parallel jobs for verify_rewrites.py (default: 2)")
    parser.add_argument("--skip-mc", action="store_true", help="Skip Minecraft asset extraction")
    parser.add_argument("--skip-build", action="store_true", help="Skip final `make runtime-v121` build")
    parser.add_argument("--check-only", action="store_true", help="Check prerequisites and stage status without running")
    args = parser.parse_args()

    exefs_dir = ROOT / "extracted" / "v121" / "exefs"
    exefs_manifest = ROOT / "extracted" / "v121" / "extraction-manifest.json"
    romfs_index = ROOT / "extracted" / "v121" / "romfs" / "index.json"
    fonts_manifest = ROOT / "extracted" / "firmware" / "shared-fonts" / "manifest.json"
    rewritten_dir = ROOT / "rewritten" / "v121"
    build_src_sha = rewritten_dir / "build_src.sha256"
    patch_nso = ROOT / "third_party" / "d2r" / "patch_nso.py"
    phase12_report = ROOT / "analysis" / "v121-port" / "phase12-validation.json"
    mc_manifest = ROOT / "assets" / "mc" / "manifest.json"
    binary = ROOT / "build" / "totk-native-v121"

    have_exefs = exefs_manifest.is_file() and all((exefs_dir / m).is_file() for m in ("main", "sdk", "subsdk0"))
    have_romfs = romfs_index.is_file()
    have_fonts = fonts_manifest.is_file()
    expected_patch_sha = sha256_file(patch_nso)
    have_rewrites = (
        build_src_sha.is_file()
        and build_src_sha.read_text(encoding="utf-8").split()[0] == expected_patch_sha
        and all((rewritten_dir / f"{m}.patched.bin").is_file() for m in ("main", "sdk", "subsdk0"))
    )
    have_verified = phase12_report.is_file() and have_rewrites
    have_mc = mc_manifest.is_file()

    missing, hactool = check_prereqs(
        need_hactool=not have_romfs,
        hactool_arg=args.hactool,
        need_mc=not args.skip_mc and not have_mc,
        need_llvm=not have_verified,
    )

    if args.check_only:
        stages = [
            ("v1.2.1 ExeFS", have_exefs),
            ("v1.2.1 RomFS", have_romfs),
            ("Shared fonts", have_fonts),
            ("Rewritten NSOs", have_rewrites),
            ("Phase 1+2 verification", have_verified),
            ("Minecraft assets", have_mc),
            ("build/totk-native-v121", binary.is_file()),
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

    # 0. Python venv + dependencies
    if not VENV_PY.is_file():
        run_step("Installing Python virtual environment", ["make", "dependencies"])

    py = str(VENV_PY)

    # 1. Extract v1.2.1 ExeFS
    if not have_exefs:
        if not args.update or not args.update.is_file():
            raise SystemExit("ERROR: --update <totk_update_1.2.1.nsp> is required to extract v1.2.1 ExeFS.")
        if not args.keys.is_file():
            raise SystemExit(f"ERROR: prod.keys not found at {args.keys} (pass --keys).")
        if exefs_dir.exists():
            shutil.rmtree(exefs_dir)
        run_step(
            "Extracting TOTK v1.2.1 ExeFS",
            [py, "scripts/extract_exefs.py", str(args.update), "--update", "--keys", str(args.keys), "--output", str(exefs_dir)],
        )
    else:
        print("==> [skip] v1.2.1 ExeFS already extracted")

    # 2. Extract & merge v1.2.1 RomFS
    if not have_romfs:
        if not args.base or not args.base.is_file():
            raise SystemExit("ERROR: --base <totk_base.xci|.nsp> is required to extract v1.2.1 RomFS.")
        if not args.update or not args.update.is_file():
            raise SystemExit("ERROR: --update <totk_update_1.2.1.nsp> is required to extract v1.2.1 RomFS.")
        if not args.keys.is_file():
            raise SystemExit(f"ERROR: prod.keys not found at {args.keys} (pass --keys).")
        assert hactool is not None
        run_step(
            "Extracting and merging TOTK v1.2.1 RomFS",
            [
                py,
                "scripts/extract_update_romfs.py",
                str(args.base),
                str(args.update),
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

    # 3. Extract Switch shared fonts
    if not have_fonts:
        if not args.keys.is_file():
            raise SystemExit(f"ERROR: prod.keys not found at {args.keys} (pass --keys).")
        font_cmd = [py, "scripts/extract_shared_fonts.py", "--keys", str(args.keys)]
        if args.firmware and args.firmware.is_dir():
            font_cmd += ["--firmware", str(args.firmware)]
        elif args.base and args.base.is_file() and args.base.suffix.lower() == ".xci":
            font_cmd += ["--xci", str(args.base)]
        else:
            raise SystemExit(
                "ERROR: Shared fonts require either an .xci --base image or --firmware <unpacked_firmware_nca_dir>."
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

    # 7. Build runtime-v121
    if not args.skip_build:
        run_step("Building v1.2.1 native runtime", ["make", "runtime-v121"])

    print("\nSetup complete. Launch with:\n  make run-v121")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
