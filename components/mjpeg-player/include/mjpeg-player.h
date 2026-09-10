#pragma once

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_jpeg_dec.h"

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
enum album_event
{
    ANIME_PAUSE = 0x01,
    IDLE_DONE = 0x02,
    READY_DONE = 0x04,
    SWITCHING = 0x08,
};

void buffer_task_cb(void *arg);                   // 单帧队列,任务回调
void mjpeg_task_cb(void *arg);                    // 解码输出,任务回调
esp_err_t mjpeg_switch_album_file(bool isSwitch); // 打开/切换SD卡mjpeg相册组
esp_err_t mjpeg_close_album_file();               // 关闭文件
void mjpeg_player_init(lv_obj_t *show_obj);

EventBits_t mjpeg_group_get();     // 获取事件组状态
void mjpeg_group_set(uint8_t bit); // 设置事件组状态
esp_err_t mjpeg_group_clear();          // 清除事件组状态
