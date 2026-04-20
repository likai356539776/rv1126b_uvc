# libmy_uvc 重构任务拆分与测试要求

本文档在 **[LIBMY_UVC_REFACTOR_DESIGN.md](./LIBMY_UVC_REFACTOR_DESIGN.md)**（架构定稿）基础上，将工作拆为 **可执行子任务**，并规定 **单元测试** 与 **集成测试** 的触发时机。编码前应以本文档为 **任务清单**；任务完成时在对应条目打勾或链接 PR/issue。

---

## 1. 测试策略（总则）

| 类型 | 定义 | 典型时机 | 环境 |
|------|------|----------|------|
| **单元测试** | 对 **单模块、无硬件依赖或可 mock** 的逻辑做自动化验证（解析、布局计算、NAL 切分纯函数等） | **每个子任务提交前**；至少覆盖本任务新增/修改代码的关键分支 | 优先 **宿主机 (x86) + CMake/CTest**；纯逻辑可在 CI 跑 |
| **集成测试** | **多模块串联** 或与 **真实/模拟 UVC gadget、板端 MPP/RGA** 联调 | **每个阶段（Phase）收尾**；**模块合并到主分支前** | **目标板 RV1126** 或等价环境；部分可用 `v4l2loopback`/脚本模拟（若项目已有） |

**约定**

- **子任务完成** = 代码合并 + **该子任务所列单元测试通过**（若适用）。
- **阶段完成** = 该阶段全部子任务完成 + **阶段集成测试通过**（见各 Phase 末尾「阶段闸口」）。
- 无法在宿主机跑的测试（如依赖 `/dev/video*`）在文档中标注 **「仅板测」**，并归入 **集成测试** 与 `docs/TEST_CHECKLIST_CN.md` 条目对应。

### 1.1 用例文件与目录约定

| 目录 | 用途 |
|------|------|
| **`tests/unit/`** | 宿主机可编译运行的 **C++ 单测源文件**（`test_*.cpp`），由 CMake `add_executable` + CTest 注册 |
| **`tests/integration/`** | **shell 脚本**（`*.sh`）或板端说明；依赖硬件的脚本名带 `board_` 前缀 |
| **`tests/fixtures/config/`** | （可选）最小 ini 样例，供 `load_app_config` / 解析单测复用 |

下表各 **测试 ID** 均给出 **建议实现的文件名**（相对仓库根 `my_uvc/`）；实现时可微调，但应保持 **ID ↔ 文件** 可追踪。

---

## 2. 阶段 0 — 配置与解析（**已完成**）

| 任务 ID | 子任务 | 状态 | 单元测试建议 | 集成测试建议 |
|---------|--------|------|--------------|--------------|
| **P0-T1** | 拆分 `libmy_uvc.ini` / `libmy_uvc_pip.ini` / `uvctest.ini`，保留 `my_uvc.ini` | 完成 | — | 手动：`-c` 目录与单文件加载 |
| **P0-T2** | `load_app_config`：目录合并、区段校验、legacy 回退 | 完成 | **`tests/unit/test_app_config_ini_merge.cpp`**（样例 ini → `AppConfig` 断言） | **`tests/integration/board_config_directory_load_smoke.sh`**（板端；可选宿主机测目录存在性） |
| **P0-T3** | `config/README_CONFIG.md`、CMake `install`、安装脚本推送多文件 | 完成 | — | **`tests/integration/board_userdata_config_present_smoke.sh`**（部署后检查） |

**阶段闸口（已完成）**：分文件配置在设备上可被应用读取；与 `TEST_CHECKLIST` 中配置相关步骤不冲突。

---

## 3. 阶段 1 — `libmy_uvc.so` 骨架

### 3.1 子任务列表

| 任务 ID | 子任务 | 依赖 | 交付物 |
|---------|--------|------|--------|
| **P1-T1** | CMake：`add_library(my_uvc SHARED …)`，收录 `third_party/uvc/*.c`、`uevent_stub.c`，导出目标 `my_uvc::my_uvc` | P0 | 可链接的 `libmy_uvc.so`（或先 `OBJECT` 再合并） |
| **P1-T2** | 新增 `include/my_uvc/my_uvc.h`：定义 `my_uvc_config_t`（与 `[libmy_uvc]` 对齐字段）、错误码枚举、`my_uvc_log_fn` | P1-T1 | 头文件 + 版本宏（如 `MY_UVC_API_VERSION`） |
| **P1-T3** | 实现 `my_uvc_create` / `destroy`：拷贝配置、注册可选 log 回调、**不**在 create 里隐式起线程 | P1-T2 | 可重复 create/destroy 无泄漏（Valgrind/ASan 可选） |
| **P1-T4** | 实现 `my_uvc_start` / `stop` / `join`：内部调用 `uvc_control_run` / `uvc_control_join`、`uvc_formats_init/deinit`、`setenv("UVC_CNT",…)` | P1-T3 | 与现有生命周期一致 |
| **P1-T5** | 实现 `my_uvc_submit_mjpeg` / `my_uvc_submit_h264`（命名以头文件为准）：**参数为 `channel_id`**，内部解析 `uvc_video_id_get(channel_id)` 再 `uvc_read_camera_buffer_by_id` | P1-T4 | 与 §9 ① 定案一致 |
| **P1-T6** | 从当前 `main.cpp` **抽出**对 `uvc_*` 的调用至 `src/core/`（或等价），**vendor 不改或最小封装** | P1-T5 | 逻辑集中，便于单测 mock（若后续加） |
| **P1-T7** | **符号可见性**：`-fvisibility=hidden`，导出表仅 `my_uvc_*`；**SONAME** `libmy_uvc.so.1` | P1-T1 | `nm -D` / `readelf` 检查 |
| **P1-T8** | `install(TARGETS my_uvc …)`、`install(FILES my_uvc.h …)`；`autobuild.sh` 能编出 `.so` | P1-T7 | 安装树 `lib/`、`include/my_uvc/` 存在 |
| **P1-T9** | **最小示例** `examples/my_uvc_minimal`（或 `uvctest` 之前的临时目标）：只链 `libmy_uvc`，启动 + submit 一帧（可硬编码 buffer） | P1-T5 | 验证对外 ABI |

### 3.2 单元测试（阶段 1）

| 测试 ID | 用例文件 | 内容 | 关联任务 |
|---------|----------|------|----------|
| **P1-U1** | `tests/unit/test_my_uvc_config_mapping.cpp` | **`[libmy_uvc]` → `my_uvc_config_t`** 字段映射与边界值 | P1-T2 |
| **P1-U2** | `tests/unit/test_my_uvc_submit_errors.cpp` | **错误码**：非法 `channel_id`、NULL、`submit` 在 `stop` 之后等 | P1-T5 |
| **P1-U3** | `tests/unit/test_my_uvc_channel_video_stub.cpp` | （已完成）**Mock** `uvc_video_id_get`（`my_uvc_test_set_video_id_hook`），验证 `submit(channel_id)` 路由 | P1-T5 |

### 3.3 集成测试（阶段 1 闸口）

| 测试 ID | 用例文件 | 内容 |
|---------|----------|------|
| **P1-I1** | `tests/integration/board_libmy_uvc_submit_smoke.sh` | 板端：`create`→`start`→`submit` 一帧→`stop`/`join`/`destroy`；主机预览或 `v4l2-ctl`（与 `TEST_CHECKLIST` 对齐） |
| **P1-I2** | `tests/integration/board_usb_replug_channel_submit.sh` | USB 拔插后同 **`channel_id`** 重试 **submit**，验证内部 **`video_id`** 重映射 |
| **P1-I3** | `tests/integration/check_libmy_uvc_soname_exports.sh` | **`ldd` / `readelf` / `nm -D`**：依赖与导出符号检查（可在 SDK 环境跑） |

**阶段 1 完成定义**：P1-T1～T9 完成 + P1-U1～U2 通过 + P1-I1～I3 通过。

---

## 4. 阶段 2 — `uvctest` 应用入口

### 4.1 子任务列表

| 任务 ID | 子任务 | 依赖 | 交付物 |
|---------|--------|------|--------|
| **P2-T1** | 目录调整：`src/uvctest/main.cpp`（或 `src/main.cpp` 迁移），`add_executable(uvctest …)`，链接 `libmy_uvc` + `libmy_uvc_pip_helper`（pip 可先空实现，见阶段 3） | P1 | 可执行文件 `uvctest` |
| **P2-T2** | 将原 `main` 中送帧路径改为 **`my_uvc_submit_*`**，删除对 vendor 符号的直接依赖（仅经库） | P2-T1 | 源码边界清晰 |
| **P2-T3** | `app_config.cpp` 仅被 `uvctest` 编译；**可选** 拆 `MyUvcConfig` / `PipHelperConfig` / `UvctestConfig` | P2-T1 | 与三 ini 区段一致 |
| **P2-T4** | **安装**：`uvctest` → `bin`（**无** `my_uvc` 兼容链接；与 §3.9 一致） | P2-T1 | 符合 §3.9 |
| **P2-T5** | 更新 `my_uvc_install_to_device.sh`：推送 `libmy_uvc.so`、`uvctest`、头文件与 config | P2-T4 | 板端路径正确 |
| **P2-T6** | 文档：`docs/README.md` 中命令示例 **仅** **`uvctest`** | P2-T4 | — |

### 4.2 单元测试（阶段 2）

| 测试 ID | 用例文件 | 内容 | 关联任务 |
|---------|----------|------|----------|
| **P2-U1** | `tests/unit/test_uvctest_cli_overrides_ini.cpp` | **CLI 覆盖 ini**：模拟 argv，`channels`/`codec` 等覆盖合并后配置（§3.5） | P2-T3 |
| **P2-U2** | `tests/unit/test_app_config_path_directory_vs_file.cpp` | **`-c` 目录 vs 单文件**，`load_app_config` 结果一致（可与 P0 共用逻辑） | P0/P2 |

### 4.3 集成测试（阶段 2 闸口）

| 测试 ID | 用例文件 | 内容 |
|---------|----------|------|
| **P2-I1** | `tests/integration/board_uvctest_parity_regression.sh` | **`uvctest`** 与重构前二进制在 **同 ini + CLI** 下 **单路/多路、H.264、MJPEG** 行为一致 |
| **P2-I2** | （已由 **P4** 统一入口替代）原「`my_uvc` symlink」检查 — **删除兼容名** 后以 `doc_deploy_walkthrough_smoke.sh` / `uvctest` 为准 |
| **P2-I3** | `tests/integration/board_usb_multichannel_remap.sh` | 多路 **`video_id`** 变化；对齐 **`docs/TEST_CHECKLIST_CN.md`** 相关条 |

**阶段 2 完成定义**：P2-T1～T6 + P2-U1～U2 + P2-I1～I3。

---

## 5. 阶段 3 — `libmy_uvc_pip_helper.a`

### 5.1 子任务列表

| 任务 ID | 子任务 | 依赖 | 交付物 |
|---------|--------|------|--------|
| **P3-T1** | `add_library(my_uvc_pip_helper STATIC …)`，`src/pip_helper/`，**不**链接 `libmy_uvc` | P2 | `libmy_uvc_pip_helper.a` |
| **P3-T2** | `include/my_uvc_pip/pip_helper.h`：`pip_helper_create(channel_id, …)`、destroy、**每 channel 一实例**（§9 ④） | P3-T1 | API 定稿 |
| **P3-T3** | 迁移/重构 `pip_mjpeg` / `mpp_jpeg` 至 pip_helper；背图 JPEG/YUV/H.264、主讲人、下三分之一网格（§2.4） | P3-T2 | 功能与定稿一致 |
| **P3-T4** | 输出 **MJPEG 帧**缓冲 + 长度；由应用调用 `my_uvc_submit_mjpeg(channel_id, …)` | P3-T3 | 方案 B 边界 |
| **P3-T5** | `pip_enable=0`：**不调用** compose，快速返回或 no-op（§9 ③） | P3-T4 | 无多余编码 |
| **P3-T6** | 配置：`PipHelperConfig` 与 `[libmy_uvc_pip]` 对齐；旧键映射主讲人矩形（§3.8） | P3-T2 | ini 与头文件注释同步 |
| **P3-T7** | `uvctest`：PiP 路径 **compose → submit**；非 PiP 路径绕过 pip_helper | P3-T5 | 端到端 |

### 5.2 单元测试（阶段 3）

| 测试 ID | 用例文件 | 内容 | 关联任务 |
|---------|----------|------|----------|
| **P3-U1** | `tests/unit/test_pip_tile_layout_rects.cpp` | **布局纯函数**：`out_w/h`、`N_tile`、gap → 各 tile 像素矩形 | P3-T3 |
| **P3-U2** | `tests/unit/test_pip_helper_config_defaults.cpp` | **`PipHelperConfig`** 缺省键与 `default_*` 一致 | P3-T6 |
| **P3-U3** | `tests/unit/test_pip_compose_offline_golden.cpp` | （已完成）宿主机 **NV12 尺寸 + JPEG 字节 fixture 校验**（无 MPP/RGA 全链路 golden） | P3-T3 |

### 5.3 集成测试（阶段 3 闸口）

| 测试 ID | 用例文件 | 内容 |
|---------|----------|------|
| **P3-I1** | `tests/integration/board_pip_on_off_compare.sh` | **PiP 开/关** 主机预览对比；关时与无 pip 一致 |
| **P3-I2** | `tests/integration/board_pip_per_channel_isolation.sh` | 多 **`channel_id`**，pip 状态 **不串扰**（§9 ④） |
| **P3-I3** | `tests/integration/board_pip_longrun_stress.sh` | **长稳**：泄漏/死锁（可与 **P5** 合并执行） |

**阶段 3 完成定义**：P3-T1～T7 + P3-U1～U2（+U3 可选）+ P3-I1～I3。

---

## 6. 阶段 4 — 配置 API、安装与文档

| 任务 ID | 子任务 | 单元测试 | 集成测试 |
|---------|--------|----------|----------|
| **P4-T1** | （已完成）`load_app_config_section_from_file`（C++）+ **`my_uvc_load_ini_section_only`**（C，`my_uvc.h`） | **`tests/unit/test_my_uvc_ini_section_loader.cpp`** + **`tests/unit/test_my_uvc_load_ini_section_c_api.cpp`** | **`tests/integration/board_ini_loader_parity_smoke.sh`** |
| **P4-T2** | （已完成）安装布局、**C API** `my_uvc_load_ini_section_only`：`HANDOVER.md` §4、`docs/README.md` §6、`config/README_CONFIG.md` | — | **`tests/integration/doc_deploy_walkthrough_smoke.sh`**（`check`/`board`） |
| **P4-T3** | （已完成）`select_profile.sh` **仅**输出 **`uvctest`**；**`board_select_profile_uvc_binary_smoke.sh`** 宿主机/板端检查 | — | **`tests/integration/board_select_profile_uvc_binary_smoke.sh`** |

**阶段闸口**：文档与安装脚本与 **§7 阶段 4** 定稿一致；应用入口 **`uvctest`**（不再安装 **`my_uvc`** 可执行兼容名）。

---

## 7. 阶段 5 — 全量回归与发布门禁

| 任务 ID | 子任务 | 用例文件 / 说明 |
|---------|--------|-------------------|
| **P5-T1** | （已完成，2026-04-17）**`docs/TEST_CHECKLIST_CN.md`** + 记录 **`tests/integration/record_release_regression.md`** | 见该文件汇总表 |
| **P5-T2** | （已完成，宿主机）**分文件 ini 目录** vs **单文件 `my_uvc.ini`** — CTest **`test_app_config_path_directory_vs_file`**；可单独 **`board_config_split_vs_monolith_parity.sh`** | 已纳入 **`scripts/run_p5_host_smoke.sh`** |
| **P5-T3** | （已完成，2026-04-17）交叉编译 **`build-rv1126b`**：**`check_uvctest_and_lib_deps.sh`** + **`check_libmy_uvc_soname_exports.sh`** | 见 **`record_release_regression.md`** |
| **P5-T4** | （已完成）CI：**`.github/workflows/my_uvc_unit_tests.yml`**；本地：**`scripts/run_unit_tests_host.sh`**；合并入口：**`scripts/run_p5_host_smoke.sh`** |

**阶段闸口**：**P5-T1** 通过 + 无未关闭阻塞 issue。（**P5-T1 / P5-T3** 已于 2026-04-17 记录于 **`record_release_regression.md`**。）

---

## 8. 任务依赖总览（简图）

```
P0 ──► P1（libmy_uvc.so）──► P2（uvctest）
                              │
                              ▼
                         P3（pip_helper）
                              │
                              ▼
                         P4（文档/安装收尾）──► P5（回归门禁）
```

---

## 9. 文档维护

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-04-17 | 首版：阶段 0～5 子任务 + 单元/集成测试要求 |
| v1.1 | 2026-04-17 | **§1.1** 用例文件与目录约定；各 **P\*-U\*/I\*** 建议 **文件名** |
| v1.2 | 2026-04-17 | **§7 P5**：`run_p5_host_smoke.sh`、`record_release_regression.md` 更新 |
| v1.3 | 2026-04-17 | **P5-T1 / P5-T3** 已在 **`record_release_regression.md`** 登记通过 |

后续若裁剪范围或增加 CI 任务，请同步更新本文档与 **[LIBMY_UVC_REFACTOR_DESIGN.md](./LIBMY_UVC_REFACTOR_DESIGN.md)** 的 §7。

---

## 10. 用例文件名一览（速查）

路径均相对于仓库 **`my_uvc/`** 根目录。

### 单元测试（`tests/unit/*.cpp`）

| 文件 | 测试 ID |
|------|---------|
| `tests/unit/test_app_config_ini_merge.cpp` | P0-T2 补测 |
| `tests/unit/test_my_uvc_config_mapping.cpp` | P1-U1 |
| `tests/unit/test_my_uvc_submit_errors.cpp` | P1-U2 |
| `tests/unit/test_my_uvc_channel_video_stub.cpp` | P1-U3（可选） |
| `tests/unit/test_uvctest_cli_overrides_ini.cpp` | P2-U1 |
| `tests/unit/test_app_config_path_directory_vs_file.cpp` | P2-U2 |
| `tests/unit/test_pip_tile_layout_rects.cpp` | P3-U1 |
| `tests/unit/test_pip_helper_config_defaults.cpp` | P3-U2 |
| `tests/unit/test_pip_compose_offline_golden.cpp` | P3-U3（可选） |
| `tests/unit/test_my_uvc_ini_section_loader.cpp` | P4-T1（可选 API） |
| `tests/unit/test_my_uvc_load_ini_section_c_api.cpp` | P4-T1（C API） |

### 集成测试（`tests/integration/*.sh` 等）

| 文件 | 测试 ID |
|------|---------|
| `tests/integration/board_config_directory_load_smoke.sh` | P0-T2 |
| `tests/integration/board_userdata_config_present_smoke.sh` | P0-T3 |
| `tests/integration/board_libmy_uvc_submit_smoke.sh` | P1-I1 |
| `tests/integration/board_usb_replug_channel_submit.sh` | P1-I2 |
| `tests/integration/check_libmy_uvc_soname_exports.sh` | P1-I3 |
| `tests/integration/board_uvctest_parity_regression.sh` | P2-I1 |
| （已弃用）`board_my_uvc_symlink_parity.sh` | 由 **`doc_deploy_walkthrough_smoke.sh`** / **`uvctest`** 替代 |
| `tests/integration/board_usb_multichannel_remap.sh` | P2-I3 |
| `tests/integration/board_pip_on_off_compare.sh` | P3-I1 |
| `tests/integration/board_pip_per_channel_isolation.sh` | P3-I2 |
| `tests/integration/board_pip_longrun_stress.sh` | P3-I3 |
| `tests/integration/board_ini_loader_parity_smoke.sh` | P4-T1 |
| `tests/integration/doc_deploy_walkthrough_smoke.sh` | P4-T2（可选） |
| `tests/integration/board_select_profile_uvc_binary_smoke.sh` | P4-T3 |
| `tests/integration/record_release_regression.md` | P5-T1 记录 |
| `tests/integration/board_config_split_vs_monolith_parity.sh` | P5-T2 |
| `tests/integration/check_uvctest_and_lib_deps.sh` | P5-T3 |
| `scripts/run_unit_tests_host.sh` 或 `.github/workflows/my_uvc_unit_tests.yml` | P5-T4（可选 CI） |
| `scripts/run_p5_host_smoke.sh` | P5 宿主机合并门禁（T4 + T2 说明 + P4 脚本 check） |

---

**使用方式**：按 **P1 → P2 → P3** 顺序排期；每个 **任务 ID** 可拆为 issue；**阶段闸口**未通过则 **不合并** 下一阶段默认分支（或打 `WIP` 标签）。
