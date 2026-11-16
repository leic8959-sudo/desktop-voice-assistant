# AtomEchoS3R 无屏源码适配

2026-11-10，Asia/Shanghai。固定范围为小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个目录／188个发布变体。新增 `m5stack-atom-echos3r` 源码候选；其他 AtomS3／S3R EchoBase／Pyramid 仅完成调查，没有登记为可构建入口。

## 硬件证据与实现范围

来源为[固定板级目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/m5stack/atom-echos3r)的 config、实际板类及 README，以及[固定 codec 构造签名](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/audio/codecs/es8311_audio_codec.h)。厂家交叉证据为 [M5Unified.inl 固定提交](https://github.com/m5stack/M5Unified/blob/96576a14247ddc935149145aa6b51943464120f1/src/M5Unified.inl) 中 AtomVoiceS3R 的麦克风、扬声器和按键实现。公开副本保留在忽略目录 `evidence/atom-public-20261110/`；没有把厂家代码复制为本地驱动。

| 项目 | 本地候选配置及依据 |
| --- | --- |
| 应用身份 | `assistant_atomechos3r`，ESP32-S3-PICO-1-N8R8，无屏 |
| 存储 | 固定来源为8MB Flash／Octal 8MB PSRAM；复用既有8MB双OTA分区，每个应用 `0x2A0000`，模型 `0x240000`，不套16MB板分区 |
| Codec | 单 ES8311 输入／输出24kHz，NS4150B功放；I2C0 SDA45／SCL0 |
| 音频脚 | MCLK11／BCLK17／WS3／DIN4／DOUT48／PA18，I2S0；单声道16bit，沿既有 ES8311 音频路径 |
| Codec 时钟 | 上游构造显式 `use_mclk=false`，本地选 BCLK 衍生内部时钟；I2S仍配置MCLK11，厂家回调也有该输出。不由编译推断真实时钟正确 |
| 输入 | 固定源码实际使用低有效GPIO41；README又称无额外按键。厂家对应实现读取41，支持此候选，但按钮实际位置／可用性仍待实物确认 |
| 页面入口 | GPIO41完整松开后保持3秒，开启10分钟手机查看窗口；单次保持只触发一次。说话使用语音唤醒词，没有实现手动按住录音、硬件音量键或状态灯 |

`headless_codec_board.cpp` 只初始化该板 I2C；`board_power.cpp` 不运行 Doit 电源／灯脚逻辑。GPIO0是codec SCL、45是SDA、48是音频输出，不把它们当BOOT、状态灯或独立enable。CMake为Atom选择单codec板源，为Doit继续选择数字／PDM源；其他显示入口仍各自选择。既有缓存组件仍包含LVGL等依赖，不意味着无屏运行时创建了显示对象。

单codec路径复用现有模拟麦克风增益、软件音量、静音、24→16kHz采样和软件AEC。固定配置中的 `AUDIO_INPUT_REFERENCE` 没有被上游单codec构造消费，本地没有宣称硬件参考通道已实现。功放电压增益沿既有ES8311配置，不作为Atom实测校准值。

## 手机绑定与提醒

牛牛已选择手机连接设备热点，在配网页读取六位绑定码。Atom复用[Doit无屏呈现流程](doit_headless_adaptation_20261110.md)：首次配置保存重启后重新连接“助手-配置”，访问192.168.4.1读取绑定码；未绑定阶段先开手机窗口，绑定完成后才启用音频。绑定后按该板GPIO41手势重新开窗，页面由session返回板型对应的说明。

继续使用原AP接口限制、随机短期session、10分钟TTL和最多两个热点客户端。热点是开放AP，页面对连接该AP的人开放，不是独立账号认证。设备凭据、nonce、通知lease不返回网页。一张计时卡和一张提醒卡保留有界状态；手机可见绘制后才提交seen，固件核对ID／revision／generation／lease，动作等待服务器确认。提示音队列与呈现／送达确认分开。初始绑定阶段还没有启动按键任务，窗口过期需重启重试；无屏离线重新配网手势仍未完成。

手机重连、APSTA／TTL、访问限制、卡片更新竞态、声音和按键均未实测。没有把无屏页生成、构建通过或音频入队写成手机／硬件验收通过。

## 其他三款仍待实施

- `m5stack-atoms3-echo-base`：固定配置明确关闭PSRAM，128×128屏需要GC9107／ST7789检测；现有AFE／模型／任务及界面内存预算不能直接照搬。
- `m5stack-atoms3r-echo-base`：需要真实面板检测、LP5562背光及PI4IOE初始化／静音；音频WS6／BCLK8／DIN7／DOUT5，与无屏Atom不同。
- `m5stack-atoms3r-echo-pyramid`：另有SI5351、STM32、AW87559及ES8311／ES7210的TDM／参考路径，WS8／BCLK6／DIN5／DOUT7；需要有界启动、检测和重连流程，不能复制上游无限断连循环。

详见 `board_research_v2.5.2.json`。这三款保持调查状态，不因名称相近继承AtomEchoS3R构建结果。

## 构建与交付边界

本轮使用已有SDK5.5.6-dirty、匹配工具／Python及17份本地组件override，Python外网守卫拒绝外网socket／DNS，复用已有依赖副本，没有安装。SDK保留原NimBLE／idf_as_lib改动，override包含无关BSP，因此不是干净发行依赖锁。

新板实际完整构建入口：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board m5stack-atom-echos3r
```

Atom完整构建退出0，随后四款分别在各自隔离目录运行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，全部退出0。实际使用临时 `xy_atom_regression_20261110.ps1 -Stage <目录> -Board <profile>` 同步当前源码并设置既有工具环境，日志为 `atom-regression.log`。无屏不再编译有屏专用的 `configure_page`，最终Atom／Doit日志无该未使用函数警告；初始完整日志仍保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| m5stack-atom-echos3r | 1,615,968 | 2,752,512 | 1,136,544（约41%） |
| doit-s3-aibox | 1,581,296 | 3,145,728 | 1,564,432 |
| yunliao-s3 | 2,245,632 | 3,145,728 | 900,096 |
| waveshare-amoled-1.8-v2 | 2,615,648 | 3,145,728 | 530,080 |

四款模型均为2,203,819字节；Atom的2,359,296字节模型分区还剩155,477字节，后续模型变化需要重新核容量，不能只看应用分区余量。其余三款模型分区为4,194,304字节。

四份实际镜像检查命令均为 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0；项目名、Atom的8MB／其余16MB设置、checksum及validation hash有效。一次性 `xy_record_atom_20261110.py` 退出0，核对每份77个源码／配置摘要及完整main文件集合与当前工作区一致，验证应用／模型不超分区后归档。记录为 `firmware/assistant_espidf/cached_build_attempts_20261110_atom_<profile>.json`，日志、镜像、ELF、配置和源码副本在忽略目录 `evidence/atom-builds-20261110/`。这些记录只覆盖所选入口，不宣称未选择分支已编译。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。矩阵为44个官方变体有源码、144项尚无源码；累计24个官方变体曾分别通过缓存构建。当前源码4项通过、20项历史证据过期、5项依赖预检失败、159项未构建，其中15项已有源码。全部188项实机验收仍为 `not_run`。公共文件变化会使未回归的旧证据过期，不能把累计24项当作当前源码24项通过。

实际执行跟踪修改的 `git diff --check`，退出0；本批新增源码／配置／文档／记录使用 `git diff --no-index --check -- NUL <文件>` 空白审阅，返回1（文件与NUL存在差异），未输出空白错误，仅保留Git的LF／CRLF提示。最终重新逐项核77个输入摘要，四份记录均无不匹配。未运行测试套件或独立类型检查。

只有现有微雪1.8实物，屏控修订仍未确认。Atom的GPIO41、BCLK／MCLK、PA／收放音／增益／静音／关闭重开、唤醒／AEC、8MB／OCT内存、同板OTA及手机流程均待实机验收；Doit和云聊回归也只代表各自缓存编译证据。未运行测试套件、类型检查或长期服务，未安装依赖、刷机／OTA、Git写入、上传或发布。并发网关／运行时及自然插话改动保留。
