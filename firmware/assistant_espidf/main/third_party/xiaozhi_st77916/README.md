# ST77916 board initialization data

Only the two command tables in `../../st77916_board_init.h` are adapted from:
https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc

The version2 table was compared to the original1.85 and1.85C fixed tables and has identical command/data/delay entries. The original and C version1 path continues to use the pinned Espressif driver's default table; the B version1 table differs and is kept explicitly. The fixed board files have no separate license header or nested license in their source directories; preserve the repository's supplied MIT copyright and full permission notice from the accompanying unmodified LICENSE. This does not relicense other board files with their own notices, or choose the project's overall license.

Only active entries are included; commented-out register writes are excluded. The conversion replaces C compound literals with static C++ byte storage and retains every command, argument length and delay. No upstream application logic, unsafe SPI IO replacement or uninitialized ID-selection logic is copied. Espressif's driver remains a separate Apache-2.0 component dependency. Include this folder when distributing the adapted source or corresponding firmware binaries.
