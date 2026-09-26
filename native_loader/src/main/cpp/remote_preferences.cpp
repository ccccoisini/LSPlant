#include "remote_preferences.hpp"

#include <unistd.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "fd_protocol.hpp"
#include "logging.hpp"
#include "preference_store.hpp"

namespace zygisk_framework {
namespace {

std::mutex &PreferenceMutex() {
    static auto *mutex = new std::mutex();
    return *mutex;
}

std::map<std::string, std::map<std::string, std::string>> &PreferenceCache() {
    static auto *cache = new std::map<std::string, std::map<std::string, std::string>>();
    return *cache;
}

int &PreferenceSocket() {
    static int socket = -1;
    return socket;
}

std::string JStringToString(JNIEnv *env, jstring value) {
    if (env == nullptr || value == nullptr) return "";
    jsize length = env->GetStringLength(value);
    const jchar *chars = env->GetStringChars(value, nullptr);
    if (chars == nullptr) return "";
    std::string result;
    result.reserve(static_cast<size_t>(length) * 3);
    for (jsize index = 0; index < length; ++index) {
        uint32_t code_point = chars[index];
        if (code_point >= 0xd800 && code_point <= 0xdbff) {
            if (++index >= length || chars[index] < 0xdc00 || chars[index] > 0xdfff) {
                result.clear();
                break;
            }
            code_point = 0x10000 + ((code_point - 0xd800) << 10) + (chars[index] - 0xdc00);
        } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
            result.clear();
            break;
        }
        if (code_point <= 0x7f) {
            result.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        } else if (code_point <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        }
    }
    env->ReleaseStringChars(value, chars);
    return result;
}

jbyteArray ToByteArray(JNIEnv *env, const std::string &value) {
    jbyteArray output = env->NewByteArray(static_cast<jsize>(value.size()));
    if (output != nullptr && !value.empty()) {
        env->SetByteArrayRegion(output, 0, static_cast<jsize>(value.size()),
                                reinterpret_cast<const jbyte *>(value.data()));
    }
    return output;
}

void PutU32(std::string &output, uint32_t value) {
    output.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

void PutString(std::string &output, const std::string &value) {
    PutU32(output, static_cast<uint32_t>(value.size()));
    output.append(value);
}

}  // namespace

void InitializeRemotePreferences(ProcessState &state) {
    std::lock_guard lock(PreferenceMutex());
    PreferenceCache().clear();
    for (const auto &module : state.modules) {
        PreferenceCache().emplace(module.module_id, module.remote_preferences);
    }
    if (PreferenceSocket() >= 0) close(PreferenceSocket());
    PreferenceSocket() = state.companion_fd;
    state.companion_fd = -1;
}

void CloseRemotePreferences() {
    std::lock_guard lock(PreferenceMutex());
    if (PreferenceSocket() >= 0) {
        close(PreferenceSocket());
        PreferenceSocket() = -1;
    }
}

jbyteArray NativeGetRemotePreferencesSnapshot(JNIEnv *env, jclass,
                                              jstring module_id, jstring group) {
    std::string native_module_id = JStringToString(env, module_id);
    std::string native_group = JStringToString(env, group);
    std::string snapshot;
    {
        std::lock_guard lock(PreferenceMutex());
        auto module_it = PreferenceCache().find(native_module_id);
        if (module_it != PreferenceCache().end()) {
            auto group_it = module_it->second.find(native_group);
            if (group_it != module_it->second.end()) snapshot = group_it->second;
        }
    }
    return ToByteArray(env, snapshot);
}

jbyteArray NativeAwaitRemotePreferencesUpdate(JNIEnv *env, jclass) {
    while (true) {
        int socket = -1;
        {
            std::lock_guard lock(PreferenceMutex());
            socket = PreferenceSocket();
        }
        if (socket < 0) return nullptr;

        std::string module_id;
        std::string group;
        std::string snapshot;
        if (!ReceivePreferenceUpdate(socket, module_id, group, snapshot)) {
            std::lock_guard lock(PreferenceMutex());
            if (PreferenceSocket() == socket) {
                close(PreferenceSocket());
                PreferenceSocket() = -1;
            }
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                    "REMOTE_PREFS_DEGRADED reason=CHANNEL_CLOSED");
            return nullptr;
        }
        if (!snapshot.empty() && !ValidatePreferenceSnapshot(group, snapshot)) {
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                    "REMOTE_PREFS_INVALID id=%s group=%s", module_id.c_str(), group.c_str());
            continue;
        }
        {
            std::lock_guard lock(PreferenceMutex());
            auto module_it = PreferenceCache().find(module_id);
            if (module_it == PreferenceCache().end()) {
                ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                        "REMOTE_PREFS_INVALID id=%s reason=UNKNOWN_MODULE", module_id.c_str());
                continue;
            }
            module_it->second[group] = snapshot;
        }
        std::string event;
        PutString(event, module_id);
        PutString(event, group);
        PutString(event, snapshot);
        return ToByteArray(env, event);
    }
}

}  // namespace zygisk_framework
