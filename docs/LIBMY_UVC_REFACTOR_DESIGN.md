# libmy_uvc.so、libmy_uvc_pip_helper 与 uvctest 拆分设计（**定稿**）

本文档为 **`my_uvc` 工程重构的架构与实施路线定稿**：模块边界、对外 API 方向、PiP 方案、构建安装与风险。**不包含具体函数级实现代码**，但包含 **配置已拆分后的后续落地顺序**，供开发直接按阶段执行。

**相关文档**：运行时配置说明见仓库根目录 [`config/README_CONFIG.md`](../config/README_CONFIG.md)。

---

## 1. 背景与目标

### 1.1 现状（摘要）

单可执行文件曾将下列职责耦合在同一目标中：

| 类别 | 典型内容 |
|------|----------|
| UVC 核心 | `third_party/uvc`（gadget、control、video、encode、yuv）、`uevent_stub.c` |
| 与信源相关的准备 | 读取 H.264 Annex B / MJPEG（文件或目录）、NAL 切分、帧索引、JPEG SOI 分段 |
| 与展示相关的处理 | MJPEG 路径下的 PiP：JPEG 解码、缩放、NV12、RGA/MPP 合成（`pip_mjpeg` / `mpp_jpeg`） |
| 运行时编排 | `channel_worker` 循环、`uvc_read_camera_buffer_by_id`、重映射 `video_id`、统计线程、`uvc_control_run` / `uvc_formats_init` 等 |
| 配置 | `AppConfig`、`load_app_config`、`main` 内 CLI |

### 1.2 目标

1. **`libmy_uvc.so`**：承载 **UVC 协议栈 + 送帧**，对外 **稳定 C 风格 API**（内部可 C++）。**面向第三方应用交付**：第三方链接本库，将合成后的视频帧 **`my_uvc_submit_*`** 送入 UVC。
2. **`uvctest`**：仅作 **本仓库内的测试/示例程序**——用于验证 **`libmy_uvc` + `libmy_uvc_pip_helper`** 的联调行为；**所有** 从磁盘/目录读取的媒体（**含** H.264、YUV、JPEG 及类似 `pip_overlay_path` 的路径）**均在 `uvctest` 内完成解码/读帧**，再经 **API** 把「背图 + 各路 PiP 画面」以**数据流/缓冲区**形式交给 `pip_helper`，**不作为**第三方集成时的必选能力。
3. **`libmy_uvc_pip_helper.a`**（方案 B）：**协议栈外** 完成 **全画布合成**——**独立主讲人 PiP** + **背景下部（下三分之一）网格**（布局 **`n_tiles`（1～16）**，每帧 **`n_active`（0～`n_tiles`）** 仅叠前 **`n_active` 路**，余格露背图，见 §2.4）；**主讲人 / 各路 tile** 在 **无新数据时沿用库内上一帧**，超过 **配置的超时时间**（**缺省 5000 ms**，见 **`[libmy_uvc_pip]`** 与 **`pip_helper_config_t`**）仍无新数据则 **该路露背图**（§2.4）；**v1 合成 API 仅接收 NV12**（背图每帧；主讲人与 tile 按 API 更新或冻结）；合成结果（MJPEG，与头文件一致）再交给 **`my_uvc_submit_mjpeg`**。**面向第三方交付**：第三方在应用内将 H.264/MJPEG/YUV 等 **解码并缩放到约定尺寸后** 再传入，**不得依赖** `pip_helper` 内读路径或接受压缩帧作为 v1 入参（见 §3.8、§2.5）。
4. **构建产物**：`install` 下 **`lib/`、`include/`、`bin/`**（`uvctest` 为验证用）；脚本与设备部署规则与 CMake / `my_uvc_install_to_device.sh` 对齐并演进。

### 1.3 已落地项：配置文件拆分（**重构前置条件**）

以下已在当前仓库实现，**后续库化与 API 设计应与之对齐**，避免再回到「单文件混杂职责」：

| 文件 | 区段 | 归属（文档/职责） |
|------|------|-------------------|
| `config/libmy_uvc.ini` | `[libmy_uvc]` | 将来 **仅由 `libmy_uvc` 文档解释**；对应 UVC 协商、路数、分辨率、编码类型、H.264 发送策略、`idle_sleep_ms`、库侧日志级别等。 |
| `config/libmy_uvc_pip.ini` | `[libmy_uvc_pip]` | **布局/质量/开关/断流超时**等可由文档解释；**`pip_overlay_stale_timeout_ms`（或等价键）** 与 **`pip_helper_create` 结构体** 同步，**缺省 5000**（§2.4、§3.8）；**路径类键**（如 `pip_overlay_path`）在 **交付视角** 上主要为 **`uvctest` 读盘与回归**；**第三方集成** 以 **结构体 + 每帧 NV12** 为准（§2.5）。 |
| `config/uvctest.ini` | `[uvctest]` | 将来 **仅由测试程序 / 应用示例文档解释**；对应媒体路径、`channelN_*`、统计与周期日志等。 |
| `config/profiles/*.ini` | `[my_uvc]` | 多路产品模板；`select_profile --install` 可下发为板端 **`/userdata/profile.ini`**。 |

**加载规则**：`-c` 指向 **目录** 时依次合并 `libmy_uvc.ini` → `libmy_uvc_pip.ini` → `uvctest.ini`；**至少须存在其一**，否则加载失败。`-c` 指向 **文件** 时仍支持单文件内 **`[my_uvc]`** 等区段。解析与区段约束见 `app_config.cpp`。

**重构时的配置策略（定稿）**：

- **阶段 A（当前）**：单一 `AppConfig` 聚合，由 **可执行文件** 统一 `load_app_config`，供 `main` 使用。
- **阶段 B（库完成后）**：  
  - **`libmy_uvc`**：从 **`[libmy_uvc]`** 映射到 **`MyUvcConfig` 结构体**（或等价），由 **`my_uvc_init(&cfg)`** 传入；**不**要求库内解析 ini（避免库与文件路径策略强绑定）。可选提供 **`my_uvc_load_config_ini(path, …)`** 仅解析 `[libmy_uvc]` 段，作为便利函数。  
  - **`libmy_uvc_pip_helper`**：从 **`[libmy_uvc_pip]`** 映射到 **`PipHelperConfig`**；同理以 **结构体入参** 为主，ini 解析为可选。  
  - **`uvctest`**：继续读取 **`[uvctest]`**（或合并后的 `AppConfig` 中对应字段），**仅应用层** 持有媒体路径。

这样 **配置文件物理拆分** 与 **运行时结构体拆分** 一一对应，文档职责清晰。

---

## 2. 模块边界原则

### 2.1 放入动态库 `libmy_uvc.so`

- UVC vendor 源码闭包、`uevent` 等。
- 显式生命周期：`init` → `start` → `stop` / `join` → `deinit`（与当前 `uvc_control_run` / `uvc_control_join` / `uvc_formats_*` 语义一致）。
- 对外 **帧提交**：`my_uvc_submit_mjpeg` / `my_uvc_submit_h264`（名称以最终实现为准），内部对接 `uvc_read_camera_buffer_by_id` 等。
- **不**包含：文件 I/O、NAL 解析、PiP 合成。

### 2.2 放入测试程序 `uvctest`

- **全部** 本地/目录读取：Annex B / MJPEG / **YUV / H.264** 等文件或目录的 **I/O 与解码**（含用于 PiP 的素材路径）；准备每帧数据后 **`pip_helper` 仅见 NV12 缓冲区**（及 `pip_helper.h` 元数据）。
- `channel_worker` 等循环逻辑（薄封装：**读帧 → 调 `pip_helper` 合成 → `my_uvc_submit_*`**）。
- CLI、**`[uvctest]`** / **`[libmy_uvc_pip]`** 中与**测试用路径**相关的配置、统计线程（若保留为示例行为）。
- 链接 **`libmy_uvc_pip_helper`**，在送 UVC 前完成合成；**验证时** 可将 **同一张图** 同时作为主讲人与下排 **N 路** 的输入，以简化素材，但 **数据仍经 API 分路传入** `pip_helper`（与产品形态一致）。

### 2.3 PiP：方案 B + `libmy_uvc_pip_helper.a`

| 组件 | 职责 |
|------|------|
| **`libmy_uvc.so`** | 仅协议栈 + 送帧；**不**做 PiP。 |
| **`libmy_uvc_pip_helper.a`** | **每帧**接收调用方给出的 **背图 + 主讲人 + 下三分之一 N 路** 的 **NV12** 缓冲区（v1）；完成 RGA/MPP 等合成；**输出 MJPEG**；**不**链接 `libmy_uvc.so`；**不**承担产品级文件路径读取（§2.5、§3.8）。 |
| **第三方应用** | 链接两者；**自备信源**；每帧 **`pip_helper` → `my_uvc_submit_mjpeg`**。 |
| **`uvctest`** | 链接两者；**代劳读盘/解码**，仅用于验证上述链路。 |

### 2.4 `libmy_uvc_pip_helper.a` 功能（已定稿）

- **画面结构**：**全屏背图**；**主讲人**为 **一路独立 PiP**（矩形可配置，对应原「单 overlay / 旧 `pip_x/y/w/h`」语义）；**背景下部约三分之一高度** 内按 **`n_tiles`（1≤`n_tiles`≤16）** 计算格子几何（**≤8** 单行、**>8** 双行首行 8、次行剩余，**间隙/边距可配**），算法与 **`pip_tile_layout.hpp`** 一致。
- **每帧有效网格路数 `n_active`（v1 定案，选 A）**：**每帧** 合成调用携带 **`n_active`（0≤`n_active`≤`n_tiles`，名称以 `pip_helper.h` 为准）**。仅 **布局顺序下前 `n_active` 个格子** 叠对应 **tile NV12**；**其余格子不叠图**，该带区域 **保留背图**（不读占位缓冲、不要求调用方提供无效路的 NV12）。**`n_active` 可帧间变化**。`n_active=0` 表示本帧下排网格 **全部露背图**（**主讲人** 仍按配置单独叠加）。**布局上限 `n_tiles`** 可在 create 时或配置中给定，与 **`pip_tile_layout`** 的 `n_tiles` 一致。
- **时间对齐**：**背图** 每帧与合成调用 **同一时刻** 提交；**主讲人** 与 **各路 tile** 按 §2.4 下条 **「间断策略」** 与 API 约定提交新帧或沿用缓存（细则见 §8）。
- **数据源间断策略（v1 定案）**：**主讲人** 与 **下排每一参与合成的 grid 路**（对 **前 `n_active` 个槽位**，各路流 **相互独立**）——  
  - **无新数据**（本帧未向该路提交新的有效 NV12，语义以 `pip_helper.h` 为准）：**沿用该路在库内缓存的上一帧有效画面**（`pip_helper` **须**在收到新数据时 **同步拷贝** 入内部缓存，以便 `submit` 返回后调用方可释放外部缓冲）。  
  - **自该路最后一次收到有效数据起，连续超过配置时长仍无新数据**：该路 **不再叠加**，**对应区域露背图**（主讲人矩形或该 grid 格），直至该路 **再次出现有效数据**。  
  - **超时配置（v1 定案，选 C）**：**`pip_overlay_stale_timeout_ms`**（名称以 `pip_helper.h` / `libmy_uvc_pip.ini` 为准），**缺省 `5000`（毫秒）**；由 **`[libmy_uvc_pip]`** 与 **`pip_helper_config_t`** / **`pip_helper_create`** 一致传入。**第三方** 可只填结构体（自行读 ini 或不读）；**`uvctest`** 走合并后的 `AppConfig`。  
  - **`pip_helper_create` 未显式自定义超时（v1 定案，问题 6 选 A）**：**与 ini 缺省一致，库内按 5000 ms 生效**。**C 结构体推荐** 使用 **有符号整数**（如 **`int32_t`**）：**`-1`**（或宏 **`PIP_OVERLAY_STALE_TIMEOUT_USE_DEFAULT`**）表示 **「使用库缺省 5000」**；**`0`** 表示 **不因超时露背图**（**仅冻结上一帧**）；**`>0`** 为显式毫秒。**注意**：若将结构体 **`memset` 成全零** 且本字段为 **`0`**，语义为 **禁用超时**，**不是** 缺省 5000；需要缺省时请置 **`-1`** 或使用文档提供的 **初始化宏**。  
  - **`[libmy_uvc_pip]`（仅非负）**：键 **缺省或未写** 时解析为 **5000**；**`0`** 表示 **禁用超时**；**正整数** 为毫秒（与 `libmy_uvc_pip.ini` 注释一致）。  
  - **其它语义** 以 `pip_helper.h` 为准。  
  - **时间基准**：建议 **单调时钟**（monotonic）计量间隔，避免系统时间跳变。  
  - **背图**：本条 **不**约束背图源；背图仍按 **每帧由调用方提供**（若无背图则另属错误/重试语义，不在此扩展）。
- **输入形态（交付，v1 定案）**：**仅 NV12**。**背图、主讲人、每路 tile** 均为 **NV12**（含 **width/height/stride** 等描述，以 **`pip_helper.h`** 为准）。H.264、MJPEG、其它 YUV 等 **一律在应用内** 解码、必要时 **缩放/裁剪** 到与布局一致的平面尺寸后再传入；**v1 不接受** JPEG/H.264 **压缩帧** 作为合成输入（减少库内二次解码与格式分支，利于 RGA/MPP 快路径）。若未来扩展其它像素格式或「压缩入参」，须 **新版本 API** 与文档修订。
- **输出**：**合成后 MJPEG** 交给 **`libmy_uvc`**（`my_uvc_submit_mjpeg`）。

**与当前实现的关系**：仓库内可能存在 **仅面向 `uvctest` 的过渡实现**（例如单小窗、路径预载）；**目标形态** 以本条与 §3.8 为准，演进时应收敛为 **无库内读盘** 的 API。

### 2.5 测试程序与第三方交付边界（**定稿**）

| 事项 | `uvctest`（本仓库） | 第三方应用 |
|------|---------------------|------------|
| 读取 `pip_overlay_path`、多路媒体目录、H.264/YUV/JPEG 文件 | **负责** | **不负责**（由产品业务完成） |
| 解码/解析为 **NV12**（及缩放至合成约定尺寸） | **负责** | **负责** |
| 调用 `pip_helper` | 是，传入 **背图 + 主讲人 + N 路** **NV12** 缓冲区 | 同上 |
| 调用 `libmy_uvc` | **`my_uvc_submit_*`** | 同上 |

**结论**：凡 **「路径 / 码流 → NV12」** 的步骤均属 **应用层**；**`pip_helper` 只负责 NV12 上的叠加与 MJPEG 编码输出**，便于把 **两库** 独立交付给第三方。

---

## 3. 对外 API 设计方向（概念层）

### 3.1 上下文与配置

- **上下文**：`my_uvc_context_t *`，避免隐式全局状态。
- **配置结构体**：`my_uvc_config_t`（命名以头文件为准）与 **`[libmy_uvc]`** 对齐，在 **`my_uvc_create(&cfg)`**（或等价）时传入；**以结构体为准**，ini 仅为可选加载途径。
- **启停**：`start` / `stop` / `join`，语义与当前 `uvc_control_run` / `uvc_control_join` / `uvc_formats_deinit` 一致。

### 3.2 路由标识：**已确认** — 对外仅用 `channel_id`（§9 ①）

- **现状**：应用层使用 **逻辑通道序号** `channel_id ∈ [0, channels)`；设备侧在热插拔后会 **重映射** 为 **`video_id`**（`uvc_video_id_get`）。
- **ABI 定案**：**`my_uvc_submit_*` 仅接受 `channel_id`**；**`libmy_uvc` 内部**在每次提交路径上解析/跟踪当前 **`video_id`**（与现有 `channel_worker` 行为一致）。**不**对外暴露仅按 `video_id` 提交的并行 API（若未来需要，须新版本与扩展符号）。

### 3.3 帧提交：线程、缓冲区生命周期、错误语义

- **线程模型（建议默认）**：`my_uvc_submit_*` 为 **可重入安全** 或 **明确仅允许单线程调用**（二选一）；若为多线程，须在头文件中写清；**推荐 v1**：与现网一致，由 **每通道一个 worker 线程**调用 submit（与当前 `channel_worker` 一致）。
- **缓冲区生命周期（建议默认）**：`submit` **返回后**调用方即可 **释放/复用** 传入缓冲区；库在调用路径内 **同步完成拷贝** 或 **同步送到底层**（不持有应用指针跨帧）。若未来引入异步队列，须另增「释放回调」或明确队列深度，**v1 不引入异步所有权**。
- **错误码（建议）**：在头文件中定义 **枚举**（如 `MY_UVC_OK`、`MY_UVC_ERR_NOT_RUNNING`、`MY_UVC_ERR_NO_STREAM`、`MY_UVC_ERR_INVALID_ARG`）；**未 STREAMON** 时返回可区分码，便于应用退避（对应现有 `idle_sleep_ms` 轮询语义）。

### 3.4 日志：**已确认** — 不强制统一（§9 ②，选 A）

- **`[libmy_uvc]` 的 `log_level`**：仅约束 **`libmy_uvc.so` 内部**日志级别。
- **`my_uvc_create`**：可选传入 **`my_uvc_log_fn`（level + fmt + va_list）**；未设置时库 **写 stderr**。
- **`uvctest` / 集成应用**：**自行**实现统计、周期与业务日志（如现有 `log_msg`）；**不要求**与库共用同一回调；若产品希望一条日志总线，可在应用内 **自愿**把库回调与自有日志接到同一后端。

### 3.5 配置与 CLI 优先级（uvctest / 集成应用）

- **优先级（建议固定）**：**命令行参数 > 合并后的 ini > `default_app_config()` 默认值**。
- 与 **`[libmy_uvc]`** 相关的 CLI（如 `--channels`、`--codec`、`--size`）仅在 **应用层**解析；解析结果 **覆盖** 已从 ini 读入的 `MyUvcConfig` 对应字段，再 **`my_uvc_create`**。**库内不解析 CLI**。

### 3.6 PiP：`pip_enable` 与链接策略：**已确认**（§9 ③，选 A）

- **定案**：**`uvctest`（及参考集成方式）始终链接** `libmy_uvc_pip_helper.a` 与 **`libmy_uvc.so`**。
- **`pip_enable=0`**（`[libmy_uvc_pip]`）：**不调用**合成管线，直接按原媒体送 `libmy_uvc`；实现上为 **无 compose 快路径**（具体是否零拷贝由实现决定）。
- **CMake**：**不**以「裁掉 pip_helper」作为默认选项；若未来个别产品需极小体积，可另增 **非默认** 的编译开关，**不在 v1 主路径**。

### 3.7 PiP 上下文：**已确认** — 每 `channel_id` 独立实例（§9 ④，选 A）

- **ABI 定案**：**每个 `channel_id` 对应一个 `pip_helper` 句柄**（如 `pip_helper_create(channel_id, …)` / 或等价工厂），**不**使用全局单例承载多路 UVC 的合成状态。
- **多路**：各路背图、网格、主讲人状态 **相互隔离**；资源占用（RGA/MPP）由实现按需创建，**v1 不**引入跨通道共享单例。

### 3.8 PiP：配置、ini 与运行时 API（**定稿**）

**v1 像素格式（已定案）**：**`pip_helper` 合成接口只接受 NV12**。背图须为 **与画布同宽高** 的 NV12（或与头文件约定的对齐方式）；**主讲人** 与 **每路 grid tile** 为 **各自 NV12 平面**，尺寸与布局格一致或由 API 描述缩放目标（以 `pip_helper.h` 为准）。**UVC 侧为 MJPEG** 时：应用先 **解码 MJPEG → NV12**，合成后 **`pip_helper` 输出 MJPEG** 再 `my_uvc_submit_mjpeg`，整条链在像素域以 NV12 为主，与 RGA/MPP 常见快路径一致。

**v1 数据源间断（已定案）**：**主讲人** 与 **各路 grid** 在 **无新帧** 时 **沿用库内缓存的上一帧**；超过 **有效超时毫秒数**（**create 时** 由 **`pip_overlay_stale_timeout_ms`** 决定：**`-1`/缺省 → 5000**；**`0` → 禁用超时仅冻结**；**`>0` → 自定义**，见 §2.4）仍无该路新有效数据则 **该路露背图**。API 须能表达 **本帧某路是否提交新 NV12**（或等价约定），且 **`pip_helper` 在更新时拷贝** 以满足与 **§3.3** 一致的缓冲生命周期。

**分层**：

1. **第三方集成（目标）**  
   - **`pip_helper_create`（或等价）**：传入 **画布尺寸、JPEG 质量、主讲人矩形、网格 `n_tiles`/`gap_px`/`margin_px`**、**`pip_overlay_stale_timeout_ms`（`-1` = 库缺省 5000；`0` = 禁用超时；`>0` = 毫秒；§2.4）** 等参数（结构体字段以 `pip_helper.h` 为准）；与 **`config/libmy_uvc_pip.ini`** 中非负键语义对齐（ini **无键** → 5000）。  
   - **每帧合成调用**：传入 **背图**；**主讲人** 与各 tile 路按 API 提交 **新 NV12** 或 **本帧无更新**（触发 §2.4 冻结/超时逻辑）；**本帧 `n_active`** 与 **前 `n_active` 格** 的语义仍适用（**`n_active`≤`n_tiles`**，**余格露背图**）。**不**通过 `pip_overlay_path` 由库内读盘完成产品功能；**v1 不入参压缩 JPEG/H.264**。

2. **`uvctest` / `[libmy_uvc_pip]`（验证与示例）**  
   - **`pip_overlay_path`**、**`[uvctest]`** 下各通道媒体路径等：仅表示 **测试程序** 从哪里 **读文件**；读入后 **解码为 NV12**（及缩放），再拆成背图 / 主讲人 / N 路 **调用 `pip_helper`**。  
   - **验证便利**：允许 **同一张（或同一目录轮播）图** 同时喂给主讲人与 N 路，以简化素材；**不改变**「I/O 在 `uvctest`、合成在 `pip_helper`」的边界。

3. **旧键与网格参数（迁移）**  
   - **`pip_x` / `pip_y` / `pip_w` / `pip_h`**：**主讲人** 窗口（与现网单 PiP 行为对齐）。  
   - **网格**：**`n_tiles`（或等价）、`gap_px`、`margin_px`** 等以 **结构体 + 可选 ini 扩展键** 增加；**每帧 `n_active`** 仅 API 传入（ini 可只设 **`n_tiles` 上限** 供测试默认值）。  
   - **断流超时**：**`pip_overlay_stale_timeout_ms`** 置于 **`[libmy_uvc_pip]`**（非负；**缺省 5000**），映射入 **`pip_helper_config_t`** 时与 **create** 的 **`-1`/0/`>0`** 约定一致（见 §2.4）。**新键优先**，旧键仅主讲人；冲突时以 `pip_helper.h` / `libmy_uvc_pip.ini` 注释为准。

4. **`pip_overlay_path` 为目录（仅测试语义）**  
   - 在 **`uvctest`** 中可表示 **多帧素材序列**（如按文件名轮播），用于 **单路/主讲人** 或 **向多路复用同一素材** 的验证；**不**定义为「目录内文件自动对应网格第 k 路」的产品语义，除非将来单独增加 **`pip_overlay_path_mode`** 类键并在本文档修订。

（PiP 侧 **C API** 见 `include/my_uvc_pip/`；**布局纯函数**见 `pip_tile_layout.hpp`。）

### 3.9 可执行文件名：`uvctest`（**已更新**）

- **定案**：对外唯一应用入口名为 **`uvctest`**。CMake `install` 与 **`my_uvc_install_to_device.sh`** **不再**创建 **`/usr/bin/my_uvc`** 符号链接；历史脚本与文档中的 `my_uvc` 进程名请改为 **`uvctest`**（或 `pkill -f '/usr/bin/uvctest …'`）。
- **文档**：示例与测试清单均以 **`uvctest`** 为准。

---

## 4. 源码与目录结构（目标态）

```
my_uvc/
  include/my_uvc/my_uvc.h
  include/my_uvc_pip/pip_helper.h
  src/core/                 # libmy_uvc 实现
  src/pip_helper/           # libmy_uvc_pip_helper 实现
  src/uvctest/main.cpp      # 测试程序
  third_party/uvc/
  config/
    libmy_uvc.ini
    libmy_uvc_pip.ini
    uvctest.ini
    README_CONFIG.md
```

---

## 5. ABI、依赖与链接

- **`libmy_uvc.so`**：`pthread`、JPEG、MPP、RGA 等（与当前链路一致）；**SONAME** 建议 `libmy_uvc.so.1`。
- **`libmy_uvc_pip_helper.a`**：JPEG、MPP、RGA；**不**链接 `libmy_uvc`。
- **`uvctest`**：**始终**链接 **`libmy_uvc.so`** 与 **`libmy_uvc_pip_helper.a`**（见 §3.6）；`pip_enable=0` 时不走合成逻辑。
- **符号可见性**：`-fvisibility=hidden`，仅导出约定前缀 API。
- **对外头文件**：C 兼容。

---

## 6. CMake 与安装

- `add_library(my_uvc SHARED …)`、`add_library(my_uvc_pip_helper STATIC …)`、`add_executable(uvctest …)`；**默认** `uvctest` **同时**依赖上述二者（与 §3.6 定案一致）。
- `install(TARGETS …)`、`install(FILES include/… config/*.md …)`。
- 交叉编译与 `autobuild.sh` 不变；设备上 **`libmy_uvc.so`** 需在 `LD_LIBRARY_PATH` 或 `rpath` 中可见。

---

## 7. 后续重构实施路线（**定稿**，按顺序执行）

**子任务拆分、单元测试与集成测试清单**见 **[LIBMY_UVC_REFACTOR_TASKS.md](./LIBMY_UVC_REFACTOR_TASKS.md)**（编码前任务排期用）。

以下为 **配置拆分已完成** 之后的 **推荐顺序**。每阶段结束应 **可编译、可跑通与当前行为等价的场景**（或明确记录差异）。

### 阶段 0 — **已完成**｜配置与解析

- [x] 拆分 `libmy_uvc.ini` / `libmy_uvc_pip.ini` / `uvctest.ini`；目录加载不再回退 `my_uvc.ini`；profile 等单文件仍可用 `-c <file>`。
- [x] `load_app_config` 支持目录合并与区段校验；默认 `-c /userdata`（目录）。
- [x] `config/README_CONFIG.md`、安装脚本与 CMake `install` 同步。

**产出**：后续库文档可直接引用「各 ini 归属」。

### 阶段 1 — 抽出 **`libmy_uvc.so`（骨架）**

1. 新建 `add_library(my_uvc SHARED …)`，将 **vendor + `src/core`** 编入（`uevent_stub` 等随核心）。
2. 定义 **`my_uvc.h`**：`my_uvc_config_t`（映射 **`[libmy_uvc]`** 字段子集）、`my_uvc_create` / `destroy` / `start` / `stop`、`submit_*`。
3. 将现有 `main` 中对 `uvc_*` 的调用 **迁入库内实现**，`main` 暂仍链接 monolithic 可执行 **或** 先产出 **双目标**：`my_uvc`（旧）与 `uvctest`（新）二选一跑通。
4. **符号隐藏**、**SONAME**、**install** 到 `CMAKE_INSTALL_PREFIX`。

**验收**：仅链 `libmy_uvc` 的最小示例程序能 `init` + `submit` 一帧 MJPEG/H.264（与现有 gadget 行为一致）。

### 阶段 2 —  **`uvctest` 测试程序**

1. 将 **`src/main.cpp`** 重命名为 **`src/uvctest/main.cpp`**（或等价目录），目标名 **`uvctest`**。
2. 逻辑上：**仅** 保留读文件、解析、CLI、**`[uvctest]`** 配置；通过 **`libmy_uvc` 公开 API** 送帧。
3. **`app_config.cpp`** 保留在 **uvctest** 目标；`AppConfig` 可拆为 **`MyUvcConfig` + `PipHelperConfig` + `UvctestConfig`** 三个子结构体（可选，与 ini 三文件一一对应），或暂时保持聚合结构体但 **按区段填充**。
4. **应用入口**：仅 **`uvctest`**（见 §3.9）；**不**再提供 **`/usr/bin/my_uvc`** 符号链接。

**验收**：`uvctest` 在相同配置下行为一致（单路/多路、H.264/MJPEG、插拔）。

### 阶段 3 —  **`libmy_uvc_pip_helper.a`**

1. 将 **`pip_mjpeg` / `mpp_jpeg`** 等合成逻辑迁入 **`src/pip_helper/`**，产出 **静态库**。
2. 实现 §2.4 / §3.8：**主讲人 + 下三分之一网格（`n_tiles` + 每帧 `n_active`）**、**每帧 API 仅入参 NV12（背图/主讲人/前 `n_active` 路 tile）**、**MJPEG 输出**；**库内不以路径为产品接口**（读盘仅保留在 `uvctest` 或过渡期，须标注 deprecated）。
3. **`pip_helper` 结构体** 与 **`[libmy_uvc_pip]`** 中 **非路径** 字段对齐；**不**链接 `libmy_uvc`。
4. **`uvctest`**：**应用内读 H.264/YUV/JPEG** → **解码/缩放为 NV12** → **`pip_helper` 合成** → **`my_uvc_submit_mjpeg`**。

**验收**：PiP 开/关、**`n_active` 变更**、多路 tile、**断流冻结 / 超时露背图**（**`pip_overlay_stale_timeout_ms` 缺省与自定义、`0` 若支持**）、与 `libmy_uvc` 组合无死锁、无双重释放；第三方集成路径 **不依赖** `pip_helper` 打开媒体文件。

### 阶段 4 — 配置与文档收尾

1. 若库提供 **可选 ini 解析函数**，保证 **只读取各自区段**，与 `config/*.ini` 注释一致。
2. 更新 **`docs/README.md`**、**`HANDOVER.md`**、**`my_uvc_install_to_device.sh`**：推送 **`libmy_uvc.so`**、头文件、**`uvctest`**（见 §3.9）；配置文件保持分文件推送。
3. **可执行文件名**：仅 **`uvctest`**（§3.9）。

### 阶段 5 — 硬指标与回归

- 线程与锁顺序与现网一致；USB 插拔、多路 **`video_id` 重映射**（与 §3.2 **`channel_id` 对外语义**一致）。
- 设备上 **`ldd uvctest`**、**`ldd libmy_uvc.so`** 检查依赖。
- **发布门禁**：以 **`docs/TEST_CHECKLIST_CN.md`** 为主线回归；示例需与 **`-c /userdata`（目录）+ 分文件 ini** 对齐；关键场景（单路/多路、H.264/MJPEG、PiP、插拔）**建议作为 release 必跑**。

---

## 8. 风险与测试要点

- **线程与锁**：`channel_worker` 与 `uvc_control` 的协作顺序不变；**库内** `stop` 须与 `join` 成对。
- **PiP**：**背图** 与 **每帧合成** 对齐；**主讲人 / tile** 支持 **断流冻结上一帧** 与 **可配置超时露背图**（§2.4），须验证 **单调时钟**、多路 **独立超时**、**`pip_overlay_stale_timeout_ms`** 与 **`n_active`** 组合无错乱；**`pip_helper` 更新层时拷贝 NV12** 与 **§3.3** 缓冲生命周期一致。**方案 B** 下应用负责 **同时链接** 两库、**应用侧读盘与解码至 NV12** 与 **`pip_helper` → `libmy_uvc` 送帧顺序**。
- **配置**：**结构体入参为主、ini 为辅**；避免 `libmy_uvc` 与 `uvctest` 各读一半导致覆盖顺序混乱。
- **ABI**：`libmy_uvc.so` 升级遵循 SONAME；对外结构体加 **版本字段** 或 **sizeof 校验**（可选）。

---

## 9. 待确认事项（**已全部确认**）

以下 **①～⑤** 已定案并回填 **§3 / §5 / §6 / §7 / §3.9**；若产品变更策略，须走文档修订并升 **API/文档版本**。

| ID | 主题 | 定案 |
|----|------|------|
| ① | **`my_uvc_submit_*` 使用 `channel_id` 还是 `video_id`？** | **`channel_id` 对外，库内解析 `video_id`**（选 A） |
| ② | **日志** 是否强制统一 `log_fn`？ | **不强制**；库可选回调/stderr，应用自管日志（选 A） |
| ③ | **`pip_enable=0` 时是否仍链接 pip_helper？** | **始终链接**（选 A） |
| ④ | **pip_helper** 实例粒度？ | **每 `channel_id` 一实例**（选 A） |
| ⑤ | **`my_uvc` 与 `uvctest` 命名策略？** | **长期并存、不设弃用截止**；`my_uvc` 为兼容入口（选 A） |

---

## 10. 文档修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 草案 v0.1–v0.3 | 2026-04-17 | PiP 方案、网格策略、配置拆分前讨论 |
| **v1.0 定稿** | 2026-04-17 | **定稿**：纳入配置拆分落地、**阶段 0–5** 实施路线、配置与三库映射策略、与 `config/README_CONFIG.md` 交叉引用 |
| **v1.1** | 2026-04-17 | **完善**：§3 扩展（路由 ID、线程/缓冲/错误码、日志、CLI 优先级、pip 链接与实例）；§3.8 旧 PiP 键迁移；§9 待确认表；阶段 5 与 TEST_CHECKLIST 门禁 |
| **v1.2** | 2026-04-17 | **§9 ① 确认**：对外 `submit` 仅用 **`channel_id`**，库内解析 **`video_id`** |
| **v1.3** | 2026-04-17 | **§9 ② 确认**：日志 **不强制统一**（库可选 `log_fn`/stderr；uvctest 自管日志） |
| **v1.4** | 2026-04-17 | **§9 ③ 确认**：**uvctest 始终链接** `libmy_uvc_pip_helper`；`pip_enable=0` 时不合成 |
| **v1.5** | 2026-04-17 | **§9 ④ 确认**：**每 `channel_id` 一个 `pip_helper` 实例**，非全局单例 |
| **v1.6** | 2026-04-17 | **§9 ⑤ 确认**：**`my_uvc` 与 `uvctest` 长期并存**（不设弃用截止）；新增 **§3.9**；§9 表改为已全部确认 |
| **v1.7** | 2026-04-21 | **PiP 交付边界**：§1.2/§2.2/§2.3/§2.4/§2.5 明确 **第三方仅收两库 + API**；**路径/H.264/YUV/JPEG 读盘与解码均在应用（`uvctest` 为示例）**；**主讲人独立 + 下三分之一动态 N 路（≤16）** 经 **每帧缓冲区** 传入 `pip_helper`；重写 **§3.8**；§7 阶段 3、§8 PiP 风险对齐 |
| **v1.8** | 2026-04-21 | **v1 像素格式定案**：**`pip_helper` 合成 API 仅接收 NV12**（背图/主讲人/tile）；**不接受压缩帧入参**；§1.2、§2.3–§2.5、§3.8 首部、§7 阶段 3、§8、文末结论对齐 |
| **v1.9** | 2026-04-21 | **网格有效路数**：每帧 **`n_active`（≤`n_tiles`）**，仅前 **`n_active`** 格叠 tile，**余格露背图**；不要求无效路 NV12；§2.4、§3.8、§8 对齐 |
| **v1.10** | 2026-04-21 | **断流策略**：**主讲人 + 各 grid 路** 无新数据时 **沿用库内上一帧**；超时后 **该路露背图**；`pip_helper` **拷贝**缓存、单调时钟；§2.4、§3.8、§7 阶段 3 验收、§8 |
| **v1.11** | 2026-04-21 | **断流超时可配置（选 C）**：**`pip_overlay_stale_timeout_ms`** 在 **`[libmy_uvc_pip]`** 与 **`pip_helper_config_t`/`pip_helper_create`** 同步，**缺省 5000**；**`0` = 不因超时露背图**（仅冻结）；§1.2、§1.3、`libmy_uvc_pip.ini`、§2.4、§3.8、§7、§8、结论 |
| **v1.12** | 2026-04-21 | **问题 6 选 A**：**create 未自定义时与 ini 一致按 5000 ms**；**C API 用 `-1`（或宏）表示库缺省**，**`0` 仍为禁用超时**；**`memset(0)` 陷阱** 写入 §2.4 / §3.8 |

---

**结论**：配置文件已按 **libmy_uvc / pip_helper / uvctest** 职责拆分；**§9 ①～⑤ 已全部定案**。**PiP 产品形态**以 **§2.4 / §2.5 / §3.8（v1.12）** 为准：**合成在 `pip_helper`（**NV12 入、MJPEG 出**；**`n_active`**；**断流冻结 / 可配置超时**；**`-1`/缺省 → 5000 ms**），**信源、解码与文件 I/O 在集成应用**（`uvctest` 仅验证）。**后续编码**请按 **§7 阶段 1→2→3** 推进并实现与 **v1.12** 一致的 API 与 ini，**阶段 4–5** 与发布、文档、回归同步完成。
