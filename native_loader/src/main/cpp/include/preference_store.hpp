#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace zygisk_framework {

constexpr size_t kMaxPreferenceGroupSize = 1024 * 1024;
constexpr size_t kMaxPreferenceModuleSize = 4 * 1024 * 1024;
constexpr size_t kMaxPreferenceGroups = 64;

/**
 * 确保模块的 root-only Preferences 目录存在。
 *
 * @param module_id 已校验的模块 ID
 * @param path 输出目录路径
 * @return 目录安全且可用时返回 true
 */
bool EnsurePreferenceDirectory(const std::string &module_id, std::string &path);

/**
 * 读取并校验模块所有 Remote Preferences 快照。
 *
 * @param module_id 已校验的模块 ID
 * @param groups 输出组名到版本化快照的映射
 * @return 所有已发现文件都有效时返回 true；无目录视为空且返回 true
 */
bool LoadModulePreferences(const std::string &module_id,
                           std::map<std::string, std::string> &groups);

/**
 * 校验单个组的版本化 Remote Preferences 快照。
 *
 * @param expected_group 预期组名
 * @param snapshot 文件原始内容
 * @return 格式、类型和大小全部有效时返回 true
 */
bool ValidatePreferenceSnapshot(const std::string &expected_group,
                                const std::string &snapshot);

}  // namespace zygisk_framework
