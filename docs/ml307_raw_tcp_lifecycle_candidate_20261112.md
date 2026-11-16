# ML307原始TCP连接生命周期候选（2026-11-12）

本批为前轮原始读写草稿补充配置、打开、关闭及连接ID状态。六文件完整补丁仍只保存于 `evidence/ml307-lifecycle-source-20261112/patches/`，**没有应用到固件、安装、编译、链接或执行测试**。当前148／188官方变体有源码入口、40项待源码，149本地profile含1历史入口；本批不增加支持数。

基线为 `78/esp-ml307 3.7.5` 提交 `506477b5af639f5dce9386e423e939091fe7fa11`，UART-UHCI依赖继续精确固定0.4.0。补丁包含前轮原始接收／发送和HEX保护，旧证据保持原文。

## 候选调用流程

真正网络owner必须先完成模组身份／固件确认、硬件重启、SIM／APN／PDP准备和串口波特率设置，停止所有旧消费者及并发初始化操作。随后独占UART调用 `EnableMl307RawReceive()`，要求无旧连接和残留接收数据；开关本身不重启模组，也不能证明这些前提成立。

开关开启后，普通 `SendCommandWithData`／`SendCommand` 全部拒绝，包括小写、组合命令与数据形式；`SetBaudRate`也拒绝，防止通用接口修改已建立的收发格式或连接状态。所有旧操作必须在切换前静止，不能把开关当成与旧初始化并发安全的接管功能。以后需要信号、网络注册等诊断时，仍需另接有明确响应格式的接口。

| 入口 | 候选请求与确认 |
| --- | --- |
| `ConfigureMl307RawTcp` | ATE0关闭回显，MIPSTATE确认INITIAL；设置并逐项查询SSL=0／SSL上下文0、发送与接收原始编码0／0、autofree=1手动释放 |
| `OpenMl307RawTcp` | 仅Prepared可用；MIPOPEN指定TCP及缓存模式2，等待本连接成功结果和OK，再查询MIPMODE=2及MIPSTATE的TCP／端口／CONNECTED |
| 原始读写 | 只允许Connected且控制事务空闲；保留前轮4096字节上限和专用长度帧／提示符处理 |
| `CloseMl307RawTcp` | 允许Connected或空闲断线后的Disconnected；MIPCLOSE不传模式参数，等待本连接关闭成功和OK，再查INITIAL或CLOSED；之后ID标Retired |

三个新入口均限定socket0..5、时限1..60000ms。打开端口非零，主机长度1..253且只允许ASCII字母、数字、点、短横线和冒号，阻止引号／控制字符进入AT命令；这不等于完整DNS或IP语法验证。MIPSTATE的地址可以是域名解析后的IP，不能代替TLS对原主机名的认证。

ATE0只取得OK，未做独立回显状态查询。SSL=0只建立底层TCP；后续仍必须在ESP端实现证书链和主机名校验，没有以明文HTTP／WS替代产品HTTPS／WSS要求。

## 事务和断线边界

每个新入口持有共享command锁，内部所有命令TX及响应等待沿用该入口同一截止时刻，不逐条重新计时。响应解析使用独立RX锁，不在事件回调等待AT；控制查询必须同时收到匹配的类型、字段数、连接和值及最终OK才完成。配置、打开、关闭在发布最终状态前再次检查原中间状态，避免两条控制命令之间的断线被覆盖。

连接状态固定六槽：Unused→Configuring→Prepared→Opening→Connected→Closing→Retired。空闲Connected收到断线改为Disconnected，禁止读写；因autofree=1仍需调用专用关闭入口清理，再Retired。关闭过程中断线可伴随关闭结果，不把它误当为新的可用连接。初始化／打开中断线、读写中断线、错误响应、重复或无事务的OK、响应格式不符、丢段和超时都会锁存整个raw会话错误，不隐式重试或扫描载荷重新同步。

原始读取增加AwaitOk阶段，按长度收完二进制并校验CRLF后等待最终OK；空缓存只OK仍保留。已经Complete时再来OK会拒绝，避免重复完成信号。普通文本接口不再能共享这些完成事件。

成功关闭后不在当前UART会话复用ID；最多六个新连接后必须由未来owner重启模组并重建UART对象。当前没有实现该硬件／资源生命周期。Retired不是实际硬件已释放的独立证明，INITIAL／CLOSED查询也不能替代子型号实机验收。模组没有事务序列号，任意同值迟到URC／OK仍可能难以区分，禁复用ID和错误锁存不解决所有重排问题。

## 来源与未完成项

候选命令关系来自已归档TCPIP V5手册的13–18、21–25、31–34页及精确3.7.5源码。只复核本地固定来源，没有新下载或重新渲染PDF。公开镜像手册不是当前实物固件行为的证明；冒号空格、分片、顺序、Close结果、DNS与错误码均须目标验收。

本批仍未实现真正socket owner、硬件重启、SIM／APN／PDP、接收ring／背压、持续重连、ESP TLS、HTTP绑定／OTA、WebSocket媒体与蜂窝板profile。内存预算、任务／回调销毁和完整吞吐时序未验收。C3／C5／C6固定唤醒词取舍仍待确认，没有静默改成按键模式。

无屏绑定继续采用牛牛选定的手机连接设备热点、在配网页读取六位绑定码方案，本批没有重改绑定和控制协议代码。

## 本批实际检查

执行以下来源／摘要／差异检查，未执行候选C++或测试套件：

```text
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-lifecycle-source-20261112/close_research.py
git diff --check
```

来源ZIP与六份原文、完整补丁及SHA256、所引用UHCI／手册摘要和选定源码关系由close_research.py核对，结果见同目录final-audit.json；这些检查不是编译、协议、并发或时限测试。固件240个main代码文件及native registry与本批前快照一致，研究仅追加八个ML307变体的本批记录，矩阵状态和支持数量不变。

当前构建账本2 passed_cached_sources／62 stale_cached_sources／6 failed_cached_sources／118 not_run，来自此前记录，本批未重跑；188项本轮实机均not_run，仅有现有微雪1.8可用于后续实物验收。未安装依赖／SDK、启动服务／Docker、写Git、刷机、OTA、上传或发布；全量适配目标继续进行。
