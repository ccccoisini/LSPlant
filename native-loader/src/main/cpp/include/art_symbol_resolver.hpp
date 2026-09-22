#pragma once

#include <string_view>

namespace zygisk_framework {

/**
 * 解析当前进程 libart.so 中的完整符号名。
 *
 * @param symbol_name 需要解析的符号名称
 * @return 成功时返回符号地址，失败时返回 nullptr
 */
void *ResolveArtSymbol(std::string_view symbol_name);

/**
 * 解析当前进程 libart.so 中第一个匹配前缀的符号。
 *
 * @param symbol_prefix 需要匹配的符号前缀
 * @return 成功时返回符号地址，失败时返回 nullptr
 */
void *ResolveArtSymbolPrefix(std::string_view symbol_prefix);

}  // namespace zygisk_framework
