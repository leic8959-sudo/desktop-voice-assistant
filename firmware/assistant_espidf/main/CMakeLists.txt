# 版本号独立成文件并声明 configure 依赖：直接改 PROJECT_VER 不会触发
# esp_app_desc/编译定义的增量重编，OTA 版本比对会因此失效。
file(READ ${CMAKE_CURRENT_LIST_DIR}/version.txt PROJECT_VER_USER)
string(STRIP "${PROJECT_VER_USER}" PROJECT_VER_USER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/version.txt)
idf_component_register(
    SRCS "main.cpp" "device_config.cpp" "wifi_provisioning.cpp" "display_ui.cpp" "ui_font.c" "audio_io.cpp" "control_protocol.cpp" "ota_update.cpp" "storage.cpp" "wake_word.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash esp_http_server esp_websocket_client esp_http_client esp_https_ota esp-tls json driver esp_lcd fatfs sdmmc lvgl esp_lvgl_port esp32_s3_touch_amoled_1_8 esp-sr esp_codec_dev app_update esp_partition mbedtls
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ASSISTANT_FW_VERSION="${PROJECT_VER_USER}")
set_source_files_properties(display_ui.cpp PROPERTIES COMPILE_OPTIONS "-O2")
option(ASSISTANT_WAKE_PCM_PROBE "Run local PCM wake model diagnostics at startup" OFF)
if(ASSISTANT_WAKE_PCM_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ASSISTANT_WAKE_PCM_PROBE=1)
endif()
