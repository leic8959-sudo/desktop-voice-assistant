#include "board_power.h"

#include "board_profile.h"
#if defined(ASSISTANT_BOARD_XINGZHI_TFT) || defined(ASSISTANT_BOARD_ZHENGCHEN_TFT) || defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_OLED_CUBE) || defined(ASSISTANT_BOARD_DUCHATX) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_AIPI)
#include "driver/rtc_io.h"
#endif
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
#if defined(ASSISTANT_BOARD_JIUCHUAN)
void jiuchuan_power_button_task(void *) {
    bool released = false;
    int64_t pressed_at = 0;
    while (true) {
        // This board's power key is active high; wait for the boot hold to end.
        if (!gpio_get_level(GPIO_NUM_3)) { released = true; pressed_at = 0; }
        else if (released) {
            const int64_t now = esp_timer_get_time();
            if (!pressed_at) pressed_at = now;
            if (now - pressed_at >= 3000000) {
                ESP_LOGI("board_power", "jiuchuan_power_hold release_RTC5");
                const esp_err_t result = rtc_gpio_set_level(GPIO_NUM_5, 0);
                if (result != ESP_OK) ESP_LOGE("board_power", "jiuchuan_power_release_failed error=%s", esp_err_to_name(result));
                released = false;
                pressed_at = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif
#if defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_AMOLED_18) || defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_216) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)) || defined(ASSISTANT_BOARD_LCD_183) || defined(ASSISTANT_BOARD_LCD_154) || defined(ASSISTANT_BOARD_LCD_085) || (defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185)) || defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_WS4B) || defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_WS_EPAPER397)
constexpr const char *TAG = "board_power";
#endif
#if defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_LCD_154) || defined(ASSISTANT_BOARD_LCD_085) || (defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185))
#if defined(ASSISTANT_BOARD_LCD_154) || defined(ASSISTANT_BOARD_LCD_085)
constexpr gpio_num_t kLatchPin = GPIO_NUM_2, kPowerKey = GPIO_NUM_5;
#elif (defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185))
constexpr gpio_num_t kLatchPin = GPIO_NUM_7, kPowerKey = GPIO_NUM_6;
#else
constexpr gpio_num_t kLatchPin = GPIO_NUM_18, kPowerKey = GPIO_NUM_17;
#endif
void power_button_task(void *) {
    // Ignore the initial power-on hold. A fresh long press releases the
    // battery latch in provisioning, pairing and normal operation alike.
    // LCD1.46/original1.85 use a five-second hold; other latches use two.
    bool released = false;
    int64_t pressed_at = 0;
    while (true) {
        if (gpio_get_level(kPowerKey)) {
            released = true;
            pressed_at = 0;
        } else if (released) {
            const int64_t now = esp_timer_get_time();
            if (!pressed_at) pressed_at = now;
#if (defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185))
            constexpr int64_t hold_us = 5000000;
#else
            constexpr int64_t hold_us = 2000000;
#endif
            if (now - pressed_at >= hold_us) {
                ESP_LOGI(TAG, "power_button long_hold release_battery_latch");
                const esp_err_t result = gpio_set_level(kLatchPin, 0);
                if (result != ESP_OK) ESP_LOGE(TAG, "power_latch_release failed: %s", esp_err_to_name(result));
                // On USB the board can remain powered after releasing its latch.
                released = false;
                pressed_at = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif
}

esp_err_t board_power_init() {
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
    return sensair_board_init();
#endif
#if defined(ASSISTANT_BOARD_SPOT_C5)
    return spot_c5_board_init();
#endif
#if defined(ASSISTANT_BOARD_MOJI2_C5)
    return moji2_c5_board_init();
#endif
#if defined(ASSISTANT_BOARD_C5_LCD169)
    return c5_lcd169_board_init();
#endif
#if defined(ASSISTANT_BOARD_S31_KORVO)
    return s31_korvo_board_init();
#endif
#if defined(ASSISTANT_BOARD_MOSAICO)
    return mosaico_board_init();
#endif
#if defined(ASSISTANT_BOARD_S31_CORE)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_57, 0), "board_power", "S31 PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_57; amplifier.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), "board_power", "S31 PA pad with readback");
    return bsp_i2c_init();
#endif
#if defined(ASSISTANT_BOARD_M5_COREP4)
    return m5_corep4_board_init();
#endif
#if defined(ASSISTANT_BOARD_M5_TAB5)
    return m5_tab5_board_init();
#endif
#if defined(ASSISTANT_BOARD_P4_MIPI)
    return waveshare_p4_mipi_prepare();
#endif
#if defined(ASSISTANT_BOARD_SENSECAP)
    return sensecap_board_init();
#endif
#if defined(ASSISTANT_BOARD_MATRIX)
    // OE is active low. Keep the panel blank until the driver has a black
    // buffer; retain credentials/preferences instead of the source volume70.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_2, 1), "board_power", "matrix OE blank latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_11, 0), "board_power", "matrix PA inactive latch");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(GPIO_NUM_2), "board_power", "matrix old OE hold");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(GPIO_NUM_11), "board_power", "matrix old PA hold");
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_11);
    outputs.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), "board_power", "matrix startup pads");
    return gpio_get_level(GPIO_NUM_2) && !gpio_get_level(GPIO_NUM_11) ? ESP_OK : ESP_ERR_INVALID_STATE;
#endif
#if defined(ASSISTANT_BOARD_WS_RLCD)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "RLCD PA inactive latch");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(GPIO_NUM_46), "board_power", "RLCD old PA hold");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_46;
    amplifier.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), "board_power", "RLCD PA pad");
    if (gpio_get_level(GPIO_NUM_46)) return ESP_ERR_INVALID_STATE;
    // Hardware BC2104/RTC circuit owns the power latch. GPIO18 is a key input,
    // not a software supply output; preserve charging and regulator settings.
    gpio_config_t power_key = {};
    power_key.pin_bit_mask = 1ULL << GPIO_NUM_18;
    power_key.mode = GPIO_MODE_INPUT;
    power_key.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&power_key);
#endif
#if defined(ASSISTANT_BOARD_WS_EPAPER397)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_39, 0), "board_power", "paper3.97 PA inactive latch");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(GPIO_NUM_39), "board_power", "paper3.97 old PA hold");
    gpio_config_t paper_outputs = {};
    paper_outputs.pin_bit_mask = 1ULL << GPIO_NUM_39;
    paper_outputs.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&paper_outputs), "board_power", "paper3.97 PA pad");
    if (gpio_get_level(GPIO_NUM_39)) return ESP_ERR_INVALID_STATE;
    gpio_config_t power_key = {};
    power_key.pin_bit_mask = 1ULL << GPIO_NUM_1;
    power_key.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&power_key), "board_power", "paper3.97 reserved PWR input");
    // Continue below to the shared checked TG28/AXP-compatible rail setup.
#endif
#if defined(ASSISTANT_BOARD_ZECTRIX)
    // NOTE4 black/white board: all three rails/latch are active HIGH.
    // Release old firmware holds only after preloading the intended levels.
    const gpio_num_t rails[] = {GPIO_NUM_17, GPIO_NUM_6, GPIO_NUM_42};
    for (const auto pin : rails) {
        ESP_RETURN_ON_ERROR(gpio_set_level(pin, 1), "board_power", "NOTE4 rail latch");
        ESP_RETURN_ON_ERROR(gpio_hold_dis(pin), "board_power", "NOTE4 old rail hold");
    }
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "NOTE4 PA inactive latch");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(GPIO_NUM_46), "board_power", "NOTE4 old PA hold");
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << GPIO_NUM_17) | (1ULL << GPIO_NUM_6) | (1ULL << GPIO_NUM_42) | (1ULL << GPIO_NUM_46);
    outputs.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), "board_power", "NOTE4 supply pads");
    for (const auto pin : rails) {
        ESP_RETURN_ON_ERROR(gpio_set_level(pin, 1), "board_power", "NOTE4 supply on");
        if (!gpio_get_level(pin)) return ESP_ERR_INVALID_STATE;
        ESP_RETURN_ON_ERROR(gpio_hold_en(pin), "board_power", "NOTE4 supply hold");
    }
    if (gpio_get_level(GPIO_NUM_46)) return ESP_ERR_INVALID_STATE;
    // The codec owns PA46 afterwards: do not hold it or clock-independent
    // mute/close would leave the amplifier enabled. No shutdown/OTA key task.
    gpio_config_t reserved_keys = {};
    reserved_keys.pin_bit_mask = (1ULL << GPIO_NUM_39) | (1ULL << GPIO_NUM_18);
    reserved_keys.mode = GPIO_MODE_INPUT;
    reserved_keys.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&reserved_keys), "board_power", "NOTE4 reserved UP/DOWN keys");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
#endif
#if defined(ASSISTANT_BOARD_WS_EPAPER)
    // Keep the battery latch and both supply rails on in screen-off mode.
    // Preload output latches before enabling pads; PA stays off until codec open.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_17, 1), "board_power", "paper battery latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_6, 0), "board_power", "paper display supply");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_42, 0), "board_power", "paper audio supply");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "paper PA inactive");
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << GPIO_NUM_17) | (1ULL << GPIO_NUM_6) | (1ULL << GPIO_NUM_42) | (1ULL << GPIO_NUM_46);
    outputs.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), "board_power", "paper supply outputs");
    if (!gpio_get_level(GPIO_NUM_17) || gpio_get_level(GPIO_NUM_6) || gpio_get_level(GPIO_NUM_42) || gpio_get_level(GPIO_NUM_46)) return ESP_ERR_INVALID_STATE;
    // PWR18 is reserved. No unbounded boot-key wait, power-off task or fake ADC.
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
#endif
#if defined(ASSISTANT_BOARD_ELECTRON)
    // No motion controller or LEDC servo channel is created. Only the
    // displaybacklight is owned;servo4/5/6/7/15/16 remain untouched.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "electronBot backlight dark latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_46;
    backlight.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&backlight);
#endif
#if defined(ASSISTANT_BOARD_K10)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "K10I2C1");
    return k10_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_MVSRLORA)
    return mvsrlora_board_init();
#endif
#if defined(ASSISTANT_BOARD_TCAMERA)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "T-CameraPlus backlight dark latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_21, 1), "board_power", "shared SPI SD CS inactive latch");
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << GPIO_NUM_46) | (1ULL << GPIO_NUM_21);
#if defined(ASSISTANT_BOARD_TCAMERA_V12)
    // ManufacturerQ4/Q6P-channelgates:LOW powers microphoneandamplifier.
    // Never use this net as a speaker-only mute or microphone loses power.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_18, 0), "board_power", "V1.2 sharedMIC/PA supply latch");
    outputs.pin_bit_mask |= 1ULL << GPIO_NUM_18;
#endif
    outputs.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), "board_power", "T-CameraPlus startup outputs");
    vTaskDelay(pdMS_TO_TICKS(10));
    // KEY17 remains a reservedinput;camera/IRcut16/SY6970chargeruntouched.
    gpio_config_t key = {};
    key.pin_bit_mask = 1ULL << GPIO_NUM_17;
    key.mode = GPIO_MODE_INPUT;
    key.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&key);
#endif
#if defined(ASSISTANT_BOARD_VOCAT)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "VoCat I2C0");
    return vocat_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_SPARKBOT)
    // SharedBL/PA46 starts low. Display startup raises it once;runtime screen
    // blanking uses DCS commands and codec mute never toggles this shared net.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "SparkBot shared enable low latch");
    gpio_config_t enable = {};
    enable.pin_bit_mask = 1ULL << GPIO_NUM_46;
    enable.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&enable);
#endif
#if defined(ASSISTANT_BOARD_LCD_EV)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "LCD EV I2C0");
    return lcd_ev_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_STOPWATCH)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Stopwatch SYS I2C0");
    return stopwatch_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_CARDPUTER_ADV)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_38, 0), "board_power", "Cardputer backlight dark latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_38;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), "board_power", "Cardputer backlight output");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Cardputer I2C0");
    return cardputer_adv_keys_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_ATOM_S3_BASE)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_16, 0), "board_power", "AtomS3 backlight dark latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_16;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), "board_power", "AtomS3 backlight output");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "AtomS3 Echo Base I2C1");
    return atomic_echo_base_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_WS143C)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "Waveshare1.43C PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_46;
    amplifier.mode = GPIO_MODE_OUTPUT;
    // Manufacturer config mentions LDO_EN18 but neither its checked audio
    // init nor the fixed board class uses it. Do not guess a new supply pin.
    return gpio_config(&amplifier);
#endif
#if defined(ASSISTANT_BOARD_WS349)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Waveshare3.49 codec I2C0");
    return waveshare_349_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_WS35)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Waveshare3.5 I2C0");
    ESP_RETURN_ON_ERROR(waveshare_35_board_init(bsp_i2c_get_handle()), "board_power", "Waveshare3.5 LCD reset/PA");
    // ALDO1 supplies codec/MIC3.3V. Camera BLDO1/2 and charge policy stay
    // with their existing configuration; this entry does not initialize video.
#endif
#if defined(ASSISTANT_BOARD_WS4B)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Waveshare4B I2C0");
    ESP_RETURN_ON_ERROR(waveshare_4b_board_init(bsp_i2c_get_handle()), "board_power", "Waveshare4B expander/reset");
    // Continue to the common ALDO1-only setup; leave all other rails and
    // charger/power-button configuration unchanged.
#endif
#if defined(ASSISTANT_BOARD_WS_RGB_C)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Waveshare RGB I2C0");
    return waveshare_rgb_c_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_KEVIN_RGB)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_45, 0), "board_power", "Kevin RGB PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_45;
    amplifier.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&amplifier);
#endif
#if defined(ASSISTANT_BOARD_OTTO)
    return otto_board_init();
#endif
#if defined(ASSISTANT_BOARD_EDA_ROBOT)
    // The common OLED keys40/39 overlap this board's speaker bus. GPIO14 is
    // its actual press-to-talk key; no servo pin or controller is initialized.
    return ESP_OK;
#endif
#if defined(ASSISTANT_BOARD_WS_CAM)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Waveshare CAM I2C1");
    return waveshare_cam_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_GUITION_EXT_AMP)
    // The live onboard NS4168 has no enable pin. Park its unused I2S lines
    // before configuring output direction; only the external bus is clocked.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_42, 0), "board_power", "NS4168 BCLK low latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_2, 0), "board_power", "NS4168 WS low latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_41, 0), "board_power", "NS4168 data low latch");
    gpio_config_t parked = {};
    parked.pin_bit_mask = (1ULL << GPIO_NUM_42) | (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_41);
    parked.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&parked);
#endif
#if defined(ASSISTANT_BOARD_KORVO2)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, 0), "board_power", "Korvo2 PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_48;
    amplifier.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), "board_power", "Korvo2 PA output");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Korvo2 I2C1");
    return korvo2_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_DF_AI_CAM)
    // Owning GpioLed uses output_invert=0: low is initially dark. No Doit
    // supply48/LED45 writes: those pins have different roles on this board.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_3, 0), "board_power", "DFRobot LED off latch");
    gpio_config_t led = {};
    led.pin_bit_mask = 1ULL << GPIO_NUM_3;
    led.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&led);
#endif
#if defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_LCDWIKI)
    // Establish active-low PA mute before enabling its output driver.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_1, 1), "board_power", "active-low PA1 off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_1;
    amplifier.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&amplifier);
#endif
#if defined(ASSISTANT_BOARD_CHD_EYE)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_40, 0), "board_power", "CHD EYE PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_40;
    amplifier.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&amplifier);
#endif
#if defined(ASSISTANT_BOARD_STICK_S3)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "StickS3 I2C0");
    return stick_s3_power_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_ECHO_PYRAMID)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Pyramid audio I2C1");
    ESP_RETURN_ON_ERROR(echo_pyramid_board_init(bsp_i2c_get_handle()), "board_power", "Pyramid clock/power");
    return atom_s3r_backlight_init();
#endif
#if defined(ASSISTANT_BOARD_ATOM_S3R_BASE)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "AtomS3R base I2C1");
    ESP_RETURN_ON_ERROR(atomic_echo_base_init(bsp_i2c_get_handle()), "board_power", "Atomic base power");
    return atom_s3r_backlight_init();
#endif
#if defined(ASSISTANT_BOARD_ATOM_M12_BASE)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Atomic base I2C0");
    return atomic_echo_base_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_CHD)
    // PA46 is active high. Establish mute before enabling its output driver.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), "board_power", "CHD PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_46;
    amplifier.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&amplifier);
#endif
#if defined(ASSISTANT_BOARD_MAGICLICK)
    // The LED feature is not implemented: keep its active-low supply off.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_39, 1), "board_power", "Magiclick LED supply latch");
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = 1ULL << GPIO_NUM_39;
    outputs.mode = GPIO_MODE_OUTPUT;
#if defined(ASSISTANT_BOARD_MAGICLICK_25)
    // Match the owning board's Wi-Fi mode: never power the unused modem.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_40, 0), "board_power", "Magiclick modem off latch");
    outputs.pin_bit_mask |= 1ULL << GPIO_NUM_40;
#endif
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), "board_power", "Magiclick supply outputs");
    gpio_config_t keys = {};
    keys.pin_bit_mask = (1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_47);
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&keys);
#endif
#if defined(ASSISTANT_BOARD_MINSI) || defined(ASSISTANT_BOARD_AIVOX3)
    // GPIO18 is microphone DIN on K08; never instantiate its upstream lamp
    // example on that pin. Battery/charge pins remain reserved inputs.
    gpio_config_t volume_keys = {};
#if defined(ASSISTANT_BOARD_MINSI)
    volume_keys.pin_bit_mask = (1ULL << GPIO_NUM_47) | (1ULL << GPIO_NUM_46);
#else
    volume_keys.pin_bit_mask = (1ULL << GPIO_NUM_46) | (1ULL << GPIO_NUM_45);
#endif
    volume_keys.mode = GPIO_MODE_INPUT;
    volume_keys.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&volume_keys);
#endif
#if defined(ASSISTANT_BOARD_WS_AUDIO)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "Waveshare audio I2C0");
    return waveshare_audio_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_AIPI)
    static bool initialized = false;
    if (initialized) return ESP_OK;
    ESP_RETURN_ON_ERROR(rtc_gpio_hold_dis(GPIO_NUM_10), "board_power", "AiPi supply hold");
    ESP_RETURN_ON_ERROR(rtc_gpio_init(GPIO_NUM_10), "board_power", "AiPi supply RTC");
    ESP_RETURN_ON_ERROR(rtc_gpio_set_direction(GPIO_NUM_10, RTC_GPIO_MODE_OUTPUT_ONLY), "board_power", "AiPi supply direction");
    ESP_RETURN_ON_ERROR(rtc_gpio_set_level(GPIO_NUM_10, 1), "board_power", "AiPi supply high");
    initialized = true;
    return ESP_OK;
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX3)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "BOX3 I2C0");
    return atk_box3_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX2)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "BOX2 I2C0");
    return atk_box2_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "ATK BOX I2C0");
    return atk_box_board_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX0)
    // Set output latches before enabling the rails; never cycle codec power
    // when starting a recording or playing a short cue.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_2, 1), "board_power", "BOX0 system latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_14, 1), "board_power", "BOX0 codec latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_47, 1), "board_power", "BOX0 charge control latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_21, 0), "board_power", "BOX0 PA initial off");
    gpio_config_t rails = {};
    rails.pin_bit_mask = (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_47) | (1ULL << GPIO_NUM_21);
    rails.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&rails), "board_power", "BOX0 power outputs");
    gpio_config_t inputs = {};
    inputs.pin_bit_mask = (1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_48);
    inputs.mode = GPIO_MODE_INPUT;
    inputs.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&inputs);
#endif
#if defined(ASSISTANT_BOARD_ATK_DEV)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "ATK I2C0");
    return atk_xl9555_init(bsp_i2c_get_handle());
#endif
#if defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), "board_power", "PCA box I2C1");
    ESP_RETURN_ON_ERROR(pca9557_box_init(bsp_i2c_get_handle()), "board_power", "PCA9557 LCD/PA");
#if defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    gpio_config_t keys = {};
    keys.pin_bit_mask = (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_46);
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&keys);
#else
    return ESP_OK;
#endif
#endif
#if defined(ASSISTANT_BOARD_JIUCHUAN)
    ESP_RETURN_ON_ERROR(rtc_gpio_hold_dis(GPIO_NUM_5), "board_power", "Jiuchuan RTC5 hold");
    ESP_RETURN_ON_ERROR(rtc_gpio_init(GPIO_NUM_5), "board_power", "Jiuchuan RTC5 init");
    ESP_RETURN_ON_ERROR(rtc_gpio_set_direction(GPIO_NUM_5, RTC_GPIO_MODE_OUTPUT_ONLY), "board_power", "Jiuchuan RTC5 direction");
    ESP_RETURN_ON_ERROR(rtc_gpio_set_level(GPIO_NUM_5, 1), "board_power", "Jiuchuan RTC5 supply");
    gpio_config_t power_key = {};
    power_key.pin_bit_mask = 1ULL << GPIO_NUM_3;
    power_key.mode = GPIO_MODE_INPUT;
    power_key.pull_down_en = GPIO_PULLDOWN_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&power_key), "board_power", "Jiuchuan active-high power key");
    gpio_config_t volume_keys = {};
    volume_keys.pin_bit_mask = (1ULL << GPIO_NUM_6) | (1ULL << GPIO_NUM_7);
    volume_keys.mode = GPIO_MODE_INPUT;
    volume_keys.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&volume_keys), "board_power", "Jiuchuan volume keys");
    return xTaskCreate(jiuchuan_power_button_task, "power_button", 2048, nullptr, 3, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
#endif
#if defined(ASSISTANT_BOARD_ATOM_ECHOS3R)
    // GPIO0/45 belong to codec I2C. No Doit LED,enable or volume pins here.
    return bsp_i2c_init();
#endif
#if defined(ASSISTANT_BOARD_DOIT)
    gpio_config_t enable = {};
    enable.pin_bit_mask = (1ULL << GPIO_NUM_48) | (1ULL << GPIO_NUM_45);
    enable.mode = GPIO_MODE_OUTPUT;
    enable.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&enable), "board_power", "Doit enable/LED outputs");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, 1), "board_power", "Doit initial GPIO48 high");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_45, 1), "board_power", "Doit LED off");
    gpio_config_t keys = {};
    keys.pin_bit_mask = (1ULL << GPIO_NUM_15) | (1ULL << GPIO_NUM_9);
    keys.mode = GPIO_MODE_INPUT; keys.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&keys);
#endif
#if defined(ASSISTANT_BOARD_YUNLIAO)
    gpio_config_t boost = {};
    boost.pin_bit_mask = 1ULL << GPIO_NUM_3;
    boost.mode = GPIO_MODE_OUTPUT;
    boost.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&boost), "board_power", "Yunliao 5V boost GPIO");
    return gpio_set_level(GPIO_NUM_3, 1);
#endif
#if defined(ASSISTANT_BOARD_MAZA)
    gpio_config_t power = {};
#if defined(ASSISTANT_BOARD_MAZA_S31)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_2, 0), "board_power", "Maza S31 PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_2; amplifier.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), "board_power", "Maza S31 PA readback pad");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_6, 1), "board_power", "Maza S31 power latch");
    power.pin_bit_mask = 1ULL << GPIO_NUM_6;
#else
    power.pin_bit_mask = 1ULL << GPIO_NUM_12;
#endif
    power.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&power), "board_power", "Maza power GPIO");
#if defined(ASSISTANT_BOARD_MAZA_S31)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_6, 1), "board_power", "Maza S31 power high");
#else
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_12, 1), "board_power", "Maza power high");
#endif
    gpio_config_t keys = {};
#if defined(ASSISTANT_BOARD_MAZA_S31)
    keys.pin_bit_mask = (1ULL << GPIO_NUM_12) | (1ULL << GPIO_NUM_4) | (1ULL << GPIO_NUM_61);
#else
    keys.pin_bit_mask = (1ULL << GPIO_NUM_10) | (1ULL << GPIO_NUM_8) | (1ULL << GPIO_NUM_0);
#endif
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&keys);
#endif
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
    gpio_config_t keys = {};
    keys.pin_bit_mask = (1ULL << GPIO_NUM_1) | (1ULL << GPIO_NUM_2);
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&keys), "board_power", "Kevin volume keys");
#endif
#if defined(ASSISTANT_BOARD_HU087)
    // The single GPIO18 key is initialized by the common input task. GPIO17
    // belongs to the digital audio amplifier, not to the breadboard key set.
    return ESP_OK;
#elif defined(ASSISTANT_BOARD_OLED) && !defined(ASSISTANT_BOARD_KEVIN_BOX2)
    gpio_config_t keys = {};
#if defined(ASSISTANT_BOARD_WK)
    keys.pin_bit_mask = (1ULL << GPIO_NUM_38) | (1ULL << GPIO_NUM_39);
#else
    keys.pin_bit_mask = (1ULL << GPIO_NUM_40) | (1ULL << GPIO_NUM_39);
#endif
#if !defined(ASSISTANT_BOARD_OLED_CUBE) && !defined(ASSISTANT_BOARD_WK)
    keys.pin_bit_mask |= 1ULL << GPIO_NUM_47;
#endif
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&keys), "board_power", "OLED hardware keys");
#if defined(ASSISTANT_BOARD_OLED_CUBE)
    ESP_RETURN_ON_ERROR(rtc_gpio_hold_dis(GPIO_NUM_21), "board_power", "OLED supply hold");
    ESP_RETURN_ON_ERROR(rtc_gpio_init(GPIO_NUM_21), "board_power", "OLED supply RTC");
    ESP_RETURN_ON_ERROR(rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_OUTPUT_ONLY), "board_power", "OLED supply direction");
    ESP_RETURN_ON_ERROR(rtc_gpio_set_level(GPIO_NUM_21, 1), "board_power", "OLED supply high");
#endif
    return ESP_OK;
#elif defined(ASSISTANT_BOARD_GENJUTECH)
    // The pinned board codec never disables output, citing a display/PA IO
    // conflict. Keep PA21 high; the physical routing still needs verification.
    gpio_config_t supply = {};
    supply.pin_bit_mask = 1ULL << spi_board::amplifier;
    supply.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&supply), "board_power", "shared display/PA GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::amplifier, 1), "board_power", "shared display/PA high");
    gpio_config_t keys = {};
    keys.pin_bit_mask = (1ULL << GPIO_NUM_42) | (1ULL << GPIO_NUM_41);
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&keys);
#elif defined(ASSISTANT_BOARD_XINGZHI_TFT) || defined(ASSISTANT_BOARD_ZHENGCHEN_TFT) || defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_DUCHATX)
#if defined(ASSISTANT_BOARD_SPOTPEAR)
    constexpr gpio_num_t supply_pin = GPIO_NUM_3;
#elif defined(ASSISTANT_BOARD_DUCHATX)
    constexpr gpio_num_t supply_pin = GPIO_NUM_1;
#elif defined(ASSISTANT_BOARD_XINGZHI_TFT) || defined(ASSISTANT_BOARD_XINGZHI_085)
    constexpr gpio_num_t supply_pin = GPIO_NUM_21;
#else
    constexpr gpio_num_t supply_pin = GPIO_NUM_2;
#endif
    // Match the board's initial power-on state, including release of a hold
    // inherited from a previous deep sleep. No battery estimate is fabricated.
    esp_err_t result = rtc_gpio_hold_dis(supply_pin);
    if (result == ESP_OK) result = rtc_gpio_init(supply_pin);
    if (result == ESP_OK) result = rtc_gpio_set_direction(supply_pin, RTC_GPIO_MODE_OUTPUT_ONLY);
    if (result == ESP_OK) result = rtc_gpio_set_level(supply_pin, 1);
    if (result != ESP_OK) ESP_LOGE("board_power", "TFT supply setup failed: %s", esp_err_to_name(result));
#if defined(ASSISTANT_BOARD_XINGZHI_085)
    if (result == ESP_OK) {
        gpio_config_t auxiliary = {};
        auxiliary.pin_bit_mask = 1ULL << GPIO_NUM_45;
        auxiliary.mode = GPIO_MODE_OUTPUT;
        auxiliary.pull_down_en = GPIO_PULLDOWN_ENABLE;
        result = gpio_config(&auxiliary);
        if (result == ESP_OK) result = gpio_set_level(GPIO_NUM_45, 0);
    }
#endif
    return result;
#elif defined(ASSISTANT_BOARD_XINGZHI_ABS) || defined(ASSISTANT_BOARD_XINGZHI_METAL)
    // Preserve the upstream initial supply-high state. Its later shutdown uses
    // USB ADC checks and repeated47 pulses, not the simple latch-key task.
    gpio_config_t supply = {};
#if defined(ASSISTANT_BOARD_XINGZHI_METAL)
    constexpr gpio_num_t supply_pin = GPIO_NUM_48;
#else
    constexpr gpio_num_t supply_pin = GPIO_NUM_47;
#endif
    supply.pin_bit_mask = 1ULL << supply_pin;
    supply.mode = GPIO_MODE_OUTPUT;
    supply.pull_down_en = GPIO_PULLDOWN_ENABLE;
    esp_err_t result = gpio_config(&supply);
    if (result == ESP_OK) result = gpio_set_level(supply_pin, 1);
#if defined(ASSISTANT_BOARD_XINGZHI_METAL)
    gpio_config_t power_key = {};
    power_key.pin_bit_mask = 1ULL << GPIO_NUM_47;
    power_key.mode = GPIO_MODE_INPUT;
    if (result == ESP_OK) result = gpio_config(&power_key);
#endif
    if (result != ESP_OK) ESP_LOGE("board_power", "supply setup failed board=%s error=%s", board_profile::name, esp_err_to_name(result));
    return result;
#elif defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_LCD_154) || defined(ASSISTANT_BOARD_LCD_085) || (defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185))
    // The battery power latch must stay asserted after the user releases PWR.
    // Pinned configs:1.32=18/17; LCD1.54/0.85=2/5; LCD1.46/original1.85=7/6 (latch/key).
    gpio_config_t latch = {};
    latch.pin_bit_mask = 1ULL << kLatchPin;
    latch.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&latch), TAG, "power latch configuration");
    ESP_RETURN_ON_ERROR(gpio_set_level(kLatchPin, 1), TAG, "power latch hold");
    gpio_config_t button = {};
    button.pin_bit_mask = 1ULL << kPowerKey;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&button), TAG, "power button input");
    if (xTaskCreate(power_button_task, "power_button", 2048, nullptr, 3, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
#elif defined(ASSISTANT_BOARD_AMOLED_18) || defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_216) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)) || defined(ASSISTANT_BOARD_LCD_183) || defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_WS4B) || defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_WS_EPAPER397)
    // Fixed upstream uses ALDO1 for Waveshare MIC,plus ALDO2 on2.06.
    // Kevin uses ALDO2 only. Verify the selected voltage and enable bits.
    // Preserve other rails, charger values and power-button configuration.
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
    ESP_RETURN_ON_ERROR(bsp_codec_i2c_init(), TAG, "power I2C1");
    const auto power_bus = bsp_codec_i2c_get_handle();
#else
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "power I2C");
    const auto power_bus = bsp_i2c_get_handle();
#endif
    if (!power_bus) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = 0x34;
    config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t device = nullptr;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(power_bus, &config, &device), TAG, "AXP2101 device");
#if defined(ASSISTANT_BOARD_WS_EPAPER397)
    // Schematic: ALDO2 feeds MIC/ES8311 analog; ALDO3 controls EPD supply.
    // ALDO1 reaches PA only through unpopulated R74; PA39 owns fitted R23.
    constexpr uint8_t enabled_mask = 6, voltage_base = 0x93;
    constexpr int rail_count = 2;
#elif defined(ASSISTANT_BOARD_KEVIN_BOX2)
    constexpr uint8_t enabled_mask = 2, voltage_base = 0x93;
    constexpr int rail_count = 1;
#elif defined(ASSISTANT_BOARD_AMOLED_206)
    constexpr uint8_t voltage_base = 0x92;
    constexpr uint8_t enabled_mask = 3;
    constexpr int rail_count = 2;
#else
    constexpr uint8_t voltage_base = 0x92;
    constexpr uint8_t enabled_mask = 1;
    constexpr int rail_count = 1;
#endif
    const uint8_t enable_register = 0x90;
    uint8_t previous_enable = 0, enabled = 0;
    esp_err_t result = i2c_master_transmit_receive(device, &enable_register, 1, &previous_enable, 1, 100);
    for (int rail = 0; result == ESP_OK && rail < rail_count; ++rail) {
        const uint8_t voltage_register = voltage_base + rail;
        uint8_t voltage = 0;
        result = i2c_master_transmit_receive(device, &voltage_register, 1, &voltage, 1, 100);
#if defined(ASSISTANT_BOARD_WS_EPAPER397)
        const uint8_t desired_voltage = (voltage & 0xe0) | 28;
#else
        const uint8_t desired_voltage = 28;
#endif
        const uint8_t command[] = {voltage_register, desired_voltage};
        if (result == ESP_OK && voltage != desired_voltage) result = i2c_master_transmit(device, command, sizeof(command), 100);
        if (result == ESP_OK) result = i2c_master_transmit_receive(device, &voltage_register, 1, &voltage, 1, 100);
        if (result == ESP_OK && voltage != desired_voltage) result = ESP_ERR_INVALID_STATE;
    }
    const uint8_t enable_command[] = {enable_register, static_cast<uint8_t>(previous_enable | enabled_mask)};
    if (result == ESP_OK && (previous_enable & enabled_mask) != enabled_mask) result = i2c_master_transmit(device, enable_command, sizeof(enable_command), 100);
    if (result == ESP_OK) result = i2c_master_transmit_receive(device, &enable_register, 1, &enabled, 1, 100);
    if (result == ESP_OK && (enabled & enabled_mask) != enabled_mask) result = ESP_ERR_INVALID_STATE;
    const esp_err_t cleanup = i2c_master_bus_rm_device(device);
    if (result == ESP_OK) result = cleanup;
    ESP_RETURN_ON_ERROR(result, TAG, "AXP2101 rail setup/readback");
    ESP_LOGI(TAG, "rails_ready board=%s millivolts=3300 mask=0x%02x previous_enable=0x%02x enable=0x%02x", board_profile::name, enabled_mask, previous_enable, enabled);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
#else
    return ESP_OK;
#endif
}
