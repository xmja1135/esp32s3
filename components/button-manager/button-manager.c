#include <stdio.h>
#include "button-manager.h"

static const char *TAG = "button_demo";

void button_init(button_cb_t button_single_click_cb)
{
    // 1. 配置按键
    const button_config_t btn1_cfg = {0}; // 使用默认配置
    const button_gpio_config_t btn1_gpio_cfg = {
        .gpio_num = BUTTON_GPIO_NUM,
        .active_level = BUTTON_ACTIVE_LEVEL, // 有效电平
    };

    // 2. 创建按键设备
    button_handle_t btn1 = NULL;
    iot_button_new_gpio_device(&btn1_cfg, &btn1_gpio_cfg, &btn1);
    if (btn1 == NULL)
    {
        ESP_LOGE(TAG, "Failed to create button");
        return;
    }

    // 3. 注册事件回调
    iot_button_register_cb(btn1, BUTTON_SINGLE_CLICK, NULL, button_single_click_cb, NULL);
    iot_button_register_cb(btn1, BUTTON_DOUBLE_CLICK, NULL, button_single_click_cb, NULL);

    ESP_LOGI(TAG, "Button test started!");
}
