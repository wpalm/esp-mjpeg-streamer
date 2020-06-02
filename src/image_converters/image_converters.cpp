// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "image_converters.h"

#include <stddef.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "img_converters.h"
#include "jpge.h"
#include "soc/efuse_reg.h"
#include "yuv.h"

#if ESP_IDF_VERSION_MAJOR >= 4  // IDF 4+
#if CONFIG_IDF_TARGET_ESP32     // ESP32/PICO-D4
#include "esp32/spiram.h"
#else
#error Target CONFIG_IDF_TARGET is not supported
#endif
#else  // ESP32 Before IDF 4.0
#include "esp_spiram.h"
#endif

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
#endif

static void *_malloc(size_t size) {
  void *res = malloc(size);
  if (res) {
    return res;
  }
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static IRAM_ATTR void convert_line_format_2d(uint8_t *src, pixformat_t format, uint8_t *dst, size_t width,
                                             size_t in_channels, size_t line) {
  int i = 0, o = 0, l = 0;
  if (format == PIXFORMAT_GRAYSCALE) {
    memcpy(dst, ((uint8_t **)src)[line], width);
  } else if (format == PIXFORMAT_RGB888) {
    l = width * 3;
    for (i = 0; i < l; i += 3) {
      dst[o++] = ((uint8_t **)src)[line][i + 2];
      dst[o++] = ((uint8_t **)src)[line][i + 1];
      dst[o++] = ((uint8_t **)src)[line][i];
    }
  } else if (format == PIXFORMAT_RGB565) {
    l = width * 2;
    for (i = 0; i < l; i += 2) {
      dst[o++] = ((uint8_t **)src)[line][i] & 0xF8;
      dst[o++] = (((uint8_t **)src)[line][i] & 0x07) << 5 | (((uint8_t **)src)[line][i + 1] & 0xE0) >> 3;
      dst[o++] = (((uint8_t **)src)[line][i + 1] & 0x1F) << 3;
    }
  } else if (format == PIXFORMAT_YUV422) {
    uint8_t y0, y1, u, v;
    uint8_t r, g, b;
    l = width * 2;
    for (i = 0; i < l; i += 4) {
      y0 = ((uint8_t **)src)[line][i];
      u = ((uint8_t **)src)[line][i + 1];
      y1 = ((uint8_t **)src)[line][i + 2];
      v = ((uint8_t **)src)[line][i + 3];

      yuv2rgb(y0, u, v, &r, &g, &b);
      dst[o++] = r;
      dst[o++] = g;
      dst[o++] = b;

      yuv2rgb(y1, u, v, &r, &g, &b);
      dst[o++] = r;
      dst[o++] = g;
      dst[o++] = b;
    }
  }
}

static bool convert_image_2d(uint8_t *src, uint16_t width, uint16_t height, pixformat_t format, uint8_t quality,
                             jpge::output_stream *dst_stream) {
  int num_channels = 3;
  jpge::subsampling_t subsampling = jpge::H2V2;

  if (format == PIXFORMAT_GRAYSCALE) {
    num_channels = 1;
    subsampling = jpge::Y_ONLY;
  }

  if (!quality) {
    quality = 1;
  } else if (quality > 100) {
    quality = 100;
  }

  jpge::params comp_params = jpge::params();
  comp_params.m_subsampling = subsampling;
  comp_params.m_quality = quality;

  jpge::jpeg_encoder dst_image;

  if (!dst_image.init(dst_stream, width, height, num_channels, comp_params)) {
    ESP_LOGE(TAG, "JPG encoder init failed");
    return false;
  }

  uint8_t *line = (uint8_t *)_malloc(width * num_channels);
  if (!line) {
    ESP_LOGE(TAG, "Scan line malloc failed");
    return false;
  }

  for (int i = 0; i < height; i++) {
    convert_line_format_2d(src, format, line, width, num_channels, i);
    if (!dst_image.process_scanline(line)) {
      ESP_LOGE(TAG, "JPG process line %u failed", i);
      free(line);
      return false;
    }
  }
  free(line);

  if (!dst_image.process_scanline(NULL)) {
    ESP_LOGE(TAG, "JPG image finish failed");
    return false;
  }
  dst_image.deinit();
  return true;
}

class memory_stream : public jpge::output_stream {
 protected:
  uint8_t *out_buf;
  size_t max_len, index;

 public:
  memory_stream(void *pBuf, uint buf_size) : out_buf(static_cast<uint8_t *>(pBuf)), max_len(buf_size), index(0) {}

  virtual ~memory_stream() {}

  virtual bool put_buf(const void *pBuf, int len) {
    if (!pBuf) {
      // end of image
      return true;
    }
    if ((size_t)len > (max_len - index)) {
      ESP_LOGW(TAG, "JPG output overflow: %d bytes", len - (max_len - index));
      len = max_len - index;
    }
    if (len) {
      memcpy(out_buf + index, pBuf, len);
      index += len;
    }
    return true;
  }

  virtual size_t get_size() const { return index; }
};

class callback_stream : public jpge::output_stream {
 protected:
  jpg_out_cb ocb;
  void *oarg;
  size_t index;

 public:
  callback_stream(jpg_out_cb cb, void *arg) : ocb(cb), oarg(arg), index(0) {}
  virtual ~callback_stream() {}
  virtual bool put_buf(const void *data, int len) {
    index += ocb(oarg, index, data, len);
    return true;
  }
  virtual size_t get_size() const { return index; }
};

bool toJpeg(uint8_t *src, size_t src_len, uint16_t width, uint16_t height, pixformat_t format, uint8_t quality,
            uint8_t **out, size_t *out_len, bool line_format_2d) {
  if (line_format_2d) {
    // todo: allocate proper buffer for holding JPEG data
    // this should be enough for CIF frame size
    int jpg_buf_len = 64 * 1024;

    uint8_t *jpg_buf = (uint8_t *)_malloc(jpg_buf_len);
    if (jpg_buf == NULL) {
      ESP_LOGE(TAG, "JPG buffer malloc failed");
      return false;
    }
    memory_stream dst_stream(jpg_buf, jpg_buf_len);

    if (!convert_image_2d(src, width, height, format, quality, &dst_stream)) {
      free(jpg_buf);
      return false;
    }

    *out = jpg_buf;
    *out_len = dst_stream.get_size();
    return true;
  } else {
    return fmt2jpg(src, src_len, width, height, format, quality, out, out_len);
  }
}

bool toJpeg_cb(uint8_t *src, size_t src_len, uint16_t width, uint16_t height, pixformat_t format, uint8_t quality,
               jpg_out_cb cb, void *arg, bool line_format_2d) {
  if (line_format_2d) {
    callback_stream dst_stream(cb, arg);
    return convert_image_2d(src, width, height, format, quality, &dst_stream);
  } else {
    return fmt2jpg_cb(src, src_len, width, height, format, quality, cb, arg);
  }
}