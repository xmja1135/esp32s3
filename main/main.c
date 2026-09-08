#include "includes/main.h"
#include "weather_font_ttf.h"
#include "includes/damping.h"
#include "includes/decode_mjpeg.h"
#include "includes/button.h"

#define VIDEO_WIDTH 432
#define VIDEO_HEIGHT 243
#define JPEG_CHUNK_SIZE (32 * 1024) // jpeg最大单帧大小(字节)
#define ANIME_EXPECT_FRAME 30
#define ANIME_EXPECT_FRAME_MS (1000 / ANIME_EXPECT_FRAME)
/* 声明变量 ====================== */
static const char *TAG = "stu6";
static lv_obj_t *topDiv;
static lv_obj_t *body;
static lv_obj_t *tabview;
static lv_obj_t *tab0;
static lv_obj_t *tab1;
static lv_obj_t *wallpaper;
static TaskHandle_t mjpeg_task_handle;    // 解码输出出列任务
static TaskHandle_t buffer_task_handle;   // 单帧缓冲入列任务
static TaskHandle_t damping_task_handle;  // 阻尼任务
static QueueHandle_t idle_queue;          // 空闲队列
static QueueHandle_t ready_queue;         // 就绪队列
static EventGroupHandle_t sw_album_group; // 相册切换任务组
static mjpeg_player_t g_player;           // 杂项(esp_new_jpeg和lvgl相关)

// 相册组在sd卡中的位置
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
/* 声明变量 ====================== */
/* 声明函数 ++++++++++++++++++++++ */
// static void button_single_click_cb(void *arg, void *usr_data); // 按键事件回调函数
// static void button_init();                                     // 按钮初始化

static void buffer_task_cb(void *arg); // 单帧队列,任务回调
static void mjpeg_task_cb(void *arg);  // 解码输出,任务回调
static void mjpeg_player_init();
static esp_err_t switchAlbumFile(bool isSwitch); // 打开/切换SD卡mjpeg相册组
static esp_err_t closeAlbumFile();               // 关闭文件
static void tabview_changed_cb(lv_event_t *);    // 标签页变化事件回调
/* 声明函数 ++++++++++++++++++++++ */
static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    int32_t value = lv_slider_get_value(slider);
    if (code == LV_EVENT_RELEASED)
    {
        ESP_LOGI(TAG, "slider released at %d", (int)value);
        damping_set_brightness((uint8_t)value);
    }
}
void button_single_click_cb(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "Button single click!");
    xEventGroupSetBits(sw_album_group, ANIME_PAUSE | SWITCHING);
}

static void tabview_changed_cb(lv_event_t *e)
{
    uint32_t tabIndex = lv_tabview_get_tab_active(tabview);
    EventBits_t bits = xEventGroupGetBits(sw_album_group);
    if (tabIndex == 0 && (bits & READY_DONE))
    {

        xEventGroupClearBits(sw_album_group, 0x0F);
        ESP_LOGI(TAG, "tabview changed, anime playing!");
    }
    else
    {
        xEventGroupSetBits(sw_album_group, ANIME_PAUSE);
        ESP_LOGI(TAG, "tabview changed,anime paused!");
    }
}
/**
 * @brief 将 Unicode 码位 (如 0xF101) 转换为正确的 UTF-8 字符串,用于展现和风天气图标
 * @param code_point Unicode 码位 (例如: 0xF000 | 101 的 hex/offset)
 * @param out_utf8   输出缓冲区 (至少分配 4 字节)
 */
void build_unicode_utf8(uint16_t val, char *out_utf8)
{
    // 例如：101 -> 十六进制 0x0101 -> 加上基数 0xF000 -> 得到 0xF101
    uint16_t hex_val = ((val / 100) << 8) | (((val / 10) % 10) << 4) | (val % 10);
    uint32_t code_point = 0xF000 | hex_val; // 得到 0xF101

    // 2. 直接转成 3 字节的 UTF-8 编码（屏幕能识别的字节）
    out_utf8[0] = (char)(0xE0 | ((code_point >> 12) & 0x0F)); // 0xEF
    out_utf8[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));  // 0xA4
    out_utf8[2] = (char)(0x80 | (code_point & 0x3F));         // 0x81
    out_utf8[3] = '\0';
}

static void buffer_task_cb(void *arg)
{
    mjpeg_player_t *player = arg;
    frame_packet buf;
    bool isSwitch;
    for (;;)
    {
        EventBits_t bits = xEventGroupGetBits(sw_album_group);
        if (bits & ANIME_PAUSE)
        {
            // 停止移入ready队列,准备切换mjpeg
            xEventGroupSetBits(sw_album_group, IDLE_DONE);
            // 队列全部初始化完成,关闭file
            if (bits & READY_DONE)
            {
                if (closeAlbumFile() == ESP_OK)
                {
                    isSwitch = (bits & 0x08) ? true : false;
                    if (switchAlbumFile(isSwitch) == ESP_OK)
                    {
                        xEventGroupClearBits(sw_album_group, 0x0F);
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));
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

static void mjpeg_task_cb(void *arg)
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
            if (xQueueReceive(ready_queue, &buf, pdMS_TO_TICKS(500)) == pdPASS)
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

static void mjpeg_player_init()
{
    // 注册按键
    button_init();
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
    xTaskCreatePinnedToCore(buffer_task_cb, "buffer_task", 4096 * 3, &g_player, 4, &buffer_task_handle, 0);
    xTaskCreatePinnedToCore(mjpeg_task_cb, "mjpeg_task", 4096 * 3, &g_player, 4, &mjpeg_task_handle, 1);
}

static esp_err_t switchAlbumFile(bool isSwitch)
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
static esp_err_t closeAlbumFile()
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
// extern const unsigned char __qweather_icons_ttf[];
// extern const unsigned int __qweather_icons_ttf_len;
void app_main(void)
{

    bsp_display_start();

    bsp_display_lock(-1);
    // 最外层盒
    lv_obj_t *scr = lv_screen_active();
    body = lv_obj_create(scr);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(body, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x000601), LV_PART_MAIN);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_column(body, 0, LV_PART_MAIN);
    // 渲染头部
    topDiv = lv_obj_create(body);
    lv_obj_set_flex_grow(topDiv, 1);
    lv_obj_set_style_size(topDiv, LV_PCT(100), LV_PCT(10), LV_PART_MAIN);
    lv_obj_set_style_bg_color(topDiv, lv_color_hex(0x000601), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(topDiv, 0, 0);
    lv_obj_set_style_border_width(topDiv, 0, LV_PART_MAIN);
    lv_obj_set_layout(topDiv, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(topDiv, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topDiv, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(topDiv, 3, LV_PART_MAIN);
    // tabview
    lv_obj_t *contentDiv = lv_obj_create(body);
    lv_obj_set_style_size(contentDiv, LV_PCT(100), LV_PCT(100), 0);
    lv_obj_set_flex_flow(contentDiv, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(contentDiv, 9);
    lv_obj_set_style_pad_all(contentDiv, 0, LV_PART_MAIN);      // 内边距为 0
    lv_obj_set_style_border_width(contentDiv, 0, LV_PART_MAIN); // 边框宽度为 0
    tabview = lv_tabview_create(contentDiv);
    lv_obj_add_event_cb(tabview, tabview_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    // 获取标签按钮容器,并隐藏它们
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000601), 0);
    tab0 = lv_tabview_add_tab(tabview, "tab0");
    tab1 = lv_tabview_add_tab(tabview, "tab1");
    lv_obj_set_style_pad_all(tab0, 0, LV_PART_MAIN);

    // 自定义图标
    lv_font_t *icon_font = lv_tiny_ttf_create_data(
        __qweather_icons_ttf,     // 数据长度
        __qweather_icons_ttf_len, // 字体数据
        50                        // 渲染尺寸（像素）
    );
    if (icon_font == NULL)
    {
        printf("Failed to create icon font!");
    }
    // 右侧天气图标
    lv_obj_t *weatherDiv = lv_obj_create(tab0);
    lv_obj_set_size(weatherDiv, 130, 130);
    lv_obj_set_style_bg_color(weatherDiv, lv_color_hex(0x000601), 0);
    lv_obj_set_style_bg_opa(weatherDiv, 0, 0);
    lv_obj_align(weatherDiv, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_border_width(weatherDiv, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(weatherDiv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(weatherDiv, 16, LV_PART_MAIN);
    lv_obj_set_layout(weatherDiv, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(weatherDiv, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(weatherDiv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label_icon = lv_label_create(weatherDiv);
    lv_obj_set_style_text_font(label_icon, icon_font, 0);
    lv_obj_set_style_text_color(label_icon, lv_color_hex(0xffffff), 0);
    uint16_t code = 104;
    char utf8_buf[6] = {0};
    build_unicode_utf8(code, utf8_buf);
    ESP_LOGI(TAG, "unicode is %s", utf8_buf);
    lv_label_set_text(label_icon, utf8_buf);
    lv_obj_t *label_text = lv_label_create(weatherDiv);
    lv_obj_set_style_text_font(label_text, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(label_text, lv_color_hex(0xffffff), 0);
    lv_label_set_text(label_text, "27°C");
    // 左侧时间图标
    lv_obj_t *timeDiv = lv_obj_create(tab0);
    lv_obj_set_size(timeDiv, 160, 90);
    lv_obj_set_style_bg_color(timeDiv, lv_color_hex(0x000601), 0);
    lv_obj_set_style_bg_opa(timeDiv, 0, 0);
    lv_obj_align(timeDiv, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_border_width(timeDiv, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(timeDiv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(timeDiv, 16, LV_PART_MAIN);
    lv_obj_set_layout(timeDiv, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(timeDiv, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(timeDiv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label_time = lv_label_create(timeDiv);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label_time, lv_color_hex(0xffffff), 0);
    lv_label_set_text(label_time, "12:00");
    // 用来测试的滑块
    lv_obj_t *slider = lv_slider_create(tab1);
    lv_obj_set_width(slider, 200);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_size(slider, 30, 200, 0);
    lv_slider_set_range(slider, 20, 100);
    lv_slider_set_value(slider, 100, LV_ANIM_ON);
    lv_slider_set_orientation(slider, LV_SLIDER_ORIENTATION_VERTICAL);
    // 动态壁纸区域
    wallpaper = lv_img_create(tab0);
    lv_obj_set_style_border_width(wallpaper, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wallpaper, 32, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wallpaper, 0, LV_PART_MAIN);
    lv_obj_align(wallpaper, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_size(wallpaper, 432, 243);
    bsp_display_unlock();
    g_player.img_obj = wallpaper;
    xTaskCreatePinnedToCore(damping_task_cb, "damping_task", 4096, NULL, 3, &damping_task_handle, 0);
    // 加载外部flash
    if (bsp_sdcard_mount() == ESP_OK)
    {
        ESP_LOGI(TAG, "FAT filesystem Mounted");
    }
    else
    {
        vTaskDelete(NULL);
        return;
    }
    switchAlbumFile(false);
    // 播放mjpeg动态壁纸
    mjpeg_player_init();
}