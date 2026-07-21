#!/usr/bin/env bash
set -euo pipefail

SERIAL_ARG=()
if [ "${1:-}" = "--serial" ] && [ -n "${2:-}" ]; then
  SERIAL_ARG=(-s "$2")
fi

adb "${SERIAL_ARG[@]}" logcat -d \
  -s ZHook.Native:V ZHook.Runtime:V ZHook.Module:V ZHook.Companion:V AndroidRuntime:E
