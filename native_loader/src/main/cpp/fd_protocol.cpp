#include "fd_protocol.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "logging.hpp"

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

bool SendMessageWithFds(int socket, const WireHeader &header, const std::string &payload,
                        const std::vector<int> &fds) {
    if (!SendAll(socket, &header, sizeof(header)) ||
        !SendAll(socket, payload.data(), payload.size())) {
        return false;
    }
    if (fds.empty()) {
        return true;
    }

    char byte = 0;
    iovec iov{&byte, sizeof(byte)};
    std::vector<char> control(CMSG_SPACE(sizeof(int) * fds.size()));
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();

    cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
    memcpy(CMSG_DATA(cmsg), fds.data(), sizeof(int) * fds.size());
    msg.msg_controllen = cmsg->cmsg_len;
    return TEMP_FAILURE_RETRY(sendmsg(socket, &msg, 0)) == 1;
}

bool RecvFds(int socket, uint32_t fd_count, std::vector<int> &fds) {
    if (fd_count == 0) {
        return true;
    }
    if (fd_count > 64) {
        return false;
    }
    char byte = 0;
    iovec iov{&byte, sizeof(byte)};
    std::vector<char> control(CMSG_SPACE(sizeof(int) * fd_count));
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();
    ssize_t read_size = TEMP_FAILURE_RETRY(recvmsg(socket, &msg, 0));
    if (read_size != 1) {
        return false;
    }
    for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        auto *data = reinterpret_cast<int *>(CMSG_DATA(cmsg));
        fds.assign(data, data + count);
        return fds.size() == fd_count;
    }
    return false;
}

}  // namespace

bool SendProcessQuery(int socket, const std::string &process_name) {
    if (process_name.size() > 512) {
        return false;
    }
    WireHeader header{kProtocolMagic, kProtocolVersion, kOperationQueryProcess, 0,
                      static_cast<uint32_t>(process_name.size()), 0};
    return SendAll(socket, &header, sizeof(header)) &&
           SendAll(socket, process_name.data(), process_name.size());
}

bool ReceiveProcessQuery(int socket, std::string &process_name) {
    WireHeader header{};
    if (!RecvAll(socket, &header, sizeof(header))) {
        return false;
    }
    if (header.magic != kProtocolMagic || header.version != kProtocolVersion ||
        header.operation != kOperationQueryProcess || header.payload_size > 512 ||
        header.fd_count != 0) {
        return false;
    }
    process_name.assign(header.payload_size, '\0');
    return RecvAll(socket, process_name.data(), process_name.size());
}

bool SendNoMatchResponse(int socket, uint32_t status, const std::string &message) {
    std::string payload;
    PutU32(payload, 0);
    PutString(payload, message);
    WireHeader header{kProtocolMagic, kProtocolVersion, kOperationQueryProcess, status,
                      static_cast<uint32_t>(payload.size()), 0};
    return SendMessageWithFds(socket, header, payload, {});
}

bool SendMatchResponse(int socket, const ProcessState &state) {
    std::string payload;
    PutU32(payload, 1);
    PutString(payload, "");
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
        PutU32(payload, static_cast<uint32_t>(module.remote_preferences.size()));
        for (const auto &[group, snapshot] : module.remote_preferences) {
            PutString(payload, group);
            PutString(payload, snapshot);
        }
    }
    WireHeader header{kProtocolMagic, kProtocolVersion, kOperationQueryProcess, 0,
                      static_cast<uint32_t>(payload.size()), 0};
    return SendMessageWithFds(socket, header, payload, {});
}

bool ReceiveCompanionResponse(int socket, ProcessState &state) {
    WireHeader header{};
    if (!RecvAll(socket, &header, sizeof(header))) {
        return false;
    }
    if (header.magic != kProtocolMagic || header.version != kProtocolVersion ||
        header.operation != kOperationQueryProcess || header.payload_size > 80 * 1024 * 1024) {
        return false;
    }
    std::string payload(header.payload_size, '\0');
    if (!RecvAll(socket, payload.data(), payload.size())) {
        return false;
    }
    std::vector<int> fds;
    bool fd_receive_ok = RecvFds(socket, header.fd_count, fds);
    if (!fd_receive_ok) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_FD_RECEIVED code=SCM_RIGHTS_RECEIVE_FAILED");
        fds.clear();
    }

    size_t offset = 0;
    uint32_t matched = 0;
    std::string message;
    if (!GetU32(payload, offset, matched) || !GetString(payload, offset, message, 64 * 1024)) {
        return false;
    }
    if (header.status != 0 || matched == 0) {
        state.target = false;
        for (int fd : fds) close(fd);
        return true;
    }
    uint32_t module_count = 0;
    if (!GetU32(payload, offset, module_count)) {
        for (int fd : fds) close(fd);
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
            !GetString(payload, offset, module.scope_list, 64 * 1024)) {
            for (size_t j = i; j < fds.size(); ++j) close(fds[j]);
            return false;
        }
        module.dex_bytes.assign(module_dex_blob.begin(), module_dex_blob.end());
        module.dex_fd = fds.size() == module_count ? fds[i] : -1;
        uint32_t group_count = 0;
        if (!GetU32(payload, offset, group_count) || group_count > 64) {
            for (size_t j = i; j < fds.size(); ++j) close(fds[j]);
            return false;
        }
        size_t preference_bytes = 0;
        for (uint32_t group_index = 0; group_index < group_count; ++group_index) {
            std::string group;
            std::string snapshot;
            if (!GetString(payload, offset, group, 128) ||
                !GetString(payload, offset, snapshot, 1024 * 1024)) {
                for (size_t j = i; j < fds.size(); ++j) close(fds[j]);
                return false;
            }
            preference_bytes += snapshot.size();
            if (group.empty() || preference_bytes > 4 * 1024 * 1024 ||
                !module.remote_preferences.emplace(group, std::move(snapshot)).second) {
                for (size_t j = i; j < fds.size(); ++j) close(fds[j]);
                return false;
            }
        }
        state.modules.push_back(module);
    }
    return offset == payload.size();
}

bool SendPreferenceUpdate(int socket, const std::string &module_id,
                          const std::string &group, const std::string &snapshot) {
    if (module_id.empty() || module_id.size() > 512 || group.empty() || group.size() > 128 ||
        snapshot.size() > 1024 * 1024) {
        return false;
    }
    std::string payload;
    PutString(payload, module_id);
    PutString(payload, group);
    PutString(payload, snapshot);
    WireHeader header{kProtocolMagic, kProtocolVersion, kOperationPreferenceUpdate, 0,
                      static_cast<uint32_t>(payload.size()), 0};
    return SendMessageWithFds(socket, header, payload, {});
}

bool ReceivePreferenceUpdate(int socket, std::string &module_id,
                             std::string &group, std::string &snapshot) {
    WireHeader header{};
    if (!RecvAll(socket, &header, sizeof(header)) ||
        header.magic != kProtocolMagic || header.version != kProtocolVersion ||
        header.operation != kOperationPreferenceUpdate || header.status != 0 ||
        header.fd_count != 0 || header.payload_size > 1024 * 1024 + 1024) {
        return false;
    }
    std::string payload(header.payload_size, '\0');
    if (!RecvAll(socket, payload.data(), payload.size())) {
        return false;
    }
    size_t offset = 0;
    return GetString(payload, offset, module_id, 512) &&
           GetString(payload, offset, group, 128) &&
           GetString(payload, offset, snapshot, 1024 * 1024) &&
           !module_id.empty() && !group.empty() && offset == payload.size();
}

void CloseFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

}  // namespace zygisk_framework
