#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SERIAL_ARG=()
if [ "${1:-}" = "--serial" ] && [ -n "${2:-}" ]; then
  SERIAL_ARG=(-s "$2")
fi

ZIP="$(find "$ROOT_DIR/dist" -maxdepth 1 -name 'zygisk-lsplant-framework-*.zip' -type f -print | sort | tail -n 1)"
if [ -z "$ZIP" ] || [ ! -f "$ZIP" ]; then
  echo "Missing generated module zip under $ROOT_DIR/dist; run ./scripts/build_all.sh first." >&2
  exit 1
fi

adb "${SERIAL_ARG[@]}" push "$ZIP" /sdcard/Download/
REMOTE_ZIP="/sdcard/Download/$(basename "$ZIP")"
INSTALL_CMD="if command -v magisk >/dev/null 2>&1; then magisk --install-module $REMOTE_ZIP; elif command -v ksud >/dev/null 2>&1; then ksud module install $REMOTE_ZIP; else echo No supported module installer found: magisk/ksud >&2; exit 127; fi"
adb "${SERIAL_ARG[@]}" shell "su -c '$INSTALL_CMD'"
echo "Module installed. Reboot device before verification."
