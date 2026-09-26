#include "preference_store.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <charconv>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#include "logging.hpp"

namespace zygisk_framework {
namespace {

constexpr const char *kDataDir = "/data/adb/zygisk_framework/data";
constexpr size_t kMaxPreferenceKeySize = 512;
constexpr size_t kMaxPreferenceValueSize = 64 * 1024;
constexpr size_t kMaxPreferenceEntries = 4096;

bool IsValidModuleId(const std::string &value) {
    if (value.empty() || value.size() > 128 ||
        !std::isalpha(static_cast<unsigned char>(value.front()))) {
        return false;
    }
    for (unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '.' && ch != '_' && ch != '-') return false;
    }
    return true;
}

bool IsValidUtf8(const std::string &value) {
    size_t index = 0;
    while (index < value.size()) {
        unsigned char lead = static_cast<unsigned char>(value[index++]);
        if (lead == 0) return false;
        if (lead < 0x80) continue;
        int continuation = 0;
        uint32_t code_point = 0;
        if ((lead & 0xe0) == 0xc0) {
            continuation = 1;
            code_point = lead & 0x1f;
            if (code_point < 2) return false;
        } else if ((lead & 0xf0) == 0xe0) {
            continuation = 2;
            code_point = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0) {
            continuation = 3;
            code_point = lead & 0x07;
        } else {
            return false;
        }
        if (index + continuation > value.size()) return false;
        for (int i = 0; i < continuation; ++i) {
            unsigned char next = static_cast<unsigned char>(value[index++]);
            if ((next & 0xc0) != 0x80) return false;
            code_point = (code_point << 6) | (next & 0x3f);
        }
        if ((continuation == 2 && code_point < 0x800) ||
            (continuation == 3 && code_point < 0x10000) ||
            (code_point >= 0xd800 && code_point <= 0xdfff) || code_point > 0x10ffff) {
            return false;
        }
    }
    return true;
}

int Base64Value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

bool DecodeBase64(const std::string &encoded, std::string &decoded) {
    decoded.clear();
    if (encoded.size() % 4 != 0) return false;
    for (size_t offset = 0; offset < encoded.size(); offset += 4) {
        uint32_t block = 0;
        int padding = 0;
        for (size_t index = 0; index < 4; ++index) {
            unsigned char ch = static_cast<unsigned char>(encoded[offset + index]);
            if (ch == '=') {
                if (index < 2 || offset + 4 != encoded.size()) return false;
                ++padding;
                block <<= 6;
            } else {
                if (padding != 0) return false;
                int value = Base64Value(ch);
                if (value < 0) return false;
                block = (block << 6) | static_cast<uint32_t>(value);
            }
        }
        if (padding > 2) return false;
        decoded.push_back(static_cast<char>((block >> 16) & 0xff));
        if (padding < 2) decoded.push_back(static_cast<char>((block >> 8) & 0xff));
        if (padding < 1) decoded.push_back(static_cast<char>(block & 0xff));
    }
    return true;
}

std::string ToBase64Url(std::string value) {
    while (!value.empty() && value.back() == '=') value.pop_back();
    for (char &ch : value) {
        if (ch == '+') ch = '-';
        if (ch == '/') ch = '_';
    }
    return value;
}

std::vector<std::string> SplitTabs(const std::string &line) {
    std::vector<std::string> fields;
    size_t begin = 0;
    while (true) {
        size_t tab = line.find('\t', begin);
        if (tab == std::string::npos) {
            fields.emplace_back(line.substr(begin));
            return fields;
        }
        fields.emplace_back(line.substr(begin, tab - begin));
        begin = tab + 1;
    }
}

template <typename T>
bool ParseInteger(const std::string &value) {
    if (value.empty()) return false;
    T parsed{};
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return error == std::errc() && end == value.data() + value.size();
}

bool ParseFloat(const std::string &value) {
    if (value.empty()) return false;
    if (value == "NaN" || value == "Infinity" || value == "-Infinity") return true;
    errno = 0;
    char *end = nullptr;
    float parsed = std::strtof(value.c_str(), &end);
    return end == value.c_str() + value.size() && std::isfinite(parsed) &&
           (errno != ERANGE || parsed != 0.0f);
}

bool ReadRegularFile(const std::string &path, std::string &content) {
    struct stat st {};
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
        (st.st_mode & 0777) != 0600 || st.st_size <= 0 ||
        static_cast<size_t>(st.st_size) > kMaxPreferenceGroupSize) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return content.size() == static_cast<size_t>(st.st_size);
}

}  // namespace

bool EnsurePreferenceDirectory(const std::string &module_id, std::string &path) {
    if (!IsValidModuleId(module_id)) return false;
    if (mkdir(kDataDir, 0700) != 0 && errno != EEXIST) return false;
    std::string module_dir = std::string(kDataDir) + "/" + module_id;
    if (mkdir(module_dir.c_str(), 0700) != 0 && errno != EEXIST) return false;
    path = module_dir + "/preferences";
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) return false;
    for (const std::string &candidate : {std::string(kDataDir), module_dir, path}) {
        struct stat st {};
        if (lstat(candidate.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != 0) {
            return false;
        }
        if (chmod(candidate.c_str(), 0700) != 0) return false;
    }
    return true;
}

bool ValidatePreferenceSnapshot(const std::string &expected_group,
                                const std::string &snapshot) {
    if (expected_group.empty() || expected_group.size() > 128 || !IsValidUtf8(expected_group) ||
        snapshot.empty() || snapshot.size() > kMaxPreferenceGroupSize ||
        snapshot.back() != '\n') {
        return false;
    }
    std::istringstream stream(snapshot);
    std::string line;
    if (!std::getline(stream, line)) return false;
    std::vector<std::string> header = SplitTabs(line);
    std::string decoded_group;
    if (header.size() != 3 || header[0] != "ZPREFS" || header[1] != "1" ||
        !DecodeBase64(header[2], decoded_group) || decoded_group != expected_group) {
        return false;
    }
    std::set<std::string> keys;
    size_t entry_count = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (++entry_count > kMaxPreferenceEntries) return false;
        std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() < 3) return false;
        std::string key;
        if (!DecodeBase64(fields[1], key) || key.empty() || key.size() > kMaxPreferenceKeySize ||
            !IsValidUtf8(key) || !keys.insert(key).second) {
            return false;
        }
        if (fields[0] == "string") {
            std::string value;
            if (fields.size() != 3 || !DecodeBase64(fields[2], value) ||
                value.size() > kMaxPreferenceValueSize || !IsValidUtf8(value)) return false;
        } else if (fields[0] == "string-set") {
            uint32_t count = 0;
            auto [end, error] = std::from_chars(
                    fields[2].data(), fields[2].data() + fields[2].size(), count);
            if (error != std::errc() || end != fields[2].data() + fields[2].size() ||
                count > 4096 || fields.size() != static_cast<size_t>(count) + 3) return false;
            std::set<std::string> values;
            for (size_t index = 3; index < fields.size(); ++index) {
                std::string value;
                if (!DecodeBase64(fields[index], value) || value.size() > kMaxPreferenceValueSize ||
                    !IsValidUtf8(value) || !values.insert(value).second) return false;
            }
        } else if (fields[0] == "int") {
            if (fields.size() != 3 || !ParseInteger<int32_t>(fields[2])) return false;
        } else if (fields[0] == "long") {
            if (fields.size() != 3 || !ParseInteger<int64_t>(fields[2])) return false;
        } else if (fields[0] == "float") {
            if (fields.size() != 3 || !ParseFloat(fields[2])) return false;
        } else if (fields[0] == "boolean") {
            if (fields.size() != 3 || (fields[2] != "true" && fields[2] != "false")) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool LoadModulePreferences(const std::string &module_id,
                           std::map<std::string, std::string> &groups) {
    groups.clear();
    if (!IsValidModuleId(module_id)) return false;
    std::string directory = std::string(kDataDir) + "/" + module_id + "/preferences";
    struct stat directory_st {};
    if (lstat(directory.c_str(), &directory_st) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(directory_st.st_mode) || directory_st.st_uid != 0 ||
        (directory_st.st_mode & 0777) != 0700) {
        return false;
    }
    DIR *dir = opendir(directory.c_str());
    if (dir == nullptr) return false;
    bool valid = true;
    size_t total_size = 0;
    while (dirent *entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == ".." || name.size() <= 6 ||
            name.substr(name.size() - 6) != ".prefs") continue;
        std::string snapshot;
        std::string path = directory + "/" + name;
        if (!ReadRegularFile(path, snapshot)) {
            valid = false;
            continue;
        }
        size_t newline = snapshot.find('\n');
        std::vector<std::string> header = SplitTabs(snapshot.substr(0, newline));
        std::string group;
        if (newline == std::string::npos || header.size() != 3 ||
            !DecodeBase64(header[2], group) ||
            name != ToBase64Url(header[2]) + ".prefs" ||
            !ValidatePreferenceSnapshot(group, snapshot) || groups.size() >= kMaxPreferenceGroups ||
            total_size + snapshot.size() > kMaxPreferenceModuleSize ||
            !groups.emplace(group, snapshot).second) {
            valid = false;
            continue;
        }
        total_size += snapshot.size();
    }
    closedir(dir);
    if (!valid) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_COMPANION,
                "REMOTE_PREFS_INVALID id=%s", module_id.c_str());
    }
    return valid;
}

}  // namespace zygisk_framework
