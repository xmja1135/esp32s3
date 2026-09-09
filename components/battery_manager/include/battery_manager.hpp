#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

typedef struct
{
    bool is_charging;
    uint8_t bat_percent;

} battery_manage_config_t;

void battery_monitor_task(void *pvParameters);

#ifdef __cplusplus
extern "C"
{
#endif

    void battery_init(battery_manage_config_t *);

#ifdef __cplusplus
}
#endif