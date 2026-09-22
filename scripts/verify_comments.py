#!/usr/bin/env python3
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKIP_PARTS = {"third_party", "build", ".gradle", ".tmp", "dist"}
SKIP_FILES = {"zygisk.hpp"}
JAVA_TYPE = re.compile(r"^\s*public\s+(?:final\s+|abstract\s+)?(?:class|interface|enum|record)\s+")
JAVA_MEMBER = re.compile(r"^\s*public\s+[\w<>\[\].?,\s]+\s+\w+\s*\(")
CPP_HEADER_DECL = re.compile(r"^\s*(?:[\w:<>*&]+\s+)+\w+\s*\([^;{]*\)\s*;")
CPP_EXPORT = re.compile(r"^\s*(?:JNIEXPORT|extern\s+\"C\"|REGISTER_ZYGISK_|static\s+void\s+zygisk_framework_companion_entry)")
CHINESE = re.compile(r"[\u4e00-\u9fff]")


def skipped(path: pathlib.Path) -> bool:
    return path.name in SKIP_FILES or any(part in SKIP_PARTS for part in path.parts)


def has_doc(lines, index):
    start = max(0, index - 20)
    block = "\n".join(lines[start:index])
    return "/**" in block and "*/" in block and CHINESE.search(block)


def check_file(path: pathlib.Path):
    missing = []
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    in_public_type = False
    brace_depth = 0
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("//") or " new " in line:
            continue
        if path.suffix == ".java":
            if JAVA_TYPE.search(line):
                in_public_type = True
                brace_depth = line.count("{") - line.count("}")
                if not has_doc(lines, index):
                    missing.append((path, index + 1, stripped))
                continue
            if in_public_type:
                brace_depth += line.count("{") - line.count("}")
                if brace_depth <= 0:
                    in_public_type = False
                if JAVA_MEMBER.search(line) and not has_doc(lines, index):
                    missing.append((path, index + 1, stripped))
            continue
        if path.suffix == ".hpp":
            should_check = CPP_HEADER_DECL.search(line)
        else:
            should_check = CPP_EXPORT.search(line)
        if should_check and not has_doc(lines, index):
            missing.append((path, index + 1, stripped))
    return missing


def main():
    missing = []
    for suffix in ("*.java", "*.cpp", "*.hpp"):
        for path in ROOT.rglob(suffix):
            if not skipped(path):
                missing.extend(check_file(path))
    for path, line, text in missing:
        print(f"{path.relative_to(ROOT)}:{line}: missing Chinese /** */ doc before: {text}")
    if missing:
        print(f"PUBLIC_DOCUMENTATION_CHECK: FAIL missing={len(missing)}")
        return 1
    print("PUBLIC_DOCUMENTATION_CHECK: PASS")
    print("missing=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
