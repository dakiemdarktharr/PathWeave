#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: ./scripts/build-macos.sh [debug|release] [--package]" >&2
  exit 1
fi

CONFIG="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
shift

if [[ "$CONFIG" != "debug" && "$CONFIG" != "release" ]]; then
  echo "Invalid configuration: $CONFIG" >&2
  exit 1
fi

PACKAGE=false
if [[ $# -gt 0 ]]; then
  if [[ "$1" == "--package" ]]; then
    PACKAGE=true
    shift
  else
    echo "Unknown option: $1" >&2
    exit 1
  fi
fi

if [[ $# -gt 0 ]]; then
  echo "Too many arguments: $*" >&2
  exit 1
fi

QTPREFIX="${QT_ROOT_DIR:-}"
if [[ -n "$QTPREFIX" ]]; then
  export PATH="$QTPREFIX/bin:$PATH"
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is required" >&2
  exit 1
fi

cmake --preset "macos-$CONFIG"
cmake --build --preset "macos-$CONFIG" --parallel 4
ctest --preset "macos-$CONFIG"

if [[ "$PACKAGE" == true ]]; then
  if [[ "$CONFIG" != "release" ]]; then
    echo "Packaging is supported for release only." >&2
    exit 1
  fi

  cpack --config "build/macos/Release/CPackConfig.cmake" -G DragNDrop
  shopt -s nullglob
  package_files=(release/PathWeave-Setup-macOS-*.dmg)
  if ((${#package_files[@]} == 0)); then
    echo "Package not found: release/PathWeave-Setup-macOS-*.dmg" >&2
    exit 1
  fi

  package_path="${package_files[0]}"
  SIZE=$(wc -c < "$package_path")
  SHA=$(shasum -a 256 "$package_path" | awk '{print $1}')
  echo "PathWeave 1.1.0 | $package_path | ${SIZE} bytes | SHA256 ${SHA}"
fi
