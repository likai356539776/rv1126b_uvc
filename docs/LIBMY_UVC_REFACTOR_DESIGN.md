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

1. **`libmy_uvc.so`**：承载 **UVC 协议栈 + 送帧**，对外 **稳定 C 风格 API**（内部可 C++）。
2. **`uvctest`**：仅作 **测试/示例程序**——读媒体文件、解析码流、CLI；通过 API 向 `libmy_uvc`（及可选 `pip_helper`）喂数据。
3. **`libmy_uvc_pip_helper.a`**（方案 B）：**协议栈外** 完成背图 + 主讲人 + 网格 PiP 合成，输出 MJPEG 再交给 `libmy_uvc`。
4. **构建产物**：`install` 下 **`lib/`、`include/`、`bin/`**；脚本与设备部署规则与 CMake / `my_uvc_install_to_device.sh` 对齐并演进。

### 1.3 已落地项：配置文件拆分（**重构前置条件**）

以下已在当前仓库实现，**后续库化与 API 设计应与之对齐**，避免再回到「单文件混杂职责」：

| 文件 | 区段 | 归属（文档/职责） |
|------|------|-------------------|
| `config/libmy_uvc.ini` | `[libmy_uvc]` | 将来 **仅由 `libmy_uvc` 文档解释**；对应 UVC 协商、路数、分辨率、编码类型、H.264 发送策略、`idle_sleep_ms`、库侧日志级别等。 |
| `config/libmy_uvc_pip.ini` | `[libmy_uvc_pip]` | 将来 **仅由 `libmy_uvc_pip_helper` 文档解释**；对应 PiP 开关、矩形、质量等。 |
| `config/uvctest.ini` | `[uvctest]` | 将来 **仅由测试程序 / 应用示例文档解释**；对应媒体路径、`channelN_*`、统计与周期日志等。 |
| `config/my_uvc.ini` | `[my_uvc]` | **兼容旧部署** 的单文件；仍支持 `-c <file>`。 |

**加载规则**：`-c` 指向 **目录** 时依次合并 `libmy_uvc.ini` → `libmy_uvc_pip.ini` → `uvctest.ini`；若无分文件则回退 `my_uvc.ini`。解析与区段约束见 `app_config.cpp`。

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

- 本地/目录读取、Annex B / MJPEG 帧准备、`channel_worker` 等循环逻辑（可逐步改为「调库 API」的薄循环）。
- CLI、**`[uvctest]`** 相关配置、统计线程（若保留为示例行为）。
- 可选链接 **`libmy_uvc_pip_helper`**，在送 UVC 前完成合成。

### 2.3 PiP：方案 B + `libmy_uvc_pip_helper.a`

| 组件 | 职责 |
|------|------|
| **`libmy_uvc.so`** | 仅协议栈 + 送帧；**不**做 PiP。 |
| **`libmy_uvc_pip_helper.a`** | 背图 + 主讲人 + 网格；输出 MJPEG；**不**链接 `libmy_uvc.so`。 |
| **应用** | 链接两者；合成后调用 `my_uvc_submit_*`。 |

### 2.4 `libmy_uvc_pip_helper.a` 功能（已定稿）

与 §2.3–§2.4 细化一致：**主讲人区 + 下三分之一最多 16 路网格**（≤8 单行、>8 双行、间隙可配）、**每路 JPEG**、**背图 JPEG/YUV/H.264**、**合成后 MJPEG 交 `libmy_uvc`**。实现细节以 **pip_helper 头文件** 为准。

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

### 3.8 PiP 配置键演进（旧 ini → 新网格模型）

- 当前 **`[libmy_uvc_pip]`** 仍为 **单 overlay 矩形**（`pip_x/y/w/h` 等），与 §2.4 **主讲人 + 下三分之一网格** 并存时，迁移期约定：
  - **v1 pip_helper**：旧键 **映射为「主讲人」窗口**（与现网 `pip_mjpeg` 行为一致）；**网格路数、间隙、区域** 等新参数以 **扩展键** 或 **第二版结构体字段** 增加（实现时在 `pip_helper.h` 与 `libmy_uvc_pip.ini` 注释中并列说明）。
  - 若某键与新键冲突，**以文档化优先级为准**（建议：**新键优先**，旧键仅当新键未出现时生效）。

（PiP 侧详细 API 见 `include/my_uvc_pip/`，与 **`[libmy_uvc_pip]`** 对齐。）

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

- [x] 拆分 `libmy_uvc.ini` / `libmy_uvc_pip.ini` / `uvctest.ini`，保留 `my_uvc.ini` 兼容。
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
2. 实现 §2.4 已定策略：**主讲人 + 下三分之一网格**、背图多格式、**MJPEG 输出**。
3. **`pip_helper` 配置** 与 **`[libmy_uvc_pip]`** 对齐；**不**链接 `libmy_uvc`。
4. **`uvctest`**：在 PiP 开启路径上 **先 compose 再 `my_uvc_submit_mjpeg`**。

**验收**：PiP 开/关、多路 tile、与 `libmy_uvc` 组合无死锁、无双重释放。

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
- **PiP**：背图与 tile 的 **时间对齐**、**N_tile** 变更边界；**方案 B** 下应用负责 **同时链接** 两库并正确 **送帧顺序**。
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

---

**结论**：配置文件已按 **libmy_uvc / pip_helper / uvctest** 职责拆分；**§9 ①～⑤ 已全部定案**。**后续编码**请按 **§7 阶段 1→2→3** 顺序推进，**阶段 4–5** 与发布、文档、回归同步完成。
