# ESP32-S3-BOX-3 fixed board contracts

Board pin facts, the eleven ILI9341 vendor parameter rows, and TT21100 report
fields are adapted from Espressif ESP-BSP at commit
`c9a0f385cd1bf86e1bd07f14dc2cb77b86f20a9a`:

https://github.com/espressif/esp-bsp/tree/c9a0f385cd1bf86e1bd07f14dc2cb77b86f20a9a/bsp/esp-box-3

https://github.com/espressif/esp-bsp/tree/c9a0f385cd1bf86e1bd07f14dc2cb77b86f20a9a/components/lcd_touch/esp_lcd_touch_tt21100

SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: Apache-2.0

The complete Apache License is preserved in LICENSE. Original files, Git blob
IDs and SHA256 records are in evidence/box3-native-20261113/source-artifacts.json.

Changes: local LVGL/checked codec glue, finite I2C operations, explicit errors,
raw contact-count/report length/tip validation and press cancellation. TT21100
button reports are consumed without mapping their auxiliary keys. Legacy BSP
ILI9341 length markers0x80/0xff are corrected to parameterless sleep-out/display-on
and an omitted end marker; an explicit120ms sleep-out delay is added. No read of
128 or255 bytes from a one-byte command payload is performed. Real SDK ST7789
DCS operations are reused; the original full BSP and its external drivers are
not linked. GT911 uses the existing local polling implementation and its own
third_party/gt911 notice. This is not physical hardware acceptance.

The shared board layer also handles the original ESP-BOX and ESP-BOX-Lite.
Their pin/capability facts are recorded from ESP-BSP commit
`342492186688ddb9469387528912d5312ab075d4`. The selected original BOX/Lite panel
commands come from XiaoZhi v2.5.2 at
`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`; their first eleven parameter rows
match the BOX3 table above. No XiaoZhi application implementation is imported.
The original BOX/Lite ESP-BSP ST7789 paths and the selected XiaoZhi ILI9341 path
are distinct source implementations, not interchangeable hardware evidence.
Original reset polarity, WS/backlight pins, Lite backlight inversion, no-SD
capabilities and no-touch Lite path are preserved by compile-time selection.
