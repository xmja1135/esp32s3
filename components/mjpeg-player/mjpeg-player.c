#include "mjpeg-player.h"
#include "decode_mjpeg.h"

#define VIDEO_WIDTH 432
#define VIDEO_HEIGHT 243
#define JPEG_CHUNK_SIZE (32 * 1024) // jpeg最大单帧大小(字节)
#define ANIME_EXPECT_FRAME 30
#define ANIME_EXPECT_FRAME_MS (1000 / ANIME_EXPECT_FRAME)

struct esp_jpeg_stream jpeg_stream_handle = {0};
static TaskHandle_t mjpeg_task_handle;    // 解码输出出列任务
static TaskHandle_t buffer_task_handle;   // 单帧缓冲入列任务
static QueueHandle_t idle_queue;          // 空闲队列
static QueueHandle_t ready_queue;         // 就绪队列
static EventGroupHandle_t sw_album_group; // 相册切换任务组
static mjpeg_player_t g_player;           // 杂项(esp_new_jpeg和lvgl相关)

static const char *files_path[] = {
    "/sdcard/wallpaper/p3loop.mjpeg",
    "/sdcard/wallpaper/ravenda.mjpeg",
    // "/sdcard/wallpaper/persona.mjpeg",
    // "/sdcard/wallpaper/twins.mjpeg",
    "/sdcard/wallpaper/kurisu.mjpeg",
    "/sdcard/wallpaper/nier.mjpeg",
    "/sdcard/wallpaper/witch.mjpeg",
    "/sdcard/wallpaper/oldWoman.mjpeg",
    "/sdcard/wallpaper/ellen.mjpeg",
    "/sdcard/wallpaper/shower.mjpeg",
    // "/sdcard/wallpaper/link.mjpeg",
    "/sdcard/wallpaper/bochii.mjpeg",
    // "/sdcard/wallpaper/frieren1.mjpeg",
    "/sdcard/wallpaper/frieren2.mjpeg",
    "/sdcard/wallpaper/sunna.mjpeg",
    // "/sdcard/wallpaper/usagi.mjpeg",
};

static const char *TAG = "mjpeg-player";

void buffer_task_cb(void *arg)
{
    mjpeg_player_t *player = arg;
    frame_packet buf;
    bool isSwitch;
    for (;;)
    {
        EventBits_t bits = xEventGroupGetBits(sw_album_group);
        if (bits & ANIME_PAUSE)
        {
            // 停止移入ready队列
            xEventGroupSetBits(sw_album_group, IDLE_DONE);
            isSwitch = (bits & SWITCHING) ? true : false;
            // 必须队列初始化完成且使用切换相册时,任务才会再次播放动画
            if (bits & READY_DONE && isSwitch)
            {
                // 队列全部初始化完成,关闭file,切换相册
                if (mjpeg_close_album_file() == ESP_OK && mjpeg_switch_album_file(isSwitch) == ESP_OK)
                {
                    // 清除事件组,立即播放动画
                    xEventGroupClearBits(sw_album_group, 0x0F);
                }
            }
            isSwitch = false;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (xQueueReceive(idle_queue, &buf, portMAX_DELAY) == pdPASS)
        {
            size_t jpg_len = read_jpeg_frame(player->file, buf.buffer, JPEG_CHUNK_SIZE);

            if (jpg_len == 0)
            {
                // 读到了文件末尾（或解析失败），重置文件指针回到开头
                ESP_LOGI("MJPEG", "Video ended, looping...");
                fseek(player->file, 0, SEEK_SET); // 回到文件头
                // 重新读取开头的第 1 帧
                jpg_len = read_jpeg_frame(player->file, buf.buffer, JPEG_CHUNK_SIZE);
            }
            buf.len = jpg_len;
            xQueueSend(ready_queue, &buf, portMAX_DELAY);
        }
    }
}

void mjpeg_task_cb(void *arg)
{
    frame_packet buf;
    mjpeg_player_t *player = (mjpeg_player_t *)arg;
    ESP_LOGI(TAG, "MJPEG playback started");

    for (;;)
    {
        EventBits_t bits = xEventGroupGetBits(sw_album_group);
        if (bits & ANIME_PAUSE)
        {
            // 将ready队列全部移入idle队列,准备切换mjpeg
            if (xQueueReceive(ready_queue, &buf, pdMS_TO_TICKS(100)) == pdPASS)
            {
                xQueueSend(idle_queue, &buf, portMAX_DELAY);
            }
            else
            {
                ESP_LOGI(TAG, "闲置队列已满,允许切换!!");
                xEventGroupSetBits(sw_album_group, READY_DONE);
            }
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }
        if (xQueueReceive(ready_queue, &buf, portMAX_DELAY) == pdPASS)
        {
            if (buf.len == 0)
            {
                ESP_LOGE(TAG, "单帧读取失败");
                break;
            }
            esp_jpeg_stream_decode(&jpeg_stream_handle, buf.buffer, buf.len, &(player->rgb_buf), (int *)&player->rgb_len);
            // 设置图像源到 LVGL 对象
            player->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            player->img_dsc.header.w = VIDEO_WIDTH;
            player->img_dsc.header.h = VIDEO_HEIGHT;
            player->img_dsc.data_size = player->rgb_len;
            player->img_dsc.data = player->rgb_buf;
            // 4. 更新 LVGL 显示（需要获取锁）
            bsp_display_lock(-1);
            lv_image_set_src(player->img_obj, &player->img_dsc);
            lv_obj_invalidate(player->img_obj);
            bsp_display_unlock();

            xQueueSend(idle_queue, &buf, portMAX_DELAY);
        }
        // 5. 控制帧率 (30 FPS)
        vTaskDelay(pdMS_TO_TICKS(ANIME_EXPECT_FRAME_MS));
    }
    esp_jpeg_stream_close(&jpeg_stream_handle);
    vTaskDelete(NULL);
}

void mjpeg_player_init(lv_obj_t *show_obj)
{
    mjpeg_switch_album_file(false);
    g_player.img_obj = show_obj;
    // 注册事件组
    sw_album_group = xEventGroupCreate();
    // 注册队列
    idle_queue = xQueueCreate(4, sizeof(frame_packet));
    ready_queue = xQueueCreate(4, sizeof(frame_packet));
    for (int i = 0; i < 4; i++)
    {
        frame_packet packet;
        packet.buffer = (uint8_t *)heap_caps_malloc(JPEG_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
        packet.len = 0; // 顺便初始化长度

        if (packet.buffer == NULL)
        {
            ESP_LOGE("MJPEG", "PSRAM 内存申请失败！Index: %d, Size: %d", i, JPEG_CHUNK_SIZE);
            // 可以在这里触发断言或错误处理
            assert(packet.buffer != NULL);
        }

        // 压入空闲队列
        xQueueSend(idle_queue, &packet, portMAX_DELAY);
    }
    // 初始化解码器
    esp_jpeg_stream_open(&jpeg_stream_handle);

    // 任务
    xTaskCreatePinnedToCore(buffer_task_cb, "buffer_task", 2048, &g_player, 4, &buffer_task_handle, 0);
    xTaskCreatePinnedToCore(mjpeg_task_cb, "mjpeg_task", 2048, &g_player, 4, &mjpeg_task_handle, 1);
}

// 手动触发播放动画
esp_err_t mjpeg_group_clear()
{
    EventBits_t bits = mjpeg_group_get();
     if (bits == 0)
    {
        return ESP_OK;
    }
    if (bits & READY_DONE)
    {
        xEventGroupClearBits(sw_album_group, 0x0F);
        return ESP_OK;
    }
    return ESP_FAIL;
}
// 手动切换状态(如果只是暂停动画,需要手动调用"mjpeg_group_clear"重新播放它);如果是切换相册,在任务中会自动执行动画播放
void mjpeg_group_set(uint8_t bit)
{
    xEventGroupSetBits(sw_album_group, bit);
}
EventBits_t mjpeg_group_get()
{
    return xEventGroupGetBits(sw_album_group);
}
esp_err_t mjpeg_switch_album_file(bool isSwitch)
{
    static uint8_t num = 0;
    uint8_t maxNum = sizeof(files_path) / sizeof(char *);
    if (isSwitch)
    {
        num++;
    }
    const char *str = files_path[num % maxNum];
    // 打开文件
    g_player.file = fopen(str, "rb");
    if (g_player.file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file: %s", str);
        return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t mjpeg_close_album_file()
{
    // 2. 正式关闭文件
    int ret = fclose(g_player.file);
    if (ret != 0)
    {
        ESP_LOGE("SD_TASK", "文件关闭失败 errno: %d", ret);
        ret = ESP_FAIL;
    }
    else
    {
        g_player.file = NULL;
        ESP_LOGI("SD_TASK", "旧文件已成功安全关闭");
        ret = ESP_OK;
    }

    return ret;
}
