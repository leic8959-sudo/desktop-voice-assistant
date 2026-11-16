# M5Stack CoreS3 fixed ESP-BSP facts

Pin assignments and power-feature register facts originate from Espressif
ESP-BSP3.0.2 at commit `1bd663be5317bfc4457335011889eec5db33098d`:

https://github.com/espressif/esp-bsp/tree/1bd663be5317bfc4457335011889eec5db33098d/bsp/m5stack_core_s3

SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: Apache-2.0

The original license is preserved in LICENSE and the source/blob/hash manifest
is evidence/cores3-native-20261113/source-artifacts.json. This local implementation
uses the existing SDK, LVGL port, FT5x06, checked codec and ILI9341 parameter table;
it does not link the original full BSP or copy M5Unified/M5GFX implementation.

Changes: update/read back only owned AW9523/AXP2101 bits; preserve unowned rails,
USB/extension outputs and charger settings; keep codec enable across mute;
use the real AW88298 mute callback; brightness0 disables only DLDO1; finite
I2C calls and mutex waits; raw touch count/event validation and cancellation;
SD never formats on mount failure. SPI DC/MISO sharing relies on the exact
current SDK's existing callbacks, not an added SDK patch. Manufacturer sources
are comparison references only and are not imported or linked.
