"""Apply the QSPI flush fix and preserve brightness I/O errors in Waveshare BSP."""

import argparse
from pathlib import Path


def patch(project):
    path = Path(project) / "managed_components/waveshare__esp32_s3_touch_amoled_1_8/esp32_s3_touch_amoled_1_8.c"
    source = path.read_text(encoding="utf-8")
    old = "lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);"
    new = "lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);"
    if old in source:
        start = source.index("    const lvgl_port_display_rgb_cfg_t rgb_cfg = {")
        end = source.index(old, start)
        source = source[:start] + "    // CO5300 is a QSPI panel: wait for SPI color-transfer completion.\n    " + source[end:].replace(old, new, 1)
        print("Waveshare QSPI flush callback corrected.")
    elif new in source and "const lvgl_port_display_rgb_cfg_t rgb_cfg" not in source:
        print("Waveshare QSPI flush callback already corrected.")
    else:
        raise RuntimeError("Unexpected BSP source; inspect QSPI display registration before building")


    old_brightness = "    esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &param, 1);\n\n    return ESP_OK;"
    new_brightness = "    return esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &param, 1);"
    if old_brightness in source:
        source = source.replace(old_brightness, new_brightness, 1)
        print("Waveshare brightness I/O errors preserved.")
    elif new_brightness not in source:
        raise RuntimeError("Unexpected BSP brightness implementation; inspect before building")
    original = path.read_bytes()
    newline = "\r\n" if b"\r\n" in original else "\n"
    updated = source.replace("\n", newline).encode("utf-8")
    if original != updated:
        path.write_bytes(updated)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project")
    patch(parser.parse_args().project)
