# GC9D01N initialization data

Only the initialization command table in `../../t_circle_gc9d01n_init.h` is adapted from:
https://github.com/Xinyuan-LilyGO/T-Circle-S3/blob/5a6f28748f2fb753c25816bfe156f3ba0155627e/libraries/TFT_eSPI-2.5.43/TFT_Drivers/GC9D01N_Init.h

The same nested TFT_eSPI library supplies the accompanying unmodified `license.txt`:
https://github.com/Xinyuan-LilyGO/T-Circle-S3/blob/5a6f28748f2fb753c25816bfe156f3ba0155627e/libraries/TFT_eSPI-2.5.43/license.txt

The library states FreeBSD terms for its original code (Copyright 2023 Bodmer), and retains Adafruit notices. Preserve this entire license in source and binary distributions that contain the table. No Arduino or GPL board implementation is copied. The table groups each command with its data and delay; the final empty RAM-write command is omitted until the first pixel transfer, and its20ms delay is retained after display-on. Panel operations, touch polling and the audio adapter are local code; the project's overall license has not been changed.
