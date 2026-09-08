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
static void damping_set_brightness(damping_data_t *data, uint8_t brightness_to)
{
    data->now_count = 0;
    uint8_t now_b = bsp_display_brightness_get();
    data->prev_brightness = now_b;
    data->delta_brightness = brightness_to - now_b;
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

void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    int32_t value = lv_slider_get_value(slider);
    if (code == LV_EVENT_RELEASED)
    {
        ESP_LOGI(TAG, "slider released at %d", (int)value);
        damping_set_brightness(&damping_data, (uint8_t)value);
    }
}

// void app_main(void)
// {

//     xTaskCreatePinnedToCore(damping_task_cb, "damping_task", 4096, NULL, 3, &damping_task_handle, 0);
// }