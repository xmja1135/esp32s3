#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#define DAMPING_DURING 120 // 阻尼持续值
#define EASING_CASE 0.9    // 缓动参数

// 阻尼状态
typedef struct
{
    uint16_t max_count;
    uint16_t now_count;
    uint8_t prev_brightness;
    int8_t delta_brightness;
} damping_data_t;

void damping_task_cb(void *arg);
void damping_set_brightness(uint8_t brightness_to);
void damping_init(void);