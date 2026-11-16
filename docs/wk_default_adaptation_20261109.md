# WK ESP32-S3默认OLED32配置接入

2026-11-09，Asia/Shanghai。全量目标仍为固定小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个发布目录／188个变体。本批新增WK默认配置源码后，共40个本地profile对应39个官方变体，另有一个历史兼容入口；149项尚无本地实现。源码／缓存编译不代表整板所有可配置组合或实机验收完成。

## 确切发布配置与引脚

profile=`wk-esp32s3-dev`，应用身份=`assistant_wkoled32`。固定[WK目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/wk-esp32s3-dev)的config.json只有一个发布变体，未附加显示选项；固定Kconfig默认依次为`WK_ESP32S3_DEV_DISPLAY_OLED`、`OLED_SSD1306_128X32`、`BOARD_NO_MOTOR_CONTROL`，config.h默认Simplex音频。本地仅覆盖这份默认配置，registry的implementation_scope明确范围，其他组合没有冒充新官方变体或自动记支持。

README标注ESP32-S3N16R8，沿固定根16MB Flash／Octal PSRAM默认，尚非容量测量。固定config、Kconfig、README和实际wk-esp32s3-dev.cc均已读取，公开副本在忽略目录`evidence/wk-nexts3-public-20261109/`，来源和阻塞进入board_research_v2.5.2.json。

SSD1306为128×32，I2C0 SDA8／SCL9、地址0x3C、400kHz、X／Y镜像、无reset。复用已有OLED页转换、1秒事务超时、中文滚动／状态行、六位配对码和卡片确认，未虚构彩屏或坐标触摸。

数字音频分离两个I2S：麦克风16kHz，BCLK42／WS40／DATA41，**左声道**；扬声器24kHz，BCLK47／WS48／DATA21，**右声道**。实际板类显式传入不同slot_mask，不能套HU-087双右或面包板双左。32位槽采集移位12并饱和至PCM16，输出PCM16放32位槽高位，复用已有数字音频有界传输、软件音量、打开状态检查与静音清零。无独立PA GPIO；GPIO47是扬声器BCLK，不能按面包板误配置成按键输入。

操作键为BOOT0、音量上38／下39。启动先等待完整释放，多键与跨键继续走原取消及等待松开逻辑；根页按住说话，卡片短按选择／长按松手确认。音量沿本地对话音量，首次10、持续2秒后每200ms调整5、限定0至100，不更改提醒或静音偏好。当前进程保存，不新增服务器协议或持久化。原上游长按跳0／100、单击切换对话手感未照搬。

## 明确未完成的组合与扩展

只有OLED32／无电机／Simplex候选。OLED64／SH1106、ST7789多尺寸、ILI9341、GC9A01、NV3030B、ILI9486和自定义LCD，电机版硬件、Duplex音频及LED／灯控／扩展接口均未完成。原示例LampController使用GPIO15，本地不初始化或控制它；没有启动电机。一个发布变体有多个编译选项，不等于默认配置编译可覆盖全部组合，后续仍需逐项引脚、驱动、配置及实机证据。

牛牛只有现有微雪1.8。WK的OLED方向／对比度／配对卡片／按键极性与抖动、16kHz左槽收音／24kHz右槽播放、实际时钟／音量／静音／唤醒／AEC、N16R8容量及同板OTA均未验收，源码和镜像不能代替这些结果。

## 下一组的调查结果

- Doit S3 AIBox：无屏，实际GetAudioCodec使用NoAudioCodecSimplexPdm，PDM CLK41／DATA42、16kHz输入，24kHz扬声器BCLK17／WS16／DATA18；config的SCK40未被PDM构造使用。GPIO48初始化高，共阳LED45，BOOT10／音量15／9。仍是inventory_only，需要真正的无屏配对、错误／提醒确认与操作流程，不能挂假OLED。
- 云聊S3：ST7789 320×240横屏、SPI2 mode3／40MHz，ES8388 24kHz与硬件参考，GPIO3升压及4G电源／使能，DualNetworkBoard和复杂启动／关机手势。仍是inventory_only，需要ES8388／参考路由、供电、IPS修订和网络能力分别实现，不能沿ES8311同尺寸板配置。
- 全动S3：实际类为单ES8311，PA1低有效，与config中残留的ES7210／PCA9557标志不一致；ILI9341 SPI2 40MHz、320×240横屏。固定厂家表存在`cmd=0,data={0},length=0x11`和`length=0x29`条目，长度超出单字节数据。另读取公开ILI9341参考API，第三字段确为数据字节数，调用会照此长度发送；参考master不是固定1.2.0包的构建证据，疑似序列仍须厂家／面板证据确认，不能按猜测改成sleep-out/display-on后宣称已适配。该板仍是inventory_only，没有把可疑表复制进固件。

这些调查均有固定板级URL登记，未用候选名字推断硬件。原config中不一致或不合理条目是后续实现输入，不能据此写入未证明的硬件行为。

## 实际构建命令与边界

```powershell
$env:TMP = 'I:\xy_board_builds_20261109'
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board wk-esp32s3-dev
```

使用已安装SDK5.5.6-dirty／17个既有组件override及Python外网守卫，不安装依赖。HU-087、面包板OLED32、T-Circle V1.1分别在原隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，核对共用OLED、数字槽位和PDM分支。最终命令退出、镜像、分区容量与源码匹配在本批结束后补充。

没有运行测试套件／类型检查，未改SDK、刷机／OTA、启动服务、Git写入、上传或发布。网关／运行时及自然插话改动保留。全部188项实机验收保持未执行。


## 最终实际构建与镜像

WK默认配置完整缓存构建、最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=wk-esp32s3-dev reconfigure build`均退出0。HU-087、面包板OLED32、T-Circle V1.1三项独立回归构建均退出0。四款分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用身份、16MB设置、checksum及validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| wk-esp32s3-dev | 2,156,752 | 988,976（约31%） |
| hu-087 | 2,155,328 | 990,400 |
| bread-compact-wifi | 2,156,800 | 988,928 |
| lilygo-t-circle-s3-v11 | 2,224,000 | 921,728 |

每份记录有67个源码／配置摘要及完整main文件集合与当前工作区一致；WK隔离配置确实启用dummy codec、中文14号字体、16MB及Octal PSRAM。记录为`cached_build_attempts_20261109_wk_<profile>.json`，日志、镜像、配置及源码副本在忽略目录`evidence/wk-builds-20261109/`。SDK保留原NimBLE／idf_as_lib改动并自报5.5.6-dirty；既有LVGL弃用警告和17个override中的非当前BSP保留。这不是干净发行依赖锁，也不能证明未选硬件组合已经编译或运行。

矩阵当前39个官方变体有源码，4项当前源码缓存构建通过，17项历史通过因公共文件变化为stale，3项GC9A01依赖预检失败，15项尚未构建；累计21个官方变体曾各自编译通过。149项没有源码，全部188项实机验收仍为not_run。Doit／云聊／全动本轮仅有调查，native_profile保持空。

实际执行`git diff --check`审阅跟踪文件，以及对5个新增／未跟踪源码文件的`git diff --no-index --check -- NUL <文件>`审阅，无空白错误，保留Git的LF／CRLF提示；执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`同步矩阵。未运行测试套件／类型检查、设备测试、刷机或发布。
