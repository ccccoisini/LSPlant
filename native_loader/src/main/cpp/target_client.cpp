#include "target_client.hpp"

#include <unistd.h>
#include <fcntl.h>

#include <vector>

#include "fd_protocol.hpp"
#include "logging.hpp"

namespace zygisk_framework {
namespace {

bool ReadFdFully(int fd, size_t max_size, std::vector<uint8_t> &bytes) {
    if (fd < 0) {
        return false;
    }
    off_t size = lseek(fd, 0, SEEK_END);
    if (size <= 0 || static_cast<size_t>(size) > max_size || lseek(fd, 0, SEEK_SET) < 0) {
        return false;
    }
    bytes.assign(static_cast<size_t>(size), 0);
    size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t read_size = TEMP_FAILURE_RETRY(read(fd, bytes.data() + offset, bytes.size() - offset));
        if (read_size <= 0) {
            return false;
        }
        offset += static_cast<size_t>(read_size);
    }
    return true;
}

bool ReadDefaultModulePathsIntoMemory(ProcessState &state) {
    for (auto &module : state.modules) {
        std::string module_path =
                "/data/adb/zygisk_framework/modules/" + module.module_id + "/module.dex";
        int fd = open(module_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return false;
        }
        bool ok = ReadFdFully(fd, 32 * 1024 * 1024, module.dex_bytes);
        close(fd);
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool ReadAllModuleDexIntoMemory(ProcessState &state) {
    for (auto &module : state.modules) {
        if (!ReadFdFully(module.dex_fd, 32 * 1024 * 1024, module.dex_bytes)) {
            return false;
        }
        CloseFd(module.dex_fd);
    }
    return true;
}

bool AppendFdToIgnore(JNIEnv *env, zygisk::AppSpecializeArgs *args, int fd) {
    if (env == nullptr || args == nullptr || args->fds_to_ignore == nullptr || fd < 0) {
        return false;
    }
    jintArray current = *args->fds_to_ignore;
    jsize current_size = current == nullptr ? 0 : env->GetArrayLength(current);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    std::vector<jint> values(static_cast<size_t>(current_size) + 1);
    if (current_size > 0) {
        env->GetIntArrayRegion(current, 0, current_size, values.data());
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        for (jsize index = 0; index < current_size; ++index) {
            if (values[static_cast<size_t>(index)] == fd) return true;
        }
    }
    values[static_cast<size_t>(current_size)] = fd;
    jintArray replacement = env->NewIntArray(current_size + 1);
    if (replacement == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    env->SetIntArrayRegion(replacement, 0, current_size + 1, values.data());
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    *args->fds_to_ignore = replacement;
    return true;
}

bool PreserveFdAcrossSpecialize(zygisk::Api *api, JNIEnv *env,
                                zygisk::AppSpecializeArgs *args, int fd) {
    if (api->exemptFd(fd)) return true;
    if (!AppendFdToIgnore(env, args, fd)) return false;
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
            "FD_EXEMPT_FALLBACK mode=FDS_TO_IGNORE");
    return true;
}

}  // namespace

bool QueryCompanion(zygisk::Api *api, JNIEnv *env, zygisk::AppSpecializeArgs *args,
                    const std::string &process_name, ProcessState &state) {
    if (api == nullptr || env == nullptr || args == nullptr || process_name.empty()) {
        return false;
    }
    int socket = api->connectCompanion();
    if (socket < 0) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "TARGET_CONFIG_ERROR code=COMPANION_CONNECT_FAILED");
        return false;
    }
    bool ok = SendProcessQuery(socket, process_name) &&
              ReceiveCompanionResponse(socket, state);
    if (!ok || !state.target) {
        close(socket);
        if (!ok) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "TARGET_CONFIG_ERROR code=COMPANION_RESPONSE_FAILED");
        }
        return false;
    }

    if (PreserveFdAcrossSpecialize(api, env, args, socket)) {
        state.companion_fd = socket;
    } else {
        close(socket);
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                "REMOTE_PREFS_DEGRADED reason=SOCKET_EXEMPT_FAILED");
    }

    bool has_module_fd = false;
    for (const auto &module : state.modules) {
        has_module_fd = module.dex_fd >= 0 || has_module_fd;
    }
    if (!has_module_fd) {
        bool all_modules_have_bytes = true;
        for (const auto &module : state.modules) {
            all_modules_have_bytes = !module.dex_bytes.empty() && all_modules_have_bytes;
        }
        if (all_modules_have_bytes) {
            ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_FD_RECEIVED code=PAYLOAD_MEMORY modules=%zu",
                    state.modules.size());
            return true;
        }
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_FD_RECEIVED code=NO_FD_FALLBACK_PATH");
        if (!ReadDefaultModulePathsIntoMemory(state)) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_PATH_MEMORY_FALLBACK_FAILED");
            state.target = false;
            return false;
        }
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_FD_RECEIVED modules=%zu", state.modules.size());
        return true;
    }

    bool exempt_ok = true;
    for (const auto &module : state.modules) {
        if (module.dex_fd >= 0) {
            exempt_ok = PreserveFdAcrossSpecialize(api, env, args, module.dex_fd) && exempt_ok;
        }
    }
    if (!exempt_ok) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_FD_RECEIVED code=EXEMPT_FAILED_FALLBACK_MEMORY");
        if (!ReadAllModuleDexIntoMemory(state)) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=FD_MEMORY_FALLBACK_FAILED");
            for (auto &module : state.modules) {
                CloseFd(module.dex_fd);
            }
            state.target = false;
            return false;
        }
    }
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_FD_RECEIVED modules=%zu", state.modules.size());
    return true;
}

}  // namespace zygisk_framework
