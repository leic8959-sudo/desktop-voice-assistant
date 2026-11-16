# Sensair ADC就绪检查及文件级编译

本批修正Sensair采集就绪的判断。当前codec_dev1.5.11的open会忽略data_if的set_fmt／enable返回值，之前仅检查ADC对象is_open，会将“对象创建完成”和“实际完成配置并开始采集”混为一谈。

ADC owner新增独立configured／enabled状态，只在对应任务操作成功后发布；enable在尚未配置时拒绝。格式／启用参数错误或入口无法执行时停止复用owner，确保codec_dev即便忽略返回值，后续就绪检查仍失败。关闭及失败后就绪也为false。PDM层的sensair_audio_status改为调用此就绪查询，与输出格式、启用及错误状态一起决定是否允许通用音频初始化继续。

修改范围只有sensair_adc_owner.h/.cpp和sensair_audio.cpp，沿用既有单任务ADC生命周期、固定请求槽、内部PCM缓冲及超时锁存，不改控制协议人工决定、固定唤醒词、显示行为或板型注册。就绪检查仍是软件状态，不能证明真实ADC信号、采样连续性或功放发声。

本批实际执行四次成功的文件级编译：sensair_adc_owner.cpp、sensair_audio.cpp、sensair_board.cpp及Sensair选择器下的audio_io.cpp。使用已安装riscv32-esp-elf-g++、SDK5.5.6和既有Spot生成的SDK配置，替换为Sensair板级宏并显式开启CODEC_DATA_ADC_SUPPORT。显示文件额外引用已归档、按固定提交核对过的ILI9341 2.0.2真实公开头文件，没有创建桩接口，也没有安装组件。

首次独立编译暴露了编译环境设置问题：命中了旧暂存目录的owner头文件，且缺少显式esp_adc公共include。保留该次失败日志；随后把本批头文件与源码一起快照，补上等价于Sensair REQUIRES esp_adc的include，两份音频文件编译退出0，再编译显示与通用音频均退出0。具体编译器参数、工作目录、源码及目标文件SHA256记录在`evidence/sensair-audio-readiness-20261112/object-compilation.json`。四份对象均经readelf检查为RISC-V可重定位目标，检查退出0。

**这不是Sensair整板配置或链接通过。** 没有执行Sensair的完整CMake配置、解析ILI9341组件及传递依赖、编译其驱动实现、生成板级镜像或进行运行测试；仍缺已安装的ILI9341 2.0.2。文件级编译只能证明这组真实头文件和配置下的源码兼容性，不能证明Sensair最终sdkconfig完全相同。

没有执行行为／超时／错误注入测试、模型推理或实机操作。ADC清理等待、SDK stop／deinit、PDM波形与首次解除静音、内存峰值、实时性和声学效果仍待完整目标与实物验收。

本批执行矩阵同步、源码／产物摘要审计和git diff --check。当前152／188官方源码入口，153本地profile，36项待源码；Sensair整板build仍not_run，188项本轮实机均not_run。全源码摘要账本将Spot上批缓存记录随这三份源码更新标为stale，构建状态为0 passed_cached_sources／65 stale_cached_sources／6 failed_cached_sources／117 not_run；上批Spot编译成功的历史产物保持原文，不冒称本批重跑。

未安装依赖／SDK、启动服务、执行独立测试套件／类型检查、写Git、刷机、OTA、上传或发布。全量适配目标继续进行。
