#include "headless_board.h"

namespace {
i2c_master_bus_handle_t g_codec_bus;
}

esp_err_t bsp_i2c_init() {
    if (g_codec_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
#if defined(ASSISTANT_BOARD_S31_CORE)
    bus.sda_io_num = GPIO_NUM_51;
    bus.scl_io_num = GPIO_NUM_50;
#elif defined(ASSISTANT_BOARD_ATOM_M12_BASE)
    bus.sda_io_num = GPIO_NUM_38;
    bus.scl_io_num = GPIO_NUM_39;
#else
    bus.sda_io_num = GPIO_NUM_45;
    bus.scl_io_num = GPIO_NUM_0;
#endif
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_codec_bus);
}

i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_codec_bus; }
