#include "includes/damping.h"

static const char *TAG = "stu6";
// static TaskHandle_t damping_task_handle;

damping_data_t damping_data = {
    .max_count = DAMPING_DURING,
    .now_count = 0,
    .prev_brightness = 100,
    .delta_brightness = 0,
};

// 以阻尼的方式,动态调整屏幕亮度
void damping_set_brightness(uint8_t brightness_to)
{
    uint8_t now_b = bsp_display_brightness_get();
    damping_data.now_count = 0;
    damping_data.prev_brightness = now_b;
    damping_data.delta_brightness = brightness_to - now_b;
}
void damping_task_cb(void *arg)
{
    uint8_t b;
    float p = 0.0;
    const TickType_t xFrequency = pdMS_TO_TICKS(20);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        // 等待到下一个周期
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        damping_data.now_count += 1;
        if (damping_data.now_count >= damping_data.max_count)
        {
            p = 1.0;
            damping_data.now_count = damping_data.max_count;
            damping_data.delta_brightness = 0;
            continue;
        }
        p = (float)damping_data.now_count / (float)damping_data.max_count;
        float curve = p / (p * EASING_CASE + (1.0 - EASING_CASE));

        b = damping_data.prev_brightness + (uint8_t)(curve * damping_data.delta_brightness);
        bsp_display_lock(-1);
        bsp_display_brightness_set(b);
        bsp_display_unlock();
        // ESP_LOGI(TAG, "brightness is %d", brightness);
    }
    vTaskDelete(NULL);
}
