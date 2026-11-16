#include "board_profile.h"

#include <atomic>
#include <cstring>
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if defined(ASSISTANT_BOARD_WS_RLCD)
#include "rlcd_panel_commands.h"
#elif defined(ASSISTANT_BOARD_WS_EPAPER)
#include "waveshare_paper_commands.h"
#elif !defined(ASSISTANT_BOARD_ZECTRIX) && !defined(ASSISTANT_BOARD_WS_EPAPER397)
#include "maza_panel_commands.h"
#endif

namespace {
constexpr const char *TAG = "paper_display";
#if defined(ASSISTANT_BOARD_MAZA_S31)
constexpr gpio_num_t kDc = GPIO_NUM_35, kReset = GPIO_NUM_14, kBusy = GPIO_NUM_13;
constexpr gpio_num_t kClock = GPIO_NUM_34, kCs = GPIO_NUM_17, kMosi = GPIO_NUM_33;
#elif defined(ASSISTANT_BOARD_WS_RLCD)
constexpr gpio_num_t kDc = GPIO_NUM_5, kReset = GPIO_NUM_41, kBusy = GPIO_NUM_6;
constexpr gpio_num_t kClock = GPIO_NUM_11, kCs = GPIO_NUM_40, kMosi = GPIO_NUM_12;
#else
#if defined(ASSISTANT_BOARD_WS_EPAPER397)
constexpr gpio_num_t kDc = GPIO_NUM_9, kReset = GPIO_NUM_46, kBusy = GPIO_NUM_3;
constexpr gpio_num_t kClock = GPIO_NUM_11, kCs = GPIO_NUM_10, kMosi = GPIO_NUM_12;
#else
constexpr gpio_num_t kMosi = GPIO_NUM_13;
#if defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX)
constexpr gpio_num_t kDc = GPIO_NUM_10, kReset = GPIO_NUM_9, kBusy = GPIO_NUM_8;
constexpr gpio_num_t kClock = GPIO_NUM_12, kCs = GPIO_NUM_11;
#else
constexpr gpio_num_t kDc = GPIO_NUM_17, kReset = GPIO_NUM_18, kBusy = GPIO_NUM_21;
constexpr gpio_num_t kClock = GPIO_NUM_14, kCs = GPIO_NUM_46;
#endif
#endif
#endif
constexpr int kWidth = board_profile::width, kHeight = board_profile::height;
constexpr size_t kFrameBytes = kWidth * kHeight / 8;
#if defined(ASSISTANT_BOARD_WS_RLCD)
constexpr int64_t kMinimumRefreshUs = 100000;
portMUX_TYPE g_te_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_te_edges;
bool g_panel_ready;
void IRAM_ATTR te_edge(void *) {
    portENTER_CRITICAL_ISR(&g_te_lock);
    ++g_te_edges;
    portEXIT_CRITICAL_ISR(&g_te_lock);
}
uint32_t te_edge_count() {
    portENTER_CRITICAL(&g_te_lock);
    const uint32_t edges = g_te_edges;
    portEXIT_CRITICAL(&g_te_lock);
    return edges;
}
#else
constexpr int64_t kBusyTimeoutUs = 10000000, kMinimumRefreshUs = 1000000;
#endif
#if defined(ASSISTANT_BOARD_ZECTRIX)
constexpr uint32_t kPartialLimit = 10;
DMA_ATTR uint8_t g_native_line[400 / 4];
#else
constexpr uint32_t kPartialLimit = 20;
#endif
i2c_master_bus_handle_t g_i2c;
spi_device_handle_t g_spi;
lv_display_t *g_display;
SemaphoreHandle_t g_frames_lock;
TaskHandle_t g_worker;
uint16_t *g_pixels;
uint8_t *g_transfer;
// Only the worker owns this persistent transaction/transfer storage. On a
// timeout they stay alive, and no subsequent SPI transfer is started.
spi_transaction_t g_transaction;
EXT_RAM_BSS_ATTR uint8_t g_pending[kFrameBytes], g_converted[kFrameBytes], g_shown[kFrameBytes];
std::atomic<uint32_t> g_submitted{0}, g_completed{0};
std::atomic<bool> g_failed{false};
bool g_dirty, g_shown_valid, g_started;

esp_err_t transfer(const uint8_t *data, size_t length, bool parameter) {
    ESP_RETURN_ON_ERROR(gpio_set_level(kDc, parameter), TAG, "panel DC");
    g_transaction = {};
    g_transaction.length = length * 8;
#if defined(ASSISTANT_BOARD_ZECTRIX)
    g_transaction.override_freq_hz = 20000000;
#endif
    if (length <= sizeof(g_transaction.tx_data)) {
        g_transaction.flags = SPI_TRANS_USE_TXDATA;
        std::memcpy(g_transaction.tx_data, data, length);
    } else g_transaction.tx_buffer = data;
    ESP_RETURN_ON_ERROR(spi_device_queue_trans(g_spi, &g_transaction, pdMS_TO_TICKS(1000)), TAG, "SPI queue");
    spi_transaction_t *completed = nullptr;
    ESP_RETURN_ON_ERROR(spi_device_get_trans_result(g_spi, &completed, pdMS_TO_TICKS(1000)), TAG, "SPI completion");
    return completed == &g_transaction ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t command(uint8_t code, const uint8_t *data = nullptr, size_t length = 0) {
    ESP_RETURN_ON_ERROR(transfer(&code, 1, false), TAG, "panel command");
    return length ? transfer(data, length, true) : ESP_OK;
}

#if !defined(ASSISTANT_BOARD_WS_RLCD)
esp_err_t wait_ready() {
    const int64_t deadline = esp_timer_get_time() + kBusyTimeoutUs;
#if defined(ASSISTANT_BOARD_ZECTRIX)
    while (!gpio_get_level(kBusy)) {
#else
    while (gpio_get_level(kBusy)) {
#endif
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ESP_OK;
}

#if !defined(ASSISTANT_BOARD_ZECTRIX)
esp_err_t window() {
#if defined(ASSISTANT_BOARD_WS_EPAPER397)
    // SSD1677 X is a16-bit pixel address; entry01 incrementsX/decrementsY.
    // Start at the descending window's last row, not RAM row0.
    const uint8_t x[] = {0, 0, 0x1f, 3}, y[] = {0xdf, 1, 0, 0}, cursor[] = {0xdf, 1}, zero[] = {0, 0};
    ESP_RETURN_ON_ERROR(command(0x44, x, sizeof(x)), TAG, "paper3.97 X pixel window");
    ESP_RETURN_ON_ERROR(command(0x45, y, sizeof(y)), TAG, "paper3.97 Y descending window");
    ESP_RETURN_ON_ERROR(command(0x4e, zero, sizeof(zero)), TAG, "paper3.97 X cursor");
    return command(0x4f, cursor, sizeof(cursor));
#elif defined(ASSISTANT_BOARD_WS_EPAPER)
    const uint8_t x[] = {0, 24}, y[] = {199, 0, 0, 0}, cursor[] = {199, 0}, zero = 0;
    ESP_RETURN_ON_ERROR(command(0x44, x, sizeof(x)), TAG, "RAM X window");
    ESP_RETURN_ON_ERROR(command(0x45, y, sizeof(y)), TAG, "RAM Y window descending");
    ESP_RETURN_ON_ERROR(command(0x4e, &zero, 1), TAG, "RAM X cursor");
    return command(0x4f, cursor, sizeof(cursor));
#else
    const uint8_t x[] = {0, 15}, y[] = {0, 0, 0x27, 0x01}, zero[] = {0, 0};
    ESP_RETURN_ON_ERROR(command(0x44, x, sizeof(x)), TAG, "RAM X window");
    ESP_RETURN_ON_ERROR(command(0x45, y, sizeof(y)), TAG, "RAM Y window");
    ESP_RETURN_ON_ERROR(command(0x4e, zero, 1), TAG, "RAM X cursor");
    return command(0x4f, zero, sizeof(zero));
#endif
}

esp_err_t activate(uint8_t mode) {
    ESP_RETURN_ON_ERROR(command(0x22, &mode, 1), TAG, "refresh mode");
    ESP_RETURN_ON_ERROR(command(0x20), TAG, "refresh start");
    if (mode == 0xc4 || mode == 0x04 || mode == 0xc7 || mode == 0xcf || mode == 0xf7 || mode == 0xff) {
        // A stuck-low/unconnected BUSY is not evidence of a presented frame.
        const int64_t assert_deadline = esp_timer_get_time() + 1000000;
        while (!gpio_get_level(kBusy)) {
            if (esp_timer_get_time() >= assert_deadline) return ESP_ERR_TIMEOUT;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } else vTaskDelay(pdMS_TO_TICKS(10));
    return wait_ready();
}

#endif

#endif

#if defined(ASSISTANT_BOARD_WS_RLCD)
esp_err_t wait_te(uint32_t edges) {
    const uint32_t start = te_edge_count();
    const int64_t deadline = esp_timer_get_time() + 500000;
    while (static_cast<uint32_t>(te_edge_count() - start) < edges) {
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

esp_err_t initialize_panel(bool partial) {
    (void)partial;
    if (g_panel_ready) return ESP_OK;
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "RLCD reset idle");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 0), TAG, "RLCD reset low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "RLCD reset high");
    vTaskDelay(pdMS_TO_TICKS(50));
    for (const auto &item : RLCD_PANEL_SETUP) {
        ESP_RETURN_ON_ERROR(command(item.code, item.data, item.length), TAG, "RLCD fixed panel setup");
        if (item.delay_ms) vTaskDelay(pdMS_TO_TICKS(item.delay_ms));
    }
    ESP_RETURN_ON_ERROR(wait_te(2), TAG, "RLCD initial vertical scan");
    g_panel_ready = true;
    return ESP_OK;
}
#elif defined(ASSISTANT_BOARD_ZECTRIX)
// SSD2683 OTP/transition sequence adapted from Zectrix Lab's MIT reference:
// itopinion/zectrix-note4-epd-demo @ ca285c98ed0641f86780edb1f5ec77b0335fe649.
// Keep the shared bounded worker/transaction storage; no LVGL-time SPI calls.
esp_err_t initialize_panel(bool partial) {
    (void)partial;
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "NOTE4 reset idle");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 0), TAG, "NOTE4 reset low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "NOTE4 reset high");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "NOTE4 reset BUSY");
    const uint8_t panel[] = {0x2f, 0x0e}, otp = 1;
    ESP_RETURN_ON_ERROR(command(0x00, panel, sizeof(panel)), TAG, "NOTE4 OTP select");
    ESP_RETURN_ON_ERROR(command(0xe9, &otp, 1), TAG, "NOTE4 OTP init");
    return wait_ready();
}

esp_err_t read_temperature(uint8_t *temperature) {
    ESP_RETURN_ON_ERROR(command(0x40), TAG, "NOTE4 temperature start");
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "NOTE4 temperature BUSY");
    ESP_RETURN_ON_ERROR(gpio_set_level(kDc, 1), TAG, "NOTE4 temperature DC");
    g_transaction = {};
    g_transaction.flags = SPI_TRANS_USE_RXDATA;
    g_transaction.rxlength = 8;
    g_transaction.override_freq_hz = 8000000;
    ESP_RETURN_ON_ERROR(spi_device_queue_trans(g_spi, &g_transaction, pdMS_TO_TICKS(1000)), TAG, "NOTE4 temperature queue");
    spi_transaction_t *completed = nullptr;
    ESP_RETURN_ON_ERROR(spi_device_get_trans_result(g_spi, &completed, pdMS_TO_TICKS(1000)), TAG, "NOTE4 temperature completion");
    if (completed != &g_transaction) return ESP_ERR_INVALID_STATE;
    *temperature = g_transaction.rx_data[0];
    return ESP_OK;
}

esp_err_t prepare_frame(bool full) {
    if (full) {
        uint8_t temperature = 0;
        ESP_RETURN_ON_ERROR(read_temperature(&temperature), TAG, "NOTE4 actual temperature read");
        const uint8_t encoded = temperature <= 5 ? 232 : temperature <= 10 ? 235 : temperature <= 20 ? 238 : temperature <= 30 ? 241 : temperature <= 127 ? 244 : 232;
        const uint8_t mode = 2;
        ESP_RETURN_ON_ERROR(command(0xe0, &mode, 1), TAG, "NOTE4 OTP temperature mode");
        ESP_RETURN_ON_ERROR(command(0xe6, &encoded, 1), TAG, "NOTE4 OTP temperature value");
        ESP_LOGD(TAG, "NOTE4 temperature_raw=%u encoded=%u", temperature, encoded);
    } else {
        const uint8_t border = 0x77, mode = 0;
        ESP_RETURN_ON_ERROR(command(0x50, &border, 1), TAG, "NOTE4 transition border");
        ESP_RETURN_ON_ERROR(command(0xe0, &mode, 1), TAG, "NOTE4 transition temperature mode");
    }
    ESP_RETURN_ON_ERROR(command(0xa5), TAG, "NOTE4 temperature activate");
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "NOTE4 temperature activation BUSY");
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!full) {
        // Whole-screen partial refresh still needs SSD2683's explicit window.
        static constexpr uint8_t window[] = {0, 0, 1, 0x8f, 0, 0, 1, 0x2b, 1};
        ESP_RETURN_ON_ERROR(command(0x83, window, sizeof(window)), TAG, "NOTE4 transition window");
    }
    ESP_RETURN_ON_ERROR(command(0x10), TAG, "NOTE4 frame RAM");
    return wait_ready();
}

esp_err_t finish_frame() {
    ESP_RETURN_ON_ERROR(command(0x04), TAG, "NOTE4 internal power on");
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "NOTE4 internal power BUSY");
    const uint8_t zero = 0;
    ESP_RETURN_ON_ERROR(command(0x12, &zero, 1), TAG, "NOTE4 refresh start");
    const int64_t assert_deadline = esp_timer_get_time() + 1000000;
    while (gpio_get_level(kBusy)) {
        if (esp_timer_get_time() >= assert_deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "NOTE4 physical refresh BUSY");
    // Never send power-off after a timeout: the worker stops and retains DMA.
    ESP_RETURN_ON_ERROR(command(0x02, &zero, 1), TAG, "NOTE4 internal power off");
    return wait_ready();
}
#elif defined(ASSISTANT_BOARD_WS_EPAPER397)
esp_err_t initialize_panel(bool partial) {
    // Preserve the established old-image RAM when entering partial mode.
    if (!partial) {
        ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "paper3.97 reset idle");
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 0), TAG, "paper3.97 reset low");
        vTaskDelay(pdMS_TO_TICKS(2));
        ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "paper3.97 reset high");
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_RETURN_ON_ERROR(wait_ready(), TAG, "paper3.97 reset BUSY");
        ESP_RETURN_ON_ERROR(command(0x12), TAG, "paper3.97 software reset");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(wait_ready(), TAG, "paper3.97 software reset BUSY");
        static constexpr uint8_t booster[] = {0xae, 0xc7, 0xc3, 0xc0, 0x80};
        const uint8_t gate[] = {0xdf, 1, 2};
        ESP_RETURN_ON_ERROR(command(0x0c, booster, sizeof(booster)), TAG, "paper3.97 booster");
        ESP_RETURN_ON_ERROR(command(0x01, gate, sizeof(gate)), TAG, "paper3.97 gate outputs");
    }
    const uint8_t temperature = 0x80, border = partial ? uint8_t{0x80} : uint8_t{1}, entry = 1;
    ESP_RETURN_ON_ERROR(command(0x18, &temperature, 1), TAG, "paper3.97 temperature sensor");
    ESP_RETURN_ON_ERROR(command(0x3c, &border, 1), TAG, "paper3.97 border");
    ESP_RETURN_ON_ERROR(command(0x11, &entry, 1), TAG, "paper3.97 data entry");
    return window();
}
#elif defined(ASSISTANT_BOARD_WS_EPAPER)
esp_err_t waveform(const uint8_t *data) {
    ESP_RETURN_ON_ERROR(command(0x32, data, 153), TAG, "paper waveform");
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "paper waveform BUSY");
    ESP_RETURN_ON_ERROR(command(0x3f, data + 153, 1), TAG, "paper waveform end");
    ESP_RETURN_ON_ERROR(command(0x03, data + 154, 1), TAG, "paper gate voltage");
    ESP_RETURN_ON_ERROR(command(0x04, data + 155, 3), TAG, "paper source voltage");
    return command(0x2c, data + 158, 1);
}

esp_err_t initialize_panel(bool partial) {
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "paper reset idle");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 0), TAG, "paper reset low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "paper reset high");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "paper reset BUSY");
    if (partial) {
        // Reassert the descending-Y contract after hardware reset as well.
        const uint8_t gate[] = {0xc7, 0, 1}, entry = 1;
        ESP_RETURN_ON_ERROR(command(0x01, gate, sizeof(gate)), TAG, "partial gate outputs");
        ESP_RETURN_ON_ERROR(command(0x11, &entry, 1), TAG, "partial data entry");
        ESP_RETURN_ON_ERROR(waveform(WS_PAPER_PARTIAL_WAVEFORM), TAG, "partial waveform");
        // The queue may outlive a timeout. Long payloads must be persistent,
        // just like g_transfer; small commands are copied into tx_data.
        static constexpr uint8_t options[] = {0, 0, 0, 0, 0, 0x40, 0, 0, 0, 0}, border = 0x80;
        ESP_RETURN_ON_ERROR(command(0x37, options, sizeof(options)), TAG, "partial options");
        ESP_RETURN_ON_ERROR(command(0x3c, &border, 1), TAG, "partial border");
        return activate(0xc0);
    }
    ESP_RETURN_ON_ERROR(command(0x12), TAG, "paper software reset");
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "paper software reset BUSY");
    const uint8_t gate[] = {0xc7, 0, 1}, entry = 1, border = 1, temperature = 0x80;
    ESP_RETURN_ON_ERROR(command(0x01, gate, sizeof(gate)), TAG, "paper gate outputs");
    ESP_RETURN_ON_ERROR(command(0x11, &entry, 1), TAG, "paper data entry");
    ESP_RETURN_ON_ERROR(window(), TAG, "paper initial RAM window");
    ESP_RETURN_ON_ERROR(command(0x3c, &border, 1), TAG, "paper border");
    ESP_RETURN_ON_ERROR(command(0x18, &temperature, 1), TAG, "paper temperature");
    ESP_RETURN_ON_ERROR(activate(0xb1), TAG, "paper temperature waveform load");
    return waveform(WS_PAPER_FULL_WAVEFORM);
}
#else
esp_err_t initialize_panel(bool partial) {
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 0), TAG, "panel reset low");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(kReset, 1), TAG, "panel reset high");
    vTaskDelay(pdMS_TO_TICKS(10));
    for (const auto &item : MAZA_SETUP) ESP_RETURN_ON_ERROR(command(item.code, item.data, item.length), TAG, "panel setup");
    ESP_RETURN_ON_ERROR(window(), TAG, "panel RAM");
    ESP_RETURN_ON_ERROR(wait_ready(), TAG, "panel initial BUSY");
    ESP_RETURN_ON_ERROR(command(0x32, MAZA_FULL_WAVEFORM, sizeof(MAZA_FULL_WAVEFORM)), TAG, "full LUT");
    if (partial) {
        ESP_RETURN_ON_ERROR(command(0x32, MAZA_PARTIAL_WAVEFORM, sizeof(MAZA_PARTIAL_WAVEFORM)), TAG, "partial LUT");
        ESP_RETURN_ON_ERROR(activate(0xc0), TAG, "partial power");
        const uint8_t border = 0x80;
        ESP_RETURN_ON_ERROR(command(0x3c, &border, 1), TAG, "partial border");
    }
    return ESP_OK;
}

#endif

esp_err_t draw(bool full) {
#if defined(ASSISTANT_BOARD_WS_RLCD)
    (void)full;
    ESP_RETURN_ON_ERROR(wait_te(1), TAG, "RLCD next vertical blank");
    const uint8_t columns[] = {0x12, 0x2a}, pages[] = {0, 0xc7};
    ESP_RETURN_ON_ERROR(command(0x2a, columns, sizeof(columns)), TAG, "RLCD column window");
    ESP_RETURN_ON_ERROR(command(0x2b, pages, sizeof(pages)), TAG, "RLCD page window");
    ESP_RETURN_ON_ERROR(command(0x2c, g_transfer, kFrameBytes), TAG, "RLCD frame RAM");
    // The first following blank may close a scan that began during the write.
    // A second complete scan boundary is needed before a shown receipt.
    return wait_te(2);
#elif defined(ASSISTANT_BOARD_ZECTRIX)
    ESP_RETURN_ON_ERROR(prepare_frame(full), TAG, "NOTE4 frame prepare");
    for (int y = 0; y < kHeight; ++y) {
        std::memset(g_native_line, 0, sizeof(g_native_line));
        for (int x = 0; x < kWidth; ++x) {
            const int byte = y * (kWidth / 8) + x / 8, shift = 7 - x % 8;
            const uint8_t current = (g_transfer[byte] >> shift) & 1;
            const uint8_t previous = full ? 0 : (g_shown[byte] >> shift) & 1;
            g_native_line[x / 4] |= ((previous << 1) | current) << (6 - (x % 4) * 2);
        }
        ESP_RETURN_ON_ERROR(transfer(g_native_line, sizeof(g_native_line), true), TAG, "NOTE4 native row");
    }
    return finish_frame();
#else
    ESP_RETURN_ON_ERROR(window(), TAG, "frame window");
    ESP_RETURN_ON_ERROR(command(0x24, g_transfer, kFrameBytes), TAG, "frame RAM");
#if defined(ASSISTANT_BOARD_WS_EPAPER397)
    if (full) {
        ESP_RETURN_ON_ERROR(window(), TAG, "paper3.97 base window");
        ESP_RETURN_ON_ERROR(command(0x26, g_transfer, kFrameBytes), TAG, "paper3.97 full base RAM");
    }
    const uint8_t update[] = {full ? uint8_t{0x40} : uint8_t{0}, 0};
    ESP_RETURN_ON_ERROR(command(0x21, update, sizeof(update)), TAG, "paper3.97 update planes");
    ESP_RETURN_ON_ERROR(activate(full ? 0xf7 : 0xff), TAG, "paper3.97 physical refresh");
    if (!full) {
        // Only advance the old-image baseline after the physical BUSY cycle.
        ESP_RETURN_ON_ERROR(window(), TAG, "paper3.97 next base window");
        ESP_RETURN_ON_ERROR(command(0x26, g_transfer, kFrameBytes), TAG, "paper3.97 next base RAM");
    }
    return ESP_OK;
#elif defined(ASSISTANT_BOARD_WS_EPAPER)
    if (full) ESP_RETURN_ON_ERROR(command(0x26, g_transfer, kFrameBytes), TAG, "full base RAM");
    return activate(full ? 0xc7 : 0xcf);
#else
    return activate(full ? 0xc4 : 0x04);
#endif
#endif
}

void refresh_task(void *) {
    uint32_t partial_count = 0;
    int64_t previous_refresh = 0;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (g_failed.load()) continue;
        while (true) {
            const int64_t remaining = kMinimumRefreshUs - (esp_timer_get_time() - previous_refresh);
            if (remaining > 0) vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>((remaining + 999) / 1000)));
            if (xSemaphoreTake(g_frames_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
                g_failed = true;
                ESP_LOGE(TAG, "refresh frame lock timeout; presentation stopped");
                break;
            }
            const bool pending = g_dirty;
            const uint32_t generation = g_submitted.load();
            if (pending) { std::memcpy(g_transfer, g_pending, kFrameBytes); g_dirty = false; }
            xSemaphoreGive(g_frames_lock);
            if (!pending) break;
            esp_err_t result = ESP_OK;
            const bool changed = !g_shown_valid || std::memcmp(g_transfer, g_shown, kFrameBytes) != 0;
            bool full = !g_shown_valid || partial_count >= kPartialLimit;
#if defined(ASSISTANT_BOARD_ZECTRIX)
            // Match the owning driver's full-refresh boundary for a large edit.
            if (!full && changed) {
                size_t changed_bits = 0;
                for (size_t index = 0; index < kFrameBytes; ++index) changed_bits += __builtin_popcount(static_cast<unsigned>(g_transfer[index] ^ g_shown[index]));
                full = changed_bits * 10 >= kFrameBytes * 8 * 3;
            }
#endif
            if (changed) {
#if defined(ASSISTANT_BOARD_WS_RLCD)
                result = initialize_panel(false);
                if (result == ESP_OK) result = draw(false);
#elif defined(ASSISTANT_BOARD_ZECTRIX)
                // Reset/OTP before every refresh; internal power-off resets the
                // controller state, while the physical image remains the base.
                result = initialize_panel(!full);
                if (result == ESP_OK) result = draw(full);
#else
                if (full) result = initialize_panel(false);
                if (result == ESP_OK) result = draw(full);
                // Establish the full base image before switching to the fixed
                // panel's partial waveform, matching its owning driver sequence.
#if !defined(ASSISTANT_BOARD_WS_EPAPER) && !defined(ASSISTANT_BOARD_WS_EPAPER397)
                if (result == ESP_OK && full) result = draw(true);
#endif
                if (result == ESP_OK && full) result = initialize_panel(true);
#endif
                previous_refresh = esp_timer_get_time();
            }
            if (result != ESP_OK) {
                g_failed = true;
                ESP_LOGE(TAG, "refresh_failed generation=%lu error=%s; presentation stopped", static_cast<unsigned long>(generation), esp_err_to_name(result));
                // Keep the task and all DMA/transaction memory alive after a
                // possible outstanding SPI timeout. Future flushes are rejected.
                break;
            }
            if (changed) {
                std::memcpy(g_shown, g_transfer, kFrameBytes);
                g_shown_valid = true;
                partial_count = full ? 0 : partial_count + 1;
            }
            g_completed.store(generation, std::memory_order_release);
            ESP_LOGD(TAG, "presented generation=%lu changed=%d full=%d", static_cast<unsigned long>(generation), changed, full);
        }
    }
}

void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    if (!g_failed.load()) {
        if (area->x1 != 0 || area->y1 != 0 || area->x2 != kWidth - 1 || area->y2 != kHeight - 1) {
            g_failed = true;
            ESP_LOGE(TAG, "unexpected partial LVGL frame; presentation stopped");
        } else {
            const auto *source = reinterpret_cast<const uint16_t *>(pixels);
            std::memset(g_converted, 0, kFrameBytes);
            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    // Pure black/white UI avoids a panel-specific gray palette.
#if defined(ASSISTANT_BOARD_WS_RLCD)
                    // ST7305 native byte contains two columns by four rows.
                    const int inverted_y = kHeight - 1 - y;
                    const size_t index = (x / 2) * (kHeight / 4) + inverted_y / 4;
                    const int bit = 7 - ((inverted_y % 4) * 2 + x % 2);
                    if (source[y * kWidth + x] >= 0x7fff) g_converted[index] |= 1U << bit;
#elif defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_WS_EPAPER397)
                    if (source[y * kWidth + x] >= 0x7fff) g_converted[y * (kWidth / 8) + x / 8] |= 1U << (7 - x % 8);
#else
                    // Maza landscape pixel(x,y) maps to portrait RAM(y,295-x).
                    if (source[y * kWidth + x] >= 0x7fff) g_converted[(295 - x) * 16 + y / 8] |= 1U << (7 - y % 8);
#endif
                }
            }
            if (xSemaphoreTake(g_frames_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                std::memcpy(g_pending, g_converted, kFrameBytes);
                g_submitted.fetch_add(1);
                g_dirty = true;
                xSemaphoreGive(g_frames_lock);
                xTaskNotifyGive(g_worker);
            } else { g_failed = true; ESP_LOGE(TAG, "flush frame lock timeout; presentation stopped"); }
        }
    }
    // Physical refresh is independent of the LVGL lock. Completion is exposed
    // separately so UI receipts and card confirmation do not claim early display.
    lv_display_flush_ready(display);
}

esp_err_t initialize_spi() {
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << kDc) | (1ULL << kReset);
    outputs.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "panel outputs");
    gpio_config_t busy = {};
    busy.pin_bit_mask = 1ULL << kBusy;
    busy.mode = GPIO_MODE_INPUT;
#if defined(ASSISTANT_BOARD_WS_RLCD)
    busy.pull_down_en = GPIO_PULLDOWN_ENABLE;
#elif defined(ASSISTANT_BOARD_ZECTRIX)
    // An unconnected active-low BUSY must fail initial readiness, not look idle.
    busy.pull_down_en = GPIO_PULLDOWN_ENABLE;
#else
    busy.pull_up_en = GPIO_PULLUP_ENABLE;
#endif
    ESP_RETURN_ON_ERROR(gpio_config(&busy), TAG, "panel BUSY/TE input");
#if defined(ASSISTANT_BOARD_WS_RLCD)
    const esp_err_t service = gpio_install_isr_service(0);
    if (service != ESP_OK && service != ESP_ERR_INVALID_STATE) return service;
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(kBusy, te_edge, nullptr), TAG, "RLCD TE callback");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(kBusy, GPIO_INTR_POSEDGE), TAG, "RLCD TE rising edge");
    ESP_RETURN_ON_ERROR(gpio_intr_enable(kBusy), TAG, "RLCD TE interrupt");
#endif
    spi_bus_config_t bus = {};
    bus.mosi_io_num = kMosi; bus.sclk_io_num = kClock;
    bus.miso_io_num = GPIO_NUM_NC; bus.quadwp_io_num = GPIO_NUM_NC; bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = kFrameBytes;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "paper SPI3");
    spi_device_interface_config_t device = {};
#if defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_WS_EPAPER397)
    device.clock_speed_hz = 20000000;
#elif defined(ASSISTANT_BOARD_ZECTRIX)
    device.clock_speed_hz = 20000000;
    device.flags = SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX;
#else
    device.clock_speed_hz = 40000000;
#endif
    device.mode = 0; device.spics_io_num = kCs; device.queue_size = 1;
    return spi_bus_add_device(SPI3_HOST, &device, &g_spi);
}
}

esp_err_t bsp_i2c_init() {
    if (g_i2c) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = BSP_I2C_NUM;
#if defined(ASSISTANT_BOARD_MAZA_S31)
    bus.sda_io_num = GPIO_NUM_3; bus.scl_io_num = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_WS_RLCD)
    bus.sda_io_num = GPIO_NUM_13; bus.scl_io_num = GPIO_NUM_14;
#elif defined(ASSISTANT_BOARD_WS_EPAPER397)
    bus.sda_io_num = GPIO_NUM_41; bus.scl_io_num = GPIO_NUM_42;
#elif defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX)
    bus.sda_io_num = GPIO_NUM_47; bus.scl_io_num = GPIO_NUM_48;
#else
    bus.sda_io_num = GPIO_NUM_48; bus.scl_io_num = GPIO_NUM_47;
#endif
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_i2c);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }

lv_display_t *bsp_display_start() {
    if (g_started) return g_display;
    g_started = true;
    g_frames_lock = xSemaphoreCreateMutex();
    g_transfer = static_cast<uint8_t *>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    g_pixels = static_cast<uint16_t *>(heap_caps_malloc(kWidth * kHeight * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_frames_lock || !g_transfer || !g_pixels) return nullptr;
    if (bsp_i2c_init() != ESP_OK || initialize_spi() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    if (xTaskCreate(refresh_task, "paper_refresh", 4096, nullptr, 2, &g_worker) != pdPASS) return nullptr;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lv_display_create(kWidth, kHeight);
    if (g_display) {
        lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(g_display, g_pixels, nullptr, kWidth * kHeight * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(g_display, flush);
    }
    lvgl_port_unlock();
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    // Reflective paper has no backlight/contrast control. Retain its last image
    // when the generic screen-off preference requests zero brightness.
    return percent >= 0 && percent <= 100 ? ESP_OK : ESP_ERR_INVALID_ARG;
}
uint32_t paper_display_submitted_generation() { return g_submitted.load(); }
bool paper_display_presented_since(uint32_t generation) {
    const uint32_t completed = g_completed.load(std::memory_order_acquire);
    return !g_failed.load() && static_cast<int32_t>(completed - generation) > 0 && completed == g_submitted.load();
}
