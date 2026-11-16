# ML307候选的S3实际编译与中断信号修正

本批首次将此前ML307读写／连接／TLS候选及配套UHCI源码放到现有Xtensa ESP32-S3工具链下编译。最终六份源码文件均退出0，生成Xtensa可重定位目标。**这不是蜂窝整板配置／链接通过，也没有模组、网络或TLS握手实测。** 没有应用到固件main或安装依赖，源码支持计数仍152／188、36项待源码。

## 实际发现与修正

第一次编译，at_uart.cc因`std::atomic<bool>::is_always_lock_free`断言失败。实际SDK配置启用了`CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND`，GCC对bool及int的原子能力宏均为1（不保证始终无锁）。这不能直接推断所有std::atomic都不安全，但此前“始终无锁”的前提不成立。

候选新增UART专用的小标志类，用SDK既有portMUX临界区在任务／正常FreeRTOS兼容中断间同步。每段临界区只读取／写入一个bool，退出后才调用回调、等待或日志；按SDK做法以SAFE原语区分任务和中断，不用普通mutex。内联实现避免ISR调用未确认的外部C++原子函数。该实现使用临界区，**不宣称无锁或任意中断优先级／cache关闭场景都已验证**。

AtUart的raw enabled／poison／loss／cancel及UHCI的running／overflow都改用这类标志，UHCI发送取消参数同步改为该类型。AtUart和UHCI初始化检查对象在内部RAM，发送也检查取消标志地址；未来owner必须按这些前提分配，不能直接将PSRAM中的对象注册为ISR状态。初始化返回或GPIO波形仍未实测。

原始3.7.5和UART-UHCI0.4.0完整补丁分别记录于：

- `evidence/ml307-compile-source-20261112/patches/ml307_stream_s3_compile.patch`：13文件，包含之前收发／连接／TLS候选及本批修正。
- `evidence/ml307-compile-source-20261112/patches/uart_uhci_isr_flag.patch`：4文件，包括新标志头文件、接口／实现及显式esp_hw_support依赖。

**必须将这两份依赖源码候选一起构建。** 发送取消参数ABI已改变，不能将新AtUart对象与未修改的registry UHCI0.4.0二进制／源码混用。现有版本pin只指固定来源，不表示官方0.4.0已经包含本补丁。没有修改历史补丁或已安装组件。

## 编译覆盖与TLS配置缺口

实际编译文件为：at_uart.cc、ml307_raw_stream.cc、ml307_tls_stream.cc、network_error.cc、ml307_tcp.cc、uart_uhci.cc。使用已安装xtensa-esp32s3-elf-g++、SDK5.5.6及已有S3生成配置，按候选要求使用C++23；不创建桩接口。初次驱动编译还缺esp_mm公共include，补齐与UHCI REQUIRES相符的真实路径后通过。两处多余return move已清理，最终编译日志无这些警告。

预处理检查进一步发现：现有S3配置虽然启用证书包，却没有开启证书日期校验。因此最初TLS文件编译通过只覆盖Create拒绝配置的分支。候选本来会在这种配置下返回不支持，没有静默绕过日期校验。

本批为TLS文件显式增加`CONFIG_MBEDTLS_HAVE_TIME_DATE=1`重新编译，并核对宏中存在`MBEDTLS_HAVE_TIME_DATE`、证书包和S3目标；实际认证握手成功路径也编译退出0。这个命令行设置**没有重新配置或构建完整mbedTLS库**，不能据此声称日期校验已经在运行库生效。未来蜂窝profile必须开启对应Kconfig、重建库并提供可信时钟，再验收证书链／日期／原主机名及错误场景。

六份对象均经readelf检查为Xtensa REL，检查退出0。nm核对AT UART及UHCI对象没有未解析的`__atomic_*`函数引用；任务层原始流／旧TCP对象仍可引用标准原子辅助函数，不将它们当ISR无锁证明。另实际执行ld -r合并这六份对象，退出0，核对UART Transmit取消接口、AtUart缓存读取及原始流读取的内部引用已解析；SDK／C++／mbedTLS符号仍待完整链接。此为可重定位对象间ABI检查，没有生成可执行固件。符号、编译及来源摘要均归档，未做完整组件符号解析或链接。

编译命令数组和工作目录在`object-compilation.json`，每份源码及目标文件SHA256、失败记录和最终日志可追溯。SDK真实配置头、响应参数文件和相关newlib／FreeRTOS实现已归档，总计35项来源。额外验证命令包括xtensa readelf、nm和TLS源码预处理，退出记录见`object-checks.json`及编译记录。

## 待完成和当前状态

仍缺真正模组owner、硬件重启、SIM／APN／PDP、唯一连接ID和重连资源、对端断线后模组尾部排空、跨线程读写调度、esp_transport／HTTP绑定OTA／WSS媒体接入及整板编译。需要验证UART吞吐、ISR／DMA丢段与取消、内部RAM分配、TLS真实服务器及证书失败、堆和任务时序。此次编译没有替代这些验收。

八个ML307变体只追加本批研究，仍未登记蜂窝profile。固件main、native registry及全部现有profile保持本批前原文；152官方源码入口＋1历史入口＝153本地profile，36项待源码。当前构建账本仍0 passed_cached_sources／65 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。

实际执行矩阵同步、`python -B evidence/ml307-compile-source-20261112/close_research.py`和`git diff --check`。审计只核对来源、差异、编译及对象证据，不执行C++、协议或错误注入测试。未安装、启动服务／Docker、写Git、刷机、OTA、上传或发布；全量适配继续进行。
