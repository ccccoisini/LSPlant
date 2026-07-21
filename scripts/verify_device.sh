#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PACKAGE="io.hammer.developmentenvironmentdetection"
MODULE="hammer-demo"
SERIAL_ARG=()

while [ $# -gt 0 ]; do
  case "$1" in
    --serial)
      SERIAL_ARG=(-s "$2")
      shift 2
      ;;
    --package)
      PACKAGE="$2"
      shift 2
      ;;
    --module)
      MODULE="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

REPORT="$ROOT_DIR/dist/device-verification-report.txt"
mkdir -p "$ROOT_DIR/dist"
: > "$REPORT"

run_adb() {
  if [ "${#SERIAL_ARG[@]}" -eq 0 ]; then
    adb "$@"
  else
    adb "${SERIAL_ARG[@]}" "$@"
  fi
}

run_root() {
  local script="$1"
  local local_script
  local remote_script
  local_script="$(mktemp)"
  remote_script="/data/local/tmp/zhook-root-$$-$RANDOM.sh"
  printf '%s\n' "$script" > "$local_script"
  run_adb push "$local_script" "$remote_script" >/dev/null
  rm -f "$local_script"
  run_adb shell "su -c 'sh $remote_script; rc=\$?; rm -f $remote_script; exit \$rc'"
}

record() {
  echo "$*" | tee -a "$REPORT"
}

require_marker() {
  local marker="$1"
  if grep -Fq "$marker" "$LOG_FILE"; then
    record "PASS marker=$marker"
  else
    record "FAIL marker=$marker"
    return 1
  fi
}

command -v adb >/dev/null
run_adb get-state >/dev/null
API="$(run_adb shell getprop ro.build.version.sdk | tr -d '\r')"
MODEL="$(run_adb shell getprop ro.product.model | tr -d '\r')"
ABI="$(run_adb shell getprop ro.product.cpu.abilist | tr -d '\r')"
MAGISK="$(run_root 'if command -v magisk >/dev/null 2>&1; then magisk -V; elif command -v ksud >/dev/null 2>&1; then ksud --version; fi' | tr -d '\r' || true)"

record "device=$MODEL"
record "api=$API"
record "abi=$ABI"
record "magisk=$MAGISK"

if [ "$API" -lt 26 ]; then
  record "REAL_DEVICE_HOOK_ACCEPTANCE: FAIL reason=API_TOO_LOW"
  exit 1
fi

run_root "mkdir -p /data/adb/hook/modules/$MODULE/META-INF/xposed"
run_root "printf '%s\n' '$PACKAGE' > /data/adb/hook/target.txt"
run_adb push "$ROOT_DIR/dist/demo-hook-module/module.dex" "/sdcard/Download/$MODULE.dex" >/dev/null
run_root "cp /sdcard/Download/$MODULE.dex /data/adb/hook/modules/$MODULE/module.dex"
for file in java_init.list module.prop scope.list; do
  run_adb push "$ROOT_DIR/dist/demo-hook-module/META-INF/xposed/$file" "/sdcard/Download/$file" >/dev/null
  run_root "cp /sdcard/Download/$file /data/adb/hook/modules/$MODULE/META-INF/xposed/$file"
done
run_root "sha256sum /data/adb/hook/modules/$MODULE/module.dex | cut -d' ' -f1 > /data/adb/hook/modules/$MODULE/module.sha256"
run_root "chown -R 0:0 /data/adb/hook && chmod 0755 /data/adb/hook && chmod 0644 /data/adb/hook/target.txt"
run_root "find /data/adb/hook/modules -type d -exec chmod 0755 {} \\;"
run_root "find /data/adb/hook/modules -type f -exec chmod 0644 {} \\;"

run_adb shell am force-stop "$PACKAGE" >/dev/null || true
run_adb logcat -c
run_adb shell monkey -p "$PACKAGE" -c android.intent.category.LAUNCHER 1 >/dev/null
sleep 5
PID="$(run_adb shell pidof "$PACKAGE" | tr -d '\r' || true)"
record "targetPid=$PID"

LOG_FILE="$(mktemp)"
run_adb logcat -d -s ZHook.Native:V ZHook.Runtime:V ZHook.Module:V ZHook.Companion:V AndroidRuntime:E > "$LOG_FILE"
cat "$LOG_FILE" >> "$REPORT"

FAIL=0
require_marker "TARGET_MATCH process=$PACKAGE" || FAIL=1
require_marker "LSPLANT_INIT_OK" || FAIL=1
require_marker "FRAMEWORK_DEX_LOADED" || FAIL=1
require_marker "DEX_ELEMENTS_RESTORED" || FAIL=1
require_marker "MODULE_DEX_LOADED id=$MODULE" || FAIL=1
require_marker "MODULE_ENTRY_LOADED" || FAIL=1
require_marker "DEMO_HOOK_INSTALLED" || FAIL=1
require_marker "DEMO_BEFORE package=$PACKAGE" || FAIL=1
require_marker "DEMO_AFTER package=$PACKAGE" || FAIL=1

if grep -E 'FATAL EXCEPTION|Fatal signal|Abort message' "$LOG_FILE" >/dev/null; then
  record "FAIL crash_marker_present=true"
  FAIL=1
else
  record "PASS crash_marker_present=false"
fi

run_adb logcat -c
run_adb shell am force-stop com.android.settings >/dev/null || true
run_adb shell monkey -p com.android.settings -c android.intent.category.LAUNCHER 1 >/dev/null || true
sleep 3
NON_TARGET_LOG="$(run_adb logcat -d -s ZHook.Native:V ZHook.Runtime:V ZHook.Module:V)"
if echo "$NON_TARGET_LOG" | grep -Fq "TARGET_MATCH process=com.android.settings"; then
  record "FAIL non_target_injected=true"
  FAIL=1
else
  record "PASS non_target_injected=false"
fi

if [ "$FAIL" -eq 0 ]; then
  record "REAL_DEVICE_HOOK_ACCEPTANCE: PASS"
  record "TARGET_PACKAGE: $PACKAGE"
else
  record "REAL_DEVICE_HOOK_ACCEPTANCE: FAIL"
  exit 1
fi
