# 微雪P4 Nano／P4X Nano 10.1A源码候选（2026-11-12）

本批新增 `waveshare-esp32-p4-nano-10.1-a`／`waveshare-esp32-p4x-nano-10.1-a`。复用公共MIPI与音频生命周期，单独实现Nano的I²C电源／背光和单ES8311。**目标未编译、未实机验收；源码入口不代表可刷写发布。**

## 与一体屏不同的配置

采用小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 中 `waveshare/esp32-p4-nano` 的config、实际类与命令表：

| 项目 | Nano 10.1A候选 |
| --- | --- |
| 面板／格式 | JD9365，800×1280，RGB565，独立200条初始化命令 |
| MIPI／DPI | 双通道各1500Mbps，80MHz |
| H同步／后肩／前肩 | 20／20／40 |
| V同步／后肩／前肩 | 10／4／30 |
| LCD复位／背光GPIO | 均NC，禁止配置GPIO_NC或对它移位 |
| 面板控制 | I²C1 SDA7／SCL8，0x45，100kHz |
| 触摸 | GT911，100kHz，已知0x5D／0x14，无复位／IRQ／坐标变换 |
| 音频 | 单ES8311 BOTH、单IN_OUT设备、单声道16bit／24kHz；没有ES7210／TDM4 |
| I2S0 | MCLK13／BCLK12／WS10／DIN11／DOUT9，PA53 |
| 内存 | 一份2,048,000字节PSRAM帧＋三份20行缓冲共96,000字节，PPA关闭 |

应用身份 `assistant_ws_p4nano`／`assistant_ws_p4xnano`，旧P4最低1.0／P4X最低3.0独立；候选沿16MB根分区、NVS保留、mn7及C6 Hosted2.12.11／Remote1.6.4 SDIO slot1。实际容量、芯片／库版本、协处理器固件和声学效果仍需验收。

## 电源、背光与驱动选择

固定原板明确通过0x45写 `95=11、95=17、96=00、96=FF`，动态亮度写0x96；其原实现每次添加设备且未释放，动态调用还没有超时。本候选保留一个设备句柄，每次写显式50ms超时并检查ACK。启动执行前两项电源命令后写96=00，保持暗屏，界面完成后才将设置的0–100映射为0–255写入0x96；失败返回错误，并尝试再次写零。

原板README推荐专用 `waveshare/esp_lcd_jd9365_10_1`。本批读取其精确2.0.0包，提交 `9daffb7168b3c093a330da700988a22092294eef`：创建函数还会用旧i2c_bus重复创建I2C1、执行不检查结果的0x45写入，并将背光设FF，与本项目共享I2C及启动暗屏边界冲突。它的包头名为 `esp_lcd_jd9365_10_1.h`，与固定原板类的 `esp_lcd_jd9365.h` 引用也不同，不能只按README当作已解析依赖。

候选显式采用此前核对的 `espressif/esp_lcd_jd9365 2.0.2` 公共JD9365接口和固定Nano自己的200条表；复位NC时沿驱动软件复位，保留RGB565／lane_num=2／mirror(false,false)。板级代码拥有0x45初始化，不链接厂家专用驱动；保留其一秒电源稳定等待，但等待期间背光为零。这是有来源的源码替换方案，仍须目标构建和硬件验证，不能据API相同宣称电气兼容。I²C ACK也不等于寄存器回读或真实亮度。

## 单ES8311接入及旧板边界

`waveshare_spi_audio.cpp`为Nano增加单ES8311 BOTH分支、STD mono16输入输出、一个IN_OUT设备和共享句柄；显式排除原P4 MIPI的TDM4配置，不创建ES7210。初始化失败清理避免重复释放同一设备。继续使用既有checked data封装，实际set_fmt／enable错误参与open检查；麦克风增益和DAC静音使用真正codec回调结果。

PA53由板级设INPUT_OUTPUT，codec的PA设NC，继续由公共路径在数字静音成功后开启、静音／关闭时切断并检查GPIO读回。`audio_io.cpp`无需修改：Nano的separate_microphone=false与同一设备句柄保留单codec打开／关闭路径。BOOT35、GT911读错／多点取消和松手门控、HTTP绑定、WebSocket媒体、OTA和软件AEC沿既有链路。

140个旧profile对象、九个面板参数、三份旧命令表、许可、布局和CMake不变。对140个旧profile的音频及18个旧MIPI入口做了158项本地条件文本比较；去掉注释／空白及状态变量重命名后相同。此脚本只解释本地profile宏，未展开SDK／include，**不是C++预处理或编译证明**。control_protocol人工决策、主入口、OTA、配网、唤醒、board_power与audio_io七文件摘要未变。

## 实际执行与当前状态

20个MIPI入口重新实际执行只读预检，完整参数和逐项退出码见本批 `preflight-results.json`：

```text
python -B tools/prepare_cached_firmware_build.py --profile <板型> --destination I:/xy_board_builds_20261109/nano_review_<板型>_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

全部退出1且未创建stage。两项Nano首缺JD9365精确2.0.2；其余首缺各自面板驱动，GT911／Hosted／Remote与完整传递依赖待准备。48份原板、两种驱动、SDK／codec参考已归档核摘要，仅供读取，未安装或加入override。

两项已授权旧S3缓存回归实际退出0，完整包装命令和会话终态见 `build-command-results.json`：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app2,263,824字节、RLCD2,228,000字节，各3MB分区；model均2,203,819字节／4MB。各212个main文件、220输入摘要与本批最终源码匹配，23产物归档。实际image_info、nm、size、SDK分区解码均退出0，核对身份／16MB／NVS／双OTA／model及codec、FT或TE符号。使用既有IDF5.5.6-dirty、17个override、offline_guard和每stage并发度2；**S3未编译Nano／P4分支**。

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0。`close_batch.py`核对来源、Nano命令字节／顺序／长度／延时、旧板条件文本、旧profile、保护文件、预检日志、最终快照和构建产物；`git diff --check`退出0。完整证据位于 `evidence/waveshare-p4-nano-source-20261112/`。

当前141个官方源码入口＋1历史，共142本地profile；47官方变体待源码。P4族27项中24源码候选／3待实现。累计64个官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run，其中71有源码。188项本轮实机全部not_run，只有微雪1.8实物。Nano的Ethernet分支、相机、SD缓存、蓝牙、电池与原厂手势未实现；WiFi基础终端仍待完整验收。

本轮未运行测试套件、安装SDK／依赖、启动Docker、写Git、刷机、OTA、上传或发布。
