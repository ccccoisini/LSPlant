#!/usr/bin/env bash
set -euo pipefail

SERIAL_ARG=()
if [ "${1:-}" = "--serial" ] && [ -n "${2:-}" ]; then
  SERIAL_ARG=(-s "$2")
fi

adb "${SERIAL_ARG[@]}" logcat -d \
  -s zygisk_framework.Native:V zygisk_framework.Runtime:V zygisk_framework.Module:V zygisk_framework.Companion:V AndroidRuntime:E
