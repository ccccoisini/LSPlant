#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace zygisk_framework {

/**
 * 保存 Root Companion 返回的单个业务模块描述。
 */
struct ModuleDescriptor {
    std::string module_id;
    int dex_fd = -1;
    std::vector<uint8_t> dex_bytes;
    std::string java_init_list;
    std::string module_prop;
    std::string scope_list;
    std::map<std::string, std::string> remote_preferences;
    uint32_t remote_preferences_generation = 0;
};

/**
 * 保存当前 fork 后进程独占的注入状态。
 */
struct ProcessState {
    std::string process_name;
    std::string package_name;
    bool target = false;
    void *remote_preferences_region = nullptr;
    size_t remote_preferences_region_size = 0;
    std::vector<ModuleDescriptor> modules;
};

/**
 * 关闭文件描述符并把变量重置为 -1。
 *
 * @param fd 需要关闭并重置的文件描述符引用
 */
void CloseFd(int &fd);

}  // namespace zygisk_framework
