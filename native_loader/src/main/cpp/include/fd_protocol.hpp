#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "state.hpp"

namespace zygisk_framework {

constexpr uint32_t kProtocolMagic = 0x5a484b31;
constexpr uint32_t kProtocolVersion = 3;
constexpr uint32_t kOperationQueryProcess = 1;
constexpr uint32_t kOperationPreferenceUpdate = 2;

/**
 * 通过 Root Companion socket 发送当前进程查询请求。
 *
 * @param socket 已连接 companion 的 Unix domain socket
 * @param process_name 当前进程名
 * @return 发送成功时返回 true
 */
bool SendProcessQuery(int socket, const std::string &process_name);

/**
 * 从 Root Companion socket 接收匹配结果、模块 DEX 和模块元数据。
 *
 * @param socket 已连接 companion 的 Unix domain socket
 * @param state 当前进程状态，成功时会被填充
 * @return 协议解析成功时返回 true；未命中目标也返回 true
 */
bool ReceiveCompanionResponse(int socket, ProcessState &state);

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
 * @return 发送成功时返回 true
 */
bool SendMatchResponse(int socket, const ProcessState &state);

/**
 * 从 socket 中读取查询请求里的进程名。
 *
 * @param socket 已连接客户端 socket
 * @param process_name 输出的进程名
 * @return 读取并校验成功时返回 true
 */
bool ReceiveProcessQuery(int socket, std::string &process_name);

/**
 * Companion 向已注入进程发送一个 Remote Preferences 组的完整快照。
 *
 * @param socket 已连接目标进程的 socket
 * @param module_id 快照所属模块 ID
 * @param group Preferences 组名
 * @param snapshot 版本化类型数据；空字符串表示组已删除
 * @return 发送成功时返回 true
 */
bool SendPreferenceUpdate(int socket, const std::string &module_id,
                          const std::string &group, const std::string &snapshot);

/**
 * 在目标进程中阻塞接收一个 Remote Preferences 更新。
 *
 * @param socket 已豁免并保留的 companion socket
 * @param module_id 输出模块 ID
 * @param group 输出组名
 * @param snapshot 输出完整快照
 * @return 成功收到并校验消息时返回 true；连接关闭或协议错误时返回 false
 */
bool ReceivePreferenceUpdate(int socket, std::string &module_id,
                             std::string &group, std::string &snapshot);

}  // namespace zygisk_framework
