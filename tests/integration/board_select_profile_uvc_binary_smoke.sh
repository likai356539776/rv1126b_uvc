#!/usr/bin/env bash
# P4-T3: select_profile 建议命令仅以 uvctest 为应用入口（不再提供 my_uvc 兼容名）。
#
# 用法:
#   ./board_select_profile_uvc_binary_smoke.sh check   # 宿主机：跑 select_profile 1，断言输出含 uvctest -c
#   ./board_select_profile_uvc_binary_smoke.sh board    # 若 adb 有设备：检查 /usr/bin/uvctest
#
# 环境: ADB_SERIAL（可选）
set -euo pipefail

MY_UVC_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SELECT="${MY_UVC_ROOT}/scripts/select_profile.sh"

run_check()
{
	[[ -f "${SELECT}" ]] || {
		echo "missing ${SELECT}"
		exit 1
	}
	if [[ ! -x "${SELECT}" ]]; then
		chmod +x "${SELECT}" || true
	fi
	local out
	out="$(bash "${SELECT}" 1 2>&1)" || {
		echo "select_profile.sh failed"
		exit 1
	}
	echo "${out}" | grep -q 'uvctest -c' || {
		echo "FAIL: suggested commands should include uvctest -c"
		exit 1
	}
	if echo "${out}" | grep -qE '[[:space:]]my_uvc[[:space:]]+-c'; then
		echo "FAIL: suggested commands should not use my_uvc as executable (use uvctest only)"
		exit 1
	fi
	echo "board_select_profile_uvc_binary_smoke check: ok"
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
	if ! "${adb_cmd[@]}" shell "test -x /usr/bin/uvctest" 2>/dev/null; then
		echo "board: /usr/bin/uvctest missing — skip"
		exit 0
	fi
	echo "board: /usr/bin/uvctest present (ok)"
	if "${adb_cmd[@]}" shell "test -e /usr/bin/my_uvc" 2>/dev/null; then
		echo "WARN: /usr/bin/my_uvc still present (legacy; entrypoint is uvctest)"
	fi
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
