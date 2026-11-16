# ESP-Spot C5源码与缓存构建（2026-11-12）

新增 `espressif-esp-spot-c5` 无屏原生入口，应用身份 `assistant_spotc5`，并完成本机SDK5.5.6的实际C5缓存编译／链接。当前151／188官方变体有源码入口，37项待源码；另1历史入口，共152本地profile。**构建通过不是实机或声学验收通过，Spot尚未刷机。**

## 固定来源与内存布局

小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的板类、config.h/json、README和Button接口均按Git blob核对。README指定C5-WROOM-1-N8R8，config指定8MB Flash；本板配置QUAD80MHz PSRAM，SDK约束 `>=5.5.6,<5.6.0`。这些是来源声明，实体模块容量和修订仍待核实。

8MB分区保留NVS0x9000/0x4000、OTA数据0xd000/0x2000和PHY0xf000/0x1000；app0从0x20000、app1从0x320000开始，各3MiB，storage为896KiB，model从0x700000开始占1MiB。模型只选择wn9s_nihaoxiaozhi，无MultiNet；本次实际生成模型125,951字节。已解析实际分区二进制，未靠CSV推断产物布局。

## 硬件与音频实现

| 模块 | 当前源码路径 |
| --- | --- |
| MCU／外设供电 | app_main早期先保持MCU2高，PA23先低，再保持外设0高；先锁存目标电平、释放旧hold、设INPUT_OUTPUT、读回后hold，保留软件重启供电意图 |
| 功放 | PA23 INPUT_OUTPUT，100ms互斥保护与pad读回；codec PA=NC，实际DAC解除静音确认后才启用PA |
| I2C／codec | I2C0 SDA25／SCL26；ES8311 BOTH、一个checked IN_OUT设备，真实静音／增益callback结果传播 |
| I2S | I2S0 mono16kHz，MCLK=NC，BCLK7／WS8／DOUT9／DIN6；use_mclk=false，按源板与现有codec驱动从BCLK取得时钟 |
| 音频算法 | 复用16k采集、网络PCM24k及16k扬声器FIR／软件参考，AFE_TYPE_SR core0、C5 SR_LOW_COST AEC filter2；AEC或buffer创建失败停止启动 |
| KEY | GPIO5高有效，来源Button(KEY,true)及公共Button接口共同确认；先松手再PTT，BOOT或两键重叠取消当前按压 |
| BOOT | GPIO28低有效；全部松手后、KEY保持松开时长按3秒，打开既有手机查看窗口，不擦除绑定／凭据 |

GPIO锁存和读回只证明API／pad状态，不证明电源波形、实际扬声器或麦克风行为；上电、软件重启、掉电与电池供电仍需实物验收。没有移植原厂双长按关机、十分钟深睡、IMU唤醒或电池／充电推断，GPIO2／0不在本实现中主动拉低。

## 无屏绑定和固定词

复用已存在的headless_ui和手机热点页面，在音频初始化前显示六位绑定码；卡片状态、seen回执、提醒未读和DND边界沿现有实现。KEY按住说话，BOOT用于手机页面。手机页面提示本板实际唤醒词 **“你好小智”／wn9s_nihaoxiaozhi**，与原有自定义“你好，小燕子”分别记录。

固定模型在音频／控制通道前核对完整模型名、元数据词、16kHz单声道及单词，缺失时停止后续启动；不退成正常的纯按键设备。现有固定后端阈值采用模型默认基线、播放候选加0.05；这是未声学校准的技术选择。C5 SR AEC不等同于S3 FD/NLP效果，真实播放期唤醒、自然插话、CPU／堆峰值和吞吐均未实测。

RGB27、BMI270／IRQ3、电池ADC4、充电、睡眠与原厂MCP未实现。没有用MCU保持脚电平当充电证据，电量保持未知。

## 实际构建过程与产物

先执行只读直接依赖预检，退出0：SDK5.5.6满足约束，16个现有兼容组件可用、1个目标不兼容组件被排除；未创建预检目标目录。该检查不等于完整依赖解析。

随后使用已有SDK、工具链和组件，在隔离目录 `C:/Users/29248/AppData/Local/Temp/xy_cached_9247fb9f6d62` 构建，保留Python外网阻断。首次构建因新Spot驱动缺少driver/gpio.h声明而失败；日志、当时源码和输入摘要已归档。只补齐该头文件后，在同一隔离目录继续构建，idf.py最终退出0。

实际命令记录：

```text
tools/build_cached_firmware.ps1 -SdkPath C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 -ToolsPath C:/Espressif/official-5.5.6 -PythonEnvironment C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env -Board espressif-esp-spot-c5
idf.py -DIDF_TARGET=esp32c5 -DASSISTANT_BOARD=espressif-esp-spot-c5 build
```

| 产物 | 本次实际结果 |
| --- | --- |
| 应用 | 1,550,400字节；3MiB最小应用槽检查通过，约51%槽空间剩余 |
| 应用SHA256 | f5d4f2b33eaf4a670a49435818440657ac90940d4e59d51287311450e8856368 |
| 模型 | 125,951字节，SHA256 fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b，与前批单模型包相同 |
| 镜像检查 | esptool image_info退出0，checksum及validation hash均valid |
| ELF | readelf -h退出0，ELF32／RISC-V；project_description目标esp32c5、应用名assistant_spotc5 |
| 分区 | gen_esp32part.py读取产物二进制退出0，双3MiB app、896KiB storage、1MiB model与本板布局一致 |

SDK报告v5.5.6-dirty，其既有dirty路径和HEAD另存；本轮没有更改SDK。组件通过本机override解析，不是干净安装／规范发行依赖锁。构建含固定词、C5音频和Spot电源／按键所选分支，不能证明微雪C5／Moji2或其他板型也编译通过。

28项产物／日志摘要、255项源输入摘要及最终源码快照归档，权威构建记录：`firmware/assistant_espidf/cached_build_attempts_20261112_spot_c5.json`。未把测试套件、模型推理、实体音频、网络业务或OTA验收写成构建成功的推论。

## 收尾与当前状态

执行矩阵同步、来源／快照／差异审阅和git diff --check；结果见 `evidence/spot-c5-source-20261112/final-audit.json`。151个旧profile保持原文，控制协议人工决定、既有固定词后端、无屏绑定决策代码、OTA、凭据和存储代码未修改。只为Spot扩展供电／按键／音频／无屏选择。

当前151官方源码入口、152本地profile、37待源码；构建账本1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run。本轮首次新增一个C5目标缓存构建通过，旧64项缓存结果仍待当前源码重验。188项本轮实机全部not_run，只有现有微雪1.8可用于实体验收。

未安装依赖／SDK、启动服务／Docker、执行单独测试或类型检查、写Git、刷机、OTA、上传或发布。全量目标继续进行，仍需清洁依赖构建、板卡实体容量和电源／音频／声学／手机业务及同板HTTPS OTA验收。
