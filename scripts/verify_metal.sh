#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="macos-metal-dev"
BUILD_DIR="$ROOT_DIR/out/build/$PRESET"
SMOKE_FRAMES="${SMOKE_FRAMES:-2}"
JOBS="${JOBS:-}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "Metal verification must run on macOS." >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake was not found in PATH." >&2
    exit 1
fi

if [[ -z "$JOBS" ]]; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

cmake --preset "$PRESET"
cmake --build --preset "$PRESET" --parallel "$JOBS"

EXE=""
for candidate in \
    "$BUILD_DIR/bin/song_demo" \
    "$BUILD_DIR/game/song/song_demo" \
    "$BUILD_DIR/song_demo"; do
    if [[ -x "$candidate" ]]; then
        EXE="$candidate"
        break
    fi
done

if [[ -z "$EXE" ]]; then
    echo "song_demo executable was not found under $BUILD_DIR." >&2
    exit 1
fi

SMOKE_LOG="$(mktemp -t next-metal-smoke.XXXXXX)"
trap 'rm -f "$SMOKE_LOG"' EXIT

NEXT_RENDERER_BACKEND=metal "$EXE" --smoke-frames "$SMOKE_FRAMES" --allow-placeholder-cells 2>&1 | tee "$SMOKE_LOG"

required_patterns=(
    "Metal renderer initialized"
)

for pattern in "${required_patterns[@]}"; do
    if ! grep -Fq "$pattern" "$SMOKE_LOG"; then
        echo "Metal smoke log did not contain required evidence: $pattern" >&2
        exit 1
    fi
done

forbidden_patterns=(
    "Invalid window for Metal renderer"
    "Failed to initialize renderer"
    "Metal renderer initialization failed"
    "NEXT_RENDERER_BACKEND=metal is set, but"
)

for pattern in "${forbidden_patterns[@]}"; do
    if grep -Fq "$pattern" "$SMOKE_LOG"; then
        echo "Metal smoke log contained forbidden failure evidence: $pattern" >&2
        exit 1
    fi
done

echo "Metal build and smoke verification completed."
