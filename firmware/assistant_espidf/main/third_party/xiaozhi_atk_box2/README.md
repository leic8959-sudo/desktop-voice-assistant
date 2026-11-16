# ATK BOX2 Wi-Fi panel data provenance

`../../atk_box2_panel_commands.h` adapts the20 command/data transfers following
standardST7789 initialization in XiaoZhi v2.5.2 fixed commit
`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`:

`main/boards/alientek/atk-dnesp32s3-box2-wifi/atk_dnesp32s3_box2.cc`

Command order, literal data lengths (maximum14bytes) and values are retained.
The fixed tree lists no nested license/NOTICE for this board; its repository MIT
notice is preserved verbatim in `LICENSE`. The application's driver, power
manager, codec and button code are not vendored wholesale. This does not change
the project's overall license or establish actual panel-controller identity.

Native I80 IO uses the installed ESP-IDF implementation; ES8389 uses the existing
esp_codec_dev1.5.11 driver and its own license. The two commands writing MADCTL
and COLMOD remain in owning order before the driver applies no-swap/no-mirror.
Physical colors, timings, power rails, PA and audio remain unaccepted.
