#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "state.hpp"

namespace zygisk_framework {

constexpr uint32_t kProtocolMagic = 0x5a484b31;
constexpr uint32_t kProtocolVersion = 4;
constexpr uint32_t kOperationQueryProcess = 1;

/**
 * 通过 Root Companion socket 发送当前进程查询请求。
 *
 * @param socket 已连接 companion 的 Unix domain socket
 * @param process_id 当前目标进程 PID
 * @param shared_memory_fd 目标进程预创建的 memfd 编号；不可用时为 -1
 * @param process_name 当前进程名
 * @return 发送成功时返回 true
 */
bool SendProcessQuery(int socket, int32_t process_id, int shared_memory_fd,
                      const std::string &process_name);

/**
 * 从 Root Companion socket 接收匹配结果、模块 DEX 和模块元数据。
 *
 * @param socket 已连接 companion 的 Unix domain socket
 * @param shared_memory_fd 目标进程预创建的 memfd
 * @param state 当前进程状态，成功时会被填充
 * @return 协议解析成功时返回 true；未命中目标也返回 true
 */
bool ReceiveCompanionResponse(int socket, int shared_memory_fd, ProcessState &state);

/**
 * Companion 发送未命中或错误响应。
 *
 * @param socket 已连接客户端 socket
 * @param status 状态码，0 表示协议成功
 * @param message 错误说明或空字符串
 * @return 发送成功时返回 true
 */
bool SendNoMatchResponse(int socket, uint32_t status, const std::string &message);

/**
 * Companion 发送命中响应和模块描述。
 *
 * @param socket 已连接客户端 socket
 * @param state 已填充的目标进程状态
 * @param shared_preferences_size 已初始化的共享区域大小；降级时为 0
 * @return 发送成功时返回 true
 */
bool SendMatchResponse(int socket, const ProcessState &state,
                       size_t shared_preferences_size);

/**
 * 从 socket 中读取查询请求里的进程名。
 *
 * @param socket 已连接客户端 socket
 * @param process_id 输出目标进程 PID
 * @param shared_memory_fd 输出目标进程预创建的 memfd 编号
 * @param process_name 输出的进程名
 * @return 读取并校验成功时返回 true
 */
bool ReceiveProcessQuery(int socket, int32_t &process_id, int &shared_memory_fd,
                         std::string &process_name);

}  // namespace zygisk_framework
