#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "state.hpp"

namespace zygisk_framework {

struct RemotePreferencesSession {
    uint64_t id = 0;
    size_t region_size = 0;
    std::string failure_reason;
};

/**
 * 为命中的目标进程创建共享内存 Preferences 会话。
 *
 * @param process_id 目标进程 PID
 * @param shared_memory_fd 目标进程预创建的 memfd 编号
 * @param process_name 目标进程名
 * @param state 已加载模块及初始快照
 * @return 会话 ID、区域大小或降级原因
 */
RemotePreferencesSession RegisterRemotePreferencesSession(
        int32_t process_id, int shared_memory_fd, const std::string &process_name,
        ProcessState &state);

/**
 * 注销尚未成功交付给目标进程的共享内存会话。
 *
 * @param session_id 注册时返回的会话 ID
 */
void UnregisterRemotePreferencesSession(uint64_t session_id);

}  // namespace zygisk_framework
