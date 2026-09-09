
#include "battery_manager.hpp"
#include "bsp/esp-bsp.h"
// 引入 XPowers 库头文件
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "AXP2101_MONITOR";

// 定义 I2C 引脚 (根据你的硬件开发板修改，如 T-Beam-S3 或自定义板)
#define PMU_I2C_SDA (gpio_num_t) CONFIG_PMU_I2C_SDA
#define PMU_I2C_SCL (gpio_num_t) CONFIG_PMU_I2C_SCL
#define I2C_MASTER_PORT_NUM (i2c_port_num_t) CONFIG_I2C_MASTER_PORT_NUM
#define I2C_MASTER_FREQUENCY CONFIG_I2C_MASTER_FREQUENCY

// 实例化 AXP2101 对象
static XPowersAXP2101 PMU;
i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t pmu_dev_handle = NULL;

static esp_err_t i2c_master_init(void)
{
    i2c_bus_handle = bsp_i2c_get_handle();
    if (i2c_bus_handle == NULL)
    {
        return ESP_FAIL;
    }

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = 0x34; /* AXP2101 slave address */
    dev_config.scl_speed_hz = I2C_MASTER_FREQUENCY;
    dev_config.scl_wait_us = 0;
    dev_config.flags.disable_ack_check = 0;

    return i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &pmu_dev_handle);
}

/**
 * @brief 电池状态监控任务
 */
void battery_monitor_task(void *pvParameters)
{
    battery_manage_config_t *bct = (battery_manage_config_t *)pvParameters;
    while (1)
    {
        // 1. 检查电池是否连接
        bool is_bat_connected = PMU.isBatteryConnect();

        if (!is_bat_connected)
        {
            ESP_LOGW(TAG, "电池未连接！");
        }
        else
        {
            // 2. 读取电池电压 (mV)
            uint16_t bat_voltage_mv = PMU.getBattVoltage();

            // 3. 读取电量百分比 (%)
            uint8_t bat_percent = PMU.getBatteryPercent();

            // 4. 检查充电状态
            bool is_charging = PMU.isCharging();
            bool is_vbus_in = PMU.isVbusIn(); // 检查是否有 USB/VBUS 插入
            // 5. 获取充/放电电流 (mA)
            // 注：AXP2101 库中充电电流为正，放电电流通过 getBatteryDischargeCurrent 获取
            // int charge_current_ma = 0;
            // if (is_charging)
            // {
            //     charge_current_ma = PMU.getBattChargeCurrent();
            // }
            bct->is_charging = is_charging;
            bct->bat_percent = bat_percent;

            // 打印日志
            ESP_LOGI(TAG, "----------------------------------------");
            ESP_LOGI(TAG, "VBUS (USB) 插入状态 : %s", is_vbus_in ? "YES" : "NO");
            ESP_LOGI(TAG, "电池状态           : %s", bct->is_charging ? "充电中 (Charging)" : "放电中/未充电");
            ESP_LOGI(TAG, "电池电压           : %u mV (%.2f V)", bat_voltage_mv, bat_voltage_mv / 1000.0);
            ESP_LOGI(TAG, "剩余电量           : %u %%", bct->bat_percent);
            // if (is_charging)
            // {
            //     ESP_LOGI(TAG, "充电电流           : %d mA", charge_current_ma);
            // }
        }

        vTaskDelay(pdMS_TO_TICKS(3000)); // 每 3 秒刷新一次
    }
}

extern "C" void battery_init(battery_manage_config_t *btc)
{
    ESP_LOGI(TAG, "初始化 I2C 总线...");
    ESP_ERROR_CHECK(i2c_master_init());

    ESP_LOGI(TAG, "初始化 AXP2101 PMU...");

    // 初始化 AXP2101 (传入自定义的 I2C 读写适配函数，或直接传入端口)
    // xpowerslib 支持传入 ESP-IDF 的 i2c_port_t
    if (!PMU.begin(i2c_bus_handle, AXP2101_SLAVE_ADDRESS))
    {
        ESP_LOGE(TAG, "AXP2101 初始化失败！请检查 I2C 引脚或芯片地址。");
        return;
    }

    ESP_LOGI(TAG, "AXP2101 初始化成功，芯片 ID: 0x%X", PMU.getChipID());

    // 开启电池电量计 (Fuel Gauge) ADC 功能，确保读数准确
    // PMU.enableBatteryVoltageMeasure();
    PMU.enableVbusVoltageMeasure();
    PMU.enableSystemVoltageMeasure();

    // 创建后台监控任务
    xTaskCreate(battery_monitor_task, "bat_monitor", 4096, btc, 5, NULL);
}