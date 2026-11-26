#!/usr/bin/env bash
# Build a private patched Lean shared library; do not modify the elan toolchain.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" == --help ]]; then
    cat <<'HELP'
Usage: [environment variables] ./patch-lean.sh

Required:
  LEAN_SOURCE          Matching Lean source checkout/extracted source root
  MIMALLOC_SOURCE      mimalloc 2.2.7 source root
  LEAN_INCLUDE_DIRS    Colon-separated matching libc++/dependency header dirs
                      Include generated libc++ configuration headers.
Optional:
  LEAN_VERSION         4.34.0, v4.34.0, or leanprover/lean4:v4.34.0
                      Defaults to this repository's lean-toolchain.
  PYLE_LEAN_RUNTIME_DIR  Output directory (default: build/lean-runtime-VERSION)
  PYLE_PATCH_TASKS     1 enables experimental completed-task patch (default: 0)
  PYLE_BUILD_JOBS      Parallel compiler jobs (default: 8)
  PYTHON              Python interpreter (default: python3)

Linux only. Requires elan with the selected toolchain already installed, patch,
ar, matching C++ headers, and GMP/libuv/OpenSSL development libraries.
Sources and headers must already exist; this script does not download them.
After success, source OUTPUT/pyle-runtime.env before installing Pyle with uv.
HELP
    exit 0
fi
[[ $# == 0 ]] || { echo 'Use --help for usage.' >&2; exit 2; }
fail() { echo "patch-lean: $*" >&2; exit 1; }
[[ "$(uname -s)" == Linux ]] || fail 'Only Linux is supported by this runtime builder.'
version="${LEAN_VERSION:-$(cat "$script_dir/lean-toolchain")}"
version="${version##*:}"
version="${version#v}"
[[ "$version" == 4.34.0 ]] || fail "Patches currently support Lean 4.34.0 only, not $version."
[[ "${PYLE_PATCH_TASKS:-0}" =~ ^[01]$ ]] || fail 'PYLE_PATCH_TASKS must be 0 or 1.'
[[ "${PYLE_BUILD_JOBS:-8}" =~ ^[1-9][0-9]*$ ]] || fail 'PYLE_BUILD_JOBS must be a positive integer.'
for cmd in elan patch ar "${PYTHON:-python3}"; do
    command -v "$cmd" >/dev/null || fail "Missing required command: $cmd"
done
: "${LEAN_SOURCE:?Set LEAN_SOURCE to matching Lean sources; see --help}"
: "${MIMALLOC_SOURCE:?Set MIMALLOC_SOURCE to mimalloc 2.2.7 sources; see --help}"
: "${LEAN_INCLUDE_DIRS:?Set LEAN_INCLUDE_DIRS to matching header directories; see --help}"
[[ -f "$LEAN_SOURCE/src/library/ir_interpreter.cpp" ]] || fail 'Invalid LEAN_SOURCE.'
[[ -f "$MIMALLOC_SOURCE/src/static.c" ]] || fail 'Invalid MIMALLOC_SOURCE.'
toolchain="leanprover/lean4:v$version"
lean_executable="$(ELAN_TOOLCHAIN="$toolchain" elan which lean)"
export PATH="$(dirname -- "$lean_executable"):$PATH"
# Prevent a surrounding project's Lake environment from selecting other modules.
unset LEAN_SYSROOT LEAN_PATH
output="${PYLE_LEAN_RUNTIME_DIR:-$script_dir/build/lean-runtime-$version}"
mkdir -p -- "$output"
output="$(cd -- "$output" && pwd)"
args=(--source "$LEAN_SOURCE" --mimalloc-source "$MIMALLOC_SOURCE"
      --output "$output" --jobs "${PYLE_BUILD_JOBS:-8}")
IFS=: read -r -a includes <<< "$LEAN_INCLUDE_DIRS"
for include in "${includes[@]}"; do
    [[ -d "$include" ]] || fail "Header directory does not exist: $include"
    args+=(--include "$include")
done
if [[ "${PYLE_PATCH_TASKS:-0}" == 1 ]]; then
    args+=(--completed-task-fast-path)
fi
"${PYTHON:-python3}" "$script_dir/scripts/build_lean_runtime.py" "${args[@]}"
printf 'export PYLE_LEAN_RUNTIME_DIR=%q\n' "$output" > "$output/pyle-runtime.env"
printf '\nPatched runtime built. Next, in your current shell:\n  source %q\n' "$output/pyle-runtime.env"
printf '  uv add --no-cache git+https://github.com/offendo/pyle\n'
