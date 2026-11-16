#include "device_config.h"
#include "wifi_provisioning.h"
#include "device_network.h"
#include "device_http_client.h"
#include "display_ui.h"
#include "audio_io.h"
#include "control_protocol.h"
#include "storage.h"
#include "board_profile.h"
#if defined(ASSISTANT_BOARD_FIXED_WAKE)
#include "wake_word.h"
#endif
#if defined(ASSISTANT_BOARD_SENSECAP)
#include "sensecap_partitions.h"
#endif
#if defined(ASSISTANT_BOARD_AIPI)
#include "board_power.h"
#include "esp_partition.h"
#endif
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "assistant";

static void log_startup_memory(const char *phase) {
    ESP_LOGI(TAG, "startup_memory phase=%s internal_free=%u internal_largest=%u psram_free=%u", phase,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

// 配对阶段不启用音频/工具协议，设备 nonce 仅用于领取本次会话凭据。
static cJSON *pairing_post(DeviceHttpClient &session, const char *body, int *status) {
    *status = 0;
    esp_http_client_handle_t client = session.handle();
    if (!client) return nullptr;
    esp_err_t error = esp_http_client_clear_response_buffer(client);
    if (error == ESP_OK) error = esp_http_client_set_method(client, HTTP_METHOD_POST);
    if (error == ESP_OK) error = esp_http_client_set_header(client, "Content-Type", "application/json");
    cJSON *result = nullptr;
    bool complete = false;
    const int body_length = strlen(body);
    if (error == ESP_OK) error = esp_http_client_open(client, body_length);
    if (error == ESP_OK) {
        int sent = 0;
        while (sent < body_length) {
            int count = esp_http_client_write(client, body + sent, body_length - sent);
            if (count <= 0) break;
            sent += count;
        }
        if (sent == body_length && esp_http_client_fetch_headers(client) >= 0) {
            *status = esp_http_client_get_status_code(client);
            char response[2048] = {}; int received = 0;
            while (received < static_cast<int>(sizeof(response))) {
                int count = esp_http_client_read(client, response + received, sizeof(response) - received);
                if (count == 0) complete = esp_http_client_is_complete_data_received(client);
                if (count <= 0) break;
                received += count;
            }
            // Filling the buffer leaves no room to prove all cached body bytes
            // were consumed. Reject it even if the parser has finished already.
            if (complete) result = cJSON_ParseWithLength(response, received);
        }
    }
    if (!complete) ESP_LOGW(TAG, "pairing_http_incomplete status=%d error=%s", *status, esp_err_to_name(error == ESP_OK ? ESP_FAIL : error));
    // open() prepares the next request without reconnecting a live transport.
    // Partial/oversized replies must never enter the next response parser.
    if (!complete) {
        // A failed custom-transport connect can still own a partial TLS stream
        // while the SDK is INIT; close alone does not release that stream.
        (void)session.Reset();
    } else if (!esp_http_client_is_persistent_connection(client)) {
        if (esp_http_client_close(client) != ESP_OK) (void)session.Reset();
    }
    return result;
}

static esp_err_t pair_device(DeviceConfig *config) {
    char base[224];
    const bool secure = strncmp(config->ws_url, "wss://", 6) == 0;
    const char *authority = config->ws_url + (secure ? 6 : 5);
    const char *slash = strchr(authority, '/');
    const size_t length = slash ? static_cast<size_t>(slash - authority) : strlen(authority);
    if (!length || length > 180 || memchr(authority, '@', length)) return ESP_ERR_INVALID_ARG;
    snprintf(base, sizeof(base), "%s://%.*s", secure ? "https" : "http", static_cast<int>(length), authority);
    uint8_t random[24]; esp_fill_random(random, sizeof(random));
    char nonce[49]; for (size_t index = 0; index < sizeof(random); ++index) snprintf(nonce + index * 2, 3, "%02x", random[index]);
    char body[256]; snprintf(body, sizeof(body), "{\"device_nonce\":\"%s\"}", nonce);
    char url[384]; snprintf(url, sizeof(url), "%s/api/devices/pairing/device-start", base);
    DeviceHttpClient http;
    if (http.Init(url, 10000) != ESP_OK) return ESP_FAIL;
    int status = 0;
    cJSON *session = pairing_post(http, body, &status);
    (void)http.Reset();
    const cJSON *identifier = session ? cJSON_GetObjectItem(session, "pairing_id") : nullptr;
    const cJSON *code = session ? cJSON_GetObjectItem(session, "pairing_code") : nullptr;
    if (status != 201 || !cJSON_IsString(identifier) || !cJSON_IsString(code) || strlen(identifier->valuestring) > 64 || strlen(code->valuestring) != 6) { cJSON_Delete(session); return ESP_FAIL; }
    for (const char *digit = code->valuestring; *digit; ++digit) {
        if (!isdigit(static_cast<unsigned char>(*digit))) { cJSON_Delete(session); return ESP_FAIL; }
    }
    display_ui_pairing(code->valuestring);
    snprintf(url, sizeof(url), "%s/api/devices/pairing/%s/device-result", base, identifier->valuestring);
    snprintf(body, sizeof(body), "{\"code\":\"%s\",\"device_nonce\":\"%s\"}", code->valuestring, nonce);
    cJSON_Delete(session);
    // A separate fixed-endpoint client avoids changing the cellular factory's
    // TLS authority. Reuse it across all result polls; never retry device-start.
    if (http.Init(url, 10000) != ESP_OK) return ESP_FAIL;
    const int64_t deadline = esp_timer_get_time() + 600LL * 1000000;
    while (esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(3000)); status = 0;
        // Result lookup is repeatable; a failed request gets a fresh client on
        // the next scheduled poll, with a new lease from the same modem owner.
        if (!http.handle() && http.Init(url, 10000) != ESP_OK) return ESP_FAIL;
        cJSON *result = pairing_post(http, body, &status);
        const cJSON *token = result ? cJSON_GetObjectItem(result, "device_credential") : nullptr;
        if (status == 200 && cJSON_IsString(token) && strlen(token->valuestring) >= 24 && strlen(token->valuestring) < sizeof(config->device_token)) {
            strlcpy(config->device_token, token->valuestring, sizeof(config->device_token)); cJSON_Delete(result);
            esp_err_t saved = device_config_save(config);
            if (saved == ESP_OK) display_ui_state(UiState::Connecting, "Pairing complete");
            return saved;
        }
        cJSON_Delete(result);
        if (status == 403 || status == 429) return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

static void button_task(void *) {
    if constexpr (!board_profile::touch_listen && board_profile::listen_button != GPIO_NUM_NC) {
        const int button_pin = board_profile::listen_button == GPIO_NUM_NC ? 0 : static_cast<int>(board_profile::listen_button);
        gpio_config_t cfg = {.pin_bit_mask = 1ULL << button_pin, .mode = GPIO_MODE_INPUT,
            .pull_up_en = board_profile::listen_active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
            .pull_down_en = board_profile::listen_active_high ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }
    ManualInputKey previous = ManualInputKey::None;
    bool card_button_held = false;
    bool cancellation_pending = false;
    bool wait_for_release = false;
    int64_t volume_pressed_at = 0, volume_repeat_at = 0;
    int64_t last_display_report = 0;
#if defined(ASSISTANT_BOARD_HEADLESS)
    int64_t portal_chord_started = 0;
    bool portal_chord_consumed = false;
    bool portal_chord_armed = false;
#endif
    while (true) {
        const int64_t now = esp_timer_get_time();
#if defined(ASSISTANT_BOARD_HEADLESS)
#if defined(ASSISTANT_BOARD_DOIT)
        const bool up_released = gpio_get_level(GPIO_NUM_15), down_released = gpio_get_level(GPIO_NUM_9);
        const bool listen_released = gpio_get_level(GPIO_NUM_10);
        if (up_released && down_released && listen_released) portal_chord_armed = true;
        const bool portal_chord = !up_released && !down_released && listen_released;
#elif defined(ASSISTANT_BOARD_SPOT_C5)
        const bool portal_chord = !gpio_get_level(GPIO_NUM_28) && !gpio_get_level(GPIO_NUM_5);
        if (gpio_get_level(GPIO_NUM_28) && !gpio_get_level(GPIO_NUM_5)) portal_chord_armed = true;
#elif defined(ASSISTANT_BOARD_S31_CORE)
        const bool portal_chord = !gpio_get_level(GPIO_NUM_61);
        if (!portal_chord) portal_chord_armed = true;
#elif defined(ASSISTANT_BOARD_ATOM_ECHOS3R)
        const bool portal_chord = !gpio_get_level(GPIO_NUM_41);
        if (!portal_chord) portal_chord_armed = true;
#elif defined(ASSISTANT_BOARD_EDA_BEAR)
        const bool portal_chord = !gpio_get_level(GPIO_NUM_38);
        if (!portal_chord) portal_chord_armed = true;
#elif defined(ASSISTANT_BOARD_DF_AI_CAM)
        const bool portal_chord = !gpio_get_level(GPIO_NUM_0);
        if (!portal_chord) portal_chord_armed = true;
#elif defined(ASSISTANT_BOARD_ATOM_M12_BASE)
        const bool portal_chord = false;
#endif
        if (!portal_chord) { portal_chord_started = 0; portal_chord_consumed = false; }
        else {
            if (!portal_chord_started) portal_chord_started = now;
            if (portal_chord_armed && !portal_chord_consumed && now - portal_chord_started >= 3000000) {
                portal_chord_consumed = true;
                portal_chord_armed = false;
                const esp_err_t result = provisioning_open_device_portal();
                if (result != ESP_OK) ESP_LOGE(TAG, "phone_portal_open_failed error=%s", esp_err_to_name(result));
                else display_ui_notification("查看窗口已开启：连接助手-配置，访问192.168.4.1", nullptr);
            }
        }
#endif
        if (now - last_display_report >= 10000000) { last_display_report = now; display_ui_log_diagnostics(); }
        if constexpr (!board_profile::touch_listen) {
            const ManualInputKey current = board_profile::read_manual_key();
            const bool changed_held_key = previous != ManualInputKey::None && current != ManualInputKey::None && current != previous;
            if (current == ManualInputKey::Invalid || changed_held_key) {
                if (previous == ManualInputKey::Listen && !card_button_held) control_protocol_listen_cancel();
                cancellation_pending = cancellation_pending || card_button_held;
                card_button_held = false;
                wait_for_release = true;
                previous = ManualInputKey::None;
            }
            if (cancellation_pending) cancellation_pending = !display_ui_cancel_hardware_button();
            if (wait_for_release) {
                if (current == ManualInputKey::None && !cancellation_pending) wait_for_release = false;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (previous == ManualInputKey::None && current == ManualInputKey::Listen) {
                card_button_held = display_ui_handle_hardware_button(true);
                if (!card_button_held) control_protocol_listen_start();
            }
            if (previous == ManualInputKey::Listen && current == ManualInputKey::None) {
                if (card_button_held) display_ui_handle_hardware_button(false);
                else control_protocol_listen_stop();
                card_button_held = false;
            }
            if (current == ManualInputKey::VolumeUp || current == ManualInputKey::VolumeDown) {
                const bool first_press = previous != current;
                if (first_press) { volume_pressed_at = now; volume_repeat_at = now; }
                if (first_press || (now - volume_pressed_at >= 2000000 && now - volume_repeat_at >= 200000)) {
                    volume_repeat_at = now;
                    const int step = first_press ? 10 : 5;
                    uint8_t volume = 0;
                    const esp_err_t result = audio_adjust_conversation_volume(current == ManualInputKey::VolumeUp ? step : -step, &volume);
                    if (result == ESP_OK) {
                        char text[48]; snprintf(text, sizeof(text), "对话音量 %u%%", volume);
                        display_ui_notification(text, nullptr);
                    } else ESP_LOGE(TAG, "manual_volume_failed error=%s", esp_err_to_name(result));
                }
            }
            previous = current;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

extern "C" void app_main() {
#if defined(ASSISTANT_BOARD_SPOT_C5)
    const esp_err_t supply = spot_c5_board_init();
    if (supply != ESP_OK) { ESP_LOGE(TAG, "Spot C5 startup supply failed=%s", esp_err_to_name(supply)); return; }
#endif
    ESP_LOGI(TAG, "hardware_profile board=%s screen=%dx%d round=%d", board_profile::name, board_profile::width, board_profile::height, board_profile::round);
#if defined(ASSISTANT_BOARD_SENSECAP)
    if (!sensecap_partition_layout_valid()) {
        ESP_LOGE(TAG, "SenseCAP layout mismatch;stopped before NVS and board writes;factory information preserved");
        return;
    }
    const esp_err_t power = sensecap_board_init();
    if (power != ESP_OK) { ESP_LOGE(TAG, "SenseCAP startup power failed:%s", esp_err_to_name(power)); return; }
#endif
#if defined(ASSISTANT_BOARD_GUITION)
    const esp_partition_t *single_app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    const esp_partition_t *configuration = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!single_app || single_app->address != 0x20000 || single_app->size != 0x3e0000 ||
        !configuration || configuration->address != 0x9000 || configuration->size != 0x4000 ||
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr) ||
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model")) {
        ESP_LOGE(TAG, "Guition single-app partition layout mismatch; startup stopped before NVS writes");
        return;
    }
#endif
#if defined(ASSISTANT_BOARD_AIPI)
    ESP_ERROR_CHECK(board_power_init());
    const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "nvsfactory");
    const esp_partition_t *configuration = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    const esp_partition_t *ota_data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, "otadata");
    if (!factory || factory->address != 0x9000 || factory->size != 0x4000 || !factory->readonly ||
        !configuration || configuration->address != 0x10000 || configuration->size != 0x4000 || configuration->readonly ||
        !ota_data || ota_data->address != 0x14000 || ota_data->size != 0x2000) {
        ESP_LOGE(TAG, "AiPi partition layout mismatch; factory area preserved, NVS initialization skipped");
        return;
    }
#endif
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) { ESP_LOGE(TAG, "NVS 初始化失败: %s；保留现有分区，不自动擦除", esp_err_to_name(nvs_err)); return; }
    // 通用音频/模型分配超过1KB优先PSRAM；内部RAM留给PHY定时器、DMA和任务控制块。
#if CONFIG_SPIRAM
    heap_caps_malloc_extmem_enable(1024);
#endif
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(wifi_prepare());
    DeviceConfig config;
    ESP_ERROR_CHECK(device_config_load(&config));
    log_startup_memory("before_display");
#if defined(ASSISTANT_BOARD_SENSECAP)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t mute = sensecap_set_amplifier(false);
        const esp_err_t dark = gpio_set_level(GPIO_NUM_8, 0);
        ESP_LOGE(TAG, "SenseCAP display startup stopped:%s PA=%s BL=%s;restart required", esp_err_to_name(display_result), esp_err_to_name(mute), esp_err_to_name(dark));
        return;
    }
#elif defined(ASSISTANT_BOARD_S31_KORVO)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t mute = gpio_set_level(GPIO_NUM_7, 0), blank = bsp_display_brightness_set(0);
        ESP_LOGE(TAG, "S31 Korvo display stopped:%s PA=%s panel_enable=%s;backlight powered/restart required", esp_err_to_name(display_result), esp_err_to_name(mute), esp_err_to_name(blank));
        return;
    }
#elif defined(ASSISTANT_BOARD_MOSAICO)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t dark = bsp_display_brightness_set(0), mute = mosaico_set_amplifier(false);
        ESP_LOGE(TAG, "Mosaico display stopped:%s dark=%s PA=%s;restart required", esp_err_to_name(display_result), esp_err_to_name(dark), esp_err_to_name(mute));
        return;
    }
#elif defined(ASSISTANT_BOARD_M5_COREP4)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t dark = bsp_display_brightness_set(0), mute = m5_corep4_set_amplifier(false);
        ESP_LOGE(TAG, "CoreP4X display stopped:%s BL=%s PA=%s;restart required", esp_err_to_name(display_result), esp_err_to_name(dark), esp_err_to_name(mute));
        return;
    }
#elif defined(ASSISTANT_BOARD_M5_TAB5)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t dark = bsp_display_brightness_set(0), mute = m5_tab5_set_amplifier(false);
        ESP_LOGE(TAG, "Tab5 display stopped:%s dark=%s PA=%s;restart required", esp_err_to_name(display_result), esp_err_to_name(dark), esp_err_to_name(mute));
        return;
    }
#elif defined(ASSISTANT_BOARD_P4_MIPI)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t dark = bsp_display_brightness_set(0);
        const esp_err_t mute = gpio_set_level(GPIO_NUM_53, 0);
        ESP_LOGE(TAG, "P4 MIPI display stopped board=%s:%s dark=%s PA=%s;restart required", board_profile::name, esp_err_to_name(display_result), esp_err_to_name(dark), esp_err_to_name(mute));
        return;
    }
#elif defined(ASSISTANT_BOARD_MATRIX)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t blank = matrix_display_quiesce();
        const esp_err_t amplifier_cut = gpio_set_level(GPIO_NUM_11, 0);
        ESP_LOGE(TAG, "matrix startup stopped error=%s blank=%s PA=%s;restart to retry", esp_err_to_name(display_result), esp_err_to_name(blank), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_WS_EPAPER397) || defined(ASSISTANT_BOARD_WS_RLCD)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        // Preserve the battery latch/supply rails; keep the speaker inactive.
        const esp_err_t amplifier_cut = gpio_set_level(BSP_POWER_AMP_IO, 0);
        ESP_LOGE(TAG, "paper startup stopped error=%s amplifier_cut=%s;restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_CORES3)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t dark = bsp_display_brightness_set(0);
        ESP_LOGE(TAG, "CoreS3 startup stopped error=%s dark=%s;restart required", esp_err_to_name(display_result), esp_err_to_name(dark));
        return;
    }
#elif (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = box3_set_amplifier(false);
        ESP_LOGE(TAG, "BOX startup stopped board=%s error=%s PA=%s;restart required", board_profile::name, esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif ((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_AMOLED_216) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = amoled216_set_amplifier(false);
        ESP_LOGE(TAG, "AMOLED startup stopped board=%s error=%s PA=%s;restart required", board_profile::name, esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_ELECTRON)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t backlight_cut = gpio_set_level(GPIO_NUM_46, 0);
        ESP_LOGE(TAG, "electronBot startup stopped error=%s backlight_cut=%s;restart to retry", esp_err_to_name(display_result), esp_err_to_name(backlight_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_K10)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        ESP_LOGE(TAG, "K10startup stopped:%s;GPIO0reservedforaudio/restart to retry", esp_err_to_name(display_result));
        return;
    }
#elif defined(ASSISTANT_BOARD_MVSRLORA)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = mvsrlora_set_amplifier(false);
        ESP_LOGE(TAG, "MVSRLoRa startup stopped error=%s amplifier_cut=%s;restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_TCAMERA)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t backlight_cut = gpio_set_level(GPIO_NUM_46, 0);
        // Do not switch V1.2's sharedMIC/PA18justbecause displayfailed.
        ESP_LOGE(TAG, "T-CameraPlus startup stopped error=%s backlight_cut=%s;restart to retry", esp_err_to_name(display_result), esp_err_to_name(backlight_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_VOCAT)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        // Unknown revision must never drive both PA candidates;the helper
        // touches only an already-configured,identified PA pin.
        const esp_err_t amplifier_cut = vocat_set_amplifier(false);
        ESP_LOGE(TAG, "VoCat startup stopped error=%s amplifier_cut=%s;check PCB revision/power-cycle if requested", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_SPARKBOT)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t enable_cut = gpio_set_level(GPIO_NUM_46, 0);
        ESP_LOGE(TAG, "SparkBot startup stopped error=%s shared_enable_cut=%s;restart to retry", esp_err_to_name(display_result), esp_err_to_name(enable_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_LCD_EV)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = lcd_ev_set_amplifier(false);
        ESP_LOGE(TAG, "LCD EV startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_STOPWATCH)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = stopwatch_set_amplifier(false);
        ESP_LOGE(TAG, "Stopwatch startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_CARDPUTER_ADV)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        ESP_LOGE(TAG, "Cardputer startup stopped error=%s; restart to retry", esp_err_to_name(display_result));
        return;
    }
#elif defined(ASSISTANT_BOARD_ATOM_S3_BASE)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = atomic_echo_base_set_amplifier(false);
        ESP_LOGE(TAG, "AtomS3 startup stopped error=%s amplifier_cut=%s; power Echo Base USB-C and restart", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_WS143C)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = gpio_set_level(GPIO_NUM_46, 0);
        ESP_LOGE(TAG, "Waveshare1.43C startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_WS349)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = waveshare_349_set_amplifier(false);
        ESP_LOGE(TAG, "Waveshare3.49 startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_WS35)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = waveshare_35_set_amplifier(false);
        ESP_LOGE(TAG, "Waveshare3.5 startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_WS4B)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = waveshare_4b_set_amplifier(false);
        ESP_LOGE(TAG, "Waveshare4B startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_WS_RGB_C)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = waveshare_rgb_c_set_amplifier(false);
        ESP_LOGE(TAG, "Waveshare RGB startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_KEVIN_RGB)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = gpio_set_level(GPIO_NUM_45, 0);
        ESP_LOGE(TAG, "Kevin RGB startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_LCDWIKI)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = gpio_set_level(GPIO_NUM_1, 1);
        ESP_LOGE(TAG, "LCDWiki startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_OTTO)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        ESP_LOGE(TAG, "Otto board/display initialization stopped error=%s; restart to retry", esp_err_to_name(display_result));
        return;
    }
#elif defined(ASSISTANT_BOARD_WS_CAM)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = waveshare_cam_set_amplifier(false);
        ESP_LOGE(TAG, "Waveshare CAM startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_GUITION)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        ESP_LOGE(TAG, "Guition display/touch startup stopped error=%s; restart to retry", esp_err_to_name(display_result));
        return;
    }
#elif defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = pca9557_box_set_amplifier(false);
        ESP_LOGE(TAG, "ZhengchenCam startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_KORVO2)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = gpio_set_level(GPIO_NUM_48, 0);
        ESP_LOGE(TAG, "Korvo2 startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_CHD_EYE)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = gpio_set_level(GPIO_NUM_40, 0);
        ESP_LOGE(TAG, "CHD EYE startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_STICK_S3)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = stick_s3_set_amplifier(false);
        ESP_LOGE(TAG, "StickS3 startup stopped error=%s amplifier_cut=%s; restart to retry", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#elif defined(ASSISTANT_BOARD_ECHO_PYRAMID)
    const esp_err_t display_result = display_ui_init();
    if (display_result != ESP_OK) {
        const esp_err_t amplifier_cut = echo_pyramid_set_amplifier(false);
        ESP_LOGE(TAG, "Pyramid startup stopped error=%s amplifier_cut=%s; check base USB-C power and restart", esp_err_to_name(display_result), esp_err_to_name(amplifier_cut));
        return;
    }
#else
    ESP_ERROR_CHECK(display_ui_init());
#endif
    log_startup_memory("after_display");
    storage_mount();
    if (!device_config_network_ready(&config)) {
        display_ui_state(UiState::Alert, "请连接“助手-配置”并访问192.168.4.1");
        ESP_ERROR_CHECK(provisioning_start(&config));
        return;
    }
    const esp_err_t network = device_network_start(&config);
    if (network != ESP_OK) {
        ESP_LOGE(TAG, "network startup failed type=%s error=%s;restart required", device_network_name(), esp_err_to_name(network));
        display_ui_state(UiState::Error, "Network startup failed\nRestart to retry");
        return;
    }
#if defined(ASSISTANT_BOARD_DF_AI_CAM) || defined(ASSISTANT_BOARD_EDA_BEAR)
    // This board's key task only opens the phone view: read_manual_key always
    // returns None. Keep it available through Wi-Fi/pairing/audio failures.
    if (xTaskCreate(button_task, "phone_key", 3072, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "phone key task allocation failed board=%s", board_profile::name);
        display_ui_state(UiState::Error, "Phone key unavailable\nRestart to retry");
        return;
    }
#endif
    // PHY定时器和连接资源先于音频前端分配；配对阶段不占用音频资源。
    display_ui_state(UiState::Connecting, device_network_name());
    bool online = false;
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (device_network_has_address()) { online = true; break; }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!online) { display_ui_state(UiState::Error, "Network address timeout\nRestart to retry"); return; }
    log_startup_memory("network_ready");
    if (!device_config_ready(&config)) {
        esp_err_t paired = pair_device(&config);
        if (paired != ESP_OK) {
            ESP_LOGW(TAG, "pairing_failed error=%s", esp_err_to_name(paired));
            display_ui_state(UiState::Error, "Pairing failed / expired\nRestart to retry");
            return;
        }
    }
#if defined(ASSISTANT_BOARD_FIXED_WAKE)
    const esp_err_t wake = wake_word_init();
    if (wake != ESP_OK) {
        ESP_LOGE(TAG, "required_fixed_wake_init_failed error=%s", esp_err_to_name(wake));
        display_ui_state(UiState::Error, "固定唤醒模型缺失或不可用，请同步更新模型");
        return;
    }
#endif
    esp_err_t audio = audio_io_init();
    log_startup_memory("after_audio");
    if (audio != ESP_OK) {
        ESP_LOGE(TAG, "audio_init_failed error=%s", esp_err_to_name(audio));
        display_ui_state(UiState::Error, "Audio initialization failed");
        return;
    }
#if !defined(ASSISTANT_BOARD_DF_AI_CAM) && !defined(ASSISTANT_BOARD_EDA_BEAR)
    xTaskCreate(button_task, "button", 3072, nullptr, 5, nullptr);
#endif
    // Wi-Fi event readiness is handled by the websocket client's reconnect loop.
    ESP_ERROR_CHECK(control_protocol_start(&config));
    ESP_ERROR_CHECK(display_ui_enable_manual_input());
    ESP_LOGI(TAG, "固件启动：控制通道=%s", config.ws_url);
}
