#include "display_ui.h"
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "board_profile.h"
#include "board_power.h"
#include "display_layout.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_font.h"
#include "control_protocol.h"
#include "paper_display.h"

#if defined(ASSISTANT_BOARD_WS_RLCD)
static const lv_font_t *const kUiFont = &lv_font_source_han_sans_sc_16_cjk;
static const lv_font_t *const kHeaderFont = &lv_font_montserrat_14;
#elif defined(ASSISTANT_BOARD_WS_EPAPER397)
static const lv_font_t kPaperUiFont = [] {
    lv_font_t font = ui_font;
    // Keep the existing CJK coverage for characters absent from the large font.
    font.fallback = &lv_font_source_han_sans_sc_16_cjk;
    return font;
}();
static const lv_font_t *const kUiFont = &kPaperUiFont;
static const lv_font_t *const kHeaderFont = &lv_font_montserrat_20;
#elif defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_MAZA) || defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_INTERNAL_RAM)
static const lv_font_t *const kUiFont = &lv_font_source_han_sans_sc_14_cjk;
static const lv_font_t *const kHeaderFont = &lv_font_montserrat_10;
#elif defined(ASSISTANT_BOARD_WS_RGB_C) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_LCDWIKI) || defined(ASSISTANT_BOARD_WS_CAM) || defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_KORVO2) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_TAIJI) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_CHD)
static const lv_font_t *const kUiFont = &lv_font_source_han_sans_sc_16_cjk;
static const lv_font_t *const kHeaderFont = kUiFont;
#else
static const lv_font_t *const kUiFont = board_profile::width <= 240 ? &lv_font_source_han_sans_sc_16_cjk : &ui_font;
static const lv_font_t *const kHeaderFont = kUiFont;
#endif

// ui_font 使用压缩字形；关闭解码会导致点云正常但所有文字为空。
#if !LV_USE_FONT_COMPRESSED
#error "ui_font requires CONFIG_LV_USE_FONT_COMPRESSED=y"
#endif

static const char *TAG = "display";
static lv_obj_t *g_title;
static lv_obj_t *g_pairing_guide;
static lv_obj_t *g_detail;
static lv_obj_t *g_cloud;
static lv_obj_t *g_status_dot;
static lv_obj_t *g_listen_button;
static bool g_touch_listen_ready;
static bool g_touch_listen_held;
static lv_obj_t *g_card;
static lv_obj_t *g_card_title;
static lv_obj_t *g_card_detail;
static lv_obj_t *g_card_clock;
static lv_obj_t *g_card_badge;
static lv_obj_t *g_card_buttons[2];
static const char *g_card_button_actions[2];
static int g_card_key_selection;
static bool g_card_key_active;
static uint32_t g_card_key_started;
static char g_card_key_id[81];
static char g_card_key_revision[65];
struct StoredCard { ScreenCardData data; uint32_t received_ms; uint32_t shown_ms; bool active; bool shown; bool dirty; };
static StoredCard g_timer_card = {}, g_info_card = {}, g_reminder_card = {};
static StoredCard g_info_pending[3] = {};
static size_t g_info_pending_count;
static StoredCard *g_visible_card;
static bool g_screen_link;
static uint32_t g_screen_last_tick;
static uint32_t g_action_started;
static uint32_t g_error_started;
static bool g_action_pending;
static char g_action_reference[81];
static char g_completed_timer[81];
static char g_last_error_cue[81];
static char g_oled_card_note[64];
static uint32_t g_paper_card_token;
static lv_obj_t *g_paper_body_viewport;
static constexpr int PAPER_CONTENT_X = DISPLAY_LAYOUT.content_x;
static constexpr int PAPER_CONTENT_WIDTH = DISPLAY_LAYOUT.content_width;
static constexpr int PAPER_CARD_BODY_Y = DISPLAY_LAYOUT.card_detail_y;
static constexpr int PAPER_HINT_HEIGHT = board_profile::width >= 800 ? 36 : (DISPLAY_LAYOUT.badge_height >= 20 ? 24 : 17);
static constexpr int PAPER_CARD_BODY_HEIGHT = DISPLAY_LAYOUT.buttons_y - PAPER_CARD_BODY_Y - PAPER_HINT_HEIGHT - 4;
static constexpr int PAPER_IDLE_BODY_HEIGHT = board_profile::height - DISPLAY_LAYOUT.detail_y - 21;
static uint32_t g_paper_page_started;
static int g_paper_page;
static void render_screen_card(bool force = false);
static UiState g_state = UiState::Idle;
static UiState g_after_notification = UiState::Idle;
static char g_after_detail[192];
static uint32_t g_notice_started;
static bool g_notice_active;
static volatile uint8_t g_audio_level;
static constexpr int CLOUD_WIDTH = DISPLAY_LAYOUT.cloud_width;
static constexpr int CLOUD_HEIGHT = DISPLAY_LAYOUT.cloud_height;
static constexpr float CLOUD_SCALE = CLOUD_WIDTH / 368.0f < CLOUD_HEIGHT / 240.0f ? CLOUD_WIDTH / 368.0f : CLOUD_HEIGHT / 240.0f;
static constexpr int PARTICLE_COUNT = 768;
static constexpr int SINE_COUNT = 512;
static constexpr int FRAME_PERIOD_MS = 40;
#if defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)
static constexpr int TRANSFER_ROWS = 20;
#endif
#if defined(ASSISTANT_BOARD_SPARKBOT)
// SharedBL/PA46 cannot dim independently. Keep a valid binary display state
// so idle blanking can always restore the panel through DCS29,withPAstable.
static uint8_t g_brightness = 100;
#else
static constexpr int DISPLAY_BRIGHTNESS_PERCENT = 75;
static uint8_t g_brightness = DISPLAY_BRIGHTNESS_PERCENT;
#endif
static uint8_t g_night_brightness = 35;
static bool g_night_mode_enabled = false;
static uint32_t g_screen_off_seconds = 0;
static uint32_t g_last_activity_ms;
static bool g_screen_dimmed;
static constexpr int CLOUD_EDGE_LIGHT_THRESHOLD = 16;
// 导航图标形态：播报导航指令时粒子重组为指令图标（straight/left/right/uturn/flag），线段展开表按图标分段。
static constexpr int NAV_ICON_COUNT = 5;
static constexpr float NAV_STROKE_HALF_WIDTH = 6.0f;
struct IconSegment { int16_t x1, y1, x2, y2; };
static const IconSegment NAV_SEGMENTS[] = {
    {184, 188, 184, 62}, {152, 104, 184, 62}, {184, 62, 216, 104},
    {212, 184, 212, 118}, {212, 118, 140, 118}, {167, 92, 139, 118}, {139, 118, 167, 144},
    {156, 184, 156, 118}, {156, 118, 228, 118}, {201, 92, 229, 118}, {229, 118, 201, 144},
    {150, 188, 150, 112}, {150, 112, 214, 112}, {214, 112, 214, 180}, {193, 158, 214, 182}, {214, 182, 235, 158},
    {150, 78, 150, 192}, {150, 78, 226, 100}, {226, 100, 150, 124},
};
static const struct { uint8_t offset, count; } NAV_ICON_RANGES[NAV_ICON_COUNT] = {{0, 3}, {3, 4}, {7, 4}, {11, 5}, {16, 3}};
static constexpr float TAU = 6.28318530718f;
struct CloudParticle { float x, y, z, angle, phi, layer; };
struct CloudProjection { int16_t x, y; uint8_t light; };
static CloudParticle *g_particles;
static CloudProjection *g_projected;
static float *g_sines;
static lv_color_t g_palette[32];
static uint16_t *g_pixels;
static uint16_t *g_front_pixels;
static uint16_t *g_display_buffer;
static float g_listen;
static float g_think;
static float g_speak;
static float g_free_cloud = 1.0f;
static float g_timer_cloud;
static float g_reminder;
static float g_nav_icon_w;
static int16_t *g_icon_sets[NAV_ICON_COUNT] = {};
static volatile uint8_t g_nav_icon;
static float g_envelope;
static float g_phase;
static float g_flow_phase;
static float g_rotation;
static volatile float g_pulse;
static float g_cloud_red = 197;
static float g_cloud_green = 218;
static float g_cloud_blue = 244;
static uint32_t g_previous_frame;
static uint32_t g_frame_count;
static uint32_t g_render_total_us;
static uint32_t g_render_max_us;
static uint32_t g_stats_started;

// 由现有按键任务读取，不依赖LVGL锁；显示卡死时仍能留下诊断心跳。
static std::atomic<uint32_t> g_display_timer_ms{0};
static std::atomic<uint32_t> g_refresh_started{0}, g_refresh_finished{0};
static std::atomic<uint32_t> g_flush_started{0}, g_flush_returned{0};
static std::atomic<uint32_t> g_flush_wait_ms{0}, g_display_lock_timeouts{0};

static bool lock_display() {
    if (board_profile::display_lock(1000)) return true;
    g_display_lock_timeouts.fetch_add(1, std::memory_order_relaxed);
    return false;
}

// 只在显示锁内调用，避免亮度命令插入一组窗口地址/像素写入中间。
static esp_err_t set_display_brightness(uint8_t brightness, const char *reason) {
#if defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_SENSAIR_C5)
    // Digital backlight or panel blanking supports only off/on.
    const uint8_t requested = brightness;
    brightness = brightness == 0 ? 0 : 100;
    ESP_LOGI(TAG, "binary_backlight requested=%u applied=%u", requested, brightness);
#endif
    const esp_err_t result = bsp_display_brightness_set(brightness);
    if (result == ESP_OK) ESP_LOGI(TAG, "brightness_applied percent=%u reason=%s", brightness, reason);
    else ESP_LOGE(TAG, "brightness_failed percent=%u reason=%s error=%s", brightness, reason, esp_err_to_name(result));
    return result;
}

static void restore_display_brightness() {
    if (g_screen_dimmed && set_display_brightness(g_brightness, "activity") == ESP_OK) g_screen_dimmed = false;
}

static void display_refresh_event(lv_event_t *event) {
    switch (lv_event_get_code(event)) {
        case LV_EVENT_REFR_START: g_refresh_started.fetch_add(1, std::memory_order_relaxed); break;
        case LV_EVENT_REFR_READY: g_refresh_finished.fetch_add(1, std::memory_order_relaxed); break;
        case LV_EVENT_FLUSH_START: g_flush_started.fetch_add(1, std::memory_order_relaxed); break;
        case LV_EVENT_FLUSH_FINISH: g_flush_returned.fetch_add(1, std::memory_order_relaxed); break;
        case LV_EVENT_FLUSH_WAIT_START: g_flush_wait_ms.store(esp_timer_get_time() / 1000, std::memory_order_relaxed); break;
        case LV_EVENT_FLUSH_WAIT_FINISH: g_flush_wait_ms.store(0, std::memory_order_relaxed); break;
        default: break;
    }
}

void display_ui_log_diagnostics() {
    const uint32_t now = esp_timer_get_time() / 1000;
    const uint32_t timer = g_display_timer_ms.load(std::memory_order_relaxed);
    const uint32_t wait = g_flush_wait_ms.load(std::memory_order_relaxed);
    ESP_LOGI(TAG, "display_health timer_age_ms=%lu refresh=%lu/%lu flush_calls=%lu/%lu flush_wait_ms=%lu lock_timeouts=%lu lv_tick=%lu",
        static_cast<unsigned long>(now - timer), static_cast<unsigned long>(g_refresh_finished.load()), static_cast<unsigned long>(g_refresh_started.load()),
        static_cast<unsigned long>(g_flush_returned.load()), static_cast<unsigned long>(g_flush_started.load()),
        static_cast<unsigned long>(wait ? now - wait : 0), static_cast<unsigned long>(g_display_lock_timeouts.load()), static_cast<unsigned long>(lv_tick_get()));
}

static float wave(float angle) {
    return g_sines[static_cast<int>(angle * (SINE_COUNT / TAU)) & (SINE_COUNT - 1)];
}

static esp_err_t cloud_allocate() {
    if constexpr (board_profile::text_only) return ESP_OK;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    g_particles = static_cast<CloudParticle *>(heap_caps_malloc(PARTICLE_COUNT * sizeof(CloudParticle), caps));
    g_projected = static_cast<CloudProjection *>(heap_caps_malloc(PARTICLE_COUNT * sizeof(CloudProjection), caps));
    g_sines = static_cast<float *>(heap_caps_malloc(SINE_COUNT * sizeof(float), caps));
    g_pixels = static_cast<uint16_t *>(heap_caps_calloc(CLOUD_WIDTH * CLOUD_HEIGHT, sizeof(uint16_t), caps));
    g_front_pixels = static_cast<uint16_t *>(heap_caps_calloc(CLOUD_WIDTH * CLOUD_HEIGHT, sizeof(uint16_t), caps));
    // ESP32-S3 SPI需要内部DMA传输缓冲；粒子与画布仍留在PSRAM。
#if defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)
    g_display_buffer = static_cast<uint16_t *>(heap_caps_malloc(board_profile::width * TRANSFER_ROWS * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
#endif
    bool display_buffer_ready = true;
#if defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)
    display_buffer_ready = g_display_buffer != nullptr;
#endif
    if (!g_particles || !g_projected || !g_sines || !g_pixels || !g_front_pixels || !display_buffer_ready) {
        heap_caps_free(g_particles); heap_caps_free(g_projected); heap_caps_free(g_sines); heap_caps_free(g_pixels); heap_caps_free(g_front_pixels); heap_caps_free(g_display_buffer);
        g_particles = nullptr; g_projected = nullptr; g_sines = nullptr; g_pixels = nullptr; g_front_pixels = nullptr; g_display_buffer = nullptr;
        return ESP_ERR_NO_MEM;
    }
    for (int index = 0; index < SINE_COUNT; ++index) g_sines[index] = sinf(index * TAU / SINE_COUNT);
    for (int index = 0; index < PARTICLE_COUNT; ++index) {
        const float y = 1.0f - 2.0f * (index + 0.5f) / PARTICLE_COUNT;
        const float angle = fmodf(index * 2.39996322973f, TAU);
        const float radius = sqrtf(1.0f - y * y);
        g_particles[index] = {cosf(angle) * radius, y, sinf(angle) * radius, angle, acosf(y), index % 11 == 0 ? 0.55f : 1.0f};
    }
    // 图标点集：粒子沿折线按弧长均匀采样，PSRAM 常驻，运行时零计算。
    for (int icon = 0; icon < NAV_ICON_COUNT; ++icon) {
        g_icon_sets[icon] = static_cast<int16_t *>(heap_caps_malloc(PARTICLE_COUNT * 2 * sizeof(int16_t), caps));
        if (!g_icon_sets[icon]) {
            for (int reset = 0; reset <= icon; ++reset) heap_caps_free(g_icon_sets[reset]);
            for (int reset = 0; reset < NAV_ICON_COUNT; ++reset) g_icon_sets[reset] = nullptr;
            return ESP_ERR_NO_MEM;
        }
        const IconSegment *segs = NAV_SEGMENTS + NAV_ICON_RANGES[icon].offset;
        const int seg_count = NAV_ICON_RANGES[icon].count;
        float lengths[6] = {}, total = 0;
        for (int s = 0; s < seg_count; ++s) { const float dx = segs[s].x2 - segs[s].x1, dy = segs[s].y2 - segs[s].y1; lengths[s] = sqrtf(dx * dx + dy * dy); total += lengths[s]; }
        for (int index = 0; index < PARTICLE_COUNT; ++index) {
            const float target = total * (index + 0.5f) / PARTICLE_COUNT;
            float acc = 0; int s = 0;
            while (s < seg_count - 1 && acc + lengths[s] < target) { acc += lengths[s]; ++s; }
            const float t = lengths[s] > 0 ? (target - acc) / lengths[s] : 0;
            const float dx = segs[s].x2 - segs[s].x1, dy = segs[s].y2 - segs[s].y1;
            const float across = ((index * 7 % 17) / 8.0f - 1.0f) * NAV_STROKE_HALF_WIDTH;
            const float inverse_length = lengths[s] > 0 ? 1.0f / lengths[s] : 0;
            g_icon_sets[icon][index * 2] = static_cast<int16_t>(segs[s].x1 + dx * t - dy * inverse_length * across);
            g_icon_sets[icon][index * 2 + 1] = static_cast<int16_t>(segs[s].y1 + dy * t + dx * inverse_length * across);
        }
    }
    return ESP_OK;
}

static const char *state_title(UiState state) {
    if constexpr (board_profile::text_only) {
        switch (state) {
            case UiState::Listening: return "LISTENING";
            case UiState::Speaking: return "SPEAKING";
            case UiState::Thinking: return "THINKING";
            case UiState::Executing: return "WORKING";
            case UiState::Connecting: return "CONNECTING";
            case UiState::Error: return "ERROR";
            case UiState::Alert: return "NOTICE";
            case UiState::Notification: return "REMINDER";
            case UiState::Updating: return "UPDATING";
            default: return "XIAOYANZI";
        }
    }
    switch (state) {
        case UiState::Listening: return "我在听";
        case UiState::Speaking: return "正在回答";
        case UiState::Thinking: return "想一想";
        case UiState::Executing: return "正在处理";
        case UiState::Connecting: return "正在连接";
        case UiState::Error: return "连接需要检查";
        case UiState::Alert: return "请留意";
        case UiState::Notification: return "提醒时间到了";
        case UiState::Updating: return "固件升级中";
        default: return "伊迪斯在这里";
    }
}

static uint32_t state_color(UiState state) {
    if (state == UiState::Error) return 0xebaaa4;
    if (state == UiState::Alert || state == UiState::Notification) return 0xe9cea3;
    if (state == UiState::Listening) return 0xb2e6d6;
    if (state == UiState::Thinking || state == UiState::Executing || state == UiState::Updating) return 0xc8c4e8;
    return 0xc5daf4;
}

static void supported_text(char *output, size_t capacity, const char *input) {
    size_t written = 0;
    if (!input) { output[0] = '\0'; return; }
    while (*input && written + 1 < capacity) {
        const unsigned char first = static_cast<unsigned char>(*input);
        size_t length = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (!length) { ++input; continue; }
        uint32_t codepoint = first & (length == 1 ? 0x7f : length == 2 ? 0x1f : length == 3 ? 0x0f : 0x07);
        bool valid = true;
        for (size_t index = 1; index < length; ++index) {
            const unsigned char next = static_cast<unsigned char>(input[index]);
            if (!next || (next & 0xc0) != 0x80) { valid = false; break; }
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (!valid) { ++input; continue; }
        if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) { input += length; continue; }
        lv_font_glyph_dsc_t glyph = {};
        const bool supported = codepoint == '\n' || (codepoint >= 0x20 && lv_font_get_glyph_dsc(kUiFont, &glyph, codepoint, 0) && !glyph.is_placeholder);
        if (supported) {
            if constexpr (board_profile::text_only) {
                if (codepoint == '\n') { output[written++] = ' '; input += length; continue; }
            }
            if (written + length >= capacity) break;
            memcpy(output + written, input, length); written += length;
        }
        input += length;
    }
    output[written] = '\0';
}

static void apply_state(UiState state, const char *detail) {
    if (g_state != state) ESP_LOGI(TAG, "ui_state previous=%u current=%u", static_cast<unsigned>(g_state), static_cast<unsigned>(state));
    g_state = state;
    if (g_listen_button) {
        if (!g_touch_listen_ready || state == UiState::Updating) {
            if (g_touch_listen_held) { g_touch_listen_held = false; control_protocol_listen_stop(); }
            lv_obj_add_state(g_listen_button, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(g_listen_button, LV_STATE_DISABLED);
        }
    }
    if (state == UiState::Error) g_error_started = lv_tick_get();
    render_screen_card();
    lv_label_set_text(g_title, state_title(state));
    char safe[384];
#if defined(ASSISTANT_BOARD_FIXED_WAKE)
    const char *fallback = "说“你好小智”唤醒我";
#else
    const char *fallback = board_profile::manual_audio_only ? "按住设备按键说话" : "说“你好，伊迪斯”唤醒我";
#endif
    if (state == UiState::Listening) fallback = "可以继续说话";
    else if (state == UiState::Thinking) fallback = "正在理解你的请求，请稍候";
    else if (state == UiState::Speaking) fallback = "正在播放回答";
    else if (state == UiState::Executing) fallback = "正在调用服务，结果尚未确认";
    else if (state == UiState::Connecting) fallback = "正在连接网络或服务，请稍候";
    else if (state == UiState::Error) fallback = "操作未完成，请核查网络、配置或后台状态";
    else if (state == UiState::Updating) fallback = "正在下载或校验固件，请保持供电";
    else if (state == UiState::Alert || state == UiState::Notification) fallback = "请查看当前提示，待办不会自动完成";
    supported_text(safe, sizeof(safe), detail && detail[0] ? detail : fallback);
    if constexpr (board_profile::reflective_display) {
        if (strcmp(lv_label_get_text(g_detail), safe) != 0) {
            lv_label_set_text(g_detail, safe); lv_obj_set_y(g_detail, 0);
            g_paper_page = 0; g_paper_page_started = lv_tick_get();
        }
    } else lv_label_set_text(g_detail, safe);
    const lv_color_t color = lv_color_hex(state_color(state));
    if (g_status_dot) lv_obj_set_style_bg_color(g_status_dot, color, 0);
    if constexpr (board_profile::text_only) return;
    // 状态切换过渡：文字淡入 + 云形态激发脉冲，重复切换时新动画替换旧的。
    g_pulse = 1.0f;
    lv_obj_t *labels[2] = {g_title, g_detail};
    for (lv_obj_t *label : labels) {
        lv_anim_t fade;
        lv_anim_init(&fade);
        lv_anim_set_var(&fade, label);
        lv_anim_set_exec_cb(&fade, [](void *var, int32_t value) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(var), value, 0); });
        lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&fade, 240);
        lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
        lv_anim_start(&fade);
    }
}

static void delete_screen_card() {
    if (g_card) lv_obj_delete(g_card);
    g_card = nullptr; g_card_title = nullptr; g_card_detail = nullptr; g_card_clock = nullptr; g_card_badge = nullptr;
    g_card_buttons[0] = nullptr; g_card_buttons[1] = nullptr; g_visible_card = nullptr;
    g_card_button_actions[0] = nullptr; g_card_button_actions[1] = nullptr;
    g_card_key_active = false; g_card_key_selection = 0;
    g_oled_card_note[0] = '\0';
    g_paper_body_viewport = nullptr; g_paper_page = 0; g_paper_page_started = lv_tick_get();
    if (g_title) lv_obj_remove_flag(g_title, LV_OBJ_FLAG_HIDDEN);
    if (g_detail) {
        lv_obj_remove_flag(g_detail, LV_OBJ_FLAG_HIDDEN);
        if constexpr (board_profile::reflective_display) lv_obj_set_y(g_detail, 0);
    }
}

static void draw_card_arrow(lv_obj_t *parent, const char *direction) {
    static lv_point_precise_t shaft[4], head[3];
    uint32_t count = 2;
    if (strcmp(direction, "left") == 0) { shaft[0] = {210, 210}; shaft[1] = {210, 145}; shaft[2] = {130, 145}; count = 3; head[0] = {154, 122}; head[1] = {130, 145}; head[2] = {154, 168}; }
    else if (strcmp(direction, "right") == 0) { shaft[0] = {155, 210}; shaft[1] = {155, 145}; shaft[2] = {235, 145}; count = 3; head[0] = {211, 122}; head[1] = {235, 145}; head[2] = {211, 168}; }
    else if (strcmp(direction, "uturn") == 0) { shaft[0] = {150, 210}; shaft[1] = {150, 135}; shaft[2] = {225, 135}; shaft[3] = {225, 205}; count = 4; head[0] = {203, 183}; head[1] = {225, 205}; head[2] = {247, 183}; }
    else if (strcmp(direction, "straight") == 0) { shaft[0] = {184, 215}; shaft[1] = {184, 125}; head[0] = {158, 151}; head[1] = {184, 125}; head[2] = {210, 151}; }
    else return;
    const float arrow_scale = board_profile::height < 300 ? 0.45f : 1.0f;
    const int arrow_center_y = board_profile::height < 300 ? 104 : board_profile::round ? 202 :
        board_profile::width >= 410 ? DISPLAY_LAYOUT.route_detail_y - 54 : 170;
    for (uint32_t index = 0; index < count; ++index) {
        shaft[index].x = board_profile::width / 2 + (shaft[index].x - 184) * arrow_scale;
        shaft[index].y = arrow_center_y + (shaft[index].y - 170) * arrow_scale;
    }
    for (auto &point : head) {
        point.x = board_profile::width / 2 + (point.x - 184) * arrow_scale;
        point.y = arrow_center_y + (point.y - 170) * arrow_scale;
    }
    const lv_point_precise_t *paths[2] = {shaft, head};
    for (int index = 0; index < 2; ++index) { lv_obj_t *line = lv_line_create(parent); lv_line_set_points(line, paths[index], index == 0 ? count : 3); lv_obj_set_style_line_width(line, 9, 0); lv_obj_set_style_line_color(line, lv_color_hex(0xb8a4ff), 0); lv_obj_set_style_line_rounded(line, true, 0); }
}

static void card_badge_message(const char *text, const char *oled_text) {
    if constexpr (board_profile::text_only) {
        snprintf(g_oled_card_note, sizeof(g_oled_card_note), "%s", oled_text);
        lv_label_set_text(g_card_badge, oled_text);
    } else lv_label_set_text(g_card_badge, text);
}

static void update_oled_card_header() {
    if constexpr (!board_profile::text_only) return;
    if (!g_visible_card || !g_card_badge) return;
    const auto &data = g_visible_card->data;
#if defined(ASSISTANT_BOARD_MATRIX)
    // This board has no application keys or touch. Do not invent a BOOT
    // confirmation gesture; keep actions available through voice/the console.
    char matrix_header[32];
    if (strcmp(data.kind, "timer") == 0) {
        int32_t remaining = data.remaining_s;
        if (strcmp(data.status, "running") == 0) remaining -= static_cast<int32_t>(lv_tick_elaps(g_visible_card->received_ms) / 1000);
        remaining = remaining > 0 ? remaining : 0;
        snprintf(matrix_header, sizeof(matrix_header), "%02ld:%02ld", static_cast<long>(remaining / 60), static_cast<long>(remaining % 60));
    } else snprintf(matrix_header, sizeof(matrix_header), "%s", g_screen_link ? "REMINDER" : "OFFLINE");
    if (strcmp(lv_label_get_text(g_card_badge), matrix_header) != 0) lv_label_set_text(g_card_badge, matrix_header);
    return;
#endif
    const char *action = g_card_button_actions[g_card_key_selection];
    char timer[24] = {}, header[224];
    if (strcmp(data.kind, "timer") == 0) {
        int32_t remaining = data.remaining_s;
        if (strcmp(data.status, "running") == 0) remaining -= static_cast<int32_t>(lv_tick_elaps(g_visible_card->received_ms) / 1000);
        if (remaining < 0) remaining = 0;
        snprintf(timer, sizeof(timer), "%02ld:%02ld:%02ld ", static_cast<long>(remaining / 3600), static_cast<long>((remaining / 60) % 60), static_cast<long>(remaining % 60));
    }
    if constexpr (board_profile::reflective_display) {
        if (strcmp(data.kind, "timer") == 0) {
            int32_t remaining = data.remaining_s;
            if (strcmp(data.status, "running") == 0) remaining -= static_cast<int32_t>(lv_tick_elaps(g_visible_card->received_ms) / 1000);
            remaining = remaining < 0 ? 0 : ((remaining + 9) / 10) * 10;
            snprintf(timer, sizeof(timer), "~%02ld:%02ld:%02ld ", static_cast<long>(remaining / 3600), static_cast<long>((remaining / 60) % 60), static_cast<long>(remaining % 60));
        }
    }
    if (g_action_pending) snprintf(header, sizeof(header), "SENT / WAIT FOR CONFIRMATION");
    else if (g_oled_card_note[0]) snprintf(header, sizeof(header), "%s", g_oled_card_note);
    else snprintf(header, sizeof(header), "%s>%s %s%s / SHORT:NEXT HOLD:OK", g_screen_link ? "" : "OFF ", action ? action : "none", timer, data.status);
    if constexpr (board_profile::reflective_display) {
        if (!g_action_pending && !g_oled_card_note[0]) snprintf(header, sizeof(header), "%s>%s %s%s", g_screen_link ? "" : "OFF ", action ? action : "none", timer, data.status);
    }
    // Preserve the horizontal scroll position while an unchanged status is
    // revisited by the shared card timer.
    if (strcmp(lv_label_get_text(g_card_badge), header) != 0) lv_label_set_text(g_card_badge, header);
    if constexpr (board_profile::height == 32) {
        if (!g_card_detail) return;
        lv_obj_t *button = g_card_buttons[g_card_key_selection];
        lv_obj_t *label = button ? lv_obj_get_child(button, 0) : nullptr;
        const char *selection = label ? lv_label_get_text(label) : "操作";
        char title[181], detail[541], route[64] = {}, body[900];
        supported_text(title, sizeof(title), data.title);
        supported_text(detail, sizeof(detail), data.detail);
        if (strcmp(data.kind, "route") == 0) snprintf(route, sizeof(route), "%.1f公里 约%ld分钟 | ", data.distance_m / 1000.0f, static_cast<long>((data.duration_s + 59) / 60));
        snprintf(body, sizeof(body), "%s | 短按换项，长按确认 | %s | %s%s", selection, title, route, detail);
        if (strcmp(lv_label_get_text(g_card_detail), body) != 0) lv_label_set_text(g_card_detail, body);
    }
}

static void perform_screen_action(const char *action) {
    g_last_activity_ms = lv_tick_get();
    restore_display_brightness();
    if (!g_visible_card || g_action_pending) return;
    if (strcmp(action, "close") == 0) { g_visible_card->active = false; delete_screen_card(); render_screen_card(); return; }
    if (!g_screen_link) { card_badge_message("离线 · 操作未提交", "OFFLINE / NOT SENT"); return; }
    if (!control_protocol_queue_screen_action(action, g_visible_card->data.reference_id, g_visible_card->data.reference_revision)) { card_badge_message("未提交 · 请稍后再试", "NOT SENT / RETRY"); return; }
    g_action_pending = true; g_action_started = lv_tick_get(); snprintf(g_action_reference, sizeof(g_action_reference), "%s", g_visible_card->data.reference_id);
    for (auto *button : g_card_buttons) if (button) lv_obj_add_state(button, LV_STATE_DISABLED);
    card_badge_message("请求已提交 · 等待确认", "SENT / WAIT FOR CONFIRMATION");
}

static void screen_button(lv_event_t *event) {
    perform_screen_action(static_cast<const char *>(lv_event_get_user_data(event)));
}

static void focus_card_button() {
    bool focus_changed = false;
    for (int offset = 0; offset < 2; ++offset) {
        const int candidate = (g_card_key_selection + offset) % 2;
        if (g_card_buttons[candidate] && !lv_obj_has_state(g_card_buttons[candidate], LV_STATE_DISABLED)) {
            g_card_key_selection = candidate;
            break;
        }
    }
    for (int index = 0; index < 2; ++index) {
        if (!g_card_buttons[index]) continue;
        if (index == g_card_key_selection) {
            focus_changed = focus_changed || !lv_obj_has_state(g_card_buttons[index], LV_STATE_FOCUSED);
            lv_obj_add_state(g_card_buttons[index], LV_STATE_FOCUSED);
        }
        else lv_obj_remove_state(g_card_buttons[index], LV_STATE_FOCUSED);
        if constexpr (board_profile::text_only) {
            lv_obj_t *label = lv_obj_get_child(g_card_buttons[index], 0);
            if (label) lv_obj_set_style_text_color(label,
                (index == g_card_key_selection) != board_profile::reflective_display ? lv_color_black() : lv_color_white(), 0);
        }
    }
    update_oled_card_header();
    if constexpr (board_profile::reflective_display) {
        if (focus_changed) g_paper_card_token = paper_display_submitted_generation();
    }
}

bool display_ui_cancel_hardware_button() {
    if (!lock_display()) return false;
    g_card_key_active = false;
    bsp_display_unlock();
    return true;
}

bool display_ui_handle_hardware_button(bool pressed) {
    if constexpr (!board_profile::hardware_card_input) return false;
    // A missed display lock must not turn an intended card press into recording.
    if (!lock_display()) return true;
    if (pressed) {
        if (!g_visible_card || !g_card || lv_obj_has_flag(g_card, LV_OBJ_FLAG_HIDDEN)) {
            bsp_display_unlock();
            return false;
        }
        if constexpr (board_profile::reflective_display) {
            if (!paper_display_presented_since(g_paper_card_token)) { bsp_display_unlock(); return true; }
        }
        g_card_key_active = true; g_card_key_started = lv_tick_get();
        snprintf(g_card_key_id, sizeof(g_card_key_id), "%s", g_visible_card->data.card_id);
        snprintf(g_card_key_revision, sizeof(g_card_key_revision), "%s", g_visible_card->data.reference_revision);
        g_last_activity_ms = lv_tick_get(); restore_display_brightness();
        bsp_display_unlock();
        return true;
    }
    const bool consumed = g_card_key_active;
    g_card_key_active = false;
    const bool same_card = consumed && g_visible_card && g_card && !lv_obj_has_flag(g_card, LV_OBJ_FLAG_HIDDEN) &&
        strcmp(g_card_key_id, g_visible_card->data.card_id) == 0 && strcmp(g_card_key_revision, g_visible_card->data.reference_revision) == 0;
    if (same_card && !g_action_pending && (!board_profile::reflective_display || paper_display_presented_since(g_paper_card_token))) {
        const uint32_t duration = lv_tick_elaps(g_card_key_started);
        if (duration >= 600 && g_card_buttons[g_card_key_selection] && !lv_obj_has_state(g_card_buttons[g_card_key_selection], LV_STATE_DISABLED)) {
            perform_screen_action(g_card_button_actions[g_card_key_selection]);
        } else if (duration >= 50) {
            g_card_key_selection = (g_card_key_selection + 1) % 2;
            focus_card_button();
        }
    }
    bsp_display_unlock();
    return consumed;
}

static void add_card_button(int index, const char *text, const char *action) {
    lv_obj_t *button = lv_button_create(g_card); g_card_buttons[index] = button;
    g_card_button_actions[index] = action;
    lv_obj_set_size(button, DISPLAY_LAYOUT.button_width, DISPLAY_LAYOUT.button_height);
    if constexpr (board_profile::compact_display) lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_pos(button, DISPLAY_LAYOUT.buttons_x + index * (DISPLAY_LAYOUT.button_width + DISPLAY_LAYOUT.button_gap), DISPLAY_LAYOUT.buttons_y);
    lv_obj_set_style_bg_color(button, lv_color_hex(index == 0 ? 0x4c3876 : 0x20202c), 0);
    if constexpr (board_profile::hardware_card_input) {
        lv_obj_set_style_outline_color(button, lv_color_hex(0xe5edf1), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUSED);
    }
    lv_obj_add_event_cb(button, screen_button, LV_EVENT_CLICKED, const_cast<char *>(action));
    lv_obj_t *label = lv_label_create(button); lv_obj_set_style_text_font(label, kUiFont, 0); lv_label_set_text(label, text); lv_obj_center(label);
}

static lv_obj_t *card_label(int y, int height, uint32_t color) {
    lv_obj_t *label = lv_label_create(g_card); lv_obj_set_width(label, DISPLAY_LAYOUT.content_width); lv_obj_set_height(label, height); lv_obj_set_pos(label, DISPLAY_LAYOUT.content_x, y);
    lv_obj_set_style_text_font(label, kUiFont, 0); lv_obj_set_style_text_color(label, lv_color_hex(color), 0); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_label_set_long_mode(label, LV_LABEL_LONG_DOT); return label;
}

static void update_card_clock() {
    if constexpr (board_profile::text_only) { update_oled_card_header(); return; }
    if (!g_visible_card || !g_card_clock || strcmp(g_visible_card->data.kind, "timer") != 0) return;
    const ScreenCardData &data = g_visible_card->data;
    int32_t remaining = data.remaining_s;
    if (strcmp(data.status, "running") == 0) remaining -= static_cast<int32_t>(lv_tick_elaps(g_visible_card->received_ms) / 1000);
    if (remaining < 0) remaining = 0;
    char clock[40]; snprintf(clock, sizeof(clock), "%02ld:%02ld:%02ld", static_cast<long>(remaining / 3600), static_cast<long>((remaining / 60) % 60), static_cast<long>(remaining % 60));
    lv_label_set_text(g_card_clock, clock);
}

static void confirm_reminder_display(StoredCard *card) {
    if constexpr (board_profile::reflective_display) {
        if (!paper_display_presented_since(g_paper_card_token)) return;
        if (!card->shown) {
            card->shown = true; card->shown_ms = lv_tick_get();
            control_protocol_queue_screen_action("receipt", card->data.card_id, "shown");
        }
    }
    if (strcmp(card->data.kind, "reminder") != 0 || !card->data.notification_lease[0]) return;
    if (control_protocol_queue_screen_action("reminder_ack", card->data.reference_id, card->data.notification_lease)) { card->data.notification_lease[0] = '\0'; control_protocol_queue_screen_action("cue_reminder", "", ""); }
}

static bool operation_result_card_disabled(const ScreenCardData &card) {
    return strcmp(card.kind, "timer") != 0 && strcmp(card.kind, "reminder") != 0 && card.status[0] && strcmp(card.status, "clear") != 0;
}

static void prune_info_cards(const char *clear_card_id = nullptr) {
    if (g_info_card.active && (operation_result_card_disabled(g_info_card.data) || (clear_card_id && strcmp(g_info_card.data.card_id, clear_card_id) == 0))) g_info_card.active = false;
    size_t kept = 0;
    for (size_t index = 0; index < g_info_pending_count; ++index) {
        const auto &card = g_info_pending[index];
        if (operation_result_card_disabled(card.data) || (clear_card_id && strcmp(card.data.card_id, clear_card_id) == 0)) continue;
        g_info_pending[kept++] = card;
    }
    for (size_t index = kept; index < g_info_pending_count; ++index) g_info_pending[index] = {};
    g_info_pending_count = kept;
}

static void render_screen_card(bool force) {
    if (!g_title) return;
    // 先删除旧可见对象再压缩队列，防止缓存中的操作结果在语音结束或重连后复活。
    if (g_visible_card && operation_result_card_disabled(g_visible_card->data)) delete_screen_card();
    prune_info_cards();
    while (!g_info_card.active && g_info_pending_count) {
        g_info_card = g_info_pending[0];
        for (size_t index = 1; index < g_info_pending_count; ++index) g_info_pending[index - 1] = g_info_pending[index];
        --g_info_pending_count;
        if (lv_tick_elaps(g_info_card.received_ms) >= 120000) { g_info_card.active = false; control_protocol_queue_screen_action("receipt", g_info_card.data.card_id, "expired"); }
    }
    StoredCard *selected = g_reminder_card.active ? &g_reminder_card : g_info_card.active ? &g_info_card : g_timer_card.active ? &g_timer_card : nullptr;
    // 聆听待命允许看结果；收音文字、思考和播放仍保留原语音状态。
    const bool busy = g_state == UiState::Speaking || g_state == UiState::Thinking || g_state == UiState::Executing || g_state == UiState::Error || g_state == UiState::Updating || g_pairing_guide || g_notice_active || g_touch_listen_held;
    const bool timer_selected = selected == &g_timer_card && !busy;
    if (timer_selected) { lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_detail, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_remove_flag(g_title, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_detail, LV_OBJ_FLAG_HIDDEN); }
    if (busy || !selected) { if (g_card) lv_obj_add_flag(g_card, LV_OBJ_FLAG_HIDDEN); return; }
    if (!board_profile::reflective_display && !selected->shown) { selected->shown = true; selected->shown_ms = lv_tick_get(); }
    if (g_visible_card == selected && g_card && !force && !selected->dirty) {
        const bool was_hidden = lv_obj_has_flag(g_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_card, LV_OBJ_FLAG_HIDDEN);
        if (was_hidden) {
            if constexpr (board_profile::reflective_display) g_paper_card_token = paper_display_submitted_generation();
            else control_protocol_queue_screen_action("receipt", selected->data.card_id, "shown");
        }
        confirm_reminder_display(selected); update_card_clock(); return;
    }
    delete_screen_card(); g_visible_card = selected;
    if constexpr (board_profile::reflective_display) g_paper_card_token = paper_display_submitted_generation();
    g_card = lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(g_card); lv_obj_set_size(g_card, board_profile::width, board_profile::height); lv_obj_set_style_bg_color(g_card, lv_color_hex(0x09090f), 0); lv_obj_set_style_bg_opa(g_card, LV_OPA_COVER, 0); lv_obj_remove_flag(g_card, LV_OBJ_FLAG_SCROLLABLE);
    // 倒计时沿已选预设显示点云环；只有文字与按钮叠在画布上，不再用整页卡片盖住点云。
    if (timer_selected) { lv_obj_set_style_bg_opa(g_card, LV_OPA_TRANSP, 0); lv_obj_add_flag(g_title, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_detail, LV_OBJ_FLAG_HIDDEN); }
    g_card_badge = card_label(DISPLAY_LAYOUT.badge_y, DISPLAY_LAYOUT.badge_height, 0xb8a4ff);
    const auto &data = selected->data;
    const char *badge = strcmp(data.kind, "timer") == 0 ? (strcmp(data.status, "paused") == 0 ? "计时已暂停" : strcmp(data.status, "finished") == 0 ? "时间到了" : "计时中") : strcmp(data.kind, "reminder") == 0 ? "提醒 · 点击稍后或知道了" : strcmp(data.kind, "weather") == 0 ? "天气预报 · 读取快照" : strcmp(data.kind, "route") == 0 ? "路线预览 · 非实时导航" : "信息卡片";
    if (strcmp(data.status, "error") == 0) badge = "操作未完成 · 请核查";
    if (strcmp(data.status, "unconfirmed") == 0) badge = "目标状态未确认";
    if (strcmp(data.status, "processing") == 0) badge = "正在处理 · 请稍候";
    if (strcmp(data.status, "completed") == 0) badge = "操作已返回结果";
    if (strcmp(data.status, "saved") == 0) badge = "已保存 · 生效时间见说明";
    if (strcmp(data.status, "verified") == 0) badge = "本次连接检查通过";
    if (strcmp(data.status, "empty") == 0) badge = "本次没有匹配结果";
    if (strcmp(data.status, "pending_confirmation") == 0) badge = "等待后台明确确认";
    if (strcmp(data.status, "pending_authorization") == 0) badge = "等待浏览器授权";
    if (strcmp(data.status, "authorized") == 0) badge = "已有授权 · 有效性待核对";
    if (strcmp(data.status, "unconfigured") == 0) badge = "缺少配置 · 请到后台设置";
    if (strcmp(data.status, "blocked") == 0) badge = "操作被拦截 · 请核对权限";
    if (strcmp(data.status, "unknown") == 0) badge = "结果待核实 · 停止重试";
    if (strcmp(data.status, "cancelled") == 0) badge = "操作已取消";
    if (strcmp(data.status, "error") == 0) lv_obj_set_style_text_color(g_card_badge, lv_color_hex(0xff9c9c), 0);
    lv_label_set_text(g_card_badge, g_screen_link ? badge : strcmp(data.kind, "timer") == 0 ? "离线 · 本地计时" : "连接已断开 · 旧快照");
    char title[181], detail[541]; supported_text(title, sizeof(title), data.title); supported_text(detail, sizeof(detail), data.detail);
    if (strcmp(data.kind, "route") == 0) { char instruction[361]; supported_text(instruction, sizeof(instruction), data.detail); snprintf(detail, sizeof(detail), "%.1f 公里 · 约 %ld 分钟\n%s", data.distance_m / 1000.0f, static_cast<long>((data.duration_s + 59) / 60), instruction); }
    g_card_title = card_label(DISPLAY_LAYOUT.card_title_y, DISPLAY_LAYOUT.card_title_height, 0xeeeaf7); lv_label_set_text(g_card_title, title);
    g_card_clock = card_label(DISPLAY_LAYOUT.clock_y, DISPLAY_LAYOUT.clock_height, 0xd2c1ff); update_card_clock();
    const int detail_y = strcmp(data.kind, "route") == 0 ? DISPLAY_LAYOUT.route_detail_y : strcmp(data.kind, "timer") == 0 ? DISPLAY_LAYOUT.timer_detail_y : DISPLAY_LAYOUT.card_detail_y;
    g_card_detail = card_label(detail_y, DISPLAY_LAYOUT.detail_bottom - detail_y, 0xb0a9bf); lv_label_set_text(g_card_detail, detail);
    if (timer_selected) {
        lv_obj_set_y(g_card_badge, DISPLAY_LAYOUT.timer_badge_y);
        lv_obj_set_y(g_card_clock, DISPLAY_LAYOUT.timer_clock_y); lv_obj_set_height(g_card_clock, board_profile::height < 300 ? 22 : 32);
        lv_obj_set_y(g_card_title, DISPLAY_LAYOUT.timer_title_y); lv_obj_set_height(g_card_title, 22);
        lv_obj_set_y(g_card_detail, DISPLAY_LAYOUT.timer_small_detail_y); lv_obj_set_height(g_card_detail, 18);
    }
    if constexpr (board_profile::width <= 240) {
        if constexpr (board_profile::compact_display) {
            const int badge_x = board_profile::round ? 30 : DISPLAY_LAYOUT.content_x;
            lv_obj_set_x(g_card_badge, badge_x);
            lv_obj_set_width(g_card_badge, board_profile::width - 2 * badge_x);
        }
        lv_label_set_long_mode(g_card_badge, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_long_mode(g_card_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
        // Keep the actions fixed while long reminder/route text can be dragged
        // vertically inside its own viewport at the original16px font size.
#if defined(ASSISTANT_BOARD_SPOTPEAR)
        if (bsp_display_touch_available()) {
#else
        if constexpr (board_profile::has_touch) {
#endif
            lv_obj_t *viewport = lv_obj_create(g_card);
            lv_obj_remove_style_all(viewport);
            lv_obj_set_pos(viewport, DISPLAY_LAYOUT.content_x, timer_selected ? DISPLAY_LAYOUT.timer_small_detail_y : detail_y);
            lv_obj_set_size(viewport, DISPLAY_LAYOUT.content_width, timer_selected ? 18 : DISPLAY_LAYOUT.detail_bottom - detail_y);
            lv_obj_set_scroll_dir(viewport, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_AUTO);
            lv_obj_set_parent(g_card_detail, viewport);
            lv_obj_set_pos(g_card_detail, 0, 0);
            lv_obj_set_height(g_card_detail, LV_SIZE_CONTENT);
            lv_label_set_long_mode(g_card_detail, LV_LABEL_LONG_WRAP);
        } else {
            lv_label_set_long_mode(g_card_detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }
    } else if (!board_profile::text_only && strcmp(data.kind, "route") == 0) draw_card_arrow(g_card, data.direction);
    if constexpr (board_profile::width == 64) {
        //64px panels have no local action surface. Existing voice/console
        // requests remain authoritative; text does not become a fake button.
    } else if (strcmp(data.kind, "timer") == 0 && (strcmp(data.status, "running") == 0 || strcmp(data.status, "paused") == 0)) {
        add_card_button(0, strcmp(data.status, "paused") == 0 ? "继续" : "暂停", strcmp(data.status, "paused") == 0 ? "resume" : "pause"); add_card_button(1, "取消", "cancel");
    } else if (strcmp(data.kind, "reminder") == 0) { add_card_button(0, (board_profile::compact_display || (board_profile::round && board_profile::width <= 240)) ? "稍后" : "稍后5分钟", "snooze"); add_card_button(1, "知道了", "close"); if (!data.reference_revision[0]) lv_obj_add_state(g_card_buttons[0], LV_STATE_DISABLED); }
    else { add_card_button(0, "关闭", strcmp(data.kind, "timer") == 0 ? "cancel" : "close"); }
    if constexpr (board_profile::hardware_card_input) {
        focus_card_button();
#if defined(ASSISTANT_BOARD_SENSECAP)
        lv_label_set_text(g_card_badge, g_screen_link ? "旋钮短按选择，长按松手确认" : "离线 · 旋钮短按选择，长按确认");
#elif defined(ASSISTANT_BOARD_LCD_085)
        lv_label_set_text(g_card_badge, g_screen_link ? "PLUS短按选择，长按松手确认" : "离线 · PLUS短按选择，长按确认");
#elif defined(ASSISTANT_BOARD_XINGZHI_METAL) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_ATK_BOX3)
        lv_label_set_text(g_card_badge, g_screen_link ? "中键短按选择，长按松手确认" : "离线 · 中键短按选择，长按确认");
#elif defined(ASSISTANT_BOARD_CARDPUTER_ADV)
        lv_label_set_text(g_card_badge, g_screen_link ? "Enter短按选择，长按松手确认" : "离线 · Enter短按选择，长按确认");
#elif defined(ASSISTANT_BOARD_ATOM_S3_BASE)
        lv_label_set_text(g_card_badge, g_screen_link ? "屏幕键短按选择，长按松手确认" : "离线 · 屏幕键短按选择，长按确认");
#else
        lv_label_set_text(g_card_badge, g_screen_link ? "BOOT短按选择，长按松手确认" : "离线 · BOOT短按选择，长按确认");
#endif
    }
    if constexpr (board_profile::text_only) {
        lv_obj_set_style_bg_color(g_card, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(g_card, LV_OPA_COVER, 0);
        lv_obj_set_pos(g_card_badge, 0, 0); lv_obj_set_size(g_card_badge, board_profile::width, 12);
        lv_obj_set_style_text_font(g_card_badge, kHeaderFont, 0);
        lv_obj_set_style_text_color(g_card_badge, lv_color_white(), 0);
        lv_obj_add_flag(g_card_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(g_card_title, lv_color_white(), 0);
        lv_obj_set_style_text_color(g_card_detail, lv_color_white(), 0);
        lv_obj_set_style_text_line_space(g_card_detail, 0, 0);
        if constexpr (board_profile::height == 32) {
            lv_obj_add_flag(g_card_title, LV_OBJ_FLAG_HIDDEN);
            char combined[800]; snprintf(combined, sizeof(combined), "%s | %s", title, detail);
            lv_obj_set_pos(g_card_detail, 0, 14); lv_obj_set_size(g_card_detail, board_profile::width, 17);
            lv_label_set_text(g_card_detail, combined);
        } else {
            lv_obj_set_pos(g_card_title, 0, 13); lv_obj_set_size(g_card_title, board_profile::width, 17);
            const int body_y = board_profile::manual_audio_only ? 34 : 30;
            const int body_height = board_profile::manual_audio_only ? board_profile::height - 60 : 17;
            lv_obj_set_pos(g_card_detail, 0, body_y); lv_obj_set_size(g_card_detail, board_profile::width, body_height);
        }
        lv_label_set_long_mode(g_card_detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
        for (auto *button : g_card_buttons) {
            if (!button) continue;
            lv_obj_set_style_pad_all(button, 0, 0);
            lv_obj_set_style_border_width(button, 0, 0);
            lv_obj_set_style_radius(button, 0, 0);
            lv_obj_set_style_bg_color(button, lv_color_black(), 0);
            lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_FOCUSED);
            lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
        }
        update_oled_card_header();
#if defined(ASSISTANT_BOARD_MATRIX)
        lv_obj_set_pos(g_card_detail, 0, 34); lv_obj_set_size(g_card_detail, 64, 20);
        lv_label_set_long_mode(g_card_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_long_mode(g_card_detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t *hint = lv_label_create(g_card);
        lv_obj_set_style_text_font(hint, kHeaderFont, 0);
        lv_obj_set_style_text_color(hint, lv_color_white(), 0);
        lv_label_set_text(hint, "VOICE / WEB"); lv_obj_set_pos(hint, 0, 54);
#endif
    }
    if constexpr (board_profile::reflective_display) {
        lv_obj_set_style_bg_color(g_card, lv_color_white(), 0);
        lv_obj_set_size(g_card_badge, board_profile::width, DISPLAY_LAYOUT.badge_height);
        lv_obj_set_style_text_color(g_card_badge, lv_color_black(), 0);
        lv_label_set_long_mode(g_card_badge, LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(g_card_title, PAPER_CONTENT_X, DISPLAY_LAYOUT.card_title_y); lv_obj_set_size(g_card_title, PAPER_CONTENT_WIDTH, DISPLAY_LAYOUT.card_title_height);
        lv_obj_set_style_text_color(g_card_title, lv_color_black(), 0);
        lv_label_set_long_mode(g_card_title, LV_LABEL_LONG_DOT);
        g_paper_body_viewport = lv_obj_create(g_card);
        lv_obj_remove_style_all(g_paper_body_viewport);
        lv_obj_set_pos(g_paper_body_viewport, PAPER_CONTENT_X, PAPER_CARD_BODY_Y); lv_obj_set_size(g_paper_body_viewport, PAPER_CONTENT_WIDTH, PAPER_CARD_BODY_HEIGHT);
        lv_obj_remove_flag(g_paper_body_viewport, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_parent(g_card_detail, g_paper_body_viewport);
        lv_obj_set_pos(g_card_detail, 0, 0); lv_obj_set_width(g_card_detail, PAPER_CONTENT_WIDTH);
        lv_obj_set_height(g_card_detail, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(g_card_detail, lv_color_black(), 0);
        lv_label_set_long_mode(g_card_detail, LV_LABEL_LONG_WRAP);
        lv_point_t title_size = {};
        lv_text_get_size(&title_size, title, kUiFont, 0, 0, PAPER_CONTENT_WIDTH, LV_TEXT_FLAG_NONE);
        if (title_size.y > DISPLAY_LAYOUT.card_title_height) {
            char body[800]; snprintf(body, sizeof(body), "%s\n%s", title, detail);
            lv_label_set_text(g_card_detail, body);
        }
        lv_obj_t *hint = lv_label_create(g_card);
        lv_obj_set_style_text_font(hint, kUiFont, 0); lv_obj_set_style_text_color(hint, lv_color_black(), 0);
        lv_label_set_text(hint, "短按换项，长按确认"); lv_obj_set_pos(hint, PAPER_CONTENT_X, DISPLAY_LAYOUT.buttons_y - PAPER_HINT_HEIGHT - 2);
        for (auto *button : g_card_buttons) {
            if (!button) continue;
            lv_obj_set_style_transition(button, nullptr, LV_PART_MAIN);
            lv_obj_set_style_bg_color(button, lv_color_white(), 0);
            lv_obj_set_style_bg_color(button, lv_color_black(), LV_STATE_FOCUSED);
            lv_obj_t *label = lv_obj_get_child(button, 0);
            if (label) lv_obj_set_style_text_color(label, lv_obj_has_state(button, LV_STATE_FOCUSED) ? lv_color_white() : lv_color_black(), 0);
        }
    } else control_protocol_queue_screen_action("receipt", data.card_id, "shown");
    confirm_reminder_display(selected);
    if ((strcmp(data.status, "error") == 0 || strcmp(data.status, "unknown") == 0 || strcmp(data.status, "blocked") == 0) && strcmp(g_last_error_cue, data.card_id) != 0) { snprintf(g_last_error_cue, sizeof(g_last_error_cue), "%s", data.card_id); control_protocol_queue_screen_action("cue_error", "", ""); }
    selected->dirty = false;
}

const char *display_ui_screen_card(const ScreenCardData &card) {
    if (operation_result_card_disabled(card)) return "expired";
    if (!g_title || !lock_display()) return "queued";
    g_last_activity_ms = lv_tick_get();
    restore_display_brightness();
    StoredCard *target = strcmp(card.kind, "timer") == 0 ? &g_timer_card : strcmp(card.kind, "reminder") == 0 ? &g_reminder_card : &g_info_card;
    if (strcmp(card.status, "clear") == 0) { prune_info_cards(card.card_id); render_screen_card(); bsp_display_unlock(); return "expired"; }
    if (target == &g_info_card && target->active && strcmp(target->data.card_id, card.card_id) != 0 && strcmp(target->data.status, "processing") != 0) {
        // 结果优先于后续工具的“处理中”；最多再保留3张结果，防止连续工具吞掉前一结果。
        if (strcmp(card.status, "processing") == 0) { bsp_display_unlock(); return "expired"; }
        for (size_t index = 0; index < g_info_pending_count; ++index) {
            if (strcmp(g_info_pending[index].data.card_id, card.card_id) == 0) { g_info_pending[index].data = card; g_info_pending[index].dirty = true; bsp_display_unlock(); return "queued"; }
        }
        if (g_info_pending_count == 3) { control_protocol_queue_screen_action("receipt", g_info_pending[0].data.card_id, "expired"); for (size_t index = 1; index < 3; ++index) g_info_pending[index - 1] = g_info_pending[index]; --g_info_pending_count; }
        g_info_pending[g_info_pending_count++] = {card, lv_tick_get(), 0, true, false, true};
        bsp_display_unlock(); return "queued";
    }
    if (target == &g_reminder_card && target->active && strcmp(target->data.card_id, card.card_id) != 0) { bsp_display_unlock(); return "queued"; }
    const bool same = target->active && strcmp(target->data.card_id, card.card_id) == 0;
    const bool changed = !same || strcmp(target->data.status, card.status) != 0 || strcmp(target->data.title, card.title) != 0 || strcmp(target->data.detail, card.detail) != 0;
    char previous_revision[65]; snprintf(previous_revision, sizeof(previous_revision), "%s", target->data.reference_revision);
    target->data = card; target->received_ms = lv_tick_get(); target->active = strcmp(card.status, "cancelled") != 0;
    if (changed) { target->shown = false; target->shown_ms = 0; target->dirty = true; }
    if (target == &g_reminder_card && same && !card.reference_revision[0]) snprintf(target->data.reference_revision, sizeof(target->data.reference_revision), "%s", previous_revision);
    const bool finished = strcmp(card.kind, "timer") == 0 && strcmp(card.status, "finished") == 0;
    if (finished && strcmp(g_completed_timer, card.reference_id) != 0) { snprintf(g_completed_timer, sizeof(g_completed_timer), "%s", card.reference_id); control_protocol_queue_screen_action("cue_timer", "", ""); }
    render_screen_card(changed);
    const bool shown = g_visible_card == target && g_card && !lv_obj_has_flag(g_card, LV_OBJ_FLAG_HIDDEN) && (!board_profile::reflective_display || target->shown);
    bsp_display_unlock(); return shown ? "shown" : "queued";
}

void display_ui_screen_link(bool connected) {
    if (!g_title || !lock_display()) return;
    g_screen_link = connected; g_action_pending = false; render_screen_card(true); bsp_display_unlock();
}

void display_ui_reminder_revision(const char *reference_id, const char *revision) {
    if (!lock_display()) return;
    if (g_reminder_card.active && strcmp(g_reminder_card.data.reference_id, reference_id) == 0) { snprintf(g_reminder_card.data.reference_revision, sizeof(g_reminder_card.data.reference_revision), "%s", revision); render_screen_card(true); }
    bsp_display_unlock();
}

void display_ui_screen_action_result(const char *reference_id, bool accepted) {
    if (!lock_display()) return;
    if (strcmp(g_action_reference, reference_id) == 0) { g_action_pending = false; if (accepted && g_reminder_card.active && strcmp(g_reminder_card.data.reference_id, reference_id) == 0) g_reminder_card.active = false; render_screen_card(true); if (!accepted && g_card_badge) card_badge_message("操作未完成 · 请刷新后台", "FAILED / CHECK CONSOLE"); }
    bsp_display_unlock();
}

static void expression_timer(lv_timer_t *) {
    g_display_timer_ms.store(esp_timer_get_time() / 1000, std::memory_order_relaxed);
#if !defined(ASSISTANT_BOARD_LCD_EV)
    if (g_screen_off_seconds && g_state == UiState::Idle && !g_card && !g_notice_active && lv_tick_elaps(g_last_activity_ms) >= g_screen_off_seconds * 1000U) {
        if (!g_screen_dimmed && set_display_brightness(0, "idle_timeout") == ESP_OK) g_screen_dimmed = true;
    }
#endif
    // 回调在LVGL锁内，固定PSRAM缓冲循环复用，不按帧分配对象。
    const int64_t started = esp_timer_get_time();
    const uint32_t now = lv_tick_get();
    if (g_state == UiState::Error && g_screen_link && lv_tick_elaps(g_error_started) >= 8000) apply_state(UiState::Idle, "错误详情请到后台核查，可重新发起请求");
    if (g_notice_active && lv_tick_elaps(g_notice_started) >= 5000) { g_notice_active = false; apply_state(g_after_notification, g_after_detail); }
    if (!g_screen_last_tick || now - g_screen_last_tick >= 1000) {
        g_screen_last_tick = now;
        StoredCard *cards[3] = {&g_info_card, &g_reminder_card, &g_timer_card};
        for (auto *card : cards) {
            if (!card->active) continue;
            if (strcmp(card->data.kind, "timer") == 0 && strcmp(card->data.status, "running") == 0) {
                if (lv_tick_elaps(card->received_ms) / 1000 >= static_cast<uint32_t>(card->data.remaining_s)) {
                    snprintf(card->data.status, sizeof(card->data.status), "finished"); snprintf(card->data.detail, sizeof(card->data.detail), "计时结束 · 等待后台同步"); card->data.remaining_s = 0;
                    card->dirty = true;
                    card->received_ms = now; card->data.ttl_s = 30;
                    if (strcmp(g_completed_timer, card->data.reference_id) != 0) { snprintf(g_completed_timer, sizeof(g_completed_timer), "%s", card->data.reference_id); control_protocol_queue_screen_action("cue_timer", "", ""); }
                    render_screen_card(true);
                }
            } else if (strcmp(card->data.status, "paused") != 0 && strcmp(card->data.status, "finished") != 0 && ((card->shown && lv_tick_elaps(card->shown_ms) / 1000 >= card->data.ttl_s) || (!card->shown && lv_tick_elaps(card->received_ms) >= 120000))) { card->active = false; control_protocol_queue_screen_action("receipt", card->data.card_id, "expired"); }
        }
        if (g_action_pending && now - g_action_started > 8000) { g_action_pending = false; render_screen_card(true); if (g_card_badge) card_badge_message("结果待核实 · 可刷新后台", "UNKNOWN / CHECK CONSOLE"); }
        render_screen_card();
    }
    if constexpr (board_profile::reflective_display) {
        const bool visible = g_card && !lv_obj_has_flag(g_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *body = visible ? g_card_detail : g_detail;
        if (body && !g_pairing_guide && lv_tick_elaps(g_paper_page_started) >= 10000) {
            const int page_height = visible ? PAPER_CARD_BODY_HEIGHT : PAPER_IDLE_BODY_HEIGHT;
            const int pages = (lv_obj_get_height(body) + page_height - 1) / page_height;
            if (pages > 1) {
                g_paper_page = (g_paper_page + 1) % pages;
                lv_obj_set_y(body, -g_paper_page * page_height);
                if (visible) g_paper_card_token = paper_display_submitted_generation();
            }
            g_paper_page_started = lv_tick_get();
        }
    }
    if constexpr (board_profile::text_only) return;
    const bool visible_card = g_card && !lv_obj_has_flag(g_card, LV_OBJ_FLAG_HIDDEN);
    const bool timer_visible = visible_card && g_visible_card == &g_timer_card;
    if (visible_card && !timer_visible) return;
    // 待机时优先保障离线唤醒的实时预算，减少大画布刷新与模型争用PSRAM。
    if (g_state == UiState::Idle && g_previous_frame && now - g_previous_frame < 100) return;
    const uint32_t elapsed = g_previous_frame ? now - g_previous_frame : 40;
    g_previous_frame = now;
    const float delta = (elapsed > 120 ? 120 : elapsed) / 1000.0f;
    const float smooth = delta * 3.0f;
    g_timer_cloud += ((timer_visible ? 1.0f : 0.0f) - g_timer_cloud) * smooth;
    g_pulse += (0.0f - g_pulse) * smooth;
    g_listen += ((g_state == UiState::Listening ? 1.0f : 0.0f) - g_listen) * smooth;
    g_think += ((g_state == UiState::Thinking || g_state == UiState::Executing || g_state == UiState::Connecting ? 1.0f : 0.0f) - g_think) * smooth;
    g_speak += ((g_state == UiState::Speaking ? 1.0f : 0.0f) - g_speak) * smooth;
    const bool free_state = g_state == UiState::Idle || g_state == UiState::Listening || g_state == UiState::Speaking;
    g_free_cloud += ((free_state ? 1.0f : 0.0f) - g_free_cloud) * smooth;
    g_reminder += ((g_state == UiState::Notification || g_state == UiState::Alert ? 1.0f : 0.0f) - g_reminder) * smooth;
    g_nav_icon_w += ((g_nav_icon ? 1.0f : 0.0f) - g_nav_icon_w) * smooth;
    const float target_level = g_state == UiState::Speaking ? fminf(g_audio_level / 55.0f, 1.0f) : 0.0f;
    g_envelope += (target_level - g_envelope) * 0.25f;
    const float listening_motion = 1.0f - g_listen * 0.85f;
    g_phase = fmodf(g_phase + delta * listening_motion, TAU * 100.0f);
    g_rotation = fmodf(g_rotation + delta * (0.16f + g_think * 0.09f - g_free_cloud * (0.09f + 0.025f * wave(g_phase * 0.19f))) * (1.0f - g_listen * 0.95f), TAU);
    const float rotation_sin = wave(g_rotation), rotation_cos = wave(g_rotation + TAU / 4);
    const float pitch = 0.28f + wave(g_phase * 0.17f) * 0.18f * listening_motion;
    const float pitch_sin = wave(pitch), pitch_cos = wave(pitch + TAU / 4);
    const uint32_t color = g_nav_icon ? 0xb8a4ff : timer_visible ? (strcmp(g_timer_card.data.status, "finished") == 0 ? 0xe9cea3 : 0xb8a4ff) : state_color(g_state);
    g_cloud_red += (((color >> 16) & 255) - g_cloud_red) * smooth;
    g_cloud_green += (((color >> 8) & 255) - g_cloud_green) * smooth;
    g_cloud_blue += ((color & 255) - g_cloud_blue) * smooth;
    for (int index = 0; index < 32; ++index) {
        // 保留最暗层次，同时抬高低光粒子的可见度；最高层仍保持满亮度。
        const float intensity = 0.18f + index * (0.82f / 31.0f);
        const uint8_t red = g_cloud_red * intensity, green = g_cloud_green * intensity, blue = g_cloud_blue * intensity;
        g_palette[index] = lv_color_make(red, green, blue);
    }
    memset(g_pixels, 0, CLOUD_WIDTH * CLOUD_HEIGHT * sizeof(uint16_t));
    // 积分速度保持相位连续，避免切换状态时累计时间乘新速度造成点位突跳。
    g_flow_phase = fmodf(g_flow_phase + delta * (0.44f - g_listen * 0.38f + g_speak * 0.15f), TAU * 100.0f);
    const float flow_phase = g_flow_phase;
    const float flow_strength = g_free_cloud * (11.0f - g_listen * 9.0f + g_speak * (5.0f + g_envelope * 10.0f));
    const float drift_x = g_free_cloud * listening_motion * (8.0f * wave(g_phase * 0.27f) + 4.0f * wave(g_phase * 0.53f));
    const float drift_y = g_free_cloud * listening_motion * 5.0f * wave(g_phase * 0.31f + 1.7f);
    for (int index = 0; index < PARTICLE_COUNT; ++index) {
        const CloudParticle &particle = g_particles[index];
        const float uneven = 1.0f + 0.13f * wave(particle.angle * 3 + particle.y * 5 + g_phase * 0.55f)
            + 0.095f * wave(particle.phi * 5 - particle.angle * 2 - g_phase * 0.45f) + 0.055f * wave(particle.y * 9 + g_phase * 0.8f);
        const float voice = g_speak * (0.025f + g_envelope * 0.12f) * wave(particle.angle * 5 + g_phase * 7 + particle.y * 4);
        const float radius = (77 + g_listen * 8) * (uneven + voice + g_reminder * 0.04f * wave(g_phase * 2)) * particle.layer * (1.0f + 0.10f * g_pulse);
        const float ring_phase = particle.phi * 7 + g_phase * 0.2f;
        const float ring = 55 + 21 * wave(ring_phase + TAU / 4), mix = g_think * 0.76f;
        float x = particle.x * radius * (1 - mix) + wave(particle.angle + TAU / 4) * ring * mix;
        float y = particle.y * radius * (1 - mix) + wave(particle.angle) * ring * 0.85f * mix;
        float z = particle.z * radius * (1 - mix) + wave(ring_phase) * 24 * mix;
        // 连续流场和体积密度打破球壳轮廓；所有相位确定且连续，不逐帧随机闪点。
        const float seed = index * 1.618034f;
        const float volume = index % 13 == 0 ? 1.06f : 0.38f + 0.60f * (0.5f + 0.5f * wave(seed * 2.17f));
        const float flow_x = wave(particle.y * 3.1f + particle.z * 2.7f + flow_phase) + 0.45f * wave(particle.z * 5.3f - flow_phase * 0.63f);
        const float flow_y = wave(particle.z * 3.7f - particle.x * 2.3f - flow_phase * 0.81f);
        const float flow_z = wave(particle.x * 3.9f + particle.y * 2.1f + flow_phase * 0.69f);
        x *= 1.0f + g_free_cloud * (volume - 1.0f);
        y *= 1.0f + g_free_cloud * (volume * 0.86f - 1.0f);
        z *= 1.0f + g_free_cloud * (volume - 1.0f);
        x += flow_x * flow_strength + drift_x;
        y += flow_y * flow_strength * 0.75f + drift_y;
        z += flow_z * flow_strength * 0.8f;
        const float twist = 0.15f * listening_motion * wave(y * 0.022f + g_phase * 0.4f);
        const float twist_sin = wave(twist), twist_cos = wave(twist + TAU / 4);
        const float twisted_x = x * twist_cos - z * twist_sin;
        z = x * twist_sin + z * twist_cos; x = twisted_x * (1 + g_listen * 0.04f); y *= 1 - g_listen * 0.03f;
        const float rotated_x = x * rotation_cos - z * rotation_sin;
        const float rotated_z = x * rotation_sin + z * rotation_cos;
        const float rotated_y = y * pitch_cos - rotated_z * pitch_sin;
        const float depth = y * pitch_sin + rotated_z * pitch_cos;
        const float scale = 420.0f / (420.0f + depth);
        const int light = 5 + static_cast<int>((scale - 0.78f) * 48 + g_free_cloud * 2.0f * wave(seed + g_phase * 0.63f)) + index % 4;
        CloudProjection &projection = g_projected[index];
        projection = {static_cast<int16_t>(184 + rotated_x * scale), static_cast<int16_t>(120 + rotated_y * scale), static_cast<uint8_t>(light < 0 ? 0 : light > 31 ? 31 : light)};
        if (g_timer_cloud > 0.001f) {
            const float angle = -TAU / 4 + TAU * 0.965f * index / (PARTICLE_COUNT - 1);
            const float radius = 70.0f + 1.7f * wave(seed * 3.1f) + 0.6f * wave(seed + g_phase * 0.3f);
            const float ring_x = 184 + wave(angle + TAU / 4) * radius;
            const float ring_y = 120 + wave(angle) * radius;
            projection.x += static_cast<int16_t>((ring_x - projection.x) * g_timer_cloud);
            projection.y += static_cast<int16_t>((ring_y - projection.y) * g_timer_cloud);
            const int ring_light = 12 + index % 18;
            projection.light += static_cast<int>((ring_light - projection.light) * g_timer_cloud);
        }
        if (g_nav_icon_w > 0.001f && g_nav_icon >= 1 && g_nav_icon <= NAV_ICON_COUNT && g_icon_sets[g_nav_icon - 1]) {
            // 图标目标位：路径采样点 + 有序微噪呼吸，保持点云质感
            const int16_t *icon = g_icon_sets[g_nav_icon - 1];
            const float tx = icon[index * 2] + wave(seed * 7.13f + g_phase * 0.8f) * 2.2f;
            const float ty = icon[index * 2 + 1] + wave(seed * 5.71f + g_phase * 0.65f) * 2.2f;
            projection.x += static_cast<int16_t>((tx - projection.x) * g_nav_icon_w);
            projection.y += static_cast<int16_t>((ty - projection.y) * g_nav_icon_w);
            const int icon_light = 15 + index % 12;
            projection.light += static_cast<int>((icon_light - projection.light) * g_nav_icon_w);
        }
        projection.x = static_cast<int16_t>(CLOUD_WIDTH / 2 + (projection.x - 184) * CLOUD_SCALE);
        projection.y = static_cast<int16_t>(CLOUD_HEIGHT / 2 + (projection.y - 120) * CLOUD_SCALE);
        if (projection.x >= 0 && projection.x + 1 < CLOUD_WIDTH && projection.y >= 0 && projection.y + 1 < CLOUD_HEIGHT) {
            const uint16_t pixel = lv_color_to_u16(g_palette[projection.light]);
            uint16_t &destination = g_pixels[projection.y * CLOUD_WIDTH + projection.x];
            if (pixel > destination) destination = pixel;
            if (g_nav_icon && g_nav_icon_w > 0.5f) {
                // 导航笔画使用2×2亮点，其他状态沿用原粒子大小与边缘亮度。
                for (int row = 0; row < 2; ++row) {
                    for (int column = 0; column < 2; ++column) {
                        uint16_t &neighbor = g_pixels[(projection.y + row) * CLOUD_WIDTH + projection.x + column];
                        if (pixel > neighbor) neighbor = pixel;
                    }
                }
            } else if (projection.light > CLOUD_EDGE_LIGHT_THRESHOLD && index % 3 != 0) {
                const uint16_t edge = lv_color_to_u16(g_palette[projection.light / 2]);
                uint16_t &right = g_pixels[projection.y * CLOUD_WIDTH + projection.x + 1];
                uint16_t &bottom = g_pixels[(projection.y + 1) * CLOUD_WIDTH + projection.x];
                if (edge > right) right = edge;
                if (edge > bottom) bottom = edge;
            }
        }
    }
    // 绘制完整后再提交：LVGL读取前台图像时不再清空/改写同一源缓冲。
    uint16_t *completed = g_pixels; g_pixels = g_front_pixels; g_front_pixels = completed;
    lv_canvas_set_buffer(g_cloud, g_front_pixels, CLOUD_WIDTH, CLOUD_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_draw_buf_set_flag(lv_canvas_get_draw_buf(g_cloud), LV_IMAGE_FLAGS_MODIFIABLE);
    lv_obj_invalidate(g_cloud);
    const uint32_t duration = esp_timer_get_time() - started;
    ++g_frame_count; g_render_total_us += duration; if (duration > g_render_max_us) g_render_max_us = duration;
    if (g_frame_count == 100) {
        ESP_LOGI(TAG, "pointcloud_stats frames=%lu render_avg_us=%lu render_max_us=%lu elapsed_ms=%lu particles=%d", static_cast<unsigned long>(g_frame_count), static_cast<unsigned long>(g_render_total_us / g_frame_count), static_cast<unsigned long>(g_render_max_us), static_cast<unsigned long>(now - g_stats_started), PARTICLE_COUNT);
        g_frame_count = 0; g_render_total_us = 0; g_render_max_us = 0; g_stats_started = now;
    }
}

static lv_obj_t *shape(lv_obj_t *parent, uint32_t color, int radius, lv_opa_t opacity = LV_OPA_COVER) {
    lv_obj_t *object = lv_obj_create(parent); lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_opa(object, opacity, 0); lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_radius(object, radius, 0); lv_obj_set_scrollable(object, false); return object;
}

static void touch_listen(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED && g_touch_listen_ready && g_state != UiState::Updating) {
        g_last_activity_ms = lv_tick_get();
        restore_display_brightness();
        g_touch_listen_held = true;
        control_protocol_listen_start();
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) && g_touch_listen_held) {
        g_touch_listen_held = false;
        control_protocol_listen_stop();
    }
}

static esp_err_t reset_display_peripherals() {
#if defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)
    // 厂家同板 board_variant.c：TCA9554 P0=LCD_RST，P1=DSI_PWR_EN，P2=TOUCH_RST。
    // OTA软件重启不会切断电池供电；在创建SPI/LVGL前复位外设，避免沿用上次运行状态。
    esp_io_expander_handle_t expander = bsp_io_expander_init();
    if (!expander) return ESP_ERR_INVALID_STATE;
    const uint32_t display_pins = IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2;
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(expander, display_pins, IO_EXPANDER_OUTPUT), TAG, "display reset direction");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, display_pins, 0), TAG, "display reset assert");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, display_pins, 1), TAG, "display reset release");
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI(TAG, "display_peripherals_reset mask=0x%02lx assert_ms=20 settle_ms=150", static_cast<unsigned long>(display_pins));
#endif
    return ESP_OK;
}

esp_err_t display_ui_init() {
    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "board power");
    ESP_RETURN_ON_ERROR(reset_display_peripherals(), TAG, "board display reset");
    ESP_RETURN_ON_ERROR(cloud_allocate(), TAG, "point cloud PSRAM allocation");
#if defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)
    bsp_display_cfg_t display_config = {};
    display_config.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    display_config.lvgl_port_cfg.task_affinity = 1;
    display_config.lvgl_port_cfg.task_priority = 2;
    lv_display_t *display = bsp_display_start_with_config(&display_config);
#else
    // Use the selected BSP's own touch transform, DMA buffers and LVGL task.
    lv_display_t *display = bsp_display_start();
#endif
    if (!display) return ESP_FAIL;
    if (!lock_display()) return ESP_ERR_TIMEOUT;
    if (lv_display_get_horizontal_resolution(display) != board_profile::width || lv_display_get_vertical_resolution(display) != board_profile::height) {
        ESP_LOGE(TAG, "unexpected_display_size board=%s expected=%dx%d", board_profile::name, board_profile::width, board_profile::height);
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }
#if defined(ASSISTANT_BOARD_LCD_EV)
    ESP_LOGI(TAG, "LCD EV fixed backlight;brightness/night dimming/screen-off unavailable");
#else
    const esp_err_t brightness_result = set_display_brightness(g_brightness, "initialization");
    if (brightness_result != ESP_OK) { bsp_display_unlock(); return brightness_result; }
#endif
    lv_display_add_event_cb(display, display_refresh_event, LV_EVENT_ALL, nullptr);
    // BSP仍持有其原生小缓冲；使用固定内部DMA条带，避免SPI为PSRAM临时分配搬运缓冲。
#if defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)
    lv_display_set_buffers(display, g_display_buffer, nullptr, board_profile::width * TRANSFER_ROWS * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
    lv_obj_t *screen = lv_screen_active(); lv_obj_set_style_bg_color(screen, board_profile::reflective_display ? lv_color_white() : lv_color_black(), 0); lv_obj_set_scrollable(screen, false);
    if constexpr (board_profile::text_only) {
        g_title = lv_label_create(screen); g_detail = lv_label_create(screen);
        lv_obj_set_size(g_title, board_profile::width, board_profile::reflective_display ? DISPLAY_LAYOUT.badge_height : 12); lv_obj_set_pos(g_title, 0, 0);
        lv_obj_set_style_text_font(g_title, kHeaderFont, 0);
        lv_obj_set_size(g_detail, board_profile::width, board_profile::height - 14); lv_obj_set_pos(g_detail, 0, 14);
        lv_obj_set_style_text_font(g_detail, kUiFont, 0);
        lv_obj_t *oled_labels[] = {g_title, g_detail};
        for (auto *label : oled_labels) {
            lv_obj_set_style_text_color(label, board_profile::reflective_display ? lv_color_black() : lv_color_white(), 0);
            lv_obj_set_style_text_line_space(label, 0, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }
        if constexpr (board_profile::reflective_display) {
            lv_label_set_long_mode(g_title, LV_LABEL_LONG_CLIP);
            lv_obj_t *viewport = lv_obj_create(screen); lv_obj_remove_style_all(viewport);
            lv_obj_set_pos(viewport, PAPER_CONTENT_X, DISPLAY_LAYOUT.detail_y); lv_obj_set_size(viewport, PAPER_CONTENT_WIDTH, PAPER_IDLE_BODY_HEIGHT);
            lv_obj_remove_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_parent(g_detail, viewport); lv_obj_set_pos(g_detail, 0, 0);
            lv_obj_set_width(g_detail, PAPER_CONTENT_WIDTH); lv_obj_set_height(g_detail, LV_SIZE_CONTENT);
            lv_label_set_long_mode(g_detail, LV_LABEL_LONG_WRAP);
        }
        g_last_activity_ms = lv_tick_get(); apply_state(UiState::Idle, nullptr);
        lv_timer_create(expression_timer, 250, nullptr);
        bsp_display_unlock();
        ESP_LOGI(TAG, "text_ready screen=%dx%d cloud=disabled", board_profile::width, board_profile::height);
        return ESP_OK;
    }
    lv_obj_t *brand = lv_label_create(screen); lv_obj_set_style_text_font(brand, kUiFont, 0); lv_obj_set_style_text_color(brand, lv_color_hex(0x617080), 0); lv_label_set_text(brand, (board_profile::compact_display || (board_profile::round && board_profile::width <= 240)) ? "伊迪斯" : "伊迪斯  /  MORPH"); lv_obj_set_pos(brand, DISPLAY_LAYOUT.brand_x, DISPLAY_LAYOUT.brand_y);
    g_status_dot = shape(screen, 0xc5e4ef, LV_RADIUS_CIRCLE); lv_obj_set_size(g_status_dot, 5, 5); lv_obj_set_pos(g_status_dot, DISPLAY_LAYOUT.status_x, DISPLAY_LAYOUT.status_y);
    g_cloud = lv_canvas_create(screen); lv_canvas_set_buffer(g_cloud, g_front_pixels, CLOUD_WIDTH, CLOUD_HEIGHT, LV_COLOR_FORMAT_RGB565); lv_obj_set_pos(g_cloud, DISPLAY_LAYOUT.cloud_x, DISPLAY_LAYOUT.cloud_y);
    g_title = lv_label_create(screen); g_detail = lv_label_create(screen);
    lv_obj_t *labels[2] = {g_title, g_detail};
    for (auto *label : labels) { lv_obj_set_width(label, DISPLAY_LAYOUT.content_width); lv_obj_set_style_text_font(label, kUiFont, 0); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP); }
    lv_obj_set_style_text_color(g_title, lv_color_hex(0xe5edf1), 0); lv_obj_set_pos(g_title, DISPLAY_LAYOUT.content_x, DISPLAY_LAYOUT.title_y);
    lv_obj_set_style_text_color(g_detail, lv_color_hex(0x82939f), 0); lv_obj_set_pos(g_detail, DISPLAY_LAYOUT.content_x, DISPLAY_LAYOUT.detail_y); lv_obj_set_height(g_detail, DISPLAY_LAYOUT.detail_height); lv_obj_set_style_text_line_space(g_detail, 6, 0);
    if constexpr (board_profile::width <= 240) {
        lv_label_set_long_mode(g_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_long_mode(g_detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    lv_obj_t *line = shape(screen, 0x28333b, LV_RADIUS_CIRCLE); lv_obj_set_size(line, 30, 3); lv_obj_set_pos(line, (board_profile::width - 30) / 2, DISPLAY_LAYOUT.footer_y);
    if constexpr (board_profile::touch_listen) {
        g_listen_button = lv_button_create(screen);
        lv_obj_set_size(g_listen_button, 160, 36);
        lv_obj_set_pos(g_listen_button, (board_profile::width - 160) / 2, 192);
        lv_obj_add_flag(g_listen_button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_state(g_listen_button, LV_STATE_DISABLED);
        lv_obj_add_event_cb(g_listen_button, touch_listen, LV_EVENT_ALL, nullptr);
        lv_obj_t *label = lv_label_create(g_listen_button);
        lv_obj_set_style_text_font(label, kUiFont, 0);
        lv_label_set_text(label, "按住说话");
        lv_obj_center(label);
    }
    // 状态点常亮呼吸：只重绘5x5小区域，待机跳帧预算不受影响。
    lv_anim_t breath;
    lv_anim_init(&breath);
    lv_anim_set_var(&breath, g_status_dot);
    lv_anim_set_exec_cb(&breath, [](void *var, int32_t value) { lv_obj_set_style_opa(static_cast<lv_obj_t *>(var), value, 0); });
    lv_anim_set_values(&breath, LV_OPA_COVER, LV_OPA_40);
    lv_anim_set_duration(&breath, 1300);
    lv_anim_set_path_cb(&breath, lv_anim_path_ease_in_out);
    lv_anim_set_playback_duration(&breath, 1300);
    lv_anim_set_repeat_count(&breath, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&breath);
    g_last_activity_ms = lv_tick_get(); apply_state(UiState::Idle, nullptr); g_stats_started = lv_tick_get(); expression_timer(nullptr); lv_timer_create(expression_timer, FRAME_PERIOD_MS, nullptr); bsp_display_unlock();
    ESP_LOGI(TAG, "pointcloud_ready board=%s screen=%dx%d particles=%d area=%dx%d psram_bytes=%u period_ms=%d image_buffers=2", board_profile::name, board_profile::width, board_profile::height, PARTICLE_COUNT, CLOUD_WIDTH, CLOUD_HEIGHT, static_cast<unsigned>(2 * CLOUD_WIDTH * CLOUD_HEIGHT * sizeof(uint16_t) + PARTICLE_COUNT * (sizeof(CloudParticle) + sizeof(CloudProjection)) + SINE_COUNT * sizeof(float)), FRAME_PERIOD_MS);
    return ESP_OK;
}

esp_err_t display_ui_enable_manual_input() {
    if constexpr (!board_profile::touch_listen) return ESP_OK;
    if (!g_listen_button) return ESP_ERR_INVALID_STATE;
    if (!lock_display()) return ESP_ERR_TIMEOUT;
    g_touch_listen_ready = true;
    if (g_state != UiState::Updating) lv_obj_remove_state(g_listen_button, LV_STATE_DISABLED);
    bsp_display_unlock();
    return ESP_OK;
}

esp_err_t display_ui_apply_preferences(uint8_t brightness, uint8_t night_brightness, bool night_mode_enabled, uint32_t screen_off_seconds) {
    if (brightness > 100 || night_brightness > 100 || screen_off_seconds > 86400) return ESP_ERR_INVALID_ARG;
#if defined(ASSISTANT_BOARD_SPARKBOT) || defined(ASSISTANT_BOARD_SENSAIR_C5)
    // Reject unsupported levels before changing the cached restore value.
    // A rejected50% preference must not strand the display blank at wakeup.
    if ((brightness != 0 && brightness != 100) || night_mode_enabled) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (!g_title) return ESP_ERR_INVALID_STATE;
    if (!lock_display()) return ESP_ERR_TIMEOUT;
    g_brightness = brightness;
    g_night_brightness = night_brightness;
    g_night_mode_enabled = night_mode_enabled;
    g_screen_off_seconds = screen_off_seconds;
    g_last_activity_ms = lv_tick_get();
    (void)g_night_brightness;
    (void)g_night_mode_enabled;
    (void)g_screen_off_seconds;
    const esp_err_t result = set_display_brightness(g_brightness, "preferences");
    if (result == ESP_OK) g_screen_dimmed = false;
    bsp_display_unlock();
    return result;
}

void display_ui_state(UiState state, const char *detail, bool media) {
    if (!g_title || !lock_display()) return;
    g_last_activity_ms = lv_tick_get();
    restore_display_brightness();
    if (g_state == UiState::Updating && state != UiState::Updating && state != UiState::Error) { bsp_display_unlock(); return; }
    if (state == UiState::Error && g_state != UiState::Error) control_protocol_queue_screen_action("cue_error", "", "");
    if (g_pairing_guide) { lv_obj_delete(g_pairing_guide); g_pairing_guide = nullptr; }

    if (g_notice_active && (state == UiState::Idle || state == UiState::Connecting)) { g_after_notification = state; supported_text(g_after_detail, sizeof(g_after_detail), detail); }
    else { g_notice_active = false; apply_state(state, detail); }
    if (media && state == UiState::Speaking) lv_label_set_text(g_title, "正在播放");
    bsp_display_unlock();
}

bool display_ui_notification(const char *title, const char *due_at) {
    if (!g_title || !lock_display()) return false;
    if (g_state == UiState::Updating) { bsp_display_unlock(); return false; }
    if (g_card) lv_obj_add_flag(g_card, LV_OBJ_FLAG_HIDDEN);
    g_after_notification = g_state; g_after_detail[0] = '\0'; g_notice_active = true; g_notice_started = lv_tick_get();
    char text[384]; char safe_title[256]; supported_text(safe_title, sizeof(safe_title), title);
    snprintf(text, sizeof(text), "%s\n%s", safe_title, due_at ? due_at : ""); apply_state(UiState::Notification, text); bsp_display_unlock(); return true;
}

void display_ui_audio_level(uint8_t level) { g_audio_level = level; }

void display_ui_nav_icon(uint8_t icon) {
    // 只写单字节标量（渲染任务每帧读取快照）；不碰 LVGL 对象，免显示锁。
    if (icon > NAV_ICON_COUNT) return;
    g_nav_icon = icon;
}

void display_ui_pairing(const char *code) {
    if (!code || !g_title) return;
    if (!lock_display()) {
        ESP_LOGE(TAG, "pairing_guide_display_lock_failed");
        return;
    }
    g_notice_active = false;
    delete_screen_card();
    apply_state(UiState::Alert, nullptr);
    // 独立首连遮罩，避免配对码被普通状态文字高度裁切。
    lv_obj_t *guide = lv_obj_create(lv_screen_active());
    if (g_pairing_guide) lv_obj_delete(g_pairing_guide);
    g_pairing_guide = guide;
    lv_obj_set_size(guide, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(guide, lv_color_hex(0x090d15), 0);
    lv_obj_set_style_bg_opa(guide, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(guide, 0, 0);
    lv_obj_set_style_radius(guide, 0, 0);
    if constexpr (board_profile::compact_display) lv_obj_set_style_pad_all(guide, 0, 0);
    if constexpr (board_profile::text_only) {
        lv_obj_remove_style_all(guide);
        lv_obj_set_size(guide, board_profile::width, board_profile::height);
        lv_obj_set_style_bg_color(guide, board_profile::reflective_display ? lv_color_white() : lv_color_black(), 0);
        lv_obj_set_style_bg_opa(guide, LV_OPA_COVER, 0);
        lv_obj_t *heading = lv_label_create(guide);
        lv_obj_set_style_text_font(heading, kHeaderFont, 0);
        lv_obj_set_style_text_color(heading, board_profile::reflective_display ? lv_color_black() : lv_color_white(), 0);
        lv_label_set_text(heading, board_profile::width == 64 ? "PAIR / 10m" : "PAIR DEVICE / 10 MIN"); lv_obj_set_pos(heading, 0, 0);
        lv_obj_t *digits = lv_label_create(guide);
        lv_obj_set_style_text_font(digits, kUiFont, 0);
        lv_obj_set_style_text_color(digits, board_profile::reflective_display ? lv_color_black() : lv_color_white(), 0);
#if defined(ASSISTANT_BOARD_MATRIX)
        lv_obj_set_style_text_font(digits, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_letter_space(digits, 0, 0);
#endif
        lv_label_set_text(digits, code); lv_obj_align(digits, LV_ALIGN_TOP_MID, 0, board_profile::reflective_display ? 42 : 14);
        if constexpr (board_profile::height >= 64) {
            lv_obj_t *instructions = lv_label_create(guide);
            lv_obj_set_style_text_font(instructions, kUiFont, 0);
            lv_obj_set_style_text_color(instructions, board_profile::reflective_display ? lv_color_black() : lv_color_white(), 0);
            lv_obj_set_pos(instructions, 0, board_profile::reflective_display ? 90 : 36); lv_obj_set_size(instructions, board_profile::width, board_profile::reflective_display ? DISPLAY_LAYOUT.card_title_height : 17);
            lv_label_set_long_mode(instructions, board_profile::reflective_display ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_label_set_text(instructions, "控制台 > 设备 > 输入绑定码");
        }
        if constexpr (board_profile::reflective_display) ESP_LOGI(TAG, "paper_pairing_code_queued");
        else ESP_LOGI(TAG, "oled_pairing_code_displayed");
        bsp_display_unlock(); return;
    }
    lv_obj_t *heading = lv_label_create(guide);
    lv_obj_set_style_text_font(heading, kUiFont, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(0xe5edf1), 0);
    lv_label_set_text(heading, (board_profile::compact_display || (board_profile::round && board_profile::width <= 240)) ? "绑定伊迪斯" : "绑定伊迪斯 / Pair device");
    if constexpr (board_profile::width <= 240) {
        lv_obj_set_width(heading, DISPLAY_LAYOUT.content_width);
        lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(heading, LV_LABEL_LONG_WRAP);
    }
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, DISPLAY_LAYOUT.pairing_heading_y);
    lv_obj_t *digits = lv_label_create(guide);
    lv_obj_set_style_text_font(digits, kUiFont, 0);
    lv_obj_set_style_text_color(digits, lv_color_hex(0xb8a4ff), 0);
    lv_label_set_text(digits, code);
    lv_obj_align(digits, LV_ALIGN_CENTER, 0, -15);
    lv_obj_t *instructions = lv_label_create(guide);
    lv_obj_set_width(instructions, DISPLAY_LAYOUT.content_width - 14);
    lv_obj_set_style_text_font(instructions, kUiFont, 0);
    lv_obj_set_style_text_align(instructions, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(instructions, lv_color_hex(0xe5edf1), 0);
    lv_label_set_text(instructions, (board_profile::compact_display || (board_profile::round && board_profile::width <= 240)) ? "控制台 > 设备\n输入六位码" : "打开控制台 > 设备 > 添加设备\n输入上方六位码\n有效期10分钟\nConsole > Devices > Add device");
    lv_obj_align(instructions, LV_ALIGN_BOTTOM_MID, 0, -DISPLAY_LAYOUT.pairing_bottom);
    // 保存到期及成功状态通过普通状态接口时移除此遮罩。
    ESP_LOGI(TAG, "pairing_guide_displayed");
    bsp_display_unlock();
}
