#pragma once

#include <jni.h>

namespace zhook {

/**
 * 向 framework.dex 中的 NativeBridge 类注册所有 JNI 方法。
 *
 * @param env 当前 JNI 环境
 * @param native_bridge_class 已通过 mapping 解析并加载的 NativeBridge 类
 * @return 注册成功时返回 true
 */
bool RegisterNativeBridge(JNIEnv *env, jclass native_bridge_class);

}  // namespace zhook
