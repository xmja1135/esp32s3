#pragma once

#include "esp_log.h"
#include "iot_button.h"
#include "button_gpio.h"

#define BUTTON_GPIO_NUM 18    // 根据你的硬件修改GPIO号
#define BUTTON_ACTIVE_LEVEL 0 // 0: 低电平有效，1: 高电平有效

void button_init(button_cb_t button_single_click_cb);
