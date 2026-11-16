# 虫洞 ESP32-S3 EYE 适配（2026-11-10）

新增发布变体 `chd-esp32s3-eye`，独立应用 `assistant_chdeye`。依据小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的配置、实际板类、README和ES8311音频类。只有现有微雪1.8实物，本板无实机验收；公开清单仍154目录／188变体。

## 板级差异与实现

| 项目 | 虫洞 EYE |
| --- | --- |
| 显示 | 240×240 ST7789，RGB565／RGB，反色，不交换不镜像，gap0／0 |
| 显示接口 | SPI3 mode0／40MHz，MOSI47／CLK21／DC43／CS44／RST NC，BL48高有效 |
| UI | 复用240×240布局与16px中文；BOOT0低有效，首次释放后按住说话／无触摸操作卡 |
| 控制总线 | I2C1 SDA4／SCL5 |
| 音频 | 单ES8311，16kHz输入／输出、mono16；I2S0 MCLK2／BCLK42／WS45／DIN41／DOUT46 |
| 时钟 | ES8311使用MCLK，名义256fs／4.096MHz；不是其他CHD BOX的双codec或BCLK派生路径 |
| 功放 | GPIO40高有效，启动先锁存低，codec PA设NC，外部显式控制静音生命周期 |
| Flash／PSRAM | 固定发布未覆盖容量，使用其16MB／Octal发布默认；未测定任意未知模组容量 |

固定板类初始化总线为I2C1，GetAudioCodec却传I2C_NUM_0和I2C1句柄。本地将BSP_I2C_NUM、创建总线和codec控制端口统一为1，保留实际SDA4／SCL5；不把这处上游参数矛盾复制到新的构建入口。GPIO41是音频输入，不当成按键；GPIO43／44是LCD DC／CS，不启用UART0。

音频复用现有单ES8311双向handle：16kHz原生采集进入既有16kHzAFE，服务器PCM24播放经过已有24→16kHz路径；软件参考及AEC沿用共用管道。待机增益30dB、播放12dB，实际增益／静音回调返回值另行检查。逻辑音量不采用StickS3专有60%缩放。

功放40由板级初始化保持关闭，codec内部PA pin设NC：静音／关闭先关GPIO40，取消静音在实际codec mute回调成功后开功放，GPIO返回错误向上传播。显示／电源初始化失败记录错误并尝试关PA后停止主入口，避免该处fatal-check重启。寄存器／GPIO返回值不证明实际静音、电压或无爆音，需要实物。

本批没有创建新的面板／音频库或额外组件依赖，修改限于板型宏、引脚、采样率、单codec控制与显示／启动路由。摄像头初始化、图像翻转／识别、五击清NVS等原厂手势未实现；没有引入自动配置擦除。相机相关GPIO保留不驱动。基础Wi-Fi语音入口不等于完整相机设备功能已完成。

## 同批其他板型调查

Freenove2.8固定板类使用ILI9341默认驱动、320×240 BGR、SPI3 20MHz、PA1低有效，与全动或ST7789板不可混用。上游触摸0x38读取5字节，但任务只判断按下／时长，没有提供可验证的屏幕坐标映射，还忽略读取失败后把状态当释放的风险。本机缺ILI9341组件，本批只归档源码调查，没有注册或编译该板，也不推测触摸坐标。额外读取厂家仓库固定提交 `90004f874ed567b3c09d534d021172cb716e61a1` 的FNK0104AB 2.8专有分支，确认FT6336U与RST18／INT17；其中LVGL示例回调为空、显示为240×320竖屏，仍不能证明小智profile的横屏坐标。

DFRobot S3 AI CAM实际无屏，PDM16kHz输入／I2S24kHz播放，GPIO38／39是PDM时钟／数据，GAIN41仅定义未被实际板类使用，不能假设为功放使能。该板另有Wi-Fi最大功率10dBm与相机配置。本批只归档事实，尚未移植无屏按键／查看窗和PDM路径，也不继承其他无屏板通过状态。

这两项保留inventory_only，未增加源码入口计数。后续需分别核对PDM麦克风时钟、真实容量、ILI9341依赖和触摸控制方案。

## 构建与证据

虫洞EYE完整缓存构建退出0，StickS3／微雪Audio Board公共回归各退出0。只复用本机IDF5.5.6-dirty、匹配工具和17个既有组件副本，没有安装依赖。

新板实际命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board chd-esp32s3-eye
```

旧两板在原隔离目录同步变化main文件／注册表／defaults并刷新目的时间，分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。三份日志实际包含main／audio编译，新EYE的SPI display也在自己的宏下编译；没有把旧板构建套到EYE。

| Profile | 应用字节 | 最小应用分区字节 | 剩余字节 | 结果 |
| --- | ---: | ---: | ---: | --- |
| chd-esp32s3-eye | 2,244,624 | 3,145,728 | 901,104 | 完整缓存build退出0 |
| m5stack-stick-s3 | 2,247,808 | 2,752,512 | 504,704 | 旧单codec／PM1回归退出0 |
| waveshare-audio-board | 2,256,096 | 3,145,728 | 889,632 | 旧双codec／JD9853回归退出0 |

三镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目身份／16或8MB容量／checksum／validation hash通过；`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认EYE实际链接ES8311／显示／16px中文字体，没有误入ES7210／PM1路径。旧两入口仍保留PM1单codec和微雪双codec／专用JD9853。

三份各119项输入摘要、112个main文件集合、合并defaults、实际sdkconfig、main／audio／wifi编译宏、二进制板型身份、应用／模型容量匹配当前工作区。模型2,203,819字节，在EYE／微雪4MB模型分区和StickS3的2,359,296字节模型分区内。原始日志、镜像／ELF、配置、依赖锁、编译命令、源快照及SHA-256在忽略目录 `evidence/chdeye-builds-20261110/<profile>/`，轻量记录 `cached_build_attempts_20261110_chdeye_<profile>.json`。SDK已有改动、17组件override及LVGL弃用告警保留，不是干净发行构建。

实际同步 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前70官方变体有源码／118待源码、71本地profile含1历史入口；累计43官方变体曾各自缓存编译通过。当前3 passed／40 stale／6历史依赖预检失败／139 not_run（其中21项已有源码），全部188实机not_run。固定板类、配置、README与音频类URL／SHA-256在 `board_research_v2.5.2.json`，原始文件归档于忽略目录 `evidence/chdeye-source-20261110/`。

未实测Flash／PSRAM、240方屏颜色／方向／复位、16kHz MCLK／mono／增益／静音／关闭重开、PA40、BOOT、AFE／唤醒／回声、联网绑定和同板OTA。未运行测试套件／独立类型检查、安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。全量目标仍进行中。

收尾实际执行 `git diff --check` 退出0；本批未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。三份119输入／16项归档产物摘要及三板固定／厂家调查源证据摘要匹配。另核GPIO LED构造签名，DFRobot参数0表示output_invert=0，未依据该值猜成低有效。全部本批构建进程已结束。
