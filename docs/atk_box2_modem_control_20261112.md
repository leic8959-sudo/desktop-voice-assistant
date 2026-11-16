# ATK BOX2模组使能控制接口

本批为已有BOX2板级驱动增加`atk_box2_set_modem_enable(bool)`，提供后续4G板型所需的EN_4G控制和回读。实际重新构建当前BOX2 Wi‑Fi目标退出0。**接口未接到启动调用链，没有执行供电切换，不是BOX2 4G完整适配或模组复位验收。**官方源码入口数量仍为152／188。

## 最小源码改动

只修改`firmware/assistant_espidf/main/atk_box2_board.cpp`及其头文件，新增一个接口，不改原init、按键或功放逻辑。调用前要求既有板级init已就绪、模组流量已停。新接口复用功放路径的同一个I²C设备和mutex，避免为TCA9555的同一端口引入第二个读改写owner。

固定4G源定义XIO_EN_4G为全局bit12，即P1.4；它初始化为高。本接口只操作这一位：先设置0x03输出锁存，再清0x05该位输入极性，最后清0x07该位配置以选择输出；每步复用现有masked更新与回读。随后读取0x01输入bank，核对P1.4是否与请求一致。读写或回读不符会返回实际错误并记录`modem_enable_unconfirmed`，不把错误吞成成功。

所有其他位沿现值保留，包括系统锁存、3V3A、PA、USB／VBUS、充电控制和SBU。普通BOX2 Wi‑Fi init仍保持之前“4G12未拥有、原状态保留”的行为，只有未来显式调用本接口才取得此位控制。接口自身不延时、不发送AT、不重启ESP或模组；每次I²C操作及锁获取沿用100ms超时，若发生部分失败硬件状态可能未知，不追加未经验证的断电补偿。

成功仅说明控制寄存器及扩展口引脚回读符合请求。它不读取模组VBAT，不证明电压已下降、放电时长或完整新会话；不得把它直接包装成始终成功的`prepare_clean_session`。同样没有实现可信时间回调、PDP启动或蜂窝客户端工厂调用。

## 电路资料核对范围

本批只读查询并保存四份仓库完整tree快照：

| 仓库 | 固定tree／commit | 本批能证明的范围 |
| --- | --- | --- |
| openedv/openedv-wiki | 2de4b1d8f1d83d574899305cf2e6384e27eddd65 | 已列板卡子模块包含开发板／BOX3相关资料；本快照未找到BOX2 4G电路 |
| openedv/ATK-ESP32-Board-Framework | 692cfa15df31a8af2ae3b58a8b0736e728bacb57 | README只列atk_dnesp32s3开发板；不能借其电路推断BOX2 |
| mlpre/esp32_box2 | 86ccd3de6cdb301b04c66efe066f4260f580cb4d | 第三方Wi‑Fi版驱动／说明，不提供所需4G放电关系证明 |
| mlpre/xiaozhi-esp32-box2 | e82e8a8af3a7c751b59a2e2fdd96e2a306059829 | 板型源码含4G／Wi‑Fi目录，所查tree未含相关电路PDF／SCH |

读取原PR954正文及评论，作者明确是外观相同的两块板，不能因Wi‑Fi可编译就继承为4G支持。没有向作者发消息。另核对旧厂家开发板tree中的`ATK_DNESP32S3 V1.2.pdf`属于开发板，不下载后冒充BOX2原理图。

上述为限定来源的搜索结果，不表示所有公开渠道均不存在电路图。7份实际读取的README／模块清单／第三方驱动／固定4G config和板类分别按tree Git blob及SHA256核对，清单见`evidence/atkbox2-4g-power-20261112/source-artifacts.json`。未安装这些仓库建议的依赖或执行其程序。

## 实际构建证据

```text
python -B evidence/atkbox2-4g-power-20261112/build_cached.py
python -B evidence/atkbox2-4g-power-20261112/collect_build.py
python -B evidence/atkbox2-4g-power-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/atkbox2-4g-power-20261112/close_research.py
```

全新stage为`I:/xy_board_builds_20261109/atkbox2_modem_control_20261112`。prepare检查既有SDK／组件后暂存，实际执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=alientek-atk-dnesp32s3-box2-wifi build`退出0。环境为既有SDK5.5.6-dirty、Xtensa14.2.0、本地组件override及Python外网保护；没有安装或下载构建依赖。

| 产物／核对 | 实际结果 |
| --- | --- |
| 应用身份／镜像 | assistant_atkbox2，2234976字节，checksum／hash有效 |
| 模型 | 2203819字节，原自定义唤醒模型配置保留 |
| 分区 | 双3MiB应用槽及模型分区，SDK容量检查／解码退出0 |
| 板级object | nm确认新增`atk_box2_set_modem_enable(bool)`已编译 |
| 最终ELF | Xtensa；板级init、PA、ES8389、AEC和原唤醒入口保留；新增接口因未调用被GC移除 |
| 来源／产物摘要 | 当前263项固件输入与stage一致，24份产物／配置／板级object快照 |

readelf／nm／size、esptool和分区解码的8个实际检查均退出0，命令数组、cwd、日志及摘要在commands.json。已有display_ui的LVGL9.6弃用警告保留，没有扩大到UI格式调整。没有运行测试套件、I²C错误注入、实际引脚回读、电源切换、音频或声学测试。

这次构建实际选择Wi‑Fi配置，未链接ML307依赖候选；不能验证蜂窝启动、TLS或4G流媒体。新增接口出现在object中却不在最终ELF，分别记录，不能把“声明存在”或“未调用路径被裁剪”当作蜂窝接入证明。

## 当前账本与后续

新增独立记录`cached_build_attempts_20261112_atkbox2_modem_control.json`，证明当前BOX2 Wi‑Fi源码完整缓存构建。Spot C5历史记录因全main摘要改变转stale；账本当前仍1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run，但当前通过目标已变为BOX2 Wi‑Fi。

除上述两文件，其他已有main、控制协议人工决定、固定词、绑定／NVS、OTA和native registry保持原文。仍152／188官方源码入口、153本地profile含1历史入口、36项待源码；188项本轮实机全部not_run。BOX2 4G还需对应修订的电路／放电或独立reset依据、可信时间、模组owner及正式客户端接入、整板构建和实机验收。未启动服务、Git写入、刷机、OTA、上传或发布。
