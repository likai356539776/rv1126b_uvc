#!/usr/bin/env bash
# P1-I2: USB 拔插后同一 channel_id 仍可通过 my_uvc_submit_* 送帧；内部 video_id 由库重解析。
# 需人工拔插 USB；自动化仅提供前置检查与清单步骤（对齐 TEST_CHECKLIST_CN.md §4.4）。
#
# 用法:
#   ./board_usb_replug_channel_submit.sh check   # 默认 — 打印步骤并检查命令可用
#   ./board_usb_replug_channel_submit.sh steps  # 仅输出 §4.4.1 步骤（无命令检查）
#
# 环境: UVCTEST, CONFIG（同其他 board 脚本）
set -euo pipefail

UVCTEST="${UVCTEST:-uvctest}"
CONFIG="${CONFIG:-/userdata}"

run_check()
{
	if ! command -v "${UVCTEST}" >/dev/null 2>&1; then
		echo "WARN: ${UVCTEST} not in PATH (set UVCTEST on device)."
	fi
	[[ -e "${CONFIG}" ]] || echo "WARN: CONFIG not found: ${CONFIG}"

	cat <<EOF
=== P1-I2 USB replug + channel_id submit (manual) ===
前置:
  - ${UVCTEST} 已在板端部署；CONFIG=${CONFIG}
  - USB 配置脚本使用过 --stop-system-usb（拔插测试建议）

步骤（摘自 TEST_CHECKLIST_CN.md §4.4.1）:
  1) 主机预览画面正常（uvctest 运行中）。
  2) 拔掉 USB，等待 2~3 秒。
  3) 重新插入 USB；主机关闭再打开摄像头。
  4) 观察板端日志：应出现 disconnect / STREAMON / Starting video stream 等恢复信息。
  5) 确认推流继续：日志中可出现 channel N remap video_id X -> Y（重枚举后），且 ch=... sent= 持续更新。

期望: 同 channel_id 仍送帧成功，无需重启 uvctest（与 §4.4.3 多次循环一致）。

board_usb_replug_channel_submit check: ok (manual replug required for full P1-I2)
EOF
}

run_steps()
{
	sed -n '/### 4.4.1/,/### 4.4.2/p' "$(cd "$(dirname "$0")/../.." && pwd)/docs/TEST_CHECKLIST_CN.md" 2>/dev/null ||
		echo "See docs/TEST_CHECKLIST_CN.md §4.4"
}

case "${1:-check}" in
check) run_check ;;
steps) run_steps ;;
-h | --help)
	echo "Usage: $0 [check|steps]"
	exit 0
	;;
*)
	echo "Usage: $0 [check|steps]"
	exit 1
	;;
esac
