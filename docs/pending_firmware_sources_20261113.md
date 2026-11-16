# 剩余37个源码入口的隔离工程与环境准备

本批已将37个尚未构建的官方源码入口分别准备为独立工程：27个P4／P4X、4个S31、6个S3。每个工程包含当前main、原始板级manifest、根／芯片／板级defaults、原有分区和独立板型锁。实际复制12210份源码与配置，共209995797字节，复制后SHA-256逐项匹配；1318份既有固件／依赖报告／剩余清单文件摘要未变。

这是源码准备完成；依赖解析、构建和实机验收仍均为`not_run`。没有新增整板构建通过项。全部188变体维持114项当前缓存构建通过、1项历史通过但过期、73项未构建；其中37项有源码、36项无入口。Cardputer ADV的历史产物和必需功能缺口继续保留，不能靠重复构建转为完整支持。

## 交付位置

- [37项独立工程根目录](I:/xy_board_builds_20261109/pending-sources-20261113)：每板的`PREPARATION.md`有SDK条件、直接组件缺项、已知功能缺口及两条尚未执行的具体命令。
- [逐板计划及依赖声明](../evidence/pending-source-preparation-20261113/plan.json)。
- [复制与原文件摘要记录](../evidence/pending-source-preparation-20261113/source-copy-audit.json)。
- [单板准备工具](../tools/prepare_firmware_sources.py)和[本批实际准备脚本](../evidence/pending-source-preparation-20261113/prepare_batch.py)。

隔离目录只包含源码和说明，未带入SDK、组件缓存、旧sdkconfig、dependencies.lock或历史产物。各板原始manifest逐字节保留，没有加入本机override或不相关的历史BSP。后续解析将按各板自己的声明处理传递依赖，不能继承缓存构建的依赖供应链结论。目录是本批快照；固件继续修改后，需要重新准备新目录再构建，不能把旧快照结果记为当前源码。

## 环境分批顺序

当前完整环境只有ESP-IDF5.5.6。以下版本是源码声明要求，不表示相应SDK、组件版本已安装或远程可得。

| 批次 | 数量 | SDK条件 | 重点 |
| --- | ---: | --- | --- |
| 现有5.5.6可覆盖的源码入口 | 31 | 27项`>=5.5.6,<5.6.0`，4项`>=5.5.0,<5.6.0` | 26项P4／P4X，RGB Matrix、Korvo RNDIS及3项有功能缺口的S3；仍缺组件 |
| NT26 | 1 | `>=6.0.1,<6.2.0` | `78/uart-eth-modem=0.7.0` |
| CoreP4X | 1 | `>=6.0.2,<6.1.0` | Hosted2.12.11、WiFiRemote1.6.4 |
| S31 Coreboard／Korvo／Maza | 3 | `>=6.1.0,<6.2.0` | 保留MN7／Octal PSRAM；Korvo另缺GT1151 1.0.1 |
| Mosaico | 1 | `>=6.2.0,<6.3.0` | 还须精确SDK提交`7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，CO5300 2.1.0、CST9220 0.1.0 |

优先从现有5.5.6中的RGB Matrix或Korvo RNDIS准备依赖，再处理P4系列公共组件。P4与P4X保持独立配置和芯片修订检查；Hosted协处理器运行状态仍须实机确认。Korvo RNDIS保留其USB网络入口，不能用Wi-Fi变体的结果替代。

所有27个P4入口都声明Hosted2.12.11与WiFiRemote1.6.4；屏控、触摸、BSP按下表分别补齐。Mosaico缓存里的CO5300 2.2.0不符合其精确2.1.0声明，不能覆盖版本条件或精确SDK提交门槛。

## 牛牛执行环境准备的方式

安装边界来自牛牛提供的全局AGENTS规则：“需要安装或卸载依赖时先通知牛牛，由牛牛执行安装”。因此本批只准备工程和命令，没有运行会下载组件的`idf.py reconfigure`，也没有下载安装SDK或依赖。

例如先准备RGB Matrix：在现有ESP-IDF5.5.6 PowerShell中核对`IDF_PATH`、Python和工具链，然后执行：

```powershell
python "$env:IDF_PATH/tools/idf.py" -C I:/xy_board_builds_20261109/pending-sources-20261113/waveshare-esp32-s3-rgb-matrix -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rgb-matrix reconfigure
```

该命令可能联网取得组件；上面只是待执行命令。解析成功后保留完整日志、生成的`dependencies.lock`和实际组件摘要。各板`PREPARATION.md`另列相同板型的后续`build`命令；只有构建、产物核对和必需功能检查通过，才能更新构建状态。无须切换原工作区板型或修改原项目缓存。

必须另行解决的功能缺口：AtomS3 Echo Base缺本地唤醒、wake clip、自然插话、媒体唤醒、AFE／AEC；Guition两款缺前四项及双app OTA。这些项即使安装和编译成功，也继续登记功能未齐。Cardputer ADV独立留在历史过期项中。无PSRAM有损Opus产品取舍仍待牛牛回复，未自行采用。

## 本批实际执行及边界

实际执行：

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/pending-source-preparation-20261113/prepare_batch.py
git diff --check
```

准备脚本退出0，完成37个目录及摘要记录；`git diff --check`退出0，仅有既有文件的LF／CRLF提示。另对新增准备工具和本文执行`git diff --no-index --check -- NUL <file>`，无空白错误（新增文件差异退出1属于正常结果）。没有运行测试、类型检查、依赖解析或构建，没有安装、刷机、OTA、上传、发布或Git写操作。

全部188项实机状态仍为`not_run`；现有微雪1.8具体屏控修订也未据源码结果推定。固定唤醒词接受范围沿此前决策：C3／C5／C6逐板注明“你好小智”差异，不能用其它芯片的产物证明C3／C6已支持。

## 逐板直接依赖差异

下表基于最新直接依赖报告，并已核对注册、manifest、缓存组件manifest和SDK版本元数据摘要未变；只表示本机直接条件，未解析传递依赖或查询远程版本。每板完整声明保存在原始manifest及`source-preparation.json`中。

| profile | 芯片 | SDK声明 | 当前缓存直接组件缺项／差异 | 必需功能缺口 |
| --- | --- | --- | --- | --- |
| `bread-compact-nt26` | `esp32s3` | `>=6.0.1,<6.2.0` | 78/uart-eth-modem=0.7.0 (missing_component) | 未登记 |
| `espressif-esp-mosaico` | `esp32s31` | `>=6.2.0,<6.3.0` | espressif/esp_lcd_co5300=2.1.0 (version_mismatch)；espressif/esp_lcd_touch_cst9220=0.1.0 (missing_component) | 未登记 |
| `espressif-esp32-p4-function-ev-board` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp32_p4_function_ev_board=5.2.3 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `espressif-esp32-p4x-function-ev-board` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp32_p4_function_ev_board=5.2.3 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `espressif-esp32-s31-function-coreboard-1` | `esp32s31` | `>=6.1.0,<6.2.0` | SDK缺项 | 未登记 |
| `espressif-esp32-s31-korvo-1` | `esp32s31` | `>=6.1.0,<6.2.0` | espressif/esp_lcd_touch_gt1151=1.0.1 (missing_component) | 未登记 |
| `espressif-esp32s3-korvo2-v3-rndis` | `esp32s3` | `>=5.5.0,<5.6.0` | espressif/iot_usbh_rndis=0.3.1 (missing_component) | 未登记 |
| `guition-jc4827w543` | `esp32s3` | `>=5.5.0,<5.6.0` | espressif/esp_lcd_touch_gt911=1.2.1 (missing_component) | local_wake、wake_clip、natural_barge_in、media_wake_word、dual_app_ota |
| `guition-jc4827w543-ext-amp` | `esp32s3` | `>=5.5.0,<5.6.0` | espressif/esp_lcd_touch_gt911=1.2.1 (missing_component) | local_wake、wake_clip、natural_barge_in、media_wake_word、dual_app_ota |
| `m5stack-atoms3-echo-base` | `esp32s3` | `>=5.5.0,<5.6.0` | espressif/esp_lcd_gc9a01=2.0.4 (missing_component) | local_wake、wake_clip、natural_barge_in、media_wake_word、afe、aec |
| `m5stack-corep4x` | `esp32p4` | `>=6.0.2,<6.1.0` | espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `m5stack-tab5` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_ili9881c=1.1.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `m5stack-tab5-p4x` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_ili9881c=1.1.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `maza-ai-esp32-s31` | `esp32s31` | `>=6.1.0,<6.2.0` | SDK缺项 | 未登记 |
| `waveshare-esp32-p4-nano-10.1-a` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-10.1` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-3.4c` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-3.5` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_st7796=1.4.0 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-4.3` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_st7701=2.0.2~2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-4b` | `esp32p4` | `>=5.5.6,<5.6.0` | waveshare/esp_lcd_st7703=2.0.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-4c` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-5` | `esp32p4` | `>=5.5.6,<5.6.0` | waveshare/esp_lcd_hx8394=2.1.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-7` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_ili9881c=1.1.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-7b` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_ek79007=2.0.2~1 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4-wifi6-touch-lcd-8` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-nano-10.1-a` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-10.1` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-3.4c` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-3.5` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_st7796=1.4.0 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-4.3` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_st7701=2.0.2~2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-4b` | `esp32p4` | `>=5.5.6,<5.6.0` | waveshare/esp_lcd_st7703=2.0.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-4c` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-5` | `esp32p4` | `>=5.5.6,<5.6.0` | waveshare/esp_lcd_hx8394=2.1.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-7` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_ili9881c=1.1.0 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-7b` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_ek79007=2.0.2~1 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-p4x-wifi6-touch-lcd-8` | `esp32p4` | `>=5.5.6,<5.6.0` | espressif/esp_lcd_jd9365=2.0.2 (missing_component)；espressif/esp_lcd_touch_gt911=1.2.1 (missing_component)；espressif/esp_hosted=2.12.11 (missing_component)；espressif/esp_wifi_remote=1.6.4 (missing_component) | 未登记 |
| `waveshare-esp32-s3-rgb-matrix` | `esp32s3` | `>=5.5.6,<5.6.0` | esphome/esp-hub75=0.3.6 (missing_component) | 未登记 |
