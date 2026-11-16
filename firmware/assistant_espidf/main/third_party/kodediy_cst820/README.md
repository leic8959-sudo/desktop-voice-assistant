# CST820 protocol reference

SPDX-FileCopyrightText: 2025 KODE DIY, SL
SPDX-License-Identifier: Apache-2.0

Register/reset facts are adapted from kodediy/esp_lcd_touch_cst820 commit
`340906c82fdee8f218c39bc8f59084f356e515db` (manifest1.0.1), within the historical
Waveshare BSP dependency range^1.0.0:

https://github.com/kodediy/esp_lcd_touch_cst820/tree/340906c82fdee8f218c39bc8f59084f356e515db

The complete original license is preserved in LICENSE. Source/blob/hash records
are in evidence/amoled132-native-20261113/cst820-sources.json. The driver is only
a reference, not installed, copied wholesale, or linked.

Local board glue reads the original five-byte register02 packet and preserves
its raw count; multi-contact/error/out-of-bounds cancels the current press.
No event-bit meaning is inferred from a different touch controller. Reset is
low200ms/high200ms; registerA7 is logged, not authenticated. No global mutable
coordinate offset or count-clamping/duplicate-coordinate behavior is imported.
