"""提取实际显示外设复位函数，以已有WSL编译器检查顺序、掩码和失败传播。"""
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    path = ROOT / "firmware/assistant_espidf/main/display_ui.cpp"
    source = path.read_text(encoding="utf-8")
    start = source.index("static esp_err_t reset_display_peripherals() {")
    end = source.index("\nesp_err_t display_ui_init()", start)
    function = source[start:end]
    harness = r'''
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using esp_err_t = int;
using esp_io_expander_handle_t = void*;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = 10, IO_EXPANDER_OUTPUT = 0;
constexpr uint32_t IO_EXPANDER_PIN_NUM_0 = 1, IO_EXPANDER_PIN_NUM_1 = 2, IO_EXPANDER_PIN_NUM_2 = 4;
static std::vector<std::string> events;
static int failure, operations;
static uint32_t output = 0xa8;
static esp_io_expander_handle_t bsp_io_expander_init() { events.push_back("init"); return failure == 1 ? nullptr : reinterpret_cast<void*>(1); }
static int esp_io_expander_set_dir(void*, uint32_t mask, int direction) {
    if (mask != 7 || direction != IO_EXPANDER_OUTPUT) throw std::runtime_error("wrong direction or unrelated pins");
    events.push_back("dir"); return ++operations == failure - 1 ? 33 : 0;
}
static int esp_io_expander_set_level(void*, uint32_t mask, int value) {
    if (mask != 7) throw std::runtime_error("unrelated pins touched");
    events.push_back(value ? "high" : "low");
    if (++operations == failure - 1) return 33;
    output = value ? output | mask : output & ~mask;
    return 0;
}
static void vTaskDelay(int ms) { events.push_back("wait" + std::to_string(ms)); }
#define pdMS_TO_TICKS(value) (value)
#define ESP_RETURN_ON_ERROR(expression, tag, ...) do { int error = (expression); if (error) return error; } while(0)
#define ESP_LOGI(...) events.push_back("success")
// ACTUAL_FUNCTION
static void require(bool value) { if (!value) throw std::runtime_error("assertion failed"); }
int main() {
    for (failure = 0; failure <= 4; ++failure) {
        events.clear(); operations = 0; output = 0xa8;
        const int result = reset_display_peripherals();
        require((output & ~7u) == 0xa8);
        if (!failure) { require(result == 0); require(events == std::vector<std::string>{"init","dir","low","wait20","high","wait150","success"}); require(output == 0xaf); }
        if (failure == 1) { require(result == ESP_ERR_INVALID_STATE); require(events == std::vector<std::string>{"init"}); }
        if (failure == 2) { require(result == 33); require(events == std::vector<std::string>{"init","dir"}); }
        if (failure == 3) { require(result == 33); require(events == std::vector<std::string>{"init","dir","low"}); }
        if (failure == 4) { require(result == 33); require(events == std::vector<std::string>{"init","dir","low","wait20","high"}); }
    }
    std::cout << "PASS 5 scenarios: order, reset timing, pins0-2 only, unrelated pins preserved, each failure propagated\n";
}
'''
    parent = ROOT / "runtime/host-checks"
    parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="display-reset-", dir=parent) as directory:
        translation = Path(directory) / "check.cpp"
        binary = Path(directory) / "check"
        translation.write_text(harness.replace("// ACTUAL_FUNCTION", function), encoding="utf-8")

        def wsl(path):
            value = str(path.resolve()).replace("\\", "/")
            return "/mnt/" + value[0].lower() + value[2:]

        subprocess.run(["wsl.exe", "-d", "Ubuntu-26.04", "--exec", "/usr/bin/g++", "-std=c++17", "-Wall", "-Wextra", wsl(translation), "-o", wsl(binary)], check=True, timeout=60)
        subprocess.run(["wsl.exe", "-d", "Ubuntu-26.04", "--exec", wsl(binary)], check=True, timeout=30)
    print("source_sha256=" + hashlib.sha256(path.read_bytes()).hexdigest())
    print("device_used=false firmware_built=false")


if __name__ == "__main__":
    main()
