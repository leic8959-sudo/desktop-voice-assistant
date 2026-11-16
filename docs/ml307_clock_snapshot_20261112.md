# ML307时钟快照与引号字段解析候选

本批修正AT参数解析无法保留引号内逗号的问题，新增`ReadClockSnapshot`及严格日历解析，并在连接owner开启raw模式前留出明确的板级校时回调。当前真实S3组件构建与链接退出0，**没有查询实际模组、设置系统时钟或验证任何日期输入／TLS握手**。固件main和native registry未改，8个ML307变体仍待适配。

## 解析缺口与改动

原ParseResponse对所有参数使用`getline(..., ',')`拆分，因此`+CCLK: "21/12/27,06:56:20+32"`会成为两个字段，无法按手册的单一字符串读取。候选按AT字段边界扫描：双引号内逗号保留，闭引号后只接受可选空白、逗号或行结束；普通未加引号字段继续按逗号分割，保留纯数字HEX的原文本并沿用数值类型分类。外围空白去除，空字段和最后的空字段明确保留。

参数列表最多65536字节、64字段，NUL、未闭合引号、引号后的多余字符等被拒绝。普通启动锁存startup_failed，raw会话锁存接收错误并向命令等待者发失败，不把畸形响应交给上层。新增扫描器只支持普通成对引号，没有实现厂商特有的转义引号扩展；所有legacy HTTP／其他模组的响应兼容性和极限字段数仍需运行验证。原raw二进制长度帧先于文本参数处理，不让密文进入引号扫描。

## 只返回校验过的日历字段

新增`ModemClockSnapshot`保存year／month／day、hour／minute／second、是否含时区、四分之一小时偏移和单调接收时刻。`parse_modem_clock`只接受`yy/MM/dd,hh:mm:ss`，或追加`±zz`的20字符形式；校验固定分隔符、所有数字、月份／日历闰年、0–23小时、0–59分秒、±96以内的偏移。两位年份按通用AT手册1970–2069映射。这是语法／日历范围，**不是某个ML307R固件的可用年份承诺**；ML307R2037上限、不支持±01等型号限制仍应由板级采用策略检查。

不通过mktime默认时区或timegm推断绝对时刻，不使用编译日期，不调用settimeofday。快照没有“已经可信”标志，也不会自动减去时区偏移。

`AtModem::ReadClockSnapshot`只允许网络就绪、非UART工作任务、1..60000ms启动查询，使用独立query try-lock防同类并发。在既有状态mutex下清旧结果，发送共享deadline的`AT+CCLK?`；回调要求单字符串、解析成功并记录实际接收单调时间。最终OK、有效快照、请求开始后的接收时刻、网络仍就绪且预算未过期同时满足才返回。CCLK没有事务序号，收件时间不能排除某个恰在窗口内到达的旧／非请求通知；本批没有将其当作带签名的新鲜时间证明。

## 校时采用点与资料差异

两份已固定资料的相关原文现分别保存在`clock-source-review.json`及clock-reference分页文本中：

| 固定来源 | 说明 |
| --- | --- |
| ML307R通信流程V1.0.1，PDF第19页 | 日期时间主体说明为UTC，±zz为四分之一小时偏移；例子将北京时间14:19对应为06:19+32 |
| 4G AT参考V1.8.1，PDF第78页 | 通用格式说明为当地时间，列出部分S系列GMT行为以及其他型号日期范围／偏移限制 |

本批没有选择一个全型号通用转换规则。实际模组型号、固件和网络校时行为仍未核实；校历有效与来源可信是不同条件。

连接owner新增可选`accept_clock_snapshot(snapshot, remaining_ms)`。如果现有`trusted_clock_ready()`已通过，保留已有时钟；否则在SIM／注册／PDP就绪后、EnableMl307RawReceive之前读取快照，再交给明确的板级回调处理。回调需按已核实的型号／固件解释、采用和检查时间；回调成功后仍必须通过原trusted_clock_ready检查并在启动预算内，才开启raw。缺少回调、日期无效、未被采用或超时均返回具体状态，不用空回调或未认证NTP结果代替现有门槛。

回调同步执行、不可抛异常或重入owner，不可保留快照临时地址；若需异步处理应自行复制。这一回调尚未在任何真实板型实现。本批只把采用点接到候选Start，没有声称已完成可信时间来源、持久化、回拨保护或时钟恢复。

## 实际源码与构建

候选目录`evidence/ml307-clock-snapshot-20261112/candidate/`。相对前批PDP候选8文件有变化：新增modem_clock.h／.cc，修改CMake、at_uart.cc、at_modem.h／.cc、ml307_transport.h和ml307_connection_owner.cc。完整固定3.7.5补丁26文件，仍须成对使用UHCI0.4.0四文件和SDK transport两文件补丁。

```text
python -B evidence/ml307-clock-snapshot-20261112/build_isolated.py
python -B evidence/ml307-clock-snapshot-20261112/link_current_abi.py
python -B evidence/ml307-clock-snapshot-20261112/package_patches.py
python -B evidence/ml307-clock-snapshot-20261112/inspect_link.py
python -B evidence/ml307-clock-snapshot-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-clock-snapshot-20261112/close_research.py
```

新stage为`I:/xy_board_builds_20261109/ml307_clock_link_20261112`。使用既有SDK5.5.6-dirty、Xtensa14.2.0、固定本地依赖／SDK组件override及Python外网保护，没有安装或下载组件。首轮编译退出0；随后同步最终owner回调并重编当前库，以当前842个T／W代码符号作为保留根再次执行`idf.py -DIDF_TARGET=esp32s3 build`退出0。

最终ELF为Xtensa EXEC，842项全部存在，包含parse_modem_clock、ReadClockSnapshot、owner及此前认证HTTP／WS工厂。镜像1038432字节，checksum／hash和分区解码检查退出0；637份直接编译源记录、28份产物／配置快照。SDK的两个强制旧启动名UND及实际esp_startup_*实现仍如实保留。app_main为空，保留根没有执行任何查询、解析样本、回调或系统时间写入。没有新增或运行独立测试套件、日期边界测试、协议／故障注入、模组或实机测试。

## 尚未完成及总体账本

后续仍需按具体模组／固件完成并验收采用策略和可信来源、板级电源／新会话复位、正式客户端工厂接入、完整板卡配置／构建及实机录放音／联网验收。纯源码判断不能证明字段兼容、时区处理、调度时限、析构并发或声学效果。

固件main、native registry及当前BOX2 Wi‑Fi缓存输入逐项保持原文，原自定义／固定唤醒决定未改。总体仍152／188官方源码入口、153本地profile含1历史、36项待源码；1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。未启动服务、Git写入、刷机、OTA、上传或发布。
