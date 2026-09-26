#include "fd_protocol.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/inotify.h>
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
            LoadModulePreferences(module_id, module.remote_preferences);
        } else {
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                    "REMOTE_PREFS_DEGRADED reason=DATA_DIRECTORY_INVALID id=%s", module_id.c_str());
        }
        state.modules.push_back(module);
    }
}

int CreatePreferenceWatcher(ProcessState &state) {
    int watcher = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (watcher < 0) return -1;
    constexpr uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_CREATE |
                              IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF;
    for (const auto &module : state.modules) {
        std::string directory;
        if (!EnsurePreferenceDirectory(module.module_id, directory) ||
            inotify_add_watch(watcher, directory.c_str(), mask) < 0) {
            close(watcher);
            return -1;
        }
    }
    return watcher;
}

bool ReloadPreferences(int socket, ProcessState &state, bool send_updates) {
    for (auto &module : state.modules) {
        std::map<std::string, std::string> current;
        if (!LoadModulePreferences(module.module_id, current)) {
            continue;
        }
        if (send_updates) {
            for (const auto &[group, snapshot] : current) {
                auto old = module.remote_preferences.find(group);
                if (old == module.remote_preferences.end() || old->second != snapshot) {
                    if (!SendPreferenceUpdate(socket, module.module_id, group, snapshot)) return false;
                }
            }
            for (const auto &[group, snapshot] : module.remote_preferences) {
                (void) snapshot;
                if (!current.contains(group) &&
                    !SendPreferenceUpdate(socket, module.module_id, group, "")) return false;
            }
        }
        module.remote_preferences = std::move(current);
    }
    return true;
}

void WatchPreferences(int socket, int watcher, ProcessState &state) {
    std::vector<char> events(16 * 1024);
    while (true) {
        pollfd fds[2] = {
                {socket, POLLIN | POLLHUP | POLLERR, 0},
                {watcher, POLLIN | POLLERR, 0},
        };
        int result = TEMP_FAILURE_RETRY(poll(fds, 2, -1));
        if (result <= 0 || (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 ||
            (fds[1].revents & (POLLERR | POLLNVAL)) != 0) {
            return;
        }
        if ((fds[0].revents & POLLIN) != 0) return;
        if ((fds[1].revents & POLLIN) == 0) continue;
        while (TEMP_FAILURE_RETRY(read(watcher, events.data(), events.size())) > 0) {
        }
        if (!ReloadPreferences(socket, state, true)) return;
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "REMOTE_PREFS_UPDATE process=%s", state.process_name.c_str());
    }
}

void CloseState(ProcessState &state) {
    for (auto &module : state.modules) {
        CloseFd(module.dex_fd);
    }
}

}  // namespace

void CompanionHandler(int socket) {
    std::string process_name;
    if (!ReceiveProcessQuery(socket, process_name)) {
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
    int watcher = CreatePreferenceWatcher(state);
    if (watcher >= 0) {
        ReloadPreferences(socket, state, false);
    }
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
            "TARGET_MATCH process=%s modules=%zu", process_name.c_str(), state.modules.size());
    bool sent = SendMatchResponse(socket, state);
    if (!sent) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "TARGET_CONFIG_ERROR code=RESPONSE_SEND_FAILED");
    } else if (watcher < 0) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "REMOTE_PREFS_DEGRADED reason=INOTIFY_SETUP_FAILED process=%s", process_name.c_str());
    } else {
        WatchPreferences(socket, watcher, state);
    }
    if (watcher >= 0) close(watcher);
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
