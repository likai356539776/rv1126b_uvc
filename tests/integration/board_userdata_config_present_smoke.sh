#!/usr/bin/env bash
# P0-T3: 与 **my_uvc_install_to_device.sh** 分文件推送、`CMake install(config/*)`、**config/README_CONFIG.md**
# 一致 — 部署前检查仓库内产物齐全；部署后检查板端 `/userdata` 下分文件 ini 已就位。
#
# 用法（在 my_uvc 仓库根）:
#   ./tests/integration/board_userdata_config_present_smoke.sh         # 宿主机：check
#   ./tests/integration/board_userdata_config_present_smoke.sh check
#   ./tests/integration/board_userdata_config_present_smoke.sh board  # adb 或在本机存在 /userdata 时检查
#
# 环境: ADB_SERIAL（可选，与安装脚本一致）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

need_f()
{
	if [[ ! -f "$1" ]]; then
		echo "board_userdata_config_present_smoke: missing file: $1"
		exit 1
	fi
}

run_check()
{
	# 与 my_uvc_install_to_device.sh「分目录部署」循环、CMake install(FILES … config/ …) 对齐
	for f in libmy_uvc.ini libmy_uvc_pip.ini uvctest.ini my_uvc.ini README_CONFIG.md; do
		need_f "${ROOT}/config/${f}"
	done
	if ! grep -q 'libmy_uvc.ini' "${ROOT}/my_uvc_install_to_device.sh"; then
		echo "board_userdata_config_present_smoke: my_uvc_install_to_device.sh should reference split ini push"
		exit 1
	fi
	echo "board_userdata_config_present_smoke check: ok (repo config/ split files + README_CONFIG.md)"
}

verify_userdata_remote()
{
	local -a adb_cmd=(adb)
	if [[ -n "${ADB_SERIAL:-}" ]]; then
		adb_cmd=(adb -s "${ADB_SERIAL}")
	fi
	for f in libmy_uvc.ini libmy_uvc_pip.ini uvctest.ini my_uvc.ini; do
		if ! "${adb_cmd[@]}" shell "test -f /userdata/${f} && test -r /userdata/${f}" 2>/dev/null; then
			echo "board_userdata_config_present_smoke: FAIL missing or unreadable /userdata/${f} (run my_uvc_install_to_device.sh?)"
			exit 1
		fi
	done
	echo "board_userdata_config_present_smoke board: ok (adb: split configs under /userdata)"
}

verify_userdata_local()
{
	for f in libmy_uvc.ini libmy_uvc_pip.ini uvctest.ini my_uvc.ini; do
		if [[ ! -f "/userdata/${f}" || ! -r "/userdata/${f}" ]]; then
			echo "board_userdata_config_present_smoke: FAIL missing or unreadable /userdata/${f}"
			exit 1
		fi
	done
	echo "board_userdata_config_present_smoke board: ok (local /userdata split configs)"
}

run_board()
{
	if [[ -f /userdata/libmy_uvc.ini ]]; then
		verify_userdata_local
		return 0
	fi
	if ! command -v adb >/dev/null 2>&1; then
		echo "board_userdata_config_present_smoke: no adb and not on a system with /userdata — skip board"
		exit 0
	fi
	if ! adb devices 2>/dev/null | grep -qE '^\S+\s+device$'; then
		echo "board_userdata_config_present_smoke: no adb device — skip board"
		exit 0
	fi
	verify_userdata_remote
}

case "${1:-check}" in
check) run_check ;;
board) run_board ;;
-h | --help)
	echo "Usage: $0 [check|board]"
	exit 0
	;;
*)
	echo "Usage: $0 [check|board]"
	exit 1
	;;
esac
