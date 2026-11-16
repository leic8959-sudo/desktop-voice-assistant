# SDK 强制 UND 与实际启动入口核对

本批只读分析已有两份镜像：LCD EV Board 2（ESP32-S3）和 Moji2（ESP32-C5）。结论是：**所检查的四个／三个 UND 名称没有被显式链接输入中的对象引用，它们是 CMake 强制保留的符号；实际启动入口和到 app_main 的代码存在。** 因此不能再仅凭这些名称出现在 `nm -u` 中，就判断这两份镜像缺少启动代码。

这项结论只覆盖下列具体镜像和静态链接问题，不证明设备可以上电启动，也不改变其它板型的构建、源码或实机状态。现有 SDK 仍为5.5.6-dirty，不据此确认干净发行 SDK、硬件初始化、内存预算、音频／网络／OTA或声学验收。

| 检查对象 | LCD EV Board 2 | Moji2 C5 |
| --- | --- | --- |
| 既有构建记录 | cached_build_attempts_20261112_gt1151_board2.json | cached_build_attempts_20261112_moji2_qspi_native.json |
| 项目名 | assistant_lcdev2 | assistant_moji2c5 |
| ELF及镜像入口 | 0x403796b8，call_start_cpu0 | 0x408002b0，call_start_cpu0 |
| 实际启动函数表 | CPU0=0x4200499c，CPU1=0x403798a4 | CPU0=0x420012ae |
| 显式链接归档／对象文件数（去重） | 138 | 138 |
| 对下列强制 UND 的对象引用 | 全部为0 | 全部为0 |
| 实机／运行验证 | 未执行 | 未执行 |

分析使用既有产物归档中的ELF／镜像。先核对原构建记录中全部产物摘要，并要求当前stage的ELF、镜像、map、build.ninja和sdkconfig与归档逐字相同。输入列表来自对应build.ninja的显式对象、LINK_LIBRARIES及LINK_PATH；`-l`优先按实际-L目录查找，否则用对应架构编译器的 `-print-file-name` 查询工具链路径。显式库及工具链静态库均记录路径／SHA256，不执行生成的shell命令。

没有仅以最终ELF“无重定位”作为结论：最终可执行文件通常已完成重定位。另用对应架构的 `nm -A -u` 检查每个显式静态库的所有对象成员及直接对象，没有发现这些UND的未定义引用。这里覆盖的对象集合甚至包括库中未被抽取的成员；它仍不是全部可能运行路径、动态行为或ROM函数的形式化证明。

| UND 名称 | 来源和实际核对结果 |
| --- | --- |
| start_app | esp_system CMake仍指定 `-u start_app`；当前startup.c实际调用 `esp_startup_start_app`，它在两份ELF中都有已定义代码。旧名称在全部显式输入对象中无引用 |
| start_app_other_cores | 双核S3 CMake仍指定旧名称；实际函数为 `esp_startup_start_app_other_cores`，S3 ELF中为强定义T。单核C5不强制保留此旧名称 |
| __cxx_fatal_exception | CXX异常关闭时CMake指定该 `-u` 名称，但当前stub源没有同名实现。两份最终ELF的 `__wrap___cxa_throw` 有实体代码并调用abort，实际链接参数保留对应wrap；没有用缺失的这个旧名称处理throw |
| esp_system_include_coredump_init | CMake无条件指定 `-u`，源定义受 `CONFIG_ESP_COREDUMP_ENABLE` 保护。两份实际配置均关闭coredump，输入对象没有对该符号的引用；此结论不能推广到开启coredump的固件 |

实际入口检查同时比对ELF entry、ESP镜像头entry和 `call_start_cpu0` 地址，并确认入口落在ELF可执行PT_LOAD范围内。通过ELF符号和只读段内容解析真实 `g_startup_fn` 指针数组，核对CPU0／CPU1公开启动函数地址；反汇编确认CPU0启动调用实际 `esp_startup_start_app`、`main_task` 调用 `app_main`，以及CXX throw wrapper调用abort。SDK源码另外记录创建main任务与启动调度器的路径。

反汇编首次按 `start_cpu0_default` 名称查询未输出函数体，审阅脚本因此停止；ELF中它与公开弱别名 `start_cpu0` 地址相同。初次脚本与输出保存在initial-alias-review，之后按公开别名反汇编并核对真实启动函数表，不修改SDK／固件，也没有把这个脚本错误记成固件失败。

完整证据在 `evidence/sdk-und-audit-20261113/`：每个架构的report.json保存真实命令、退出码、输入摘要、符号／重定位日志和各段反汇编，较大的符号及库对象引用日志用gzip归档。link-audit.json汇总两份镜像；可追踪摘要另放在 `firmware/assistant_espidf/sdk_link_audit_20261113.json`。SDK来源快照涵盖启动、FreeRTOS、CXX、coredump及S3入口链接脚本。

实际执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/sdk-und-audit-20261113/audit_links.py
python -B evidence/sdk-und-audit-20261113/finalize.py
git diff --check
```

两份镜像的readelf、nm、objdump和工具链路径查询最终全部退出0；没有构建、链接新镜像、执行固件、独立测试套件、类型检查或启动服务。main、registry、research、matrix及所有原构建记录摘要保持；构建计数仍2当前缓存通过／98历史待重建／88未构建，100个官方变体曾缓存构建通过，全部188项本轮实机未执行。36项无源码和4项明确必需功能缺口仍未解决。

未安装依赖、修改SDK、执行Git写操作、刷机、OTA、上传或发布。后续其它目标若使用不同SDK、配置或链接输入，需要单独核对，不能自动继承本结论；本批也没有消除旧CMake与当前源码命名不一致的问题。
