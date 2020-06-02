#ifndef _IMAGE_CONVERTERS_H_
#define _IMAGE_CONVERTERS_H_

#include <esp_camera.h>

bool toJpeg(uint8_t *src, size_t src_len, uint16_t width, uint16_t height, pixformat_t format, uint8_t quality,
            uint8_t **out, size_t *out_len, bool line_format_2d = false);

bool toJpeg_cb(uint8_t *src, size_t src_len, uint16_t width, uint16_t height, pixformat_t format, uint8_t quality,
               jpg_out_cb cb, void *arg, bool line_format_2d = false);

#endif