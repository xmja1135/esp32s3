
#include "decode_mjpeg.h"

#define USE_OUTBUF_POOL
#define SD_READ_CHUNK_SIZE (32 * 1024) // 64KB 内部读取块大小（建议与 SD 簇对齐）


static jpeg_pixel_format_t j_type = JPEG_PIXEL_FORMAT_RGB565_LE;
static jpeg_rotate_t j_rotation = JPEG_ROTATE_0D;

/**
 * @brief 按块高效读取下一帧 JPEG 数据
 *
 * @param file 打开的 FILE* 文件指针
 * @param out_buf 存储单帧 JPEG 数据的内存目标地址 (PSRAM)
 * @param max_buf_size out_buf 的最大容量 (如 35KB 或 64KB)
 * @return size_t 返回解析出的 JPEG 图像字节数，0 表示 EOF 或出错
 */
size_t read_jpeg_frame(FILE *file, uint8_t *out_buf, size_t max_buf_size)
{
    if (!file || !out_buf)
        return 0;

    // 申请一块 16KB 的临时内存块用于 SD 批量吞吐
    static uint8_t *chunk_buf = NULL;
    if (chunk_buf == NULL)
    {
        chunk_buf = (uint8_t *)heap_caps_malloc(SD_READ_CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!chunk_buf)
        {
            // 如果内部 SRAM 不足，退而求其次使用 PSRAM
            chunk_buf = (uint8_t *)heap_caps_malloc(SD_READ_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
        }
    }

    size_t out_len = 0;
    bool found_header = false;
    long frame_start_pos = -1;

    while (1)
    {
        // 记录当前读取前的基准文件偏移
        long current_pos = ftell(file);

        // 1. 一次性从 SD 卡吞入 16KB 数据
        size_t bytes_read = fread(chunk_buf, 1, SD_READ_CHUNK_SIZE, file);
        if (bytes_read == 0)
        {
            break; // 文件读取完毕 (EOF)
        }

        size_t i = 0;

        // 2. 寻找帧头 0xFF 0xD8 (如果尚未找到)
        if (!found_header)
        {
            for (i = 0; i < bytes_read - 1; i++)
            {
                if (chunk_buf[i] == 0xFF && chunk_buf[i + 1] == 0xD8)
                {
                    found_header = true;
                    frame_start_pos = current_pos + i;
                    break; // 找到了帧头起始位置
                }
            }
            if (!found_header)
            {
                // 当前 16KB 块内未匹配到帧头，回退 1 字节（防止 SOI 标记正好跨块分割）
                fseek(file, -1, SEEK_CUR);
                continue;
            }
        }

        // 3. 从找到帧头的位置开始寻找帧尾 0xFF 0xD9
        for (; i < bytes_read - 1; i++)
        {
            // 拷贝数据到输出缓冲区
            if (out_len < max_buf_size)
            {
                out_buf[out_len++] = chunk_buf[i];
            }
            else
            {
                ESP_LOGE("MJPEG", "JPEG 帧大小超出预设 buffer 容量!");
                return 0;
            }

            // 检查是否遇到 EOI 帧尾标记 (0xFF 0xD9)
            if (chunk_buf[i] == 0xFF && chunk_buf[i + 1] == 0xD9)
            {
                // 把最后一个字节 0xD9 塞入缓冲区
                if (out_len < max_buf_size)
                {
                    out_buf[out_len++] = chunk_buf[i + 1];
                }

                // 计算下一帧的精确起始偏移，并回退文件指针，避免吃掉下一帧的数据
                long actual_consumed = (current_pos + i + 2) - frame_start_pos;
                fseek(file, frame_start_pos + actual_consumed, SEEK_SET);

                return out_len; // 成功解析出完整的一帧！
            }
        }

        // 处理块末尾未读完最后一个字节的情况
        if (i < bytes_read && out_len < max_buf_size)
        {
            out_buf[out_len++] = chunk_buf[i];
        }

        // 若当前 16KB 没遇到帧尾，继续循环 fread 读下一块
    }

    return out_len;
}

jpeg_error_t esp_jpeg_stream_open(esp_jpeg_stream_handle_t jpeg_handle)
{
    jpeg_error_t ret = JPEG_ERR_OK;

    // Generate default configuration
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = j_type;
    config.rotate = j_rotation;
    // config.scale.width       = 0;
    // config.scale.height      = 0;
    // config.clipper.width     = 0;
    // config.clipper.height    = 0;
    jpeg_handle->output_type = j_type;

    // Create jpeg_dec handle
    ret = jpeg_dec_open(&config, &jpeg_handle->jpeg_dec);
    if (ret != JPEG_ERR_OK)
    {
        return ret;
    }

    // Create io_callback handle
    jpeg_handle->jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
    if (jpeg_handle->jpeg_io == NULL)
    {
        ret = JPEG_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }

    // Create out_info handle
    jpeg_handle->out_info = calloc(1, sizeof(jpeg_dec_header_info_t));
    if (jpeg_handle->out_info == NULL)
    {
        ret = JPEG_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }
    return JPEG_ERR_OK;

    // Decoder deinitialize
jpeg_dec_failed:
    jpeg_dec_close(jpeg_handle->jpeg_dec);
    if (jpeg_handle->jpeg_io)
    {
        free(jpeg_handle->jpeg_io);
    }
    if (jpeg_handle->out_info)
    {
        free(jpeg_handle->out_info);
    }
    return ret;
}

jpeg_error_t esp_jpeg_stream_decode(esp_jpeg_stream_handle_t jpeg_handle, uint8_t *input_buf, int len, uint8_t **output_buf, int *out_len)
{
    jpeg_error_t ret = JPEG_ERR_OK;
#ifdef USE_OUTBUF_POOL
    static unsigned char *out_buf = NULL;
#else
    unsigned char *out_buf = NULL;
#endif

    // Set input buffer and buffer len to io_callback
    jpeg_handle->jpeg_io->inbuf = input_buf;
    jpeg_handle->jpeg_io->inbuf_len = len;

    // Parse jpeg picture header and get picture for user and decoder
    ret = jpeg_dec_parse_header(jpeg_handle->jpeg_dec, jpeg_handle->jpeg_io, jpeg_handle->out_info);
    if (ret != JPEG_ERR_OK)
    {
        return ret;
    }

    *out_len = jpeg_handle->out_info->width * jpeg_handle->out_info->height * 3;
    // Calloc out_put data buffer and update inbuf ptr and inbuf_len
    if (jpeg_handle->output_type == JPEG_PIXEL_FORMAT_RGB565_LE || jpeg_handle->output_type == JPEG_PIXEL_FORMAT_RGB565_BE || jpeg_handle->output_type == JPEG_PIXEL_FORMAT_CbYCrY)
    {
        *out_len = jpeg_handle->out_info->width * jpeg_handle->out_info->height * 2;
    }
    else if (jpeg_handle->output_type == JPEG_PIXEL_FORMAT_RGB888)
    {
        *out_len = jpeg_handle->out_info->width * jpeg_handle->out_info->height * 3;
    }
    else
    {
        ret = JPEG_ERR_INVALID_PARAM;
        return ret;
    }
    // 重复利用out_buf,代替重复分配释放内存
#ifdef USE_OUTBUF_POOL

    if (out_buf == NULL)
    {
        out_buf = jpeg_calloc_align(*out_len, 16);
        if (out_buf == NULL)
        {
            ret = JPEG_ERR_NO_MEM;
            return ret;
        }
        jpeg_handle->jpeg_io->outbuf = out_buf;
        *output_buf = out_buf;
    }
#else
    out_buf = jpeg_calloc_align(*out_len, 16);
    if (out_buf == NULL)
    {
        ret = JPEG_ERR_NO_MEM;
        return ret;
    }
    if (*output_buf)
    {
        free(*output_buf);
    }
    jpeg_handle->jpeg_io->outbuf = out_buf;
    *output_buf = out_buf;
#endif

    // Start decode jpeg
    ret = jpeg_dec_process(jpeg_handle->jpeg_dec, jpeg_handle->jpeg_io);
    if (ret != JPEG_ERR_OK)
    {
        return ret;
    }
    return ret;
}

jpeg_error_t esp_jpeg_stream_close(esp_jpeg_stream_handle_t jpeg_handle)
{
    jpeg_error_t ret = JPEG_ERR_OK;

    ret = jpeg_dec_close(jpeg_handle->jpeg_dec);
    if (jpeg_handle->jpeg_io)
    {
        free(jpeg_handle->jpeg_io);
    }
    if (jpeg_handle->out_info)
    {
        free(jpeg_handle->out_info);
    }
    return ret;
}
