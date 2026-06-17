#!/bin/sh

# 停止由 start_uvc_pip.sh 启动的 uvctest 进程。
# 用法: stop_uvc_pip.sh [v4l2|rockit|all]

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 [v4l2|rockit|all]"
  exit 1
fi

case "$1" in
  v4l2|rockit)
    CAMERA_TYPES="$1"
    ;;
  all)
    CAMERA_TYPES="v4l2 rockit"
    ;;
  *)
    echo "Usage: $0 [v4l2|rockit|all]"
    exit 1
    ;;
esac

kill_pidfile() {
  CAMERA_TYPE="$1"
  PIDFILE="/var/run/uvctest_${CAMERA_TYPE}.pid"
  FALLBACK_PIDFILE="/tmp/uvctest_${CAMERA_TYPE}.pid"

  if [ -f "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE" 2>/dev/null)
  elif [ -f "$FALLBACK_PIDFILE" ]; then
    PID=$(cat "$FALLBACK_PIDFILE" 2>/dev/null)
  else
    PID=""
  fi

  if [ -n "$PID" ]; then
    if kill -0 "$PID" 2>/dev/null; then
      echo "Stopping uvctest ($CAMERA_TYPE) pid $PID..."
      kill "$PID"
      sleep 1
      if kill -0 "$PID" 2>/dev/null; then
        echo "Process still running, sending SIGKILL to $PID"
        kill -9 "$PID" 2>/dev/null || true
      fi
      rm -f "$PIDFILE" "$FALLBACK_PIDFILE"
      return 0
    else
      echo "PID file exists but process $PID is not running. Cleaning up." 
      rm -f "$PIDFILE" "$FALLBACK_PIDFILE"
    fi
  fi

  echo "No PID file found for $CAMERA_TYPE, trying pgrep by camera type..."
  PID=$(pgrep -f "uvctest.*--camera-type $CAMERA_TYPE" | head -n 1 || true)
  if [ -n "$PID" ]; then
    echo "Stopping uvctest ($CAMERA_TYPE) pid $PID..."
    kill "$PID"
    sleep 1
    if kill -0 "$PID" 2>/dev/null; then
      echo "Process still running, sending SIGKILL to $PID"
      kill -9 "$PID" 2>/dev/null || true
    fi
    return 0
  fi

  echo "No running uvctest process found for $CAMERA_TYPE"
  return 1
}

restore_usb() {
  if [ -x /usr/bin/usbdevice ]; then
    echo "Restoring system USB manager..."
    /usr/bin/usbdevice start || true
  else
    echo "usbdevice not found; cannot explicitly restore system USB manager"
  fi
}

EXIT_CODE=0
for CAMERA_TYPE in $CAMERA_TYPES; do
  kill_pidfile "$CAMERA_TYPE" || EXIT_CODE=1
done

restore_usb

exit "$EXIT_CODE"
