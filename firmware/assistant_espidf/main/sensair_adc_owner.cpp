#include "sensair_adc_owner.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
#include "esp_codec_adc.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if !CONFIG_IDF_TARGET_ESP32C5 || !CONFIG_CODEC_DATA_ADC_SUPPORT
#error "Sensair ADC candidate requires C5 and the existing codec ADC backend"
#endif

namespace {
constexpr const char *kTag = "sensair_adc_owner";
constexpr size_t kReadBytes = 1024;
constexpr uint32_t kCallMs = 3000;
constexpr EventBits_t kReady = BIT0, kClosed = BIT1;
enum class Operation { Format, Enable, Read, Wake };

struct State {
    // Bridge and task each own one reference. No task accesses the bridge
    // after its data_if is freed by audio_codec_delete_data_if().
    std::atomic<unsigned> references{1};
    std::atomic<bool> closing{false}, poisoned{false}, open{false};
    std::atomic<bool> configured{false}, enabled{false};
    std::atomic<int> result{ESP_CODEC_DEV_DRV_ERR};
    std::atomic<int> close_result{ESP_CODEC_DEV_DRV_ERR};
    QueueHandle_t commands = nullptr;
    SemaphoreHandle_t call_lock = nullptr, reply = nullptr;
    EventGroupHandle_t events = nullptr;
    TaskHandle_t task = nullptr;
    const audio_codec_data_if_t *adc = nullptr; // Task-owned exclusively.
    adc_continuous_handle_t handle = nullptr; // Explicitly owned for checked deinit.
    esp_codec_dev_sample_info_t format{};
    bool enable = false;
    size_t read_size = 0;
    uint8_t pcm[kReadBytes]{};
    ~State() {
        if (commands) vQueueDelete(commands);
        if (call_lock) vSemaphoreDelete(call_lock);
        if (reply) vSemaphoreDelete(reply);
        if (events) vEventGroupDelete(events);
    }
};

struct Bridge {
    audio_codec_data_if_t base;
    State *state;
};

void release(State *state) { if (state->references.fetch_sub(1) == 1) delete state; }

void stop(State *state, const char *reason) {
    if (!state->poisoned.exchange(true)) ESP_LOGE(kTag, "%s; reject further input, cleanup/restart required", reason);
    state->closing = true;
    const Operation wake = Operation::Wake;
    // If the sole slot already contains work, closing is observed before it
    // executes. Otherwise this wakes an idle owner so it can destroy the ADC.
    xQueueSend(state->commands, &wake, 0);
}

void owner_task(void *argument) {
    auto *state = static_cast<State *>(argument);
    audio_codec_adc_cfg_t config = {};
    config.continuous_cfg.max_store_buf_size = 2048;
    config.continuous_cfg.conv_frame_size = 1024;
    config.continuous_cfg.sample_freq_hz = 16000;
    config.continuous_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    config.continuous_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    config.continuous_cfg.pattern_num = 1;
    config.continuous_cfg.cfg_mode = AUDIO_CODEC_ADC_CFG_MODE_SINGLE_UNIT;
    config.continuous_cfg.cfg.single_unit.unit_id = ADC_UNIT_1;
    config.continuous_cfg.cfg.single_unit.atten = ADC_ATTEN_DB_12;
    config.continuous_cfg.cfg.single_unit.bit_width = ADC_BITWIDTH_12;
    config.continuous_cfg.cfg.single_unit.channel_id[0] = 5;
    adc_continuous_handle_cfg_t handle_config = {};
    handle_config.max_store_buf_size = 2048; handle_config.conv_frame_size = 1024;
    handle_config.flags.flush_pool = true;
    const esp_err_t allocated = adc_continuous_new_handle(&handle_config, &state->handle);
    if (allocated == ESP_OK) {
        config.handle = state->handle;
        state->adc = audio_codec_new_adc_data(&config);
    } else {
        ESP_LOGE(kTag, "ADC handle creation failed=%s", esp_err_to_name(allocated));
    }
    state->open = state->adc && state->adc->is_open(state->adc);
    state->result = state->adc ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
    xEventGroupSetBits(state->events, kReady);
    if (state->adc) {
        Operation operation;
        while (!state->closing.load()) {
            if (xQueueReceive(state->commands, &operation, portMAX_DELAY) != pdTRUE) continue;
            if (state->closing.load()) break;
            int result = ESP_CODEC_DEV_DRV_ERR;
            switch (operation) {
                case Operation::Format:
                    state->configured = false;
                    result = state->adc->set_fmt(state->adc, ESP_CODEC_DEV_TYPE_IN, &state->format);
                    state->configured = result == ESP_CODEC_DEV_OK;
                    break;
                case Operation::Enable:
                    result = state->enable && !state->configured.load() ? ESP_CODEC_DEV_WRONG_STATE :
                        state->adc->enable(state->adc, ESP_CODEC_DEV_TYPE_IN, state->enable);
                    state->enabled = result == ESP_CODEC_DEV_OK && state->enable;
                    break;
                case Operation::Read:
                    std::memset(state->pcm, 0, sizeof(state->pcm));
                    result = state->adc->read(state->adc, state->pcm, static_cast<int>(state->read_size));
                    break;
                case Operation::Wake:
                    break;
            }
            state->result = result;
            state->open = state->adc->is_open(state->adc);
            xSemaphoreGive(state->reply);
            if (result != ESP_CODEC_DEV_OK) stop(state, "underlying ADC operation failed");
        }
        // Keep close/deinit on the same task as create, set_fmt, start/stop,
        // and read. The bridge's lifetime is independent of this cleanup.
        // codec_dev's ADC close logs some SDK failures but returns success.
        // Own the SDK handle externally so stop/deinit have checked results.
        state->close_result = state->adc->enable(state->adc, ESP_CODEC_DEV_TYPE_IN, false);
        const int closed = state->adc->close(state->adc);
        if (closed != ESP_CODEC_DEV_OK) ESP_LOGE(kTag, "ADC close reported=%d; physical resources unconfirmed", closed);
        if (closed != ESP_CODEC_DEV_OK) state->close_result = closed;
        std::free(const_cast<audio_codec_data_if_t *>(state->adc));
        state->adc = nullptr;
    } else {
        state->close_result = ESP_CODEC_DEV_OK;
    }
    if (state->handle) {
        const esp_err_t deinitialized = adc_continuous_deinit(state->handle);
        if (deinitialized != ESP_OK) {
            state->close_result = ESP_CODEC_DEV_DRV_ERR;
            ESP_LOGE(kTag, "ADC deinit failed=%s; owner restart required", esp_err_to_name(deinitialized));
        }
        state->handle = nullptr; // On failure resources remain unconfirmed.
    }
    state->enabled = false; state->configured = false; state->open = false;
    xEventGroupSetBits(state->events, kClosed);
    release(state);
    vTaskDelete(nullptr);
}

State *state_of(const audio_codec_data_if_t *interface) {
    return interface ? reinterpret_cast<const Bridge *>(interface)->state : nullptr;
}

bool is_open(const audio_codec_data_if_t *interface) {
    State *state = state_of(interface);
    return state && state->open.load() && !state->closing.load() && !state->poisoned.load();
}

int submit_locked(State *state, Operation operation) {
    if (state->closing.load() || state->poisoned.load()) return ESP_CODEC_DEV_WRONG_STATE;
    xSemaphoreTake(state->reply, 0);
    if (xQueueSend(state->commands, &operation, 0) != pdTRUE) {
        stop(state, "ADC command slot unexpectedly occupied");
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (xSemaphoreTake(state->reply, pdMS_TO_TICKS(kCallMs)) != pdTRUE) {
        // All request fields and PCM storage belong to State. No caller
        // buffer or stack object is retained after this timeout returns.
        stop(state, "ADC call timed out");
        return ESP_CODEC_DEV_DRV_ERR;
    }
    if (state->poisoned.load()) return ESP_CODEC_DEV_DRV_ERR;
    return state->result.load();
}

bool begin(State *state) {
    if (!state || state->closing.load() || state->poisoned.load() || xTaskGetCurrentTaskHandle() == state->task) return false;
    return xSemaphoreTake(state->call_lock, pdMS_TO_TICKS(50)) == pdTRUE;
}

int set_format(const audio_codec_data_if_t *interface, esp_codec_dev_type_t type, esp_codec_dev_sample_info_t *format) {
    State *state = state_of(interface);
    if (type != ESP_CODEC_DEV_TYPE_IN || !format || format->sample_rate != 16000 || format->bits_per_sample != 16 ||
        format->channel != 1 || (format->channel_mask != 0 && format->channel_mask != 1)) {
        if (state) stop(state, "ADC format rejected");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!begin(state)) {
        if (state) stop(state, "ADC format call unavailable");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    state->format = *format;
    const int result = submit_locked(state, Operation::Format);
    xSemaphoreGive(state->call_lock);
    return result;
}

int enable(const audio_codec_data_if_t *interface, esp_codec_dev_type_t type, bool enabled) {
    State *state = state_of(interface);
    if (type != ESP_CODEC_DEV_TYPE_IN) {
        if (state) stop(state, "ADC enable type rejected");
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (!begin(state)) {
        if (state) stop(state, "ADC enable call unavailable");
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    state->enable = enabled;
    const int result = submit_locked(state, Operation::Enable);
    xSemaphoreGive(state->call_lock);
    return result;
}

int read(const audio_codec_data_if_t *interface, uint8_t *data, int size) {
    if (!data || size <= 0 || size % sizeof(int16_t) || static_cast<size_t>(size) > kReadBytes) return ESP_CODEC_DEV_INVALID_ARG;
    std::memset(data, 0, static_cast<size_t>(size));
    State *state = state_of(interface);
    if (!begin(state)) return ESP_CODEC_DEV_WRONG_STATE;
    state->read_size = static_cast<size_t>(size);
    const int result = submit_locked(state, Operation::Read);
    if (result == ESP_CODEC_DEV_OK) std::memcpy(data, state->pcm, static_cast<size_t>(size));
    xSemaphoreGive(state->call_lock);
    return result;
}

int close(const audio_codec_data_if_t *interface) {
    auto *bridge = const_cast<Bridge *>(reinterpret_cast<const Bridge *>(interface));
    State *state = bridge ? bridge->state : nullptr;
    if (!state) return ESP_CODEC_DEV_OK;
    // Caller has joined every callback before deleting the data_if. Do not
    // wait for a command mutex held by a callback on the ADC task itself.
    if (xTaskGetCurrentTaskHandle() == state->task) return ESP_CODEC_DEV_WRONG_STATE;
    state->closing = true;
    const Operation wake = Operation::Wake;
    xQueueSend(state->commands, &wake, 0);
    const bool done = (xEventGroupWaitBits(state->events, kClosed, pdFALSE, pdTRUE, pdMS_TO_TICKS(kCallMs)) & kClosed) != 0;
    if (!done) ESP_LOGE(kTag, "owner cleanup still pending; restart required before ADC reinitialization");
    const int result = done ? state->close_result.load() : ESP_CODEC_DEV_DRV_ERR;
    bridge->state = nullptr;
    release(state); // Bridge storage can now be freed, even if task is late.
    return result;
}
}

const audio_codec_data_if_t *sensair_adc_owner_new() {
    auto *state = new (std::nothrow) State;
    auto *bridge = static_cast<Bridge *>(std::calloc(1, sizeof(Bridge)));
    if (!state || !bridge) { delete state; std::free(bridge); return nullptr; }
    state->commands = xQueueCreate(1, sizeof(Operation));
    state->call_lock = xSemaphoreCreateMutex(); state->reply = xSemaphoreCreateBinary();
    state->events = xEventGroupCreate();
    if (!state->commands || !state->call_lock || !state->reply || !state->events) { delete state; std::free(bridge); return nullptr; }
    bridge->state = state;
    bridge->base.is_open = is_open; bridge->base.set_fmt = set_format;
    bridge->base.enable = enable; bridge->base.read = read; bridge->base.close = close;
    state->references.fetch_add(1);
    if (xTaskCreate(owner_task, "sensair_adc", 8192, state, 5, &state->task) != pdPASS) {
        release(state); release(state); std::free(bridge); return nullptr;
    }
    if (!(xEventGroupWaitBits(state->events, kReady, pdFALSE, pdTRUE, pdMS_TO_TICKS(kCallMs)) & kReady) || state->result.load() != ESP_CODEC_DEV_OK) {
        close(&bridge->base); std::free(bridge); return nullptr;
    }
    return &bridge->base;
}

bool sensair_adc_owner_ready(const audio_codec_data_if_t *interface) {
    const State *state = state_of(interface);
    return state && state->open.load() && state->configured.load() && state->enabled.load() &&
        !state->closing.load() && !state->poisoned.load();
}
