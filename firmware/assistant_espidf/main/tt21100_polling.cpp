// TT21100 report facts from Espressif ESP-BSP, Apache-2.0.
// Fixed sources and changes: third_party/espressif_box3/README.md.
#include "tt21100_polling.h"

namespace {
uint16_t word(const uint8_t *bytes) { return bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8); }
}

esp_err_t Tt21100Polling::Init(i2c_master_bus_handle_t bus) {
    if (!bus || device_) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7; config.device_address = 0x24; config.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(bus, &config, &device_);
}

esp_err_t Tt21100Polling::Read(Tt21100Frame *frame) {
    if (!frame) return ESP_ERR_INVALID_ARG;
    *frame = {};
    if (!device_) return ESP_ERR_INVALID_STATE;
    uint8_t length_bytes[2] = {};
    esp_err_t result = i2c_master_receive(device_, length_bytes, sizeof(length_bytes), 100);
    if (result != ESP_OK) { valid_ = false; return result; }
    const uint16_t length = word(length_bytes);
    uint8_t report[7 + 5 * 10] = {};
    // A reset/empty/unbounded report is not evidence of a valid release.
    if (length < 7 || length > sizeof(report)) { valid_ = false; return ESP_ERR_INVALID_RESPONSE; }
    result = i2c_master_receive(device_, report, length, 100);
    if (result != ESP_OK) { valid_ = false; return result; }
    if (word(report) != length) { valid_ = false; return ESP_ERR_INVALID_RESPONSE; }
    // The fixed source documents14-byte ID03 button reports separately.
    // They do not update the display pointer or synthesize a release/tap.
    if (length == 14 && report[2] == 3) {
        if (!valid_) return ESP_ERR_NOT_FINISHED;
        *frame = last_;
        return ESP_OK;
    }
    const uint8_t count = report[5] & 0x1f;
    if (count > 5 || length != 7 + count * 10 || (report[5] & 0xe0)) {
        valid_ = false;
        return ESP_ERR_INVALID_RESPONSE;
    }
    // This UI accepts a single contact. Preserve the original count rather
    // than clipping a multi-contact report to the first visible coordinate.
    if (count > 1) { valid_ = false; return ESP_ERR_INVALID_RESPONSE; }
    Tt21100Frame next{};
    if (count) {
        const uint8_t *point = report + 7;
        if ((point[0] & 7) != 0) { valid_ = false; return ESP_ERR_INVALID_RESPONSE; }
        next.pressed = (point[1] & 0x80) != 0;
        if (next.pressed) { next.x = word(point + 2); next.y = word(point + 4); }
    }
    last_ = next; valid_ = true;
    *frame = next;
    return ESP_OK;
}
