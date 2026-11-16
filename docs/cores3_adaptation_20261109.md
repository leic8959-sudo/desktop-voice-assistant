# B1：M5Stack CoreS3源码适配

后续更新：CoreS3已用本地AW9523/AXP、AW88298及FT5x06路径完成首次整板缓存构建，SD和LCD复用GPIO35由现有SDK回调处理，BOX3共享回归通过，实机未验收。见[本批说明](cores3_native_20261113.md)。

2026-11-09，Asia/Shanghai。全量目标的B1批次继续推进，本次把 `m5stack-core-s3` 从仅登记变体推进为自有固件源码入口。没有安装、解析依赖、编译、测试、类型检查、启动服务或连接实物；不能称为可刷固件或实机支持已经通过。

## 采用的硬件接口

- 固定 `espressif/m5stack_core_s3` **3.0.2**，官方源码提交 `1bd663be5317bfc4457335011889eec5db33098d`。它的声明为IDF≥5.4、codec `~1.5`、LVGL port `^2`，与本项目IDF5.5.2／codec1.5.11的声明范围相符；实际解析和API兼容性尚未构建验证。
- 复用官方BSP的AW9523／AXP2101供电和AW88298输出／ES7210输入初始化，不把微雪1.8的复位掩码或供电寄存器写入CoreS3。
- `boards/m5stack-core-s3/`固定直接依赖并指定QUAD PSRAM，显式关闭公共默认的OCT模式。新板的依赖锁仍须由实际解析生成；没有伪造传递锁。
- 320×240屏幕沿用横屏布局；CoreS3根页面预留底部160×36的“按住说话”触摸按钮，压缩普通详情高度防止覆盖。信息／提醒卡和配对遮罩在它上层显示；卡片显示时先关闭卡片或使用语音唤醒，不承诺所有屏幕都有同一个触摸说话按钮。
- GPIO0为音频MCLK，`listen_button=GPIO_NUM_NC`。按键任务保留显示诊断心跳，但不配置或读取CoreS3的GPIO按键。触摸按下／松手／失去按压沿用协议中的原子按住说话请求；启动阶段禁用，协议初始化完成才开放，升级时取消未结束的按住状态；按住期间暂缓信息卡覆盖，避免聆听状态变化把同一次手动录音立刻结束。
- CoreS3用不同输入／输出codec句柄。沿用24kHz、选择第0槽单声道、软件PCM参考和既有重采样／AEC；没有宣称双麦波束成形、硬件回采、自然双讲或声学校准通过。

## 构建与升级身份

源码profile：`m5stack-core-s3`；CMake应用名：`assistant_cores3`；默认volume：`assistant-idf-m5stack-core-s3-v1`；产物目录：`firmware/assistant_espidf/build/m5stack-core-s3/`。

`tools/build_firmware.ps1`、`tools/flash_native.py`及批次入口的显式白名单均加入该板型，维持独立构建／产物目录。OTA沿用应用名检查，不能接受BOX-3或微雪镜像作为CoreS3应用。Python网关的并发语音改动未被本次修改；本次也没有执行OTA分发。

后续依赖由牛牛准备，构建入口为 `powershell -File tools/build_firmware.ps1 -Board m5stack-core-s3`，**本次没有执行**。官方BSP还声明esp32-camera依赖，但相机初始化、拍摄、视觉识别等没有接入本助手，不能据依赖存在称已支持相机。

## 固定来源与许可

- [官方3.0.2清单](https://raw.githubusercontent.com/espressif/esp-bsp/1bd663be5317bfc4457335011889eec5db33098d/bsp/m5stack_core_s3/idf_component.yml)
- [官方BSP供电／codec／显示源码](https://raw.githubusercontent.com/espressif/esp-bsp/1bd663be5317bfc4457335011889eec5db33098d/bsp/m5stack_core_s3/m5stack_core_s3.c)
- [官方IDF5音频实现](https://raw.githubusercontent.com/espressif/esp-bsp/1bd663be5317bfc4457335011889eec5db33098d/bsp/m5stack_core_s3/m5stack_core_s3_idf5.c)
- [官方3.0.2许可](https://components.espressif.com/components/espressif/m5stack_core_s3/versions/3.0.2/license)：Apache-2.0；本次公开只读获取，未vendor官方代码或安装组件。未来二进制随包的组件、相机依赖和其他资源仍须逐项收集许可与通知。
- [小智固定CoreS3引脚](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/m5stack/core-s3/config.h)：交叉核对GPIO0为MCLK、320×240、24kHz及AW88298／ES7210，未复制其整套应用。

静态调查时也读取了新版4.1.0，但它增加esp_video、USB等依赖，且供电实现与3.0.2不同；本次固定与现有SDK声明范围相符的3.0.2，不顺带迁移新版。`m5stack_core_s3.c`在4.1.0已移入src目录，首次按旧根路径读取404后已改查实际源码，未把404当驱动缺失。

## 验收缺口与实际审查

尚需：空环境依赖解析、3MB OTA分区余量、CoreS3面板修订和触摸方向、QUAD PSRAM、触摸按下／松手／卡片覆盖取消、ES7210槽映射、扬声器物理声音、供电恢复、SD卡、软件AEC参考延迟、真实唤醒、提醒与同板OTA／回滚。没有对应实物或恢复备份时不能代写实机结果。CoreS3-SE、CoreP4、Tab5等不是该profile的别名。

CoreS3交付当时只执行公开源码读取、Git只读状态／差异、矩阵JSON整理及 `git diff --check`，没有执行固件构建／测试或类型检查；当时为4个官方变体源码入口／184个仅登记。后续B1又推进了微雪修订并更正屏控身份，当前数量以[总表](all_boards_adaptation_20261109.md)和矩阵为准；本页不把后续数量变化当作CoreS3已经通过依赖／构建／实机验收。

实际矩阵整理命令为 `python -B tools/sync_board_matrix.py --source-directory <此前下载的公开元数据临时目录>`：退出0，仍为154个发布目录／188个变体／170个配置项；CoreS3行更新为`source_profile_present`，依赖解析、构建及实机三项保持`not_run`。

为核对下一步构建条件，本次只读执行 `Get-Command docker,idf.py,cmake,ninja -ErrorAction SilentlyContinue`，当前PATH仅找到Docker CLI；`docker image inspect espressif/idf:v5.5.2 --format '{{.Id}}'` 和 `docker image inspect ubuntu:24.04 --format '{{.Id}}'` 均返回1，原因是`dockerDesktopLinuxEngine`命名管道不存在，不能连接当前配置的Docker引擎。该结果不能证明镜像不存在；只能确认当前无法使用／核验该构建环境。没有因此启动Docker长期服务、拉取镜像或自行安装工具。
