#!/usr/bin/env bash
# Guided macOS setup & .app bundler for TOTK v1.2.1 + Minecraft Overlay.
# Double-click this file in Finder (or run it from Terminal).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

DEFAULT_KEYS="$HOME/Library/Application Support/Ryujinx/system/prod.keys"
DEFAULT_APP_OUT="build/TOTK Minecraft Overlay.app"

print_header() {
    echo ""
    echo "================================================================"
    echo "  TOTK Minecraft Overlay (v1.2.1) — macOS Guided Setup"
    echo "================================================================"
    echo ""
}

osascript_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\r'/}"
    s="${s//$'\n'/\\n}"
    printf '%s' "$s"
}

show_alert() {
    local title="$1"
    local msg="$2"
    echo "" >&2
    echo "ERROR [$title]: $msg" >&2
    if command -v osascript >/dev/null 2>&1 && [[ -z "${TOTK_CLI_PROMPTS:-}" ]]; then
        local t_esc m_esc
        t_esc="$(osascript_escape "$title")"
        m_esc="$(osascript_escape "$msg")"
        osascript -e "display alert \"${t_esc}\" message \"${m_esc}\" as critical buttons {\"OK\"} default button \"OK\"" >/dev/null 2>&1 || true
    fi
}

fail_exit() {
    show_alert "$1" "$2"
    if [[ -t 0 ]]; then
        echo ""
        read -r -p "Press Return to close..." _ || true
    fi
    exit 1
}

# Ask a Yes/No question. Returns 0 for Yes, 1 for No.
# Usage: ask_yes_no "Title/Prompt" "default_yes|default_no"
ask_yes_no() {
    local prompt="$1"
    local default_choice="${2:-default_yes}"
    local default_btn="Yes"
    local term_hint="[Y/n]"
    if [[ "$default_choice" == "default_no" ]]; then
        default_btn="No"
        term_hint="[y/N]"
    fi

    if command -v osascript >/dev/null 2>&1 && [[ -z "${TOTK_CLI_PROMPTS:-}" ]]; then
        local p_esc out rc=0
        p_esc="$(osascript_escape "$prompt")"
        out="$(osascript -e "button returned of (display dialog \"${p_esc}\" with title \"TOTK Minecraft Overlay Setup\" buttons {\"No\", \"Yes\"} default button \"${default_btn}\")" 2>&1)" || rc=$?
        if (( rc == 0 )); then
            [[ "$out" == "Yes" ]] && return 0 || return 1
        elif [[ "$out" == *"User canceled"* || "$out" == *"(-128)"* ]]; then
            return 1
        fi
    fi

    local ans=""
    printf '%s ' "$prompt $term_hint:" >&2
    read -r ans
    ans="$(printf '%s' "$ans" | tr '[:upper:]' '[:lower:]')"
    if [[ -z "$ans" ]]; then
        [[ "$default_choice" == "default_yes" ]] && return 0 || return 1
    fi
    [[ "$ans" == "y" || "$ans" == "yes" ]]
}

# Ask a 3-button or 2-button choice via osascript, with terminal fallback.
# Prints chosen button name to stdout. Returns 1 only if canceled/aborted.
ask_choice() {
    local prompt="$1"
    local btn1="$2"
    local btn2="$3"
    local btn3="${4:-}"
    local default_btn="${5:-$btn2}"

    if command -v osascript >/dev/null 2>&1 && [[ -z "${TOTK_CLI_PROMPTS:-}" ]]; then
        local p_esc buttons_as out rc=0
        p_esc="$(osascript_escape "$prompt")"
        if [[ -n "$btn3" ]]; then
            buttons_as="{\"$(osascript_escape "$btn1")\", \"$(osascript_escape "$btn2")\", \"$(osascript_escape "$btn3")\"}"
        else
            buttons_as="{\"$(osascript_escape "$btn1")\", \"$(osascript_escape "$btn2")\"}"
        fi
        out="$(osascript -e "button returned of (display dialog \"${p_esc}\" with title \"TOTK Minecraft Overlay Setup\" buttons ${buttons_as} default button \"$(osascript_escape "$default_btn")\")" 2>&1)" || rc=$?
        if (( rc == 0 )); then
            printf '%s\n' "$out"
            return 0
        elif [[ "$out" == *"User canceled"* || "$out" == *"(-128)"* ]]; then
            return 1
        fi
    fi

    {
        echo ""
        echo "$prompt"
        echo "  1) $btn1"
        echo "  2) $btn2"
        [[ -n "$btn3" ]] && echo "  3) $btn3"
    } >&2
    local sel=""
    printf 'Choose [default: %s]: ' "$default_btn" >&2
    read -r sel
    case "$sel" in
        1) printf '%s\n' "$btn1" ;;
        2) printf '%s\n' "$btn2" ;;
        3) [[ -n "$btn3" ]] && printf '%s\n' "$btn3" || printf '%s\n' "$default_btn" ;;
        "") printf '%s\n' "$default_btn" ;;
        *) printf '%s\n' "$default_btn" ;;
    esac
}

# Pick a file using native Finder dialog, falling back to terminal read.
# Prints chosen POSIX path to stdout, or returns 1 if canceled.
pick_file() {
    local prompt="$1"
    if command -v osascript >/dev/null 2>&1 && [[ -z "${TOTK_CLI_PROMPTS:-}" ]]; then
        local p_esc out rc=0
        p_esc="$(osascript_escape "$prompt")"
        out="$(osascript -e "POSIX path of (choose file with prompt \"${p_esc}\")" 2>&1)" || rc=$?
        if (( rc == 0 )); then
            printf '%s\n' "${out%/}"
            return 0
        elif [[ "$out" == *"User canceled"* || "$out" == *"(-128)"* ]]; then
            return 1
        fi
    fi

    local p=""
    printf '%s ' "$prompt (enter path, or blank to cancel):" >&2
    read -r p
    # Strip surrounding quotes/spaces from drag-and-drop into Terminal
    p="${p#\"}"
    p="${p%\"}"
    p="${p#\'}"
    p="${p%\'}"
    [[ -n "$p" ]] || return 1
    p="${p/#\~/$HOME}"
    printf '%s\n' "$p"
}

# Pick a folder using native Finder dialog, falling back to terminal read.
pick_folder() {
    local prompt="$1"
    if command -v osascript >/dev/null 2>&1 && [[ -z "${TOTK_CLI_PROMPTS:-}" ]]; then
        local p_esc out rc=0
        p_esc="$(osascript_escape "$prompt")"
        out="$(osascript -e "POSIX path of (choose folder with prompt \"${p_esc}\")" 2>&1)" || rc=$?
        if (( rc == 0 )); then
            printf '%s\n' "${out%/}"
            return 0
        elif [[ "$out" == *"User canceled"* || "$out" == *"(-128)"* ]]; then
            return 1
        fi
    fi

    local p=""
    printf '%s ' "$prompt (enter folder path, or blank to cancel):" >&2
    read -r p
    p="${p#\"}"
    p="${p%\"}"
    p="${p#\'}"
    p="${p%\'}"
    [[ -n "$p" ]] || return 1
    p="${p/#\~/$HOME}"
    printf '%s\n' "${p%/}"
}

has_ext() {
    local path="$1"
    shift
    local lower_ext=".$(printf '%s' "${path##*.}" | tr '[:upper:]' '[:lower:]')"
    local allowed
    for allowed in "$@"; do
        [[ "$lower_ext" == "$allowed" ]] && return 0
    done
    return 1
}

print_header

# 1. Apple Silicon check
ARCH="$(uname -m)"
if [[ "$ARCH" != "arm64" ]]; then
    if [[ "$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)" == "1" ]]; then
        exec arch -arm64 /usr/bin/env bash "$0" "$@"
    fi
    fail_exit "Apple Silicon Required" "TOTK Minecraft Overlay requires an Apple Silicon (arm64) Mac. Detected architecture: ${ARCH}."
fi

# 2. macOS 26+ check
MACOS_VER="$(sw_vers -productVersion)"
MACOS_MAJOR="${MACOS_VER%%.*}"
if ! [[ "$MACOS_MAJOR" =~ ^[0-9]+$ ]] || (( MACOS_MAJOR < 26 )); then
    fail_exit "macOS 26+ Required" "TOTK Minecraft Overlay requires macOS 26 (Tahoe) / Metal 4 or newer. Detected macOS ${MACOS_VER}."
fi
echo "==> System check OK: Apple Silicon (${ARCH}), macOS ${MACOS_VER}"

# 3. Xcode Command Line Tools check
if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang >/dev/null 2>&1; then
    echo ""
    echo "Xcode Command Line Tools are not installed."
    echo "Run the following command in Terminal, finish the installer, then re-run this script:"
    echo "  xcode-select --install"
    if ask_yes_no $'Xcode Command Line Tools are required to compile the launcher and native runtime.\n\nOpen the Xcode Command Line Tools installer now?' "default_yes"; then
        xcode-select --install || true
    fi
    fail_exit "Xcode Command Line Tools Required" "Please complete the Xcode Command Line Tools installation ('xcode-select --install') and then run 'Setup TOTK Minecraft Overlay.command' again."
fi
echo "==> Xcode Command Line Tools OK: $(xcode-select -p)"

# 4. Homebrew check
if [[ -x /opt/homebrew/bin/brew ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
elif [[ -x /usr/local/bin/brew ]]; then
    eval "$(/usr/local/bin/brew shellenv)"
fi
if ! command -v brew >/dev/null 2>&1; then
    echo ""
    echo "Homebrew is not installed. Install it by running:"
    echo '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
    fail_exit "Homebrew Required" "Homebrew was not found. Install Homebrew from https://brew.sh (see Terminal for the install command), then re-run this script."
fi
echo "==> Homebrew OK: $(command -v brew)"

# 5. Install missing Homebrew packages
if ! command -v python3 >/dev/null 2>&1; then
    fail_exit "Python 3 Required" "python3 was not found in PATH even after checking Xcode Command Line Tools."
fi
echo "==> Checking / installing required Homebrew dependencies..."
python3 scripts/setup_v121.py --install-deps

# 6. Check which stages are already extracted on disk
NEED_ROM_INPUTS=1
if [[ -f "extracted/v121/romfs/index.json" && -f "extracted/v121/exefs/main" && -f "extracted/v121/exefs/sdk" && -f "extracted/v121/exefs/subsdk0" && -f "extracted/firmware/shared-fonts/manifest.json" ]]; then
    if ask_yes_no $'Existing extracted TOTK v1.2.1 game files were detected.\n\nReuse existing extracted game data without re-selecting ROM dumps?' "default_yes"; then
        NEED_ROM_INPUTS=0
    fi
fi

BASE_ROM=""
UPDATE_ROM=""
KEYS_PATH=""
FIRMWARE_PATH=""

if (( NEED_ROM_INPUTS == 1 )); then
    while true; do
        BASE_ROM="$(pick_file "Select TOTK Base ROM (.xci, .nsp, .xcz, or .nsz)")" || fail_exit "Setup Canceled" "Setup canceled before selecting base ROM."
        if [[ -f "$BASE_ROM" ]] && has_ext "$BASE_ROM" .xci .nsp .xcz .nsz; then
            break
        fi
        show_alert "Invalid Base ROM" "Please select an existing .xci, .nsp, .xcz, or .nsz file."
    done

    while true; do
        UPDATE_ROM="$(pick_file "Select TOTK v1.2.1 Update (.nsp or .nsz)")" || fail_exit "Setup Canceled" "Setup canceled before selecting v1.2.1 update."
        if [[ -f "$UPDATE_ROM" ]] && has_ext "$UPDATE_ROM" .nsp .nsz; then
            break
        fi
        show_alert "Invalid Update Package" "Please select an existing .nsp or .nsz v1.2.1 update file."
    done

    if [[ -f "$DEFAULT_KEYS" ]]; then
        CHOICE="$(ask_choice $'Detected prod.keys at:\n'"${DEFAULT_KEYS}"$'\n\nUse this file?' "Choose Other..." "Use Detected" "" "Use Detected")" || fail_exit "Setup Canceled" "Setup canceled."
        if [[ "$CHOICE" == "Use Detected" ]]; then
            KEYS_PATH="$DEFAULT_KEYS"
        fi
    fi
    if [[ -z "$KEYS_PATH" ]]; then
        while true; do
            KEYS_PATH="$(pick_file "Select prod.keys")" || fail_exit "Setup Canceled" "Setup canceled before selecting prod.keys."
            [[ -f "$KEYS_PATH" ]] && break
            show_alert "File Not Found" "prod.keys file not found at: ${KEYS_PATH}"
        done
    fi

    if [[ ! -f "extracted/firmware/shared-fonts/manifest.json" ]]; then
        FW_PROMPT=$'Switch Firmware (optional for .xci/.xcz, required for .nsp/.nsz).\n\nSelect an unpacked firmware NCA folder or firmware .zip, or skip if using an .xci/.xcz with an update partition.'
        FW_DEFAULT="Skip"
        if has_ext "$BASE_ROM" .nsp .nsz; then
            FW_PROMPT="Your base image is .nsp/.nsz, so Switch shared fonts require a Switch firmware folder or .zip."
            FW_DEFAULT="Choose .zip"
        fi
        while true; do
            FW_CHOICE="$(ask_choice "$FW_PROMPT" "Skip" "Choose .zip" "Choose Folder" "$FW_DEFAULT")" || FW_CHOICE="Skip"
            if [[ "$FW_CHOICE" == "Skip" ]]; then
                if has_ext "$BASE_ROM" .nsp .nsz; then
                    show_alert "Firmware Required" "An .nsp/.nsz base image requires Switch firmware for shared fonts."
                    continue
                fi
                break
            elif [[ "$FW_CHOICE" == "Choose .zip" ]]; then
                FIRMWARE_PATH="$(pick_file "Select Switch Firmware .zip archive")" || continue
                if [[ -f "$FIRMWARE_PATH" ]] && has_ext "$FIRMWARE_PATH" .zip; then
                    break
                fi
                show_alert "Invalid Firmware Archive" "Please select an existing .zip firmware file."
                FIRMWARE_PATH=""
            else
                FIRMWARE_PATH="$(pick_folder "Select Unpacked Switch Firmware NCA Folder")" || continue
                if [[ -d "$FIRMWARE_PATH" ]]; then
                    break
                fi
                FIRMWARE_PATH=""
            fi
        done
    fi
else
    if [[ -f "$DEFAULT_KEYS" ]]; then
        KEYS_PATH="$DEFAULT_KEYS"
    fi
fi

# Optional Minecraft 1.21+ JAR selection (or auto-discover local Minecraft install)
MC_JAR=""
MC_DIR=""
SKIP_MC=0
if [[ ! -f "assets/mc/manifest.json" ]]; then
    MC_CHOICE="$(ask_choice $'Minecraft: Java Edition (1.21+, e.g. 1.21.4.jar) assets are needed for the Minecraft overlay.\n\nChoose Auto-Detect to use your local ~/Library/Application Support/minecraft install, or pick an existing 1.21+ JAR.' "Skip MC" "Choose JAR..." "Auto-Detect" "Auto-Detect")" || MC_CHOICE="Auto-Detect"
    if [[ "$MC_CHOICE" == "Skip MC" ]]; then
        SKIP_MC=1
    elif [[ "$MC_CHOICE" == "Choose JAR..." ]]; then
        while true; do
            MC_JAR="$(pick_file "Select Minecraft Java 1.21+ client JAR (e.g. 1.21.4.jar)")" || { MC_JAR=""; break; }
            if [[ -f "$MC_JAR" ]] && has_ext "$MC_JAR" .jar; then
                # Derive MINECRAFT_DIR if JAR sits at <mc_dir>/versions/<ver>/<ver>.jar
                CAND_MC_DIR="$(cd "$(dirname "$MC_JAR")/../.." 2>/dev/null && pwd || true)"
                if [[ -n "$CAND_MC_DIR" && -d "$CAND_MC_DIR/assets/objects" ]]; then
                    MC_DIR="$CAND_MC_DIR"
                fi
                break
            fi
            show_alert "Invalid Minecraft JAR" "Please select an existing .jar file."
        done
    fi
fi

PREPARE_SHADERS=0
if ask_yes_no $'Pre-compile startup shaders (--prepare-shaders)?\n\nRecommended to eliminate first-launch stuttering (takes a few minutes if not already cached).' "default_no"; then
    PREPARE_SHADERS=1
fi

BUILD_APP=1
if ! ask_yes_no "Package standalone macOS app bundle (${DEFAULT_APP_OUT})?" "default_yes"; then
    BUILD_APP=0
fi

CMD=(python3 scripts/setup_v121.py)
[[ -n "$BASE_ROM" ]] && CMD+=(--base "$BASE_ROM")
[[ -n "$UPDATE_ROM" ]] && CMD+=(--update "$UPDATE_ROM")
[[ -n "$KEYS_PATH" ]] && CMD+=(--keys "$KEYS_PATH")
[[ -n "$FIRMWARE_PATH" ]] && CMD+=(--firmware "$FIRMWARE_PATH")
[[ -n "$MC_DIR" ]] && CMD+=(--mc-dir "$MC_DIR")
[[ -n "$MC_JAR" ]] && CMD+=(--mc-jar "$MC_JAR")
(( SKIP_MC == 1 )) && CMD+=(--skip-mc)
(( PREPARE_SHADERS == 1 )) && CMD+=(--prepare-shaders)
if (( BUILD_APP == 1 )); then
    CMD+=(--build-app --app-out "$DEFAULT_APP_OUT")
fi

echo ""
echo "==> Running setup:"
printf '    %q' "${CMD[@]}"
echo ""
echo ""

"${CMD[@]}"

echo ""
echo "================================================================"
echo "  Setup Complete!"
echo "================================================================"

REVEAL_TARGET=""
if (( BUILD_APP == 1 )) && [[ -d "$DEFAULT_APP_OUT" ]]; then
    REVEAL_TARGET="$DEFAULT_APP_OUT"
    echo "Built app bundle: $SCRIPT_DIR/$DEFAULT_APP_OUT"
elif [[ -x "build/totk-native-v121" ]]; then
    REVEAL_TARGET="build/totk-native-v121"
    echo "Built native binary: $SCRIPT_DIR/build/totk-native-v121"
fi

if [[ -n "$REVEAL_TARGET" ]]; then
    open -R "$REVEAL_TARGET" || true
fi

if command -v osascript >/dev/null 2>&1 && [[ -z "${TOTK_CLI_PROMPTS:-}" ]]; then
    osascript -e "display notification \"Setup finished successfully!\" with title \"TOTK Minecraft Overlay\"" >/dev/null 2>&1 || true
fi

if [[ -t 0 ]]; then
    echo ""
    read -r -p "Press Return to close..." _ || true
fi
