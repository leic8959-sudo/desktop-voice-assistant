# B1：ESP32-S3-BOX与BOX-Lite源码入口

后续更新：旧BOX与BOX-Lite已按固定小智表/低有效复位/WS47-BL45接入本地共享实现，分别首次整板缓存构建通过；Lite采用原生16kES7243E-ES8156，无触摸/无SD差异保留。另对BOX3/AMOLED标准双槽打开参数作两通道约定修正并重建。见[本批记录](box_family_native_20261113.md)。

2026-11-09，Asia/Shanghai。新增 `esp32-s3-box` 与 `esp32-s3-box-lite` 两个独立源码入口。官方BSP的接口和精确codec源码已静态核对；没有安装／解析依赖、编译、运行测试、类型检查、启动服务、刷机或OTA，不宣称当前可刷或物理可用。

## 独立依赖与硬件

| 项目 | BOX | BOX-Lite |
| --- | --- | --- |
| 官方组件 | `espressif/esp-box` 3.1.0~2 | `espressif/esp-box-lite` 2.1.0~2 |
| 板型专用codec | `espressif/esp_codec_dev` 1.1.0 | `espressif/esp_codec_dev` 1.1.0 |
| 声音输入／输出 | ES7210／ES8311 | ES7243E／ES8156 |
| 显示／触摸 | 320×240，有触摸 | 320×240，无触摸 |
| SD缓存 | 官方声明无SD，不编译本地快照路径 | 同左 |
| 输入方式 | 原有GPIO0按住说话，卡片触摸 | 普通页GPIO0按住说话；卡片页短按选择、长按后松手确认 |
| 应用名 | `assistant_box` | `assistant_boxlite` |

两个官方BSP都声明codec `~1.1` 且IDF `<6.0`，头文件标为不再维护。本次没有强改它们的依赖范围，也没有把其他板型的codec1.5.11全局降版，而是在各自manifest固定1.1.0，隔离默认volume／产物目录和应用名。IDF仍声明5.5.x、LVGL9.6.0~1与LVGL port2.9.0，实际传递依赖解析和编译兼容性尚未确认。

曾查询ESP-ADF v2.6源码，但其中codec实际声明1.0.3、types头写1.0.1，不能当作1.1.0依据；随后读取官方组件注册表的**精确1.1.0源码ZIP**，只在内存打开，核对manifest、头文件、I2S数据接口及ES7243E／ES8156源码。确认当前需要的`channel_mask`、`mclk_multiple`、open/read/write/close、增益／音量／静音接口存在；这不是SDK编译验证，也没有安装该源码包。

## 音频边界

两款都使用BSP各自返回的输入／输出句柄。BOX复用当前ES7210／ES8311的双槽选第0槽、单声道24kHz流和软件参考；BOX-Lite使用其ES7243E／ES8156实例，不再把音频日志写成ES7210／ES8311。

小智固定BOX-Lite配置选16kHz，而本项目候选继续使用已有24kHz输入／输出及24→16kHz FIR。精确codec1.1.0的ES7243E实现固定LRCK=MCLK/256，未提供一张可据此宣称“24kHz已实测”的采样率表。这里选择的是待验证源码路径，必须在实物测时钟、通道和声音后决定是否接受；若要改成16kHz，应同时调整采集、播放和软件参考重采样，不能只改常量让音频时间轴失真。没有把该选择说成已复制上游16kHz验收结果。

## BOX-Lite无触摸卡片操作

普通页面按下／松开BOOT仍走原有手动语音请求。卡片可见时，按下先交由屏幕处理，**50ms以上短按松手切换可用选项，600ms以上长按松手执行所选动作**；选项有白色焦点框，并显示按键说明。一个选项时短按仍不执行，必须长按后松手；禁用选项不会执行。

实体键和触摸入口共用 `perform_screen_action`，继续使用现有暂停／继续／取消计时、稍后提醒和关闭行为，以及服务端请求／回执，不新增旁路业务处理。按下时绑定可见卡片ID与reference_revision；卡片对象被删除或替换、变为隐藏、版本改变时松手不执行新卡，操作进行中也不重复提交。显示锁失败时消费该次卡片按键，不把本来想操作卡片的按下误转成录音。是否符合实物手感、去抖、旧版本冲突和弱网情况仍待行为验收。

卡片上的本地“关闭”沿用原语义，不代表待办已完成。没有给单键映射支付、家居开关、账号授权或其他新增高风险动作；只使既有屏幕操作在无触摸板上有可达输入。

## 文件与后续入口

新建 `boards/esp32-s3-box/`、`boards/esp32-s3-box-lite/` 清单与默认配置，CMake／板级header／横屏布局／音频日志／构建及烧录白名单分别接入。`display_ui.cpp`与`display_ui.h`增加实体键卡片入口，`main.cpp`在按下时固定该次按键属于语音还是卡片，松手不因中途换页而改用途。原有触摸板型在能力判断后直接走既有语音入口；CoreS3继续用触摸按住说话。

以下命令仅为后续构建入口，**本轮未执行**：

```powershell
powershell -File tools/build_firmware.ps1 -Board esp32-s3-box
powershell -File tools/build_firmware.ps1 -Board esp32-s3-box-lite
```

默认volume分别为`assistant-idf-esp32-s3-box-v1`、`assistant-idf-esp32-s3-box-lite-v1`，产物规划目录分别为`firmware/assistant_espidf/build/esp32-s3-box/`及`.../esp32-s3-box-lite/`。目录名不是已有镜像证据；锁文件和产物摘要仍须实际生成。不同应用名不能互相OTA，不把BOX-Lite当BOX／BOX-3别名。

## 固定来源及许可

- [BOX固定BSP](https://github.com/espressif/esp-bsp/tree/342492186688ddb9469387528912d5312ab075d4/bsp/esp-box)、[3.1.0~2许可](https://components.espressif.com/components/espressif/esp-box/versions/3.1.0~2/license)：Apache-2.0。
- [BOX-Lite固定BSP](https://github.com/espressif/esp-bsp/tree/342492186688ddb9469387528912d5312ab075d4/bsp/esp-box-lite)、[2.1.0~2许可](https://components.espressif.com/components/espressif/esp-box-lite/versions/2.1.0~2/license)：Apache-2.0。
- [codec1.1.0官方精确源码包](https://components-file.espressif.com/components/espressif/esp_codec_dev/1.1.0/download/3818a5c2-fd76-4fc7-ae38-5c2b7f2d7242.zip)：内部manifest确认为1.1.0，LICENSE为Apache-2.0；源码只在内存静态读取，未复制到仓库或managed_components，也未执行。
- [小智BOX-Lite固定配置](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/espressif/esp32-s3-box-lite/config.h)：16kHz与引脚对照依据，不能冒充本项目运行验收。

## 本轮结果与未决项

矩阵整理命令 `python -B tools/sync_board_matrix.py --source-directory <公开元数据缓存>` 成功返回154个目录／188个变体／170个选择项，现有8个对应源码入口、180个仅登记。B1只剩两款T-Circle尚无源码入口，B1全部仍待编译和实机验收；全量目标未完成。

实际只做只读Git／源码读取、公开HTTP许可／源码包调查、JSON整理、diff与空白审查。需要继续提供的证据包括：旧BSP／codec在IDF5.5.2和LVGL9下的实际解析／构建、3MB分区余量、音频时钟／增益／AEC、无SD启动、触摸或单键完整操作、配网／提醒／同板OTA和回滚。Docker引擎此前不可连接，未因此启动长期服务或自行安装工具链；其他并发网关／语音修改未被覆盖。
