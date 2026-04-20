#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------------------------------
# probe_uvc_node_mapping.sh
# 作用:
#   在主机侧自动探测 /dev/videoX 与板端 my_uvc channel 的映射关系。
#
# 原理:
#   1) 给板端日志写入唯一 marker
#   2) 主机对指定 video 节点抓取少量帧(触发 UVC STREAMON)
#   3) 从 marker 区间日志中提取:
#      - channel N stream ON (video_id=...)
#      - UVC_EVENT_STREAMON ... stream_intf=...
# ------------------------------------------------------------------

ADB_SERIAL=""
BOARD_LOG="/userdata/my_uvc.log"
VIDEO_GLOB="/dev/video*"
WIDTH=1920
HEIGHT=1080
PIXEL_FORMAT="H264"
STREAM_COUNT=1
TIMEOUT_SEC=3
ONLY_DEVICES=""
NO_ADB=0

usage() {
	echo "Usage: $0 [options]"
	echo ""
	echo "Options:"
	echo "  --adb-serial <serial>     adb 设备序列号"
	echo "  --board-log <path>        板端日志路径 (default: /userdata/my_uvc.log)"
	echo "  --size <WxH>              采样分辨率 (default: 1920x1080)"
	echo "  --pixfmt <fmt>            V4L2 像素格式 (default: H264)"
	echo "  --stream-count <N>        每个节点抓取帧数 (default: 1)"
	echo "  --timeout <sec>           每个节点超时秒数 (default: 3)"
	echo "  --devices <list>          仅探测指定节点，逗号分隔，如 /dev/video2,/dev/video12"
	echo "  --no-adb                 无 adb 模式（仅主机探测，按时间戳与串口日志对齐）"
	echo "  --help                    显示帮助"
	echo ""
	echo "Example:"
	echo "  $0 --size 1920x1080 --pixfmt H264"
}

adb_exec() {
	if [[ -n "${ADB_SERIAL}" ]]; then
		adb -s "${ADB_SERIAL}" "$@"
	else
		adb "$@"
	fi
}

require_cmd() {
	command -v "$1" >/dev/null 2>&1 || {
		echo "[probe] missing command: $1"
		exit 1
	}
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--adb-serial)
		[[ $# -ge 2 ]] || { echo "Missing value for --adb-serial"; exit 1; }
		ADB_SERIAL="$2"
		shift 2
		;;
	--board-log)
		[[ $# -ge 2 ]] || { echo "Missing value for --board-log"; exit 1; }
		BOARD_LOG="$2"
		shift 2
		;;
	--size)
		[[ $# -ge 2 ]] || { echo "Missing value for --size"; exit 1; }
		if [[ ! "$2" =~ ^([0-9]+)x([0-9]+)$ ]]; then
			echo "Invalid --size format: $2"
			exit 1
		fi
		WIDTH="${BASH_REMATCH[1]}"
		HEIGHT="${BASH_REMATCH[2]}"
		shift 2
		;;
	--pixfmt)
		[[ $# -ge 2 ]] || { echo "Missing value for --pixfmt"; exit 1; }
		PIXEL_FORMAT="$2"
		shift 2
		;;
	--stream-count)
		[[ $# -ge 2 ]] || { echo "Missing value for --stream-count"; exit 1; }
		STREAM_COUNT="$2"
		shift 2
		;;
	--timeout)
		[[ $# -ge 2 ]] || { echo "Missing value for --timeout"; exit 1; }
		TIMEOUT_SEC="$2"
		shift 2
		;;
	--devices)
		[[ $# -ge 2 ]] || { echo "Missing value for --devices"; exit 1; }
		ONLY_DEVICES="$2"
		shift 2
		;;
	--no-adb)
		NO_ADB=1
		shift
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		usage
		exit 1
		;;
	esac
done

require_cmd v4l2-ctl
require_cmd timeout
require_cmd awk
require_cmd sed
require_cmd date

if [[ "${NO_ADB}" -eq 0 ]]; then
	require_cmd adb
fi

if [[ "${WIDTH}" -le 0 || "${HEIGHT}" -le 0 ]]; then
	echo "[probe] invalid size: ${WIDTH}x${HEIGHT}"
	exit 1
fi
if [[ "${STREAM_COUNT}" -le 0 ]]; then
	echo "[probe] invalid stream-count: ${STREAM_COUNT}"
	exit 1
fi
if [[ "${TIMEOUT_SEC}" -le 0 ]]; then
	echo "[probe] invalid timeout: ${TIMEOUT_SEC}"
	exit 1
fi

if [[ "${NO_ADB}" -eq 0 ]]; then
	adb_exec wait-for-device >/dev/null
	adb_exec shell "test -f '${BOARD_LOG}' || touch '${BOARD_LOG}'" >/dev/null
fi

declare -a NODES=()
if [[ -n "${ONLY_DEVICES}" ]]; then
	IFS=',' read -r -a NODES <<<"${ONLY_DEVICES}"
else
	while IFS= read -r node; do
		[[ -n "${node}" ]] || continue
		NODES+=("${node}")
	done < <(compgen -G "${VIDEO_GLOB}" | sort -V || true)
fi

if [[ ${#NODES[@]} -eq 0 ]]; then
	echo "[probe] no video node found"
	exit 1
fi

if [[ "${NO_ADB}" -eq 0 ]]; then
	echo "[probe] board_log=${BOARD_LOG}"
else
	echo "[probe] no-adb mode enabled"
	echo "[probe] please run on board serial first:"
	echo "        tail -f ${BOARD_LOG} | grep -E 'UVC_EVENT_STREAMON|channel [0-9]+ stream ON'"
fi
echo "[probe] stream setup: ${WIDTH}x${HEIGHT} ${PIXEL_FORMAT}, count=${STREAM_COUNT}, timeout=${TIMEOUT_SEC}s"
echo
if [[ "${NO_ADB}" -eq 0 ]]; then
	printf "%-14s %-10s %-10s %-12s %s\n" "video_node" "channel" "video_id" "stream_intf" "status"
	printf "%-14s %-10s %-10s %-12s %s\n" "----------" "-------" "--------" "----------" "------"
else
	printf "%-14s %-24s %s\n" "video_node" "host_probe_time" "status"
	printf "%-14s %-24s %s\n" "----------" "------------------------" "------"
fi

for node in "${NODES[@]}"; do
	if [[ ! -e "${node}" ]]; then
		printf "%-14s %-10s %-10s %-12s %s\n" "${node}" "-" "-" "-" "skip(not-exist)"
		continue
	fi

	marker="PROBE_$(basename "${node}")_$(date +%s%N)"
	start_tag="${marker}_START"
	end_tag="${marker}_END"
	probe_time="$(date '+%F %T.%3N')"

	if [[ "${NO_ADB}" -eq 0 ]]; then
		adb_exec shell "echo '${start_tag}' >> '${BOARD_LOG}'" >/dev/null
	fi

	set +e
	timeout "${TIMEOUT_SEC}" \
		v4l2-ctl -d "${node}" \
		--set-fmt-video=width="${WIDTH}",height="${HEIGHT}",pixelformat="${PIXEL_FORMAT}" \
		--stream-mmap=3 \
		--stream-count="${STREAM_COUNT}" \
		--stream-to=/dev/null >/tmp/"$(basename "${node}")".probe.log 2>&1
	rc=$?
	set -e

	status="ok"
	if [[ ${rc} -ne 0 ]]; then
		status="host-open-failed(rc=${rc})"
	fi

	if [[ "${NO_ADB}" -eq 0 ]]; then
		adb_exec shell "echo '${end_tag}' >> '${BOARD_LOG}'" >/dev/null
		section="$(adb_exec shell "awk '/${start_tag}/{flag=1;next}/${end_tag}/{flag=0}flag' '${BOARD_LOG}'" || true)"

		channel="$(printf '%s\n' "${section}" | sed -n 's/.*channel \([0-9][0-9]*\) stream ON.*/\1/p' | tail -n 1)"
		video_id="$(printf '%s\n' "${section}" | sed -n 's/.*video_id=\([0-9][0-9]*\).*/\1/p' | tail -n 1)"
		stream_intf="$(printf '%s\n' "${section}" | sed -n 's/.*UVC_EVENT_STREAMON:.*stream_intf=\([0-9][0-9]*\).*/\1/p' | tail -n 1)"

		if [[ -z "${channel}" ]]; then
			channel="-"
		fi
		if [[ -z "${video_id}" ]]; then
			video_id="-"
		fi
		if [[ -z "${stream_intf}" ]]; then
			stream_intf="-"
		fi
		if [[ "${channel}" == "-" ]]; then
			status="no-streamon-found"
		fi
		printf "%-14s %-10s %-10s %-12s %s\n" "${node}" "${channel}" "${video_id}" "${stream_intf}" "${status}"
	else
		printf "%-14s %-24s %s\n" "${node}" "${probe_time}" "${status}"
	fi
done

echo
echo "[probe] done"
