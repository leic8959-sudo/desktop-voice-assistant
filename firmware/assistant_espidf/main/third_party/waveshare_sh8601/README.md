# SH8601 RGB565 adapter and2.06 board commands

SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: Apache-2.0

Protocol/reset facts and2.06 command data originate from Waveshare component
commit `9daffb7168b3c093a330da700988a22092294eef`:

https://github.com/waveshareteam/Waveshare-ESP32-components/tree/9daffb7168b3c093a330da700988a22092294eef/display/lcd/esp_lcd_sh8601

https://github.com/waveshareteam/Waveshare-ESP32-components/tree/9daffb7168b3c093a330da700988a22092294eef/bsp/esp32_s3_touch_amoled_2_06

Original license is preserved in LICENSE. Fixed sources/blob/hash records are
in evidence/sh8601-protocol-20261113/source-artifacts.json. The original full
BSP/driver is not installed or linked.

Changes: borrowed rawIO bridge02-parameter/32-color with SDK DCS RGB565 drawing;
bounded geometry/window/payload and transfer return propagation; SH reset
10ms+150ms or software80ms; no unsupportedYmirror/XYswap. Caller must join/drain
before deletion; rawIO is borrowed, panel user_data reserved. Initial panel
brightness51FF changes to00; other2.06 command bytes/order/length/delay remain.
No SDK ST7789 RAMCTRL initialization is invoked. Board uses existing LVGL,
FT5x06, checked ES7210/ES8311, SD and PMIC code; hardware unaccepted.

The1.32 variant's separate eleven-command table is from historical Waveshare
commit22733e088a533378680a03e398279b2f7ab09a73, not its later CO5300 registry
variant. Its466x466/gap6, singlecodec/noSD/8MB and dedicated pin facts remain
separate from2.06. Only startup brightness51FF changes to00 in that table.

Original1.8 uses its own nine-command table from Waveshare BSP1.1.4 at
c77caf968fa6b11f3b6a416c853c578d012a8cea:368x448/gap0/TCA9554P0-P2
reset/FT5x06/singleES8311. Only startup brightness51FF changes to00. This
entry is separate from the later CO5300 legacy/v2 board profiles.
