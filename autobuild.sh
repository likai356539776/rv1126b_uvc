#!/usr/bin/env bash
set -euo pipefail

# Quick build helper for my_uvc.
# Usage:
#   ./autobuild.sh                    # Release configure + build
#   ./autobuild.sh --debug            # Debug configure + build
#   ./autobuild.sh --release          # Release configure + build
#   ./autobuild.sh --clean            # clean build directory first, then build
#   ./autobuild.sh --clean --debug    # clean + Debug build
#   ./autobuild.sh -c --release       # clean + Release build

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-rv1126b"
TOOLCHAIN_FILE="${PROJECT_DIR}/cmake/toolchain-rv1126b-buildroot.cmake"
CMAKE_BUILD_TYPE="Release"
DO_CLEAN=0

print_usage() {
	echo "Usage: $0 [--clean|-c] [--debug|-d | --release|-r] [--help|-h]"
	echo "  --clean, -c      Remove build directory before building"
	echo "  --debug, -d      Build with CMAKE_BUILD_TYPE=Debug"
	echo "  --release, -r    Build with CMAKE_BUILD_TYPE=Release (default)"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	-c|--clean)
		DO_CLEAN=1
		shift
		;;
	-d|--debug)
		CMAKE_BUILD_TYPE="Debug"
		shift
		;;
	-r|--release)
		CMAKE_BUILD_TYPE="Release"
		shift
		;;
	-h|--help)
		print_usage
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		print_usage
		exit 1
		;;
	esac
done

if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
	echo "[autobuild] Toolchain file not found: ${TOOLCHAIN_FILE}"
	exit 1
fi

echo "[autobuild] Build type: ${CMAKE_BUILD_TYPE}"

if [[ ${DO_CLEAN} -eq 1 ]]; then
	echo "[autobuild] Cleaning ${BUILD_DIR}"
	rm -rf "${BUILD_DIR}"
fi

echo "[autobuild] Configuring project..."
cmake -S "${PROJECT_DIR}" \
	-B "${BUILD_DIR}" \
	-DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
	-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"

echo "[autobuild] Building project..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[autobuild] Build finished: ${BUILD_DIR}/my_uvc"
