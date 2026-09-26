#include "jni_bridge.hpp"

#include <string>

#include "dex_loader.hpp"
#include "logging.hpp"
#include "lsplant_engine.hpp"
#include "remote_preferences.hpp"

#ifndef ZYGISK_FRAMEWORK_LSPLANT_COMMIT
#define ZYGISK_FRAMEWORK_LSPLANT_COMMIT "unknown"
#endif

#ifndef ZYGISK_FRAMEWORK_DOBBY_COMMIT
#define ZYGISK_FRAMEWORK_DOBBY_COMMIT "unknown"
#endif

namespace zygisk_framework {
namespace {

jobject NativeHook(JNIEnv *env, jclass, jobject executable, jobject hooker_object,
                   jobject callback_method) {
    return HookJavaMethod(env, executable, hooker_object, callback_method);
}

jboolean NativeUnhook(JNIEnv *env, jclass, jobject executable) {
    return UnhookJavaMethod(env, executable) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeIsHooked(JNIEnv *env, jclass, jobject executable) {
    return IsJavaMethodHooked(env, executable) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeDeoptimize(JNIEnv *env, jclass, jobject executable) {
    return DeoptimizeJavaMethod(env, executable) ? JNI_TRUE : JNI_FALSE;
}

jstring NativeGetBuildInfo(JNIEnv *env, jclass) {
    std::string json = std::string("{\"lsplantCommit\":\"") + ZYGISK_FRAMEWORK_LSPLANT_COMMIT +
                       "\",\"dobbyCommit\":\"" + ZYGISK_FRAMEWORK_DOBBY_COMMIT +
                       "\",\"zygiskApi\":4}";
    return env->NewStringUTF(json.c_str());
}

jboolean NativePreloadDexInto(JNIEnv *env, jclass, jobject dex_buffer, jobject class_loader) {
    return PreloadDexIntoClassLoader(env, dex_buffer, class_loader) ? JNI_TRUE : JNI_FALSE;
}

jobject NativeCreateDexClassLoader(JNIEnv *env, jclass, jobject dex_buffer, jobject parent) {
    return CreateDexClassLoader(env, dex_buffer, parent);
}

JNINativeMethod kNativeBridgeMethods[] = {
        {"nativeHook",
         "(Ljava/lang/reflect/Executable;Ljava/lang/Object;Ljava/lang/reflect/Method;)"
         "Ljava/lang/reflect/Method;",
         reinterpret_cast<void *>(NativeHook)},
        {"nativeUnhook", "(Ljava/lang/reflect/Executable;)Z",
         reinterpret_cast<void *>(NativeUnhook)},
        {"nativeIsHooked", "(Ljava/lang/reflect/Executable;)Z",
         reinterpret_cast<void *>(NativeIsHooked)},
        {"nativeDeoptimize", "(Ljava/lang/reflect/Executable;)Z",
         reinterpret_cast<void *>(NativeDeoptimize)},
        {"nativeGetBuildInfo", "()Ljava/lang/String;",
         reinterpret_cast<void *>(NativeGetBuildInfo)},
        {"nativePreloadDexInto", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)Z",
         reinterpret_cast<void *>(NativePreloadDexInto)},
        {"nativeCreateDexClassLoader",
         "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)Ljava/lang/ClassLoader;",
         reinterpret_cast<void *>(NativeCreateDexClassLoader)},
        {"nativeGetRemotePreferencesSnapshot", "(Ljava/lang/String;Ljava/lang/String;)[B",
         reinterpret_cast<void *>(NativeGetRemotePreferencesSnapshot)},
        {"nativeAwaitRemotePreferencesUpdate", "()[B",
         reinterpret_cast<void *>(NativeAwaitRemotePreferencesUpdate)},
};

}  // namespace

bool RegisterNativeBridge(JNIEnv *env, jclass native_bridge_class) {
    if (native_bridge_class == nullptr) {
        return false;
    }
    int result = env->RegisterNatives(
            native_bridge_class,
            kNativeBridgeMethods,
            sizeof(kNativeBridgeMethods) / sizeof(kNativeBridgeMethods[0]));
    if (result != JNI_OK) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "NATIVE_BRIDGE_REGISTER_FAILED code=REGISTER_NATIVES");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return false;
    }
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "NATIVE_BRIDGE_REGISTERED");
    return true;
}

}  // namespace zygisk_framework
