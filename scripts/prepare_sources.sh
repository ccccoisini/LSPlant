#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
THIRD_PARTY="$ROOT_DIR/third_party"
LSPLANT_DIR="$THIRD_PARTY/LSPlant"
DOBBY_DIR="$THIRD_PARTY/Dobby"
LSPLANT_PATCH_DIR="$THIRD_PARTY/patches/lsplant"
LSPLANT_REPO="https://github.com/ccccoisini/LSPlant.git"
LSPLANT_COMMIT="d8b5d1dbb664abc606644036822e4bb64547edf6"
APPLY_PATCHES_ONLY=0

while [ $# -gt 0 ]; do
  case "$1" in
    --apply-patches-only)
      APPLY_PATCHES_ONLY=1
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

mkdir -p "$THIRD_PARTY"

is_git_worktree() {
  local dir="$1"
  [ -d "$dir" ] && git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1
}

ensure_repo() {
  local dir="$1"
  local url="$2"
  if is_git_worktree "$dir"; then
    return
  fi
  if [ -e "$dir" ] && [ -n "$(find "$dir" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
    echo "Refusing to clone into non-git non-empty directory: $dir" >&2
    exit 1
  fi
  rm -rf "$dir"
  git clone "$url" "$dir"
}

ensure_clean() {
  local dir="$1"
  if is_git_worktree "$dir" && [ -n "$(git -C "$dir" status --porcelain --ignore-submodules=dirty)" ]; then
    echo "Refusing to update dirty third_party checkout: $dir" >&2
    exit 1
  fi
}

verify_lsplant_commit() {
  local actual_commit
  actual_commit="$(git -C "$LSPLANT_DIR" rev-parse HEAD)"
  if [ "$actual_commit" != "$LSPLANT_COMMIT" ]; then
    echo "Unexpected LSPlant commit: expected $LSPLANT_COMMIT, got $actual_commit" >&2
    echo "Run ./scripts/prepare_sources.sh to restore the pinned source revision." >&2
    exit 1
  fi
}

for_each_patch() {
  local patch_dir="$1"
  local callback="$2"
  if [ ! -d "$patch_dir" ]; then
    return
  fi
  local found=0
  local patch_file
  for patch_file in "$patch_dir"/*.patch; do
    [ -e "$patch_file" ] || continue
    found=1
    "$callback" "$patch_file"
  done
  if [ "$found" -eq 0 ]; then
    return
  fi
}

unapply_lsplant_patch() {
  local patch_file="$1"
  if git -C "$LSPLANT_DIR" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
    git -C "$LSPLANT_DIR" apply --reverse "$patch_file"
    echo "Unapplied LSPlant patch: $(basename "$patch_file")"
  elif git -C "$LSPLANT_DIR" apply --check "$patch_file" >/dev/null 2>&1; then
    return
  else
    echo "Refusing to update LSPlant: patch state is neither clean nor applied: $patch_file" >&2
    exit 1
  fi
}

apply_lsplant_patch() {
  local patch_file="$1"
  if git -C "$LSPLANT_DIR" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
    echo "LSPlant patch already applied: $(basename "$patch_file")"
  elif git -C "$LSPLANT_DIR" apply --check "$patch_file" >/dev/null 2>&1; then
    echo "Applying required LSPlant patch: $(basename "$patch_file")"
    git -C "$LSPLANT_DIR" apply "$patch_file"
    git -C "$LSPLANT_DIR" apply --reverse --check "$patch_file" >/dev/null
    echo "Applied LSPlant patch: $(basename "$patch_file")"
  else
    echo "LSPlant patch is incompatible with commit $LSPLANT_COMMIT: $patch_file" >&2
    exit 1
  fi
}

ensure_repo "$LSPLANT_DIR" "$LSPLANT_REPO"
git -C "$LSPLANT_DIR" remote set-url origin "$LSPLANT_REPO"
if [ "$APPLY_PATCHES_ONLY" -eq 0 ]; then
  for_each_patch "$LSPLANT_PATCH_DIR" unapply_lsplant_patch
  ensure_clean "$LSPLANT_DIR"
  git -C "$LSPLANT_DIR" fetch origin "$LSPLANT_COMMIT"
  git -C "$LSPLANT_DIR" checkout --detach "$LSPLANT_COMMIT"
fi
verify_lsplant_commit
git -C "$LSPLANT_DIR" submodule sync --recursive
git -C "$LSPLANT_DIR" submodule update --init --recursive \
  lsplant/src/main/jni/external/dex_builder \
  docs/doxygen-awesome-css
for_each_patch "$LSPLANT_PATCH_DIR" apply_lsplant_patch

ensure_repo "$DOBBY_DIR" https://github.com/JingMatrix/Dobby.git
if [ "$APPLY_PATCHES_ONLY" -eq 0 ]; then
  ensure_clean "$DOBBY_DIR"
  git -C "$DOBBY_DIR" fetch origin master
  git -C "$DOBBY_DIR" checkout --detach origin/master
fi

LSPLANT_SHA="$(git -C "$LSPLANT_DIR" rev-parse HEAD)"
DOBBY_SHA="$(git -C "$DOBBY_DIR" rev-parse HEAD)"

cat > "$ROOT_DIR/versions.lock" <<EOF_LOCK
lsplant.repo=https://github.com/ccccoisini/LSPlant
lsplant.commit=$LSPLANT_SHA
dobby.repo=https://github.com/JingMatrix/Dobby
dobby.commit=$DOBBY_SHA
xposed.api=82
zygisk.api=4
ndk.version=29.0.14206865
cmake.version=3.31.0
lsplant.patches=$(find "$LSPLANT_PATCH_DIR" -maxdepth 1 -name '*.patch' -exec basename {} \; 2>/dev/null | sort | paste -sd, -)
EOF_LOCK

echo "LSPLANT_COMMIT=$LSPLANT_SHA"
echo "DOBBY_COMMIT=$DOBBY_SHA"
