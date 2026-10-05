#!/usr/bin/env bash

# 只验证模板发布的完整配置快照；不能代替 Android ID/GAID 实际替换事件。
verify_remote_config_snapshot() {
  local log_file="$1"
  local marker="$2"
  local group="$3"
  local package="$4"
  local enabled="$5"
  local android_id="$6"
  local gaid="$7"
  if awk -v marker="$marker" -v expected_group="$group" -v expected_package="$package" \
    -v expected_enabled="$enabled" -v expected_android_id="$android_id" -v expected_gaid="$gaid" '
    {
      has_marker=0; group_value=""; package_value=""; process_value="";
      enabled_value=""; android_id_value=""; gaid_value="";
      for (i=1; i<=NF; i++) {
        if ($i == marker) has_marker=1;
        separator=index($i, "=");
        key=substr($i, 1, separator-1); value=substr($i, separator+1);
        if (key == "group") group_value=value;
        if (key == "package") package_value=value;
        if (key == "process") process_value=value;
        if (key == "enabled") enabled_value=value;
        if (key == "android_id") android_id_value=value;
        if (key == "gaid") gaid_value=value;
      }
      if (has_marker && group_value == expected_group && package_value == expected_package &&
          process_value == expected_package) {
        found=1;
        matches=(enabled_value == expected_enabled && android_id_value == expected_android_id && gaid_value == expected_gaid);
      }
    }
    END { if (!found) exit 1; if (!matches) exit 2; }
  ' "$log_file"; then
    echo "PASS remote_preferences=$marker"
  else
    local status=$?
    if [ "$status" -eq 1 ]; then
      echo "FAIL remote_preferences=SNAPSHOT_NOT_OBSERVED"
    else
      echo "FAIL remote_preferences=UNEXPECTED_SNAPSHOT"
    fi
    return 1
  fi
}
