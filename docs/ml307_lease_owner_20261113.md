# ML307 候选租约回收修正（2026-11-13）

本批只修改新候选目录中的 `ml307_tls_stream.h` 和 `ml307_connection_owner.cc`。正式固件 main、板型注册表和整板构建账本没有修改；八个 ML307 变体继续待适配。候选以 `evidence/device-websocket-network-20261112/candidate` 为基线，包含此前网络就绪和设备 HTTP／WebSocket 接入工作。

## 原因与改动

原逻辑把租约 owner 保存为 `shared_ptr<void>`。如果 A 租出的流被误交给 B 的 `Retire()`，B 会先移走租约，发现流不属于自己后结束调用并析构流。A 的 Active 槽和裸指针没有经过正常退役流程，留下占用状态和失效指针。此次源码审查没有观察到运行中的失效指针解引用，也没有硬件复现。

候选改用 `shared_ptr<Ml307ConnectionOwner>` 保存实际 owner。`Retire()` 在移动租约或取得任何 owner 锁之前，先检查实际归属；遇到其它 owner 的流，持有其强引用并转交实际 owner。这样仍由原 owner 校验槽与指针、执行现有 Close，并在关闭未确认时保留资源。转交时不持有 B 的锁，避免跨 owner 嵌套加锁。

正常退役仍会移走流上的强引用，防止 owner→待回收流→owner 的引用环；函数内的强引用持续到退役锁释放之后。无租约或槽不匹配仍要求会话复位，不释放编号供复用。每会话六个编号只使用一次、调用方先停止并 join 所有使用者、禁止 UART 回调内析构等原约束继续生效。本批没有实现连接耗尽恢复或完整协议停止／重启。

## 验证范围

使用现有 ESP-IDF 5.5.6-dirty、Xtensa 14.2.0、现有本地缓存及 offline_guard，新建隔离工程 `I:/xy_board_builds_20261109/ml307_lease_owner_20261113_r2`。候选 ML307、配套 UHCI 和 SDK tcp_transport 都从本批源码复制，设备 network、config、provisioning、HTTP 和 WebSocket 源码以 ML307 宏编译。空 `app_main()` 不执行 API 链接根；单 factory／4MB／无 PSRAM 是组件工程配置，不能代替正式蜂窝板分区或整机资源验证。

首次命令误传 `idf.py -j 2`，SDK 返回退出码 2，未开始配置或编译。原记录保存在 `invalid-jobs-build-attempt.json` 和对应日志；后续使用 SDK 支持的 `IDF_PY_BUILD_JOBS=2`，另建 r2 工程，保留失败目录。

实际命令与结果由本批 `build-attempt.json`、`final-build.json`、`commands.json`、`link-inspection.json` 和 `final-audit.json` 记录；摘要见文末。当前 ABI 符号保留根从本批编译的库生成，不复用旧候选 ABI。产物检查覆盖 ELF、符号、镜像校验、分区和源码／暂存文件摘要；没有执行测试套件、模拟回收、实际模组通信、网络握手或硬件故障注入。

配套补丁为 26 文件 ML307、4 文件 UHCI、2 文件 SDK transport，manifest 固定来源与摘要。只生成候选与补丁，没有安装到固件依赖或修改已安装 SDK。ELF 中 SDK 强制旧启动名 `start_app`／`start_app_other_cores` 的 UND 按既有静态审计边界记录；编译链接通过不代表启动验收或干净 SDK 验收。

## 剩余适配

现有正式固件已经有 ML307 的设备网络、HTTP 和 WebSocket 条件分支；阻碍在于具体板型尚未提供可信的新模组会话复位、按确切型号／固件解释并采用时间、正式依赖与 owner 注册，以及连接编号耗尽后停止／join／复位／替换 owner 的生命周期。不能把 GPIO 使能等同于确认电源复位，也不能用恒真时间回调绕过 TLS 日期验证。

只为八个蜂窝变体追加本批证据，保留全部旧研究记录。官方固定版本实际是 154 目录／188 变体；152 项有源码入口，36 项无入口，另有 1 项历史本地 profile。构建账本仍为 1 项当前缓存源码通过、103 项历史通过但源码已变化、84 项未构建；累计 104 项曾通过缓存构建。188 项本轮实机均未验收，唯一现有微雪 1.8 也未在本批刷写。

未执行 Git 写操作、安装、刷机、OTA、上传或发布。

## 实际命令与结果

```text
python -B evidence/ml307-lease-owner-20261113/prepare_candidate.py
python -B evidence/ml307-lease-owner-20261113/cellular_build_isolated.py
python -B evidence/ml307-lease-owner-20261113/cellular_link_current_abi.py
python -B evidence/ml307-lease-owner-20261113/cellular_package_patches.py
python -B evidence/ml307-lease-owner-20261113/cellular_inspect_link.py
python -B evidence/ml307-lease-owner-20261113/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-lease-owner-20261113/close_research.py
```

隔离构建与当前 ABI 保留根链接退出 0；843 个当前组件代码符号出现在最终 ELF，镜像 1464128 字节，779 项编译源记录、38 份产物快照。readelf／nm／size、esptool 镜像检查及分区解析全部退出 0。首次无效参数命令的退出 2 单独保留；没有把它覆盖成成功。

最终记录检查固件 main 文件集合和摘要、原板型注册表、全部既有构建记录保持本批开始时内容，候选仅上述两文件改变，研究只为八个蜂窝变体追加字段。`git diff --check` 及两份新候选的 `git diff --no-index --check -- NUL <文件>` 用于检查空白差异；结果存入 `final-audit.json`，不涉及 Git 写操作。

最终记录脚本第一次运行时，错误地要求候选 CMake 与隔离工程逐字相等，因工程按既定流程追加 WHOLE_ARCHIVE 而返回退出 1。已将检查收窄为严格核对这两个文件的原文加该唯一后缀，其余文件继续逐字摘要相等；随后同一记录命令退出 0。该问题发生在记录检查脚本，构建与产物检查结果未改变。`--no-index --check` 对新文件返回 1，未报告空白错误；常规 `git diff --check` 返回 0。
