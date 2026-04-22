# 配置文件说明

`my_uvc` 将配置拆成多个文件，**各自对应不同职责**，便于与后续 **`libmy_uvc` / `libmy_uvc_pip_helper` / 测试程序** 的文档对齐。

| 文件 | 区段名 | 职责 |
|------|--------|------|
| `libmy_uvc.ini` | `[libmy_uvc]` | UVC 协议栈侧：路数、分辨率、编码类型、H.264 发送策略、轮询休眠、日志级别等。 |
| `libmy_uvc_pip.ini` | `[libmy_uvc_pip]` | PiP 合成侧：是否启用、叠加图路径、小窗位置与尺寸、合成 JPEG 质量、**断流超时**（`pip_overlay_stale_timeout_ms`，缺省 5000）等。 |
| `uvctest.ini` | `[uvctest]` | 测试/应用侧：媒体文件路径、`channelN_*` 覆盖、发送日志周期、统计开关与周期。 |

多路产品模板仍为 **`config/profiles/my_uvc_*ch_independent.ini`**（区段 **`[my_uvc]`**）：由 **`select_profile.sh --install`** 推到板端 **`/userdata/profile.ini`**，启动时使用 **`uvctest -c /userdata/profile.ini`**（单文件路径，与目录合并是两条路径）。

## C++ 中的区段类型（P2-T3）

合并后的 **`AppConfig`**（`include/app_config.h`）由三个子结构组成，与上表一一对应，便于各模块在头文件内自解释 ini 归属：

| 成员 | 结构体类型 | 区段 / 文件 |
|------|------------|-------------|
| `libmy_uvc` | `LibmyUvcIniFields` | `[libmy_uvc]` / `libmy_uvc.ini` — 与 `my_uvc_config_t`、`my_uvc.h` 说明一致 |
| `libmy_uvc_pip` | `LibmyUvcPipIniFields` | `[libmy_uvc_pip]` / `libmy_uvc_pip.ini` — 与 `pip_helper_config_t`、`pip_helper.h` 一致 |
| `uvctest` | `UvctestIniFields` | `[uvctest]` / `uvctest.ini` — 仅 `uvctest` 可执行文件使用 |

## 加载方式

- 传入 **目录**（例如 `-c /userdata`）：依次合并  
  `libmy_uvc.ini` → `libmy_uvc_pip.ini` → `uvctest.ini`（后者覆盖前者同名逻辑）。  
  **至少须存在上述三者之一**；若目录下没有任何分文件，加载失败（不再回退其它文件名）。
- 传入 **单个文件**（例如 `-c /userdata/profile.ini`）：按文件内区段解析（支持 **`[my_uvc]`** 旧区段名或 **`[libmy_uvc]`** 等分文件区段写在同一文件内）。

默认配置路径为 **`/userdata`（目录）**，请在设备上放置 **`libmy_uvc.ini`、`libmy_uvc_pip.ini`、`uvctest.ini`**。

## 仅嵌入 `libmy_uvc.so` 的应用（C API）

若程序**不**跑 `uvctest`，只链接 `libmy_uvc` 并调用 `my_uvc_create`：可把 **某一个** ini 文件放在已知路径，用头文件 **`my_uvc.h`** 中的 **`my_uvc_load_ini_section_only(path, "libmy_uvc", …)`** 只合并 `[libmy_uvc]`（或 legacy `[my_uvc]`）到 `my_uvc_config_t`。这与「目录 + 多文件合并」是两条路径；目录合并仍由 `uvctest` 使用。详见 `HANDOVER.md` §4。
