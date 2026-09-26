#include "fd_protocol.hpp"

#include <sys/socket.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "logging.hpp"
#include "shared_preferences_transport.hpp"

namespace zygisk_framework {
namespace {

struct WireHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t operation;
    uint32_t status;
    uint32_t payload_size;
    uint32_t fd_count;
};

bool SendAll(int fd, const void *data, size_t size) {
    auto *cursor = static_cast<const uint8_t *>(data);
    size_t left = size;
    while (left > 0) {
        ssize_t written = TEMP_FAILURE_RETRY(send(fd, cursor, left, MSG_NOSIGNAL));
        if (written <= 0) {
            return false;
        }
        cursor += written;
        left -= static_cast<size_t>(written);
    }
    return true;
}

bool RecvAll(int fd, void *data, size_t size) {
    auto *cursor = static_cast<uint8_t *>(data);
    size_t left = size;
    while (left > 0) {
        ssize_t read_size = TEMP_FAILURE_RETRY(read(fd, cursor, left));
        if (read_size <= 0) {
            return false;
        }
        cursor += read_size;
        left -= static_cast<size_t>(read_size);
    }
    return true;
}

void PutU32(std::string &payload, uint32_t value) {
    payload.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

bool GetU32(const std::string &payload, size_t &offset, uint32_t &value) {
    if (offset + sizeof(uint32_t) > payload.size()) {
        return false;
    }
    memcpy(&value, payload.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    return true;
}

void PutString(std::string &payload, const std::string &value) {
    PutU32(payload, static_cast<uint32_t>(value.size()));
    payload.append(value);
}

bool ReadFdBlob(int fd, size_t max_size, std::string &blob) {
    if (fd < 0) {
        return false;
    }
    off_t size = lseek(fd, 0, SEEK_END);
    if (size <= 0 || static_cast<size_t>(size) > max_size || lseek(fd, 0, SEEK_SET) < 0) {
        return false;
    }
    blob.assign(static_cast<size_t>(size), '\0');
    size_t offset = 0;
    while (offset < blob.size()) {
        ssize_t read_size = TEMP_FAILURE_RETRY(read(fd, blob.data() + offset, blob.size() - offset));
        if (read_size <= 0) {
            return false;
        }
        offset += static_cast<size_t>(read_size);
    }
    return true;
}

bool GetString(const std::string &payload, size_t &offset, std::string &value,
               size_t max_size = 1024 * 1024) {
    uint32_t size = 0;
    if (!GetU32(payload, offset, size) || size > max_size ||
        offset + size > payload.size()) {
        return false;
    }
    value.assign(payload.data() + offset, size);
    offset += size;
    return true;
}

bool SendMessage(int socket, const WireHeader &header, const std::string &payload) {
    return SendAll(socket, &header, sizeof(header)) &&
           SendAll(socket, payload.data(), payload.size());
}

}  // namespace

bool SendProcessQuery(int socket, int32_t process_id, int shared_memory_fd,
                      const std::string &process_name) {
    if (process_id <= 0 || process_name.empty() || process_name.size() > 512) {
        return false;
    }
    std::string payload;
    PutU32(payload, static_cast<uint32_t>(process_id));
    PutU32(payload, shared_memory_fd < 0 ? UINT32_MAX :
                                          static_cast<uint32_t>(shared_memory_fd));
    PutString(payload, process_name);
    WireHeader header{kProtocolMagic, kProtocolVersion, kOperationQueryProcess, 0,
                      static_cast<uint32_t>(payload.size()), 0};
    return SendAll(socket, &header, sizeof(header)) &&
           SendAll(socket, payload.data(), payload.size());
}

bool ReceiveProcessQuery(int socket, int32_t &process_id, int &shared_memory_fd,
                         std::string &process_name) {
    WireHeader header{};
    if (!RecvAll(socket, &header, sizeof(header))) {
        return false;
    }
    if (header.magic != kProtocolMagic || header.version != kProtocolVersion ||
        header.operation != kOperationQueryProcess || header.payload_size > 1024 ||
        header.fd_count != 0) {
        return false;
    }
    std::string payload(header.payload_size, '\0');
    if (!RecvAll(socket, payload.data(), payload.size())) return false;
    size_t offset = 0;
    uint32_t pid = 0;
    uint32_t memory_fd = UINT32_MAX;
    if (!GetU32(payload, offset, pid) || pid == 0 || pid > INT32_MAX ||
        !GetU32(payload, offset, memory_fd) ||
        (memory_fd != UINT32_MAX && memory_fd > INT32_MAX) ||
        !GetString(payload, offset, process_name, 512) || process_name.empty() ||
        offset != payload.size()) {
        return false;
    }
    process_id = static_cast<int32_t>(pid);
    shared_memory_fd = memory_fd == UINT32_MAX ? -1 : static_cast<int>(memory_fd);
    return true;
}

bool SendNoMatchResponse(int socket, uint32_t status, const std::string &message) {
    std::string payload;
    PutU32(payload, 0);
    PutString(payload, message);
    WireHeader header{kProtocolMagic, kProtocolVersion, kOperationQueryProcess, status,
                      static_cast<uint32_t>(payload.size()), 0};
    return SendMessage(socket, header, payload);
}

bool SendMatchResponse(int socket, const ProcessState &state,
                       size_t shared_preferences_size) {
    bool has_shared_preferences = shared_preferences_size > 0 &&
                                  shared_preferences_size <= UINT32_MAX;
    std::string payload;
    PutU32(payload, 1);
    PutString(payload, "");
    PutU32(payload, has_shared_preferences ? 1U : 0U);
    PutU32(payload, has_shared_preferences
                            ? static_cast<uint32_t>(shared_preferences_size)
                            : 0U);
    PutU32(payload, static_cast<uint32_t>(state.modules.size()));
    for (const auto &module : state.modules) {
        std::string module_dex;
        if (!ReadFdBlob(module.dex_fd, 32 * 1024 * 1024, module_dex)) {
            return false;
        }
        PutString(payload, module.module_id);
        PutString(payload, module_dex);
        PutString(payload, module.java_init_list);
        PutString(payload, module.module_prop);
        PutString(payload, module.scope_list);
        PutU32(payload, module.remote_preferences_generation);
        PutU32(payload, static_cast<uint32_t>(module.remote_preferences.size()));
        for (const auto &[group, snapshot] : module.remote_preferences) {
            PutString(payload, group);
            PutString(payload, snapshot);
        }
    }
    WireHeader header{kProtocolMagic, kProtocolVersion, kOperationQueryProcess, 0,
                      static_cast<uint32_t>(payload.size()), 0};
    return SendMessage(socket, header, payload);
}

bool ReceiveCompanionResponse(int socket, int shared_memory_fd, ProcessState &state) {
    WireHeader header{};
    if (!RecvAll(socket, &header, sizeof(header))) {
        return false;
    }
    if (header.magic != kProtocolMagic || header.version != kProtocolVersion ||
        header.operation != kOperationQueryProcess || header.payload_size > 80 * 1024 * 1024 ||
        header.fd_count != 0) {
        return false;
    }
    std::string payload(header.payload_size, '\0');
    if (!RecvAll(socket, payload.data(), payload.size())) return false;

    size_t offset = 0;
    uint32_t matched = 0;
    std::string message;
    if (!GetU32(payload, offset, matched) || !GetString(payload, offset, message, 64 * 1024)) {
        return false;
    }
    if (header.status != 0 || matched == 0) {
        state.target = false;
        return true;
    }
    uint32_t has_shared_preferences = 0;
    uint32_t shared_preferences_size = 0;
    uint32_t module_count = 0;
    if (!GetU32(payload, offset, has_shared_preferences) || has_shared_preferences > 1 ||
        !GetU32(payload, offset, shared_preferences_size) ||
        !GetU32(payload, offset, module_count) || module_count == 0 ||
        (has_shared_preferences == 1 &&
         (shared_preferences_size == 0 ||
          shared_preferences_size > kMaxSharedPreferencesRegionSize)) ||
        (has_shared_preferences == 0 &&
         shared_preferences_size != 0)) {
        return false;
    }
    state.target = true;
    state.modules.clear();
    for (uint32_t i = 0; i < module_count; ++i) {
        ModuleDescriptor module;
        std::string module_dex_blob;
        if (!GetString(payload, offset, module.module_id, 512) ||
            !GetString(payload, offset, module_dex_blob, 32 * 1024 * 1024) ||
            !GetString(payload, offset, module.java_init_list, 64 * 1024) ||
            !GetString(payload, offset, module.module_prop, 64 * 1024) ||
            !GetString(payload, offset, module.scope_list, 64 * 1024) ||
            !GetU32(payload, offset, module.remote_preferences_generation)) {
            return false;
        }
        module.dex_bytes.assign(module_dex_blob.begin(), module_dex_blob.end());
        uint32_t group_count = 0;
        if (!GetU32(payload, offset, group_count) || group_count > 64) {
            return false;
        }
        size_t preference_bytes = 0;
        for (uint32_t group_index = 0; group_index < group_count; ++group_index) {
            std::string group;
            std::string snapshot;
            if (!GetString(payload, offset, group, 128) ||
                !GetString(payload, offset, snapshot, 1024 * 1024)) {
                return false;
            }
            preference_bytes += snapshot.size();
            if (group.empty() || preference_bytes > 4 * 1024 * 1024 ||
                !module.remote_preferences.emplace(group, std::move(snapshot)).second) {
                return false;
            }
        }
        state.modules.push_back(module);
    }
    if (offset != payload.size()) {
        return false;
    }
    if (has_shared_preferences == 1 && shared_memory_fd >= 0) {
        void *region = mmap(nullptr, shared_preferences_size, PROT_READ, MAP_SHARED,
                            shared_memory_fd, 0);
        if (region == MAP_FAILED) {
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                    "REMOTE_PREFS_DEGRADED reason=MAP_FAILED");
        } else if (!ValidateSharedPreferencesRegion(
                           region, shared_preferences_size, state.modules.size())) {
            munmap(region, shared_preferences_size);
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                    "REMOTE_PREFS_DEGRADED reason=MAP_FAILED");
        } else {
            madvise(region, shared_preferences_size, MADV_DONTDUMP);
            state.remote_preferences_region = region;
            state.remote_preferences_region_size = shared_preferences_size;
        }
    } else if (has_shared_preferences == 1) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE,
                "REMOTE_PREFS_DEGRADED reason=MAP_FAILED");
    }
    return true;
}

void CloseFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

}  // namespace zygisk_framework
