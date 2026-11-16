# Sensairshuttle C5音频生命周期候选（2026-11-12）

本批读取固定板级完整源码，确认Sensairshuttle不能套用前三款C5的ES8311音频：它使用片内ADC麦克风和差分PDM扬声器。新增两份ADC生命周期适配候选，仅位于 `evidence/sensair-c5-source-20261112/patches/candidate/`，**没有接入main、登记新板型、编译、链接或执行测试**。当前仍151／188官方源码入口、37待源码，上一批Spot C5缓存通过不覆盖本候选。

## 固定板级来源与差异

六份板类／ADC-PDM类／配置／README固定在小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，逐份核对Git blob与SHA256。README声明C5-WROOM-1-N16R8，实体容量未测量。

| 路径 | 实际源码依据及后续要求 |
| --- | --- |
| 输入 | ADC1 channel5，16kHz、12位、12dB衰减、TYPE2、单pattern；本机C5库支持该通道与采样范围，不等于模拟信号或识别通过 |
| 输出 | I2S0 PDM，24kHz，正端7／反端8，PA1；up_sample_fs=480，四种PDM信号scale均MUL_4 |
| 时钟重配 | 现有codec_dev I2S set_fmt会把PDM up_sample_fs重设为sample_rate/100；本板24000/100=240，与源板480不同，后续必须检查并恢复实际PDM时钟 |
| 引脚复用 | 上游负端参数是GPIO8，但硬编码PIN_FUNC_SELECT使用GPIO10；后续应按实际负端配置GPIO8，再接反相矩阵信号，不能原样复制这个硬编码 |
| 显示 | 源码标ST7789，284×240、SPI2 mode0 40MHz，MOSI23／CLK24／DC26／CS25／RST NC，横向换轴、mirrorY、配置gap36,0；实际调用ILI9341公开驱动并传19条自定义初始化，不能把API名称当芯片型号 |
| 触摸 | I2C0 SDA2／SCL3、CST816D0x15，源类读取六字节但仅按接触／释放切换聊天，没有屏幕坐标映射；后续先作为触摸按键，不能声称坐标触摸已适配 |
| 资产 | 源配置指向外部wn9+字体+动画包；本项目已有界面及已授权固定wn9s后端须明确选型，不能将该外部wn9包自动当成wn9s模型分区 |

当前ILI9341组件未安装，未安装或下载它，也未下载该外部资源包。完整显示、电源／背光能力、触摸按键、差分PDM及板级入口仍需实现。固定词方向已授权，候选仍采用“你好小智”；不再把“等待固定词取舍”当作当前阻塞。

## 为什么需要同一任务拥有ADC

当前项目从初始化、采集和播放相关任务开关codec。SDK5.5.6的adc_continuous_start获取ADC硬件锁，stop释放锁，底层是newlib_lock；固定上游ADC-PDM类也明确要求start／stop位于同一任务。不能只因read可以跨任务就假设完整生命周期也能跨任务。

codec_dev1.5.11的ADC set_fmt还会执行stop→config→flush→restart；close中部分stop／deinit错误仅日志记录，最后返回OK。因此本候选将创建SDK句柄、构造ADC data_if、set_fmt、enable、read和销毁均委托给同一个任务。

继续复用现有ADC data_if的DMA解析、位宽转换和居中PCM，不另写ADC解析算法。SDK句柄由候选显式创建，再以external-handle方式交给codec库；关闭时先核对enable(false)，调用data_if close释放其本地缓冲，再核对SDK deinit结果。下层任何失败都不能用一个总OK替代。SDK清理失败后仍可能留下资源，不能据软件事件证明硬件已停稳。

## 候选接口和内存／超时边界

`sensair_adc_owner_new()`返回标准audio_codec_data_if_t，只接受16kHz／PCM16／mono输入。一个固定命令槽、一份格式／请求描述和1024字节PCM缓冲，最多512样本；没有无界队列，不保存调用者的缓冲或栈指针。read成功才复制内部PCM到调用者，合法长度的失败输出保持清零。

调用方最多等50ms竞争入口锁，登记命令后等待结果最多3000ms；任务创建也等待Ready。控制字段和PCM存储由状态对象持有，超时后整个实例停止复用，不接受下一次命令覆盖迟到操作。worker继续观察closing并尝试销毁，数据接口外壳与worker状态分别持有引用，外壳释放后worker不再访问它。

关闭前，真正调用者必须确保所有公开回调已退出；禁止从ADC任务自身删除接口。close等owner完成标志最多3000ms，返回值区分超时和实际清理结果。**当前没有向板级owner暴露清理join句柄**，超时／清理错误后必须设备重启才能重新初始化ADC；不能继续重复创建。这个约束仍需后续完整board owner接入，候选不是完整资源重建方案。

3000ms是调用方等待预算，不是底层ADC操作必定在预算内完成的证明。现有库的内部read等待、样本过滤、SDK硬件锁、调度和日志均未测量。worker栈设8192字节以容纳库的分块解析临时数组，但峰值未验收；1024只限本候选PCM数组，不含DMA池、库解析缓冲、任务栈、对象、AFE／AEC／TLS总内存。

## 本批实际证据与未完成项

读取并归档20份固定板级／本机ADC、I2S、SDK硬件锁及当前项目调用方来源；生成两文件完整补丁和摘要。执行来源、差异与摘要检查，不执行候选C++：

```text
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/sensair-c5-source-20261112/close_research.py
git diff --check
```

源码检查核对同一任务下层操作、独立句柄清理与状态／外壳分离等选定关系；它不是生命周期、并发、超时、内存故障、DMA分片或声学测试。

后续需要实际接入差分PDM及真实静音／音量／PA、PDM时钟配置回读或API确认、显示19条命令／方向和可视区域、触摸按键取消、板级初始化／清理、固定模型及双OTA分区，并完成构建和实机验收。没有用纯按键模式、模拟I2S麦克风或不认证网络代替这些要求。

本批固件main、native registry、所有既有profile和Spot构建输入摘要均未改变；只追加Sensairshuttle调查与候选记录。当前仍151官方源码／152本地profile／37待源码，1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run；188项本轮实机全not_run。未重跑构建、安装、执行单独测试或类型检查、启动服务、写Git、刷机、OTA、上传或发布，完整适配目标继续进行。
