#include "art_symbol_resolver.hpp"

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "logging.hpp"

namespace zygisk_framework {
namespace {

#if defined(__LP64__)
using NativeEhdr = Elf64_Ehdr;
using NativeShdr = Elf64_Shdr;
using NativeSym = Elf64_Sym;
constexpr unsigned char kNativeElfClass = ELFCLASS64;
#else
using NativeEhdr = Elf32_Ehdr;
using NativeShdr = Elf32_Shdr;
using NativeSym = Elf32_Sym;
constexpr unsigned char kNativeElfClass = ELFCLASS32;
#endif

struct ArtLibraryInfo {
    std::string path;
    uintptr_t load_bias = 0;
    void *handle = nullptr;
    const uint8_t *elf = nullptr;
    size_t elf_size = 0;
};

std::once_flag g_init_once;
ArtLibraryInfo g_art;

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           memcmp(value.data(), prefix.data(), prefix.size()) == 0;
}

std::string DefaultArtPath() {
#if defined(__LP64__)
    return "/apex/com.android.art/lib64/libart.so";
#else
    return "/apex/com.android.art/lib/libart.so";
#endif
}

std::string LegacyArtPath() {
#if defined(__LP64__)
    return "/system/lib64/libart.so";
#else
    return "/system/lib/libart.so";
#endif
}

int FindArtCallback(dl_phdr_info *info, size_t, void *) {
    if (info == nullptr || info->dlpi_name == nullptr) {
        return 0;
    }
    std::string path = info->dlpi_name;
    if (path.find("libart.so") == std::string::npos) {
        return 0;
    }
    g_art.path = path;
    g_art.load_bias = static_cast<uintptr_t>(info->dlpi_addr);
    return 1;
}

bool InBounds(uint64_t offset, uint64_t size) {
    return g_art.elf != nullptr && offset <= g_art.elf_size && size <= g_art.elf_size - offset;
}

const NativeShdr *SectionAt(const NativeEhdr *ehdr, size_t index) {
    uint64_t offset = ehdr->e_shoff + static_cast<uint64_t>(index) * ehdr->e_shentsize;
    if (!InBounds(offset, sizeof(NativeShdr))) {
        return nullptr;
    }
    return reinterpret_cast<const NativeShdr *>(g_art.elf + offset);
}

bool TryMapArtFile(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return false;
    }

    void *mapped = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        return false;
    }

    g_art.path = path;
    g_art.elf = static_cast<const uint8_t *>(mapped);
    g_art.elf_size = static_cast<size_t>(st.st_size);
    return true;
}

void TryOpenArtHandle() {
    if (!g_art.path.empty()) {
        g_art.handle = dlopen(g_art.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
        if (g_art.handle == nullptr) {
            g_art.handle = dlopen(g_art.path.c_str(), RTLD_NOW);
        }
    }

    if (g_art.handle == nullptr) {
        g_art.handle = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
    }
    if (g_art.handle == nullptr) {
        g_art.handle = dlopen("libart.so", RTLD_NOW);
    }
}

void TryMapArtElf() {
    std::vector<std::string> candidates;
    if (!g_art.path.empty()) {
        candidates.push_back(g_art.path);
    }
    candidates.push_back(DefaultArtPath());
    candidates.push_back(LegacyArtPath());

    for (const std::string &candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (TryMapArtFile(candidate)) {
            return;
        }
    }
}

bool IsUsableElfHeader(const NativeEhdr *ehdr) {
    if (ehdr == nullptr) {
        return false;
    }
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        return false;
    }
    if (ehdr->e_ident[EI_CLASS] != kNativeElfClass) {
        return false;
    }
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0 || ehdr->e_shentsize < sizeof(NativeShdr)) {
        return false;
    }
    uint64_t sh_size = static_cast<uint64_t>(ehdr->e_shnum) * ehdr->e_shentsize;
    return InBounds(ehdr->e_shoff, sh_size);
}

void *AddressForSymbol(const NativeEhdr *ehdr, const NativeSym *symbol) {
    if (symbol == nullptr || symbol->st_value == 0 || symbol->st_shndx == SHN_UNDEF) {
        return nullptr;
    }
    uintptr_t address = static_cast<uintptr_t>(symbol->st_value);
    if (ehdr->e_type == ET_DYN) {
        address += g_art.load_bias;
    }
    return reinterpret_cast<void *>(address);
}

void *FindInSymbolSection(const NativeEhdr *ehdr, const NativeShdr *symbol_section,
                          const NativeShdr *string_section, std::string_view query,
                          bool prefix_match) {
    if (symbol_section == nullptr || string_section == nullptr) {
        return nullptr;
    }
    if (symbol_section->sh_entsize < sizeof(NativeSym) || symbol_section->sh_size == 0) {
        return nullptr;
    }
    if (!InBounds(symbol_section->sh_offset, symbol_section->sh_size) ||
        !InBounds(string_section->sh_offset, string_section->sh_size)) {
        return nullptr;
    }

    const char *string_table = reinterpret_cast<const char *>(g_art.elf + string_section->sh_offset);
    uint64_t count = symbol_section->sh_size / symbol_section->sh_entsize;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t offset = symbol_section->sh_offset + i * symbol_section->sh_entsize;
        if (!InBounds(offset, sizeof(NativeSym))) {
            continue;
        }
        const auto *symbol = reinterpret_cast<const NativeSym *>(g_art.elf + offset);
        if (symbol->st_name >= string_section->sh_size) {
            continue;
        }
        const char *name = string_table + symbol->st_name;
        size_t max_length = static_cast<size_t>(string_section->sh_size - symbol->st_name);
        size_t name_length = strnlen(name, max_length);
        if (name_length == max_length) {
            continue;
        }

        std::string_view candidate(name, name_length);
        bool match = prefix_match ? StartsWith(candidate, query) : candidate == query;
        if (!match) {
            continue;
        }

        if (void *address = AddressForSymbol(ehdr, symbol)) {
            return address;
        }
    }
    return nullptr;
}

void *ResolveFromElf(std::string_view query, bool prefix_match) {
    if (g_art.elf == nullptr || g_art.elf_size < sizeof(NativeEhdr)) {
        return nullptr;
    }

    const auto *ehdr = reinterpret_cast<const NativeEhdr *>(g_art.elf);
    if (!IsUsableElfHeader(ehdr)) {
        return nullptr;
    }

    for (size_t i = 0; i < ehdr->e_shnum; ++i) {
        const NativeShdr *section = SectionAt(ehdr, i);
        if (section == nullptr || (section->sh_type != SHT_DYNSYM && section->sh_type != SHT_SYMTAB)) {
            continue;
        }
        if (section->sh_link >= ehdr->e_shnum) {
            continue;
        }
        const NativeShdr *strings = SectionAt(ehdr, section->sh_link);
        if (strings == nullptr || strings->sh_type != SHT_STRTAB) {
            continue;
        }
        if (void *address = FindInSymbolSection(ehdr, section, strings, query, prefix_match)) {
            return address;
        }
    }
    return nullptr;
}

void EnsureArtResolverReady() {
    std::call_once(g_init_once, [] {
        dl_iterate_phdr(FindArtCallback, nullptr);
        TryOpenArtHandle();
        TryMapArtElf();

        if (g_art.path.empty()) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "ART_LIBRARY_NOT_FOUND");
            return;
        }

        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "ART_LIBRARY_READY path=%s base=%p handle=%p elf=%zu",
                g_art.path.c_str(), reinterpret_cast<void *>(g_art.load_bias), g_art.handle,
                g_art.elf_size);
    });
}

}  // namespace

void *ResolveArtSymbol(std::string_view symbol_name) {
    EnsureArtResolverReady();
    std::string symbol(symbol_name);

    if (g_art.handle != nullptr) {
        if (void *address = dlsym(g_art.handle, symbol.c_str())) {
            return address;
        }
    }
    if (void *address = dlsym(RTLD_DEFAULT, symbol.c_str())) {
        return address;
    }
    if (void *address = ResolveFromElf(symbol_name, false)) {
        return address;
    }

    ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "ART_SYMBOL_NOT_FOUND symbol=%s", symbol.c_str());
    return nullptr;
}

void *ResolveArtSymbolPrefix(std::string_view symbol_prefix) {
    EnsureArtResolverReady();
    if (void *address = ResolveFromElf(symbol_prefix, true)) {
        return address;
    }

    ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "ART_SYMBOL_NOT_FOUND symbol=%.*s*",
            static_cast<int>(symbol_prefix.size()), symbol_prefix.data());
    return nullptr;
}

}  // namespace zygisk_framework
