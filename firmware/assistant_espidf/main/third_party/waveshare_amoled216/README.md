# Waveshare AMOLED 2.16 board and CST9217 contract

Board pin facts and the fifteen CO5300 initialization commands originate from
Waveshare BSP2.0.1 at commit `bfeb6e6d5737178cdde78b630c8118074da0a657`:

https://github.com/waveshareteam/Waveshare-ESP32-components/tree/bfeb6e6d5737178cdde78b630c8118074da0a657/bsp/esp32_s3_touch_amoled_2_16

The CST9217 register and report contract comes from the same commit:

https://github.com/waveshareteam/Waveshare-ESP32-components/tree/bfeb6e6d5737178cdde78b630c8118074da0a657/display/touch/esp_lcd_touch_cst9217

The sources use Apache License 2.0, preserved in LICENSE. The CST9217 source
contains these attribution notices:

SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
SPDX-FileCopyrightText: 2025 Waveshare

Changes: persistent C++ command payloads, initial panel brightness zero,
bounded SPI buffers, existing esp_lvgl_port and checked codec paths, finite
I2C transactions, original raw contact-count validation, press cancellation,
pixel reflection around479, and hardware reset after information reads.
Original information-mode two-transaction command sequence is retained; no
CST9220 protocol detector or unverified chip-ID allowlist was introduced.

Only the command data and protocol facts are adapted. The original full BSP,
adapter, application, and touch-driver implementation are not linked. Original
files, Git blob IDs and SHA256 records are preserved under
evidence/amoled216-native-20261113/source-artifacts.json.

The same local layer also supports the separate466x466 CO5300 panels from:

https://github.com/waveshareteam/Waveshare-ESP32-components/tree/bfeb6e6d5737178cdde78b630c8118074da0a657/bsp/esp32_s3_touch_amoled_1_75

https://github.com/waveshareteam/Waveshare-ESP32-components/tree/010c5fb39ea7f0e996efd1f8e162cc70e569e57f/bsp/esp32_s3_touch_amoled_1_75c

Their separate fourteen-command sequence, column gap6, touch mirrorX/mirrorY,
and reset/audio/SD revision facts are preserved independently of the2.16 table.
Only startup brightnessFF is changed to00 in either table. The shared filenames
retain their original2.16 names; they do not identify the selected hardware.
The complete original references and license hashes for both additional boards
are recorded in evidence/amoled175-family-20261113/source-artifacts.json.
