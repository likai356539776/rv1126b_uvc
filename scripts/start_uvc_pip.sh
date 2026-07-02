#!/bin/sh

# Make sure that we own this session (pid equals sid) to survive ADB disconnect during USB re-enumeration
if [ "$(sed 's/(.*)//' /proc/$$/stat | cut -d' ' -f6)" != "$$" ]; then
	setsid "$0" "$@"
	exit $?
fi


# 内置摄像头测试脚本，支持 v4l2(720p) 和 rockit(1080p) 两种模式，
# 使用 YOLO 模型进行目标检测，并支持 PIP（画中画）功能。
# 用法: start_uvc_pip.sh [v4l2|rockit]

if [ "$1" = "v4l2" ]; then
  CAMERA_TYPE="v4l2"
  CAMERA_NODE="/dev/video51"
  RESOLUTION="1920x1080"
  PIX_FMT="MJPEG"
elif [ "$1" = "rockit" ]; then
  CAMERA_TYPE="rockit"
  RESOLUTION="1920x1080"
  PIX_FMT="MJPEG"
else
  echo "Usage: $0 [v4l2|rockit]"
  exit 1
fi

# 根据所选模式配置并启动
# 从分辨率中解析宽高
WIDTH=$(echo "$RESOLUTION" | cut -d'x' -f1)
HEIGHT=$(echo "$RESOLUTION" | cut -d'x' -f2)

# 日志和 PID 文件
LOG="/userdata/uvctest_${CAMERA_TYPE}.log"
PIDFILE="/var/run/uvctest_${CAMERA_TYPE}.pid"
mkdir -p "$(dirname "$PIDFILE")" 2>/dev/null || true

echo "Starting with camera type: $CAMERA_TYPE, resolution: $RESOLUTION" > "$LOG" 2>&1
echo "Starting with camera type: $CAMERA_TYPE, resolution: $RESOLUTION"

# 配置摄像头相关参数
/usr/bin/my_uvc_usb_config.sh --stop-system-usb -f "$PIX_FMT" -w "$WIDTH" -h "$HEIGHT" -p 30 -n 1 >> "$LOG" 2>&1

sleep 3

# 公共参数
YOLO_MODEL="/userdata/yolov8n.rknn"
YOLO_LABELS="/userdata/coco_80_labels_list.txt"

uvctest \
  --camera-type "$CAMERA_TYPE" \
  ${CAMERA_NODE:+--camera-node $CAMERA_NODE} \
  --size "$RESOLUTION" \
  --yolo-model "$YOLO_MODEL" \
  --yolo-labels "$YOLO_LABELS" \
  --channels 1 \
  --pip-tile-n-tiles 8 \
  --pip-jpeg-quality 85 >> "$LOG" 2>&1 &

UVCTEST_PID=$!
if ! echo "$UVCTEST_PID" > "$PIDFILE" 2>/dev/null; then
  echo "$UVCTEST_PID" > "/tmp/uvctest_${CAMERA_TYPE}.pid"
fi

echo "uvctest started (pid $UVCTEST_PID), log: $LOG"

exit 0
