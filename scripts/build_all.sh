#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

PREPARE_SOURCES=0
while [ $# -gt 0 ]; do
  case "$1" in
    --prepare-sources|--update-third-party)
      PREPARE_SOURCES=1
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

is_git_worktree() {
  local dir="$1"
  [ -d "$dir" ] && git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1
}

if [ "$PREPARE_SOURCES" -eq 1 ]; then
  ./scripts/prepare_sources.sh
else
  if [ -d "$ROOT_DIR/.git" ]; then
    git submodule update --init third_party/LSPlant third_party/Dobby
  fi
  ./scripts/prepare_sources.sh --apply-patches-only
  if ! is_git_worktree "$ROOT_DIR/third_party/LSPlant" || ! is_git_worktree "$ROOT_DIR/third_party/Dobby"; then
    echo "Missing third_party sources. Pass --prepare-sources to refresh them." >&2
    exit 1
  fi
fi

./gradlew clean
./gradlew buildAll

MODULE_ZIP="$(find "$ROOT_DIR/dist" -maxdepth 1 -name 'zygisk_framework-*.zip' -type f -print | sort | tail -n 1)"
if [ -z "$MODULE_ZIP" ] || [ ! -s "$MODULE_ZIP" ]; then
  echo "Missing generated Magisk/KernelSU module zip under $ROOT_DIR/dist" >&2
  exit 1
fi

unzip -t "$MODULE_ZIP" >/dev/null

require_file() {
  local path="$1"
  if [ ! -s "$path" ]; then
    echo "Missing required build output: $path" >&2
    exit 1
  fi
}

require_zip_entry() {
  local entry="$1"
  if ! unzip -l "$MODULE_ZIP" "$entry" >/dev/null 2>&1; then
    echo "Missing zip entry: $entry" >&2
    exit 1
  fi
}

reject_zip_entry() {
  local entry="$1"
  if unzip -l "$MODULE_ZIP" "$entry" >/dev/null 2>&1; then
    echo "Unexpected zip entry after embedded framework migration: $entry" >&2
    exit 1
  fi
}

require_file "$ROOT_DIR/native-loader/build/generated/zygisk_framework/framework_dex.h"
require_file "$ROOT_DIR/native-loader/build/generated/zygisk_framework/framework_mapping.h"
require_file "$ROOT_DIR/build/dist-work/zygisk/arm64-v8a.so"
require_file "$ROOT_DIR/dist/build-info.json"

require_zip_entry "zygisk/arm64-v8a.so"
require_zip_entry "zygisk/build-info.json"
reject_zip_entry "zygisk/armeabi-v7a.so"
reject_zip_entry "zygisk/framework.dex"
reject_zip_entry "zygisk/framework.mapping"

if ! grep -Fq '"frameworkDelivery": "embedded-native-header"' "$ROOT_DIR/dist/build-info.json"; then
  echo "build-info.json does not record embedded framework delivery" >&2
  exit 1
fi

if ! file "$ROOT_DIR/build/dist-work/zygisk/arm64-v8a.so" | grep -Fq "stripped"; then
  echo "arm64-v8a.so is not stripped" >&2
  exit 1
fi

echo "BUILD_ALL_OK"
echo "Magisk/KernelSU module: $MODULE_ZIP"
if command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "$MODULE_ZIP"
fi
