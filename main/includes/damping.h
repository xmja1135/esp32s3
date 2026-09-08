#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#define DAMPING_DURING 120 // 阻尼持续值
#define EASING_CASE 0.8    // 缓动参数

// 阻尼状态
typedef struct
{
    uint16_t max_count;
    uint16_t now_count;
    uint8_t prev_brightness;
    int8_t delta_brightness;
} damping_data_t;



void damping_task_cb(void *arg);
void slider_event_cb(lv_event_t *e);