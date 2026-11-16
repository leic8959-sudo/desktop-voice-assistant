# ML307认证传输适配候选与WebSocket关闭修正

本批在已有原始TCP／认证TLS候选上增加`esp_transport_ml307_tls_init`，使其可作为真实ESP-IDF HTTP客户端的custom transport、SDK WebSocket transport的TLS父层。真实S3隔离构建最终退出0，生成1016352字节镜像，HTTP和WebSocket客户端API及修补后的SDK transport一起编译／链接。**空app_main没有执行客户端，模组owner尚未实现；这不是8个ML307板型适配完成、联网或TLS握手验收。**

候选和证据在`evidence/ml307-poll-source-20261112/`。固件main、当前263项构建输入及native registry没有因此改变；当前Spot C5缓存通过仍有效。已安装SDK／依赖未修改。

## 就绪查询与传输语义

`Ml307RawStream::WaitReadable`接收0..60000ms，不发AT、不读走PCM／密文，也不消费事件。它检查本地缓存、缓存通知以及250ms周期探测到期；后者只是提示后续Read可主动查询，避免通知合并或遗漏造成永久停读。Read清除旧提示后才发实际AT查询，成功查询后更新下次探测时刻。通知不是已交付数据，也不保证下一次Read立即得到字节。

TLS层先在SSL锁内检查已解密字节及PeerClosed，然后释放SSL锁再等待raw通知，使空闲poll不会一直占SSL锁挡住上行。`CanWrite`只做立即状态／锁快照，后续Write仍可能返回Busy。真实Read和Write仍由SSL互斥串行；本批没有证明同时录放音时的公平性、延迟或吞吐，周期探测后的空读仍可能在调用者有限时限内等待。

适配层实现同步connect／read／write／poll／close／destroy：

- connect／read／write要求1..60000ms有限时限，poll接受0..60000ms；不支持异步connect或无限等待。
- 仅健康等待超时／尚未开始事务的Busy转为读取0字节等待；TLS close_notify才映射SDK的干净FIN哨兵-1。裸TCP断线、TLS截断和其他终止错误返回-2或-3，避免HTTP把普通错误当作正常EOF。
- 单次write最多65536字节，在同一入口截止时刻内循环消费TLS的短写。只有全部完成才返回正长度；部分发送后错误／到期会停止该流，不能将同一整包从头重发。只有Busy允许内部退让重试。成功仍不代表服务器或业务执行回执。
- 连接生命周期共享锁保护流指针，读写／poll共存，close取得独占锁后才处理流。正常close先有限发送TLS关闭通知；仍有调用者时请求停止并报Busy，等待调用者退出后再关。destroy要求客户端任务和所有sender都已join，先销毁WS child、再销毁TLS parent；库不会替未来owner完成这些外部生命周期。

新增`Ml307TransportOwner`只定义未来板级owner必须提供的三个动作：判断可信时钟就绪、获取独占TLS流和消费退役流。owner必须负责真实模组初始化、内部RAM UART、SIM／APN／PDP、唯一ID租约及重连／复位。Retire必须接收失败／未清理完的流并保留到安全关闭或模组复位；不能在同一raw会话复用其ID，不能在UART回调析构。这些约定本批尚无具体owner实现，不把抽象接口当成资源闭环。

## SDK WebSocket问题与配套要求

本机SDK的`esp_transport_ws_poll_connection_closed`原实现无条件取得socket并执行`FD_SET(sock)`。ML307父层没有lwIP socket，原路径会把-1传给FD_SET，不能用虚构文件描述符绕过。

独立SDK候选在sock<0时改用父层poll／read，在一次有限截止时刻内观察关闭。0ms仅poll，不启动阻塞read；收到干净FIN才返回正常关闭，普通错误或关闭握手后的额外数据都不能当成功。带真实socket的既有路径保留原文。当前SDK没有直接修改：整个tcp_transport在新隔离工程中作为本地组件覆盖，project_description和编译命令确认实际选中了这份源码。

适配层使用固定SDK的transport私有foundation接口来提供错误存储，CMake将Windows IDF_PATH转成CMake路径后再引入私有头。本批首次配置因原反斜杠字符串触发`Invalid character escape`退出2；已保留失败源码／日志，修正后构建通过。

SDK原SSL session-ticket操作会将transport context解释成其内置SSL结构，与本适配层不同。候选对`CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS=y`显式编译报错；当前构建为关闭状态。启用真实Kconfig日期验证、证书包、HTTP custom transport及WS transport，没有使用模块auth0或跳过证书、原主机名、有效期验证。future owner仍必须提供可信时钟；也未实现客户端证书、自定义CA或内置SSL ticket ABI。

完整配套补丁为：

- `patches/ml307_transport.patch`：固定3.7.5原包起共15文件，包含此前raw／TLS／ISR修正和本批poll／adapter。
- `patches/uart_uhci.patch`：固定0.4.0原包起4文件；与前批配套一致，不能混用原始未修驱动。
- `patches/sdk_socketless_ws_close.patch`：本机固定5.5.6 SDK的transport_ws.c；只通过本地组件覆盖构建。

`patches/manifest.json`记录原包、候选和补丁SHA256；SDK基线HEAD为`880a203ae603d08a94d06aa01e0d6761958e5e9e`、版本`v5.5.6-dirty`，并固定原文件摘要，不能直接推断其他SDK版本兼容。

## 实际构建与检查

新目录`I:/xy_board_builds_20261109/ml307_transport_link_20261112`使用两份ML307／UHCI候选、现有WebSocket客户端1.8.0本地override、真实SDK HTTP客户端及本地tcp_transport覆盖，开启Python外网保护。没有安装／下载依赖。工程未启用PSRAM，单factory分区仅供组件检查，未改任何生产板型的PSRAM或双OTA方案。

```text
python -B evidence/ml307-poll-source-20261112/build_isolated.py
python -B evidence/ml307-poll-source-20261112/retry_build.py
idf.py -DIDF_TARGET=esp32s3 build
python -B evidence/ml307-poll-source-20261112/package_patches.py
python -B evidence/ml307-poll-source-20261112/inspect_link.py
python -B evidence/ml307-poll-source-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-poll-source-20261112/close_research.py
```

首项实际配置退出2；第二项修正路径后退出0。收尾进一步把SDK关闭poll／read改成共享截止时刻，实际在相同隔离目录重新build退出0，完整命令／cwd在final-build.json，前一成功日志和源码另留before-close-deadline-build。主入口始终为空，保留的API链接函数没有执行，也没有建立任何外部连接。

最终ELF为Xtensa EXEC，前批767个候选代码符号全部保留，新raw／TLS就绪API、adapter初始化、HTTP init/open、WS init/start及关闭检查符号均存在。实际634份直接编译源文件记录及28份产物／配置快照保存摘要。readelf／nm／size、esptool镜像checksum／hash、SDK分区解码退出0。dirty SDK仍遗留两个强制旧启动名UND，实际esp_startup_*实现存在，保持如实记录。没有运行协议／并发／错误注入／声学测试或独立测试套件。

## 仍待接入与总体状态

仍缺真实模组owner、ID安全退役与复位、断线后模组尾部排空、时钟启动流程、HTTP绑定／OTA和现有控制WebSocket的实际工厂接入。外部WS transport还必须按真实URI配置child路径、headers／鉴权和重定向策略；本次仅编译API不能证明这些选项会自动套用。尚需验证TLS证书／主机名／日期失败、close_notify／截断、同时上传与播放、超时取消／join、资源峰值及各板GPIO。

8个ML307变体仅追加本批研究，继续inventory_only。总体仍152／188官方源码入口，153本地profile含1历史入口，36项待源码；账本1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。未安装、启动服务、写Git、刷机、OTA、上传或发布。
