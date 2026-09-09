
#include "time_service.h"
#include "esp_log.h"
#include "esp_err.h"

typedef struct
{
    uint8_t hour;
    uint8_t minute;
    char str[TIME_SERVICE_STR_BUF_SIZE];
} time_manage_t;

esp_err_t get_now_time(time_manage_t *time_config);
void time_task_cb(void *arg);
void time_sntp_async(void);
void time_manager_init(time_manage_t *);