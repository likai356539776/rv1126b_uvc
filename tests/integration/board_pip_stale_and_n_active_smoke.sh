#!/usr/bin/env bash
# P3-I5: PiP 断流 / 网格 n_active — 板端短时跑 uvctest + 人工或日志核对。
#
# 用法:
#   ./board_pip_stale_and_n_active_smoke.sh check   # 打印配置要点与示例命令
#   ./board_pip_stale_and_n_active_smoke.sh smoke   # 与 P3-I2 类似短时跑；可选验证 pip grid 日志行
#
# 环境变量:
#   UVCTEST, CONFIG, MJPEG_SOURCE, PIP_OVERLAY, SMOKE_SEC（默认 8）
#   CONFIG 默认 /userdata（目录）：uvctest 合并 libmy_uvc.ini → libmy_uvc_pip.ini → uvctest.ini。
#   单文件（如 select_profile 下发的 profile）可 export CONFIG=/userdata/profile.ini。
#   SMOKE_SIZE（默认 1920x1080）— 勿用名 SIZE：login/getty 常设 SIZE=终端行数，会搞乱 --size（日志 width=19205）
#   EXPECT_N_ACTIVE — 非空时要求日志中出现 "pip grid NV12 test n_active=<n>"（须在 ini 配好 pip_tile 与 nv12 路径）
#
# 前置: my_uvc_usb_config.sh 与单路/多路配置一致；overlay 与 tile 测试文件可读。
#
# 若板端仍报 “log: unbound variable”：说明运行的不是本文件（常见为 /usr/bin 下旧脚本）。
# my_uvc_install_to_device.sh 不推送本脚本，须手动同步仓库 tests/integration 本文件后再 chmod +x。
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"
MJPEG_SOURCE="${MJPEG_SOURCE:-/userdata/mjpeg_frames_dir}"
PIP_OVERLAY="${PIP_OVERLAY:-/userdata/mjpeg_overlay}"
SMOKE_SIZE="${SMOKE_SIZE:-1920x1080}"
SMOKE_SEC="${SMOKE_SEC:-8}"
EXPECT_N_ACTIVE="${EXPECT_N_ACTIVE:-}"

# smoke 临时日志须全局：EXIT trap 在 run_smoke 返回后执行，local log 会在 set -u 下触发 “log: unbound variable”。
_PIP_STALE_TMPLOG=""
_pip_stale_tmp_cleanup() {
	[[ -n "${_PIP_STALE_TMPLOG:-}" ]] && rm -f -- "${_PIP_STALE_TMPLOG}"
}

usage()
{
	cat <<EOF
Usage: $0 check|smoke
  check   Stale timeout + n_active manual / ini notes (see TEST_CHECKLIST 4.0.2).
  smoke   timeout uvctest MJPEG+PiP; optional EXPECT_N_ACTIVE vs log grep.
EOF
}

need_cmd() { command -v "$1" >/dev/null 2>&1 || {
	echo "missing: $1"
	exit 1
}; }

run_check()
{
	need_cmd "${UVCTEST}"
	cat <<EOF
=== P3-I5: pip_overlay_stale_timeout_ms + grid n_active ===

1) 断流（主讲人 / 单路 NV12 API）
   - 库行为见 pip_helper_composite_mjpeg_ex：presenter_nv12_updated / tile_nv12_updated 与
     pip_overlay_stale_timeout_ms（create：-1 缺省 5000 ms，0 仅冻结，>0 自定义）。
   - uvctest 默认单文件 overlay 路径每帧刷新主讲人时间戳，板上「超时露背图」需集成方按 API 提交「无新帧」。
   - 纯函数策略单测：tests/unit/test_pip_stale_timeout_policy.cpp

2) n_active（网格槽位数与素材路数）
   - 配置：默认 -c /userdata（三文件）；[libmy_uvc_pip] pip_tile_n_tiles = N；[uvctest] pip_tile_test_nv12_paths = 逗号分隔 NV12 文件。
   - n_active = min(路径段数, N)；日志行：pip grid NV12 test n_active=<n>
   - 人工：路径数 < N 时仅前若干格有画面，余格为背图。

Example (ini 已含 [uvctest] pip_tile_test_nv12_paths 与 pip_tile_n_tiles 时，校验 n_active 日志):
  EXPECT_N_ACTIVE=2 $0 smoke

Base PiP command (no tiles):
  ${UVCTEST} -c ${CONFIG} --codec mjpeg --file ${MJPEG_SOURCE} --size ${SMOKE_SIZE} \\
    --pip-enable 1 --pip-overlay ${PIP_OVERLAY} \\
    --pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85
EOF
}

run_smoke()
{
	need_cmd "${UVCTEST}"
	command -v timeout >/dev/null 2>&1 || {
		echo "smoke needs 'timeout'"
		exit 1
	}
	if [[ ! -e "${CONFIG}" || ! -e "${MJPEG_SOURCE}" || ! -e "${PIP_OVERLAY}" ]]; then
		echo "missing one of CONFIG / MJPEG_SOURCE / PIP_OVERLAY (set env or create paths):"
		for _k in CONFIG MJPEG_SOURCE PIP_OVERLAY; do
			_v="${!_k}"
			if [[ -e "${_v}" ]]; then
				echo "  ok   ${_k}=${_v}"
			else
				echo "  MISS ${_k}=${_v}"
			fi
		done
		echo "hint: CONFIG can be a dir (e.g. /userdata with libmy_uvc*.ini) or a single file (e.g. CONFIG=/userdata/profile.ini)"
		exit 1
	fi

	_PIP_STALE_TMPLOG="$(mktemp)"
	trap '_pip_stale_tmp_cleanup' EXIT

	# 串口/login 常 export SIZE=终端行数；子进程继承后若被误用会污染行为。此处不依赖该变量。
	unset -v SIZE 2>/dev/null || true

	set +e
	set -o pipefail
	timeout "${SMOKE_SEC}" "${UVCTEST}" -c "${CONFIG}" --codec mjpeg --file "${MJPEG_SOURCE}" --size "${SMOKE_SIZE}" \
		--pip-enable 1 --pip-overlay "${PIP_OVERLAY}" \
		--pip-x 20 --pip-y 20 --pip-w 640 --pip-h 480 --pip-jpeg-quality 85 \
		2>&1 | tee "${_PIP_STALE_TMPLOG}"
	local ec=${PIPESTATUS[0]}
	set +o pipefail
	set -e
	if [[ "${ec}" -ne 124 ]] && [[ "${ec}" -ne 0 ]]; then
		echo "unexpected exit ${ec}"
		exit 1
	fi

	if [[ -n "${EXPECT_N_ACTIVE}" ]]; then
		if ! grep -E "pip grid NV12 test n_active=${EXPECT_N_ACTIVE}\\b" "${_PIP_STALE_TMPLOG}" >/dev/null; then
			echo "FAIL: expected log line pip grid NV12 test n_active=${EXPECT_N_ACTIVE}"
			exit 1
		fi
	fi

	echo "board_pip_stale_and_n_active_smoke: ok"
}

case "${1:-check}" in
	check) run_check ;;
	smoke) run_smoke ;;
	-h|--help|help) usage; exit 0 ;;
	*) usage; exit 1 ;;
esac
