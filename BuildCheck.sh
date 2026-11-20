#!/usr/bin/env bash
# Run on Linux with Unreal Editor closed. The VS Code build task calls this script.
# Optional: ./BuildCheck.sh [project.uproject] [--engine-root <engine-folder>]

set -euo pipefail

PROJECT_PATH=""
ENGINE_ROOT="${UE_ENGINE_ROOT:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --engine-root|-EngineRoot)
            ENGINE_ROOT="${2:-}"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
        *)
            PROJECT_PATH="$1"
            shift
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "$PROJECT_PATH" ]]; then
    PROJECT_PATH="$SCRIPT_DIR/LostWithin.uproject"
fi

if [[ ! -f "$PROJECT_PATH" ]] || [[ "${PROJECT_PATH##*.}" != "uproject" ]]; then
    echo "Error: ProjectPath must point to an existing Unreal .uproject file ($PROJECT_PATH)." >&2
    exit 1
fi
PROJECT_PATH="$(realpath "$PROJECT_PATH")"

if pgrep -x "UnrealEditor" >/dev/null 2>&1; then
    echo "Error: Save your work and close Unreal Editor before building." >&2
    exit 1
fi

# Resolve Unreal Engine root (UE_ENGINE_ROOT env var -> common Linux install paths)
if [[ -z "$ENGINE_ROOT" ]]; then
    for candidate in "$HOME/UnrealEngine" "/opt/unreal-engine" "/usr/share/unreal-engine"; do
        if [[ -f "$candidate/Engine/Build/BatchFiles/Linux/Build.sh" ]]; then
            ENGINE_ROOT="$candidate"
            break
        fi
    done
fi

if [[ -z "$ENGINE_ROOT" ]]; then
    echo "Error: Cannot find Unreal Engine. Set UE_ENGINE_ROOT or pass --engine-root <folder>." >&2
    exit 1
fi

BUILD_SH="$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
if [[ ! -f "$BUILD_SH" ]]; then
    echo "Error: Unreal build script not found: $BUILD_SH. Check UE_ENGINE_ROOT or --engine-root." >&2
    exit 1
fi

TARGET="$(basename "${PROJECT_PATH%.uproject}")Editor"
printf '\033[36mBuilding %s Linux Development...\033[0m\n' "$TARGET"

bash "$BUILD_SH" "$TARGET" Linux Development "-Project=$PROJECT_PATH" -WaitMutex -NoHotReloadFromIDE
BUILD_EXIT=$?

if [[ $BUILD_EXIT -ne 0 ]]; then
    printf '\033[31mBuild failed with exit code %d. See the compiler output above.\033[0m\n' "$BUILD_EXIT" >&2
    exit $BUILD_EXIT
fi

printf '\033[32mBuild succeeded. Reopen the Unreal project to load the new code.\033[0m\n'
exit 0
