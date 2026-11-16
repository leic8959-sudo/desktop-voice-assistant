# 云聊S3基础Wi-Fi与ES8388接入

2026-11-10，Asia/Shanghai。基线为小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个发布目录／188个变体。云聊新增基础源码入口后，41个本地profile对应40个官方变体，另一个是历史兼容入口；148项尚无本地实现。蜂窝、硬件参考、电池及休眠未接入，基础源码不代表整板适配完成。

## 来源与显示

profile=`yunliao-s3`，应用身份=`assistant_yunliao`。读取[固定云聊目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/yunliao-s3)的config、实际板类、README和power_manager，以及同提交`main/audio/codecs/es8388_audio_codec.cc/.h`；API另对照现有esp_codec_dev1.5.11源码。公开副本在忽略目录`evidence/yunliao-public-20261110/`，URL和边界记入board_research_v2.5.2.json。

320×240横屏ST7789，SPI2 mode3／40MHz，CLK42／MOSI40／DC41／RST45／CS不使用，背光46正向PWM；RGB、反色true、swapXY=true、mirrorY=true、gap0。复用SPI显示类，只有云聊宏选择SPI2及该旋转；LVGL登记也保留同样旋转。横屏采用独立布局和既有16号中文字体（行高20），避免31px行高的定制字体挤入22px标签。

当前固定反色为原默认true，尚未实现原板的IPS模式偏好切换。不同屏控修订需实机核对，不把默认值推广到所有出厂批次。无坐标触摸，GPIO2先见完整释放后用于根页按住说话、卡片短按选择／长按松手确认。原单击／双击／三击和长按关机／网络／AEC手势未接入。

## ES8388与软件回声参考

输入输出均24kHz：I2C0 SDA18／SCL21，MCLK14／BCLK13／WS11／输出12／输入10／PA17。复用已有checked音频数据接口和有界I2S传输；ES8388由已安装esp_codec_dev的驱动创建，不复制codec寄存器驱动。两个input／output句柄共享一个物理codec，关闭任一句柄不禁用另一侧，失败清理只释放一次共享codec。

输入声明两个物理声道、channel_mask选左侧麦克风，输出打开前重置mono参数；继续使用已有24→16kHz采集和软件回声参考，不将另一路硬件参考误当第二麦克风。原板reference=true时的右路0增益／左路增益独立设置未实现为硬件AEC输入。候选待机模拟增益24dB来自原codec构造，播放期仍为现有12dB软件AEC配置；这不是新板声学校准结果。暂停恢复按板级增益常量处理，待机日志也输出真实配置值，其他板保持原30dB。

时钟归属存在明确差异：固定上游类把ESP I2S和ES8388同时设置master，现有ES8388驱动直接写master位。本地由ESP驱动BCLK／WS，ES8388设置slave，避免两个主动时钟源。此配置是静态接口推导，必须用实物确认时钟／数据路由，不能用构建成功当作硬件正常。

原类在打开输出后将模拟输出寄存器46至49设为30（0dB）。本地保留这四个值，在打开状态检查中逐项写入并读回；任何失败或读回不一致返回错误。初次编译暴露write_reg参数要求可写void*，原新增只读字节不匹配；已改为普通uint8_t变量，没有用强制转换绕过检查。共用静音／零音量继续清零物理PCM和软件参考。

## 供电与未完成项目

GPIO3显式配置输出并置高，对应固定Start5V所指的升压初始状态。没有照搬原类先set_level再gpio_config的初始化顺序。GPIO5／6的4G供电使能、ML307串口15／16和网络切换未接入，没有主动使能蜂窝模块。

电池GPIO43闪灯计数、USB47、BT4、充电推断、开机sleep_flag、长按关机、deep sleep及蓝牙状态同步未实现。原默认70%板级音量也未作为新的持久偏好覆盖现有对话／提醒配置。Flash16MB／Octal PSRAM沿固定根默认，非实物测量。SDK依赖未新增安装。

牛牛只有现有微雪1.8。云聊5V电压与供电、ESP／codec时钟归属、左侧麦克风／硬件参考路由、增益和PA17、24kHz声学／唤醒／AEC／静音／重开、ST7789 IPS修订及旋转、按键／配对／提醒与同板OTA均未验收。仍需继续补蜂窝、硬件参考和供电管理，不从全量目标中删除这些能力。

## 实际命令和尝试

首次执行：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board yunliao-s3
```

该次进程TEMP／TMP为`I:\xy_board_builds_20261109`，IDF_PY_BUILD_JOBS=2；wrapper退出1，内部构建退出2，实际错误是上述const写缓冲。原`cached-build.log`保留，不改成成功。确认进程结束后，在原目录对最终源码执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=yunliao-s3 reconfigure build`继续构建。

Kevin Box-2、微雪LCD1.83、AMOLED1.8-v2分别在自己的隔离目录执行相同形式的reconfigure/build回归公共代码。两款旧微雪首次配置失败：临时vendor下9个组件副本缺失。保留`yunliao-regression-missing-cache.log`，按原cached-build-inputs中的目录，从项目已有managed_components补回缺失副本再重试，另存恢复清单。随后重试发现已有4个组件目录也部分缺失：每款分别补回95个codec、61个WebSocket、6563个LVGL及13个历史BSP文件；逐文件审阅显示现有文件与原缓存无差异，只复制原先不存在的文件，第二次配置失败日志另存。恢复后检查原QSPI补丁已生效。没有移除依赖约束或访问组件注册表下载安装。

所有构建沿已安装SDK5.5.6-dirty、17个本地override及Python外网守卫。最终退出、镜像、容量及源码摘要在本批收尾后补充。未运行测试套件／独立类型检查，未改SDK、刷机／OTA、启动服务、Git写入、上传或发布。网关／运行时及自然插话并发修改保留，188项实机验收仍未执行。


## 最终实际结果

云聊最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=yunliao-s3 reconfigure build`退出0；Kevin Box-2、微雪LCD1.83、AMOLED1.8-v2的最终独立回归也均退出0。初次const编译错误及两次旧缓存配置失败均在记录中保留，没有用最终成功覆盖原始失败。

四款分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0，应用身份、16MB设置、checksum和validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| yunliao-s3 | 2,245,504 | 900,224（约29%） |
| kevin-box-2 | 2,188,656 | 957,072 |
| waveshare-lcd-1.83 | 2,329,136 | 816,592 |
| waveshare-amoled-1.8-v2 | 2,615,520 | 530,208 |

每份记录有67个源码／配置摘要及完整main文件集合与当前工作区一致；云聊生成配置启用ES8388。记录为`cached_build_attempts_20261110_yunliao_<profile>.json`，日志、镜像、配置及源码副本在忽略目录`evidence/yunliao-builds-20261110/`。SDK仍保留原NimBLE／idf_as_lib改动并自报5.5.6-dirty；既有LVGL弃用警告和17个override中非当前BSP保留。这不是干净发行依赖锁，不证明实际ES8388时钟、GPIO3供电、模拟音量、参考输入或全部板型正常。

矩阵当前40个官方变体有源码，4项当前源码缓存构建通过、18项历史通过因公共文件变化为stale、3项GC9A01预检失败、15项尚未构建；累计22个官方变体曾分别编译通过。148项尚无源码，全部188项实机验收仍为not_run。

执行`git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`及对7个新增／未跟踪源码和文档的`git diff --no-index --check -- NUL <文件>`审阅，无空白错误，保留LF／CRLF提示；执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`更新清单。未运行测试套件、独立类型检查、设备测试或刷机。
