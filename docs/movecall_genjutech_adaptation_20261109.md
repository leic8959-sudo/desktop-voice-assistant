# Movecall两款圆屏与GenJuTech 1.54基础适配

后续更新：本页为历史范围记录；Spotpear1.28BOX、MovecallMoji/璀璨、Electron现已用SDK DCS操作保留原GC9A01默认42命令，各自成功完成整板构建。专用GC9107分支未替换。当前依赖963声明、74本地/73官方满足直接条件，最新来源和限制见[本批记录](gc9a01_default_sdk_20261112.md)。

2026-11-09，Asia/Shanghai。继续固定小智v2.5.2的154个发布目录／188个变体。本批新增三个源码入口后，33个本地profile对应32个官方变体，另一个是历史微雪兼容名；156个官方变体仍无本地实现。源码入口、缓存编译、干净发行构建和实机验收分别记录。

## 固定来源与板级差异

来源提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`：

- [Moji固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/movecall/moji-esp32s3)
- [璀璨固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/movecall/cuican-esp32s3)
- [GenJuTech固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/genjutech-s3-1.54tft)

已读取config.h、config.json、实际板类，以及存在的README／电源类；音频codec API另外对照本机既有esp_codec_dev1.5.11源码。固定公共源码副本在忽略目录`evidence/movecall-genjutech-public-20261109/`，调查及具体URL进入`board_research_v2.5.2.json`。没有复制上游板类／电源估算应用代码。

| profile／应用身份 | 显示与SPI3引脚 | ES8311音频 |
| --- | --- | --- |
| movecall-moji-esp32s3／assistant_mvmoji | GC9A01 240×240圆屏，CLK16／MOSI17／CS15／DC7／RST18／背光3 | 24kHz双向，I2C0 SDA5／SCL4，MCLK6／BCLK14／WS12／输入13／输出11／PA9 |
| movecall-cuican-esp32s3／assistant_mvcuican | GC9A01 240×240圆屏，CLK12／MOSI10／CS13／DC14／RST11／背光16 | 24kHz双向，I2C0 SDA6／SCL7，MCLK45／BCLK39／WS41／输入40／输出42／PA17 |
| genjutech-s3-1.54tft／assistant_genju154 | ST7789 240×240方屏，CLK4／MOSI3／CS6／DC5／RST7／背光2 | 16kHz双向，I2C0 SDA9／SCL8，MCLK14／BCLK13／WS11／输入12／输出10／PA21 |

Movecall两款为BGR、SPI mode0／40MHz、反色、X镜像、gap0，使用已有240px圆屏布局、短文字和向内收的卡片按钮；没有坐标触摸，BOOT0用于录音／卡片选择和确认，详情自动滚动。没有套用Spotpear特定调参、RTC3供电、反向背光或可选触摸总线。GenJuTech为RGB、mode3／80MHz、反色、无镜像／gap0，使用240px方屏布局。三款背光均为正向PWM。

璀璨的config.json及README明确要求8MB Flash和尺寸优化，已配置独立8MB双OTA／模型分区；不使用16MB分区尾地址。Moji、GenJuTech的变体配置无容量覆盖，依据固定根sdkconfig.defaults的16MB和sdkconfig.defaults.esp32s3的Octal PSRAM建立候选；不是实物测量结果。

## GenJuTech的16kHz收放音与PA边界

新增板级`speaker_sample_rate`，GenJuTech为16000，其他已适配板继续24000。单ES8311的I2S初始化和codec打开都使用板级硬件频率；GenJuTech采集为真实16000，直接进入已有16kHz AFE／AEC链路。

服务器播放接口仍接收24kHz PCM。复用已有24→16 FIR：对GenJuTech把其产出的同一份16kHz缓冲分别用于硬件输出和软件回声参考，不将24kHz数据以16kHz速度播放。其他板继续直接输出原24kHz数据。播放转换历史沿用已有参考锁、播放锁和abort清理，暂停／静音时参考仍置零。当前调用方单包最多2048字节，转换缓冲有界；FIR有16个源采样的前视，流尾／切换和时间对齐需实机听测，不以编译替代验收。未更改control_protocol或自然插话人工决策。

固定GenJuTech板类的`SparkBotEs8311AudioCodec::EnableOutput(false)`故意不关闭输出，注释为显示IO与PA冲突。尚无电路图证明具体共轨／复用关系。本地候选在板级初始化把PA21保持高，不把此引脚交给codec关闭逻辑；`esp_codec_set_disable_when_closed(false)`保留模拟输出启用状态，静音／音量继续走ES8311寄存器。必须检查实际PA路由、静音、爆音、功耗、显示和close/reopen行为；这项处理不是电路或实机验证结论。

音量键GPIO42／41配置为上拉输入，启动先见全部松开才能使用；同时按下多个键返回Invalid，跨键继续走已有取消并等待释放逻辑。BOOT0走原录音／卡片输入。音量复用当前本机对话音量调整：首次10、按住2秒后每200ms调整5，限定0至100，不改提醒或静音偏好；保存在当前进程，服务器偏好仍可覆盖。没有沿用上游长按直接跳0／100的快捷方式。

充电GPIO16／ADC4电池估算、自动深睡、内置LED等尚未实现；基础语音源码不代表整板全部功能交付。牛牛只有现有微雪1.8，三款均无实物验收。

## 实际构建与限制

各款执行：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board <对应profile>
```

Moji和璀璨的预检均实际退出1：缺`espressif/esp_lcd_gc9a01 2.0.4`，未创建隔离目录、未进入编译。分别保存`cached_build_attempts_20261109_movecall_<profile>.json`及失败日志；需要牛牛后续准备驱动依赖，不自行安装。

GenJuTech使用既有SDK5.5.6-dirty、17个本地组件override及Python外网守卫，设置`IDF_PY_BUILD_JOBS=2`。在原隔离目录恢复执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=genjutech-s3-1.54tft reconfigure build`，实际退出0。旧入口MUMA、T-Circle V1.1、微雪AMOLED1.8-v2在各自隔离目录执行相同形式的reconfigure/build，全部退出0；MUMA首次回归后随PA注释修正再次执行，初次日志保留。

GenJuTech首次完整构建实际因C盘空间不足退出1，原日志已保留为`cached-build-disk-full.log`，截断位置在SDK的mbedTLS目标，不能据此判断本地代码编译失败。先核对8个旧缓存的I盘归档产物摘要，再完整迁移旧临时build树至`evidence/relocated-build-cache-20261109/`，保留约2.15GB内容并复核迁移后的固件／ELF等摘要；未删除原始SDK、managed_components或项目源码。直接删除操作曾被自动审批以策略原因阻止，随后采用保留全部内容的迁移方式。迁移索引为该目录的`relocations.json`；旧记录中的8个临时staging路径之下已没有build目录，历史归档证据仍在。确认旧进程结束后，在GenJuTech原目录继续reconfigure/build，没有因观察超时启动重复构建。

另生成`native_dependency_inventory_20261109_movecall.json`：静态读取全部33个入口manifest与17个缓存组件元数据，15个入口可找到直接依赖版本，18个有缺失／版本／固定Git来源阻塞。它不解析完整传递依赖，不安装／stage／编译，也不提升任何矩阵构建状态；其中15个包含历史兼容名，不能当作15个官方变体已编译。

未运行测试套件／独立类型检查；未安装依赖、刷机、OTA、启动服务、Git写入、上传或发布。并发网关／运行时改动保留。全部188项实机验收保持not_run。


## 最终镜像与矩阵状态

四个应用分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0，应用身份、16MB设置正确，checksum及validation hash有效。每个profile的59个源码／配置摘要以及完整main文件集合与当前工作区一致。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| genjutech-s3-1.54tft | 2,531,792 | 613,936（约19%） |
| spotpear-sp-esp32-s3-1.54-muma | 2,542,368 | 603,360 |
| lilygo-t-circle-s3-v11 | 2,510,544 | 635,184 |
| waveshare-amoled-1.8-v2 | 2,614,960 | 530,768 |

记录为`cached_build_attempts_20261109_movecall_<profile>.json`，原始日志、镜像、配置与源码副本在忽略目录`evidence/movecall-builds-20261109/`。SDK原NimBLE／idf_as_lib改动保留，自报5.5.6-dirty；LVGL已有弃用警告保留，17个override仍包含非当前板的历史BSP。没有把首次磁盘不足记录改成通过，也没有用其他板构建证明GC9A01分支已编译。

当前32个官方变体有源码，其中4项当前源码缓存构建通过，10项历史通过因公共文件变更为stale，3项GC9A01依赖缺失预检失败，15项尚未构建。累计14个官方变体曾各自编译通过；156个尚无源码入口，全部188项实机验收仍未执行。

执行`git diff --check`及针对6个新源码／文档文件的`git diff --no-index --check -- NUL <文件>`审阅空白，无空白错误，保留Git的LF／CRLF提示。执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`刷新登记；未运行测试套件、类型检查或设备操作。
