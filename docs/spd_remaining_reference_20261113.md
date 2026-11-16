# SPD2010 剩余数据处理与首报告发布修正

微雪1.46上一批的“任何HDP剩余数据都取消首报告”是本地额外限制。重新核对固定厂家示例和组件实现后，本批移除了这项与参考行为不一致的拒绝条件：**合法首报告在剩余数据有界排空、完成状态为0x82且清中断成功后发布**。未知状态、读取／确认错误、长度／ID错误、超时和不支持报告仍取消当前操作。

这修正的是源码兼容性偏差，没有实现一种新推测的分片拼接协议，也没有证明所有控制器固件都可以如此处理。真实触摸、剩余数据含义、释放时序、校验及故障恢复仍需资料或对应实物确认。

## 新证据及适用范围

固定厂家仓库 `waveshareteam/ESP32-S3-Touch-LCD-1.46@fda89ff2ff32eb1bce561901225c6a36fa684237` 的Arduino与ESP-IDF示例均调用一次read_tp_hdp解析坐标，再在独立循环读取剩余数据，直到0x82后clear。Read_HDP_REMAIN_DATA只有本地临时缓冲，没有触摸报告参数，也不把剩余字节追加进坐标报告。

固定Espressif触摸2.1.0 `88152bff24d5fbc05ff2ffed3d5208c247711ded` 亦采用上述行为。另读取当前Espressif及ESP32_Display_Panel源作交叉核对，四份代码都符合“首报告只解析一次”的顺序。当前分支参考按Git blob／SHA256保存，不能将其自动视作固定发行版本。社区glue针对1.46B，仅供旁证，没有复制其方向、缓冲大小或轮询顺序到本板。

精确URL、读取命令、blob与摘要在 `evidence/spd-continuation-20261113/source-references.json`，逐项源码调用顺序在protocol-review.json。所有读取均为源码参考，没有安装组件或运行厂家代码。搜索没有找到足以定义通用HDP续包拼接格式的协议资料，因此没有声称“组包协议已解决”。

| 行为 | 本轮结果 |
| --- | --- |
| 首个HDP包 | 原5..64字节边界、4字节头及最多10个6字节点解析不变 |
| 剩余数据 | 原1..64字节单次、最多8次状态迭代、累计最多512字节、迭代间1秒检查不变；每IO100ms，非严格端到端1秒 |
| 发布时机 | 先排空到0x82，再检查清中断返回值；合法首报告允许发布，取消旧remaining_seen拒绝条件 |
| 未改变的拒绝 | 非法长度／ID、重复ID、不支持首报告、未知完成状态、超时、IO／清中断错误均不发布 |
| 消费者 | 初始与恢复释放门槛、多指／越界取消、原生坐标不变 |
| 仍待确认 | 真实剩余字节的固件含义、CRC、weight0、零长度释放、长空闲旧快照、IRQ／总线故障与实机标定 |

上一批在没有参考要求的情况下把剩余数据当作首报告不完整的证据，这个判断过强。本轮依据厂家实际实现修正，保留旧源码／记录和原说明为历史，不覆盖其当时的构建证据。按参考代码不拼接剩余数据，不等于从协议层证明剩余数据对所有固件都无影响。

## 实际改动与构建

main仅修改 `spd2010_touch.cpp`：移除remaining_seen变量和发布拒绝条件，更新注释说明参考依据；原解析／握手／限额／ACK／缓存代码及所有其它main文件摘要保持。registry仅更新1.46的实现说明，身份、引脚、容量、依赖／defaults／分区均未改。没有新增功能、调整阈值或扩大其它板的支持结论。

实际在已有1.46隔离stage同步当前输入，执行：

```text
python -B evidence/spd-continuation-20261113/review_sources.py
python -B evidence/spd-continuation-20261113/lcd146/build_cached.py
python -B evidence/spd-continuation-20261113/lcd146/collect_build.py
python -B evidence/spd-continuation-20261113/inspect_sdk_limits.py
```

真实IDF命令为 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-lcd-1.46 reconfigure build`，一次退出0；使用现有SDK5.5.6-dirty、Xtensa14.2.0、本地override、Python联网保护及2jobs，没有安装依赖。

应用2521440字节、模型2203819字节；299项当前输入及27份产物快照核对通过。实际readelf／nm／size、esptool镜像、二进制分区及SDK Git只读查询均退出0，项目assistant_lcd146、16MB镜像头、checksum／validation hash有效，双3MiB OTA及4MiB模型分区保留。ELF保留SPD触摸／屏幕、数字音频、AEC／完整唤醒及HTTP／WebSocket入口；这些检查没有运行控制器协议或模型推理。

四个既有SDK强制UND仍记录，未改SDK；前批其它镜像的静态启动审阅不自动证明本镜像上电通过。依赖报告重新执行后仍153本地／152官方、927条直接声明、105本地／104官方满足直接条件；数量未因本次源码修正变化。

最终归档命令：

```text
python -B evidence/spd-continuation-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/spd-continuation-20261113/finalize.py
git diff --check
git diff --no-index --check -- NUL firmware/assistant_espidf/main/spd2010_touch.cpp
```

当前官方源码入口152／188、36无源码、4项显式必需功能缺口不变；累计104个官方变体曾缓存构建通过。当前1项缓存通过／103项历史待重建／84项not_run，本轮只有1.46按当前输入重建，其它板不能继承新结果。188项本轮实机均not_run，触摸协议和声学未验收，全量目标仍未完成。

未运行独立测试套件、类型检查、服务、协议模拟／故障注入、I2C／触摸／声学操作；未安装依赖、Git写操作、刷机、OTA、上传或发布。
