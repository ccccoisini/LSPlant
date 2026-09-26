#include "fd_protocol.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "logging.hpp"
#include "preference_store.hpp"
#include "remote_preferences_manager.hpp"
#include "zygisk.hpp"

namespace zygisk_framework {
namespace {

constexpr const char *kHookModulesDir = "/data/adb/zygisk_framework/modules";
constexpr size_t kMaxDexSize = 32 * 1024 * 1024;

std::string Trim(const std::string &value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool ReadSmallFile(const std::string &path, size_t max_size, std::string &content) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<size_t>(st.st_size) > max_size) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return content.size() <= max_size;
}

bool MatchesRule(const std::string &process_name, const std::string &rule) {
    if (process_name == rule) {
        return true;
    }
    return rule.find(':') == std::string::npos &&
           process_name.rfind(rule + ":", 0) == 0;
}

bool IsValidModuleId(const std::string &module_id) {
    if (module_id.empty() || !std::isalpha(static_cast<unsigned char>(module_id[0]))) {
        return false;
    }
    return std::all_of(module_id.begin(), module_id.end(), [](unsigned char value) {
        return std::isalnum(value) || value == '.' || value == '_' || value == '-';
    });
}

bool ScopeMatches(const std::string &scope_list, const std::string &process_name) {
    std::istringstream stream(scope_list);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() > 512) {
            continue;
        }
        std::string rule = Trim(line);
        if (rule.empty() || rule[0] == '#') {
            continue;
        }
        if (MatchesRule(process_name, rule)) {
            return true;
        }
    }
    return false;
}

bool IsSafeRegularFile(const std::string &path, size_t max_size) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    if ((st.st_mode & S_IWOTH) != 0 || st.st_uid != 0 || st.st_size <= 0 ||
        static_cast<size_t>(st.st_size) > max_size) {
        return false;
    }
    return true;
}

int OpenSafeFile(const std::string &path, size_t max_size) {
    if (!IsSafeRegularFile(path, max_size)) {
        return -1;
    }
    return open(path.c_str(), O_RDONLY | O_CLOEXEC);
}

bool ReadMetadata(const std::string &module_dir, ModuleDescriptor &module) {
    std::string base = module_dir + "/META-INF/xposed/";
    return ReadSmallFile(base + "java_init.list", 64 * 1024, module.java_init_list) &&
           ReadSmallFile(base + "module.prop", 64 * 1024, module.module_prop) &&
           ReadSmallFile(base + "scope.list", 64 * 1024, module.scope_list);
}

void LoadModules(ProcessState &state) {
    DIR *dir = opendir(kHookModulesDir);
    if (dir == nullptr) {
        return;
    }
    std::vector<std::string> module_ids;
    while (dirent *entry = readdir(dir)) {
        std::string module_id = entry->d_name;
        if (module_id[0] == '.' || !IsValidModuleId(module_id)) continue;
        module_ids.push_back(module_id);
    }
    closedir(dir);
    std::sort(module_ids.begin(), module_ids.end());

    for (const std::string &module_id : module_ids) {
        std::string module_dir = std::string(kHookModulesDir) + "/" + module_id;
        struct stat disabled_st {};
        if (lstat((module_dir + "/disabled").c_str(), &disabled_st) == 0) {
            ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                    "MODULE_SCOPE_SKIP id=%s reason=DISABLED", module_id.c_str());
            continue;
        }
        ModuleDescriptor module;
        module.module_id = module_id;
        if (!ReadMetadata(module_dir, module)) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                    "MODULE_ENTRY_FAILED code=MODULE_FILE_INVALID id=%s", module_id.c_str());
            continue;
        }
        if (!ScopeMatches(module.scope_list, state.process_name)) {
            continue;
        }
        module.dex_fd = OpenSafeFile(module_dir + "/module.dex", kMaxDexSize);
        if (module.dex_fd < 0) {
            CloseFd(module.dex_fd);
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "MODULE_ENTRY_FAILED code=MODULE_FILE_INVALID id=%s",
                    module_id.c_str());
            continue;
        }
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "MODULE_SCOPE_MATCH id=%s process=%s", module_id.c_str(), state.process_name.c_str());
        std::string preference_directory;
        if (EnsurePreferenceDirectory(module_id, preference_directory)) {
            if (!LoadModulePreferences(module_id, module.remote_preferences)) {
                module.remote_preferences.clear();
            }
        } else {
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                    "REMOTE_PREFS_DEGRADED reason=DATA_DIRECTORY_INVALID id=%s", module_id.c_str());
        }
        state.modules.push_back(module);
    }
}

void CloseState(ProcessState &state) {
    for (auto &module : state.modules) {
        CloseFd(module.dex_fd);
    }
}

}  // namespace

void CompanionHandler(int socket) {
    int32_t process_id = 0;
    int shared_memory_fd = -1;
    std::string process_name;
    if (!ReceiveProcessQuery(socket, process_id, shared_memory_fd, process_name)) {
        SendNoMatchResponse(socket, 1, "bad request");
        return;
    }
    ProcessState state;
    state.process_name = process_name;
    LoadModules(state);
    if (state.modules.empty()) {
        SendNoMatchResponse(socket, 0, "");
        return;
    }
    state.target = true;
    RemotePreferencesSession session = RegisterRemotePreferencesSession(
            process_id, shared_memory_fd, process_name, state);
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
            "TARGET_MATCH process=%s modules=%zu", process_name.c_str(), state.modules.size());
    if (!session.failure_reason.empty()) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "REMOTE_PREFS_DEGRADED reason=%s process=%s",
                session.failure_reason.c_str(), process_name.c_str());
    }
    bool sent = SendMatchResponse(socket, state, session.region_size);
    if (!sent) {
        UnregisterRemotePreferencesSession(session.id);
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "TARGET_CONFIG_ERROR code=RESPONSE_SEND_FAILED");
    }
    CloseState(state);
}

}  // namespace zygisk_framework

/**
 * Zygisk Root Companion 入口，负责读取 root-only 配置并传递模块 DEX。
 *
 * @param socket 与目标进程相连的 Unix domain socket
 */
static void zygisk_framework_companion_entry(int socket) {
    zygisk_framework::CompanionHandler(socket);
}

REGISTER_ZYGISK_COMPANION(zygisk_framework_companion_entry)
