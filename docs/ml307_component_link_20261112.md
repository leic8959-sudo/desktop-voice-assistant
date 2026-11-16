# ML307／UHCI组件完整链接与TLS日期配置

本批在全新隔离工程中，使用已归档的ML307 3.7.5及配套UHCI 0.4.0候选、现有ESP-IDF5.5.6工具链，重新编译完整组件和mbedTLS。最终`idf.py -DIDF_TARGET=esp32s3 build`退出0，生成Xtensa EXEC及988176字节镜像。**这是入口为空的组件链接检查工程；不是可交付的小燕子蜂窝板固件，没有刷机、联网或真实TLS握手。**

## 源码、配置与链接覆盖

隔离目录：`I:/xy_board_builds_20261109/ml307_component_link_20261112`。ML307与UHCI均使用本地override，固定来源与上一批成对补丁相同，源码未改。SDK版本为`v5.5.6-dirty`，HEAD为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，现有SDK未修改。依赖解析日志确认只使用这两份本地包及SDK，IDF5.5内置MQTT参与构建，IDF6才需要的外部MQTT包被跳过；Python外网保护继续启用，未安装或下载依赖。

本工程采用基础S3配置，`CONFIG_SPIRAM`未启用；分区为SDK的单factory应用，仅供组件检查。它不能替代具体PSRAM板卡配置、内部RAM分配及生产双OTA方案的构建验收，也没有更改生产板卡分区。

真实`sdkconfig.defaults`及生成配置开启`CONFIG_MBEDTLS_HAVE_TIME`、`CONFIG_MBEDTLS_HAVE_TIME_DATE`和完整证书包，未使用上一批的命令行日期宏替代Kconfig。本批实际重建mbedTLS库，X509真实编译命令的预处理结果含`MBEDTLS_HAVE_TIME_DATE`，新生成`x509_crt.c.obj`引用日期转换及比较函数`mbedtls_x509_time_gmtime`／`mbedtls_x509_time_cmp`。这补上完整库日期分支的编译证据；可信时钟与证书过期、尚未生效、主机名／证书链失败仍需运行验收。

完整候选共24份编译单元：23份ML307包源码及1份UHCI源码。连同SDK和工程入口，共保存628份实际编译源文件的摘要、命令及工作目录；这些是直接编译输入记录，不声称覆盖所有传递头文件。生成配置、工具响应参数、组件库、ELF／镜像／map、锁文件及相关SDK来源共35份快照归档。

第一次构建对整个工程使用`--no-gc-sections`，最终链接失败，日志保留于`build.log`／`build-attempt.json`。全局保留SDK代码引入了仅供其他配置使用的bootloader、SPI等未解析符号和qsort重定位错误；没有为通过而修改SDK或添加桩接口。

第二次仅调整隔离工程的链接保留方式：两个候选库仍为`WHOLE_ARCHIVE`，从实际静态库提取767个全局T／W代码符号，以逐项`--undefined`作为保留根，SDK使用正常`--gc-sections`。767项全部出现在最终ELF中，包括原始流、认证TLS的Create／Connect／Read／Write、AT缓存读取和UHCI发送接口；候选源码保持原文。最终链接及镜像分区容量检查均退出0，不能把空入口配合裁剪得到的小镜像冒充组件覆盖。

`nm -u`仍有两个本机SDK强制名字：`start_app`及`start_app_other_cores`。SDK的CMake用`-u`保留旧名字，当前实现已使用`esp_startup_start_app`／`esp_startup_start_app_other_cores`，两份实际实现都在ELF中。最终ELF无重定位表，未出现其他未定义符号；因此记录“候选符号已解析、SDK遗留两个强制未定义名”，不写“ELF全部符号零未定义”或“干净SDK已通过”。本批未修复SDK；实际启动与运行仍未验证。

## 执行记录与边界

实际执行的入口命令（Python为既有IDF环境）：

```text
python -B evidence/ml307-component-link-20261112/build_isolated.py
python -B evidence/ml307-component-link-20261112/link_retained.py
python -B evidence/ml307-component-link-20261112/inspect_link.py
python -B evidence/ml307-component-link-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-component-link-20261112/close_research.py
```

首项记录第一次构建退出2；第二项保留767个候选代码符号后构建退出0。第三项实际调用Xtensa readelf／nm／size、真实X509命令预处理、esptool镜像信息和分区解码，命令数组、工作目录及结果见`inspection-commands.json`。镜像checksum与hash有效，ELF为Xtensa EXEC。`size`包含链接布局占位，不能用其BSS合计推断板卡实际RAM需求。未执行独立测试套件、协议／错误注入测试或类型检查。

八个ML307变体仅追加本次组件证据，不新增native profile。还缺模组owner和内部RAM分配、硬件重启、SIM／APN／PDP、可信时钟、连接ID租约及重连资源、对端断线后的模组尾部排空、跨线程调度、认证esp_transport／HTTP绑定OTA与WSS媒体接入，以及板卡配置／链接、UART实时性和实机验收。当前工程没有启动这些功能，也未验证旧库中其他传输路径的认证行为。

本批固件main和native registry未改。当前仍152／188官方源码入口＋1历史入口＝153本地profile，36项待源码；整板账本仍0 passed_cached_sources／65 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。没有安装依赖、启动服务、Git写入、刷机、OTA、上传或发布。

证据入口：`evidence/ml307-component-link-20261112/link-inspection.json`、`artifacts.json`、`retained-build.json`及`final-audit.json`。
