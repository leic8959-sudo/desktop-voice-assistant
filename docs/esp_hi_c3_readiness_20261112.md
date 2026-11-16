# ESP-Hi C3适配前置核对

ESP-Hi是带160×80彩屏的C3机器狗。此前将它当作无屏板的前提错误，本批已纠正研究记录。它仍为inventory_only，不新增native profile，不改变“全部板型”的范围，也不以纯按键或单应用固件替代完整语音、媒体、认证网络和OTA要求。

已读取10份固定小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`文件并逐份核对Git blob：8份ESP-Hi目录文件、C3 defaults及4MB分区表；另归档本机SDK和当前项目源码参考，共21项摘要。没有目标编译或实机操作。

| 模块 | 固定源码事实 | 适配含义 |
| --- | --- | --- |
| 麦克风 | ADC1 channel2，16kHz、12bit、12dB、TYPE2 | 不得沿用Sensair的channel5；需内部RAM和ADC同任务生命周期实现 |
| 扬声器 | I2S0 PDM，24kHz，正6／反7／PA3，四种scale均MUL_4，up_sample_fs=441 | 与Sensair的480不同；441是源板配置，尚无波形／时钟验收，不能替换成480后称已适配 |
| 负端复用 | 使用GPIO_PIN_MUX_REG[pdm_speak_n] | ESP-Hi没有Sensair源文件中硬编码GPIO10的问题，不把别板缺陷套在此板 |
| 显示 | SPI2 mode0 40MHz，MOSI4／CLK5／DC10，CS及RST为NC，160×80；ILI9341公开API传18条ST7735命名命令 | config标ST7789，不能据名称确定真实芯片；需要复核可视窗口和初始化 |
| 实际显示设置 | 软件reset后再等120ms；init后invert=false、gap0,24、mirrorY／swapXY | 覆盖config中的invert=true／gap0,0，应按实际执行链记录；不直接复制config值 |
| 输入 | BOOT9，移动唤醒0，音频唤醒1 | GPIO1源类处理函数为空，不能把其名称当作已实现语音输入 |
| 舵机 | FL21、FR19、BL20、BR18；仅CONFIG_ESP_CONSOLE_NONE时初始化 | C3 USB DM18／DP19及UART0 RX20／TX21均重叠；config同时列USB console和NONE，必须明确最终选择并核对身体接线 |
| LED／动作 | 源类硬编码WS2812在GPIO8，4颗；config内BUILTIN_LED为NC | 不把NC误写成没有LED；身体动作、WebUI和舵机需独立移植及实物验收，本批不驱动 |

固定词方向沿牛牛已确认的“你好小智”／wn9s_nihaoxiaozhi，但模型文件可用不等于完整C3引擎已可运行。

当前全功能分支的五项载荷合计**至少533,376字节**：唤醒缓存64,000、前缀缓存128,000、401×640发送数据256,640、32×2048播放数据65,536、插话预留19,200。这里只算载荷，尚未计结构体字段／对齐、静态区、任务栈、ADC／PDM DMA、WakeNet／AFE／AEC、LVGL、WiFi、TLS。现有实现还明确向PSRAM分配AEC缓冲及任务栈，不能仅打开C3白名单。

现有internal-RAM分支将manual_audio_only设为true、关闭本地唤醒／AFE／AEC，不符合这次固定唤醒词授权的目标。下一步需要有界的内部RAM自动唤醒链路及队列预算，同时保留失败回执和自然插话要求；不能通过缩小目标或不报告丢帧来掩盖资源问题。533,376是源代码算术下限，不是运行峰值或内存分配失败实测。

官方4MB分区实际是factory应用0x10000–0x300000及1MiB assets，虽然有otadata，也没有ota_0／ota_1，不能据此声称支持双OTA。本地固定模型包125,951字节是Flash产物，不能当作模型运行RAM。若仅做4MB总量算术、预留128KiB系统区和128KiB模型，两份应用各最多1,966,080字节，且尚无其他存储；这是预算上界，不是已选分区方案。C5的1.55MB镜像无法证明C3应用也能装入。

机器证据在`evidence/esp-hi-c3-source-20261112/`：source-artifacts、resource-review及panel-command-review。没有安装、编译、推理、舵机／设备写入、Git写操作或发布。当前仍152／188官方源码入口，36项待源码；ESP-Hi继续待内部RAM、显示／动作／音频移植、目标构建及实机验收。
