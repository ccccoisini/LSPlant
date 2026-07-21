#include "lsplant_engine.hpp"

#include <dobby.h>
#include <lsplant.hpp>
#include <unistd.h>

#include <mutex>

#include "art_symbol_resolver.hpp"
#include "logging.hpp"

namespace zhook {
namespace {

std::once_flag g_init_once;
bool g_init_result = false;

void *DobbyInlineHook(void *target, void *hooker) {
    void *origin = nullptr;
    int result = DobbyHook(target, reinterpret_cast<dobby_dummy_func_t>(hooker),
                           reinterpret_cast<dobby_dummy_func_t *>(&origin));
    return result == 0 ? origin : nullptr;
}

bool DobbyInlineUnhook(void *target) {
    return DobbyDestroy(target) == 0;
}

}  // namespace

bool InitLsplant(JNIEnv *env) {
    std::call_once(g_init_once, [env] {
        long page_size = sysconf(_SC_PAGESIZE);
        ZH_LOGI(ZH_LOG_TAG_NATIVE, "LSPLANT_INIT_BEGIN page_size=%ld", page_size);
        lsplant::InitInfo info;
        info.inline_hooker = DobbyInlineHook;
        info.inline_unhooker = DobbyInlineUnhook;
        info.art_symbol_resolver = ResolveArtSymbol;
        info.art_symbol_prefix_resolver = ResolveArtSymbolPrefix;
        info.generated_class_name = "ZHookGenerated_";
        info.generated_source_name = "ZHook";
        info.generated_field_name = "hooker";
        info.generated_method_name = "{target}";
        g_init_result = lsplant::Init(env, info);
        if (g_init_result) {
            ZH_LOGI(ZH_LOG_TAG_NATIVE, "LSPLANT_INIT_OK");
        } else {
            ZH_LOGE(ZH_LOG_TAG_NATIVE, "LSPLANT_INIT_FAILED code=INIT_RETURN_FALSE");
        }
    });
    return g_init_result;
}

jobject HookJavaMethod(JNIEnv *env, jobject executable, jobject hooker_object, jobject callback_method) {
    if (!g_init_result && !InitLsplant(env)) {
        return nullptr;
    }
    return lsplant::Hook(env, executable, hooker_object, callback_method);
}

bool UnhookJavaMethod(JNIEnv *env, jobject executable) {
    return g_init_result && lsplant::UnHook(env, executable);
}

bool IsJavaMethodHooked(JNIEnv *env, jobject executable) {
    return g_init_result && lsplant::IsHooked(env, executable);
}

bool DeoptimizeJavaMethod(JNIEnv *env, jobject executable) {
    if (!g_init_result && !InitLsplant(env)) {
        return false;
    }
    return lsplant::Deoptimize(env, executable);
}

}  // namespace zhook
