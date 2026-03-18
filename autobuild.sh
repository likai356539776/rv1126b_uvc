#!/usr/bin/env bash
set -euo pipefail

# Quick build helper for my_uvc.
# Usage:
#   ./autobuild.sh                    # Release configure + build
#   ./autobuild.sh --debug            # Debug configure + build
#   ./autobuild.sh --release          # Release configure + build
#   ./autobuild.sh --jobs 8           # Build with custom parallel jobs
#   ./autobuild.sh --clean            # clean build directory first, then build
#   ./autobuild.sh --clean --debug    # clean + Debug build
#   ./autobuild.sh -c --release       # clean + Release build
#   ./autobuild.sh --install          # Build then run deploy script
#   ./autobuild.sh --install --adb-serial <serial>

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-rv1126b"
TOOLCHAIN_FILE="${PROJECT_DIR}/cmake/toolchain-rv1126b-buildroot.cmake"
CMAKE_BUILD_TYPE="Release"
DO_CLEAN=0
DO_INSTALL=0
BUILD_JOBS="$(nproc)"
ADB_SERIAL_VALUE=""

print_usage() {
	echo "Usage: $0 [--clean|-c] [--debug|-d | --release|-r] [--jobs|-j N] [--install] [--adb-serial <serial>] [--help|-h]"
	echo "  --clean, -c      Remove build directory before building"
	echo "  --debug, -d      Build with CMAKE_BUILD_TYPE=Debug"
	echo "  --release, -r    Build with CMAKE_BUILD_TYPE=Release (default)"
	echo "  --jobs, -j N     Set parallel build jobs (default: nproc)"
	echo "  --install        Run my_uvc_install_to_device.sh after successful build"
	echo "  --adb-serial     Optional adb serial used by install step"
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
	-j|--jobs)
		[[ $# -ge 2 ]] || { echo "Missing value for $1"; exit 1; }
		BUILD_JOBS="$2"
		shift 2
		;;
	--install)
		DO_INSTALL=1
		shift
		;;
	--adb-serial)
		[[ $# -ge 2 ]] || { echo "Missing value for --adb-serial"; exit 1; }
		ADB_SERIAL_VALUE="$2"
		shift 2
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
echo "[autobuild] Build jobs: ${BUILD_JOBS}"

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
cmake --build "${BUILD_DIR}" -j"${BUILD_JOBS}"

echo "[autobuild] Build finished: ${BUILD_DIR}/my_uvc"

if [[ ${DO_INSTALL} -eq 1 ]]; then
	INSTALL_SCRIPT="${PROJECT_DIR}/my_uvc_install_to_device.sh"
	if [[ ! -x "${INSTALL_SCRIPT}" ]]; then
		chmod +x "${INSTALL_SCRIPT}"
	fi
	echo "[autobuild] Running install script..."
	if [[ -n "${ADB_SERIAL_VALUE}" ]]; then
		ADB_SERIAL="${ADB_SERIAL_VALUE}" "${INSTALL_SCRIPT}"
	else
		"${INSTALL_SCRIPT}"
	fi
fi
