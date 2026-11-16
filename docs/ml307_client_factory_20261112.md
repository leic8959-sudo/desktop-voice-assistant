# ML307 HTTP／WebSocket客户端工厂候选

本批新增`Ml307HttpClient`和`Ml307WebSocketClient`，把已有认证TLS transport接到真实SDK客户端，并明确外部transport的路径、鉴权头和释放责任。此前只给WebSocket客户端传`ext_transport`，不会触发它为内置transport配置path／headers的代码，可能导致握手使用默认`/`或缺少现有凭据。本候选显式配置外部WS child，再创建客户端。**尚未替换固件绑定、OTA或控制协议里的调用，未注册新板型、联网或刷机。**

## 实际候选行为

HTTP工厂只接受`https` URL、有限1..60000ms网络时限，复用SDK的http_parser解析，拒绝URI内用户名／密码、fragment、无主机和非法端口。客户端保留4096字节缓冲，设置`disable_auto_redirect=true`，创建独立ML307 TLS parent。调用者借用handle设置POST／GET、Content-Type、Authorization，再使用现有HTTP读取流程。析构先执行SDK cleanup，后销毁外部parent；SDK cleanup会close外部transport，但只destroy自己的transport list，故外部句柄仍由工厂管理。

WebSocket工厂只接受`wss`。通过同一个SDK URL解析器构造完整路径和query，无路径时使用`/`，仅有query时为`/?...`；显式复制到WS child，不依赖client URI对外部transport产生隐含副作用。headers同时交给child和client；保留当前4096缓冲、8192任务栈、10秒网络超时、5秒重连、10秒ping／15秒pong超时设置。控制帧传播给SDK客户端处理，继续使用它的binary/text、ping／pong／close和分片接口。工厂本身不Start任务、不连接网络。

URI最大383字节；headers最大1024字节，逐行要求CRLF、有非空字段名和冒号、不含空行或裸换行。又根据固定SDK的`CONFIG_WS_BUFFER_SIZE`为握手强制保留256字节基础头空间，若path＋host＋附加headers超过缓冲边界就在创建时失败，不制造截断的Authorization或路径。本批未运行输入边界测试。

WS child采用新增`reject_redirect`策略：拒绝3xx，且非HTTP101都失败；101还必须通过原SDK的Sec-WebSocket-Accept校验。这个选项附加到本地SDK组件的config结构，默认false保留其他SDK调用方原行为；新工厂设置true。不得混用旧头文件、旧二进制SDK transport或遗漏这份补丁。HTTP已设置不自动跳转，因此不会把当前凭据自动送到另一个URL；借用handle时也不得绕开工厂调用set_uri／set_url或手动重定向。

WebSocket释放顺序为SDK client、WS child、TLS parent；WS child借用parent的错误foundation，最后才释放parent。包装对象不可复制，unique_ptr负责中途创建失败的回收。调用者必须先join外部sender和使用者，并从普通owner任务销毁，不能在WebSocket事件／UART回调中析构或另行destroy借用handle。SDK destroy会等待内部任务，**这不是有限时限的关闭接口**，生命周期约定与底层模组资源／ID安全退役仍需运行验收。

## 后续接入位置

| 当前固件入口 | 未来工厂接入 | 仍需保持的行为 |
| --- | --- | --- |
| main.cpp的pairing_post | HTTP工厂借用handle | 先绑定后音频、nonce／六位码、响应完整性与凭据保存 |
| ota_update.cpp的下载client | HTTP工厂借用handle | 授权头、长度／SHA／板型身份、双OTA分区与失败处理 |
| control_protocol.cpp的client创建 | 长期保存WS包装对象，借用handle | 原协议事件、鉴权头、语音上传、媒体和自然插话人工决定 |

本批只读取并归档这些源文件，没有改其中人工决定或替换调用。生产路径仍使用原网络客户端，因此没有把“候选工厂可编译”当作绑定／OTA／媒体已经走蜂窝链路。ML307候选仅允许认证HTTPS／WSS；现有可能配置的局域网HTTP／WS服务需要在正式板级接入前确定可用的TLS服务和可信时间来源，不能静默降级。

## 来源与构建

目录：`evidence/ml307-client-factory-20261112/`。复制上一批固定ML3073.7.5／UHCI0.4.0及SDK候选，并新增工厂文件和依赖声明；源码与头文件的配对变更独立归档。完整补丁目前为ML30724文件、UHCI4文件、SDK2文件。新增WebSocket1.8.0精确依赖及SDK HTTP／http_parser组件，使用已有本地源码override，没有安装或下载。

实际命令：

```text
python -B evidence/ml307-client-factory-20261112/build_isolated.py
python -B evidence/ml307-client-factory-20261112/link_current_abi.py
python -B evidence/ml307-client-factory-20261112/package_patches.py
python -B evidence/ml307-client-factory-20261112/inspect_link.py
python -B evidence/ml307-client-factory-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-client-factory-20261112/close_research.py
```

新隔离工程`I:/xy_board_builds_20261109/ml307_clients_link_20261112`使用现有SDK5.5.6-dirty／Xtensa14.2.0、SDK tcp_transport本地覆盖及Python外网保护。原始app_main为空；被保留但不执行的链接函数调用真实工厂、SDK POST设置及WS start／stop，用于链接覆盖而非模拟运行。

首轮工厂构建退出0。生成的未执行链接根曾把CRLF写成双反斜杠字面量，已保存initial-link-root.cpp并修正后实际重编；没有声称首轮运行鉴权成功。之后增加HTTP101检查，前一版本保存在before-strict-upgrade。最终构建以当前静态库提取835个T／W代码符号并全部保留，包含两个工厂和析构、连接owner、startup和真实HTTP／WS API。最终镜像1033168字节，摘要见link-inspection.json；全部真实ninja／idf命令、工作目录及退出码在final-build.json。实际readelf／nm／size、esptool镜像和分区检查均退出0。

保存636份直接编译输入记录及28份产物快照；八份读取过的固件／SDK／客户端来源另行归档。SDK遗留两个强制旧启动名UND仍如实记录，未修改已安装SDK。组件布局使用单factory且无PSRAM，仅供链接，不替代各板生产双OTA／内存布局。未执行独立测试套件、HTTP请求／WS握手、证书失败、重定向或并发销毁测试。

## 当前状态

8个ML307变体仅追加候选；还缺板级新会话复位／可信时钟、主动SIM／APN／PDP、正式客户端工厂接入、整板构建和实机验收。六个ID单次使用的owner规则保留，工厂不提供无限重连或绕过reset_required。

固件main、native registry和Spot当前构建输入未变。总体仍152／188官方源码入口、153本地profile含1历史、36项待源码；1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。未安装、启动服务、写Git、刷机、OTA、上传或发布。
