#pragma once

#include <jni.h>

#include "state.hpp"

namespace zygisk_framework {

/**
 * 把 specialize 前取得的 Preferences 快照和只读共享映射转移到进程级缓存。
 *
 * @param state 当前目标进程状态；调用后共享映射所有权被转移
 */
void InitializeRemotePreferences(ProcessState &state);

/**
 * 关闭进程级 Remote Preferences 实时通道。
 */
void CloseRemotePreferences();

/**
 * 返回指定模块和组的最后有效快照。
 *
 * @param env 当前 JNI 环境
 * @param module_id Java 模块 ID
 * @param group Java Preferences 组名
 * @return 快照字节；组不存在时返回空数组
 */
jbyteArray NativeGetRemotePreferencesSnapshot(JNIEnv *env, jclass,
                                              jstring module_id, jstring group);

/**
 * 阻塞等待实时 Preferences 更新并返回内部事件帧。
 *
 * @param env 当前 JNI 环境
 * @return 更新事件；共享通道关闭或降级时返回 null
 */
jbyteArray NativeAwaitRemotePreferencesUpdate(JNIEnv *env, jclass);

}  // namespace zygisk_framework
