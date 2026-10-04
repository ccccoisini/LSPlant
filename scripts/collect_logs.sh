#!/usr/bin/env bash
set -euo pipefail

SERIAL_ARG=()
if [ "${1:-}" = "--serial" ] && [ -n "${2:-}" ]; then
  SERIAL_ARG=(-s "$2")
fi

adb "${SERIAL_ARG[@]}" logcat -d \
  -s ZH.Native:V ZH.Runtime:V ZH.Companion:V zygisk_framework.Native:V zygisk_framework.Runtime:V zygisk_framework.Module:V zygisk_framework.Companion:V HookTemplate:V HookTemplate.AndroidId:V HookTemplate.Gaid:V AndroidRuntime:E
