#pragma once

#include <jni.h>

#include <string>

namespace zygisk_framework {

/**
 * 初始化 LSPlant，并配置 Dobby inline hook 后端。
 *
 * @param env 当前进程的 JNI 环境
 * @return 初始化成功时返回 true
 */
bool InitLsplant(JNIEnv *env);

/**
 * 为 Java 反射目标安装 LSPlant Hook。
 *
 * @param env 当前 JNI 环境
 * @param executable java.lang.reflect.Executable 对象
 * @param hooker_object Java HookRecord 分发对象
 * @param callback_method HookRecord.dispatch(Object[]) 方法
 * @return LSPlant backup Method；失败时返回 nullptr
 */
jobject HookJavaMethod(JNIEnv *env, jobject executable, jobject hooker_object, jobject callback_method);

/**
 * 卸载 Java 反射目标的 LSPlant Hook。
 *
 * @param env 当前 JNI 环境
 * @param executable java.lang.reflect.Executable 对象
 * @return 卸载成功时返回 true
 */
bool UnhookJavaMethod(JNIEnv *env, jobject executable);

/**
 * 查询 Java 反射目标是否已被 LSPlant Hook。
 *
 * @param env 当前 JNI 环境
 * @param executable java.lang.reflect.Executable 对象
 * @return 已 Hook 时返回 true
 */
bool IsJavaMethodHooked(JNIEnv *env, jobject executable);

/**
 * 请求 LSPlant 对 Java 反射目标执行去优化。
 *
 * @param env 当前 JNI 环境
 * @param executable java.lang.reflect.Executable 对象
 * @return 去优化成功时返回 true
 */
bool DeoptimizeJavaMethod(JNIEnv *env, jobject executable);

}  // namespace zygisk_framework
