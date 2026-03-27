# my_uvc 多路 UVC 设计说明

## 目标

在 `my_uvc` 基础上支持多个 UVC Function（`uvc.gs1`、`uvc.gs2`...），保持每路可独立配置与独立运行，并具备 USB 热拔插恢复能力。

## 架构概览

```
┌──────────────────────────────────────────────────┐
│  main.cpp                                        │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  │
│  │ channel_   │  │ channel_   │  │ stats_     │  │
│  │ worker[0]  │  │ worker[N]  │  │ worker     │  │
│  └─────┬──────┘  └─────┬──────┘  └────────────┘  │
│        │               │                         │
│  ┌─────▼───────────────▼─────────────────────┐   │
│  │ third_party/uvc（UVC Gadget 核心层）      │   │
│  │ ┌──────────────┐  ┌──────────────────────┐│   │
│  │ │uvc_control_  │  │ uvc_gadget_pthread   ││   │
│  │ │thread        │  │ ×N（每 video_id 一个）││   │
│  │ │ - 扫描节点   │  │ - select() 事件循环  ││   │
│  │ │ - 增删管理   │  │ - 事件处理           ││   │
│  │ │ - 生命周期   │  │ - 视频数据处理       ││   │
│  │ └──────┬───────┘  │ - 热拔插线程存活     ││   │
│  │        │          └──────────────────────┘│   │
│  │ ┌──────▼───────┐                          │   │
│  │ │uevent_stub.c │                          │   │
│  │ │ - netlink    │                          │   │
│  │ │ - 监听       │                          │   │
│  │ │   video4linux │                          │   │
│  │ │   增删事件   │                          │   │
│  │ └─────────────┘                           │   │
│  └───────────────────────────────────────────┘   │
└──────────────────────────────────────────────────┘
```

## 源文件职责

| 文件 | 职责 |
|------|------|
| `src/main.cpp` | 主入口、配置解析、每路工作线程、统计线程 |
| `src/app_config.cpp` | INI 配置解析 |
| `src/pip_mjpeg.cpp` | MJPEG 画中画合成（libjpeg） |
| `src/uevent_stub.c` | netlink uevent 监听，USB 热拔插检测 |
| `third_party/uvc/uvc_control.c` | UVC 控制线程：设备扫描、生命周期管理 |
| `third_party/uvc/uvc-gadget.c` | V4L2 UVC 事件循环、缓冲区管理、热拔插恢复 |
| `third_party/uvc/uvc_video.cpp` | UVC 视频线程管理、run_state 控制 |
| `third_party/uvc/uvc_encode.c` | 编码格式初始化 |

## USB 热拔插恢复设计

### 问题

在 Rockchip gadget 平台上，USB 拔线不会销毁 `/dev/videoN` 设备节点（节点由 configfs 管理，与 USB 物理连接无关）。UVC 内核驱动会禁用 function 但保持 V4L2 设备活跃。如果旧的 mmap 缓冲区在 USB 重连时仍然映射着，`VIDIOC_REQBUFS` 会返回 `-EBUSY`，导致内核级联崩溃并完全拆除 gadget。

### 解决方案：线程原地存活 + 即时清理缓冲区

```
USB 拔线
    │
    ▼
uvc_video_process() → VIDIOC_QBUF 返回 ENODEV
    │
    ▼
立即清理：
  1. VIDIOC_STREAMOFF（可能失败，无影响）
  2. munmap() 释放所有缓冲区
  3. VIDIOC_REQBUFS(0) 释放内核侧缓冲区
  4. is_streaming = 0
    │
    ▼
线程在 select() 中以 2s 超时等待（不退出、不空转）
    │
    ▼  （USB 线重新插入）
    │
内核在同一 fd 上发送 STREAMON 事件
    │
    ▼
uvc_handle_streamon_event()：
  1. 释放任何残留旧缓冲区（安全兜底）
  2. VIDIOC_REQBUFS(nbufs) → 分配全新缓冲区
  3. mmap() 映射新缓冲区
  4. VIDIOC_STREAMON
    │
    ▼
推流自动恢复
```

### 关键设计决策

1. **线程在 ENODEV 时不退出** — 避免复杂的线程重建和控制线程协调。
2. **缓冲区立即释放** — 防止 USB 重连时出现 `-EBUSY`。
3. **`dev->mem` 释放后置 NULL** — 防止重复释放，支持安全重检查。
4. **`uvc_handle_streamon_event()` 总是先清理** — 双重保险，确保残留状态被清除。
5. **`select()` 使用 2s 超时** — 断线期间不空转 CPU，重连时仍能快速响应。

## 已完成里程碑

- M1：双路同源推流 ✅
- M2：每路独立文件 + 独立帧率 ✅
- M3：复开流鲁棒性增强（startup priming + 每路 stream gate）✅
- M4：USB 热拔插恢复（线程存活 + 缓冲区清理 + 自动恢复流）✅

## 后续里程碑

- M5：增强错误遥测（每路最近错误原因 + 时间戳）
- M6：接入实时 VENC 源（至少一路）

## 风险点

- 多路 UVC 的主机兼容性受 OS、播放器和驱动栈影响明显。
- USB2（480Mbps）带宽限制下，高码率多路并发约 4~5 路为工程上限。
- USB 热拔插恢复依赖 gadget 设备节点在 USB 断开时保持存在（Rockchip configfs 默认行为）。
