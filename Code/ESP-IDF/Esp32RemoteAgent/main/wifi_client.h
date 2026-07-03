#pragma once

#include "esp_err.h"
#include "remote_config.h"

esp_err_t wifi_client_start(const remote_config_t *config);
void wifi_client_wait_connected(void);
