#include "shared_preferences_transport.hpp"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <limits>
#include <set>

#include "preference_store.hpp"

namespace zygisk_framework {
namespace {

constexpr size_t kMaxGroupNameSize = 128;
constexpr size_t kModulePayloadCapacity =
        kMaxPreferenceModuleSize +
        kMaxPreferenceGroups * (sizeof(uint32_t) * 2 + kMaxGroupNameSize) +
        sizeof(uint32_t);
constexpr size_t kLayoutAlignment = 8;

size_t AlignUp(size_t value, size_t alignment) {
    if (value > std::numeric_limits<size_t>::max() - (alignment - 1)) return 0;
    return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T>
T AtomicLoad(const T *value, int order = __ATOMIC_ACQUIRE) {
    return __atomic_load_n(value, order);
}

template <typename T>
void AtomicStore(T *target, T value, int order = __ATOMIC_RELEASE) {
    __atomic_store_n(target, value, order);
}

uint32_t AtomicIncrement(uint32_t *target) {
    return __atomic_add_fetch(target, 1U, __ATOMIC_RELEASE);
}

void PutU32(std::string &output, uint32_t value) {
    output.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

bool GetU32(const std::string &input, size_t &offset, uint32_t &value) {
    if (offset > input.size() || input.size() - offset < sizeof(value)) return false;
    memcpy(&value, input.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

void PutString(std::string &output, const std::string &value) {
    PutU32(output, static_cast<uint32_t>(value.size()));
    output.append(value);
}

bool GetString(const std::string &input, size_t &offset, size_t max_size,
               std::string &value) {
    uint32_t size = 0;
    if (!GetU32(input, offset, size) || size > max_size ||
        offset > input.size() || input.size() - offset < size) {
        return false;
    }
    value.assign(input.data() + offset, size);
    offset += size;
    return true;
}

bool SerializeModule(const std::map<std::string, std::string> &groups,
                     std::string &payload) {
    if (groups.size() > kMaxPreferenceGroups) return false;
    payload.clear();
    PutU32(payload, static_cast<uint32_t>(groups.size()));
    size_t snapshot_bytes = 0;
    for (const auto &[group, snapshot] : groups) {
        if (group.empty() || group.size() > kMaxGroupNameSize ||
            snapshot.size() > kMaxPreferenceGroupSize ||
            snapshot_bytes > kMaxPreferenceModuleSize - snapshot.size()) {
            return false;
        }
        snapshot_bytes += snapshot.size();
        PutString(payload, group);
        PutString(payload, snapshot);
    }
    return payload.size() <= kModulePayloadCapacity;
}

bool DeserializeModule(const std::string &payload,
                       std::map<std::string, std::string> &groups) {
    groups.clear();
    size_t offset = 0;
    uint32_t count = 0;
    if (!GetU32(payload, offset, count) || count > kMaxPreferenceGroups) return false;
    size_t snapshot_bytes = 0;
    for (uint32_t index = 0; index < count; ++index) {
        std::string group;
        std::string snapshot;
        if (!GetString(payload, offset, kMaxGroupNameSize, group) || group.empty() ||
            !GetString(payload, offset, kMaxPreferenceGroupSize, snapshot) ||
            snapshot_bytes > kMaxPreferenceModuleSize - snapshot.size() ||
            !groups.emplace(group, std::move(snapshot)).second) {
            return false;
        }
        snapshot_bytes += groups.find(group)->second.size();
    }
    return offset == payload.size();
}

uint32_t Crc32(const void *data, size_t size) {
    uint32_t crc = 0xffffffffU;
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

SharedPreferencesModuleHeader *ModuleHeader(void *region, size_t index) {
    auto *bytes = static_cast<uint8_t *>(region);
    return reinterpret_cast<SharedPreferencesModuleHeader *>(
            bytes + sizeof(SharedPreferencesRegionHeader) +
            index * sizeof(SharedPreferencesModuleHeader));
}

const SharedPreferencesModuleHeader *ModuleHeader(const void *region, size_t index) {
    auto *bytes = static_cast<const uint8_t *>(region);
    return reinterpret_cast<const SharedPreferencesModuleHeader *>(
            bytes + sizeof(SharedPreferencesRegionHeader) +
            index * sizeof(SharedPreferencesModuleHeader));
}

bool HeaderBoundsValid(const SharedPreferencesRegionHeader *header, size_t region_size,
                       size_t expected_module_count) {
    if (header == nullptr || header->magic != kSharedPreferencesMagic ||
        header->version != kSharedPreferencesLayoutVersion ||
        header->total_size != region_size || header->module_count != expected_module_count) {
        return false;
    }
    size_t headers_size = sizeof(SharedPreferencesRegionHeader) +
                          expected_module_count * sizeof(SharedPreferencesModuleHeader);
    if (headers_size > region_size) return false;
    for (size_t index = 0; index < expected_module_count; ++index) {
        const auto *module = ModuleHeader(header, index);
        if (module->payload_capacity != kModulePayloadCapacity ||
            module->payload_offset < headers_size ||
            module->payload_offset > region_size ||
            module->payload_capacity > region_size - module->payload_offset) {
            return false;
        }
    }
    return true;
}

void CommitGlobalUpdate(SharedPreferencesRegionHeader *header) {
    AtomicIncrement(&header->global_generation);
    syscall(SYS_futex, &header->global_generation, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

}  // namespace

size_t SharedPreferencesRegionSize(size_t module_count) {
    if (module_count == 0 ||
        module_count > (std::numeric_limits<size_t>::max() -
                        sizeof(SharedPreferencesRegionHeader)) /
                               sizeof(SharedPreferencesModuleHeader)) {
        return 0;
    }
    size_t headers = sizeof(SharedPreferencesRegionHeader) +
                     module_count * sizeof(SharedPreferencesModuleHeader);
    size_t payload_offset = AlignUp(headers, kLayoutAlignment);
    if (payload_offset == 0 ||
        module_count > (std::numeric_limits<size_t>::max() - payload_offset) /
                               kModulePayloadCapacity) {
        return 0;
    }
    size_t total = payload_offset + module_count * kModulePayloadCapacity;
    return total <= kMaxSharedPreferencesRegionSize ? total : 0;
}

bool InitializeSharedPreferencesRegion(
        void *region, size_t region_size,
        const std::vector<std::map<std::string, std::string>> &modules,
        std::vector<uint32_t> &generations) {
    size_t expected_size = SharedPreferencesRegionSize(modules.size());
    if (region == nullptr || expected_size == 0 || region_size != expected_size) return false;
    size_t headers_size = sizeof(SharedPreferencesRegionHeader) +
                          modules.size() * sizeof(SharedPreferencesModuleHeader);
    memset(region, 0, headers_size);
    auto *header = static_cast<SharedPreferencesRegionHeader *>(region);
    header->magic = kSharedPreferencesMagic;
    header->version = kSharedPreferencesLayoutVersion;
    header->total_size = static_cast<uint32_t>(region_size);
    header->module_count = static_cast<uint32_t>(modules.size());
    header->global_generation = 1;
    header->heartbeat_ns = SharedPreferencesBootTimeNs();

    size_t payload_offset = AlignUp(headers_size, kLayoutAlignment);
    generations.assign(modules.size(), 0);
    for (size_t index = 0; index < modules.size(); ++index) {
        auto *module = ModuleHeader(region, index);
        module->payload_offset = static_cast<uint32_t>(
                payload_offset + index * kModulePayloadCapacity);
        module->payload_capacity = static_cast<uint32_t>(kModulePayloadCapacity);
        std::string payload;
        if (!SerializeModule(modules[index], payload)) return false;
        memcpy(static_cast<uint8_t *>(region) + module->payload_offset,
               payload.data(), payload.size());
        module->payload_size = static_cast<uint32_t>(payload.size());
        module->checksum = Crc32(payload.data(), payload.size());
        module->sequence = 2;
        module->generation = 1;
        generations[index] = 1;
    }
    return true;
}

bool ValidateSharedPreferencesRegion(const void *region, size_t region_size,
                                     size_t expected_module_count) {
    if (region == nullptr || region_size == 0 || region_size > kMaxSharedPreferencesRegionSize) {
        return false;
    }
    return HeaderBoundsValid(static_cast<const SharedPreferencesRegionHeader *>(region),
                             region_size, expected_module_count);
}

bool PublishSharedPreferencesModule(
        void *region, size_t region_size, size_t module_index,
        const std::map<std::string, std::string> &groups) {
    auto *header = static_cast<SharedPreferencesRegionHeader *>(region);
    if (!ValidateSharedPreferencesRegion(region, region_size, header == nullptr ? 0 :
                                         header->module_count) ||
        module_index >= header->module_count) {
        return false;
    }
    std::string payload;
    if (!SerializeModule(groups, payload)) return false;
    auto *module = ModuleHeader(region, module_index);
    uint32_t sequence = AtomicLoad(&module->sequence, __ATOMIC_RELAXED);
    if ((sequence & 1U) != 0) ++sequence;
    AtomicStore(&module->sequence, sequence + 1U);
    memcpy(static_cast<uint8_t *>(region) + module->payload_offset,
           payload.data(), payload.size());
    module->payload_size = static_cast<uint32_t>(payload.size());
    module->checksum = Crc32(payload.data(), payload.size());
    AtomicStore(&module->sequence, sequence + 2U);
    AtomicIncrement(&module->generation);
    CommitGlobalUpdate(header);
    return true;
}

bool ReadSharedPreferencesModule(
        const void *region, size_t region_size, size_t module_index,
        std::map<std::string, std::string> &groups, uint32_t &generation) {
    const auto *header = static_cast<const SharedPreferencesRegionHeader *>(region);
    if (!ValidateSharedPreferencesRegion(region, region_size, header == nullptr ? 0 :
                                         header->module_count) ||
        module_index >= header->module_count) {
        return false;
    }
    const auto *module = ModuleHeader(region, module_index);
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t sequence_before = AtomicLoad(&module->sequence);
        if ((sequence_before & 1U) != 0) continue;
        uint32_t size = module->payload_size;
        uint32_t checksum = module->checksum;
        uint32_t observed_generation = AtomicLoad(&module->generation);
        if (size > module->payload_capacity) return false;
        std::string payload(size, '\0');
        if (size > 0) {
            memcpy(payload.data(), static_cast<const uint8_t *>(region) +
                                   module->payload_offset, size);
        }
        uint32_t sequence_after = AtomicLoad(&module->sequence);
        if (sequence_before != sequence_after || (sequence_after & 1U) != 0) continue;
        if (Crc32(payload.data(), payload.size()) != checksum ||
            !DeserializeModule(payload, groups)) {
            return false;
        }
        generation = observed_generation;
        return true;
    }
    return false;
}

uint32_t ReadSharedPreferencesModuleGeneration(const void *region, size_t region_size,
                                               size_t module_index) {
    const auto *header = static_cast<const SharedPreferencesRegionHeader *>(region);
    if (!ValidateSharedPreferencesRegion(region, region_size, header == nullptr ? 0 :
                                         header->module_count) ||
        module_index >= header->module_count) {
        return 0;
    }
    return AtomicLoad(&ModuleHeader(region, module_index)->generation);
}

uint32_t ReadSharedPreferencesGlobalGeneration(const void *region) {
    if (region == nullptr) return 0;
    const auto *header = static_cast<const SharedPreferencesRegionHeader *>(region);
    return AtomicLoad(&header->global_generation);
}

uint64_t ReadSharedPreferencesHeartbeat(const void *region) {
    if (region == nullptr) return 0;
    const auto *header = static_cast<const SharedPreferencesRegionHeader *>(region);
    return AtomicLoad(&header->heartbeat_ns);
}

void UpdateSharedPreferencesHeartbeat(void *region, uint64_t heartbeat_ns) {
    if (region == nullptr) return;
    auto *header = static_cast<SharedPreferencesRegionHeader *>(region);
    AtomicStore(&header->heartbeat_ns, heartbeat_ns);
    CommitGlobalUpdate(header);
}

int WaitForSharedPreferencesGeneration(const void *region, uint32_t expected,
                                       const struct timespec *timeout) {
    if (region == nullptr) {
        errno = EINVAL;
        return -1;
    }
    auto *header = const_cast<SharedPreferencesRegionHeader *>(
            static_cast<const SharedPreferencesRegionHeader *>(region));
    return static_cast<int>(syscall(SYS_futex, &header->global_generation, FUTEX_WAIT,
                                    expected, timeout, nullptr, 0));
}

void WakeSharedPreferencesReaders(void *region) {
    if (region == nullptr) return;
    auto *header = static_cast<SharedPreferencesRegionHeader *>(region);
    syscall(SYS_futex, &header->global_generation, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

uint64_t SharedPreferencesBootTimeNs() {
    timespec now{};
    if (clock_gettime(CLOCK_BOOTTIME, &now) != 0) return 0;
    return static_cast<uint64_t>(now.tv_sec) * 1000 * 1000 * 1000 +
           static_cast<uint64_t>(now.tv_nsec);
}

}  // namespace zygisk_framework
