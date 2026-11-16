# Freenove 2.8：源码入口与依赖阻塞

后续更新：本页保留此前状态快照；Freenove2.8与K10现已移除外部ILI9341依赖并分别完成首次整板构建。当前依赖967条声明、70本地/69官方满足直接条件。最新来源、失败修正和验收边界见[本批记录](ili9341_default_sdk_20261112.md)。

日期：2026-11-10，Asia/Shanghai。只实现固定发布变体 `freenove-esp32s3-display-2.8-lcd` 的基础 Wi-Fi 语音源码；独立应用身份 `assistant_freenove28`。目标尚未编译、未生成固件、未实机验收。

## 固定来源与实现

小智 v2.5.2 固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 config.h、config.json、实际板类及 ReadMe；厂家 Freenove_ESP32_S3_Display 提交 `90004f874ed567b3c09d534d021172cb716e61a1` 的 FNK0104AB_2P8_240x320_ILI9341 示例。没有将厂家其他 3.5／4.0 型号的引脚套入本板。

ILI9341 2.1.0 的公开源码固定于 esp-bsp 提交 `93460b932beba7022a8a4ec4186e0d6b7533d05f` 的 `components/lcd/esp_lcd_ili9341/`。实际接口接受 `esp_lcd_panel_dev_config_t`，保留上游默认初始化，没有借用 ST7789。公开 header／source／manifest 仅下载供阅读和证据归档，没有安装组件。该 manifest 的传递依赖 `cmake_utilities: 0.*` 也未完成依赖解析。

| 项目 | 接入范围 |
| --- | --- |
| 屏幕 | ILI9341 默认初始化；320×240横屏，BGR／RGB565，反色，swapXY，无镜像，gap0／0 |
| SPI | SPI3，mode0／20MHz；MOSI11／MISO13／CLK12／DC46／CS10／RST NC |
| 背光 | GPIO45，高有效；复用现有 PWM |
| 音频 | 单ES8311，I2C0 SDA16／SCL15；I2S0 MCLK4／BCLK5／WS7／DIN6／DOUT8；输入输出24kHz mono，启用MCLK |
| 功放 | GPIO1低有效；codec的PA设NC，由外部门控。启动先锁存高电平后配置输出；静音／关闭先关PA，实际DAC取消静音成功后再拉低使能 |
| 输入 | BOOT0低有效，加FT6336U 0x38按下状态；共用ManualInputKey与已有录音／操作卡交互 |
| 布局 | 复用320×240布局和16px中文字体；面板与LVGL方向保持一致 |
| 容量 | 16MB／Octal沿用固定release默认；不是对实际模组容量的确认 |

继续使用既有PCM24网络播放、采集重采样／软件AEC／唤醒流程。保留原逻辑音量范围，没有套用StickS3专有60%缩放。新增宏只进入单ES8311路径，没有调用双codec初始化。

## 触摸边界

厂家示例明确FT6336U、INT17／RST18，但其LVGL坐标读取为空，竖屏示例不能证明当前横屏坐标。固定小智板类读取0x02起始状态，只用触摸是否按下和时长，不提供横屏映射；本轮仅接按下状态，不注册LVGL pointer，`BSP_CAPS_TOUCH`仍为0，操作卡使用通用按键路径。

复用本机已有FT5x06 1.1.1驱动与touch 1.2.1接口；I2C 400kHz、单次传输50ms超时，驱动默认阈值初始化需要实物确认。沿用实际板类不操作RST／INT的行为，没有猜测复位时序。板级MAX_POINTS设2并静态要求至少2，避免将双指截成单指。

上电必须先释放；多指、BOOT与触摸同时按、按住时切换输入来源会取消并等待全部释放。读取失败返回Invalid且清空驱动点缓存，不将失败当成释放确认。驱动自身对大于5的异常点数视作无点，本轮没有扩展协议故障处理，不声称完成异常报文注入验收。触摸和BOOT只进入现有同一按键任务，没有额外触摸任务竞争读取。

横屏坐标标定、原厂单双击与长按3秒重配网、LED42、电池ADC9／充放电显示均未实现。FT6336U兼容性、默认阈值、按下／释放／组合取消、屏幕方向与色彩、音频时钟与增益、PA极性／静音／重开、AEC／唤醒、联网／配对／提醒及同板OTA均待实机验收。现有实物仍只有微雪1.8，屏控修订尚未确认。

## 实际命令与结果

依赖预检实际执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile freenove-esp32s3-display-2.8-lcd --destination I:\xy_board_builds_20261109\freenove-esp32s3-display-2.8-lcd-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1：`Required component is not installed: espressif/esp_lcd_ili9341; nothing was staged`。目的目录不存在；没有启动Freenove编译。记录为 `dependency_resolution=blocked_missing_local_sources`、`build=not_run`，不能把缺组件或旧板回归写成本板编译通过。详见 `native_dependency_preflight_20261110_freenove.json`。

公共代码回归分别在既有隔离stage同步最终main文件并刷新变化文件时间，执行：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=chd-esp32s3-eye reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
```

两条均退出0，日志实际重编main、audio_io、waveshare_spi_display。使用本机IDF5.5.6-dirty、原有工具／Python和17个既有组件override，保留SDK原有修改；Python offline_guard拒绝外网。这不是干净发行依赖构建，新增Freenove宏和freenove_keys.cpp未被两款旧板选择，不能证明其可编译。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| chd-esp32s3-eye | 2,244,624 | 901,104 |
| lckfb-lichuang-dev | 2,263,424 | 882,304 |

两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0，身份／16MB／checksum／validation hash匹配；EYE仍链接单ES8311，立创仍链接ES7210／ES8311／PCA9557／FT5x06。每份121项输入摘要、114个main文件集合、编译宏、生成配置及合并defaults与工作区匹配；模型均2,203,819字节，在4MB模型分区内。两份构建记录为 `cached_build_attempts_20261110_freenove_<profile>.json`，产物／配置／源码归档位于忽略目录 `evidence/freenove-builds-20261110/`。

`python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 实际退出0：72个官方源码变体／73个本地入口含1历史入口／116待源码；累计44变体曾缓存编译通过。当前2 passed／42 stale／6历史依赖预检失败／138 not_run，其中22个not_run已有源码；全部188实机not_run。全量目标仍进行中。

`git diff --check` 实际退出0。本轮未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、执行Git写入、上传、发布、刷机或OTA。手机热点读取六位绑定码的既有人工决策实现未修改。

收尾对照本轮编辑前快照审阅diff；新增文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1表示新增差异）。两份各121输入／16项归档产物及10项固定来源证据SHA-256匹配，预检日志摘要匹配。两项回归进程均已结束。
