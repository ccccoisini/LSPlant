#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PACKAGE=""
MODULE_ZIP=""
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
    --module-zip)
      MODULE_ZIP="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [ -z "$MODULE_ZIP" ]; then
  MODULE_ZIP="$ROOT_DIR/hook_template/dist/example_hook-1.2.0.zip"
fi
if [ ! -s "$MODULE_ZIP" ]; then
  echo "Missing module ZIP: $MODULE_ZIP. Build it with hook_template/gradlew packageHookModule." >&2
  exit 1
fi
if [ -z "$PACKAGE" ]; then
  echo "Pass --package with an installed app that is listed in the module scope.list." >&2
  exit 2
fi

MODULE_ID="$(unzip -p "$MODULE_ZIP" META-INF/xposed/module.prop | sed -n 's/^id=//p' | head -n 1)"
if [ -z "$MODULE_ID" ]; then
  echo "Module ZIP has no id in META-INF/xposed/module.prop" >&2
  exit 1
fi
if [[ ! "$MODULE_ID" =~ ^[A-Za-z][A-Za-z0-9._-]*$ ]]; then
  echo "Module ZIP has an invalid id: $MODULE_ID" >&2
  exit 1
fi
if [[ ! "$PACKAGE" =~ ^[A-Za-z0-9_][A-Za-z0-9_.:$-]*$ ]]; then
  echo "Invalid package/process name: $PACKAGE" >&2
  exit 2
fi

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
  remote_script="/data/local/tmp/zygisk_framework-root-$$-$RANDOM.sh"
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
ROOT_MANAGER="$(run_root 'if command -v magisk >/dev/null 2>&1; then printf Magisk; elif command -v ksud >/dev/null 2>&1; then printf KernelSU; fi' | tr -d '\r' || true)"

record "device=$MODEL"
record "api=$API"
record "abi=$ABI"
record "rootManager=$ROOT_MANAGER"
record "moduleId=$MODULE_ID"
record "targetPackage=$PACKAGE"

if [ "$API" -lt 26 ]; then
  record "REAL_DEVICE_HOOK_ACCEPTANCE: FAIL reason=API_TOO_LOW"
  exit 1
fi

run_adb push "$MODULE_ZIP" "/sdcard/Download/zygisk-framework-module.zip" >/dev/null
run_root "/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/zygisk-framework-module.zip"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework info $MODULE_ID"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID settings set enabled boolean false"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID settings set android_id string 0123456789abcdef"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification set int_value int 42"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification set long_value long 9223372036854775807"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification set float_value float 1.25"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification set set_value string-set alpha beta"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification list"
SPECIAL_OUTPUT="$(run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification set special_text string 'spaces + slash / equals ='; /data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification get special_text")"
if printf '%s\n' "$SPECIAL_OUTPUT" | grep -Fq $'string\tspecial_text\tspaces + slash / equals ='; then
  record "PASS preferences_special_text_round_trip=true"
else
  record "FAIL preferences_special_text_round_trip=false"
  exit 1
fi
if run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification get missing_key"; then
  record "FAIL preferences_missing_key_nonzero=false"
  exit 1
else
  record "PASS preferences_missing_key_nonzero=true"
fi
if run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification set bad_boolean boolean yes"; then
  record "FAIL preferences_type_validation=false"
  exit 1
else
  record "PASS preferences_type_validation=true"
fi
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification remove special_text"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification remove special_text"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification clear"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID verification clear"

# 同版本覆盖安装必须保留独立的数据目录。
run_root "/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/zygisk-framework-module.zip"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework scope $MODULE_ID set $PACKAGE"
PREF_AFTER_UPGRADE="$(run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID settings get enabled")"
if printf '%s\n' "$PREF_AFTER_UPGRADE" | grep -Fq $'boolean\tenabled\tfalse'; then
  record "PASS preferences_preserved_on_upgrade=true"
else
  record "FAIL preferences_preserved_on_upgrade=false"
  exit 1
fi

run_adb shell am force-stop "$PACKAGE" >/dev/null || true
run_adb logcat -c
FAIL=0
run_adb shell monkey -p "$PACKAGE" -c android.intent.category.LAUNCHER 1 >/dev/null
sleep 5
PID_BEFORE="$(run_adb shell pidof "$PACKAGE" | tr -d '\r' || true)"
record "targetPidBeforePrefsUpdate=$PID_BEFORE"

run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID settings set enabled boolean true"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID settings set android_id string fedcba9876543210"
sleep 2
run_adb shell monkey -p "$PACKAGE" -c android.intent.category.LAUNCHER 1 >/dev/null
sleep 3
PID_AFTER="$(run_adb shell pidof "$PACKAGE" | tr -d '\r' || true)"
record "targetPidAfterPrefsUpdate=$PID_AFTER"
if [ -n "$PID_BEFORE" ] && [ "$PID_BEFORE" = "$PID_AFTER" ]; then
  record "PASS preferences_update_kept_pid=true"
else
  record "FAIL preferences_update_kept_pid=false"
  FAIL=1
fi

LOG_FILE="$(mktemp)"
run_adb logcat -d -s zygisk_framework.Native:V zygisk_framework.Runtime:V zygisk_framework.Module:V zygisk_framework.Companion:V HookTemplate:V HookTemplate.AndroidId:V AndroidRuntime:E > "$LOG_FILE"
cat "$LOG_FILE" >> "$REPORT"

require_marker "TARGET_MATCH process=$PACKAGE" || FAIL=1
require_marker "MODULE_SCOPE_MATCH id=$MODULE_ID process=$PACKAGE" || FAIL=1
require_marker "LSPLANT_INIT_OK" || FAIL=1
require_marker "FRAMEWORK_DEX_LOADED" || FAIL=1
require_marker "DEX_ELEMENTS_RESTORED" || FAIL=1
require_marker "MODULE_DEX_LOADED id=$MODULE_ID" || FAIL=1
require_marker "MODULE_ENTRY_LOADED" || FAIL=1
require_marker "TEMPLATE_HOOK_INSTALLED package=$PACKAGE" || FAIL=1
require_marker "TEMPLATE_HOOK_BEFORE package=$PACKAGE" || FAIL=1
require_marker "TEMPLATE_HOOK_AFTER package=$PACKAGE" || FAIL=1
require_marker "REMOTE_PREFS_READY group=settings" || FAIL=1
if grep -Fq "REMOTE_PREFS_DEGRADED reason=SOCKET_EXEMPT_FAILED" "$LOG_FILE"; then
  record "FAIL remote_preferences_live_channel=false reason=SOCKET_EXEMPT_FAILED"
  FAIL=1
else
  require_marker "REMOTE_PREFS_UPDATE id=$MODULE_ID group=settings" || FAIL=1
  require_marker "REMOTE_PREFS_CHANGED key=enabled" || FAIL=1
  require_marker "REMOTE_PREFS_CHANGED key=android_id" || FAIL=1
  require_marker "REMOTE_PREFS_APPLIED key=android_id" || FAIL=1
fi

if grep -E 'FATAL EXCEPTION|Fatal signal|Abort message' "$LOG_FILE" >/dev/null; then
  record "FAIL crash_marker_present=true"
  FAIL=1
else
  record "PASS crash_marker_present=false"
fi

run_root "/data/adb/zygisk_framework/bin/zygisk_framework disable $MODULE_ID"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework enable $MODULE_ID"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework remove $MODULE_ID"
if run_root "test ! -e /data/adb/zygisk_framework/data/$MODULE_ID"; then
  record "PASS preferences_removed_with_module=true"
else
  record "FAIL preferences_removed_with_module=false"
  FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
  record "REAL_DEVICE_HOOK_ACCEPTANCE: PASS"
  record "TARGET_PACKAGE: $PACKAGE"
else
  record "REAL_DEVICE_HOOK_ACCEPTANCE: FAIL"
  exit 1
fi
