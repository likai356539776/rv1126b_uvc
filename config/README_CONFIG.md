# 配置文件说明

`my_uvc` 将配置拆成多个文件，**各自对应不同职责**，便于与后续 **`libmy_uvc` / `libmy_uvc_pip_helper` / 测试程序** 的文档对齐。

| 文件 | 区段名 | 职责 |
|------|--------|------|
| `libmy_uvc.ini` | `[libmy_uvc]` | UVC 协议栈侧：路数、分辨率、编码类型、H.264 发送策略、轮询休眠、日志级别等。 |
| `libmy_uvc_pip.ini` | `[libmy_uvc_pip]` | PiP 合成侧：是否启用、叠加图路径、小窗位置与尺寸、合成 JPEG 质量等。 |
| `uvctest.ini` | `[uvctest]` | 测试/应用侧：媒体文件路径、`channelN_*` 覆盖、发送日志周期、统计开关与周期。 |
| `my_uvc.ini` | `[my_uvc]` | **兼容旧部署**的单文件，键与过去一致；仍可用 `-c /path/to/my_uvc.ini` 单独指定。 |

## 加载方式

- 传入 **目录**（例如 `-c /userdata`）：依次合并  
  `libmy_uvc.ini` → `libmy_uvc_pip.ini` → `uvctest.ini`（后者覆盖前者同名逻辑）。  
  若上述三个都不存在，则尝试同目录下的 **`my_uvc.ini`**。
- 传入 **单个文件**：按文件内区段解析（支持 legacy `[my_uvc]` 或分区段写在同一文件内）。

默认配置路径为 **`/userdata`（目录）**，请在设备上放置拆分后的 ini，或继续使用单文件 `my_uvc.ini`。

## 仅嵌入 `libmy_uvc.so` 的应用（C API）

若程序**不**跑 `uvctest`，只链接 `libmy_uvc` 并调用 `my_uvc_create`：可把 **某一个** ini 文件放在已知路径，用头文件 **`my_uvc.h`** 中的 **`my_uvc_load_ini_section_only(path, "libmy_uvc", …)`** 只合并 `[libmy_uvc]`（或 legacy `[my_uvc]`）到 `my_uvc_config_t`。这与「目录 + 多文件合并」是两条路径；目录合并仍由 `uvctest` 使用。详见 `HANDOVER.md` §4。
