# Doit S3 AIBox无屏接入与手机绑定页面

2026-11-10，Asia/Shanghai。固定基线仍为小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个目录／188个发布变体。新增Doit后，44个本地profile对应43个官方变体，另一个为历史兼容入口；145项尚无本地实现。源码／缓存编译与手机网络、设备实机验收分开。

## 人工选择与产品行为

牛牛本轮明确选择“手机连接设备热点，在配网页读取绑定码”。决策在headless_ui.cpp配对入口留有Asia/Shanghai时间标记。没有为无屏板分配虚拟OLED，也不把收到提醒当成屏幕已经显示。

- 首次配网：手机连接“助手-配置”，访问192.168.4.1填写Wi-Fi和服务地址。保存后设备按原流程重启；手机重新连接同一设备热点，刷新页面读取六位绑定码，在控制台添加设备。音频在绑定完成后才初始化。
- 已有网络、尚未绑定：STA连接服务的同时开启AP查看窗口，六位码及剩余有效时间仅在设备热点页面展示。绑定仍走原device_nonce／device-result／凭据保存流程，网页不参与领取凭据。
- 绑定后：同时按住音量上／下3秒开启10分钟查看窗口；普通录音键仍用于按住说话。页面显示状态、提醒和计时内容，提供稍后、关闭提示、暂停／继续／取消。操作已提交与服务器确认分别显示，不把提示送达写成待办完成。
- 窗口到期后会话先失效，再停止HTTP服务，APSTA降回STA；没有STA的初始配网页停止Wi-Fi。再次查看需物理组合键。初始绑定阶段尚未启动录音／组合键任务，窗口关闭或绑定超时后需重启重试；无屏离线重新配网的原三击手势尚未实现。

热点沿原配网方式为开放AP、最多两个客户端，配对／提醒页面会对当前连接该AP的用户开放，并不是独立账号认证页面。API再核对socket本地地址确属AP接口、10分钟窗口和随机128bit短期会话头；STA侧请求不得读取绑定码或卡片。网页无CORS授权、不允许嵌入框架、不缓存，配置提交和卡片操作必须带本次会话。窗口可由再次物理操作延长，普通轮询不续期。设备凭据、Wi-Fi密码、device_nonce及notification lease不序列化给状态页面，卡片文本通过textContent展示。

这些是源码实现与访问边界设计，**未在真实手机或ESP硬件验证**，不能宣称网络隔离、浏览器行为或认证已通过设备验收。窗口开启／APSTA切换和关闭失败会记录错误；HTTP停止失败时会话仍失效，保留句柄供后台重试，不将不能重新开放的窗口返回成功。射频／服务资源恢复仍需故障验收。

## 固定硬件

profile=`doit-s3-aibox`，应用身份=`assistant_doitaibox`。来源为[固定Doit目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/doit-s3-aibox)的config／实际板类／README，以及同提交NoAudioCodecSimplexPdm。公开副本在忽略目录`evidence/doit-public-20261110/`，细节写入调查JSON。

实际构造用PDM麦克风，CLK41／DATA42、16kHz；config中的SCK40没有被它使用，本地也不驱动。扬声器24kHz、BCLK17／WS16／DATA18，左时隙32bit，沿原默认PDM构造。复用digital_audio的16kHz采集直通、24kHz输出软件音量、PCM清零静音和既有AFE／AEC；麦克风具体型号／PDM最小时钟、音量和声学都未实测。

GPIO48沿原板初始化为高，但没有依据把它标成PA／静音脚。GPIO45共阳LED低有效，聆听／回答常亮，错误或提醒快闪，配网／绑定／未联网慢闪，空闲熄灭。BOOT10、音量上15／下9，上拉输入，先等完整松开，多键／跨键进入原取消与等待释放路径；无虚构touch GPIO。原reset多击、长按跳音量0／100和其他出厂手势不直接照搬。Flash16MB／Octal PSRAM沿固定默认，非实物测量。

## 无屏呈现与提醒闭环

CMake在无屏profile选择headless_ui.cpp，其他板继续display_ui.cpp。没有创建LVGL display、画布或点云资源；现有依赖缓存仍含LVGL／历史BSP，不等于运行时创建了这些屏幕。

headless_ui仅保留一张计时卡和一张提醒卡，提醒优先，固定容量、TTL和计时任务处理。与当前有屏产品约定一致，普通操作结果卡片继续关闭；不新建后台业务命令或服务器协议。手机状态快照不含notification lease；动作绑定card_id、reference_revision和本地呈现代数，内容／修订／计时状态或送达lease变化会使旧页面失效。

浏览器页面可见、文本进入DOM并经过绘制帧后才提交seen；固件再核对当前卡片、代数和连接状态，排队shown回执和原notification_ack。没有手机呈现时只保留queued／LED提示，不发送虚假shown或送达确认。重新连接服务会使呈现状态失效，重新呈现；断线期间不清送达lease，避免把丢失的队列消息当完成。seen表示呈现，不证明人已经阅读；队列发送／WebSocket断线竞态仍需真实系统验收。

稍后、暂停／继续／取消沿原screen_action队列等待服务器确认，8秒未确认提示结果待核实；关闭“知道了”只关闭本机提醒，不修改待办完成状态。提醒到达和计时到期均排队现有提示音，提醒声音用于提示打开手机页面，独立于seen／送达确认；静音与免打扰沿原音频偏好。队列接受不代表真实声音已播放，显示送达也不替代声学播放验收。提醒内容没有本地语音朗读，需手机页面读取；此点明确属于当前交付边界。

## 实际构建

```powershell
$env:TMP = 'I:\xy_board_builds_20261109'
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board doit-s3-aibox
```

使用已有SDK5.5.6-dirty／17组件override和Python外网守卫，不安装依赖。WK、T-Circle V1.1、云聊分别在各自隔离目录运行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，回归有屏配网、数字／PDM和模拟codec分支。最终退出、镜像及源码摘要在本批结束时补充。

没有运行测试套件／类型检查、手机浏览器验收、HTTP故障／并发注入或实际联网／绑定／提醒操作。没有改SDK、刷机／OTA、启动宿主服务、Git写入、上传或发布；原网关／运行时和自然插话改动保留。Doit的AP/STA、手机重连、访问限制／窗口TTL、卡片代数／lease与断连竞态、LED／GPIO48／按键／PDM16kHz／音量静音／唤醒AEC及同板OTA均待验收，牛牛仍只有现有微雪1.8。全部188项实机状态保持not_run。


## 最终实际构建与镜像证据

Doit完整缓存构建及最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=doit-s3-aibox reconfigure build`均退出0。最终构建纳入查看窗口停止失败处理、页面5秒HTTP超时与可见呈现确认、按钮绑定当时卡片、开机先释放再组合键、以及提醒声音与手机送达确认分离。

WK、T-Circle V1.1、云聊的最终独立回归构建均退出0。收尾修改只涉及未被这三款选择编译的headless_ui.cpp／headless_portal_page.h，已同步这些文件以保持完整源码快照；不据此宣称有屏固件编译了无屏入口。没有为归档再次执行无意义全量构建。

四款分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0，应用身份、16MB设置、checksum及validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| doit-s3-aibox | 1,580,992 | 1,564,736（约50%） |
| wk-esp32s3-dev | 2,156,864 | 988,864 |
| lilygo-t-circle-s3-v11 | 2,224,112 | 921,616 |
| yunliao-s3 | 2,245,632 | 900,096 |

每份记录有76个源码／配置摘要及完整main文件集合与当前工作区一致；Doit生成配置启用dummy codec，完整构建日志确认headless_ui／portal／数字音频被选中编译。记录为`cached_build_attempts_20261110_doit_<profile>.json`，原始日志、镜像、配置与源码副本在忽略目录`evidence/doit-builds-20261110/`。SDK仍保留原NimBLE／idf_as_lib修改并自报5.5.6-dirty，17个本地override包含非当前BSP；这不是干净发行依赖锁，也不能证明AP接口限制、真实手机绑定、提示音、卡片送达或其他板型通过验收。

矩阵当前43个官方变体有源码，4项当前源码缓存构建通过、19项历史通过因公共文件变化为stale、5项依赖预检失败、15项尚未构建；累计23个官方变体曾分别编译通过。145项尚无源码，全部188项实机验收仍为not_run。

实际执行`git diff --check`对main／wifi_provisioning／audio／CMake跟踪改动及对11个新增／未跟踪源码和文档的`git diff --no-index --check -- NUL <文件>`空白审阅，无空白错误，保留Git的LF／CRLF提示；执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`更新清单。没有运行测试套件、类型检查、手机模拟／浏览器检查、设备网络／声学／故障测试，也没有刷机、提交或发布。
