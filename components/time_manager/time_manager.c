#include <stdio.h>
#include "time_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GET_TIME_INTERVAL (10 * 1000)

static const char *TAG = "Timenow";
static time_init_result_t time_result;
static TaskHandle_t time_task_handle;

// SNTP异步获取准确时间(在got ip之后使用它)
void time_sntp_async(void)
{
    time_service_sync_async(NULL);
}

// 获取当前时间
esp_err_t get_now_time(time_manage_t *time_config)
{

    time_service_init(&time_result);
    int hour, minute, second;
    size_t ret = time_service_now_str(28800, time_config->str, sizeof(time_config->str));
    if (ret == 0)
    {
        ESP_LOGE(TAG, "get time failed !!");
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "time_config->str: %s", time_config->str);
    sscanf(time_config->str, "%*d-%*d-%*d %d:%d:%d %*s", &hour, &minute, &second);
    time_config->hour = hour;
    time_config->minute = minute;
    snprintf(time_config->str, TIME_SERVICE_STR_BUF_SIZE, "%02d:%02d", hour, minute);
    ESP_LOGE(TAG, "now time is:hour-%d,minute-%d,str-[%s]", time_config->hour, time_config->minute, time_config->str);

    return ESP_OK;
}
// 任务
void time_task_cb(void *args)
{
    time_manage_t *config = (time_manage_t *)args;
    for (;;)
    {
        get_now_time(config);
        vTaskDelay(pdMS_TO_TICKS(GET_TIME_INTERVAL));
    }
    vTaskDelete(NULL);
}

// 初始化
void time_manager_init(time_manage_t *config)
{
    xTaskCreate(time_task_cb, "my_time_task", 4096, config, 4, &time_task_handle);
}