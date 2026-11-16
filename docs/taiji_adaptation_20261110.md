# 太极派S3 STD／PDM源码接入

后续更新：两款太极派已使用保留原表的固定QSPI路径完成首次完整缓存构建，CHD2.8默认选择器回归通过；实机、触摸和声学限制仍在。见[本批说明](taiji_qspi_native_20261112.md)。

2026-11-10，Asia/Shanghai。继续固定小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个发布目录／188个变体。本批新增两个源码候选后，43个本地profile对应42个官方变体，另一个为历史微雪兼容名；146项尚无本地实现。两款因缺ST77916组件停在构建预检，**没有太极派固件编译通过或实机证据**。

## 固定变体与屏幕数据

来源为[固定太极派目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/taiji-pi-s3)的config、README和实际`taiji_pi_s3.cc`，另核同提交Kconfig。README说明2025年7月后更换麦克风及屏幕玻璃，标签批次号大于2528选择PDM；两份发布配置分别选择STD和PDM，并要求Wi-Fi／LWIP优先分配PSRAM。

| profile | 应用身份 | 采集／初始化表 |
| --- | --- | --- |
| taiji-pi-s3 | assistant_taijistd | 标准I2S麦克风24kHz，STD屏幕表216条 |
| taiji-pi-s3-pdm | assistant_taijipdm | PDM麦克风24kHz，PDM屏幕表184条 |

两表逐条从固定源提取，保留顺序、字节和延时；每条声明长度与字面量数据个数相等，最大14字节，注释条目保持不启用。数据在`main/taiji_panel_init.h`，MIT通知在`main/third_party/xiaozhi_taiji/`。提取摘要及公开副本在忽略目录`evidence/taiji-public-20261110/`，来源与边界进入board_research_v2.5.2.json。提取审阅不是驱动编译或实机验证，未修改项目整体许可证。

360×360圆屏ST77916，RGB，QSPI2 mode0／40MHz，CLK9／CS10／D0..3=11／12／13／14／RST47／背光15正向PWM，镜像X／Y、无XY交换、gap0。复用既有QSPI封装，太极派分支使用两份专用表和直接复位脚；不套微雪的TCA9554复位、CS21面板ID探测或80MHz设置。LVGL登记保留双镜像和偶数像素刷新边界，复用360px圆屏卡片布局，字体用现有16号中文字体。

固定依赖为`espressif/esp_lcd_st77916 2.0.2`，公开注册页指向`espressif/esp-iot-solution@91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f/components/display/lcd/esp_lcd_st77916`。已读取该提交API和版本声明，接口允许自定义初始化及QSPI标志；没有把公开源码读入当作已安装组件或编译成功，也未把驱动下载进managed_components。

## 触摸与输入

CST816S为I2C1 SDA7／SCL8、地址0x15、RST40／IRQ41。复用现有CST816S1.1.2组件，I2C事务显式1秒超时；轮询坐标，读错或越界取消LVGL按压。暂按面板双镜像把有效原始坐标映射为`359-x,359-y`，避免边界映射到360；原应用只识别触摸事件，未提供坐标映射实测，因此此变换需实物校准。

原板没有声明物理录音按钮，本地`listen_button=GPIO_NUM_NC`，不猜测GPIO0是可用操作键。根页面提供已有“按住说话”触摸入口，卡片由坐标触摸确认，继续复用卡片ID／revision、联网与后台确认约束。无触摸芯片或初始化失败不伪装为可操作整板；触摸方向、圆边裁切、按住／拖出／取消和配对码均无实物验收。

## 24kHz数字音频与静音

STD麦克风BCLK42／WS45／DATA46，24kHz左声道32位槽，沿固定NoAudioCodec读数移位12并饱和至PCM16。PDM版用CLK45／DATA46，实际PDM采集24kHz PCM16直通数据接口，再走共用24→16kHz转换；不把T-Circle V1.1的32kHz要求套为太极派采样率，也不据发布配置宣称其麦克风最小时钟已验收。实际型号和PDM时钟要求仍需硬件资料／实测。

两款扬声器BCLK18／WS16／DATA17，24kHz，按固定Kconfig默认`I2S_USE_2SLOT=y`使用BOTH两时隙32位输出。复用已有数字音频有界传输、软件音量与PCM清零，GPIO48低有效静音／高有效启用交给dummy codec管理。config虽然定义MCLK21，原NoAudioCodec构造没有使用它，本地不驱动虚构MCLK。无模拟codec，不调用模拟增益API；当前对话／提醒音量与静音偏好继续共用已实现行为。

Flash16MB／Octal PSRAM沿固定根默认，板级配置保留Wi-Fi／LWIP的PSRAM选项；尚未测量实物容量。单时隙可选配置、其他板卡修订、电池／低功耗等能力未完成，默认两时隙源码不证明所有菜单组合兼容。

## 实际命令与结果范围

分别执行以下入口，TEMP／TMP为本任务I盘临时根，仅使用既有SDK／工具：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board <taiji-pi-s3或taiji-pi-s3-pdm>
```

两次实际退出1，均在预检报告缺`espressif/esp_lcd_st77916`，没有创建构建目录、进入编译或生成镜像。对应`cached_build_attempts_20261110_taiji_<profile>.json`保存失败与候选源码摘要，不能据输入摘要称新分支已编译。驱动需牛牛后续准备，此轮不自行安装或绕过依赖检查。

WK默认OLED32、T-Circle V1.1、云聊S3在各自隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，设置IDF_PY_BUILD_JOBS=2、已安装SDK5.5.6-dirty和Python外网守卫。这些旧入口只覆盖自己选中的宏分支，不会编译新太极派QSPI／PDM24kHz分支。T-Circle首次配置失败是旧临时vendor副本缺失，保留失败日志后只从原始项目组件缓存复制不存在的文件，现有文件不覆盖，不安装组件；恢复清单另存。

牛牛只有现有微雪1.8。太极派批次号／面板表、QSPI时序与颜色、CST816坐标镜像、根页／卡片触摸、24kHz STD／PDM收音和双时隙输出、GPIO48静音、声学／唤醒／AEC、实际容量与供电／同板OTA均未验收。全量目标保持未完成。

没有运行测试套件或类型检查，未安装依赖、修改SDK、刷机／OTA、启动服务、Git写入、上传或发布；网关／运行时和自然插话改动保留。最终旧入口构建、镜像与矩阵状态在本批收尾时补充。


## 本批最终证据与未完成范围

WK默认OLED32、T-Circle V1.1、云聊S3的最终独立回归构建均退出0；T-Circle原临时缓存补齐后重试通过，首次失败日志保留。三款分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用身份、16MB设置、checksum及validation hash有效。

| 已有profile回归 | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| wk-esp32s3-dev | 2,156,752 | 988,976 |
| lilygo-t-circle-s3-v11 | 2,224,000 | 921,728 |
| yunliao-s3 | 2,245,504 | 900,224 |

三份通过记录各有70个源码／配置摘要及完整main文件集合与当前工作区一致；源码快照出现太极派文件不表示它们被选中编译。两个太极派失败记录也各保存70个候选输入摘要，但没有staging、固件或镜像检查结果。所有记录为`cached_build_attempts_20261110_taiji_<profile>.json`，日志、旧入口镜像、配置与源码副本在忽略目录`evidence/taiji-builds-20261110/`。归档脚本初次沿用了上一批不存在的日志路径，已修正清单后重新归档；没有因此重跑已通过的构建。

SDK仍保留原NimBLE／idf_as_lib修改并自报5.5.6-dirty，17个override含非当前BSP，既有LVGL弃用警告保留。这些旧入口构建不是干净发行依赖锁，更不证明新太极派QSPI初始化、触摸变换或PDM24kHz能运行。

矩阵当前42个官方变体有源码，3项当前源码缓存构建通过、19项历史通过因公共文件变化为stale、5项依赖预检失败（新增太极派2项ST77916缺失，原GC9A01缺失3项）、15项尚未构建；累计22个官方变体曾分别编译通过，本批未增加该数。146项无源码，全部188项实机验收仍为not_run。

实际执行`git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`和对9个新增／未跟踪源码、通知与文档的`git diff --no-index --check -- NUL <文件>`审阅，无空白错误，保留LF／CRLF提示；执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`同步清单。初始化表提取只核对源字节和声明长度，没有运行测试套件、类型检查或硬件操作。
