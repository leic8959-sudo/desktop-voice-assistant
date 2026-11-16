# Maza A01 2.9电子纸基础接入

2026-11-09，Asia/Shanghai。继续固定小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个发布目录／188个变体。新增Maza默认面板源码候选后，39个本地profile对应38个官方变体，另有1个历史微雪兼容名；150项仍无本地实现。源码／缓存编译与设备完成状态分开，不能把此次入口解释为Maza支持的所有FPC屏幕均已适配。

## 固定硬件与来源

profile=`maza-ai-esp32-s3`，应用身份=`assistant_maza29`。读取[固定板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/maza-ai/esp32-s3)的config／实际板类／README，以及同一提交的`main/display/epd_display/epd_display.cc/.h`、`epd_display_2_9.cc/.h`。公开副本在忽略目录`evidence/maza-public-20261109/`。

默认A01 2.9英寸面板RAM为128×296，实际LVGL表面为**296×128横屏**；像素(x,y)映射为RAM(y,295-x)。帧传输精确4736字节，不把上游5000字节分配长度当成面板有效RAM。SPI3 mode0／40MHz、MOSI13／CLK14／CS46／DC17／RST18／BUSY21，BUSY高表示忙；速率沿固定实际代码，尚无电气实测。SPI硬件CS负责命令／数据片选，复用已有SPI驱动而非逐字节软件轮询。

单ES8311，输入输出24kHz：I2C0 SDA48／SCL47，MCLK6／BCLK5／WS3／输入4／输出2／PA1。复用既有单codec和24→16采集／24kHz播放参考、静音PCM清零路径。GPIO12按原板初始化为高，BOOT9／离散触摸10／音量上8／下0，输入先等完整释放，多键／跨键走原取消逻辑。BOOT和触摸键都用于根页按住说话／卡片选择确认，没有坐标触摸。LED45、低功耗／软件断电及其他扩展功能未接入。

Flash16MB／Octal PSRAM来自固定默认配置，未测量实物。依赖沿已有SDK5.5.6和组件，未安装新驱动。命令及C221 25C全刷／局刷波形数据附固定来源及MIT通知于`main/third_party/xiaozhi_maza`，未改变项目整体许可证；上游头自述“by EPDiy”，独立于该固定源码之外的来源尚未确证，未据此推断其他屏幕或温度下可用。

## 刷新任务与错误边界

新增`paper_display.cpp`只负责Maza面板，不将电子纸塞进SSD1306或40ms点云刷新。LVGL全帧RGB565在短回调中转换为面板单色布局，覆盖一个“待刷最新帧”；独立任务取得快照后执行SPI／BUSY等待，不持有LVGL锁。待刷帧可覆盖合并，队列容量固定，不按通知次数分配内存；已显示／转换／待刷帧各4736字节，DMA传输帧另4736字节，全屏RGB565绘制缓冲75776字节，外加固定任务与互斥资源。

SPI队列和完成等待分别显式1秒。事务结构、DMA帧和较大常量数据均持续有效；SPI超时后停止后续传输，保留任务／事务／内存，避免一个尚未完成的DMA引用已退栈或复用的缓冲。BUSY初始／释放等待每次上限10秒；真正全刷／局刷命令还须在1秒内观察到BUSY高，再等待释放，持续低或持续高均失败，不把“恰好低”当作实际刷新证据。该握手及期限需A01实机确认。

首次及每20次变化的局刷后，按固定初始化／全刷／建立全刷基图／进入局刷波形顺序执行；普通更新使用局刷命令。两次物理刷新结束后间隔至少1秒，相同像素可复用已完成的显示状态。20次是本地候选刷新策略，不是已验证的厂家残影／寿命保证；波形仅来自C221 25C表，温度补偿和其他FPC面板不在已完成能力中。

任一SPI／BUSY／帧锁错误会记录具体错误并停止后续画面呈现；保留原图，重启恢复路径仍需实机核对。不会无限重试或在LVGL锁里等待面板。屏幕没有背光；通用亮度和熄屏偏好不抹除反射式面板图像，保留最后画面，也不声称已经实现深睡。

## 静态界面与物理显示确认

Maza采用白底黑字、14号中文正文、10号ASCII状态行，不创建点云、呼吸／淡入／滚动动画。296px宽显示配对码、标题、正文和确认按钮。正文按完整17px字行分页，每10秒静态翻页：根页最多5行／85px，卡片每页3行／51px；过长标题加入分页正文，避免标题省略后无法读全。按钮文字与选中反色保留，提示“短按换项，长按确认”。倒计时显示向上取整的约10秒刻度，逻辑结束／声音仍走既有计时，不要求纸面每秒刷新。

`paper_display_submitted_generation()`记录已提交帧，完成代数只在实际刷新握手成功或已显示的相同像素被确认后发布。创建／恢复卡片、选项改变及分页时保存当前代数，后续需完成更新后的最新帧才认为可确认。提醒的shown回执、notification lease确认和卡片长按提交等待此条件，保留原卡片ID／revision、联网与待确认检查；刷新中按键被消费，不回落为录音或把旧选项当新选项确认。画面失败后不发送新卡片已显示回执，也不接受依赖新画面的确认。

以上是源码设计；没有执行并发／超时故障注入、光学检查或真实输入验收。普通LVGL绘制完成计数只表示内存刷新，不能代替面板完成代数。配对日志表示已排队，不伪称六位码已在纸面显示。

## 实际构建命令

```powershell
$env:TMP = 'I:\xy_board_builds_20261109'
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board maza-ai-esp32-s3
```

复用已安装SDK5.5.6-dirty／17个本地override和Python外网守卫。HU-087、Kevin Box-2、微雪AMOLED1.8-v2分别在各自隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`复核共用UI／音频／电源分支。最终构建、镜像与源码匹配结果在本批收尾后补充。

牛牛只有现有微雪1.8，Maza实物缺失。面板版本／波形温度／SPI速率／残影与全局刷、BUSY握手和DMA失败恢复、画面合并与回执／按键竞态、纸面配对和分页、ES8311声学／PA1／唤醒／AEC、实际容量／供电和同板OTA均未验收。未运行测试套件／类型检查，未安装、改SDK、刷机／OTA、启动服务、Git写入、上传或发布。网关／运行时及自然插话改动保留，全部188项实机验收仍未完成。


## 最终实际构建与镜像

Maza完整缓存构建及最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=maza-ai-esp32-s3 reconfigure build`均退出0。最终构建纳入BUSY上升／释放握手、过长标题加入分页正文，以及只有焦点实际变化才更新画面等待代数的修正，避免单个“关闭”按钮短按后等待不存在的新帧。

HU-087、Kevin Box-2、微雪AMOLED1.8-v2的最终独立回归构建全部退出0。四款各自执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用身份、16MB设置、checksum及validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| maza-ai-esp32-s3 | 2,195,120 | 950,608（约30%） |
| hu-087 | 2,155,328 | 990,400 |
| kevin-box-2 | 2,188,624 | 957,104 |
| waveshare-amoled-1.8-v2 | 2,615,520 | 530,208 |

每份记录有67个源码／配置摘要及完整main文件集合与当前工作区一致；Maza最终配置启用ES8311及现有中文／ASCII字体。记录为`cached_build_attempts_20261109_maza_<profile>.json`，日志、镜像、配置和源码副本在忽略目录`evidence/maza-builds-20261109/`。SDK仍保留原NimBLE／idf_as_lib改动，自报5.5.6-dirty；17个override中非当前BSP及既有LVGL弃用警告保留，不属于干净发行依赖锁。

当前38个官方变体有源码，4项当前源码缓存构建通过，16项历史通过因公共文件变化为stale，3项GC9A01预检失败，15项尚未构建。累计20个官方变体曾各自编译通过；150项无本地源码，全部188项实机验收仍未执行。Maza只登记默认面板候选，不表示替代FPC屏幕、温度范围、光学／声学或整板低功耗能力完成。

实际执行`git diff --check -- firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/CMakeLists.txt`及对9个新增／未跟踪源码和文档的`git diff --no-index --check -- NUL <文件>`审阅，无空白错误，保留Git的LF／CRLF提示。执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`同步清单；未运行测试套件、类型检查、设备故障注入或刷机。
