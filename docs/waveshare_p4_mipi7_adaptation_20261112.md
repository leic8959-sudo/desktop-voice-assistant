# 微雪P4／P4X WiFi6 Touch LCD 7寸 ILI9881C源码适配（2026-11-12）

本批新增 `waveshare-esp32-p4-wifi6-touch-lcd-7` 与 `waveshare-esp32-p4x-wifi6-touch-lcd-7` 两个独立源码候选。两个目标缺少 `espressif/esp_lcd_ili9881c 1.1.0`，尚未目标编译或实机验收。

上游固定配置为720×1280 RGB565、双DSI 1000Mbps、DPI80MHz，时序H50/239/33、V30/20/2，复位27、背光26，GT911在I2C1 SDA7/SCL8；ILI9881C初始化命令直接提取固定 `lcd_init_cmds.h`。音频复用双ES7210＋ES8311 TDM4／MIC1／24kHz，PA53和软件AEC沿公共实现。P4旧修订最低1.0、P4X最低3.0，使用独立应用身份和库选择。

联网复用P4 C6 Hosted SDIO和现有HTTP绑定、WebSocket媒体、OTA路径。相机、SD、电池、蓝牙和原厂手势未接入；C6固件、SDIO线、触摸坐标、时钟、背光及声学效果需实物验收。19份上游、组件和本地SDK／模型参考摘要见 `evidence/waveshare-p4-mipi7-source-20261112/source-artifacts.json`。

两个真实预检均退出1：

```text
Required component is not installed: espressif/esp_lcd_ili9881c; nothing was staged
```

没有P4暂存目录、应用编译、镜像或刷机。旧立创FT5x06双codec和RLCD4.2缓存回归最终均退出0；各210个main文件集合、218项输入摘要和23份产物已归档，回归排除了ILI9881C专用分支。

当前125个官方变体有源码入口、63个待源码，全部188项实机验收未完成。下一步需要准备ILI9881C、GT911、Hosted、Remote及传递依赖后再进行P4目标构建。


后续源码复核（ST7701批次）：该未编译候选原命令表错误带入8／10.1寸条件分支，已按固定7寸分支单独提取196条命令，改为静态字节数组；背光26已核为高有效，板型配置改回与profile一致的16MB，清除7B遗留32MB和重复修订选择。精确ILI9881C1.1.0实际实现disp_on_off，早先“无该操作”的说明不准确；熄屏仍使用背光。新增P4统一修订／模型检查和关闭PPA要求，显示失败返回关闭PA。更正后的分支仍未目标编译，旧S3通过不证明这些路径。详见[waveshare_p4_st7701_adaptation_20261112.md](waveshare_p4_st7701_adaptation_20261112.md)。


后续HX8394批次：该面板已迁入共享 `waveshare_p4_mipi_display.cpp`／`waveshare_p4_mipi_board.h`，原重复显示及板级文件在新批before快照保留后移除；本记录描述原批次，不证明共享P4目标已编译。参数和vendor字节的静态比较、最新回归及范围见[公共MIPI与5寸记录](waveshare_p4_hx8394_adaptation_20261112.md)。
