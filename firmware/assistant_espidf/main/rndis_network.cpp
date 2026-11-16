#include "rndis_network.h"

#include <atomic>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "iot_usbh_cdc.h"
#include "iot_usbh_rndis.h"
#include "usb/usb_host.h"

#if !CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_CDC || !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#error "RNDIS requires UART0 console and no USB console on the shared USB PHY"
#endif

namespace {
constexpr const char *TAG = "rndis_network";
// Driver retains the match list. Keep it and all callback state alive even
// after a partial initialization failure; retry requires an explicit reboot.
usb_device_match_id_t g_match[2]{};
iot_eth_driver_t *g_driver;
iot_eth_handle_t g_eth;
iot_eth_netif_glue_handle_t g_glue;
esp_netif_t *g_netif;
esp_event_handler_instance_t g_eth_events, g_ip_events;
std::atomic<bool> g_link{false}, g_address{false};
bool g_started, g_ready;

void usb_events(void *) {
    while (true) {
        uint32_t flags = 0;
        const esp_err_t result = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (result != ESP_OK) {
            g_address = false; g_link = false;
            ESP_LOGE(TAG, "USB host event loop failed:%s;restart required", esp_err_to_name(result));
            vTaskDelete(nullptr);
            return;
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGE(TAG, "USB host has no clients;restart required");
        }
    }
}

void event(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == IOT_ETH_EVENT) {
        if (!data || *static_cast<iot_eth_handle_t *>(data) != g_eth) return;
        if (id == IOT_ETH_EVENT_CONNECTED) {
            // A fresh link must obtain its own DHCP result. An old nonzero IP
            // in esp_netif is insufficient proof after a USB unplug/replug.
            g_address = false; g_link = true;
            ESP_LOGI(TAG, "USB link connected;waiting for DHCP");
        } else if (id == IOT_ETH_EVENT_DISCONNECTED || id == IOT_ETH_EVENT_STOP) {
            g_address = false; g_link = false;
            ESP_LOGW(TAG, "USB link disconnected;address invalidated");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        const auto *ip = static_cast<const ip_event_got_ip_t *>(data);
        if (!ip || ip->esp_netif != g_netif || !g_link.load()) return;
        g_address = ip->ip_info.ip.addr != 0;
        ESP_LOGI(TAG, "RNDIS DHCP ready=%d", g_address.load());
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP) {
        // SDK5.5 posts ip_event_got_ip_t for the lost-address timer as well.
        const auto *ip = static_cast<const ip_event_got_ip_t *>(data);
        if (ip && ip->esp_netif == g_netif) {
            g_address = false;
            ESP_LOGW(TAG, "RNDIS DHCP address lost");
        }
    }
}
}

esp_err_t rndis_network_start() {
    if (g_ready) return ESP_OK;
    if (g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    // RNDIS0.3.1 starts its detection task during install, before ETH start.
    // Hold the root port off until netif/glue and callbacks are ready so a
    // module already plugged in cannot emit an early, lost link event.
    usb_host_config_t host = {};
    host.root_port_unpowered = true; host.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_RETURN_ON_ERROR(usb_host_install(&host), TAG, "USB host install");
    if (xTaskCreatePinnedToCore(usb_events, "rndis_usb_events", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    usbh_cdc_driver_config_t usb = {};
    usb.task_stack_size = 4096; usb.task_priority = configMAX_PRIORITIES - 1;
    usb.task_coreid = 0; usb.skip_init_usb_host_driver = true;
    ESP_RETURN_ON_ERROR(usbh_cdc_driver_install(&usb), TAG, "USB CDC host install");
    g_match[0].match_flags = USB_DEVICE_ID_MATCH_VID_PID;
    g_match[0].idVendor = USB_DEVICE_VENDOR_ANY; g_match[0].idProduct = USB_DEVICE_PRODUCT_ANY;
    iot_usbh_rndis_config_t rndis = {};
    rndis.match_id_list = g_match;
    ESP_RETURN_ON_ERROR(iot_eth_new_usb_rndis(&rndis, &g_driver), TAG, "RNDIS driver create");
    if (!g_driver) return ESP_ERR_INVALID_STATE;
    iot_eth_config_t eth = {};
    eth.driver = g_driver;
    ESP_RETURN_ON_ERROR(iot_eth_install(&eth, &g_eth), TAG, "RNDIS ETH install");
    esp_netif_inherent_config_t inherent = ESP_NETIF_INHERENT_DEFAULT_ETH();
    inherent.if_key = "XY_RNDIS"; inherent.if_desc = "USB RNDIS";
    esp_netif_config_t netif = {};
    netif.base = &inherent; netif.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    g_netif = esp_netif_new(&netif);
    if (!g_netif) return ESP_ERR_NO_MEM;
    g_glue = iot_eth_new_netif_glue(g_eth);
    if (!g_glue) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_netif_attach(g_netif, g_glue), TAG, "RNDIS netif attach");
    // Publish callback state only after all handles are initialized, before
    // start can emit link/DHCP events. The handles never change afterwards.
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IOT_ETH_EVENT, ESP_EVENT_ANY_ID, event, nullptr, &g_eth_events), TAG, "ETH events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, event, nullptr, &g_ip_events), TAG, "IP events");
    ESP_RETURN_ON_ERROR(iot_eth_start(g_eth), TAG, "RNDIS ETH start");
    ESP_RETURN_ON_ERROR(usb_host_lib_set_root_port_power(true), TAG, "USB root port enable");
    // Network startup is asynchronous. main owns its existing60second wait;
    // never block forever in a network driver constructor as the source did.
    g_ready = true;
    ESP_LOGI(TAG, "RNDIS installed;USB device must expose a working DHCP network");
    return ESP_OK;
}

bool rndis_network_has_address() {
    if (!g_ready || !g_link.load() || !g_address.load()) return false;
    esp_netif_ip_info_t info = {};
    return esp_netif_is_netif_up(g_netif) && esp_netif_get_ip_info(g_netif, &info) == ESP_OK && info.ip.addr;
}
