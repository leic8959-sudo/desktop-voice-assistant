# 十六款 RGB、QSPI、电子纸和反射屏当前源码重建

本批重建十六个既有 S3 板型入口，补齐当前源码整板构建证据。范围覆盖不同显示总线、界面尺寸、codec、麦克风、NVS 和 Flash 分区配置；不修改固件引擎、注册、manifest、配置、分区或人工决策。

固定上游仍为小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围为 **154 目录／188 变体**；138／171 是 README 的旧统计。仍有 152 个官方变体源码入口、153 个本地 profile（含历史入口）、36 项无入口，4 项已知必需功能缺口保留。

## 逐板配置与保留范围

| profile | 当前界面尺寸／显示路径 | 输入及音频差异 |
| --- | --- | --- |
| waveshare-esp32-s3-touch-lcd-4b | 480×480 RGB，ST7701 板级命令 | GT911；ES7210＋ES8311 |
| waveshare-esp32-s3-touch-lcd-4.3c | 800×480 RGB | GT911；ES7210＋ES8311，CH32 控制 |
| waveshare-esp32-s3-touch-lcd-7c | 800×480 RGB，独立引脚／时序 | GT911；ES7210＋ES8389，mono 到 stereo 扩展 |
| kevin-yuying-313lcd | 376×960 RGB，GC9503V 命令 | 无触摸；单 ES8311，麦克风16k／输出16k |
| espressif-esp-s3-lcd-ev-board-1p4 | 480×480 RGB，SUB2 GC9503CV | FT5x06，1.4 接线；ES7210＋ES8311，16k |
| espressif-esp-s3-lcd-ev-board-1p5 | 480×480 RGB，SUB2 GC9503CV | FT5x06，1.5 接线；ES7210＋ES8311，16k |
| espressif-esp-s3-lcd-ev-board-2 | 800×480 RGB，SUB3 ST7262E43 | GT1151；ES7210＋ES8311，24k |
| lcdwiki-es3c35p | 320×480 ST77922 QSPI | TDDI 原始触摸；单 ES8311 |
| waveshare-esp32-s3-epaper-1.54-v2 | 200×200 电子纸 | 无触摸；单 ES8311，8MB 分区 |
| waveshare-esp32-s3-epaper-3.97 | 800×480 电子纸 | 无触摸；单 ES8311，独立电源引脚 |
| waveshare-esp32-s3-rlcd-4.2 | 400×300 ST7305 反射液晶 | 无触摸，TE 扫描完成路径；ES7210＋ES8311 |
| zectrix-s3-epaper-4.2 | 400×300 电子纸 | 无触摸；单 ES8311，16k 音频 |
| sensecap-watcher | 412×412 SPD2010 QSPI | 当前为旋钮按下输入；ES7243E＋ES8311，独立工厂分区 |
| waveshare-audio-board | 320×172 JD9853 SPI，短屏布局 | BOOT 输入；ES7210＋ES8311，TCA9555 |
| taiji-pi-s3 | 360×360 ST77916 QSPI | CST816S；标准 I²S 数字麦克风 |
| taiji-pi-s3-pdm | 360×360 ST77916 QSPI | CST816S；PDM 麦克风，独立引脚配置 |

尺寸与路径来自当前板级头、CMake 和显示实现，并核对实际编译选择；不能代替真实布局、触摸角点、圆形裁切、颜色、帧同步或修订验收。RGB 双帧／bounce、QSPI 条带与纸屏后台刷新使用各自现有实现，不把 SPI 传输完成当成纸屏已经可见。

初步范围提到 I80，继续读取当前源码后确认 LCDWiki 实际使用 QSPI，本批没有 I80 目标。RLCD 是反射式液晶，不是电子纸；它复用帧任务但保留 ST7305 扫描／TE 协议。SenseCAP 当前未实现触摸和旋钮旋转，不以旋钮按下输入替代这些外设的验收。Audio Board 保留默认 JD9853，不代表可选 ST7789 屏或相机已接入。

十六项都保留自定义“你好，小燕子”、AFE／软件 AEC、媒体、绑定、提醒和双 OTA。物理时钟、麦克风槽、PA／静音／增益、自然插话及资源峰值仍未实测；不宣称硬件 AEC、波束形成、电池、传感器、摄像头、SD 或厂商全部外围已适配。

## 存储差异

SenseCAP 保留 `nvsfactory` 0x9000／0x32000／readonly，工作 NVS 为 0x3B000／0x4000，双 app 从 0x50000 和 0x350000 开始。其余本批入口的工作 NVS 保留 0x9000／0x4000。核对脚本按每板实际 CSV 比较分区名称、偏移、长度和标志，不对全部板套一个 NVS 起点。

1.54 V2 使用独立 8MB 分区，双 app 各 0x2A0000、model 0x240000。7C 与 SenseCAP 声明 32MB Flash，其既有分区仍在 16MB 范围内，不因标称容量扩大分区。其他入口沿原 16MB 配置。保留 NVS 和现有分区不代表物理容量或升级兼容性已经实测。

## 实际执行与产物检查

使用现有 ESP-IDF 5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake 3.30.2、Ninja 1.12.1、ROM 20260528。复用各板原隔离目录，复制当前 main、注册及根／芯片／板配置与分区，再重新配置构建。仅使用已有组件 override 和 offline_guard，未安装或下载。最多两个目标同时构建，每项 `IDF_PY_BUILD_JOBS=2`。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/rgb-paper-rebuild-20261113/build_batch.py
```

逐板实际命令为 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。准确解释器路径、工作目录、时间、同步输入、退出码与日志摘要均在各板 `build.json`。

完成后分次调用 `collect_batch.py` 的 `collect`，最后运行脚本汇总。每项用实际 ELF／镜像／分区执行八条命令：`readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、分区二进制解析、SDK HEAD 和 status 只读查询。全部命令与原输出保存在每板 `commands.json`。随后执行：

```powershell
# python 指上述已安装的 IDF Python。
python -B evidence/rgb-paper-rebuild-20261113/collect_batch.py
python -B evidence/rgb-paper-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/rgb-paper-rebuild-20261113/finalize.py
git diff --check
```

符号期望参考各板已有取证并对照当前驱动复核，记录在 `symbol-expectations.json`。例如 7C 必须链接 ES8389 与 mono 扩展，SUB2／SUB3 的触摸与三线初始化有不同选择，太极派两项区分 PDM 初始化。纸屏／反射屏保留 generation 提交和呈现检查；这些仍是链接证据，不能证明真实呈现、唤醒或交互已通过。

## 最终结果

十六项整板构建及每项八条实际产物核对命令均退出 0；每项保存 330 个当前输入摘要，共归档 656 个产物。

| profile | app 字节 | app 分区剩余 | 模型分区剩余 | 归档产物 |
| --- | ---: | ---: | ---: | ---: |
| waveshare-esp32-s3-touch-lcd-4b | 2,541,168 | 604,560 | 1,990,485 | 42 |
| waveshare-esp32-s3-touch-lcd-4.3c | 2,236,464 | 909,264 | 1,990,485 | 43 |
| waveshare-esp32-s3-touch-lcd-7c | 2,239,312 | 906,416 | 1,990,485 | 43 |
| kevin-yuying-313lcd | 2,245,200 | 900,528 | 1,990,485 | 39 |
| espressif-esp-s3-lcd-ev-board-1p4 | 2,543,296 | 602,432 | 1,990,485 | 41 |
| espressif-esp-s3-lcd-ev-board-1p5 | 2,543,296 | 602,432 | 1,990,485 | 41 |
| espressif-esp-s3-lcd-ev-board-2 | 2,530,464 | 615,264 | 1,990,485 | 41 |
| lcdwiki-es3c35p | 2,249,952 | 895,776 | 1,990,485 | 40 |
| waveshare-esp32-s3-epaper-1.54-v2 | 2,198,448 | 554,064 | 155,477 | 39 |
| waveshare-esp32-s3-epaper-3.97 | 2,740,672 | 405,056 | 1,990,485 | 39 |
| waveshare-esp32-s3-rlcd-4.2 | 2,229,824 | 915,904 | 1,990,485 | 40 |
| zectrix-s3-epaper-4.2 | 2,198,832 | 946,896 | 1,990,485 | 39 |
| sensecap-watcher | 2,544,976 | 600,752 | 1,990,485 | 43 |
| waveshare-audio-board | 2,258,208 | 887,520 | 1,990,485 | 42 |
| taiji-pi-s3 | 2,236,208 | 909,520 | 1,990,485 | 42 |
| taiji-pi-s3-pdm | 2,238,272 | 907,456 | 1,990,485 | 42 |

累计曾通过缓存构建的官方变体仍为 115 个，本批更新十六项当前输入通过。矩阵变为 **48 项当前通过／67 项历史通过但源码已过期／73 项未执行构建**。没有新增十六个源码入口，也没有把构建通过写成实机完成。


模型均为 2,203,819 字节，MN5 配置和自定义唤醒源码保留。逐板检查项目身份、ESP32-S3、Flash 声明、校验摘要、真实分区及 app／model 余量，核对本次全部源文件与隔离目录一致。归档镜像、ELF、map、模型、分区、配置、编译命令及每个实际选中 main 对象，不以相同产物数量作为不同板的通过条件。

SDK 四个既有强制 UND 继续记录：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。警告和 dirty 状态保留；不称干净发行、零 UND、启动或物理验收通过。最终审计也检查旧 main、注册、manifest、配置、工具和历史构建账本未被覆盖，研究只追加后续证据、不删除既有阻塞。

188 项实机均仍 `not_run`，只有一块具体修订待确认的微雪 1.8。本批没有运行独立测试套件／类型检查、启动服务、安装依赖、Git 写操作、刷机、OTA、上传或发布。新 SDK／缺失依赖、内部内存方案、蜂窝会话、4 项已知功能缺口和全部实机验收继续保留。

## 证据入口

- [最终审计](../evidence/rgb-paper-rebuild-20261113/final-audit.json)
- [构建进程结果](../evidence/rgb-paper-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/rgb-paper-rebuild-20261113/collection-summary.json)
- [本批板型和隔离目录](../evidence/rgb-paper-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/rgb-paper-rebuild-20261113/symbol-expectations.json)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
