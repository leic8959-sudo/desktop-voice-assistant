"""Apply a reviewed SH8601/CO5300 QSPI flush fix and preserve brightness errors."""

import argparse
import re
from pathlib import Path


def patch(project, panel="co5300", component="esp32_s3_touch_amoled_1_8", component_directory=None):
    if panel not in {"co5300", "sh8601"}:
        raise ValueError("Unsupported QSPI panel")
    if component not in {"esp32_s3_touch_amoled_1_8", "esp32_s3_touch_amoled_1_32", "esp32_s3_touch_amoled_2_06", "esp32_s3_touch_amoled_2_16"}:
        raise ValueError("Unreviewed QSPI BSP")
    directory = Path(component_directory) if component_directory else Path(project) / "managed_components" / ("waveshare__" + component)
    path = directory / (component + ".c")
    source = path.read_text(encoding="utf-8")
    if f'#include "esp_lcd_{panel}.h"' not in source:
        raise RuntimeError("BSP panel driver does not match the selected hardware profile")
    old = "lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);"
    new = "lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);"
    if "return lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);" in source:
        old = "return lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);"
        new = "return lvgl_port_add_disp(&disp_cfg);"
    if old in source:
        start = source.index("    const lvgl_port_display_rgb_cfg_t rgb_cfg = {")
        end = source.index(old, start)
        source = source[:start] + "    // QSPI panel: wait for SPI color-transfer completion.\n    " + source[end:].replace(old, new, 1)
        print("Waveshare QSPI flush callback corrected.")
    elif new in source and "const lvgl_port_display_rgb_cfg_t rgb_cfg" not in source:
        print("Waveshare QSPI flush callback already corrected.")
    elif component == "esp32_s3_touch_amoled_2_16" and "esp_lv_adapter_register_display(&disp_cfg)" in source:
        print("Waveshare QSPI display uses its reviewed LVGL adapter path.")
    else:
        raise RuntimeError("Unexpected BSP source; inspect QSPI display registration before building")


    old_brightness = "    esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &param, 1);\n\n    return ESP_OK;"
    new_brightness = "    return esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &param, 1);"
    if old_brightness in source:
        source = source.replace(old_brightness, new_brightness, 1)
        print("Waveshare brightness I/O errors preserved.")
    elif new_brightness not in source:
        raise RuntimeError("Unexpected BSP brightness implementation; inspect before building")
    if component == "esp32_s3_touch_amoled_1_32":
        old_config = "bsp_display_config_t disp_config = {0};"
        new_config = "bsp_display_config_t disp_config = {.max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * BSP_LCD_BITS_PER_PIXEL / 8};"
        if old_config in source:
            source = source.replace(old_config, new_config, 1)
        elif new_config not in source:
            raise RuntimeError("Unexpected 1.32 display transfer capacity")
    if component in {"esp32_s3_touch_amoled_1_32", "esp32_s3_touch_amoled_2_06"}:
        # Sparse point-cloud redraw needs a partial buffer, with SPI completion
        # notifications rather than a full-refresh/direct-mode RGB contract.
        source = re.sub(r"#if CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH\s*\.full_refresh = 1,\s*#elif CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE\s*\.direct_mode = 1,\s*#endif", ".full_refresh = false,\n            .direct_mode = false,", source)
    original = path.read_bytes()
    newline = "\r\n" if b"\r\n" in original else "\n"
    updated = source.replace("\n", newline).encode("utf-8")
    if original != updated:
        path.write_bytes(updated)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project")
    parser.add_argument("--panel", choices=("co5300", "sh8601"), default="co5300")
    parser.add_argument("--component", default="esp32_s3_touch_amoled_1_8")
    parser.add_argument("--component-directory", type=Path, help="Existing isolated local override, never downloaded by this tool")
    args = parser.parse_args()
    patch(args.project, args.panel, args.component, args.component_directory)
