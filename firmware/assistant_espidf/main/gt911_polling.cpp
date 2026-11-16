#include "gt911_polling.h"

#include "esp_log.h"

namespace {
constexpr const char *kTag = "gt911_polling";
constexpr int kProductId = 0x8140, kConfiguration = 0x8047, kStatus = 0x814e;
uint16_t word(const uint8_t *data) { return data[0] | (static_cast<uint16_t>(data[1]) << 8); }
}

esp_err_t Gt911Polling::Init(esp_lcd_panel_io_handle_t io) {
    if (!io) return ESP_ERR_INVALID_ARG;
    if (io_) return ESP_ERR_INVALID_STATE;
    uint8_t product[4]{}, configuration = 0;
    esp_err_t result = esp_lcd_panel_io_rx_param(io, kProductId, product, sizeof(product));
    if (result == ESP_OK) result = esp_lcd_panel_io_rx_param(io, kConfiguration, &configuration, 1);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "initial register read failed=%s", esp_err_to_name(result));
        return result;
    }
    // Like the reviewed driver, log the observed registers. Their contents
    // are not a physical controller identity or board revision acceptance.
    ESP_LOGI(kTag, "product=%02x%02x%02x%02x config=%02x reset=board_owned acceptance=not_run",
        product[0], product[1], product[2], product[3], configuration);
    io_ = io;
    return ESP_OK;
}

esp_err_t Gt911Polling::Read(Gt911Frame *frame) {
    if (!frame) return ESP_ERR_INVALID_ARG;
    *frame = {};
    if (!io_) return ESP_ERR_INVALID_STATE;
    uint8_t status = 0;
    esp_err_t result = esp_lcd_panel_io_rx_param(io_, kStatus, &status, 1);
    if (result != ESP_OK) { valid_ = false; return result; }
    if (!(status & 0x80)) {
        // No new report is not a release report. Do not clear a report that
        // may become ready between this read and a following write.
        if (!valid_) return ESP_ERR_NOT_FINISHED;
        *frame = last_;
        return ESP_OK;
    }
    Gt911Frame next{};
    next.count = status & 0x0f;
    const bool unsupported = status & 0x70; // Keys/proximity/reserved flags are not handled here.
    const bool invalid_count = next.count > 5;
    uint8_t points[5 * 8]{};
    if (!unsupported && !invalid_count && next.count) {
        result = esp_lcd_panel_io_rx_param(io_, kStatus + 1, points, next.count * 8);
    }
    // Acknowledge a ready report, including an unsupported one, but publish
    // nothing unless its payload and the actual acknowledgement both succeed.
    const uint8_t clear = 0;
    const esp_err_t acknowledged = esp_lcd_panel_io_tx_param(io_, kStatus, &clear, 1);
    if (result != ESP_OK || acknowledged != ESP_OK || unsupported || invalid_count) {
        valid_ = false;
        return result != ESP_OK ? result : acknowledged != ESP_OK ? acknowledged : ESP_ERR_INVALID_RESPONSE;
    }
    for (uint8_t index = 0; index < next.count; ++index) {
        const uint8_t *point = points + index * 8;
        next.points[index] = {point[0], word(point + 1), word(point + 3), word(point + 5)};
        for (uint8_t previous = 0; previous < index; ++previous) {
            if (next.points[previous].track_id == point[0]) { valid_ = false; return ESP_ERR_INVALID_RESPONSE; }
        }
    }
    last_ = next;
    valid_ = true;
    *frame = next;
    return ESP_OK;
}
