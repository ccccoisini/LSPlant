#include "remote_preferences_manager.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "logging.hpp"
#include "preference_store.hpp"
#include "shared_preferences_transport.hpp"

namespace zygisk_framework {
namespace {

constexpr const char *kDataDir = "/data/adb/zygisk_framework/data";
constexpr uint32_t kPreferenceWatchMask =
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_CREATE | IN_ATTRIB |
        IN_DELETE_SELF | IN_MOVE_SELF;

struct ManagedSession {
    uint64_t id = 0;
    int32_t process_id = 0;
    uint64_t process_start_time = 0;
    int process_fd = -1;
    int region_fd = -1;
    void *region = nullptr;
    size_t region_size = 0;
    std::vector<std::string> module_ids;
};

struct ManagedModule {
    std::map<std::string, std::string> groups;
    std::set<uint64_t> sessions;
    int watch_descriptor = -1;
};

uint64_t ReadProcessStartTime(int32_t process_id) {
    std::ifstream input("/proc/" + std::to_string(process_id) + "/stat");
    std::string line;
    if (!input || !std::getline(input, line)) return 0;
    size_t comm_end = line.rfind(')');
    if (comm_end == std::string::npos || comm_end + 2 >= line.size()) return 0;
    std::istringstream fields(line.substr(comm_end + 2));
    std::string value;
    for (int field = 3; field <= 22; ++field) {
        if (!(fields >> value)) return 0;
        if (field != 22) continue;
        uint64_t start_time = 0;
        auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), start_time);
        return error == std::errc() && end == value.data() + value.size()
                       ? start_time
                       : 0;
    }
    return 0;
}

int OpenProcessFd(int32_t process_id) {
#ifdef SYS_pidfd_open
    return static_cast<int>(syscall(SYS_pidfd_open, process_id, 0));
#else
    (void) process_id;
    errno = ENOSYS;
    return -1;
#endif
}

int OpenTargetMemoryFile(int32_t process_id, int target_fd) {
    if (process_id <= 0 || target_fd < 0) return -1;
    std::string path = "/proc/" + std::to_string(process_id) +
                       "/fd/" + std::to_string(target_fd);
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat status {};
    int seals = fcntl(fd, F_GET_SEALS);
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size != 0 ||
        seals != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool DirectoryExists(const std::string &path) {
    struct stat status {};
    return lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode) &&
           status.st_uid == 0;
}

class RemotePreferencesManager {
public:
    RemotePreferencesManager() {
        EnsureInotify();
        std::thread([this]() { Run(); }).detach();
    }

    RemotePreferencesSession Register(int32_t process_id, int shared_memory_fd,
                                      const std::string &process_name,
                                      ProcessState &state) {
        RemotePreferencesSession output;
        if (process_id <= 0 || ReadProcessStartTime(process_id) == 0) {
            output.failure_reason = "PROCESS_INVALID";
            return output;
        }
        size_t region_size = SharedPreferencesRegionSize(state.modules.size());
        if (region_size == 0) {
            output.failure_reason = "SIZE_LIMIT";
            return output;
        }
        int region_fd = OpenTargetMemoryFile(process_id, shared_memory_fd);
        if (region_fd < 0 || ftruncate(region_fd, static_cast<off_t>(region_size)) != 0) {
            if (region_fd >= 0) close(region_fd);
            output.failure_reason = "SHARED_MEMORY_CREATE_FAILED";
            return output;
        }
        void *region = mmap(nullptr, region_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, region_fd, 0);
        if (region == MAP_FAILED) {
            close(region_fd);
            output.failure_reason = "SHARED_MEMORY_CREATE_FAILED";
            return output;
        }
        madvise(region, region_size, MADV_DONTDUMP);

        std::lock_guard lock(mutex_);
        EnsureInotify();
        if (inotify_fd_ < 0) {
            munmap(region, region_size);
            close(region_fd);
            output.failure_reason = "INOTIFY_SETUP_FAILED";
            return output;
        }

        std::vector<std::map<std::string, std::string>> snapshots;
        snapshots.reserve(state.modules.size());
        for (auto &module : state.modules) {
            ManagedModule &managed = modules_[module.module_id];
            if (managed.sessions.empty()) {
                EnsureWatchLocked(module.module_id, managed);
                std::map<std::string, std::string> loaded;
                if (LoadModulePreferences(module.module_id, loaded)) {
                    managed.groups = std::move(loaded);
                } else {
                    managed.groups = module.remote_preferences;
                }
            }
            module.remote_preferences = managed.groups;
            snapshots.push_back(managed.groups);
        }

        std::vector<uint32_t> generations;
        if (!InitializeSharedPreferencesRegion(region, region_size, snapshots, generations)) {
            munmap(region, region_size);
            close(region_fd);
            output.failure_reason = "SHARED_MEMORY_CREATE_FAILED";
            return output;
        }
        constexpr int base_seals = F_SEAL_GROW | F_SEAL_SHRINK;
#ifdef F_SEAL_FUTURE_WRITE
        int seal_result = fcntl(region_fd, F_ADD_SEALS,
                                base_seals | F_SEAL_FUTURE_WRITE | F_SEAL_SEAL);
#else
        int seal_result = -1;
        errno = EINVAL;
#endif
        if (seal_result != 0 &&
            fcntl(region_fd, F_ADD_SEALS, base_seals | F_SEAL_SEAL) != 0) {
            munmap(region, region_size);
            close(region_fd);
            output.failure_reason = "SHARED_MEMORY_CREATE_FAILED";
            return output;
        }

        auto session = std::make_unique<ManagedSession>();
        session->id = next_session_id_++;
        session->process_id = process_id;
        session->process_start_time = ReadProcessStartTime(process_id);
        session->process_fd = OpenProcessFd(process_id);
        session->region_fd = region_fd;
        session->region = region;
        session->region_size = region_size;
        for (size_t index = 0; index < state.modules.size(); ++index) {
            state.modules[index].remote_preferences_generation = generations[index];
            session->module_ids.push_back(state.modules[index].module_id);
            modules_[state.modules[index].module_id].sessions.insert(session->id);
        }
        output.id = session->id;
        output.region_size = region_size;
        sessions_.emplace(session->id, std::move(session));
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "REMOTE_PREFS_CHANNEL_READY mode=SHARED_MEMORY process=%s",
                process_name.c_str());
        return output;
    }

    void Unregister(uint64_t session_id) {
        if (session_id == 0) return;
        std::lock_guard lock(mutex_);
        RemoveSessionLocked(session_id);
    }

private:
    void EnsureInotify() {
        if (inotify_fd_ >= 0) return;
        inotify_fd_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    }

    bool EnsureWatchLocked(const std::string &module_id, ManagedModule &module) {
        if (module.watch_descriptor >= 0) return false;
        if (inotify_fd_ < 0) return false;
        std::string directory = std::string(kDataDir) + "/" + module_id + "/preferences";
        if (!DirectoryExists(directory)) return false;
        int descriptor = inotify_add_watch(inotify_fd_, directory.c_str(), kPreferenceWatchMask);
        if (descriptor < 0) return false;
        module.watch_descriptor = descriptor;
        watch_modules_[descriptor] = module_id;
        return true;
    }

    void RemoveSessionLocked(uint64_t session_id) {
        auto found = sessions_.find(session_id);
        if (found == sessions_.end()) return;
        ManagedSession &session = *found->second;
        for (const std::string &module_id : session.module_ids) {
            auto module = modules_.find(module_id);
            if (module == modules_.end()) continue;
            module->second.sessions.erase(session_id);
            if (!module->second.sessions.empty()) continue;
            if (module->second.watch_descriptor >= 0) {
                watch_modules_.erase(module->second.watch_descriptor);
                inotify_rm_watch(inotify_fd_, module->second.watch_descriptor);
            }
            modules_.erase(module);
        }
        if (session.region != nullptr) munmap(session.region, session.region_size);
        if (session.region_fd >= 0) close(session.region_fd);
        if (session.process_fd >= 0) close(session.process_fd);
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "REMOTE_PREFS_SESSION_CLOSED pid=%d", session.process_id);
        sessions_.erase(found);
    }

    bool IsSessionAliveLocked(const ManagedSession &session) {
        if (session.process_fd >= 0) {
            pollfd process{session.process_fd, POLLIN, 0};
            int result = TEMP_FAILURE_RETRY(poll(&process, 1, 0));
            return result == 0;
        }
        uint64_t current_start_time = ReadProcessStartTime(session.process_id);
        return current_start_time != 0 && current_start_time == session.process_start_time;
    }

    void ReloadModuleLocked(const std::string &module_id) {
        auto module_it = modules_.find(module_id);
        if (module_it == modules_.end()) return;
        std::map<std::string, std::string> current;
        if (!LoadModulePreferences(module_id, current)) return;
        ManagedModule &module = module_it->second;
        if (current == module.groups) return;
        module.groups = current;
        for (uint64_t session_id : module.sessions) {
            auto session_it = sessions_.find(session_id);
            if (session_it == sessions_.end()) continue;
            ManagedSession &session = *session_it->second;
            auto id = std::find(session.module_ids.begin(), session.module_ids.end(), module_id);
            if (id == session.module_ids.end()) continue;
            size_t index = static_cast<size_t>(id - session.module_ids.begin());
            if (!PublishSharedPreferencesModule(session.region, session.region_size,
                                                index, current)) {
                ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                        "REMOTE_PREFS_DEGRADED reason=SIZE_LIMIT id=%s",
                        module_id.c_str());
            }
        }
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "REMOTE_PREFS_UPDATE id=%s", module_id.c_str());
    }

    void ProcessInotifyLocked() {
        std::vector<char> buffer(16 * 1024);
        std::set<std::string> dirty;
        while (true) {
            ssize_t size = TEMP_FAILURE_RETRY(read(inotify_fd_, buffer.data(), buffer.size()));
            if (size <= 0) break;
            size_t offset = 0;
            while (offset + sizeof(inotify_event) <= static_cast<size_t>(size)) {
                const auto *event = reinterpret_cast<const inotify_event *>(
                        buffer.data() + offset);
                if ((event->mask & IN_Q_OVERFLOW) != 0) {
                    for (const auto &[module_id, state] : modules_) {
                        (void) state;
                        dirty.insert(module_id);
                    }
                }
                auto module = watch_modules_.find(event->wd);
                if (module != watch_modules_.end()) {
                    dirty.insert(module->second);
                    if ((event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) != 0) {
                        auto state = modules_.find(module->second);
                        if (state != modules_.end()) state->second.watch_descriptor = -1;
                        watch_modules_.erase(module);
                    }
                }
                offset += sizeof(inotify_event) + event->len;
            }
        }
        for (const std::string &module_id : dirty) ReloadModuleLocked(module_id);
    }

    void MaintainLocked(uint64_t now) {
        std::vector<uint64_t> dead;
        for (const auto &[id, session] : sessions_) {
            if (!IsSessionAliveLocked(*session)) dead.push_back(id);
        }
        for (uint64_t id : dead) RemoveSessionLocked(id);

        if (now >= next_watch_retry_ns_) {
            EnsureInotify();
            std::vector<std::string> reloaded;
            for (auto &[module_id, module] : modules_) {
                if (!module.sessions.empty() && EnsureWatchLocked(module_id, module)) {
                    reloaded.push_back(module_id);
                }
            }
            for (const std::string &module_id : reloaded) ReloadModuleLocked(module_id);
            next_watch_retry_ns_ = now + 30ULL * 1000 * 1000 * 1000;
        }
        if (now >= next_heartbeat_ns_) {
            for (const auto &[id, session] : sessions_) {
                (void) id;
                UpdateSharedPreferencesHeartbeat(session->region, now);
            }
            next_heartbeat_ns_ = now + kSharedPreferencesHeartbeatIntervalNs;
        }
    }

    void Run() {
        while (true) {
            int watcher = -1;
            short watcher_events = 0;
            {
                std::lock_guard lock(mutex_);
                EnsureInotify();
                watcher = inotify_fd_;
            }
            if (watcher >= 0) {
                pollfd fd{watcher, POLLIN | POLLERR, 0};
                TEMP_FAILURE_RETRY(poll(&fd, 1, 1000));
                watcher_events = fd.revents;
            } else {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            std::lock_guard lock(mutex_);
            if ((watcher_events & (POLLERR | POLLNVAL)) != 0 &&
                watcher == inotify_fd_) {
                close(inotify_fd_);
                inotify_fd_ = -1;
                next_watch_retry_ns_ = 0;
                watch_modules_.clear();
                for (auto &[module_id, module] : modules_) {
                    (void) module_id;
                    module.watch_descriptor = -1;
                }
            } else if (inotify_fd_ >= 0) {
                ProcessInotifyLocked();
            }
            MaintainLocked(SharedPreferencesBootTimeNs());
        }
    }

    std::mutex mutex_;
    int inotify_fd_ = -1;
    uint64_t next_session_id_ = 1;
    uint64_t next_watch_retry_ns_ = 0;
    uint64_t next_heartbeat_ns_ = 0;
    std::map<uint64_t, std::unique_ptr<ManagedSession>> sessions_;
    std::map<std::string, ManagedModule> modules_;
    std::map<int, std::string> watch_modules_;
};

RemotePreferencesManager &Manager() {
    static auto *manager = new RemotePreferencesManager();
    return *manager;
}

}  // namespace

RemotePreferencesSession RegisterRemotePreferencesSession(
        int32_t process_id, int shared_memory_fd, const std::string &process_name,
        ProcessState &state) {
    return Manager().Register(process_id, shared_memory_fd, process_name, state);
}

void UnregisterRemotePreferencesSession(uint64_t session_id) {
    Manager().Unregister(session_id);
}

}  // namespace zygisk_framework
