#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t status_led_init(void);
void status_led_set_disconnected(void);
void status_led_set_connected(void);
void status_led_note_data(void);
void status_led_tick(bool relay_online);
