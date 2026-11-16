# ML307启动命令截止时间候选

本批在ML307连接owner和启动就绪候选上补齐启动阶段的共享截止时间。`AtUart::SendStartupCommand`／`RunStartupCommand`让一次启动命令的UART发送和响应等待共用同一个deadline；波特率探测逐个候选检查剩余时间，切换到目标速率后必须再次发送AT确认。**最终只证明源码编译／链接，未向模组发送命令、测量调度误差或完成板级接入。**

## 启动路径

Detect为内部RAM分配AtUart，Initialize后检查任务创建和实际状态。它在总预算内探测已支持波特率，发送`AT+IPR`后切换ESP UART并使用同一剩余预算验证`AT`，再用共享截止时间查询`AT+CGMR`。raw owner以`raw_transport=true`调用Detect，要求响应以ML307开头，跳过派生类旧HTTP清理；普通EC801E／ML307网络接口仍可通过`InitializeLegacy`执行原有专用命令，但每条命令使用共享阶段预算。

启动命令路径不接受换行或NUL，拒绝UART worker／raw会话／另一个命令占用；发送或确认在超时后锁存`startup_failed`，后续普通AT和raw启用均被拒绝，防止迟到OK完成下一条命令。错的波特率探测允许清掉仅用于探测的乱码；正式命令不做文本重同步。畸形CME字段在普通和raw路径都发出错误并锁存相应失败。

owner的Start先执行板级`prepare_clean_session`回调，再检测ML307、等待SIM READY／CEREG注册／ML307有效PDP，最后开启raw接收。平台回调必须确认模组是新的无旧连接会话；候选不会自动发送MREBOOT或猜测供电脚。可信时钟回调仍是Acquire TLS前置条件，未向现有固件接入。

普通`SendCommandWithData`和查询诊断接口仍沿旧独立超时语义；本批只收敛启动路径，不能宣称所有AT命令都在全局总预算内。UHCI底层发送本身已有传入超时，但FreeRTOS调度和模组响应时间只能通过实测确认。

## 实际构建

新隔离目录：`I:/xy_board_builds_20261109/ml307_deadline_link_20261112`。来源为上批固定3.7.5／UHCI0.4.0／SDK tcp_transport候选的复制，加本批启动代码；没有安装、下载或修改已安装SDK。

```text
python -B evidence/ml307-startup-deadline-20261112/build_isolated.py
python -B evidence/ml307-startup-deadline-20261112/link_current_abi.py
python -B evidence/ml307-startup-deadline-20261112/package_patches.py
python -B evidence/ml307-startup-deadline-20261112/inspect_link.py
python -B evidence/ml307-startup-deadline-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-startup-deadline-20261112/close_research.py
```

实际SDK为5.5.6-dirty，Python外网保护继续启用。最后的ninja先从当前库提取代码符号，再执行完整`idf.py -DIDF_TARGET=esp32s3 build`；所有记录退出0。最终ELF为Xtensa EXEC，当前823个组件代码符号包含`SendStartupCommand`、`RunStartupCommand`、两个派生`InitializeLegacy`、owner Start／Acquire／Retire及前批HTTP／WS入口。镜像为**1030240字节**，checksum／validation hash有效；635份编译源和28份产物快照归档。SDK强制旧启动名仍按历史状态记录，未修SDK。

当前工程app_main为空，保留根不会实际执行启动命令、波特率探测或连接ID租约；没有串口、模组、TLS握手、超时边界、故障注入或实机证据。不能把组件链接通过写成完整固件构建或网络验收。

## 补丁与板级差异

本批完整ML307补丁为22文件，UHCI补丁4文件，SDK无socket WebSocket关闭补丁1文件。`patches/manifest.json`逐文件记录固定原包与候选SHA256；必须成对使用，不能与未修UHCI混编。

8个ML307变体的TX/RX、DTR／RI、供电候选和控制台冲突仍见`evidence/ml307-startup-source-20261112/board-pin-review.json`。ATK BOX2 4G源码有IO扩展器的4G使能高电平；其他变体没有独立模组复位定义，不能由owner自动重启。ATK和正辰CAM使用43／44，与ESP32-S3默认UART0控制台冲突，板级profile必须先选择经过核对的调试通道。没有任何一个变体因本批成为native profile。

## 总体状态

8个ML307变体只追加启动截止时间候选。官方矩阵仍152／188个源码入口，153个本地profile含1历史入口，36项待源码；账本1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。仍缺板级复位／可信时钟回调、主动APN/PDP、绑定／OTA／控制媒体WS工厂、全板构建和实机验收。未安装依赖、启动服务、写Git、刷机、OTA、上传或发布。
