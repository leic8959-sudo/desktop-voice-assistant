# 绑定／OTA共用HTTP客户端入口

本批将设备绑定和OTA下载的实际调用改为`DeviceHttpClient`，统一HTTP客户端及外部transport的所有权。默认分支使用现有ESP-IDF网络栈；`ASSISTANT_BOARD_ML307`分支使用前批`Ml307HttpClient`工厂并要求板级启动先注册真实owner，没有注册时明确返回失败。本批没有为任何新板型登记该宏、安装ML307依赖或提供虚拟owner。

## 改动范围与行为

新增`main/device_http_client.h/.cpp`，修改main.cpp、ota_update.cpp及main/CMakeLists.txt。类不可复制，SDK handle为借用，调用者继续用原SDK方法设置POST／GET、Content-Type或Authorization、发送／读取响应。只有创建和释放经过共用入口；没有复制绑定、JSON、OTA或网络协议实现。

Init接收URL、1..60000ms超时和接收缓冲大小。默认网络HTTP沿原明文配置，HTTPS使用已有ESP证书包；原OTA没有在创建处提供证书包，现在也通过统一入口接入。绑定仍10秒、默认接收缓冲，OTA仍10秒／4096字节。两者禁自动跳转，调用方继续只接受各自业务状态／完成条件，不把HTTP200或新连接对象当作绑定成功。

蜂窝分支只接受工厂支持的缓冲配置，交由工厂校验HTTPS URL和管理TLS parent。它不会转用默认Wi‑Fi，也不修改TLS证书／主机名／日期要求。owner注册仅允许一次，便于当前单启动流程；六个ID耗尽后的owner替换、整机重连和停用流程尚未接入，不能认为这个注册接口支持无限会话恢复。

Reset释放SDK client，蜂窝工厂再释放其外部TLS parent；本地清理不等于对端或模组关闭确认。HTTP对象必须在所有使用者退出后销毁。FreeRTOS的`vTaskDelete`不会展开C++栈，所以OTA保留显式清理：成功和HTTP打开失败、非200、长度非法、OTA begin失败都在任务退出前Reset；下载循环后先Reset，再处理长度／SHA／板型和分区错误。默认Reset记录SDK清理失败，蜂窝工厂仍沿候选的清理日志／退役策略。本批没有运行故障注入或证明异常清理已通过实机验收。

原配对nonce／六位码、先绑定后音频、响应完整性／凭据保存，OTA长度／SHA／project_name／双OTA分区选择和内部RAM任务栈保留。control_protocol.cpp和其中自然插话／完整唤醒人工决定逐字保持原文；音频、绑定热点、NVS和native registry本批未改。CMake为未来ML307宏声明真实依赖及C++23，目前没有native profile选择它。

## 实际构建

两项构建分别证明各自范围：

| 目标／命令 | 实际证据 | 未覆盖 |
| --- | --- | --- |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=alientek-atk-dnesp32s3-box2-wifi reconfigure build` | 完整当前BOX2 Wi‑Fi固件退出0，日志明确重编main、OTA和device_http_client；镜像2235344字节，模型2203819字节，身份／双3MiB OTA／镜像校验通过，265项固件输入、24份产物归档 | 实际HTTP绑定、HTTPS下载、flash写入及设备验收 |
| `idf.py -DIDF_TARGET=esp32s3 build`，独立蜂窝组件工程 | 当前工作区device_http_client原文以ML307宏编译，并与真实候选工厂／owner／HTTP／WS库链接退出0；842个候选代码符号加DeviceHttpClient Init／Reset及owner注册入口在ELF；镜像1039440字节，638编译输入、30快照 | main／OTA本体的ML307整板编译、板级电源／时钟、owner实际注册及模组运行 |

使用现有SDK5.5.6-dirty、Xtensa14.2.0、缓存组件及Python外网保护，未安装或下载依赖。BOX2复用先前隔离stage`I:/xy_board_builds_20261109/atkbox2_modem_control_20261112`，旧源码和产物保存在先前evidence，本批同步当前文件并重新构建。蜂窝stage为`I:/xy_board_builds_20261109/ml307_http_callsite_link_20261112`，仍用SDK tcp_transport本地覆盖。后者app_main为空，不执行链接根／HTTP请求，不是蜂窝整板固件；其SDK强制旧启动名UND保持已记录限制。

主要实际命令和精确参数：

```text
python -B evidence/device-http-client-20261112/build_native.py
python -B evidence/device-http-client-20261112/collect_native.py
python -B evidence/device-http-client-20261112/cellular_build_isolated.py
python -B evidence/device-http-client-20261112/cellular_link_current_abi.py
python -B evidence/device-http-client-20261112/cellular_package_patches.py
python -B evidence/device-http-client-20261112/cellular_inspect_link.py
python -B evidence/device-http-client-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/device-http-client-20261112/close_research.py
```

前六项及构建／镜像／ELF／分区检查均退出0；命令数组、cwd和结果在native-build.json、native-commands.json、final-build.json、commands.json。native与蜂窝检查日志分开命名，避免相同文件名覆盖证据。未运行测试套件、实际配对／OTA、长期服务或类型检查。

## 仍待完成与账本

本批将共享HTTP接入点落到真实固件，尚需正式ML307依赖安装／板级配置、电源复位／可信时间、网络启动owner注册，以及控制WebSocket工厂接入。8个ML307变体继续inventory_only。还需验证重定向／错误响应、取消／清理、证书失败和真实下载，以及各板的完整构建和实机验收。

152／188官方变体有源码入口，153本地profile含1历史，36项待源码；当前1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，通过项为本批BOX2 Wi‑Fi。新增独立构建记录`cached_build_attempts_20261112_device_http_box2.json`，原记录保留为历史。188项本轮实机全部not_run。未Git写入、刷机、OTA、上传或发布。
