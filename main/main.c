// #include <stdio.h>

// void app_main(void)
// {
//     get_now_time(&time_config);
// }

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "time_manager.h"
time_manage_t time_config = {0};

/*  事件组mask
    WIFI_EVENT_STA_START                MYBIT0
    WIFI_EVENT_STA_CONNECTED            MYBIT1
    WIFI_EVENT_STA_DISCONNECTED         MYBIT2
    WIFI_EVENT_STA_AUTHMODE_CHANGE      MYBIT3
    IP_EVENT_STA_GOT_IP                 MYBIT4
    IP_EVENT_STA_LOST_IP                MYBIT5
    WIFI_EVENT_SCAN_DONE                MYBIT6
*/
#define MYBIT0 1 << 0
#define MYBIT1 1 << 1
#define MYBIT2 1 << 2
#define MYBIT3 1 << 3
#define MYBIT4 1 << 4
#define MYBIT5 1 << 5
#define MYBIT6 1 << 6

#define DEFAULT_SCAN_LIST_SIZE 11

static const char *TAG = "wifi station";
static uint16_t retry_count = 0;
static uint16_t retry_delay_s = 0;

static EventGroupHandle_t wifi_eventgroup;
static TaskHandle_t wifi_task_handle;
static wifi_config_t wifi_config = {
    .sta = {
        .ssid = "1407",
        .password = "15651921070",
        // 智能连接优化：设置阈值，信号弱于-75dBm时不尝试连接
        .threshold.rssi = -127,
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
    }};

static wifi_scan_config_t scan_config;
static uint16_t scan_number = 0;
static uint16_t hold_number = DEFAULT_SCAN_LIST_SIZE;
static wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

static void wifi_init_driver();
static void wifi_init_sta();
// wifi事件,softAP配网事件回调
static void event_handler(void *event_handler_arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id != WIFI_EVENT_STA_DISCONNECTED)
        {
            retry_count = 0;
            retry_delay_s = 0;
        }
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
        {

            ESP_LOGI(TAG, "Wi-Fi STA started");
            xEventGroupSetBits(wifi_eventgroup, MYBIT0);
            break;
        }
        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "Connected to AP: %s, Channel: %d",
                     event->ssid, event->channel);
            xEventGroupSetBits(wifi_eventgroup, MYBIT1);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            xEventGroupSetBits(wifi_eventgroup, MYBIT2);
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
        {
            xEventGroupSetBits(wifi_eventgroup, MYBIT6);
            break;
        }
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_eventgroup, MYBIT4);
            break;
        }
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "Lost IP address");
            xEventGroupSetBits(wifi_eventgroup, MYBIT5);
            break;
        }
    }
}

void wifi_task_cb(void *arg)
{

    for (;;)
    {
        EventBits_t wait_mask = xEventGroupWaitBits(wifi_eventgroup, 255, pdTRUE, pdFALSE, portMAX_DELAY);
        switch (wait_mask & 255)
        {
        case MYBIT0:
            // 可以在这里触发自动连接
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        case MYBIT1:
            break;
        case MYBIT2:
            // 处理断开，如前文所示
            vTaskDelay(pdMS_TO_TICKS(1000 * retry_delay_s));
            ESP_LOGI(TAG, "DisConnected!!");
            retry_delay_s = 1 << retry_count++;
            if (retry_delay_s > 30)
            {
                retry_count = 30;
            }
            if (retry_count > 5)
            {

                ESP_LOGI(TAG, "重复连接次数过多,准备进入配对状态...");

                // esp_wifi_scan_start(&scan_config, false);

                retry_count = 0;
                retry_delay_s = 0;
            }
            else
            {
                ESP_LOGI(TAG, "重新连接中...");

                ESP_ERROR_CHECK(esp_wifi_connect());
            }

            break;
        case MYBIT3:
            break;
        case MYBIT4:

            // 此时可以启动MQTT、HTTP等网络服务
            time_sntp_async();
            break;
        case MYBIT5:
            // 停止网络服务
            ESP_ERROR_CHECK(esp_wifi_disconnect());
            // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
            break;
        case MYBIT6:
            // 扫描结束
            scan_number = 0;
            hold_number = DEFAULT_SCAN_LIST_SIZE;
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&scan_number));
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&hold_number, ap_info));
            for (int i = 0; i < hold_number; i++)
            {
                ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
                ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
                // print_auth_mode(ap_info[i].authmode);
                // if (ap_info[i].akm_dpp)
                // {
                //     ESP_LOGI(TAG, "DPP \t\tSupported%s", (ap_info[i].authmode != WIFI_AUTH_DPP) ? " (mixed mode)" : " (DPP-only)");
                // }
                // if (ap_info[i].authmode != WIFI_AUTH_WEP)
                // {
                //     print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
                // }
                ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
            }
            break;

        default:
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void wifi_init_sta()
{
    // 配置STA参数
    wifi_config_t nvs_wifi_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &nvs_wifi_config) == ESP_OK)
    {
        if (strlen((char *)nvs_wifi_config.sta.ssid) > 0)
        {
            ESP_LOGI(TAG, "Found saved WiFi: %s", nvs_wifi_config.sta.ssid);
            wifi_config = nvs_wifi_config;
        }
    }
    // 设置模式并启动
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}
static void wifi_init_driver()
{
    // 初始化事件组
    wifi_eventgroup = xEventGroupCreate();
    // 初始化任务
    xTaskCreate(wifi_task_cb, "my_wifi_task", 4096, NULL, 4, &wifi_task_handle);

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND || ret == ESP_ERR_NVS_INVALID_STATE)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // 初始化TCP/IP栈和默认事件循环
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    // 初始化wifi驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // 注册综合wifi事件处理器
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL);
}

void app_main(void)
{
    wifi_init_driver();
    wifi_init_sta();
    time_manager_init(&time_config);
}
