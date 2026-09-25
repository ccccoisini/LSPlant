#pragma once

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
};

/**
 * 保存当前 fork 后进程独占的注入状态。
 */
struct ProcessState {
    std::string process_name;
    std::string package_name;
    bool target = false;
    std::vector<ModuleDescriptor> modules;
};

/**
 * 关闭文件描述符并把变量重置为 -1。
 *
 * @param fd 需要关闭并重置的文件描述符引用
 */
void CloseFd(int &fd);

}  // namespace zygisk_framework
