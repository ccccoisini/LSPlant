#include "dex_loader.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "framework_dex.h"
#include "framework_mapping.h"
#include "jni_bridge.hpp"
#include "logging.hpp"

namespace zygisk_framework {
namespace {

constexpr uint32_t kDexEndianConstant = 0x12345678;
constexpr uint32_t kDexHeaderSize = 0x70;
constexpr uint32_t kDexStringIdsSizeOffset = 0x38;
constexpr uint32_t kDexStringIdsOffOffset = 0x3c;
constexpr uint32_t kDexTypeIdsSizeOffset = 0x40;
constexpr uint32_t kDexTypeIdsOffOffset = 0x44;
constexpr uint32_t kDexClassDefsSizeOffset = 0x60;
constexpr uint32_t kDexClassDefsOffOffset = 0x64;
constexpr size_t kDexStringIdSize = 4;
constexpr size_t kDexTypeIdSize = 4;
constexpr size_t kDexClassDefSize = 32;

struct MappedDex {
    void *data = nullptr;
    size_t size = 0;
};

struct DexElementsPatch {
    jobject path_list = nullptr;
    jfieldID dex_elements_field = nullptr;
    jobjectArray original_elements = nullptr;
};

std::vector<MappedDex> &PersistentMappings() {
    static auto *mappings = new std::vector<MappedDex>();
    return *mappings;
}

std::vector<std::vector<uint8_t>> &PersistentBytes() {
    static auto *bytes = new std::vector<std::vector<uint8_t>>();
    return *bytes;
}

std::vector<jobject> &PersistentClassLoaders() {
    static auto *loaders = new std::vector<jobject>();
    return *loaders;
}

bool ClearJniException(JNIEnv *env, const char *context) {
    if (env == nullptr || !env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionDescribe();
    env->ExceptionClear();
    ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "JNI_EXCEPTION context=%s", context);
    return true;
}

bool HasDexMagic(const void *data, size_t size) {
    if (data == nullptr || size < 4) {
        return false;
    }
    auto *bytes = static_cast<const uint8_t *>(data);
    return bytes[0] == 'd' && bytes[1] == 'e' && bytes[2] == 'x' && bytes[3] == '\n';
}

bool MapDexFd(int fd, MappedDex &mapping) {
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < 4 || st.st_size > 32 * 1024 * 1024) {
        return false;
    }
    void *data = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        return false;
    }
    if (!HasDexMagic(data, static_cast<size_t>(st.st_size))) {
        munmap(data, static_cast<size_t>(st.st_size));
        return false;
    }
    mapping.data = data;
    mapping.size = static_cast<size_t>(st.st_size);
    PersistentMappings().push_back(mapping);
    return true;
}

bool MapDexBytes(const std::vector<uint8_t> &bytes, MappedDex &mapping) {
    if (!HasDexMagic(bytes.data(), bytes.size()) || bytes.size() > 32 * 1024 * 1024) {
        return false;
    }
    PersistentBytes().push_back(bytes);
    const auto &stored = PersistentBytes().back();
    mapping.data = const_cast<uint8_t *>(stored.data());
    mapping.size = stored.size();
    return true;
}

bool DexRangeOk(size_t dex_len, uint32_t offset, uint32_t count, size_t item_size) {
    uint64_t begin = offset;
    uint64_t bytes = static_cast<uint64_t>(count) * item_size;
    return begin <= dex_len && bytes <= dex_len - begin;
}

bool ReadDexU32(const uint8_t *dex, size_t dex_len, uint32_t offset, uint32_t *out) {
    if (offset > dex_len || dex_len - offset < sizeof(uint32_t)) {
        return false;
    }
    *out = static_cast<uint32_t>(dex[offset]) |
           (static_cast<uint32_t>(dex[offset + 1]) << 8) |
           (static_cast<uint32_t>(dex[offset + 2]) << 16) |
           (static_cast<uint32_t>(dex[offset + 3]) << 24);
    return true;
}

bool SkipDexUleb128(const uint8_t *dex, size_t dex_len, size_t *offset) {
    for (size_t i = 0; i < 5; ++i) {
        if (*offset >= dex_len) {
            return false;
        }
        uint8_t value = dex[(*offset)++];
        if ((value & 0x80) == 0) {
            return true;
        }
    }
    return false;
}

bool ReadDexString(const uint8_t *dex, size_t dex_len, uint32_t string_data_off,
                   std::string *out) {
    size_t offset = string_data_off;
    if (!SkipDexUleb128(dex, dex_len, &offset)) {
        return false;
    }
    size_t begin = offset;
    while (offset < dex_len && dex[offset] != '\0') {
        ++offset;
    }
    if (offset >= dex_len) {
        return false;
    }
    out->assign(reinterpret_cast<const char *>(dex + begin), offset - begin);
    return true;
}

bool DescriptorToClassName(const std::string &descriptor, std::string *out) {
    if (descriptor.size() < 2 || descriptor.front() != 'L' || descriptor.back() != ';') {
        return false;
    }
    out->assign(descriptor.data() + 1, descriptor.size() - 2);
    for (char &ch : *out) {
        if (ch == '/') {
            ch = '.';
        }
    }
    return !out->empty();
}

std::vector<std::string> CollectDexClassNames(const MappedDex &mapping) {
    auto *dex = static_cast<const uint8_t *>(mapping.data);
    size_t dex_len = mapping.size;
    std::vector<std::string> class_names;

    if (dex_len < kDexHeaderSize || !HasDexMagic(dex, dex_len)) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=CLASS_SCAN_HEADER_INVALID");
        return class_names;
    }

    uint32_t file_size = 0;
    uint32_t header_size = 0;
    uint32_t endian_tag = 0;
    if (!ReadDexU32(dex, dex_len, 0x20, &file_size) ||
        !ReadDexU32(dex, dex_len, 0x24, &header_size) ||
        !ReadDexU32(dex, dex_len, 0x28, &endian_tag) ||
        header_size < kDexHeaderSize || file_size < header_size ||
        file_size > dex_len || endian_tag != kDexEndianConstant) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=CLASS_SCAN_LAYOUT_INVALID");
        return class_names;
    }

    size_t parse_len = file_size;
    uint32_t string_ids_size = 0;
    uint32_t string_ids_off = 0;
    uint32_t type_ids_size = 0;
    uint32_t type_ids_off = 0;
    uint32_t class_defs_size = 0;
    uint32_t class_defs_off = 0;
    if (!ReadDexU32(dex, parse_len, kDexStringIdsSizeOffset, &string_ids_size) ||
        !ReadDexU32(dex, parse_len, kDexStringIdsOffOffset, &string_ids_off) ||
        !ReadDexU32(dex, parse_len, kDexTypeIdsSizeOffset, &type_ids_size) ||
        !ReadDexU32(dex, parse_len, kDexTypeIdsOffOffset, &type_ids_off) ||
        !ReadDexU32(dex, parse_len, kDexClassDefsSizeOffset, &class_defs_size) ||
        !ReadDexU32(dex, parse_len, kDexClassDefsOffOffset, &class_defs_off) ||
        !DexRangeOk(parse_len, string_ids_off, string_ids_size, kDexStringIdSize) ||
        !DexRangeOk(parse_len, type_ids_off, type_ids_size, kDexTypeIdSize) ||
        !DexRangeOk(parse_len, class_defs_off, class_defs_size, kDexClassDefSize)) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=CLASS_SCAN_TABLE_INVALID");
        return class_names;
    }

    class_names.reserve(class_defs_size);
    for (uint32_t i = 0; i < class_defs_size; ++i) {
        uint32_t class_idx = 0;
        uint32_t descriptor_idx = 0;
        uint32_t string_data_off = 0;
        uint32_t class_def_off = class_defs_off + i * kDexClassDefSize;
        if (!ReadDexU32(dex, parse_len, class_def_off, &class_idx) ||
            class_idx >= type_ids_size ||
            !ReadDexU32(dex, parse_len, type_ids_off + class_idx * kDexTypeIdSize,
                        &descriptor_idx) ||
            descriptor_idx >= string_ids_size ||
            !ReadDexU32(dex, parse_len, string_ids_off + descriptor_idx * kDexStringIdSize,
                        &string_data_off)) {
            continue;
        }
        std::string descriptor;
        std::string class_name;
        if (ReadDexString(dex, parse_len, string_data_off, &descriptor) &&
            DescriptorToClassName(descriptor, &class_name)) {
            class_names.emplace_back(class_name);
        }
    }
    return class_names;
}

std::string Trim(const std::string &value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::map<std::string, std::string> ParseMapping(const std::string &mapping_text) {
    std::map<std::string, std::string> result;
    std::istringstream stream(mapping_text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == ' ' || line[0] == '\t') {
            continue;
        }
        size_t arrow = line.find(" -> ");
        if (arrow == std::string::npos || line.back() != ':') {
            continue;
        }
        std::string original = Trim(line.substr(0, arrow));
        std::string obfuscated = Trim(line.substr(arrow + 4, line.size() - arrow - 5));
        if (!original.empty() && !obfuscated.empty()) {
            result[original] = obfuscated;
        }
    }
    return result;
}

std::string MapClassName(
        const std::map<std::string, std::string> &mapping,
        const std::string &original) {
    auto it = mapping.find(original);
    return it == mapping.end() ? original : it->second;
}

jobject SystemClassLoader(JNIEnv *env) {
    jclass class_loader = env->FindClass("java/lang/ClassLoader");
    jmethodID get_system = env->GetStaticMethodID(
            class_loader, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = env->CallStaticObjectMethod(class_loader, get_system);
    env->DeleteLocalRef(class_loader);
    ClearJniException(env, "ClassLoader.getSystemClassLoader");
    return loader;
}

jobject NewDirectBuffer(JNIEnv *env, const MappedDex &mapping) {
    return env->NewDirectByteBuffer(mapping.data, static_cast<jlong>(mapping.size));
}

jobjectArray NewByteBufferArray(JNIEnv *env, const std::vector<MappedDex> &mappings) {
    jclass buffer_class = env->FindClass("java/nio/ByteBuffer");
    if (ClearJniException(env, "FindClass(ByteBuffer)") || buffer_class == nullptr) {
        return nullptr;
    }
    jobjectArray array = env->NewObjectArray(mappings.size(), buffer_class, nullptr);
    env->DeleteLocalRef(buffer_class);
    if (ClearJniException(env, "NewObjectArray(ByteBuffer)") || array == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < mappings.size(); ++i) {
        jobject buffer = NewDirectBuffer(env, mappings[i]);
        if (ClearJniException(env, "NewDirectByteBuffer") || buffer == nullptr) {
            env->DeleteLocalRef(array);
            return nullptr;
        }
        env->SetObjectArrayElement(array, static_cast<jsize>(i), buffer);
        env->DeleteLocalRef(buffer);
        if (ClearJniException(env, "SetObjectArrayElement(ByteBuffer)")) {
            env->DeleteLocalRef(array);
            return nullptr;
        }
    }
    return array;
}

jobject NewArrayList(JNIEnv *env) {
    jclass array_list_class = env->FindClass("java/util/ArrayList");
    if (ClearJniException(env, "FindClass(ArrayList)") || array_list_class == nullptr) {
        return nullptr;
    }
    jmethodID constructor = env->GetMethodID(array_list_class, "<init>", "()V");
    if (ClearJniException(env, "ArrayList.<init> lookup") || constructor == nullptr) {
        env->DeleteLocalRef(array_list_class);
        return nullptr;
    }
    jobject list = env->NewObject(array_list_class, constructor);
    env->DeleteLocalRef(array_list_class);
    if (ClearJniException(env, "ArrayList.<init>") || list == nullptr) {
        return nullptr;
    }
    return list;
}

jobjectArray MakeInMemoryDexElements(JNIEnv *env, const std::vector<MappedDex> &mappings) {
    jclass dex_path_list_class = env->FindClass("dalvik/system/DexPathList");
    if (ClearJniException(env, "FindClass(DexPathList)") || dex_path_list_class == nullptr) {
        return nullptr;
    }

    jmethodID make_elements = env->GetStaticMethodID(
            dex_path_list_class,
            "makeInMemoryDexElements",
            "([Ljava/nio/ByteBuffer;Ljava/util/List;)[Ldalvik/system/DexPathList$Element;");
    if (ClearJniException(env, "DexPathList.makeInMemoryDexElements lookup") ||
        make_elements == nullptr) {
        env->DeleteLocalRef(dex_path_list_class);
        return nullptr;
    }

    jobjectArray buffers = NewByteBufferArray(env, mappings);
    jobject suppressed = NewArrayList(env);
    if (buffers == nullptr || suppressed == nullptr) {
        if (buffers != nullptr) env->DeleteLocalRef(buffers);
        if (suppressed != nullptr) env->DeleteLocalRef(suppressed);
        env->DeleteLocalRef(dex_path_list_class);
        return nullptr;
    }

    auto elements = static_cast<jobjectArray>(
            env->CallStaticObjectMethod(dex_path_list_class, make_elements, buffers, suppressed));
    env->DeleteLocalRef(suppressed);
    env->DeleteLocalRef(buffers);
    env->DeleteLocalRef(dex_path_list_class);
    if (ClearJniException(env, "DexPathList.makeInMemoryDexElements") ||
        elements == nullptr) {
        return nullptr;
    }
    return elements;
}

jfieldID GetPathListField(JNIEnv *env, jobject class_loader) {
    jclass base_loader_class = env->FindClass("dalvik/system/BaseDexClassLoader");
    if (ClearJniException(env, "FindClass(BaseDexClassLoader)") ||
        base_loader_class == nullptr) {
        return nullptr;
    }
    if (!env->IsInstanceOf(class_loader, base_loader_class)) {
        env->DeleteLocalRef(base_loader_class);
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=CLASSLOADER_NOT_BASE_DEX");
        return nullptr;
    }
    jfieldID field = env->GetFieldID(
            base_loader_class, "pathList", "Ldalvik/system/DexPathList;");
    env->DeleteLocalRef(base_loader_class);
    if (ClearJniException(env, "BaseDexClassLoader.pathList lookup") || field == nullptr) {
        return nullptr;
    }
    return field;
}

jfieldID GetDexElementsField(JNIEnv *env) {
    jclass dex_path_list_class = env->FindClass("dalvik/system/DexPathList");
    if (ClearJniException(env, "FindClass(DexPathList)") || dex_path_list_class == nullptr) {
        return nullptr;
    }
    jfieldID field = env->GetFieldID(
            dex_path_list_class, "dexElements", "[Ldalvik/system/DexPathList$Element;");
    env->DeleteLocalRef(dex_path_list_class);
    if (ClearJniException(env, "DexPathList.dexElements lookup") || field == nullptr) {
        return nullptr;
    }
    return field;
}

jobject GetPathList(JNIEnv *env, jobject class_loader, jfieldID path_list_field) {
    jobject path_list = env->GetObjectField(class_loader, path_list_field);
    if (ClearJniException(env, "BaseDexClassLoader.pathList") || path_list == nullptr) {
        return nullptr;
    }
    return path_list;
}

jobjectArray GetDexElements(JNIEnv *env, jobject path_list, jfieldID dex_elements_field) {
    auto dex_elements = static_cast<jobjectArray>(
            env->GetObjectField(path_list, dex_elements_field));
    if (ClearJniException(env, "DexPathList.dexElements") || dex_elements == nullptr) {
        return nullptr;
    }
    return dex_elements;
}

jclass GetArrayComponentClass(JNIEnv *env, jobjectArray array) {
    jclass array_class = env->GetObjectClass(array);
    if (ClearJniException(env, "GetObjectClass(array)") || array_class == nullptr) {
        return nullptr;
    }
    jclass class_class = env->FindClass("java/lang/Class");
    if (ClearJniException(env, "FindClass(Class)") || class_class == nullptr) {
        env->DeleteLocalRef(array_class);
        return nullptr;
    }
    jmethodID get_component_type = env->GetMethodID(
            class_class, "getComponentType", "()Ljava/lang/Class;");
    if (ClearJniException(env, "Class.getComponentType lookup") ||
        get_component_type == nullptr) {
        env->DeleteLocalRef(class_class);
        env->DeleteLocalRef(array_class);
        return nullptr;
    }
    auto component_class = static_cast<jclass>(
            env->CallObjectMethod(array_class, get_component_type));
    env->DeleteLocalRef(class_class);
    env->DeleteLocalRef(array_class);
    if (ClearJniException(env, "Class.getComponentType") || component_class == nullptr) {
        return nullptr;
    }
    return component_class;
}

bool CopyObjectArray(JNIEnv *env, jobjectArray src, jsize src_len,
                     jobjectArray dst, jsize dst_offset) {
    for (jsize i = 0; i < src_len; ++i) {
        jobject element = env->GetObjectArrayElement(src, i);
        if (ClearJniException(env, "GetObjectArrayElement")) {
            return false;
        }
        env->SetObjectArrayElement(dst, dst_offset + i, element);
        if (element != nullptr) {
            env->DeleteLocalRef(element);
        }
        if (ClearJniException(env, "SetObjectArrayElement")) {
            return false;
        }
    }
    return true;
}

jobjectArray ConcatDexElements(JNIEnv *env, jobjectArray original_elements,
                               jobjectArray injected_elements) {
    jsize original_len = env->GetArrayLength(original_elements);
    if (ClearJniException(env, "GetArrayLength(originalElements)")) {
        return nullptr;
    }
    jsize injected_len = env->GetArrayLength(injected_elements);
    if (ClearJniException(env, "GetArrayLength(injectedElements)")) {
        return nullptr;
    }
    jclass element_class = GetArrayComponentClass(env, original_elements);
    if (element_class == nullptr) {
        return nullptr;
    }
    jobjectArray combined = env->NewObjectArray(
            original_len + injected_len, element_class, nullptr);
    env->DeleteLocalRef(element_class);
    if (ClearJniException(env, "NewObjectArray(combinedDexElements)") ||
        combined == nullptr) {
        return nullptr;
    }
    if (!CopyObjectArray(env, original_elements, original_len, combined, 0) ||
        !CopyObjectArray(env, injected_elements, injected_len, combined, original_len)) {
        env->DeleteLocalRef(combined);
        return nullptr;
    }
    return combined;
}

bool SetDexElements(JNIEnv *env, jobject class_loader, jobjectArray dex_elements) {
    jfieldID path_list_field = GetPathListField(env, class_loader);
    jfieldID dex_elements_field = GetDexElementsField(env);
    if (path_list_field == nullptr || dex_elements_field == nullptr) {
        return false;
    }

    jobject path_list = GetPathList(env, class_loader, path_list_field);
    if (path_list == nullptr) {
        return false;
    }

    env->SetObjectField(path_list, dex_elements_field, dex_elements);
    env->DeleteLocalRef(path_list);
    return !ClearJniException(env, "Set DexPathList.dexElements");
}

jobject CreatePathClassLoaderWithElements(JNIEnv *env, jobject parent, jobjectArray dex_elements,
                                          const char *patch_failure_code) {
    jobject safe_parent = parent == nullptr ? SystemClassLoader(env) : parent;
    if (safe_parent == nullptr || dex_elements == nullptr) {
        return nullptr;
    }

    jclass path_loader_class = env->FindClass("dalvik/system/PathClassLoader");
    if (ClearJniException(env, "FindClass(PathClassLoader)") || path_loader_class == nullptr) {
        return nullptr;
    }
    jmethodID constructor = env->GetMethodID(
            path_loader_class, "<init>", "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    if (ClearJniException(env, "PathClassLoader.<init> lookup") || constructor == nullptr) {
        env->DeleteLocalRef(path_loader_class);
        return nullptr;
    }

    jstring empty_path = env->NewStringUTF("");
    jobject loader = env->NewObject(path_loader_class, constructor, empty_path, safe_parent);
    env->DeleteLocalRef(empty_path);
    env->DeleteLocalRef(path_loader_class);
    if (ClearJniException(env, "PathClassLoader.<init>") || loader == nullptr) {
        return nullptr;
    }

    if (!SetDexElements(env, loader, dex_elements)) {
        env->DeleteLocalRef(loader);
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=%s", patch_failure_code);
        return nullptr;
    }
    return loader;
}

bool MountDexElements(JNIEnv *env, jobject class_loader, jobjectArray injected_elements,
                      DexElementsPatch &patch) {
    if (class_loader == nullptr || injected_elements == nullptr) {
        return false;
    }
    jfieldID path_list_field = GetPathListField(env, class_loader);
    jfieldID dex_elements_field = GetDexElementsField(env);
    if (path_list_field == nullptr || dex_elements_field == nullptr) {
        return false;
    }

    jobject path_list = GetPathList(env, class_loader, path_list_field);
    jobjectArray original_elements = path_list == nullptr
            ? nullptr
            : GetDexElements(env, path_list, dex_elements_field);
    if (path_list == nullptr || original_elements == nullptr) {
        if (original_elements != nullptr) env->DeleteLocalRef(original_elements);
        if (path_list != nullptr) env->DeleteLocalRef(path_list);
        return false;
    }

    jobjectArray combined_elements = ConcatDexElements(env, original_elements, injected_elements);
    if (combined_elements == nullptr) {
        env->DeleteLocalRef(original_elements);
        env->DeleteLocalRef(path_list);
        return false;
    }

    env->SetObjectField(path_list, dex_elements_field, combined_elements);
    env->DeleteLocalRef(combined_elements);
    if (ClearJniException(env, "Mount injected dexElements")) {
        env->DeleteLocalRef(original_elements);
        env->DeleteLocalRef(path_list);
        return false;
    }

    patch.path_list = path_list;
    patch.dex_elements_field = dex_elements_field;
    patch.original_elements = original_elements;
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_ELEMENTS_MOUNTED");
    return true;
}

bool RestoreDexElements(JNIEnv *env, DexElementsPatch &patch) {
    if (patch.path_list == nullptr || patch.dex_elements_field == nullptr ||
        patch.original_elements == nullptr) {
        return false;
    }
    env->SetObjectField(patch.path_list, patch.dex_elements_field, patch.original_elements);
    bool failed = ClearJniException(env, "Restore DexPathList.dexElements");
    env->DeleteLocalRef(patch.original_elements);
    env->DeleteLocalRef(patch.path_list);
    patch.path_list = nullptr;
    patch.dex_elements_field = nullptr;
    patch.original_elements = nullptr;
    if (failed) {
        return false;
    }
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_ELEMENTS_RESTORED");
    return true;
}

bool PreloadDexClasses(JNIEnv *env, const MappedDex &mapping, jobject class_loader) {
    std::vector<std::string> class_names = CollectDexClassNames(mapping);
    if (class_names.empty()) {
        return false;
    }
    jclass class_class = env->FindClass("java/lang/Class");
    if (ClearJniException(env, "FindClass(Class)") || class_class == nullptr) {
        return false;
    }
    jmethodID for_name = env->GetStaticMethodID(
            class_class, "forName",
            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
    if (ClearJniException(env, "Class.forName lookup") || for_name == nullptr) {
        env->DeleteLocalRef(class_class);
        return false;
    }

    size_t failed = 0;
    for (const std::string &name : class_names) {
        jstring class_name = env->NewStringUTF(name.c_str());
        if (ClearJniException(env, "NewStringUTF(preloadClass)") || class_name == nullptr) {
            ++failed;
            continue;
        }
        jobject clazz = env->CallStaticObjectMethod(
                class_class, for_name, class_name, JNI_FALSE, class_loader);
        env->DeleteLocalRef(class_name);
        if (ClearJniException(env, name.c_str()) || clazz == nullptr) {
            ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_PRELOAD_CLASS_FAILED class=%s", name.c_str());
            ++failed;
            continue;
        }
        env->DeleteLocalRef(clazz);
    }
    env->DeleteLocalRef(class_class);
    if (failed != 0) {
        ZYGISK_FRAMEWORK_LOGW(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_PRELOAD_PARTIAL loaded=%zu total=%zu",
                class_names.size() - failed, class_names.size());
        return false;
    }
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_PRELOAD_OK classes=%zu", class_names.size());
    return true;
}

jclass LoadClass(JNIEnv *env, jobject class_loader, const std::string &dot_name) {
    jclass loader_class = env->FindClass("java/lang/ClassLoader");
    jmethodID load_class = env->GetMethodID(
            loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF(dot_name.c_str());
    auto clazz = static_cast<jclass>(env->CallObjectMethod(class_loader, load_class, name));
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loader_class);
    if (ClearJniException(env, dot_name.c_str()) || clazz == nullptr) {
        return nullptr;
    }
    return clazz;
}

jobjectArray NewStringArray(JNIEnv *env, const std::vector<std::string> &values) {
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray array = env->NewObjectArray(values.size(), string_class, nullptr);
    env->DeleteLocalRef(string_class);
    for (size_t i = 0; i < values.size(); ++i) {
        jstring value = env->NewStringUTF(values[i].c_str());
        env->SetObjectArrayElement(array, static_cast<jsize>(i), value);
        env->DeleteLocalRef(value);
    }
    return array;
}

MappedDex EmbeddedFrameworkDex() {
    return {
            const_cast<uint8_t *>(zygisk_framework_generated::framework_dex::kBytes),
            zygisk_framework_generated::framework_dex::kSize,
    };
}

std::string EmbeddedFrameworkMappingText() {
    return {
            reinterpret_cast<const char *>(zygisk_framework_generated::framework_mapping::kBytes),
            zygisk_framework_generated::framework_mapping::kSize,
    };
}

}  // namespace

bool PreloadDexIntoClassLoader(JNIEnv *env, jobject dex_buffer, jobject class_loader) {
    if (env == nullptr || dex_buffer == nullptr || class_loader == nullptr) {
        return false;
    }
    void *data = env->GetDirectBufferAddress(dex_buffer);
    jlong capacity = env->GetDirectBufferCapacity(dex_buffer);
    if (ClearJniException(env, "DirectByteBuffer access") || data == nullptr || capacity <= 0) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_BUFFER_NOT_DIRECT");
        return false;
    }

    MappedDex mapping{data, static_cast<size_t>(capacity)};
    if (!HasDexMagic(mapping.data, mapping.size) || mapping.size > 32 * 1024 * 1024) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_DEX_INVALID");
        return false;
    }

    std::vector<MappedDex> mappings{mapping};
    jobjectArray injected_elements = MakeInMemoryDexElements(env, mappings);
    DexElementsPatch patch;
    bool mounted = injected_elements != nullptr &&
                   MountDexElements(env, class_loader, injected_elements, patch);
    if (injected_elements != nullptr) {
        env->DeleteLocalRef(injected_elements);
    }
    if (!mounted) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_DEX_ELEMENTS_MOUNT_FAILED");
        return false;
    }

    bool preload_ok = PreloadDexClasses(env, mapping, class_loader);
    bool restore_ok = RestoreDexElements(env, patch);
    if (!restore_ok) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_DEX_ELEMENTS_RESTORE_FAILED");
    }
    return preload_ok && restore_ok;
}

jobject CreateDexClassLoader(JNIEnv *env, jobject dex_buffer, jobject parent) {
    if (env == nullptr || dex_buffer == nullptr) {
        return nullptr;
    }
    jobject safe_parent = parent == nullptr ? SystemClassLoader(env) : parent;
    if (safe_parent == nullptr) {
        return nullptr;
    }

    void *data = env->GetDirectBufferAddress(dex_buffer);
    jlong capacity = env->GetDirectBufferCapacity(dex_buffer);
    if (ClearJniException(env, "DirectByteBuffer access") || data == nullptr || capacity <= 0) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_BUFFER_NOT_DIRECT");
        return nullptr;
    }

    MappedDex mapping{data, static_cast<size_t>(capacity)};
    if (!HasDexMagic(mapping.data, mapping.size) || mapping.size > 32 * 1024 * 1024) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_DEX_INVALID");
        return nullptr;
    }

    std::vector<MappedDex> mappings{mapping};
    jobjectArray dex_elements = MakeInMemoryDexElements(env, mappings);
    if (dex_elements == nullptr) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_DEX_ELEMENTS_CREATE_FAILED");
        return nullptr;
    }

    jclass path_loader_class = env->FindClass("dalvik/system/PathClassLoader");
    if (ClearJniException(env, "FindClass(PathClassLoader)") || path_loader_class == nullptr) {
        env->DeleteLocalRef(dex_elements);
        return nullptr;
    }
    jmethodID constructor = env->GetMethodID(
            path_loader_class, "<init>", "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    if (ClearJniException(env, "PathClassLoader.<init> lookup") || constructor == nullptr) {
        env->DeleteLocalRef(path_loader_class);
        env->DeleteLocalRef(dex_elements);
        return nullptr;
    }

    jstring empty_path = env->NewStringUTF("");
    jobject module_loader = env->NewObject(path_loader_class, constructor, empty_path, safe_parent);
    env->DeleteLocalRef(empty_path);
    env->DeleteLocalRef(path_loader_class);
    if (ClearJniException(env, "PathClassLoader.<init>") || module_loader == nullptr) {
        env->DeleteLocalRef(dex_elements);
        return nullptr;
    }

    if (!SetDexElements(env, module_loader, dex_elements)) {
        env->DeleteLocalRef(dex_elements);
        env->DeleteLocalRef(module_loader);
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_CLASSLOADER_PATCH_FAILED");
        return nullptr;
    }
    env->DeleteLocalRef(dex_elements);

    if (!PreloadDexClasses(env, mapping, module_loader)) {
        env->DeleteLocalRef(module_loader);
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_PRELOAD_FAILED");
        return nullptr;
    }

    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "MODULE_CLASSLOADER_READY");
    return module_loader;
}

bool LoadFrameworkAndBootstrap(JNIEnv *env, ProcessState &state, jobject app_class_loader) {
    MappedDex framework_mapping = EmbeddedFrameworkDex();
    if (!HasDexMagic(framework_mapping.data, framework_mapping.size)) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=FRAMEWORK_DEX_INVALID");
        return false;
    }
    auto class_mapping = ParseMapping(EmbeddedFrameworkMappingText());

    jobject parent = SystemClassLoader(env);
    jobject host_loader = app_class_loader == nullptr ? parent : app_class_loader;
    if (host_loader == nullptr) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=HOST_CLASSLOADER_MISSING");
        return false;
    }

    std::vector<MappedDex> framework_mappings{framework_mapping};
    jobjectArray framework_elements = MakeInMemoryDexElements(env, framework_mappings);
    if (framework_elements == nullptr) {
        ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=FRAMEWORK_DEX_ELEMENTS_CREATE_FAILED");
        return false;
    }
    jobject framework_loader = CreatePathClassLoaderWithElements(
            env, host_loader, framework_elements, "FRAMEWORK_CLASSLOADER_PATCH_FAILED");
    env->DeleteLocalRef(framework_elements);
    if (framework_loader == nullptr) {
        return false;
    }
    PersistentClassLoaders().push_back(env->NewGlobalRef(framework_loader));
    ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "FRAMEWORK_CLASSLOADER_READY");

    bool result = false;
    do {
        if (!PreloadDexClasses(env, framework_mapping, framework_loader)) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=FRAMEWORK_PRELOAD_FAILED");
            break;
        }

        std::string native_bridge_name =
                MapClassName(class_mapping, "com.zygisk.framework.runtime.NativeBridge");
        std::string bootstrap_name =
                MapClassName(class_mapping, "com.zygisk.framework.runtime.RuntimeBootstrap");

        jclass native_bridge = LoadClass(env, framework_loader, native_bridge_name);
        if (!RegisterNativeBridge(env, native_bridge)) {
            break;
        }
        jclass bootstrap_class = LoadClass(env, framework_loader, bootstrap_name);
        if (bootstrap_class == nullptr) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=BOOTSTRAP_CLASS_NOT_FOUND");
            break;
        }

        std::vector<MappedDex> module_mappings;
        std::vector<std::string> module_ids;
        std::vector<std::string> java_init_lists;
        std::vector<std::string> module_props;
        std::vector<std::string> scope_lists;
        for (auto &module : state.modules) {
            MappedDex module_mapping;
            bool module_ok = module.dex_bytes.empty()
                    ? MapDexFd(module.dex_fd, module_mapping)
                    : MapDexBytes(module.dex_bytes, module_mapping);
            if (!module_ok) {
                ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=MODULE_DEX_INVALID id=%s",
                        module.module_id.c_str());
                continue;
            }
            module_mappings.push_back(module_mapping);
            module_ids.push_back(module.module_id);
            java_init_lists.push_back(module.java_init_list);
            module_props.push_back(module.module_prop);
            scope_lists.push_back(module.scope_list);
        }

        jmethodID bootstrap = env->GetStaticMethodID(
                bootstrap_class,
                "bootstrap",
                "(Ljava/lang/ClassLoader;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;"
                "[Ljava/nio/ByteBuffer;[Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V");
        if (bootstrap == nullptr || ClearJniException(env, "RuntimeBootstrap.bootstrap lookup")) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_VERIFY_FAILED code=BOOTSTRAP_METHOD_NOT_FOUND");
            break;
        }
        jstring process = env->NewStringUTF(state.process_name.c_str());
        jstring package = env->NewStringUTF(state.package_name.c_str());
        env->CallStaticVoidMethod(
                bootstrap_class,
                bootstrap,
                host_loader,
                process,
                package,
                NewStringArray(env, module_ids),
                NewByteBufferArray(env, module_mappings),
                NewStringArray(env, java_init_lists),
                NewStringArray(env, module_props),
                NewStringArray(env, scope_lists));
        env->DeleteLocalRef(process);
        env->DeleteLocalRef(package);
        if (ClearJniException(env, "RuntimeBootstrap.bootstrap")) {
            ZYGISK_FRAMEWORK_LOGE(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "MODULE_ENTRY_FAILED code=BOOTSTRAP_EXCEPTION");
            break;
        }
        result = true;
    } while (false);

    if (result) {
        // Host dexElements are left intact because framework.dex lives in its own loader.
        ZYGISK_FRAMEWORK_LOGI(ZYGISK_FRAMEWORK_LOG_TAG_NATIVE, "DEX_ELEMENTS_RESTORED");
    }
    env->DeleteLocalRef(framework_loader);
    return result;
}

}  // namespace zygisk_framework
