#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
toolchain="$repo_root/cmake/toolchains/GCC.cmake"

if [[ ! -f "$toolchain" ]]; then
  echo "GCC compatibility toolchain not found: $toolchain" >&2
  exit 1
fi

resolve_miracle() {
  if [[ -n "${MIRACLE_SOURCE_DIR:-}" ]]; then
    miracle_source="$(cd "$MIRACLE_SOURCE_DIR" && pwd)"
    if git -C "$miracle_source" rev-parse --verify HEAD >/dev/null 2>&1; then
      miracle_sha="$(git -C "$miracle_source" rev-parse HEAD)"
    else
      miracle_sha="local-source"
    fi
    return
  fi

  local repository="${MIRACLE_GCC_REPOSITORY:-https://github.com/spwn02/Miracle.git}"
  miracle_sha="$(git ls-remote "$repository" refs/heads/gcc | awk 'NR == 1 { print $1 }')"
  if [[ ! "$miracle_sha" =~ ^[0-9a-f]{40}$ ]]; then
    echo "Could not resolve Miracle:gcc from $repository" >&2
    exit 1
  fi

  miracle_source="${RUNNER_TEMP:-$repo_root/build}/Miracle-gcc-$miracle_sha"
  rm -rf "$miracle_source"
  git clone --no-checkout --filter=blob:none "$repository" "$miracle_source"
  git -C "$miracle_source" fetch --depth 1 origin "$miracle_sha"
  git -C "$miracle_source" checkout --detach "$miracle_sha"
}

resolve_miracle

printf 'Switch GCC dependency: Miracle %s (%s)\n' "$miracle_sha" "$miracle_source"

alignment_file="$repo_root/build/gcc-dependency-alignment.env"
mkdir -p "$(dirname "$alignment_file")"
printf 'MIRACLE_GCC_SHA=%s\n' "$miracle_sha" > "$alignment_file"

cd "$repo_root"

echo "== GCC compatibility toolchain =="
g++ --version
cmake --version
ninja --version

echo "== Switch self-tests + example =="
cmake --preset tests --fresh \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DFETCHCONTENT_SOURCE_DIR_MIRACLE="$miracle_source"
cmake --build --preset tests
ctest --preset tests
"$repo_root/build/tests/examples/SwitchQuickstart"

echo "== add_subdirectory consumer =="
cmake \
  -S tests/consumer/add-subdirectory \
  -B build/gcc-consumer-add \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DFETCHCONTENT_SOURCE_DIR_MIRACLE="$miracle_source" \
  -DSWITCH_SOURCE_DIR="$repo_root"
cmake --build build/gcc-consumer-add
"$repo_root/build/gcc-consumer-add/switch_consumer"

echo "== FetchContent consumer =="
cmake \
  -S tests/consumer/fetch-content \
  -B build/gcc-consumer-fetch \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DFETCHCONTENT_SOURCE_DIR_MIRACLE="$miracle_source" \
  -DFETCHCONTENT_SOURCE_DIR_SWITCH="$repo_root"
cmake --build build/gcc-consumer-fetch
"$repo_root/build/gcc-consumer-fetch/switch_consumer"

echo "== Release + installed-package consumer =="
cmake --preset release --fresh \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DFETCHCONTENT_SOURCE_DIR_MIRACLE="$miracle_source"
cmake --build --preset release

install_prefix="$repo_root/build/gcc-install"
rm -rf "$install_prefix"
cmake --install build/release --prefix "$install_prefix"

cmake \
  -S tests/consumer/find-package \
  -B build/gcc-consumer-package \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DCMAKE_PREFIX_PATH="$install_prefix"
cmake --build build/gcc-consumer-package
"$repo_root/build/gcc-consumer-package/switch_consumer"
