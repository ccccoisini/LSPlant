#pragma once

#include <android/log.h>

#define ZYGISK_FRAMEWORK_LOG_TAG_NATIVE "ZH.Native"
#define ZYGISK_FRAMEWORK_LOG_TAG_COMPANION "ZH.Companion"

#define ZYGISK_FRAMEWORK_LOGI(tag, ...) __android_log_print(ANDROID_LOG_INFO, tag, __VA_ARGS__)
#define ZYGISK_FRAMEWORK_LOGW(tag, ...) __android_log_print(ANDROID_LOG_WARN, tag, __VA_ARGS__)
#define ZYGISK_FRAMEWORK_LOGE(tag, ...) __android_log_print(ANDROID_LOG_ERROR, tag, __VA_ARGS__)
