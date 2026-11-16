# Waveshare Audio Board JD9853 data provenance

`../../jd9853_panel_commands.h` adapts the32 active default commands and delays
from XiaoZhi v2.5.2 fixed commit `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`:

`main/boards/waveshare/esp32-s3-audio-board/esp_lcd_jd9853.c`

The source carries Espressif2022-2023 Apache-2.0 notices; the data header retains
them and `LICENSE` contains the Apache2.0 terms already preserved for the native
Jiuchuan adapter. No nested NOTICE/license is listed for this board in the fixed
tree. This does not change the project's overall license.

Active command/data/delay order is retained, including duplicatedDF writes,
the32-byte gamma payload, COLMOD05, address windows and the120ms table delay.
The owning initializer first sends sleep-out/100ms, MADCTL00/COLMOD55 before this
table; the native lifecycle retains that prefix. Commented alternative tables
are not enabled. Literal array bytes beyond an explicitzero length are not sent.

`../../jd9853_panel.cpp` is a native lifecycle around these data with checked
transfer results, explicitRGB565-only config and draw bounds. It retains the
owning software reset/20ms, uses a cachedMADCTL committed after successfulwrites,
and rejects draw windows outside the physical240x320RAM after offsets. The local
board uses the finalLVGL rotation(swapXY/mirrorY) and gap0/34. This does not assert
actual panelidentity, colors, orientation or hardware acceptance.
