#include "status_led.h"

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

static const char *TAG = "StatusLed";

static led_strip_handle_t s_status_led;
static bool s_status_led_ready;
static int64_t s_data_flash_until_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint8_t led_scale(uint8_t value)
{
#if CONFIG_REMOTE_AGENT_STATUS_LED_ENABLED
    return (uint8_t)((uint16_t)value * CONFIG_REMOTE_AGENT_STATUS_LED_BRIGHTNESS / 255);
#else
    return 0;
#endif
}

static void status_led_apply(uint8_t red, uint8_t green, uint8_t blue)
{
#if CONFIG_REMOTE_AGENT_STATUS_LED_ENABLED
    if (!s_status_led_ready) {
        return;
    }

    led_strip_set_pixel(s_status_led, 0, led_scale(red), led_scale(green), led_scale(blue));
    led_strip_refresh(s_status_led);
#else
    (void)red;
    (void)green;
    (void)blue;
#endif
}

void status_led_set_disconnected(void)
{
    status_led_apply(255, 0, 0);
}

void status_led_set_connected(void)
{
    status_led_apply(0, 255, 0);
}

void status_led_note_data(void)
{
    s_data_flash_until_ms = now_ms() + 80;
    status_led_apply(0, 0, 255);
}

void status_led_tick(bool relay_online)
{
    if (s_data_flash_until_ms > 0 && now_ms() >= s_data_flash_until_ms) {
        s_data_flash_until_ms = 0;
        if (relay_online) {
            status_led_set_connected();
        } else {
            status_led_set_disconnected();
        }
    }
}

esp_err_t status_led_init(void)
{
#if CONFIG_REMOTE_AGENT_STATUS_LED_ENABLED
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_REMOTE_AGENT_STATUS_LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_status_led);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status LED init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_status_led_ready = true;
    led_strip_clear(s_status_led);
    status_led_set_disconnected();
#endif
    return ESP_OK;
}
