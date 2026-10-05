#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/verify_gaid_events.sh"
source "$ROOT_DIR/scripts/verify_remote_config_events.sh"
PACKAGE=""
MODULE_ZIP=""
REQUIRE_GAID=false
CHECK_REMOTE_PREFERENCES=false
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
    --require-gaid)
      REQUIRE_GAID=true
      shift
      ;;
    --check-remote-preferences)
      CHECK_REMOTE_PREFERENCES=true
      shift
      ;;
    --help)
      echo "Usage: $0 --package APP [--serial SERIAL] [--module-zip ZIP] [--require-gaid] [--check-remote-preferences]"
      echo "--require-gaid requires a GAID_HOOK_APPLIED event; installation alone is insufficient."
      echo "--check-remote-preferences checks live configuration updates and an unchanged target PID."
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [ -z "$MODULE_ZIP" ]; then
  MODULE_ZIP="$ROOT_DIR/hook_template/dist/example_hook-1.4.0.zip"
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
PREFERENCES_GROUP="verification"
TEMPLATE_GROUP="settings.$PACKAGE"
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

capture_logs() {
  run_adb logcat -d -s ZH.Native:V ZH.Runtime:V ZH.Companion:V zygisk_framework.Native:V zygisk_framework.Runtime:V zygisk_framework.Module:V zygisk_framework.Companion:V HookTemplate:V HookTemplate.Config:V HookTemplate.AndroidId:V HookTemplate.Gaid:V AndroidRuntime:E > "$LOG_FILE"
}

wait_remote_config() {
  local phase="$1"
  local enabled="$2"
  local android_id="$3"
  local gaid="$4"
  local deadline=$((SECONDS + 15))
  local current_pid
  local result
  while [ "$SECONDS" -lt "$deadline" ]; do
    current_pid="$(run_adb shell pidof "$PACKAGE" | tr -d '\r' || true)"
    if [ -z "$TARGET_PID" ] || [ "$current_pid" != "$TARGET_PID" ]; then
      record "FAIL remote_preferences=PID_CHANGED phase=$phase before=$TARGET_PID after=$current_pid"
      return 1
    fi
    capture_logs
    if result="$(verify_remote_config_snapshot "$LOG_FILE" REMOTE_CONFIG_UPDATED "$TEMPLATE_GROUP" "$PACKAGE" "$enabled" "$android_id" "$gaid")"; then
      record "$result phase=$phase pid=$current_pid"
      return 0
    fi
    sleep 1
  done
  record "$result phase=$phase"
  return 1
}

command -v adb >/dev/null
run_adb get-state >/dev/null
LAUNCHER_COMPONENT="$(run_adb shell cmd package resolve-activity --brief \
  -a android.intent.action.MAIN -c android.intent.category.LAUNCHER "$PACKAGE" \
  2>/dev/null | tr -d '\r' | tail -n 1)"
if [[ ! "$LAUNCHER_COMPONENT" =~ ^[A-Za-z0-9_.]+/[A-Za-z0-9_.$]+$ ]]; then
  echo "Unable to resolve launcher activity for $PACKAGE" >&2
  exit 1
fi
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
record "preferencesGroup=$PREFERENCES_GROUP"
record "requireGaid=$REQUIRE_GAID"
record "checkRemotePreferences=$CHECK_REMOTE_PREFERENCES"

TRANSPORT_TEST="$(find "$ROOT_DIR/native_loader/build/intermediates/cxx" \
  -type f -name remote_preferences_transport_test -print 2>/dev/null | head -n 1)"
if [ -z "$TRANSPORT_TEST" ] || [ ! -s "$TRANSPORT_TEST" ]; then
  record "REAL_DEVICE_HOOK_ACCEPTANCE: FAIL reason=NATIVE_TRANSPORT_TEST_MISSING"
  exit 1
fi
run_adb push "$TRANSPORT_TEST" /data/local/tmp/remote_preferences_transport_test >/dev/null
run_adb shell chmod 0755 /data/local/tmp/remote_preferences_transport_test
TRANSPORT_OUTPUT="$(run_root /data/local/tmp/remote_preferences_transport_test | tr -d '\r')"
run_adb shell rm -f /data/local/tmp/remote_preferences_transport_test
if [ "$TRANSPORT_OUTPUT" = "REMOTE_PREFERENCES_TRANSPORT_TEST_PASS" ]; then
  record "PASS native_remote_preferences_transport=true"
else
  record "REAL_DEVICE_HOOK_ACCEPTANCE: FAIL reason=NATIVE_TRANSPORT_TEST_FAILED"
  exit 1
fi

if [ "$API" -lt 26 ]; then
  record "REAL_DEVICE_HOOK_ACCEPTANCE: FAIL reason=API_TOO_LOW"
  exit 1
fi

run_adb push "$MODULE_ZIP" "/sdcard/Download/zygisk-framework-module.zip" >/dev/null
run_root "/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/zygisk-framework-module.zip"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework info $MODULE_ID"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $PREFERENCES_GROUP set boolean_value boolean false"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $PREFERENCES_GROUP set string_value string fixed-value"
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
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $PREFERENCES_GROUP set boolean_value boolean false"

# 同版本覆盖安装必须保留独立的数据目录。
run_root "/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/zygisk-framework-module.zip"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework scope $MODULE_ID set $PACKAGE"
PREF_AFTER_UPGRADE="$(run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $PREFERENCES_GROUP get boolean_value")"
if printf '%s\n' "$PREF_AFTER_UPGRADE" | grep -Fq $'boolean\tboolean_value\tfalse'; then
  record "PASS preferences_preserved_on_upgrade=true"
else
  record "FAIL preferences_preserved_on_upgrade=false"
  exit 1
fi

# 标识验收从默认配置开始，独立 verification 组不控制业务行为。
run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $TEMPLATE_GROUP clear"
run_adb shell am force-stop "$PACKAGE" >/dev/null || true
run_adb logcat -c
FAIL=0
# 目标启动时必须实际读取 Android ID；require-gaid 模式还必须触发标准 SDK getId。
run_adb shell am start -W -n "$LAUNCHER_COMPONENT" -f 0x10008000 >/dev/null
sleep 5
TARGET_PID="$(run_adb shell pidof "$PACKAGE" | tr -d '\r' || true)"
record "targetPid=$TARGET_PID"
if [ -z "$TARGET_PID" ]; then
  record "FAIL target_process_running=false"
  FAIL=1
fi

LOG_FILE="$(mktemp)"
trap 'rm -f "$LOG_FILE"' EXIT
capture_logs
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
require_marker "FIXED_ANDROID_ID_APPLIED" || FAIL=1
if CONFIG_RESULT="$(verify_remote_config_snapshot "$LOG_FILE" REMOTE_CONFIG_LOADED "$TEMPLATE_GROUP" "$PACKAGE" true 0000000000000000 00000000-0000-0000-0000-000000000000)"; then
  record "$CONFIG_RESULT phase=startup"
else
  record "$CONFIG_RESULT phase=startup"
  FAIL=1
fi
if grep -F "TEMPLATE_HOOK_INSTALLED package=$PACKAGE" "$LOG_FILE" | grep -Eq 'failed=[1-9][0-9]*'; then
  record "FAIL template_installation_failure=true"
  FAIL=1
fi

if GAID_RESULT="$(verify_gaid_events "$LOG_FILE" "$REQUIRE_GAID")"; then
  record "$GAID_RESULT"
else
  record "$GAID_RESULT"
  FAIL=1
fi

if [ "$CHECK_REMOTE_PREFERENCES" = true ]; then
  CUSTOM_ANDROID_ID="1111111111111111"
  CUSTOM_GAID="11111111-1111-1111-1111-111111111111"
  run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $TEMPLATE_GROUP set android_id string $CUSTOM_ANDROID_ID"
  run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $TEMPLATE_GROUP set gaid string $CUSTOM_GAID"
  wait_remote_config custom true "$CUSTOM_ANDROID_ID" "$CUSTOM_GAID" || FAIL=1
  run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $TEMPLATE_GROUP set enabled boolean false"
  wait_remote_config disabled false "$CUSTOM_ANDROID_ID" "$CUSTOM_GAID" || FAIL=1
  run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $TEMPLATE_GROUP remove android_id"
  wait_remote_config removed false 0000000000000000 "$CUSTOM_GAID" || FAIL=1
  run_root "/data/adb/zygisk_framework/bin/zygisk_framework prefs $MODULE_ID $TEMPLATE_GROUP clear"
  wait_remote_config cleared true 0000000000000000 00000000-0000-0000-0000-000000000000 || FAIL=1
  capture_logs
  cat "$LOG_FILE" >> "$REPORT"
  # 配置快照更新成功不代表应用再次读取了标识；单独报告实际自定义值事件。
  for IDENTIFIER in android_id gaid; do
    if [ "$IDENTIFIER" = android_id ]; then
      APPLIED_MARKER="FIXED_ANDROID_ID_APPLIED value=$CUSTOM_ANDROID_ID"
    else
      APPLIED_MARKER="GAID_HOOK_APPLIED value=$CUSTOM_GAID"
    fi
    if grep -F "$APPLIED_MARKER" "$LOG_FILE" | grep -Fq "package=$PACKAGE process=$PACKAGE "; then
      record "PASS ${IDENTIFIER}_remote_value=APPLIED"
    else
      record "NOT_VERIFIED ${IDENTIFIER}_remote_value=NO_APPLIED_EVENT"
    fi
  done
fi

if grep -E 'FATAL EXCEPTION|Fatal signal|Abort message' "$LOG_FILE" >/dev/null; then
  record "FAIL crash_marker_present=true"
  FAIL=1
else
  record "PASS crash_marker_present=false"
fi

run_root "/data/adb/zygisk_framework/bin/zygisk_framework disable $MODULE_ID"
run_root "/data/adb/zygisk_framework/bin/zygisk_framework enable $MODULE_ID"
run_adb shell am force-stop "$PACKAGE" >/dev/null || true
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
