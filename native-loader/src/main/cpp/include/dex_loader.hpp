#pragma once

#include <jni.h>

#include "state.hpp"

namespace zhook {

/**
 * 装载 framework.dex，按 mapping 绑定 NativeBridge，并启动 Java Runtime。
 *
 * @param env 当前 JNI 环境
 * @param state 当前目标进程状态和 FD 列表
 * @param app_class_loader 当前线程可见的 App ClassLoader，可为空
 * @return 启动成功时返回 true
 */
bool LoadFrameworkAndBootstrap(JNIEnv *env, ProcessState &state, jobject app_class_loader);

/**
 * 临时把一个 DirectByteBuffer DEX 挂载到宿主 ClassLoader，预加载全部类后恢复原始
 * dexElements。
 *
 * @param env 当前 JNI 环境
 * @param dex_buffer 指向 DEX 字节的 DirectByteBuffer
 * @param class_loader 需要临时挂载 DEX 的宿主 ClassLoader
 * @return 全部类预加载且 dexElements 恢复成功时返回 true
 */
bool PreloadDexIntoClassLoader(JNIEnv *env, jobject dex_buffer, jobject class_loader);

/**
 * 创建一个独立的模块 ClassLoader，并把 DirectByteBuffer DEX 生成的 dexElements 写入该
 * ClassLoader 自身的 DexPathList。
 *
 * @param env 当前 JNI 环境
 * @param dex_buffer 指向 DEX 字节的 DirectByteBuffer
 * @param parent 模块 ClassLoader 的父加载器
 * @return 创建成功的模块 ClassLoader；失败时返回 nullptr
 */
jobject CreateDexClassLoader(JNIEnv *env, jobject dex_buffer, jobject parent);

}  // namespace zhook
