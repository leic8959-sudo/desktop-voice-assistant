#include "nt26_network.h"

#include <atomic>
#include <new>
#include "driver/gpio.h"
#include "esp_idf_version.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "uart_eth_modem.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 1)
#error "uart-eth-modem0.7.0 requires ESP-IDF6.0.1 or newer"
#endif

namespace {
constexpr const char *TAG = "nt26_network";
// Retain the modem for the firmware lifetime. Its workers may still be stopping
// after a startup failure; destroying it without a successful Stop is unsafe.
UartEthModem *g_modem;
std::atomic<bool> g_address{false};
bool g_started, g_ready;

void network_event(UartEthModem::UartEthModemEvent event, const std::string &) {
    using Event = UartEthModem::UartEthModemEvent;
    switch (event) {
        case Event::Connected:
            // In0.7.0 Connected requires a DHCP result (or the driver's
            // retained IP plus data-link state on registration recovery).
            g_address = true;
            break;
        case Event::RequestingPdpContext:
        case Event::PlmnSearchFallback:
            break;
        default:
            g_address = false;
            break;
    }
    // Detailed reasons are already logged by the driver. Do not log modem
    // identifiers or issue blocking AT/Stop calls from its worker callback.
    ESP_LOGI(TAG, "network event=%s address_ready=%d", UartEthModem::GetNetworkEventName(event), g_address.load());
}
}

esp_err_t nt26_network_start() {
    if (g_ready) return ESP_OK;
    if (g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    const esp_err_t interrupts = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (interrupts != ESP_OK && interrupts != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service failed:%s", esp_err_to_name(interrupts));
        return interrupts;
    }
    UartEthModem::Config config = {};
    config.uart_num = UART_NUM_1; config.baud_rate = 3000000;
    config.tx_pin = GPIO_NUM_12; config.rx_pin = GPIO_NUM_11;
    config.mrdy_pin = GPIO_NUM_9; config.srdy_pin = GPIO_NUM_10;
    // The fixed S3 profile requires PSRAM. Keep the driver's bounded TX pool
    // and reassembly buffer there, preserving internal RAM for UART DMA/audio.
    config.use_psram = true;
    g_modem = new (std::nothrow) UartEthModem(config);
    if (!g_modem) return ESP_ERR_NO_MEM;
    g_modem->SetDebug(false);
    g_modem->SetNetworkEventCallback(network_event);
    const esp_err_t result = g_modem->Start();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "modem startup failed:%s;restart required", esp_err_to_name(result));
        return result;
    }
    g_ready = true;
    ESP_LOGI(TAG, "NT26 UART Ethernet startup pending;UART1 3Mbps TX12 RX11 MRDY9 SRDY10");
    return ESP_OK;
}

bool nt26_network_has_address() {
    // Do not borrow GetNetif(): the driver can destroy and recreate it during
    // modem-reset recovery. Read only its atomic initialization and our state.
    return g_ready && g_address.load() && g_modem->IsInitialized();
}
