# M5Stack CoreP4X源码候选（2026-11-12）

新增 `m5stack-corep4x` 独立入口，接入ST7102、CST3530、PM1／IOE电源及ES8311＋ES7210。至此固定发布清单中的27个P4变体都有源码候选，**这些P4目标仍未编译或实机验收**。CoreP4X要求独立IDF6环境，不将当前S3缓存构建视为P4证明。

## 来源和SDK边界

发行身份来自小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`；该版本只发布CoreP4X，旧CoreP4留在源码配置中。官方给CoreP4X设置IDF≥6.0，候选进一步固定范围 `>=6.0.2,<6.1.0`，并读取6.0.2的DPI／I2S公共头文件确认接口。SDK未安装，公共头文件不等于完整工具链或构建成功。

M5Stack M5GFX树 `28d47949bdc84f30881623476f555398e2194313` 的CoreP4X元数据、ST7102命令、CST3530实现和电源寄存器操作按Git blob核对归档。精确M5IOE1 1.0.9／M5PM1 1.0.7作为寄存器及引脚枚举参考，不直接安装。厂家示例库会搜索备用地址，候选则使用已确认的0x6E／0x4F／0x58和带超时的SDK事务。

上游引用的 `kevincoooool/esp_lcd_st7102 1.0.4` 同时包含Apache标识及“仅供指定板用户学习、禁止商业倒卖”等用途限制文字，还附带esp_video传递依赖。本候选未引用该组件或复制其驱动。屏幕路径使用M5Stack公开命令表和SDK DPI接口，保留M5GFX MIT及其引用的LovyanGFX许可文本；参考包只在evidence读取。

## 屏幕和触摸

| 项目 | CoreP4X候选 |
| --- | --- |
| 屏幕 | ST7102，480×480，RGB565 |
| DSI／DPI | 双通道各600Mbps，24MHz |
| H同步／后肩／前肩 | 2／40／40 |
| V同步／后肩／前肩 | 4／8／200 |
| 缓冲 | 两帧921,600字节＋三份20行缓冲57,600字节PSRAM |
| 触摸 | CST3530，I²C1 0x58，400kHz，480×480 |
| 方向 | 厂家rotation_offset=2；LVGL软件旋转180°，输入由LVGL同向转换 |

ST7102按厂家顺序软件复位、等120ms、启动SDK DPI，再发送四组共25条命令，保留组间两处20ms延时。SDK6使用 `esp_lcd_dpi_panel_enable_dma2d()`，没有套用5.5的dpi.flags.use_dma2d字段。PPA显式关闭，避免未核对的PPA旋转分支。现有480×480布局复用项目已有布局；物理方向／触摸边角、视觉密度、配对和卡片均待验收。

CST3530沿厂家五个setup命令，报告先读9字节，再按总记录数有界续读，最后发送0xD00002AB确认。固定50字节缓冲只接受总数≤9；单点交互只接受一指、零按键、有效标记和坐标0–479。多点、按键混合、越界或读／确认错误取消触摸并等待松手，避免把错误伪装成一次有效点击。未复制厂家对异常记录截断后继续索引的风险。

GPIO0在源码中既叫BOOT又被README列为IMU中断，候选先使用触摸按住说话和mn7唤醒，不把运动中断当手动键；物理按键接线仍待确认。RTC／IMU未初始化，不占用与I2S WS等共享的引脚。

## 电源和音频

I²C1 SDA11／SCL9使用外部上拉。PM1只在0x6E确认ID／model合成值0x2050，随后按厂家电源序列设置0x06 bit3；IOE只在0x4F进行拥有位的读改写／回读，事务100ms超时并用互斥锁保护PA及PWM更新。其他PMIC／IOE配置保留，不全局重置充电等状态。

精确M5IOE1头文件的 `PIN_n` 名称从1开始，寄存器位从0开始。这与原板注释里的G数字容易混淆，本批按枚举明确映射：

| 名称 | 寄存器位 | 用途 |
| --- | --- | --- |
| PIN_1 | bit0 | 音频电源 |
| PIN_3 | bit2 | PA使能 |
| PIN_8 | bit7 | 触摸复位 |
| PIN_9 | bit8 | PWM1背光 |
| PIN_10 | bit9 | LCD电源 |
| PIN_11 | bit10 | LCD复位 |

草稿误将名称数字作为位号，已在最终回归前纠正。低字节拥有掩码0x85，高字节0x07，PA为低字节0x04。PWM1先写零再启相关输出，1000Hz、12bit占空比并读回；PA启动关闭，20ms电源等待后释放LCD／触摸，再等150ms。回读证明控制寄存器状态，不证明面板真实亮度或扬声器状态。

ES7210 TDM4 MIC1 slot0＋ES8311 DAC24kHz，I2S0 MCLK2／BCLK6／WS4／DIN5／DOUT3。codec PA设NC，由IOE PIN_3承担受控PA；公共路径检查真正静音回调成功后再启PA，关闭时先切PA。保留共享checked data、增益实际返回、软件AEC和mn7。C6 SDIO slot1 CMD44／CLK43／D0..3=45／46／47／48，复位42及上游RESET_ACTIVE_HIGH配置独立保存。

应用身份 `assistant_corep4x`，P4 v3+、16MB根分区及完整NVS保留；绑定、WS媒体和同板OTA沿既有路径。原README称8MB PSRAM，当前M5GFX元数据称32MB OPI，均记录来源，不把任一数字当实物容量；SDK目标默认HEX设置仍需对应硬件修订验收。

## 实际执行结果

新目标实际只读预检命令：

```text
python -B tools/prepare_cached_firmware_build.py --profile m5stack-corep4x --destination I:/xy_board_builds_20261109/corep4_review_m5stack-corep4x_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

退出1：本机SDK5.5.6不满足 `>=6.0.2,<6.1.0`，未创建stage、解析完整传递依赖或目标编译。已有20个微雪MIPI和2个Tab5预检沿各自历史证据，本批未重跑，也未将它们算为新执行。

修正最终引脚掩码后，两项已授权S3缓存回归真实终态退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

每项230个main文件、238输入摘要与最终工作区匹配，23产物归档；立创app2,263,824字节、RLCD2,228,000字节，各3MB；model均2,203,819字节／4MB。实际image_info、nm、size、SDK分区解码均退出0，核对身份／16MB／分区／codec／FT或TE。使用既有IDF5.5.6-dirty、17个override、offline_guard和每stage并发度2，**未编译CoreP4X或使用SDK6**。修正前的早期回归不作为最终摘要依据。

144旧profile对象未改，旧音频本地宏条件文本144项比较相同；这不展开SDK／include，不是目标预处理或编译。控制协议人工决策、OTA、配网、唤醒、display_ui、Tab5板级／显示及微雪MIPI共八保护文件摘要未变。主入口／CMake／音频／board_profile／board_power只增加CoreP4X路径，布局复用现有480×480配置。

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0。`close_batch.py`核对76份来源、25命令、实际引脚枚举及掩码、缓冲上界、旧profile／研究记录、保护文件、预检／构建日志、最终快照和产物；`git diff --check`退出0。完整证据位于 `evidence/m5stack-corep4-source-20261112/`。

当前144个官方源码入口＋1历史，共145本地profile；44官方变体待源码。P4族27项全部为源码候选，全部待目标构建和实机验收。累计64官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（74有源码），188项本轮实机全not_run，仅现有微雪1.8。

相机、SD缓存、IMU、RTC、物理按键、电池／充电、蓝牙与原厂手势未实现。本轮未运行测试套件、安装SDK／依赖、启动Docker、写Git、刷机、OTA、上传或发布；全量目标保持进行中。
