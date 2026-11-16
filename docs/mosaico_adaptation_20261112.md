# ESP-Mosaico S31源码候选（2026-11-12）

新增 `espressif-esp-mosaico` 独立入口，接入硬件修订识别、480×480 CO5300／CST9220及单ES8311。**目标SDK未安装，尚未Mosaico编译或实机验收。** 旧S3回归与本板目标证据分开记录。

## SDK和硬件修订

来源为小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的Mosaico目录。虽然config.json声明IDF≥6.1，README明确要求开发提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`；读取该提交的version.cmake为6.2.0。候选manifest选择 `>=6.2.0,<6.3.0`，profile另存精确提交，CMake配置时只读检查IDF仓库HEAD。这个保护目前只复核了源码，未在6.2环境实际执行；HEAD匹配也不能证明SDK没有本地修改。

初始化先只读eFuse USER_DATA前16位：0x0100为v1.0，0x0101／0x0102采用后续修订布线，其他值停止。没有猜测未烧录或未知版本，也没有写eFuse。版本未确认时不套用任一修订的GPIO配置：

| 信号 | v1.0 | v1.1／v1.2 |
| --- | --- | --- |
| I²C0 SDA／SCL | GPIO0／1 | GPIO56／3 |
| QSPI时钟／屏复位 | GPIO44／42 | GPIO42／44 |
| codec电源GPIO56 | 输出高 | 用作I²C SDA，不配置为电源输出 |
| 原板状态灯GPIO3 | 未接入 | 用作I²C SCL，不配置为LED |

共同电源VCC GPIO60低有效，shutdown GPIO57以开漏高释放；PA45启动关闭，INPUT_OUTPUT保留读回。识别错误或初始化失败不复位绑定／NVS，也不探测其他引脚组合。v1.0的单独codec电源和共享总线创建顺序沿固定原板，等待10ms。

候选应用身份 `assistant_mosaico`，S31、CPU320MHz、OCT PSRAM250MHz。16MB是当前固定源码体系／本项目根分区候选，未测实物容量；对应硬件修订和PSRAM频率／容量仍需验收。控制台采用UART，沿该板上游配置关闭USB Serial/JTAG；没有启用相机或占用相机引脚。

## 显示、触摸和音频

CO5300 480×480 RGB565，SPI2 QSPI mode0／40MHz，CS50，D0..3=36／51／35／9；时钟和复位按上述修订选择。精确驱动 `espressif/esp_lcd_co5300 2.1.0`，源码提交 `56f3c5620464c061e968d3358bc897c528e097ee`。

固定原板14条命令转为C++静态字节数组，保留原顺序、长度和延时；唯一显式差异是0x51从FF改为00，防止界面就绪前满亮。随后沿驱动亮度API将设置的0–100映射为硬件亮度，失败传播错误。背光是面板亮度命令，没有套用PWM GPIO；驱动对象失败时保留可能被回调引用的对象，尝试暗屏及关闭PA。

使用两份20行内部DMA缓冲，共38,400字节，SPI最大单次传输19,200字节。与上游全帧PSRAM直DMA配置不同，候选使用项目已有SPI LVGL缓冲方式；没有据上游flag直接宣称本机PSRAM DMA可用。复用现有480×480矩形布局，圆角安全区、颜色字节序和视觉密度待实物验收。

CST9220精确0.1.0，提交 `2d289eb1f14431e345b345c3fd6fc299a555d770`；已读取其身份／legacy与HYN212协议检测、报告长度检查、释放事件和ACK错误传播源码。只使用已知地址0x5A／400kHz，共享I²C0，触摸reset NC，IRQ6按驱动配置但轮询读取。两点容量用于拒绝多点，读错／多点／坐标越界取消并等松手，不把触摸初始化失败静默降为“支持无触摸”。实际两种协议的兼容性仍待验收。

音频单ES8311 BOTH，实际7位地址0x19、codec_dev使用8位0x32；不能沿用默认0x18。I2S0 MCLK54／BCLK37／WS49／DIN40／DOUT52，STD mono16／24kHz，一个IN_OUT设备。复用既有checked data、真正codec增益／静音回调和失败清理，codec PA设NC，由板级读回路径在数字静音成功后启PA。AI键7先松开再按住说话／卡片操作，触摸也保留现有交互。

保留mn7“你好，小燕子”、软件PCM AEC、Wi-Fi配网绑定、WebSocket媒体／真实回执及同板OTA。相机模块EEPROM、闪光灯、相机、SPI NAND、BQ27220电量计配置／封印、IMU、磁传感器、状态灯、断电和原厂手势未实现，不初始化或写入这些模块，也不显示猜测电量。

## 实际命令与结果

新目标实际只读预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp-mosaico --destination I:/xy_board_builds_20261109/mosaico_review_espressif-esp-mosaico_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

退出1：SDK5.5.6不满足 `>=6.2.0,<6.3.0`。没有创建stage、执行Mosaico CMake提交检查、解析完整依赖或生成目标镜像。参考驱动包只归档读取，未安装或加入override；CO5300／CST9220与传递依赖仍待准备。

已授权两项旧S3缓存回归实际终态均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app2,263,824字节、RLCD2,228,000字节，各3MB；model均2,203,819字节／4MB。每项234个main文件、242项输入摘要与最终工作区匹配，23产物归档。实际image_info、nm、size和SDK分区解码均退出0，核对身份／16MB／NVS／双OTA／model及codec、FT或TE；使用既有IDF5.5.6-dirty、17个override、offline_guard和每stage并发度2。**未编译Mosaico源文件或使用SDK6.2。**

147个旧profile对象不变，147项旧音频本地宏条件文本比较一致；旧默认ES8311地址移到相同值的局部常量，比较显式记录归一化。此脚本不展开SDK／include，不是C++预处理或编译。控制协议人工决策、OTA、配网、唤醒、display_ui、纸屏、CoreP4板级和Tab5显示八文件摘要保持原值。

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0；`close_batch.py`核对49份来源、硬件修订映射顺序、14命令及唯一亮度差异、旧profile／研究记录、保护摘要、预检日志、最终输入与产物；`git diff --check`退出0。完整证据在 `evidence/mosaico-source-20261112/`。

当前147官方源码入口＋1历史，共148本地profile，41官方变体待源码。S31四项中3候选／1待实现。累计64官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（77有源码）；188项本轮实机全not_run，唯一微雪1.8不能替代Mosaico。C3／C5／C6唤醒词取舍仍保持待确认。

本轮未运行测试套件、安装SDK／依赖、启动Docker、写Git、刷机、OTA、上传或发布；全量目标继续进行。
