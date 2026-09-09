#include <stdio.h>
#include "includes/button.h"

static const char *TAG = "button_demo";

// 按键事件回调函数
// static void button_single_click_cb(void *arg, void *usr_data)
// {
//     ESP_LOGI(TAG, "Button single click!");
// }

// extern void button_single_click_cb(void *arg, void *usr_data);
void button_init(button_cb_t button_single_click_cb)
{
    // 1. 配置按键
    const button_config_t btn_cfg = {0}; // 使用默认配置
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BUTTON_GPIO_NUM,
        .active_level = BUTTON_ACTIVE_LEVEL, // 有效电平
    };

    // 2. 创建按键设备
    button_handle_t btn = NULL;
    iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
    if (btn == NULL)
    {
        ESP_LOGE(TAG, "Failed to create button");
        return;
    }

    // 3. 注册事件回调
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, button_single_click_cb, NULL);

    ESP_LOGI(TAG, "Button test started!");
}
