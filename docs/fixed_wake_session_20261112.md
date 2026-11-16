# 固定唤醒词声明与每台设备的会话校验

本批补齐 C3／C5／C6 固定词接入的服务端缺口。此前设备侧选择“你好小智”，但原生桥接会话仍只匹配“你好小燕子”，连接、媒体和会话结束提示也写死旧词。本地识别成功不能保证通过云端完整口令复核；整板编译通过也不能证明这条运行链路正确。

## 改动

固件只在 `send_hello` 增加 `wake_phrase` 字段：固定词分支声明“你好小智”，其他分支声明“你好，小燕子”。未改唤醒模型、阈值、音频采集、字节统计、AEC、自然插话、媒体暂停和 OTA 处理。

新增 `wake_phrase.py` 集中处理有限词表。握手只接受“你好小智”及原“你好小燕子”（允许原词的中文逗号显示形式），缺字段沿用旧词；空值、空串、其他类型和任意新词均不接受。原生入口在无效词时走关闭码 1003 的拒绝分支，不将不支持的词解释成无需复核。

`NativeDevice` 保存本连接声明；创建 `VoiceSession` 时复制为该会话的唤醒词。复核前缀、分段转录状态、重复完整词的纯唤醒判断及诊断匹配使用会话快照，不使用另一设备的声明。现有静态辅助方法保留旧词默认参数，浏览器和旧 Opus 入口沿原默认词。

连接、会话结束和媒体续播提示使用同一词表的显示文本。“你好小智”只是硬件支持的唤醒词，助手名称仍是小燕子。未知词不会进入提示文本。重复 hello 的词在修改设备能力前先经过同一解析；已创建的语音会话仍持自己的字符串快照。

Realtime 初次配置、记忆上下文刷新、Qwen Chat／DeepSeek 回退的模型系统提示也传入该会话的词。公共提示函数保留旧词默认值，仅将口令文本参数化；纯唤醒只回复“我在”和其他既有业务规则保持原文。没有将设备能力写入持久化全局配置。

## 验证范围

实际语法和来源审计记录于 `evidence/fixed-wake-session-20261112/source-structure-review.json`：使用 `ast.parse` 读取五份 Python 源码，未 import 或执行业务代码。人工决策注释和对应业务分支保留；其中 Realtime connect 仅增加会话词参数，公共提示函数仅增加参数和口令插值，其余含人工决定的函数逐字不变。固件除 `send_hello` 外逐字一致，保护的语音处理和两个人工决策未改。Dockerfile 使用 `COPY . .`，新模块不需修改打包清单，但本批没有构建或部署服务镜像。

本批不会把静态审计或固件编译写成服务器运行验收。桥接源码尚未重启部署，没有执行真实 WebSocket 握手、转录、双设备并发或声学验收。新固定词固件需要配套桥接更新；未声明词的旧固定词固件仍会沿用旧默认，不能据此声称兼容链路已现场验证。

实际构建命令和产物来源在本批 evidence，最终汇总 `final-audit.json`：

| 实际执行 | 结果与边界 |
| --- | --- |
| `idf.py -DIDF_TARGET=esp32c5 -DASSISTANT_BOARD=espressif-esp-sensairshuttle reconfigure build` | 退出 0，真实固定词分支；应用 2492672 字节、模型 125951 字节；267 项当前输入、23 份产物快照 |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=alientek-atk-dnesp32s3-box2-wifi reconfigure build` | 退出 0，真实默认词分支；应用 2236352 字节、模型 2203819 字节；267 项当前输入、24 份产物快照 |
| 两板镜像／ELF／分区核对 | readelf／nm／size、esptool 和分区解码命令全部退出 0；项目身份、checksum／hash、双 3 MiB OTA 分区检查通过；不是运行验收 |

使用前批已隔离的真实两板工程，逐项同步并核对当前 main 和根配置输入，生成的本地依赖 override 清单另外保存。未安装或下载构建依赖。SDK 仍为现有 5.5.6-dirty：实际 nm 核对 Sensair 三个、BOX2 四个既有强制 UND 名，来源见本批 `sdk-link-limitations.json`；未修改 SDK，不称零 UND 或可启动验收。

```text
python -B evidence/fixed-wake-session-20261112/build_sensair.py
python -B evidence/fixed-wake-session-20261112/build_native.py
python -B evidence/fixed-wake-session-20261112/collect_sensair.py
python -B evidence/fixed-wake-session-20261112/collect_native.py
python -B evidence/fixed-wake-session-20261112/review_sources.py
python -B evidence/fixed-wake-session-20261112/inspect_sdk_limits.py
python -B evidence/fixed-wake-session-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/fixed-wake-session-20261112/finalize.py
```

没有运行测试套件、类型检查或长期服务；没有安装依赖、Git 写入、刷机、OTA、上传或发布。

24 个 C3／C5／C6 变体的研究记录仅追加端云口令来源和验证边界，未登记新的 C3／C6 板型入口。官方仍为 152／188 有源码入口、含历史入口共 153 本地 profile、36 项待源码；当前 2 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／116 not_run，188 项本轮实机均 not_run。Opus 仅完成另行前置调查，产品取舍待牛牛回复，没有改变本批 PCM 格式或通过计数。
