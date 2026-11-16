# ML307默认承载接入及供电／时间核对

本批补齐默认CID1的主动激活、就绪隔离和socket绑定候选。新来源确认，ATK BOX2的4G使能源码还不足以证明模组已复位；时间查询也不能直接代替已有可信时钟门槛。**新增PDP代码已完成真实S3组件编译／链接，但没有接入板型或执行模组通信。**8个ML307变体继续待适配，官方源码入口数量不变。

## 新取得的固定手册

从公开镜像`RomaWan/ML307`固定提交`34463464c18898825c9ae90affcab600edea0175`取得两份中移物联网标识手册；下载后逐份核对Git blob SHA1、大小及SHA256。TCP/IP V5手册复用此前已核对的固定文件，并在本目录保留副本。镜像提供来源可追溯性，不能替代实际模组型号、AT固件版本或板卡修订确认。

| 手册／PDF页码 | 原文依据及影响 |
| --- | --- |
| ML307R硬件设计AT版V1.0.1，第23–24页，表16／图5 | VBAT正常3.4–4.5V；电源重启需连续至少100ms低于1.8V。一个GPIO输出低或等待100ms不能证明VBAT已达到此条件。 |
| 同上第35–37页，表25／图12 | PWR_ON/OFF为模组开关机接口：低2–3.5s开机、低3.5–4s关机；稳定VBAT到按键动作建议至少500ms。不能将其与板上4G电源使能混用。 |
| 同上第38页，图13 | RESET低至少300ms后释放实现复位；涉及1.8V接口／推荐开集控制电路，不能猜测未定义GPIO直接驱动。 |
| ML307R通信流程V1.0.1，第11页 | 自适应波特率模式可能没有MATREADY，输入AT并得到OK后可继续；每条命令完成后才能发下一条。不能只靠等待MATREADY判断启动。 |
| 同上第12–13页 | 默认承载为CID1，由模组自动配置；未激活时可用MIPCALL=1,1建立应用层拨号，不建议用CGACT作通用激活／去激活。响应可列多个CID、单地址或双栈两个地址。CID8是IMS专用。 |
| 同上第19–20页 | CCLK可读取自动网络校时，MNTP可向NTP服务器查询。该指南把日期时间主体说明为UTC，±zz为四分之一小时偏移，并给出北京时间对应UTC主体＋32的例子；不能未经核对按通用“当地时间”规则再减一次8小时。手册未说明认证时间机制，本批未放宽trusted_clock_ready。 |
| TCP/IP V5，第13、17页 | MIPCFG="cid"可给socket设置并查询PDP上下文，指定的CID应已经激活。 |

两份新PDF分别29／61页。使用既有pypdf提取完整分页文本，以既有PyMuPDF渲染11页，另渲染TCP/IP第13页；其中7页实际目视核对（硬件23／35／38、通信12／13／19、TCP/IP13）。没有把“12页已渲染”写成“12页均目视”。文件、摘要及页索引见`evidence/ml307-power-time-20261112/manual-source-artifacts.json`、`manual-review.json`。

## 实现的PDP接入

新增共享常量`AtUart::kMl307PdpContextId=1`，供模组就绪判断、激活命令和raw socket配置共用。此前MIPCALL回调会对任何CID的状态发布network_ready，可能让CID2／IMS等其他承载的激活造成误就绪，或其下线误清除当前连接。候选只使用选定CID1的响应；其他合法CID不改变本连接就绪。

选定CID的失活响应要求状态0、两字段；激活响应要求状态1、一个或两个字符串地址。任一非空地址须可解析；允许双栈中的一个字段为空或为未指定地址（0.0.0.0／::），但至少一个地址必须非全零，否则拒绝。字段类型／数量不符锁存PDP错误并停止启动；没有把错误的响应解释成正常断线或空地址成功。这仅按固定手册列出的响应实现，其他模组固件格式须另核对，不承诺所有ML307型号等价。

注册成功后先查询MIPCALL，每次查询都要求看到选定CID响应；已激活且地址有效才放行。如果选定CID未激活，则只发送一次`AT+MIPCALL=1,1`，保留原共享截止时间并等待URC／继续查询确认。命令最终OK只表示命令已接受，仍需SIM READY、注册有效、选定CID激活及有效IP同时满足；到期或畸形响应明确失败。未强行写死cmnet等运营商APN、配置IMS或使用CGACT改变PDP。

每个raw TCP socket在INITIAL状态下设置`MIPCFG="cid",socket,1`，再读取`MIPCFG="cid",socket`。只有选定socket、类型正确且CID1的回读和最终OK都确认后，才继续SSL关闭／原始编码／手动释放配置。使用既有控制事务deadline和会话锁存，回读不符即停止raw会话。这样模组就绪检查与实际TCP使用的承载一致，不能只验证一个CID却让socket使用默认的另一个CID。

本批相对上一候选只修改4文件：at_uart.h／at_uart.cc、ML307 at_modem头／实现。HTTP／WebSocket工厂、语音行为、固件main和native registry未改。

## BOX2整板结论

当前`atk_box2_board` Wi-Fi驱动明确保留4G bit12和所有未拥有位，不能直接复制成“已经复位模组”的回调。固定4G板源码只表明XIO_EN_4G初始化为高，尚缺其与模组VBAT／PWR_ON/OFF的电路关系、放电路径、实际最低电压和脉宽。新手册说明为什么简单翻转这个使能位仍不能通过prepare_clean_session：必须有对应电路证据或实际测量。其他7个ML307变体没有独立复位脚定义，门槛同样没有消失。

本批未给trusted_clock_ready编写恒true、当前编译日期或未验证的CCLK回调，也没有取消证书日期校验。CCLK／MNTP可作为后续校时方案依据，但实际字符串格式、可信来源、持久化与新启动策略尚待完成。上述条件仍明确登记为板级接入阻塞，没有登记BOX2 4G新profile。

## 真实构建与检查

新隔离目录`I:/xy_board_builds_20261109/ml307_pdp_link_20261112`使用SDK5.5.6-dirty、固定候选、真实HTTP／WebSocket1.8.0及SDK tcp_transport本地覆盖，Python外网保护保留；未安装、下载或修改已安装SDK。新资料下载只读使用gh blob接口，不运行模组固件。

```text
python -B evidence/ml307-power-time-20261112/build_isolated.py
python -B evidence/ml307-power-time-20261112/link_current_abi.py
python -B evidence/ml307-power-time-20261112/package_patches.py
python -B evidence/ml307-power-time-20261112/inspect_link.py
python -B evidence/ml307-power-time-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ml307-power-time-20261112/close_research.py
```

首项因生成的未执行C++链接根CRLF字面量有真实换行导致退出2，失败main保存在first-attempt，build.log和build-attempt.json不覆盖。修正生成器为raw字符串，并用上批已核对的链接根实际重编；随后ninja、当前库符号提取和`idf.py -DIDF_TARGET=esp32s3 build`均退出0。PDP候选文件没有因生成问题回退。

最终ELF为Xtensa EXEC，当前835个T／W代码符号全部保留；镜像**1034336字节**，身份、checksum／hash检查退出0。636份直接编译源记录、28份产物快照，完整ML30724文件＋UHCI4文件＋SDK2文件配套补丁均保存摘要。当前源码与stage一致，SDK遗留两旧启动名UND及实际esp_startup_*实现继续如实记录。app_main为空，未运行激活、CID回读、多PDP通知、TLS、重置、时钟或独立测试套件。

## 当前状态

8个ML307变体仅追加本批候选和电气／时间依据。仍152／188官方源码入口，153本地profile含1历史，36项待源码；1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。全量适配继续，仍需板级电源／可信时间、正式客户端调用接入、整板构建和实机验收。未启动服务、Git写入、刷机、OTA、上传或发布。
