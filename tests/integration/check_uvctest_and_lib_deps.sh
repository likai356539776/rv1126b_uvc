#!/usr/bin/env bash
# P5-T3: uvctest / libmy_uvc.so 动态依赖与 RPATH — 交叉编译后在 SDK 或板端核对。
# 与 check_libmy_uvc_soname_exports.sh 互补：本脚本强调 NEEDED 解析链与 rpath。
#
# 用法:
#   ./check_uvctest_and_lib_deps.sh <build-dir>
# 交叉工具链前缀（可选）:
#   READELF=aarch64-buildroot-linux-gnu-readelf LDD=aarch64-buildroot-linux-gnu-ldd \
#     ./check_uvctest_and_lib_deps.sh build-rv1126b
#
# 验收:
#   - uvctest 的 NEEDED 含 libmy_uvc.so.1
#   - libmy_uvc.so 的 NEEDED / SONAME 与链接配置一致
#   - RPATH：build 目录下未安装的二进制通常为 **BUILD_RPATH**（指向 CMAKE_BINARY_DIR 的绝对路径）；**cmake --install** 后为 **\$ORIGIN/../lib**（见 CMakeLists INSTALL_RPATH）— 二者任一均可，本脚本不强制 ORIGIN
#   - 脚本以 0 退出并打印 check_uvctest_and_lib_deps: ok
set -euo pipefail

BUILD_DIR="${1:?usage: $0 <build-dir e.g. build-rv1126b>}"
READELF="${READELF:-readelf}"
LDD="${LDD:-ldd}"

SO="${BUILD_DIR}/libuvc/libmy_uvc.so.1.0.0"
BIN="${BUILD_DIR}/uvc_main/uvctest"

for f in "${SO}" "${BIN}"; do
	if [[ ! -f "${f}" ]]; then
		echo "missing ${f}"
		exit 1
	fi
done

echo "=== uvctest: NEEDED / RPATH / RUNPATH ==="
${READELF} -d "${BIN}" | grep -E 'NEEDED|RPATH|RUNPATH' || true
rp="$(${READELF} -d "${BIN}" 2>/dev/null | grep -E 'RPATH|RUNPATH' || true)"
if echo "${rp}" | grep -q 'ORIGIN'; then
	echo "=== RPATH: 含 ORIGIN — 典型为 **cmake --install** 后的 INSTALL_RPATH（\$ORIGIN/../lib）==="
elif echo "${rp}" | grep -qE 'RPATH|RUNPATH'; then
	echo "=== RPATH: **构建目录绝对路径**（CMake BUILD_RPATH）— 仅在 build 树跑通时正常；**安装**后期望为 \$ORIGIN/../lib ==="
fi

echo "=== libmy_uvc.so: NEEDED / SONAME ==="
${READELF} -d "${SO}" | grep -E 'NEEDED|SONAME' || true

fail=0
if ! ${READELF} -d "${BIN}" | grep -q 'libmy_uvc.so.1'; then
	echo "FAIL: uvctest should list NEEDED libmy_uvc.so.1"
	fail=1
fi

# Optional: ldd (works when host loader matches binary arch, or with toolchain ldd)
if command -v "${LDD}" >/dev/null 2>&1; then
	echo "=== ldd uvctest (${LDD}) ==="
	if "${LDD}" "${BIN}" 2>&1; then
		:
	else
		echo "(ldd failed — common for foreign arch; rely on readelf NEEDED above)"
	fi
	echo "=== ldd libmy_uvc.so (${LDD}) ==="
	if "${LDD}" "${SO}" 2>&1; then
		:
	else
		echo "(ldd failed — common for foreign arch; rely on readelf NEEDED above)"
	fi
else
	echo "=== (skip) ${LDD} not in PATH ==="
fi

cat <<'NOTE'

板端运行提示:
  - 安装（cmake --install）后: prefix/bin/uvctest、prefix/lib/libmy_uvc.so.1*；uvctest 的 RPATH 一般为 $ORIGIN/../lib，通常无需再设 LD_LIBRARY_PATH。
  - 若把 **build 目录**里未安装的 uvctest 拷到板子（RPATH 仍为主机构建路径）: 请在板端设置 LD_LIBRARY_PATH 指向板上的 libmy_uvc.so 所在目录，或改用 install/staging 产物。
NOTE

if [[ "${fail}" -ne 0 ]]; then
	exit 1
fi

echo "check_uvctest_and_lib_deps: ok"
