# ML307认证TLS字节流候选（2026-11-12）

本批在原始同步流上增加 `Ml307TlsStream`，使用现有mbedTLS自定义BIO接ML307原始TCP，复用ESP证书包校验服务器证书链和原主机名。**仅为evidence源码候选，尚未应用、安装、编译、链接、联网或执行测试。** 不能据此宣称HTTPS／WSS或八款蜂窝板已经适配。

完整补丁位于 `evidence/ml307-tls-source-20261112/patches/ml307_tls_stream.patch`，相对 `78/esp-ml307 3.7.5` 提交 `506477b5af639f5dce9386e423e939091fe7fa11`，包含九份既有文件和四份新增原始流／TLS文件，共十三文件。UART-UHCI仍精确0.4.0。新源码显式依赖mbedTLS，没有另写加密算法。

## 身份验证和配置

候选仅在证书包及MBEDTLS_HAVE_TIME_DATE启用时允许Create，缺配置直接拒绝，不切换到无认证模式。Connect建立客户端TLS流配置，使用ESP熵与CTR-DRBG；按本机SDK做法，在TLS1.3配置启用时调用PSA初始化。最低TLS1.2，其他协议能力沿实际SDK配置，未作协议握手验收。

设置MBEDTLS_SSL_VERIFY_REQUIRED，检查esp_crt_bundle_attach返回值，在SSL setup后以原始请求主机名调用mbedtls_ssl_set_hostname。握手成功后再核对verify flags，均通过才发布Open。原始TCP的ssl=0是这层的下层，不作为产品明文HTTP／WS入口。没有skip-common-name、auth0、可忽略证书错误或按IP替换主机名的降级分支。

真正owner须先完成模块身份／硬件重启／SIM／APN／PDP和串口准备，并提供可信墙上时钟、正确证书包及唯一ID租约。代码不自行设时间或下载信任锚；错误时间／证书应使验证失败。私有CA、客户端证书、ALPN、会话复用和TLS1.3后握手事件尚无产品接入验收，未新增相关配置。

本机证书包detach会清共享全局bundle状态，因此单连接析构只释放本地SSL／config／DRBG／entropy，不detach全局证书包、不全局释放PSA。该选择对应现有SDK源码，不能替代多连接并发和内存生命周期验证。

## 收发和错误边界

BIO调用原始流Read／Write，透传正字节数及短读／短写。Busy只在未进入新SSL操作或未开始底层AT事务时使用；NetworkErrc末尾新增Busy并补齐名称、消息及AtError映射，旧枚举顺序保留。普通空缓存Timeout且原始流没有终止标志，才转成WANT_READ；底层已失败的超时／断线保留原始NetworkError，不能转成可继续等待。

握手、读、写和关闭各有包装入口预算，BIO只取其剩余毫秒。WANT_READ／WANT_WRITE循环按一个FreeRTOS tick退让；毫秒向上取整、调度、熵／证书CPU工作和日志不构成严格实时总时限保证。

TLS写单次最多4096明文字节，保留原指针和本次长度，遇WANT_READ／WRITE在同一调用内使用相同参数续写。超过本次预算就停止复用TLS上下文，不能让下次调用用新数据接着未完成的记录。正返回只表示mbedTLS消费了这部分明文并完成其底层发送处理，不是服务器或业务回执。调用方仍须处理短写，错误后不得盲目重发。

TLS读单次交付最多4096明文字节。只有明确的MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY进入PeerClosed并返回0；mbedtls_ssl_read返回裸0或底层TCP断开是错误，不制造干净EOF。普通空缓存到期返回Timeout并允许以后再读；协议／传输致命错误停止上下文。

SSL上下文以一把try-lock串行访问；锁忙时返回Busy，未进入新的SSL操作。一个长读等待时，其他发送可能暂时Busy；未来HTTP／WebSocket transport仍需适配调度、公平性和超时映射，不能把所有NetworkUnavailable／Timeout都当成可安全重发。此类并发行为本轮没有运行验证。

## 关闭和待完成项

Close在健康上下文先尝试发close_notify，再停止raw流，并在剩余预算内确认模组关闭。TLS通知失败仍尝试底层清理；底层关闭成功也保留通知错误，不把两步合并成“都成功”。若预算已经耗尽或其他调用仍运行，返回错误／Busy，owner须等调用结束后重试清理。收到对端close_notify不等于已经发出我方通知，也不等于对端应用处理了全部数据。

RequestStop只做原子停止与raw事件唤醒，不能中断已在AT内的事务或正在执行的密码运算。析构要求所有调用已结束、位于UART任务和回调之外；析构不隐式AT关闭。硬件重启、ID分配、join、重复重连及完整资源所有权仍须真正模组owner完成。

原始流仍不能证明对端断线后的模组缓存可排空；本地已完成的缓存可交付，未读取的模组尾部可能导致TLS或HTTP Connection:close失败。TLS层没有把这种失败改成成功。仍未实现esp_transport句柄、HTTP绑定／OTA、WebSocket帧／媒体客户端或蜂窝板profile，也没有完成吞吐、RAM峰值、物理模组、真实服务器与证书失败场景验收。4096只限单次应用明文和原始流数组，不是TLS总内存上限。

## 牛牛本轮确认的唤醒词方向

[人工决策-2026-11-12 10:07:55] C3／C5／C6接受库支持的固定唤醒词，逐板标明差异。独立记录覆盖10项C3、4项C5、10项C6，共24变体；具体词和模型待核实，未据此新增profile或宣称识别成功。详见[c3_c5_c6_fixed_wake_decision_20261112.md](c3_c5_c6_fixed_wake_decision_20261112.md)。

## 本批实际证据

读取并归档本机SDK5.5.6的mbedTLS3.6.7、证书包、ESP TLS配置和PSA参考，复用原始ZIP与UHCI来源。当前本机文件不代表所有目标SDK的同等实现或干净发行构建。本批未下载、安装或运行这些库。

```text
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-tls-source-20261112/close_research.py
git diff --check
```

close_research.py仅核对九份基线与ZIP、四份新增文件的补丁关系、来源摘要和输入快照，静态核对认证配置／原主机名、同参数续写、WANT与失败区分及close_notify／裸EOF路径；不是编译、协议、证书、并发、时间或内存测试。另核对24项人工决定清单与固定发布矩阵一致。

八项ML307研究追加本批字段，矩阵只同步这些研究；固件240个main代码文件及native registry摘要未变。仍148／188官方源码入口、149本地profile、40待源码；此前构建账本2 passed_cached_sources／62 stale_cached_sources／6 failed_cached_sources／118 not_run，本轮未重跑；188项本轮实机全not_run。未写Git、刷机、OTA、上传、发布或启动服务／Docker，全量目标继续进行。
