#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "xn_wifi_manage.h"

static const char *TAG = "stu7";

/**
 * @brief WiFi 管理状态回调
 *
 * 配网 AP / HTTP 服务器的开关由管理模块内部按状态自动完成，这里只做展示：
 * - 获取到 IP 后，AP 与 HTTP 服务器会被自动关闭；
 * - 连接失败后，AP 与 HTTP 服务器会被自动重新打开。
 */
void wifi_event_cb(wifi_manage_state_t state)
{
    switch (state)
    {
    case WIFI_MANAGE_STATE_DISCONNECTED:
        ESP_LOGI(TAG, "正在连接WIFI...");
        break;
    case WIFI_MANAGE_STATE_CONNECTED:
        ESP_LOGI(TAG, "已获取ip! 已自动关闭配网AP与HTTP服务器");
        break;
    case WIFI_MANAGE_STATE_CONNECT_FAILED:
        ESP_LOGI(TAG, "连接WIFI失败! 已自动打开配网AP与HTTP服务器");
        break;

    default:
        break;
    }
}
void app_main(void)
{
    wifi_manage_config_t config = WIFI_MANAGE_DEFAULT_CONFIG();
    config.wifi_event_cb = wifi_event_cb;
    esp_err_t ret = wifi_manage_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_manage_init failed: %s", esp_err_to_name(ret));
    }
}