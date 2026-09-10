#pragma once

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_jpeg_dec.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"


// extern struct esp_jpeg_stream jpeg_stream_handle;

struct esp_jpeg_stream
{
    jpeg_dec_handle_t jpeg_dec;
    jpeg_dec_io_t *jpeg_io;
    jpeg_dec_header_info_t *out_info;
    jpeg_pixel_format_t output_type;
};
typedef struct esp_jpeg_stream *esp_jpeg_stream_handle_t;
size_t read_jpeg_frame(FILE *file, uint8_t *out_buf, size_t max_buf_size);
jpeg_error_t esp_jpeg_stream_open(esp_jpeg_stream_handle_t jpeg_handle);
jpeg_error_t esp_jpeg_stream_decode(esp_jpeg_stream_handle_t jpeg_handle, uint8_t *input_buf, int len, uint8_t **output_buf, int *out_len);
jpeg_error_t esp_jpeg_stream_close(esp_jpeg_stream_handle_t jpeg_handle);