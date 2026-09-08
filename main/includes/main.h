#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "esp_jpeg_dec.h"
#include "iot_button.h"
#include "button_gpio.h"

/* 声明结构体/联合体 ~~~~~~~~~~~~~~~~~~~~ */
typedef struct
{
    uint8_t *buffer;
    size_t len;
} frame_packet;
// 播放控制句柄与状态
typedef struct
{
    FILE *file;
    uint8_t *rgb_buf;
    size_t rgb_len;
    lv_img_dsc_t img_dsc;
    lv_obj_t *img_obj;
    jpeg_dec_handle_t jpeg_dec;
} mjpeg_player_t;

// 定义枚举,成员当作宏来使用
enum
{
    ANIME_PAUSE = 0x01,
    IDLE_DONE = 0x02,
    READY_DONE = 0x04,
    SWITCHING = 0x08,
} album_event;

/* 声明结构体/联合体 ~~~~~~~~~~~~~~~~~~~~ */

void build_unicode_utf8(uint16_t val, char *out_utf8);
