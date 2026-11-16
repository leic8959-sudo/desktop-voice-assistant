# ML307启动就绪与回调生命周期候选

后续已加入连接管理候选并更新同一evidence目录：当前817个组件符号、1026832字节镜像、635份编译源记录。本文以下保留启动整改说明及前轮阶段数字；最终候选、限制和产物以[连接管理候选](ml307_connection_owner_20261112.md)为准，仍无整板或运行验收。

本批处理上一轮传输接入前发现的启动基础问题：严格区分SIM可用、蜂窝已注册与PDP已有地址，修正CEREG解析越界及遗留回调，确保UART对象位于内部RAM。完整候选及真实S3组件构建／链接已归档；**未接入具体板卡的启动owner、未向模组发送AT命令、未验证联网／重连或实物。**8个ML307变体仍inventory_only。

## 启动状态修正

原库CPIN查询只要最终返回OK，等待者就再次写入pin_ready=true，即使回调刚收到SIM PIN／PUK等未就绪状态。现在要求本次查询确实收到有效的单字符串字段且值为READY；没有有效响应、PIN未就绪及无SIM分别返回失败，不因一个OK放行。

原CEREG只有一个状态字段时把stat写成0；带TAC但缺CI时，边界检查只保证了TAC存在，却仍访问下一项。候选分别检查查询格式的n／stat、URC单stat、可选TAC／CI／AcT类型和各自索引。注册状态只接受1或5作为数据注册；畸形响应、注册丢失或PIN丢失清除就绪，不从错误类型的int字段读取值。cereg结构的更新／读取加既有mutex保护，SIM／注册／地址等关键就绪标志使用原子值；这不是整个依赖包所有诊断字段竞争问题的完整审计。

原基类收到CEREG注册成功就发布network_ready，ML307随后查询MIPCALL却即使没等到IP也返回原先Ready。现在ML307在基类构造时声明需要PDP：CEREG只发布独立注册事件，不触发ML307的联网成功。只有有效MIPCALL头、激活状态1、非空且可解析的非零IPv4／IPv6地址，以及当前SIM READY、注册有效同时满足，才发布network_ready。失活、无效地址、模块MATREADY重启会清除网络就绪；没有确认PDP时明确返回超时，不回退成注册成功。

Detect和注册等待的旧无限默认值改为60000ms，拒绝更大的输入。SIM查询不在发送超时后继续下一个不同查询，注册和PDP阶段使用剩余时间安排等待，PDP查询错误直接传播失败。**通用SendCommand仍有独立TX等待，构造函数还保留原HTTP实例清理命令，Detect／构造／注册没有合并为一个严格总deadline。**本批没有把剩余时间计算称为整条启动路径严格60秒上限，也未实现PDP激活、自定义APN或SIM PIN输入。

MIPCALL字段依据固定3.7.5原库的实际查询／URC用法收敛；已有通用AT手册明确了CPIN、CEREG字段，而缓存的TCP/IP V5手册没有MIPCALL命令章节。实际模组型号和固件对应的完整PDP协议仍需补证、实测，不把新解析条件当成制造商全型号支持证明。

## 对象与回调

原AtModem注册捕获this的UART回调，却不保存注册句柄，析构后UART仍可能调用它。候选保存句柄，ML307和EC801E派生析构体先注销并等待已有回调退出，基类再幂等注销并释放事件组。调用者仍必须先join公共使用者、在UART任务／回调之外销毁；本批没有做并发析构测试或替具体owner建立join流程。

网络发布用单独mutex串行更新标志、事件位及回调次序；回调设置和函数复制使用既有状态mutex，调用回调时不持有该状态mutex，便于回调读取注册快照。URC回调仍不得发送AT、等待自身UART任务或销毁modem。没有新增后台线程。

前批AtUart已要求ISR状态对象在内部RAM，而原Detect用普通make_shared创建对象，开启PSRAM的工程可能使其初始化被拒绝。本批明确用MALLOC_CAP_INTERNAL分配AtUart并以配套deleter释放；shared_ptr控制块不宣称也在内部RAM，因为ISR不读取它。Initialize后检查实际initialized状态，未知CGMR型号返回不支持，不再默认当ML307。

UART两个必要任务创建结果现在必须为pdPASS才会走到initialized=true，失败沿已有ESP_ERROR_CHECK风格进入错误处理／重启，而不是无工作线程仍报初始化成功。资源失败注入、部分初始化析构、任务真正运行及实机重启尚未验收，不宣称所有内存失败已可恢复。跨发送任务／解析任务的wait_for_response标志改为原子；它只在任务上下文使用，ISR raw／UHCI信号仍使用前批内部RAM portMUX实现。

本批相对上一候选仅改7份文件；完整基线补丁变为20份ML307文件，仍须配套前批4份UHCI及1份SDK无socket关闭补丁。AtModem构造接口增加PDP要求参数，必须完整重建，不能混用旧对象或头文件。

## 逐板串口与恢复约束

固定小智v2.5.2提交仍为`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。17份实际配置／类／公共头文件按既有Git blob SHA1和SHA256重新核对并归档，覆盖8个发布变体。通用源码使用921600波特；实际波特率／模组能力还需查询与实测。

| 变体 | MCU TX／RX | 固定源码的模组供电／复位 |
| --- | --- | --- |
| alientek-atk-dnesp32s3-box2-4g | 43／44 | IO扩展器bit12，XIO_EN_4G初始化为高；无独立reset定义 |
| bread-compact-ml307 | 12／11 | 无独立控制定义 |
| bread-compact-ml307-128x64 | 12／11 | 同目录，屏幕高度不同；无独立控制定义 |
| nologo-xingzhi-cube-0.85tft-ml307 | 12／11 | 无独立控制定义 |
| nologo-xingzhi-cube-0.96oled-ml307 | 12／11 | 无独立控制定义 |
| nologo-xingzhi-cube-1.54tft-ml307 | 12／11 | 无独立控制定义 |
| zhengchen-1.54tft-ml307 | 12／11 | 无独立控制定义 |
| zhengchen-cam-ml307 | 43／44 | 无模组reset；camera reset=-1不作为模组脚 |

各自构造调用及公共默认参数均表明DTR／RI为NC，没有把它们猜成复位或电源脚。SDK固定S3 UART0默认TX43／RX44，因此ATK及正辰CAM后续必须选择经核对的替代控制台。ATK的4G供电使能只是源码候选，关断时长、共享电源影响和实际复位效果尚无证据，不能凭一个高低电平声称恢复已完成。其他板不能靠重启ESP就假设独立模组已重置。

逐板结果在`board-pin-review.json`；尚未写入native profile或触碰现有板的供电配置。

## 实际构建与证据

```text
python -B evidence/ml307-startup-source-20261112/build_isolated.py
python -B evidence/ml307-startup-source-20261112/link_current_abi.py
python -B evidence/ml307-startup-source-20261112/record_board_pins.py
python -B evidence/ml307-startup-source-20261112/package_patches.py
python -B evidence/ml307-startup-source-20261112/inspect_link.py
python -B evidence/ml307-startup-source-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-startup-source-20261112/close_research.py
```

新目录`I:/xy_board_builds_20261109/ml307_startup_link_20261112`使用既有SDK5.5.6-dirty、固定本地候选、WebSocket1.8.0和SDK tcp_transport本地覆盖，Python外网保护保持启用，没有安装或下载依赖。首次组件编译退出0；之后同步最后两处状态清理，用ninja重新构建当前库，nm从当前库生成767个T／W符号，响应文件成为明确链接依赖，再执行`idf.py -DIDF_TARGET=esp32s3 build`退出0。没有用旧构造ABI的强制未定义符号凑数。

最终Xtensa EXEC内当前767个导出代码符号全部存在，包括新AtModem(bool)构造、DetachUrc、SetNetworkReady、ML307等待及前批HTTP／WS适配入口。634份直接编译源文件记录、28份产物／配置快照归档。镜像1019824字节，身份、checksum／hash和分区解码检查退出0。SDK仍有两个强制旧启动名UND，实际esp_startup_*存在；不声明干净SDK或可启动验收。app_main为空，链接根没有执行，未运行AT状态机、模组、并发／故障注入或独立测试套件。

## 剩余工作与账本

仍需具体模组启动／可信时钟／独占UART／ID租约退役owner、安全关闭和模组复位、注册恢复后的PDP重新确认、认证HTTP绑定／OTA及控制／媒体WS实际工厂接入，以及整板配置、运行资源与实物验收。原库的旧SSL路径、诊断信息读取和构造清理仍有独立限制；认证传输结论仅适用于前批新TLS适配候选。

固件main、native registry和Spot C5当前缓存输入逐项保持原文。仍152／188官方源码入口、153本地profile含1历史入口、36项待源码；账本1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。未安装、启动服务、Git写入、刷机、OTA、上传或发布。
