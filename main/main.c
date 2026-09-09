#include "battery_manager.hpp"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
battery_manage_config_t battery_config = {0};
void app_main()
{
    bsp_display_start();
    battery_init(&battery_config);
}