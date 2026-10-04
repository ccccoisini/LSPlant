#!/usr/bin/env bash

# 纯日志判定函数：安装标记不能代替实际 APPLIED 事件，便于独立测试。
verify_gaid_events() {
  local log_file="$1"
  local required="$2"
  if grep -Fq "GAID_HOOK_FAILED" "$log_file"; then
    echo "FAIL gaid=INSTALL_FAILED"
    return 1
  elif grep -Fq "GAID_HOOK_APPLIED" "$log_file"; then
    echo "PASS gaid=APPLIED"
  elif [ "$required" = true ]; then
    echo "FAIL gaid=NOT_APPLIED"
    return 1
  elif grep -Fq "GAID_HOOK_SKIPPED" "$log_file"; then
    echo "SKIP gaid=CLASS_OR_METHOD_NOT_FOUND"
  else
    echo "NOT_VERIFIED gaid=NO_APPLIED_EVENT"
  fi
}
