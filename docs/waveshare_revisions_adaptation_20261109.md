# B1：微雪1.75C、1.8 SH8601与1.8-v2源码适配

后续更新：1.75及1.75C已按各自14命令、复位/音频引脚、16/32MB及SD差异接入共享本地CO5300/LVGL/CST9217路径，两款首次整板缓存构建与2.16共享回归通过，实机未验收。见[本批说明](amoled175_family_native_20261113.md)。

最新阶段见[1.85系列源码记录](waveshare_st77916_adaptation_20261109.md)：21个本地profile对应20个官方变体，168个待源码；新增三板缺驱动未编译。AMOLED1.75C已依据独立厂家页更正32MB容量。以下保留本页对应阶段的数量和证据。

2026-11-09，Asia/Shanghai。继续全量板型目标，新增三个明确硬件修订的源码入口，并纠正之前“1.8”名称与上游发布变体的对应。只做源码、依赖声明、固定来源调查和差异审查，没有安装、依赖解析、构建、测试、类型检查、服务启动、刷机或OTA。

本页的6个对应源码入口／182个待实现是微雪修订交付时的快照；后续批次继续推进，当前总数见[全量总表](all_boards_adaptation_20261109.md)，不据其他板型的进展推断本页硬件已验收。

## 屏控身份更正

小智固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的1.8原版实现使用SH8601，而1.8-v2使用CO5300。此前本项目默认名 `waveshare-amoled-1.8` 固定的是CO5300 BSP2.0.3，不能据此声称已适配上游SH8601原版。现在分别提供：

| 本地profile | 对应上游变体／驱动 | 固定BSP | 独立应用名 |
| --- | --- | --- | --- |
| `waveshare-amoled-1.75c` | `waveshare-esp32-s3-touch-amoled-1.75c`，CO5300／CST9217 | `waveshare/esp32_s3_touch_amoled_1_75c` 3.0.0，adapter0.6.0 | `assistant_amoled175c` |
| `waveshare-amoled-1.8-sh8601` | `waveshare-esp32-s3-touch-amoled-1.8`，SH8601／FT5x06 | `waveshare/esp32_s3_touch_amoled_1_8` 1.1.4，LVGL port2.9.0 | `assistant_sh8601` |
| `waveshare-amoled-1.8-v2` | `waveshare-esp32-s3-touch-amoled-1.8-v2`，CO5300，BSP探测触摸 | `waveshare/esp32_s3_touch_amoled_1_8` 2.0.3，LVGL port2.9.0 | `assistant_amoled18v2` |

旧 `waveshare-amoled-1.8` 默认名仍保留原配置／应用名 `assistant_espidf`，作为已有使用者的兼容入口，不悄悄换成SH8601驱动，也不额外计成一个官方变体。全量矩阵另列 `legacy_profiles`；两个上游1.8变体只映射到上述明确入口。

新增3个本地入口后，共7个本地入口，其中1个为历史兼容入口；实际覆盖的官方变体源码入口从4变为6（补上两项，并用正确SH8601入口替换此前错误映射）。188个官方变体中182个仍无本项目源码入口，所有新入口都未构建或实机验收。

## 本次实现

- 三套板型各自固定 `idf_component.yml` 和 `sdkconfig.defaults`，使用独立默认构建volume／产物子目录；编译宏、BSP头文件和应用名分别选择。不是只有一个名称别名。
- 1.75C与1.75都为466×466，但C型的音频MCLK=GPIO16、LCD复位=GPIO1、触摸复位=GPIO2；非C型为42／39／40。本次使用专用C型BSP，不改写厂商引脚。ES7210输入和ES8311输出由BSP分别初始化，圆屏布局复用，显示锁按`ESP_OK==0`判断成功。
- 官方1.75C头文件明确 `BSP_CAPS_SDCARD=0`，源码没有SD挂载API。`storage.cpp`现按能力编译缓存路径：有SD的板型沿用既有路径，无SD板型返回不支持、明确日志说明不启用本地缓存，不引用不存在的挂载函数／路径。服务端仍是记忆权威副本，未新增替代数据库。
- SH8601与CO5300版本具有相同的本轮已核对I2S／I2C／QSPI引脚、TCA9554复位位0／1／2和ES8311音频路径，复用既有显示复位／有限MIC供电配置；区别在驱动版本、面板初始化、触摸和偏移，分别由对应BSP处理。
- CO5300 BSP2.0.3按触摸探测处理gap：CST816S时16，FT5x06时0。没有给所有v2实物强制写16，也不根据屏幕尺寸猜控制器。实际修订与触摸仍须实物核验。
- `patch_waveshare_qspi.py`新增 `--panel sh8601|co5300`，先核对选定BSP的include驱动，再匹配相应旧注册形式；SH8601的`return lvgl_port_add_disp_rgb(...)`与CO5300的赋值形式分别处理，改成SPI/QSPI注册路径并保留亮度I/O错误。源码形状不符即失败，所有处理成功后才写文件。未在本地managed_components上执行该补丁。
- 构建脚本给1.8两个明确版本选择对应QSPI分支；1.75C走adapter，不调用该补丁。烧录及批次白名单加入明确入口；应用名检查继续防止不同profile间的固件混用。本轮没有生成任何镜像，也未改变正在运行设备的保护能力。

## 后续入口（未执行）

```powershell
powershell -File tools/build_firmware.ps1 -Board waveshare-amoled-1.75c
powershell -File tools/build_firmware.ps1 -Board waveshare-amoled-1.8-sh8601
powershell -File tools/build_firmware.ps1 -Board waveshare-amoled-1.8-v2
```

三个默认volume分别为 `assistant-idf-<profile>-v1`，产物位于 `firmware/assistant_espidf/build/<profile>/`；对应依赖锁必须由真实解析生成。没有把CO5300的既有锁伪装成SH8601／1.75C新锁。依赖安装仍由牛牛执行，Docker环境和实机条件尚未就绪不启动构建／刷机。

## 固定官方来源与许可

- [小智1.8原版实现](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-amoled-1.8/esp32-s3-touch-amoled-1.8.cc)、[v2实现](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-amoled-1.8-v2/esp32-s3-touch-amoled-1.8-v2.cc)：SH8601与CO5300的区分依据。
- [SH8601 BSP1.1.4固定源码](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/c77caf968fa6b11f3b6a416c853c578d012a8cea/bsp/esp32_s3_touch_amoled_1_8)，[Apache-2.0许可](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8/versions/1.1.4/license)。1.0.1固定codec1.1.0，与本项目不符；选择1.1.4的声明范围`~1.5`，实际解析仍待完成。
- [CO5300 BSP2.0.3固定源码](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/9f4030c6e5cb888ad4cc268bfa7584c93ad53e30/bsp/esp32_s3_touch_amoled_1_8)：按触摸探测选择gap，不依赖本地私有补丁作为来源。
- [1.75C BSP3.0.0固定源码](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/010c5fb39ea7f0e996efd1f8e162cc70e569e57f/bsp/esp32_s3_touch_amoled_1_75c)，[Apache-2.0许可](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75c/versions/3.0.0/license)：C型引脚、锁返回值、ES7210／ES8311及无SD接口均按源码核对，未把头文件历史注释中的“ES8311收放一体”当实际实现。
- [小智1.75／1.75C条件引脚](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-amoled-1.75/config.h)。

本轮公开只读获取源码／许可，不复制整套上游工程，不安装驱动包；第三方组件未来随镜像／固件的通知和传递许可仍需补齐。

## 实际审查与剩余工作

实际执行公开HTTP读取、`Get-Content`／`rg`、Git只读状态与diff、矩阵数据整理；`python -B tools/sync_board_matrix.py --source-directory <公开元数据缓存>`成功返回154目录／188变体／170选择项，并记录6个对应源码入口／182个待实现。该命令不构建或测试固件。网络读取版本页曾出现EOF，后续成功获取固定版本资料；没有把失败请求当许可或驱动不存在。

编辑后检查差异和空白。尚需依赖解析、SDK/API兼容性、QSPI刷新／亮度错误、原版与v2触摸偏移、C型复位、无SD启动、MIC供电和收放音、同板OTA与回滚的实际证据。无SD分支、补丁幂等性和其他板型回归都尚未通过行为验证。B1剩BOX、BOX-Lite、T-Circle-S3和V1.1四个官方变体待源码实施；全量目标保持进行中。
