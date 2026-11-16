# ML307连接管理候选

本轮新增具体`Ml307ConnectionOwner`，将前批传输接口与模组检测、就绪检查、原始接收模式和连接ID生命周期连起来。修正后的候选已与真实HTTP／WebSocket客户端、UHCI及SDK覆盖组件完成S3编译／链接。**没有在设备上启动模组，也未接入任何native板型。** 当前来源和产物统一存于`evidence/ml307-startup-source-20261112/`，下文数字替代该目录此前startup／首版owner记录。

## 实现的管理路径

Create检查UART引脚及两个必需的平台回调：`prepare_clean_session(timeout_ms)`确认模组已进入无旧连接的新会话，`trusted_clock_ready()`确认TLS所用系统时钟可信。回调必须不抛异常、不重入owner；本轮没有为任何板型编写这两个回调，也没有把“ESP刚启动”当作模组已重置。可信时间不能用任意日期或关闭证书有效期校验替代。

Start每个owner只尝试一次，竞争调用返回Busy。依次执行平台确认新会话、内部RAM UART检测、确认具体类型支持ML307 raw协议、等待SIM READY／有效蜂窝注册／激活PDP地址，然后调用`EnableMl307RawReceive`。仅EC801E／NT26K检测成功不能进入这个ML307 owner。启动失败保留错误状态并要求外部确认新模组会话，不自动发送MREBOOT、切供电或循环创建线程。

入口接受1..60000ms，把剩余预算传到后续阶段，并在阶段结束后拒绝超期成功。但底层波特率探测、普通AT发送TX和原库构造清理仍各有等待，**尚未实现严格的端到端截止时间**。先前启动修正和这些限制见[启动就绪说明](ml307_startup_readiness_20261112.md)。

Acquire只在owner已启动、当前SIM／注册／PDP标志仍有效、raw UART未丢帧／锁存失败且可信时钟就绪时创建TLS流。它拒绝UART工作任务调用，使用try-lock返回Busy，不在获取租约时发AT。每个流记录连接ID和owner引用，指针身份一起用于退役核对；活动流会保持owner存活，避免modem先于流释放。

六个ID严格执行`Unused -> Active -> Retired`，即使已确认关闭也不在同一raw会话复用。已退役ID与迟到URC之间没有序列号隔离证据，不能靠“Close返回成功”重新标Unused。未使用过的其他ID仍可分配；所有ID耗尽后必须停止／join使用者、销毁旧本地资源，并由板级流程确认模组重置，再建立新owner。这个板级重建流程尚未实现，不能把六个ID当作无限重连能力。

Retire要求读／写／poll和外部sender均已退出，在非UART任务调用。它最多追加一次1000ms关闭尝试；只能在TLS流记录raw关闭完成后释放流。未确认关闭的流放入最多六个固定退役槽，标记`reset_required`，停止进一步Acquire。退役前移除流内owner引用，用局部引用维持方法执行期间存活，避免`owner -> pending stream -> owner`循环。最终析构先释放已join的pending流回调，再释放modem和UART；本地释放不表示远端socket或模组状态已经复位。

外来／重复身份的流会记录错误并要求重置。直接销毁流而不Retire属于违反owner调用约定：其ID保持Active而不会复用，析构时记录未退役错误。候选没有替客户端实现join，也没有完成所有错误输入、异常分配或析构竞争的运行验收。固件应通过已有transport的close／destroy流程和同一个owner接入，不能跨owner交付流。

## 首版候选修正及来源

第一次owner编译成功的版本仍有逻辑缺口：释放租约会使ID被再次选择、Start未开启raw模式、启动失败会调用Reboot、未确认关闭的流没有持久保留。独立源码检查后修正了这些路径。首次候选及编译记录保存在`owner-first-link/`，不能将那次793符号链接成功当作生命周期正确的证明；最终候选也尚未运行验证。

本轮沿用同一隔离startup目录，更新其候选、配套补丁、当前产物快照及8项研究字段。历史启动说明和总览旧段落保留，当前`link-inspection.json`／`final-audit.json`以本轮最终源码为准。相比前批poll候选，共13个文件有差异，包含此前7文件启动修正以及owner接入；完整固定ML3073.7.5补丁21文件，仍须同时使用UHCI0.4.0四文件和SDK无socket关闭一文件补丁。源代码和依赖没有安装到固件工程或已安装SDK。

## 真实构建与产物

主要实际命令：

```text
python -B evidence/ml307-startup-source-20261112/link_current_abi.py
python -B evidence/ml307-startup-source-20261112/package_patches.py
python -B evidence/ml307-startup-source-20261112/inspect_link.py
python -B evidence/ml307-startup-source-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-startup-source-20261112/close_research.py
```

`link_current_abi.py`将候选同步到`I:/xy_board_builds_20261109/ml307_startup_link_20261112`，以真实ninja重新构建ML307／UHCI库，从当前库提取全局T／W符号，再实际执行`idf.py -DIDF_TARGET=esp32s3 build`。命令数组、cwd和退出码在`final-build.json`：全部退出0。环境仍为现有SDK5.5.6-dirty、工具链14.2.0、既有WebSocket1.8.0和本地SDK tcp_transport覆盖，Python外网保护启用。

最终ELF为Xtensa EXEC，当前817个组件代码符号全部保留，包括owner的Create／Start／Acquire／Retire／状态查询和此前HTTP／WS接口。镜像为**1026832字节**，checksum／validation hash有效；635份直接编译源记录、28份产物／配置快照保存摘要。检查实际调用readelf、nm、size、esptool及分区解码，均退出0。SDK仍有两个强制旧启动名UND，实际esp_startup_*存在，未改SDK或声称干净SDK通过。

app_main始终为空，当前链接工程没有执行owner／AT命令／TLS握手。编译布局没有PSRAM、使用单factory，仅用于组件链接，不能替代具体板卡的生产PSRAM／双OTA配置。未运行测试套件、生命周期／ID／断网故障注入、识别／声学或实机验收。

## 未完成与账本

下一步仍需完成板级供电／复位与可信时间回调、安全重连和整体截止时间、SIM／APN／PDP主动配置、绑定／OTA／控制媒体WebSocket工厂接入及各板音频显示配置。17份固定来源给出的8板串口差异仍在`board-pin-review.json`：六项TX12／RX11，ATK与正辰CAM为43／44，需要避开默认控制台；只有ATK源码定义独立4G电源使能，复位时序与硬件效果未验收。

固件main、native registry、Spot C5当前263项缓存构建输入均保持原文。152／188个官方变体有源码入口，153个本地profile含1历史入口，36项待源码；构建账本仍1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。未安装、启动服务、写Git、刷机、OTA、上传或发布。全量适配目标继续，不能按本轮组件交付宣称完成。
