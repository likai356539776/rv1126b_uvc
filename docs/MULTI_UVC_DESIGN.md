# my_uvc Multi-UVC Design

## Goal

Support multiple UVC functions (`uvc.gs1`, `uvc.gs2`, ...) with each channel independently configurable, and provide resilience against USB cable disconnect/reconnect.

## Architecture Overview

```
┌──────────────────────────────────────────────────┐
│  main.cpp                                        │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  │
│  │ channel_   │  │ channel_   │  │ stats_     │  │
│  │ worker[0]  │  │ worker[N]  │  │ worker     │  │
│  └─────┬──────┘  └─────┬──────┘  └────────────┘  │
│        │               │                         │
│  ┌─────▼───────────────▼─────────────────────┐   │
│  │ third_party/uvc (UVC Gadget core)         │   │
│  │ ┌──────────────┐  ┌──────────────────────┐│   │
│  │ │uvc_control_  │  │ uvc_gadget_pthread   ││   │
│  │ │thread        │  │ ×N (per video_id)    ││   │
│  │ │ - scan nodes │  │ - select() loop      ││   │
│  │ │ - add/remove │  │ - events_process     ││   │
│  │ │ - lifecycle  │  │ - video_process      ││   │
│  │ └──────┬───────┘  │ - hot-plug survive   ││   │
│  │        │          └──────────────────────┘│   │
│  │ ┌──────▼───────┐                          │   │
│  │ │uevent_stub.c │                          │   │
│  │ │ - netlink    │                          │   │
│  │ │ - video4linux│                          │   │
│  │ │   add/remove │                          │   │
│  │ └─────────────┘                           │   │
│  └───────────────────────────────────────────┘   │
└──────────────────────────────────────────────────┘
```

## Source File Responsibilities

| File | Role |
|------|------|
| `src/main.cpp` | Entry point, config parsing, per-channel worker threads, stats thread |
| `src/app_config.cpp` | INI configuration parsing |
| `src/pip_mjpeg.cpp` | MJPEG picture-in-picture compositing (libjpeg) |
| `src/uevent_stub.c` | Netlink uevent monitoring for USB hot-plug detection |
| `third_party/uvc/uvc_control.c` | UVC control thread: device scanning, lifecycle management |
| `third_party/uvc/uvc-gadget.c` | V4L2 UVC event loop, buffer management, hot-plug resilience |
| `third_party/uvc/uvc_video.cpp` | UVC video thread management, run_state control |
| `third_party/uvc/uvc_encode.c` | Codec format initialization |

## USB Hot-Plug Recovery Design

### Problem

On Rockchip gadget platforms, USB cable disconnect does not destroy the `/dev/videoN` device nodes (they are tied to configfs, not USB link state). The UVC kernel driver disables the function but keeps the V4L2 device alive. If old mmap buffers remain mapped when USB reconnects, `VIDIOC_REQBUFS` returns `-EBUSY` and the kernel tears down the entire gadget.

### Solution: Thread Survival with Immediate Cleanup

```
USB disconnect
    │
    ▼
uvc_video_process() → VIDIOC_QBUF returns ENODEV
    │
    ▼
Immediate cleanup:
  1. VIDIOC_STREAMOFF (may fail, OK)
  2. munmap() all buffers
  3. VIDIOC_REQBUFS(0) to release kernel buffers
  4. is_streaming = 0
    │
    ▼
Thread stays alive in select() with 2s timeout
    │
    ▼  (USB cable reconnected)
    │
Kernel sends STREAMON event on same fd
    │
    ▼
uvc_handle_streamon_event():
  1. Release any residual old buffers (safety net)
  2. VIDIOC_REQBUFS(nbufs) → allocate fresh buffers
  3. mmap() new buffers
  4. VIDIOC_STREAMON
    │
    ▼
Streaming resumes automatically
```

### Key Design Decisions

1. **Thread does NOT exit on ENODEV** — avoids complex thread recreation and control thread coordination.
2. **Buffers are released immediately** — prevents `-EBUSY` when USB reconnects.
3. **`dev->mem` set to NULL after free** — prevents double-free and enables safe re-check.
4. **`uvc_handle_streamon_event()` always cleans up first** — double safety net for any residual state.
5. **`select()` uses 2s timeout** — prevents CPU spin during disconnect, still responsive to reconnect events.

## Completed Milestones

- M1: Dual-channel same-source streaming ✅
- M2: Per-channel independent file and fps ✅
- M3: Stream reopen robustness (startup priming + per-channel stream gate) ✅
- M4: USB hot-plug recovery (thread survival + buffer cleanup + auto-resume) ✅

## Future Milestones

- M5: Error telemetry (per-channel last error reason + timestamp)
- M6: Real-time VENC source integration (at least one channel)

## Risks

- Multi-UVC host compatibility varies by OS and camera application.
- USB2 (480Mbps) bandwidth limits concurrent high-bitrate streams to approximately 4-5 channels.
- USB hot-plug recovery depends on gadget device nodes persisting across disconnect (Rockchip configfs default behavior).
