#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

if [ ! -f "$ROOT_DIR/hook_template/gradlew" ]; then
  echo "Missing hook_template submodule. Run: git submodule update --init hook_template" >&2
  exit 1
fi

cd "$ROOT_DIR/hook_template"
./gradlew packageHookModule
