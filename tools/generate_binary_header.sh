#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "Usage: $0 <input-file> <output-header> <namespace> <array-name>" >&2
  exit 2
fi

input_file="$1"
output_header="$2"
namespace_name="$3"
array_name="$4"

if [[ ! -f "$input_file" ]]; then
  echo "Input file not found: $input_file" >&2
  exit 1
fi

input_size="$(wc -c < "$input_file" | tr -d '[:space:]')"
if [[ -z "$input_size" || "$input_size" == "0" ]]; then
  echo "Input file is empty: $input_file" >&2
  exit 1
fi

mkdir -p "$(dirname "$output_header")"
tmp_header="$(mktemp)"

{
  cat <<EOF
#pragma once

// Generated from $(basename "$input_file"). Do not edit manually.

#include <cstddef>
#include <cstdint>

namespace ${namespace_name} {

inline constexpr std::uint8_t ${array_name}[] = {
EOF
  od -An -tx1 -v "$input_file" | awk '
    {
      for (i = 1; i <= NF; ++i) {
        if (count > 0) {
          if (count % 12 == 0) {
            printf ",\n    "
          } else {
            printf ", "
          }
        } else {
          printf "    "
        }
        printf "0x%s", $i
        ++count
      }
    }
    END {
      printf "\n"
    }
  '
  cat <<EOF
};

inline constexpr std::size_t kSize = sizeof(${array_name});
static_assert(kSize == ${input_size}, "Generated binary size mismatch");

}  // namespace ${namespace_name}
EOF
} > "$tmp_header"

mv "$tmp_header" "$output_header"
chmod 0644 "$output_header"

echo "Generated binary header:"
echo "  input:  $input_file"
echo "  size:   $input_size bytes"
echo "  header: $output_header"
