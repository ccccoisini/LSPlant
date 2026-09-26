#include "shared_preferences_transport.hpp"

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using zygisk_framework::InitializeSharedPreferencesRegion;
using zygisk_framework::PublishSharedPreferencesModule;
using zygisk_framework::ReadSharedPreferencesGlobalGeneration;
using zygisk_framework::ReadSharedPreferencesModule;
using zygisk_framework::SharedPreferencesModuleHeader;
using zygisk_framework::SharedPreferencesRegionHeader;
using zygisk_framework::SharedPreferencesRegionSize;
using zygisk_framework::WaitForSharedPreferencesGeneration;

[[noreturn]] void Fail(const char *message) {
    std::cerr << "REMOTE_PREFERENCES_TRANSPORT_TEST_FAIL: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, const char *message) {
    if (!condition) Fail(message);
}

std::map<std::string, std::string> Groups(std::string value) {
    return {{"settings", std::move(value)}};
}

void TestLayoutAndFinalState() {
    size_t size = SharedPreferencesRegionSize(1);
    Require(size > 0, "single-module layout");
    Require(SharedPreferencesRegionSize(1024) == 0, "region size limit");
    std::vector<uint8_t> bytes(size);
    std::vector<uint32_t> generations;
    Require(InitializeSharedPreferencesRegion(bytes.data(), bytes.size(),
                                              {Groups("initial")}, generations),
            "region initialization");
    Require(generations.size() == 1 && generations[0] == 1, "baseline generation");
    Require(PublishSharedPreferencesModule(bytes.data(), bytes.size(), 0, Groups("middle")),
            "first publish");
    Require(PublishSharedPreferencesModule(bytes.data(), bytes.size(), 0, Groups("final")),
            "second publish");
    std::map<std::string, std::string> current;
    uint32_t generation = 0;
    Require(ReadSharedPreferencesModule(bytes.data(), bytes.size(), 0,
                                        current, generation),
            "read final snapshot");
    Require(current == Groups("final") && generation == 3, "final-state coalescing");
    Require(PublishSharedPreferencesModule(bytes.data(), bytes.size(), 0, {}),
            "publish module deletion");
    Require(ReadSharedPreferencesModule(bytes.data(), bytes.size(), 0,
                                        current, generation) && current.empty(),
            "read module deletion");
}

void TestChecksumRejection() {
    size_t size = SharedPreferencesRegionSize(1);
    std::vector<uint8_t> bytes(size);
    std::vector<uint32_t> generations;
    Require(InitializeSharedPreferencesRegion(bytes.data(), bytes.size(),
                                              {Groups("valid")}, generations),
            "checksum region initialization");
    auto *module = reinterpret_cast<SharedPreferencesModuleHeader *>(
            bytes.data() + sizeof(SharedPreferencesRegionHeader));
    bytes[module->payload_offset + module->payload_size - 1] ^= 0x7f;
    std::map<std::string, std::string> current;
    uint32_t generation = 0;
    Require(!ReadSharedPreferencesModule(bytes.data(), bytes.size(), 0,
                                         current, generation),
            "checksum rejection");
}

void TestCrossProcessFutex() {
    size_t size = SharedPreferencesRegionSize(1);
    void *region = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    Require(region != MAP_FAILED, "shared mapping");
    std::vector<uint32_t> generations;
    Require(InitializeSharedPreferencesRegion(region, size,
                                              {Groups("before")}, generations),
            "futex region initialization");
    uint32_t expected = ReadSharedPreferencesGlobalGeneration(region);
    pid_t child = fork();
    Require(child >= 0, "fork");
    if (child == 0) {
        timespec timeout{5, 0};
        WaitForSharedPreferencesGeneration(region, expected, &timeout);
        _exit(ReadSharedPreferencesGlobalGeneration(region) == expected ? 1 : 0);
    }
    usleep(100 * 1000);
    Require(PublishSharedPreferencesModule(region, size, 0, Groups("after")),
            "futex publish");
    int status = 0;
    Require(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                    WEXITSTATUS(status) == 0,
            "cross-process futex wake");
    munmap(region, size);
}

void TestReadOnlyMemfdHandoff() {
    size_t size = SharedPreferencesRegionSize(1);
    int target_fd = static_cast<int>(syscall(
            SYS_memfd_create, "remote_preferences_test",
            MFD_CLOEXEC | MFD_ALLOW_SEALING));
    Require(target_fd >= 0, "target memfd creation");
    std::string path = "/proc/self/fd/" + std::to_string(target_fd);
    int writable_fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    Require(writable_fd >= 0 && ftruncate(writable_fd, static_cast<off_t>(size)) == 0,
            "companion memfd open");
    void *writable = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, writable_fd, 0);
    Require(writable != MAP_FAILED, "writable memfd mapping");
    std::vector<uint32_t> generations;
    Require(InitializeSharedPreferencesRegion(writable, size,
                                              {Groups("memfd")}, generations),
            "memfd initialization");
    constexpr int base_seals = F_SEAL_GROW | F_SEAL_SHRINK;
    bool future_write_sealed = false;
#ifdef F_SEAL_FUTURE_WRITE
    future_write_sealed = fcntl(writable_fd, F_ADD_SEALS,
                                base_seals | F_SEAL_FUTURE_WRITE | F_SEAL_SEAL) == 0;
#endif
    if (!future_write_sealed) {
        Require(fcntl(writable_fd, F_ADD_SEALS, base_seals | F_SEAL_SEAL) == 0,
                "memfd fallback seals");
    }
    void *read_only = mmap(nullptr, size, PROT_READ, MAP_SHARED, target_fd, 0);
    Require(read_only != MAP_FAILED, "read-only memfd mapping");
    if (future_write_sealed) {
        void *forbidden = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                               MAP_SHARED, target_fd, 0);
        Require(forbidden == MAP_FAILED, "sealed memfd rejects writable mapping");
    }
    std::map<std::string, std::string> current;
    uint32_t generation = 0;
    Require(ReadSharedPreferencesModule(read_only, size, 0, current, generation) &&
                    current == Groups("memfd"),
            "read-only memfd snapshot");
    munmap(read_only, size);
    close(target_fd);
    munmap(writable, size);
    close(writable_fd);
}

}  // namespace

int main() {
    TestLayoutAndFinalState();
    TestChecksumRejection();
    TestCrossProcessFutex();
    TestReadOnlyMemfdHandoff();
    std::cout << "REMOTE_PREFERENCES_TRANSPORT_TEST_PASS\n";
    return 0;
}
