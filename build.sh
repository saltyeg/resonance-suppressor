#!/usr/bin/env bash
# Configure + build on Linux / macOS (run inside `nix-shell`, or with cmake/ninja on PATH).
#   ./build.sh                 Release build
#   ./build.sh --debug         Debug build
#   ./build.sh --target rsui   build one target
#   ./build.sh -DFOO=ON        -D... options go to the configure step, anything else to `cmake --build`
set -euo pipefail

usage() {
  sed -n '2,6p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="Release"
TARGET=""
CONFIGURE_ARGS=()
BUILD_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    -Debug|--debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    -Target|--target)
      if [[ $# -lt 2 || -z "$2" ]]; then
        echo "error: $1 needs a target name" >&2
        usage >&2
        exit 2
      fi
      TARGET="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -D*)
      CONFIGURE_ARGS+=("$1")
      shift
      ;;
    *)
      BUILD_ARGS+=("$1")
      shift
      ;;
  esac
done

# Same layout as build.ps1 (build/Release, build/Debug).
BUILD_DIR="${ROOT_DIR}/build/${BUILD_TYPE}"

# The ${arr[@]+...} form keeps empty arrays safe under `set -u` on macOS's bash 3.2.
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DRS_VST3_DIR="${BUILD_DIR}" \
  "${CONFIGURE_ARGS[@]+"${CONFIGURE_ARGS[@]}"}"

if [[ -n "${TARGET}" ]]; then
  cmake --build "${BUILD_DIR}" --target "${TARGET}" "${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}"
else
  cmake --build "${BUILD_DIR}" "${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}"
fi

# Hosts on macOS refuse unsigned bundles; ad-hoc sign the copied plugin.
BUNDLE="${BUILD_DIR}/resonance-suppressor.vst3"
if [[ "$(uname -s)" == "Darwin" && -d "${BUNDLE}" ]]; then
  if [[ ! -x /usr/bin/codesign ]]; then
    echo "warning: /usr/bin/codesign not found; ${BUNDLE} is unsigned and may not load" >&2
  elif ! codesign_out="$(/usr/bin/codesign -f -s - "${BUNDLE}" 2>&1)"; then
    echo "warning: ad-hoc codesign failed; the plugin may not load:" >&2
    echo "${codesign_out}" >&2
  fi
fi
