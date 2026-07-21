#pragma once

#include <android/log.h>

#define ZH_LOG_TAG_NATIVE "ZHook.Native"
#define ZH_LOG_TAG_COMPANION "ZHook.Companion"

#define ZH_LOGI(tag, ...) __android_log_print(ANDROID_LOG_INFO, tag, __VA_ARGS__)
#define ZH_LOGW(tag, ...) __android_log_print(ANDROID_LOG_WARN, tag, __VA_ARGS__)
#define ZH_LOGE(tag, ...) __android_log_print(ANDROID_LOG_ERROR, tag, __VA_ARGS__)
