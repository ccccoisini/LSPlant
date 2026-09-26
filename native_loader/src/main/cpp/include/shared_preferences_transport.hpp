#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zygisk_framework {

constexpr uint32_t kSharedPreferencesMagic = 0x5a505246;
constexpr uint32_t kSharedPreferencesLayoutVersion = 1;
constexpr size_t kMaxSharedPreferencesRegionSize = 64 * 1024 * 1024;
constexpr uint64_t kSharedPreferencesHeartbeatIntervalNs = 15ULL * 1000 * 1000 * 1000;
constexpr uint64_t kSharedPreferencesStaleTimeoutNs = 45ULL * 1000 * 1000 * 1000;

struct alignas(8) SharedPreferencesRegionHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t module_count;
    alignas(4) uint32_t global_generation;
    uint32_t reserved;
    alignas(8) uint64_t heartbeat_ns;
};

struct alignas(8) SharedPreferencesModuleHeader {
    uint32_t payload_offset;
    uint32_t payload_capacity;
    uint32_t payload_size;
    uint32_t checksum;
    alignas(4) uint32_t sequence;
    alignas(4) uint32_t generation;
    uint32_t reserved[2];
};

/**
 * 计算指定模块数需要的共享区域大小。
 *
 * @param module_count 当前进程加载的模块数
 * @return 合法区域字节数；超过全局限制时返回 0
 */
size_t SharedPreferencesRegionSize(size_t module_count);

/**
 * 初始化共享区域及每个模块的基线完整快照。
 *
 * @param region 可写共享映射
 * @param region_size 映射大小
 * @param modules 按模块顺序排列的组快照
 * @param generations 输出各模块的基线 generation
 * @return 布局和快照全部合法时返回 true
 */
bool InitializeSharedPreferencesRegion(
        void *region, size_t region_size,
        const std::vector<std::map<std::string, std::string>> &modules,
        std::vector<uint32_t> &generations);

/**
 * 校验共享区域固定布局及模块槽边界。
 *
 * @param region 只读或可写共享映射
 * @param region_size 映射大小
 * @param expected_module_count 预期模块数
 * @return 布局完整且所有偏移合法时返回 true
 */
bool ValidateSharedPreferencesRegion(const void *region, size_t region_size,
                                     size_t expected_module_count);

/**
 * 原子发布一个模块的最新完整 Preferences 快照。
 *
 * @param region Companion 持有的可写共享映射
 * @param region_size 映射大小
 * @param module_index 模块槽下标
 * @param groups 当前全部组快照
 * @return 成功提交并唤醒 reader 时返回 true
 */
bool PublishSharedPreferencesModule(
        void *region, size_t region_size, size_t module_index,
        const std::map<std::string, std::string> &groups);

/**
 * 稳定读取并校验一个模块的完整 Preferences 快照。
 *
 * @param region Hook 进程持有的只读共享映射
 * @param region_size 映射大小
 * @param module_index 模块槽下标
 * @param groups 输出全部组快照
 * @param generation 输出本次快照 generation
 * @return sequence 和 checksum 均有效时返回 true
 */
bool ReadSharedPreferencesModule(
        const void *region, size_t region_size, size_t module_index,
        std::map<std::string, std::string> &groups, uint32_t &generation);

/**
 * 读取单个模块当前 generation。
 *
 * @param region 共享映射
 * @param region_size 映射大小
 * @param module_index 模块槽下标
 * @return 当前 generation；布局非法时返回 0
 */
uint32_t ReadSharedPreferencesModuleGeneration(const void *region, size_t region_size,
                                               size_t module_index);

/** 返回整个共享区域的通知 generation。 */
uint32_t ReadSharedPreferencesGlobalGeneration(const void *region);

/** 返回 Companion 最近写入的启动时钟 heartbeat。 */
uint64_t ReadSharedPreferencesHeartbeat(const void *region);

/** 更新 heartbeat、递增通知 generation 并唤醒 reader。 */
void UpdateSharedPreferencesHeartbeat(void *region, uint64_t heartbeat_ns);

/** 在跨进程 futex 上等待 generation 发生变化。 */
int WaitForSharedPreferencesGeneration(const void *region, uint32_t expected,
                                       const struct timespec *timeout);

/** 唤醒等待共享 generation 的全部 reader。 */
void WakeSharedPreferencesReaders(void *region);

/** 返回 CLOCK_BOOTTIME 纳秒时间。 */
uint64_t SharedPreferencesBootTimeNs();

}  // namespace zygisk_framework
