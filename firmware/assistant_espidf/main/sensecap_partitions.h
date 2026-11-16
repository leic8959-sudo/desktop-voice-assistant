#pragma once

#include "esp_partition.h"

// Run before default nvs_flash_init, device_config_load or any board writes.
// The factory range is raw/readonly; it is never mounted as default NVS.
inline bool sensecap_partition_layout_valid() {
    const auto *factory = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "nvsfactory");
    const auto *nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    const auto *ota = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, "otadata");
    const auto *app0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
    const auto *app1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "app1");
    const auto *model = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model");
    return factory && factory->address == 0x9000 && factory->size == 0x32000 && factory->readonly &&
        nvs && nvs->address == 0x3b000 && nvs->size == 0x4000 && !nvs->readonly &&
        ota && ota->address == 0x3f000 && ota->size == 0x2000 &&
        app0 && app0->address == 0x50000 && app0->size == 0x300000 &&
        app1 && app1->address == 0x350000 && app1->size == 0x300000 &&
        model && model->address == 0xc00000 && model->size == 0x400000;
}
