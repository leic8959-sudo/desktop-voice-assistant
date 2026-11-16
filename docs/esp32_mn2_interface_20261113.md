# ESP32七变体的MN2初始化与接口核对

本批新增的是一个具体的模型初始化阻碍：本机ESP-SR2.4.7的ESP32 MN2库有命令注册接口，但不能直接沿用现有S3的`create("mn2_cn", duration)`写法。实际初始化对象的系数路径为`COEFF_NULL`，没有引用另一个归档库中已存在的`get_coeff_multinet2_ch`。这需要先取得并核实可用的旧版构造／系数绑定实现，再写正式ESP32唤醒入口。未运行模型，未观察到实机崩溃，也未声称当前口令已被注册或识别。

七项分别是bread-compact-esp32、bread-compact-esp32-128x32、bread-compact-esp32-lcd、m5stack-atommatrix-echo-base、waveshare-esp32-touch-lcd-3.5、wdmomo-esp32-cgc、wdmomo-esp32-cgc-144。仅为它们的研究字段追加本批证据，没有添加native profile或修改固件main。

## 官方来源与本机文件

通过只读GitHub tree API取得ESP-SR固定提交`2f8c4b0459db5bbb39abd77adae27962d6d94bcb`的非截断树；仅取得Git元数据，未下载或安装组件。

两份库和三份API文件均按Git blob算法重新计算并与固定上游树一致：

| 文件 | 字节 | Git blob SHA1 |
| --- | ---: | --- |
| lib/esp32/libmultinet.a | 799332 | `657054c120b74164871cdd275dfc147120a7a547` |
| lib/esp32/libmultinet2_ch.a | 1315932 | `70664fca4b2cecff7fe98c137e3499071ac3140c` |
| include/esp32/esp_mn_iface.h | 8066 | `61b9f40fbe2adc25126a6e78429efc13e245936e` |
| include/esp32/esp_mn_models.h | 1575 | `8571648f304c211d4158934d80f0d1e81215b8fd` |
| src/esp_mn_speech_commands.c | 13780 | `0c6c13751e3b8c1251b6d10136d368efe336724b` |

因此不能把本次异常解释为上述本机文件损坏。本结论也不覆盖未读取的其它版本或厂商私有库。

## 真实对象、接口与初始化

实际读取`libmultinet.a`内的`multinet2_single_quantized_cn.c.obj`，用既有Xtensa nm／objdump和已安装pyelftools分析；没有执行目标代码。

`esp_sr_multinet2_single_quantized_cn`是60字节、15个32位字段的接口表，与本机ESP32头文件顺序一致：

| 字段／位置 | 实际重定位／内容 | 含义 |
| --- | --- | --- |
| create，0x00 | `.text.model_init` | 需核对这个真实构造函数，不能由公开函数签名推断行为 |
| set_speech_commands，0x2c | `.text.set_speech_commands_by_link` | 有命令链注册实现；仍未运行“小燕子”口令 |
| switch_loader_mode，0x30 | 原值0，无重定位 | MN2不能调用新版的加载模式切换接口 |
| print_active_speech_commands，0x34 | `.text.model_print_active_speech_commands` | 有接口，不等于口令已成功注册 |
| check_speech_command，0x38 | `.text.model_check_speech_command` | 调用拼音转换；本批未运行 |

`model_init`的反汇编与字面量重定位联合表明：

1. `.literal.model_init+0x04`的实际字符串是`NULL`；初始化用首参和该字符串比较。
2. `.literal.model_init+0x00`指向`COEFF_NULL`，而非`get_coeff_multinet2_ch`结构。
3. 对普通模型名`mn2_cn`，`strcmp`非零路径在`model_init+0x53`将系数指针选择为零；`+0x96`保存该值，后续`+0xbd`读取其偏移4的getter并间接调用。这里是静态路径分析；尚未在实机调用，不把推导写成已复现的崩溃。
4. MN2对象的全部重定位没有`get_coeff_multinet2_ch`；整个`libmultinet.a`的未定义引用也没有该getter。另一个库确实导出它，但“两个库都在磁盘／链接命令里”不能补出缺失的引用。

本机头文件中关于`create(&MULTINET_COEFF)`的旧示例与新版`create(const char*, int)`声明并存。这不能成为强制转换模型系数指针、二进制改库或绕过校验的依据。应用Kconfig变化也不会重编已经发布的预编译对象。

命令注册函数确实调用拼音转换、`phrase_init`、`commands_add_one_phrase_v3`和语言图更新，因此不能说MN2没有自定义命令接口。相应分配、词表接受范围和实际“ni hao xiao yan zi”识别仍未知。官方对象还包含“MultiNet may cause WatchDog on ESP32 sometimes!”字符串；没有据此关闭看门狗或推定推理耗时。

## 本批实际命令、记录与纠错

使用既有IDF Python运行以下源码／元数据读取脚本：

```text
python -B evidence/esp32-mn2-interface-20261113/inspect_library.py
python -B evidence/esp32-mn2-interface-20261113/inspect_followup.py
python -B evidence/esp32-mn2-interface-20261113/verify_upstream.py
python -B evidence/esp32-mn2-interface-20261113/inspect_object.py
python -B evidence/esp32-mn2-interface-20261113/correct_literal_addends.py
python -B evidence/esp32-mn2-interface-20261113/record_research.py
```

内部真实工具命令、输出及摘要在`library-inspection.json`、`followup-commands.json`和`object-review.json`；官方blob比较在`upstream-blob-comparison.json`。没有运行测试、类型检查、固件构建或模型推理。

三处读取工具问题均保留记录：首次猜测的`.data.rel.ro.*`节不存在，objdump退出1；正确节为实际节表中的`.rodata.*`，后续读取退出0。首次反汇编用的`multinet2_create`符号不存在，退出0但没有函数体，后续改读实际`model_init`。首次Windows `ar p`经stdout取得的对象被增加CR，pyelftools失败；改用`ar x`直接写入新目录，原错误对象逐字节等于正确对象在每个LF前加CR，确认是提取问题而非库损坏。错误对象和脚本原文保留。

第一次ELF摘要还漏算Xtensa R_XTENSA_32的原位加数，把第二个字符串误记为`COEFF_NULL`。已保留旧报告并修正读取：显式加数0＋原位加数12，真实字符串为`NULL`，与objdump原文一致。没有因此改动二进制、SDK或依赖。

## 矩阵与后续要求

记录脚本和离线库存同步命令均退出0。最终核对1314份固件、注册、配置、manifest及构建记录摘要未变；188行变体只允许上述七行新增研究字段，其它状态逐项相同。依然是152官方源码入口、153本地profile、36项无入口，**114当前缓存通过／1历史过期／73未构建，188项实机not_run**。

下一步需找到能正确绑定MN2系数的受支持实现并核实其许可／SDK兼容性，再做自定义词、音频、AFE／AEC、RAM、网络和双OTA的完整适配。ESP32的Flash／PSRAM与硬件修订仍须逐板确认。不能套用C3／C5／C6已接受的固定词决策，也不能改为纯按键、删算法或单OTA来计数。

本批没有安装、替换库、写Git、刷机、OTA、上传或发布。37项待环境的独立工程仍在[前批清单](pending_firmware_sources_20261113.md)，本次调查不改变它们的源码输入。

## 后续：核对当前官方2.6.2源码

继续读取官方master的非截断Git树，并固定为`bb552f1a559396a17681299cb3e199a0f1a4520a`。该提交manifest版本为2.6.2；两份ESP32 MN2库和两个模型接口头文件的Git blob仍与2.4.7逐项相同。另读取并核对该提交的manifest、CMake及两个接口头文件；CMake对原版ESP32继续链接同一`lib/esp32/libmultinet.a`与`libmultinet2_ch.a`，声明的srcs没有替换MN2构造函数的源文件。

因此，直接把全项目依赖版本升级到2.6.2，不能为本批查到的构造路径提供不同实现。2.6.2还引入`esp-dl=3.3.13`等声明；没有据此升级或安装它，也没有把新依赖作为已经确认有效的修复方案。这只覆盖本次固定提交中检查的路径，不推断所有分支、registry修订或外部实现均无解决办法。

只读历史元数据显示，MN2系数库路径最近一次变更提交为2021年的`048995531b98d354ea796016ba5ec2683013c5ba`；最近十条模型库历史也已留档，未仅凭提交标题判定修复效果。证据见`upstream-history-review.json`、`latest-reference/comparison.json`及`latest-followup-audit.json`。

实际执行`inspect_upstream_history.py`、`review_latest_source.py`和`record_latest_followup.py`。首次raw源码读取遇到WinError10054；保留已取得的固定tree，改读Git blob API后成功，四份源码均核对blob。未下载新二进制库、安装、编译或执行模型。离线矩阵同步退出0，仍只为七款追加研究信息；1314份保护文件及全部构建／实机状态不变。
