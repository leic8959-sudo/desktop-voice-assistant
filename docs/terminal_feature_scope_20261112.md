# 全板型必需功能缺口与批次范围纠正

当前152个官方变体有本地源码入口，其中**4个入口明确关闭了完整终端目标中的必需功能**；它们是实验候选，不能因为登记了profile或以后编译通过就算完整适配。其余148个入口只表示本次检查未发现这两类选择器带来的显式关闭，不表示全部功能、启动或实机已通过。另36个变体尚无源码入口，全部188个变体仍保留在目标范围内。

本轮原计划继续GT911接入，但读取Guition实际调用链时发现上述范围问题；先修正可见状态和批次执行边界，没有为满足数量继续构建功能缺失的方案。

| 已有源码入口 | 源码中明确缺失的必需功能 | 当前直接依据 |
| --- | --- | --- |
| guition-jc4827w543 | 本地唤醒、完整唤醒片段、自然插话、媒体语音唤醒、双应用OTA | CMake选择manual_wake_word／single_app_update；4MB分区仅factory、无model／otadata／备用app；控制协议采用手动触发 |
| guition-jc4827w543-ext-amp | 同上 | 外接功放只改变音频引脚，未增加模型或第二应用分区 |
| m5stack-atoms3-echo-base | 本地唤醒、完整唤醒片段、自然插话、媒体语音唤醒、AFE、AEC | INTERNAL_RAM选择manual_wake_word，board_profile.manual_audio_only=true；audio_io手动分支不创建AFE／AEC；控制协议关闭相关能力 |
| m5stack-cardputer-adv | 同上 | 同一INTERNAL_RAM手动实现；它是另一独立板型，不因已有显示／键盘源码而具备完整语音能力 |

Guition的AEC路径与无PSRAM手动路径不同，本次未把它标记成缺AFE／AEC；它真正缺的是本地唤醒及对应会话能力和双应用OTA。两类缺口按现有代码分别记录，不互相套用。硬件容量等仍需实物核实；不能据上游自身的功能取舍替代牛牛本轮的完整功能目标，也不能将4MB改填为更大容量来过门槛。

## 实际修改

`tools/board_profiles.py`增加共享的 `required_terminal_feature_gaps`，只根据当前已审阅的GUITION／INTERNAL_RAM编译选择器标出显式关闭的功能；没有修改registry身份、引脚、固件源码、分区或配置。

`tools/sync_board_matrix.py`每次刷新为188项重新生成 `source_feature_scope` 和 `required_terminal_feature_gaps`，不从旧矩阵继承这两个派生字段，避免旧值覆盖新规则。顶层增加4／148／36计数以及九项实际源码／分区／规则文件摘要，记录该分类适用的输入。无源码项明确为no_source_entry，不以空缺口列表解释成支持。

`tools/build_firmware_batch.ps1`预览逐板显示功能范围和缺口；`-Execute`在构建循环前检查registry及审阅输入摘要是否与矩阵一致。存在缺口或缺少当前审计字段时拒绝完整终端批次。`-SourceProfilesOnly`仅解决没有源码的项，不覆盖功能缺口，也不静默跳过这四项。没有增加“忽略缺口”选项，没有触发安装或Docker。

实验源码保留，直接单板工程入口未删除；若日后开展其完整功能实现，应继续修复原功能缺口，再按真实源码更新分类。此门槛不是批准削减功能，也不将这四项从188范围排除。至少36项无源码加4项明确缺功能，需要继续推进，不能称只剩36项即可完成整个目标；其它148项也仍有依赖、硬件和运行验收任务。

## 实际执行与边界

```text
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
./tools/build_firmware_batch.ps1 -Batch all
```

矩阵刷新退出0，仍154目录／188变体。默认批次只输出计划：188登记、152源码、36无源码、4项明确缺功能，逐板输出已保存到 `evidence/terminal-feature-scope-20261112/batch-plan.log`。**未传-Execute，没有执行拒绝分支的运行验证，也没有启动任何构建／Docker。** 这次预览只是工具输出证据，不是功能测试或硬件验收。

随后来源归档和矩阵摘要核对命令见本批 `record_scope.py` 和 `final-audit.json`。记录逐项比较所有main、registry以及此前三份当前成功记录的源码摘要，确认本批没有固件改动、没有将旧构建状态更新成新通过。没有新增测试套件、运行类型检查或服务。

目前构建账本仍3 passed_cached_sources／77 stale_cached_sources／3 failed_cached_sources／105 not_run，累计80个官方变体曾缓存构建成功；这些数字独立于功能缺口，不能相互替代。188项本轮实机仍全部not_run；只有微雪1.8实物。未安装依赖、Git写入、刷机、OTA、上传或发布。

GT911后续应优先推进仍保留完整终端目标的板型。Guition即使补齐GT911并编译通过，本次列明的完整语音／双OTA缺口仍然存在；无PSRAM方案的Opus有损压缩取舍仍待回复，不能静默采用或改成纯按键完成适配。
