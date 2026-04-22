#!/usr/bin/env bash
# P4-T2（可选）：与 HANDOVER.md / docs/README.md / config/README_CONFIG.md 所述部署流程一致 —
# 仓库内脚本与分文件配置存在；可选 adb 检查板端已部署路径。
#
# 用法:
#   ./doc_deploy_walkthrough_smoke.sh check   # 宿主机：断言本地路径（默认）
#   ./doc_deploy_walkthrough_smoke.sh board   # 若 adb 有设备：检查 /usr/bin/uvctest、配置
#
# 环境: ADB_SERIAL（可选）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

need_f()
{
	if [[ ! -f "$1" ]]; then
		echo "missing file: $1"
		exit 1
	fi
}

need_x()
{
	if [[ ! -f "$1" ]]; then
		echo "missing: $1"
		exit 1
	fi
	chmod +x "$1" 2>/dev/null || true
}

run_check()
{
	need_x "${ROOT}/my_uvc_install_to_device.sh"
	need_x "${ROOT}/scripts/my_uvc_usb_config.sh"
	need_x "${ROOT}/scripts/select_profile.sh"
	for f in libmy_uvc.ini libmy_uvc_pip.ini uvctest.ini README_CONFIG.md; do
		need_f "${ROOT}/config/${f}"
	done
	need_f "${ROOT}/docs/README.md"
	need_f "${ROOT}/HANDOVER.md"
	need_f "${ROOT}/CHANGELOG.md"
	need_f "${ROOT}/include/my_uvc/my_uvc.h"
	need_f "${ROOT}/config/README_CONFIG.md"
	if ! grep -q 'my_uvc_load_ini_section_only' "${ROOT}/include/my_uvc/my_uvc.h"; then
		echo "missing C API: my_uvc_load_ini_section_only in include/my_uvc/my_uvc.h"
		exit 1
	fi
	echo "doc_deploy_walkthrough_smoke check: ok (install script, split configs, docs, my_uvc.h C API)"
}

run_board()
{
	command -v adb >/dev/null 2>&1 || {
		echo "adb not in PATH — skip board"
		exit 0
	}
	if ! adb devices 2>/dev/null | grep -qE '^\S+\s+device$'; then
		echo "no adb device — skip board"
		exit 0
	fi
	local adb_cmd=(adb)
	if [[ -n "${ADB_SERIAL:-}" ]]; then
		adb_cmd=(adb -s "${ADB_SERIAL}")
	fi
	"${adb_cmd[@]}" shell "test -x /usr/bin/uvctest" 2>/dev/null || {
		echo "FAIL: /usr/bin/uvctest missing or not executable"
		exit 1
	}
	echo "board: /usr/bin/uvctest ok"
	if "${adb_cmd[@]}" shell "test -e /usr/bin/my_uvc" 2>/dev/null; then
		echo "WARN: /usr/bin/my_uvc still present (legacy compat name removed; use uvctest)"
	fi

	"${adb_cmd[@]}" shell "test -f /usr/lib/libmy_uvc.so.1.0.0 || test -f /usr/lib/libmy_uvc.so.1" 2>/dev/null || {
		echo "WARN: libmy_uvc.so not under /usr/lib (install may be incomplete)"
	}
	for f in libmy_uvc.ini libmy_uvc_pip.ini uvctest.ini; do
		if ! "${adb_cmd[@]}" shell "test -f /userdata/${f}" 2>/dev/null; then
			echo "WARN: /userdata/${f} missing — run my_uvc_install_to_device.sh to push split configs"
		fi
	done
	echo "doc_deploy_walkthrough_smoke board: ok (required: uvctest)"
}

case "${1:-check}" in
	check) run_check ;;
	board) run_board ;;
	-h|--help)
		echo "Usage: $0 check|board"
		exit 0
		;;
	*)
		echo "Usage: $0 check|board"
		exit 1
		;;
esac
