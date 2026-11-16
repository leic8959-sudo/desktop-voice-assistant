# 绑定结果轮询复用 HTTP 会话

本批仅修改固件 `main/main.cpp` 的配对请求和客户端生命周期。原来每三秒查询一次结果时都会重新初始化、连接和销毁 HTTP 客户端；ML307 候选 owner 每个模组会话只有六个 ID，正常关闭也会退役 ID，因此这种轮询无法持续十分钟。

现在创建绑定请求使用一次独立客户端；结果查询初始化另一个固定地址的客户端，在十分钟轮询窗口内复用。没有对借用 handle 调用 set_url，也没有增加跨主机、跨协议或重定向能力。ML307 仍要求 HTTPS 和原证书、主机名、可信时钟校验。初始化两次不代表一定只建立两条连接，实际还取决于服务端、代理、网络和模组健康。

## 响应与失败处理

- 仅完整消费响应、读到结束且 SDK 判断可持久连接时保留传输。结果仍受 2047 字节上限约束，恰好填满 2048 字节缓冲也拒绝，避免 SDK 已解析完成但仍有缓存正文未消费时误接收或复用。
- 写入失败、读超时、响应不完整或过大均销毁整个客户端。握手失败可能在 SDK 仍为 INIT 时留下外部 TLS 流，SDK close 单独调用不足以释放；包装对象 Reset 同时销毁外部 parent。
- 完整响应要求 Connection close 时调用 SDK close；关闭失败则销毁客户端。下一次查询仍按原三秒间隔进行，必要时为同一结果地址重新初始化，不在当前请求中自动重发。
- device-start 不重发；读取结果允许重复查询。本地服务的 `device_result` 只读既有会话凭据，不消费一次性 token。凭据仍需满足原状态码、字符串长度要求才写入 NVS。

本地 `pairing_device_start` 返回 JSON 201，未绑定的 `pairing_device_result` 返回 404，绑定后返回 JSON 200；这些 handler 没有 force_close，gateway 沿 aiohttp 默认配置启动。但部署代理和真实 HTTP/TLS 保活行为尚未联网验证，不能据此保证任何部署都持续复用连接。

## 实际验证

实际命令和结果在本批 `evidence/pairing-http-session-20261112/`，来源核对记入 `final-audit.json`。未运行测试套件、模拟请求或服务，未实机配对。

| 实际执行 | 结果与边界 |
| --- | --- |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=alientek-atk-dnesp32s3-box2-wifi reconfigure build` | 最终退出 0，当前 BOX2 Wi-Fi 整板编译链接；应用 2236320 字节、模型 2203819 字节；267 项源码／配置摘要、24 份产物快照；镜像身份、checksum／hash、双 3 MiB OTA 分区核对命令全部退出 0。不是实机验证。 |
| 当前 `main.cpp` 使用真实 ML307 候选头文件、显式宏和 C++23 编译 | 退出 0，命令、对象摘要及未解析引用归档；不是正式蜂窝板型配置、整板链接或网络运行。 |

第一轮完整构建也退出 0，但随后补充握手失败时的 Reset 清理；第一轮日志与源码保存为 `initial-*`，当前通过记录仅引用重新构建后的最终源码／产物。

```text
python -B evidence/pairing-http-session-20261112/build_native.py
python -B evidence/pairing-http-session-20261112/collect_native.py
python -B evidence/pairing-http-session-20261112/compile_cellular_callsite.py
python -B evidence/pairing-http-session-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/pairing-http-session-20261112/finalize.py
```

BOX2 Wi-Fi 使用现有 SDK 5.5.6-dirty、Xtensa 14.2.0、隔离 stage 和外网保护构建。蜂窝仅将当前真实 main.cpp 用现有 BOX2 生成配置加 ML307 宏／C++23 编译，未建立正式 4G 整板配置、链接或运行证据；候选依赖继续引用上一批，没有安装或修改 SDK。SDK 既有启动名称 UND 限制继续保留，不称干净 SDK 通过。

## 剩余限制

保活减少正常轮询消耗 ID；服务主动关闭或断线仍需新 ID。六 ID 耗尽、reset_required、owner 替换和板级新会话恢复仍未实现，不能称长期蜂窝连接已完成。若 owner 已要求复位，重建客户端也不能绕过该要求。

本批未增加任何板型 profile，未改固定唤醒词差异、音频、自然插话、OTA、绑定顺序和手机热点查看绑定码入口。现有微雪 1.8 仍是唯一可用实物；其他板型实机验收不能由构建替代。没有 Git 写入、安装、刷机、OTA、上传或发布。

总表仍为 154 官方目录／188 变体，152 项有源码入口、36 项待源码；含历史入口共 153 个本地 profile。9 项研究仅追加本批证据，既有内容保留。账本为 1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，唯一当前通过项为本批 BOX2 Wi-Fi；188 项本轮实机均 not_run。
