#include "fd_protocol.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "logging.hpp"
#include "zygisk.hpp"

namespace zygisk_framework {
namespace {

constexpr const char *kTargetPath = "/data/adb/zygisk_framework/target.txt";
constexpr const char *kHookModulesDir = "/data/adb/zygisk_framework/modules";
constexpr size_t kMaxTargetFileSize = 64 * 1024;
constexpr size_t kMaxLineSize = 512;
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

std::set<std::string> LoadTargetRules() {
    std::string content;
    std::set<std::string> rules;
    if (!ReadSmallFile(kTargetPath, kMaxTargetFileSize, content)) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "TARGET_CONFIG_ERROR code=READ_FAILED path=%s", kTargetPath);
        return rules;
    }
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() > kMaxLineSize) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "TARGET_CONFIG_ERROR code=LINE_TOO_LONG");
            continue;
        }
        std::string rule = Trim(line);
        if (rule.empty() || rule[0] == '#') {
            continue;
        }
        rules.insert(rule);
    }
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "TARGET_CONFIG_LOADED count=%zu", rules.size());
    return rules;
}

bool MatchesRule(const std::string &process_name, const std::string &rule) {
    if (process_name == rule) {
        return true;
    }
    return rule.find(':') == std::string::npos &&
           process_name.rfind(rule + ":", 0) == 0;
}

bool IsTargetProcess(const std::string &process_name) {
    auto rules = LoadTargetRules();
    for (const auto &rule : rules) {
        if (MatchesRule(process_name, rule)) {
            ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "TARGET_MATCH process=%s", process_name.c_str());
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
    while (dirent *entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        std::string module_id = entry->d_name;
        std::string module_dir = std::string(kHookModulesDir) + "/" + module_id;
        ModuleDescriptor module;
        module.module_id = module_id;
        module.dex_fd = OpenSafeFile(module_dir + "/module.dex", kMaxDexSize);
        if (module.dex_fd < 0 || !ReadMetadata(module_dir, module)) {
            CloseFd(module.dex_fd);
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "MODULE_ENTRY_FAILED code=MODULE_FILE_INVALID id=%s",
                    module_id.c_str());
            continue;
        }
        state.modules.push_back(module);
    }
    closedir(dir);
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
    if (!IsTargetProcess(process_name)) {
        SendNoMatchResponse(socket, 0, "");
        return;
    }

    ProcessState state;
    state.process_name = process_name;
    state.target = true;
    LoadModules(state);
    bool sent = SendMatchResponse(socket, state);
    CloseState(state);
    if (!sent) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION, "TARGET_CONFIG_ERROR code=RESPONSE_SEND_FAILED");
    }
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
