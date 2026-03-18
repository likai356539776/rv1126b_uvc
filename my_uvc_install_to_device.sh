#!/bin/bash
set -euo pipefail

# -----------------------------------------------------------------------------
# my_uvc 一键部署脚本
# 用途:
#   1) 将当前工程产物部署到目标板
#   2) 设置执行权限和配置文件权限
#
# 约定:
#   - 可执行程序部署到: /usr/bin
#   - 配置文件部署到:   /userdata
#
# 后续如新增产物(例如多路配置文件/额外脚本/资源文件)，请在
# "Deploy artifacts" 区域追加对应 adb push 即可。
# -----------------------------------------------------------------------------

# Deploy artifacts: executable + usb config script + runtime config.
adb push build-rv1126b/my_uvc /usr/bin/
adb push scripts/my_uvc_usb_config.sh /usr/bin/
adb push config/my_uvc.ini /userdata/

# Set permissions on target board.
adb shell chmod +x /usr/bin/my_uvc
adb shell chmod +x /usr/bin/my_uvc_usb_config.sh
adb shell chmod 666 /userdata/my_uvc.ini

echo "[my_uvc_install] Deploy finished."
