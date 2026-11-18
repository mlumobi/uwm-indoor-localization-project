#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_dashboard.h"
#include "ranging_log.h"
#include "uwb_ranging.h"

static const gpio_num_t SW1_GPIO = GPIO_NUM_3;
static const char *const TAG = "uwm";

enum {
    BUTTON_POLL_MS = 10,
    BUTTON_DEBOUNCE_US = 30000,
    BUTTON_LONG_PRESS_US = 1000000,
};

typedef struct {
    bool sampled_pressed;
    bool stable_pressed;
    int64_t sampled_since_us;
    int64_t pressed_at_us;
} button_test_t;

static void sample_button(button_test_t *button, bool pressed, int64_t now_us)
{
    if (pressed != button->sampled_pressed) {
        button->sampled_pressed = pressed;
        button->sampled_since_us = now_us;
    }

    if (pressed == button->stable_pressed ||
        now_us - button->sampled_since_us < BUTTON_DEBOUNCE_US) {
        return;
    }

    button->stable_pressed = pressed;
    if (pressed) {
        button->pressed_at_us = button->sampled_since_us;
        ESP_LOGI(TAG, "SW1 GPIO3 LOW (pressed)");
        return;
    }

    const int64_t duration_us = button->sampled_since_us - button->pressed_at_us;
    const char *kind = duration_us >= BUTTON_LONG_PRESS_US ? "LONG" : "SHORT";
    ESP_LOGI(TAG, "SW1 %s PRESS (%" PRId64 " ms)", kind, duration_us / 1000);
}

void app_main(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t console_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t console_err = usb_serial_jtag_driver_install(&console_config);
    if (console_err == ESP_OK) {
        // The interrupt-driven console terminates full 64-byte USB transfers.
        usb_serial_jtag_vfs_use_driver();
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(console_err);
    }
#endif

    ESP_LOGI(TAG, "Indoor localization firmware started on ESP32-C3");

    // SW1 shorts GPIO3 to ground. The internal pull-up supplements PCB resistor R8.
    const gpio_config_t button_config = {
        .pin_bit_mask = UINT64_C(1) << SW1_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));
    ESP_LOGI(TAG, "SW1 test ready on GPIO3 (active low). Long press: 1000 ms or more.");

    ESP_LOGI(TAG, "SW1 GPIO3 at startup with pull-up: %s", gpio_get_level(SW1_GPIO) ? "HIGH" : "LOW");

    ESP_ERROR_CHECK_WITHOUT_ABORT(ranging_log_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(node_dashboard_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(uwb_ranging_start());

    button_test_t button = {0};
    while (true) {
        const int64_t now_us = esp_timer_get_time();
        sample_button(&button, gpio_get_level(SW1_GPIO) == 0, now_us);

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}
