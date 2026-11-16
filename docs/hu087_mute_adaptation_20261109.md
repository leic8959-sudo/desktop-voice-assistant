# HU-087右声道OLED接入与共用静音修正

2026-11-09，Asia/Shanghai。继续小智v2.5.2固定提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个发布目录／188个变体。新增HU-087后，37个本地profile对应36个官方变体，另一个是历史兼容入口；152项尚无自有源码入口。源码、缓存编译、干净发行构建与实机验收分别记录。

## HU-087具体接法

固定来源为[hu-087目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/hu-087)的config.h、config.json、hu_087_board.cc、README.md，公开副本在忽略目录`evidence/hu087-kevin-maza-public-20261109/`。

profile=`hu-087`，应用身份=`assistant_hu087`。128×64 SSD1306，I2C0 SDA41／SCL42、地址0x3C、400kHz、X／Y镜像，复用OLED64文本／卡片／六位配对码、对比度和开关显示，显式1秒I2C事务超时。16MB Flash／Octal PSRAM沿固定默认配置，尚非实物测量。依赖最小IDF5.5.6，沿用17个既有本地组件，不安装驱动。

麦克风16kHz，BCLK5／WS4／DATA6；扬声器24kHz，BCLK15／WS16／DATA7。**输入和输出均为32位右声道槽**，来自实际板类带显式slot_mask的NoAudioCodecSimplex构造，不能沿之前面包板默认左声道。复用数字音频数据接口，输入移位12并饱和，输出PCM16放32位槽高位；软件音量、PCM静音和打开状态检查复用既有实现。

原板GPIO17输出高使能功放。本地将它交给已有dummy codec管理开启／静音／关闭生命周期，不把它当成无PA板；实际使能电平、启动爆音、待机功耗和音频行为仍须实物核对。本地输入I2S0／输出I2S1沿现有数字音频引擎，上游simplex控制器编号相反，物理引脚不变；该差异不表示已经得到硬件验证。

唯一操作键为GPIO18离散触摸输入，启动先等待完整释放。根页按住说话，卡片短按选择／长按松手确认；没有坐标触摸，也没有猜测GPIO0或39／40／47为操作键。上游点击切换聊天／长按加音量没有照搬；单键音量手势、LED、电池遥测和深睡仍未实现。README明确板载USB只用于充电，编程需外部EN／IO0接口；没有连接串口或执行刷机。

牛牛只有现有微雪1.8，HU-087实机不可用。GPIO18极性／抖动／录音卡片手感、右声道音频／PA17、显示镜像与对比度、配对、提醒、唤醒／AEC和同板OTA均未验收。

## 本轮发现并修正的共用音频错误

旧`audio_play_pcm24()`在每包写入前执行`g_output_muted.exchange(false)`并解除codec静音，未区分用户对话静音、提醒静音和临时暂停。`audio_playback_pause(false)`直接以false写静音；偏好更新总按对话通道更新硬件；提醒切换没有同步软件静音状态。这些路径可使已选择静音的通道重新出声，数字PA及模拟codec板均受共用代码影响，不能只加HU-087引脚后留下原行为。

`requested_output_mute()`统一表达当前通道的用户静音或临时暂停。`apply_output_settings_locked()`只负责编排现有codec音量／静音API，不增加第二套驱动。播放包、暂停恢复、偏好更新、提醒通道和本地音量调整按`g_playback_lock`→`g_state_lock`顺序处理；硬件已应用通道与请求通道分别保存。提醒切换即使锁超时仍保留请求，下一包先补写设置；设置API失败会保留dirty标志，阻止该包PCM写入。偏好更新若锁超时则返回失败，不伪称已应用；首次音频初始化前的偏好继续保留供打开codec时使用。

每包在锁内取得确认的静音／音量快照，随后同一包的输出和AEC参考都使用该快照。暂停／恢复不再覆盖用户静音；音量为0时参考也为零。本机对话音量键仍只影响对话音量，提醒通道使用中不会被它改成对话音量；设置只在当前进程，不新增持久化或服务器协议。

已读取本地esp_codec_dev1.5.11源码：`esp_codec_dev_set_out_mute()`调用codec的mute回调后直接返回OK，未传递该回调的错误；单凭返回值不能证明硬件静音。播放路径因此在静音／零音量时还将实际PCM清零，适用于模拟和数字板。16kHz输出分支同时清零FIR产出的短历史尾音，静音源数据先置零再进入FIR，避免将静音期间的源音频保留到恢复后。AEC参考也置零。它不能替代故障注入、声学或功放爆音验证，没有改第三方依赖。

以下是源码应满足的行为约定，尚无运行／实机测试结果：

| 场景 | 该包输出和参考 |
| --- | --- |
| 对话静音，当前为对话通道 | PCM与参考为0，首包不解除用户静音 |
| 对话静音、提醒未静音，切到提醒 | 使用提醒音量与静音设置；结束后恢复对话静音 |
| 提醒静音、对话未静音，切到提醒 | 提醒PCM与参考为0；本机对话音量键不改当前提醒音量 |
| 临时暂停后恢复 | 暂停为0，恢复按当前通道用户偏好，保持原有静音 |
| 当前通道音量0 | PCM与参考为0，不保留非零软件参考 |
| abort后收到后续有效包 | 先重用当前偏好设置，再写PCM；用户静音持续有效 |
| 设置API失败 | 返回错误、保留dirty、该包不写PCM；不将失败当已应用 |

没有修改control_protocol自然插话／唤醒人工决策处，也未把已有设备校准数值推广为新板实测参数。未运行测试套件或新增镜像实现的单元测试；下面仅记录真实构建和镜像证据。

## 其他板的调查进展

- Kevin Box-2：SSD1306为I2C0 SDA7／SCL8、无镜像，codec／AXP2101在另一条I2C1 SDA42／SCL41；ES7210＋ES8311双codec、24kHz共享I2S；原板还启用ML307和修改充电参数。已写入调查，仍是inventory_only；需要正确双I2C、PMIC供电读回、双codec和网络能力记录，不套HU-087或面包板接法，不执行充电／蜂窝配置。
- Maza AI ESP32-S3：默认A01 2.9英寸128×296电子纸，ES8311＋NS4150B，独立BUSY／电源脚。已写入调查，仍是inventory_only；进一步读取实际EpdDisplay后确认：物理RAM是128×296，但LVGL界面是296×128横屏，不能按config宽高直接排版。原初始化依次全清／全刷／建立局刷基图，2.9子类提供C22125C全刷和局刷波形；`read_busy()`每5ms检查高电平却没有超时。需要电子纸驱动、BUSY超时和有界刷新调度，不能套SSD1306或40ms彩屏动画，也不能原样沿用无界等待。具体引脚及来源见board_research_v2.5.2.json。

## 实际构建命令

HU-087在本任务I盘临时根下执行：

```powershell
$env:TMP = 'I:\xy_board_builds_20261109'
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board hu-087
```

既有入口按各自隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，设置IDF_PY_BUILD_JOBS=2、现有工具路径／Python外网守卫。最终源码与产物匹配和每项退出结果在本批结束后补充；中间成功不自动提升未选分支。

没有安装依赖、刷机／OTA、运行服务、Git写入、上传或发布；网关和运行时并发改动保留。全部188项实机验收仍未完成。


## 最终构建、镜像与未完成范围

HU-087完整缓存构建、最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=hu-087 reconfigure build`均退出0。面包板OLED32、GenJuTech1.54、T-Circle V1.1、微雪AMOLED1.8-v2及LCD1.83各自独立回归构建全部退出0，覆盖左／右数字音频、PDM、单／双模拟codec和16kHz输出分支。首三款旧入口在末尾加入显式PCM清零后再次构建，初次回归日志另存保留。没有把其他未选宏分支说成已编译。

六个应用各自执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0，应用身份、16MB设置、checksum及validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| hu-087 | 2,155,200 | 990,528（约31%） |
| bread-compact-wifi | 2,156,752 | 988,976 |
| genjutech-s3-1.54tft | 2,245,360 | 900,368 |
| lilygo-t-circle-s3-v11 | 2,224,000 | 921,728 |
| waveshare-amoled-1.8-v2 | 2,615,520 | 530,208 |
| waveshare-lcd-1.83 | 2,329,104 | 816,624 |

每个profile有61个源码／配置摘要及完整main文件集合与当前工作区一致，记录为`cached_build_attempts_20261109_hu087_<profile>.json`，日志、镜像、配置和源码副本保留在忽略目录`evidence/hu087-builds-20261109/`。SDK仍保留原NimBLE／idf_as_lib修改，自报5.5.6-dirty；既有LVGL弃用警告和17个override中的非当前BSP保留。镜像检查证明文件结构与身份，不能证明真实声学或静音效果。

当前36个官方变体有源码，6项当前源码缓存构建通过、12项历史通过因公共源码变化为stale、3项GC9A01依赖预检失败、15项未构建；累计18个官方变体曾各自编译通过。152项尚无源码，全部188项实机验收仍为not_run。Kevin Box-2及Maza依然是调查状态，没有增加虚假源码支持条目。

执行`git diff --check -- firmware/assistant_espidf/main/audio_io.cpp`及对5个新增／未跟踪源码和文档的`git diff --no-index --check -- NUL <文件>`进行空白审阅，无空白错误；存在Git的LF／CRLF提示。另执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`同步清单。未运行测试套件／类型检查、故障注入、声学测试或设备操作，未安装依赖、改SDK、刷机、提交／上传／发布。
