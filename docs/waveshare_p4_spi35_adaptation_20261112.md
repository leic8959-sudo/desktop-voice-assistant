# 微雪P4／P4X WiFi6 Touch LCD 3.5源码适配（2026-11-12）

本批新增3.5寸SPI屏的两个修订源码候选：`waveshare-esp32-p4-wifi6-touch-lcd-3.5`／`waveshare-esp32-p4x-wifi6-touch-lcd-3.5`。**均缺ST7796目标依赖，未目标编译或实机验收。**这组使用SPI，与同系列MIPI型号不同，未把尺寸名字当作驱动兼容性依据。

## 固定硬件映射

上游基线为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。本批核对实际初始化类、配置、ST7796精确包、已安装FT5x06和SDK两种P4修订的管脚表，共29份参考，SHA256及来源见 `evidence/waveshare-p4-spi35-source-20261112/source-artifacts.json`。参考包没有安装或参与链接。

| 能力 | 源码候选配置 |
| --- | --- |
| 屏幕 | ST7796、320×480、SPI2、mode3、80MHz、BGR、反色、mirrorX、零偏移 |
| LCD管脚 | CLK21、MOSI20、CS23、DC26、RST27、BL28 |
| 刷新／背光 | 20行内部DMA条带；PWM初始化占空比0，面板成功后沿现有亮度路径开启 |
| 触摸 | FT5x06，地址0x38；I2C1 SDA7／SCL8、400kHz、RST29、IRQ50；swapXY＋mirrorX |
| 音频 | 单ES8311、I2S0、MCLK13／BCLK12／WS10／DIN11／DOUT9／PA53，24kHz输入输出 |
| 按键 | BOOT35；启动按住时必须先松手；随后复用按住说话／卡片交互 |
| 网络 | C6、Hosted SDIO slot1／四线，采用精确组件P4默认14–19及复位54；物理连线与固件仍未验收 |

配置头虽然有ES7210和参考通道宏，实际 `GetAudioCodec()` 创建的是 `Es8311AudioCodec`。因此本批复用项目已有一个BOTH句柄的单ES8311路径和PCM软件AEC，不创建双编解码器或声称已接硬件参考。I2C声明与实际总线均为1，避免端口号与bus_handle不一致。

显示、触摸和音频共享既有SPI／单编解码器实现，新增的分支按该板引脚和方向选择。保留触摸越界／失败取消逻辑，FT5x06缺失会使初始化失败，不伪装成可选触摸已经验收。SDK管脚表显示USB Serial JTAG在24／25，与SPI20／21分开；LCD占用26／27，本应用未初始化USB OTG。

旧P4入口最低修订1.0、P4X最低3.0，应用名分别为 `assistant_ws_p435`／`assistant_ws_p4x35`，共享此前P4端口的mn7、公共→芯片→板型配置顺序、C6 Remote网络和独立镜像身份。保留小燕子绑定和唤醒门控，没有照搬原类启动时直接开麦或正常状态BOOT重启的行为。

相机、SD缓存、蓝牙、电池以及原厂专属手势仍未实现；不会因头文件宏存在就宣称整板功能完成。其他微雪P4 MIPI屏、Nano和M5型号仍按各自驱动／时序／电源继续排队。

## 依赖与真实预检

单板manifest明确：SDK `>=5.5.6,<5.6.0`、ST7796 `1.4.0`、FT5x06 `1.1.1`、Hosted `2.12.11`、Remote `1.6.4`，其余复用现有固定公共依赖。ST7796参考包声明支持P4；这不等于已经在本项目编译。

两项真实命令均退出1：

```text
python -B tools/prepare_cached_firmware_build.py --profile waveshare-esp32-p4-wifi6-touch-lcd-3.5 --destination I:/xy_board_builds_20261109/ws_p435_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
python -B tools/prepare_cached_firmware_build.py --profile waveshare-esp32-p4x-wifi6-touch-lcd-3.5 --destination I:/xy_board_builds_20261109/ws_p4x35_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

结果均为 `Required component is not installed: espressif/esp_lcd_st7796; nothing was staged`。两个目标目录均未创建，无P4编译、链接或应用镜像；Hosted／Remote及完整传递依赖仍需准备。没有安装依赖或SDK、拉取Docker镜像、刷写P4或C6。

此前120项直接SDK／缓存预检的profile对象、直接manifest、准备工具、SDK基础版本与配置源、组件集合和组件manifest摘要均核对未变，本批保留原执行时间并复用这些结果，再加入上述两项实际检查。当前122个入口的汇总为：官方121项中64直接预检通过、44首缺组件、11首缺BSP、1精确版本不符、1 SDK不符；历史入口另1通过。**这是2项新执行＋120项经输入一致性核对后的复用，并非本轮新跑122项，也不是固件构建结果。**证明与固件侧索引分别为 `preflight-reuse-proof.json` 和 `native_dependency_preflight_20261112_ws_p435_all_profiles.json`。

## 已授权的旧板回归

实际最终缓存命令均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=bread-compact-wifi reconfigure build
```

| 回归目标 | 应用身份 | 应用字节／3MB |
| --- | --- | --- |
| 旧立创FT5x06＋双codec | assistant_lichuang | 2,263,824 |
| 旧OLED32数字音频 | assistant_oled32 | 2,157,056 |

使用既有SDK5.5.6-dirty、17份本地组件override、offline_guard和构建并发度2。两目标model均2,203,819字节／4MB。各205个main文件集合、213项输入摘要与最终工作区匹配，23份产物归档；`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0，独立应用名／16MB／校验／双OTA／NVS／model布局有效。

旧立创FT5x06、PCA9557、ES8311／ES7210仍在选中源码及ELF中，OLED数字音频仍链接；编译定义和镜像排除P4 3.5专用分支。因此这些回归不证明P4 GPIO／80MHz／FT方向／单codec／C6网络／mn7／修订选择已经编译或运行。最终源码加入了BOOT35松手门控后，已重新同步并完成最终回归。

`git diff --check`退出0；9个保护文件（含控制协议人工决策、主启动、OTA、唤醒、网络配网、旧双codec实现和桥接端）保持原摘要。没有运行测试套件、类型检查、长期服务或Git写操作，没有刷机、OTA、上传或发布。

## 当前范围与未完成项

121个官方变体有源码入口，另1个历史入口，共122本地profile；67个官方变体待源码。P4族27项中4项有源码候选、23项仍待实现。累计64个官方变体曾缓存构建通过；当前2 passed、62 stale、6历史failed、118 not_run，其中51项已有源码。全部188项本轮实机not_run，现有微雪1.8不能替代3.5寸P4验收。

仍须完成：精确依赖解析及P4目标构建、芯片修订与镜像容量、实际C6连线／固件、SPI信号、显示角落与触摸、PA／麦克风／时钟、mn7短语和AEC、网络重连、媒体与同板OTA。源码候选不等于整板已适配。命令、来源、镜像／ELF、摘要与最终diff见 `evidence/waveshare-p4-spi35-source-20261112/`。
