#pragma once

#include <string>

#include "state.hpp"
#include "zygisk.hpp"

namespace zygisk_framework {

/**
 * 从 Zygisk Root Companion 查询当前进程是否需要注入。
 *
 * @param api 当前 Zygisk API 指针
 * @param process_name 当前进程名
 * @param state 当前进程状态，命中时会写入共享映射和模块信息
 * @return 协议成功且命中目标时返回 true
 */
bool QueryCompanion(zygisk::Api *api, const std::string &process_name,
                    ProcessState &state);

}  // namespace zygisk_framework
