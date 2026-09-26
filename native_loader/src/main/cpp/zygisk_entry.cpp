#include "zygisk.hpp"

#include <jni.h>

#include <string>

#include "dex_loader.hpp"
#include "logging.hpp"
#include "remote_preferences.hpp"
#include "state.hpp"
#include "target_client.hpp"

namespace zygisk_framework {
namespace {

std::string JStringToString(JNIEnv *env, jstring value) {
    if (env == nullptr || value == nullptr) {
        return "";
    }
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return "";
    }
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

std::string ExtractPackageName(const std::string &process_name, const std::string &app_data_dir) {
    if (!app_data_dir.empty()) {
        size_t end = app_data_dir.find_last_not_of('/');
        if (end != std::string::npos) {
            size_t begin = app_data_dir.find_last_of('/', end);
            return app_data_dir.substr(begin == std::string::npos ? 0 : begin + 1,
                                       end - (begin == std::string::npos ? 0 : begin));
        }
    }
    size_t colon = process_name.find(':');
    return colon == std::string::npos ? process_name : process_name.substr(0, colon);
}

jobject CurrentContextClassLoader(JNIEnv *env) {
    jclass thread_class = env->FindClass("java/lang/Thread");
    jmethodID current_thread = env->GetStaticMethodID(
            thread_class, "currentThread", "()Ljava/lang/Thread;");
    jobject thread = env->CallStaticObjectMethod(thread_class, current_thread);
    jmethodID get_loader = env->GetMethodID(
            thread_class, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = env->CallObjectMethod(thread, get_loader);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return loader;
}

void CloseProcessState(ProcessState &state) {
    for (auto &module : state.modules) {
        CloseFd(module.dex_fd);
    }
    CloseFd(state.companion_fd);
}

}  // namespace

/**
 * Zygisk 模块实现，负责普通 App 进程筛选和 framework.dex 启动。
 */
class ZygiskFrameworkModule final : public zygisk::ModuleBase {
public:
    /**
     * 保存 Zygisk API 和初始 JNI 环境。
     *
     * @param api Zygisk 提供的稳定 API 表
     * @param env 当前 JNI 环境
     */
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
    }

    /**
     * 在 App specialize 前通过 Root Companion 判断是否命中目标。
     *
     * @param args Zygisk App specialize 参数
     */
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        if (args == nullptr || env_ == nullptr) {
            return;
        }
        state_ = ProcessState();
        state_.process_name = JStringToString(env_, args->nice_name);
        state_.package_name = ExtractPackageName(
                state_.process_name,
                JStringToString(env_, args->app_data_dir));
        if (state_.process_name.empty()) {
            return;
        }
        if (!QueryCompanion(api_, env_, args, state_.process_name, state_)) {
            CloseProcessState(state_);
            return;
        }
    }

    /**
     * 在 App specialize 后装载 framework.dex 和独立业务模块 DEX。
     *
     * @param args Zygisk App specialize 参数
     */
    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        (void) args;
        if (!state_.target || env_ == nullptr) {
            return;
        }
        InitializeRemotePreferences(state_);
        jobject app_loader = CurrentContextClassLoader(env_);
        bool ok = LoadFrameworkAndBootstrap(env_, state_, app_loader);
        CloseProcessState(state_);
        if (!ok) {
            CloseRemotePreferences();
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "MODULE_ENTRY_FAILED code=POST_BOOTSTRAP_FAILED");
        }
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    ProcessState state_;
};

}  // namespace zygisk_framework

/**
 * 注册 Zygisk 模块入口给 Magisk 调用。
 */
REGISTER_ZYGISK_MODULE(zygisk_framework::ZygiskFrameworkModule)
