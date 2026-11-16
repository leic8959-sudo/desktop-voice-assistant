# Jiuchuan 240x296 panel data provenance

`../../jiuchuan_panel_commands.h` adapts the48 active command/data transfers and
80ms/10ms delays in the initializer from XiaoZhi v2.5.2 fixed commit
`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`:

`main/boards/jiuchuan-s3/esp_lcd_panel_gc9301.c`

The original file declares Apache-2.0 and carries Espressif2021-2024 attribution
and a second `2024 Your Name` placeholder. Both notices are retained on the data
header; `LICENSE` supplies the Apache2.0 terms from the installed ESP-IDF license.
The fixed upstream tree has no nested NOTICE for this board. This does not change
the project's overall license or establish any additional panel provenance.

Despite the filename, the owning factory/implementation is named GC9309NA. The
active initializer programs240x296, COLMOD0x05, MADCTL0x48 and B4=0x80. Commented
commands are excluded; command order, literal lengths and active delays are
preserved. The source's misleading ST7789 header text, duplicate includes and
no-op mirror/swap functions are not imported.

`../../jiuchuan_panel.cpp` provides a local ESP-IDF panel lifecycle around these
data. It checks transfers/draw bounds, commits cached MADCTL only after successful
writes and implements mirror/swap bits instead of silently returning success.
The fixed board uses BGR, mirrorX=true, no swap and no physical reset. Reset keeps
the owning120ms power-on wait; no undocumented reset command is synthesized.
Actual controller identity, initialization, byte order and rotation still need
hardware acceptance. This source does not imply support for arbitrary GC9301 or
GC9309NA modules.
