# NV3023 board command data

Only the initialization data in `../../nv3023_board_init.h` is adapted from:
https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-cube-0.85tft-wifi/xingzhi-cube-0.85tft-wifi.cc

The fixed board file has no separate license header and its directory has no overriding license. Preserve the accompanying unmodified repository MIT LICENSE, including all copyright and permission notices, with source or corresponding binary distributions. This does not relicense board files that carry other individual notices or choose the project's overall license. Active commands are grouped into static C++ byte storage; comments are excluded and all effective data lengths and delays are preserved. The NV3023 driver remains a separate declared component (78/esp_lcd_nv3023 1.0.1, Apache-2.0), not vendored application code.
