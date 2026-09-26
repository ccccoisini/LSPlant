#include "remote_preferences.hpp"

#include <sys/mman.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "logging.hpp"
#include "preference_store.hpp"
#include "shared_preferences_transport.hpp"

namespace zygisk_framework {
namespace {

struct RemotePreferencesState {
    std::mutex mutex;
    std::condition_variable condition;
    std::map<std::string, std::map<std::string, std::string>> cache;
    std::deque<std::tuple<std::string, std::string, std::string>> pending_events;
    std::vector<std::string> module_ids;
    std::vector<uint32_t> generations;
    void *region = nullptr;
    size_t region_size = 0;
    size_t waiters = 0;
    bool closing = false;
    bool disabled = false;
};

RemotePreferencesState &State() {
    static auto *state = new RemotePreferencesState();
    return *state;
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

jbyteArray TakePendingEvent(JNIEnv *env, std::unique_lock<std::mutex> &lock) {
    RemotePreferencesState &state = State();
    auto [module_id, group, snapshot] = std::move(state.pending_events.front());
    state.pending_events.pop_front();
    if (--state.waiters == 0) state.condition.notify_all();
    lock.unlock();
    std::string event;
    PutString(event, module_id);
    PutString(event, group);
    PutString(event, snapshot);
    return ToByteArray(env, event);
}

void LeaveWaiter(std::unique_lock<std::mutex> &lock) {
    RemotePreferencesState &state = State();
    if (--state.waiters == 0) state.condition.notify_all();
    lock.unlock();
}

bool ValidateModuleSnapshot(const std::map<std::string, std::string> &groups) {
    for (const auto &[group, snapshot] : groups) {
        if (!ValidatePreferenceSnapshot(group, snapshot)) return false;
    }
    return true;
}

void QueueModuleChangesLocked(size_t module_index,
                              std::map<std::string, std::string> current) {
    RemotePreferencesState &state = State();
    const std::string &module_id = state.module_ids[module_index];
    auto &previous = state.cache[module_id];
    std::set<std::string> groups;
    for (const auto &[group, snapshot] : previous) {
        (void) snapshot;
        groups.insert(group);
    }
    for (const auto &[group, snapshot] : current) {
        (void) snapshot;
        groups.insert(group);
    }
    for (const std::string &group : groups) {
        auto old_value = previous.find(group);
        auto new_value = current.find(group);
        bool changed = old_value == previous.end() || new_value == current.end() ||
                       old_value->second != new_value->second;
        if (!changed) continue;
        state.pending_events.emplace_back(
                module_id, group,
                new_value == current.end() ? std::string() : new_value->second);
    }
    previous = std::move(current);
}

}  // namespace

void InitializeRemotePreferences(ProcessState &process) {
    RemotePreferencesState &state = State();
    std::lock_guard lock(state.mutex);
    state.cache.clear();
    state.pending_events.clear();
    state.module_ids.clear();
    state.generations.clear();
    state.closing = false;
    state.disabled = false;
    for (const auto &module : process.modules) {
        state.cache.emplace(module.module_id, module.remote_preferences);
        state.module_ids.push_back(module.module_id);
        state.generations.push_back(module.remote_preferences_generation);
    }
    state.region = process.remote_preferences_region;
    state.region_size = process.remote_preferences_region_size;
    process.remote_preferences_region = nullptr;
    process.remote_preferences_region_size = 0;
    if (state.region != nullptr) {
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                "REMOTE_PREFS_CHANNEL_READY mode=SHARED_MEMORY");
    }
}

void CloseRemotePreferences() {
    RemotePreferencesState &state = State();
    std::unique_lock lock(state.mutex);
    state.closing = true;
    WakeSharedPreferencesReaders(state.region);
    state.condition.wait(lock, [&state]() { return state.waiters == 0; });
    if (state.region != nullptr) munmap(state.region, state.region_size);
    state.region = nullptr;
    state.region_size = 0;
    state.disabled = true;
}

jbyteArray NativeGetRemotePreferencesSnapshot(JNIEnv *env, jclass,
                                              jstring module_id, jstring group) {
    std::string native_module_id = JStringToString(env, module_id);
    std::string native_group = JStringToString(env, group);
    std::string snapshot;
    RemotePreferencesState &state = State();
    {
        std::lock_guard lock(state.mutex);
        auto module_it = state.cache.find(native_module_id);
        if (module_it != state.cache.end()) {
            auto group_it = module_it->second.find(native_group);
            if (group_it != module_it->second.end()) snapshot = group_it->second;
        }
    }
    return ToByteArray(env, snapshot);
}

jbyteArray NativeAwaitRemotePreferencesUpdate(JNIEnv *env, jclass) {
    RemotePreferencesState &state = State();
    std::unique_lock lock(state.mutex);
    if (state.region == nullptr || state.closing || state.disabled) return nullptr;
    ++state.waiters;

    while (true) {
        if (!state.pending_events.empty()) return TakePendingEvent(env, lock);
        if (state.closing || state.disabled) {
            LeaveWaiter(lock);
            return nullptr;
        }
        void *region = state.region;
        size_t region_size = state.region_size;
        uint32_t expected_global = ReadSharedPreferencesGlobalGeneration(region);
        std::vector<uint32_t> observed = state.generations;
        lock.unlock();

        for (size_t index = 0; index < observed.size(); ++index) {
            uint32_t current_generation = ReadSharedPreferencesModuleGeneration(
                    region, region_size, index);
            if (current_generation == 0 || current_generation == observed[index]) continue;
            std::map<std::string, std::string> current;
            uint32_t decoded_generation = 0;
            if (!ReadSharedPreferencesModule(region, region_size, index,
                                             current, decoded_generation)) {
                lock.lock();
                if (state.closing || state.region != region) {
                    LeaveWaiter(lock);
                    return nullptr;
                }
                state.generations[index] = current_generation;
                ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                        "REMOTE_PREFS_INVALID id=%s reason=SHARED_MEMORY_SNAPSHOT",
                        state.module_ids[index].c_str());
                lock.unlock();
                continue;
            }
            bool valid = ValidateModuleSnapshot(current);
            lock.lock();
            if (state.closing || state.region != region) {
                LeaveWaiter(lock);
                return nullptr;
            }
            state.generations[index] = decoded_generation;
            if (valid) {
                QueueModuleChangesLocked(index, std::move(current));
            } else {
                ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                        "REMOTE_PREFS_INVALID id=%s reason=SHARED_MEMORY_SNAPSHOT",
                        state.module_ids[index].c_str());
            }
            lock.unlock();
        }

        lock.lock();
        if (!state.pending_events.empty()) return TakePendingEvent(env, lock);
        if (state.closing || state.region != region) {
            LeaveWaiter(lock);
            return nullptr;
        }
        uint64_t heartbeat = ReadSharedPreferencesHeartbeat(region);
        uint64_t now = SharedPreferencesBootTimeNs();
        if (heartbeat == 0 || now > heartbeat + kSharedPreferencesStaleTimeoutNs) {
            state.disabled = true;
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                    "REMOTE_PREFS_DEGRADED reason=MANAGER_STALLED");
            LeaveWaiter(lock);
            return nullptr;
        }
        if (ReadSharedPreferencesGlobalGeneration(region) != expected_global) continue;
        lock.unlock();
        timespec timeout{20, 0};
        WaitForSharedPreferencesGeneration(region, expected_global, &timeout);
        lock.lock();
    }
}

}  // namespace zygisk_framework
