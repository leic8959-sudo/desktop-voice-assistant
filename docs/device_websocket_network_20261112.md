# WebSocket与蜂窝网络共用owner接入

本批把控制／媒体WebSocket的客户端创建落到真实固件的`DeviceWebSocketClient`，并将模组owner统一放到网络模块。绑定／OTA的HTTP与WebSocket现在借用同一个已注册owner；ML307网络启动和地址检查已有明确分支，服务地址配置不再依赖SSID。**没有新增ML307板型、安装候选依赖、执行联网或刷机。**

## 实际改动

新增`main/device_websocket_client.h/.cpp`。默认分支使用现有SDK客户端，参数与原控制协议创建处相同：4096字节缓冲、8192字节任务栈、5秒重连、10秒网络超时、10秒ping／15秒pong，WSS使用现有证书包。ML307分支使用前批真实工厂，显式配置外部WS child的路径／query／headers，保留HTTP101、禁止跳转及认证TLS要求；没有注册owner会返回错误，不转用Wi‑Fi。

控制协议持有全固件生命周期的包装对象，原`g_ws`仍作为借用handle交给SDK发送／接收和事件接口。这里只替换创建段，增加重复启动拒绝，以及事件注册／SDK任务启动失败时的客户端释放。此时尚无协议sender任务，Reset在普通启动任务执行。成功后的部分任务分配失败仍沿原启动错误路径，**没有完成整个协议的停止／重启或所有队列回收**。Reset／析构必须先join所有外部sender，不能在WebSocket／UART回调调用；SDK destroy的等待仍非严格有限时限。

原始音频收发、控制回执、ping业务消息、媒体操作、自然插话、完整唤醒暂停媒体的代码区段保持原文。审计比较`g_listening`定义起至control_protocol_start之前的整个区段，以及begin_listening函数起至文件末尾，均与修改前一致，覆盖两个人工决定。未修改这些决定，也没有因换客户端启用旧强打断、改变音频帧或凭据格式。

## 网络owner与配置

`device_network_register_ml307_owner`接收实际`Ml307ConnectionOwner`，启动前注册一次。原仅供HTTP的注册入口移至网络模块，HTTP和WebSocket通过同一getter取得shared_ptr，避免独立分配UART／连接ID资源。注册不自动Start，网络Start调用owner的真实Start(60000)，让板级复位、SIM／注册／PDP、时钟门槛先完成；缺owner或启动错误即返回错误，不能进入默认Wi‑Fi分支。

候选owner新增`NetworkReady()`，用try-lock检查启动状态、reset_required、当前PDP状态和raw UART健康。ML307地址就绪依赖此快照，不创建或假设lwIP DHCP netif。这个函数可能因锁竞争暂时返回false，调用方沿现有等待逻辑重试；它不是服务器可达或TLS握手证明。

ML307配置只要求WSS服务地址，SSID／密码原值继续保存。配置页提供服务地址和旧设备令牌入口，POST不要求Wi‑Fi字段，也不覆盖旧SSID／密码。其他Wi‑Fi／NT26／RNDIS路径继续使用原分支。新页、保存、NVS和错误返回均未启动运行或做浏览器验收；无屏热点授权及六位绑定码代码没有改动，蜂窝无屏页面的完整接入仍需单独覆盖。

owner替换和连接ID耗尽后的会话重建尚未实现；当前注册一次、六个ID每会话只用一次的限制保留。因此本批不能宣称绑定长轮询或长期WebSocket重连已可靠运行，仍需板级的停止／join／复位／重新注册方案。实际电源复位和可信时钟回调也尚无板型实现。

## 验证范围

| 实际执行 | 结果 | 边界 |
| --- | --- | --- |
| BOX2 Wi‑Fi `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=alientek-atk-dnesp32s3-box2-wifi reconfigure build` | 退出0，完整镜像2236000字节、模型2203819字节；267项当前输入、24份产物快照；镜像／身份／双3MiB OTA检查通过 | 现有Wi‑Fi整板编译链接，未实机联网／音频／声学验收 |
| 独立ML307工程`idf.py -DIDF_TARGET=esp32s3 build` | 退出0，当前network、config、provisioning、HTTP、WS源码以ML307宏编译；候选owner当前843个代码符号保留，镜像1464016字节；779编译源记录、38产物快照 | app_main为空，无网络请求／NVS写入；不含真实板级音频显示和控制协议整板链接 |
| 当前main.cpp／ota_update.cpp／control_protocol.cpp的ML307临时编译 | 三份真实S3源码各退出0，目标及未解析引用日志归档 | 使用已有BOX2生成配置＋显式ML307宏和C++23，仅编译单元，非正式4G板型配置／完整链接 |

两项工程使用现有SDK5.5.6-dirty、Xtensa14.2.0、本地源码override及Python外网保护，未安装或下载依赖。BOX2复用原隔离stage并先保存历史源码／产物；ML307新stage为`I:/xy_board_builds_20261109/ml307_ws_network_link_20261112`，其单factory／无PSRAM组件配置不替代生产板型分区。SDK两个强制旧启动名UND及实际esp_startup_*实现的既有限制仍如实记录。真实模组来源仍需26文件ML307、4文件UHCI、2文件SDK成对补丁，尚未安装至固件依赖。

实际入口命令：

```text
python -B evidence/device-websocket-network-20261112/build_native.py
python -B evidence/device-websocket-network-20261112/collect_native.py
python -B evidence/device-websocket-network-20261112/cellular_build_isolated.py
python -B evidence/device-websocket-network-20261112/cellular_link_current_abi.py
python -B evidence/device-websocket-network-20261112/compile_cellular_callsites.py
python -B evidence/device-websocket-network-20261112/cellular_package_patches.py
python -B evidence/device-websocket-network-20261112/cellular_inspect_link.py
python -B evidence/device-websocket-network-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/device-websocket-network-20261112/close_research.py
```

全部编译／链接、readelf／nm／size、esptool及分区检查退出0。命令数组／cwd／源码与产物摘要在本批evidence；编译与链接是两种范围的证据，未运行测试套件、模拟状态机、服务、实际配对、HTTP／WS握手或硬件故障注入。

## 剩余与总体状态

本批改8个已有main文件、增2个main文件；依赖候选只在owner头／实现增加NetworkReady。native registry、audio_io、固定唤醒、OTA内容校验／双分区选择、NVS布局和绑定顺序未改。下一步仍需具体板型的电源／新会话复位与可信时间、正式依赖配置／owner注册、六ID耗尽恢复、整板构建和实机验收。

官方源码入口仍152／188，153本地profile含1历史，36项待源码；账本1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，当前通过项是本批BOX2 Wi‑Fi。188项本轮实机全部not_run。未Git写入、刷机、OTA、上传或发布。
