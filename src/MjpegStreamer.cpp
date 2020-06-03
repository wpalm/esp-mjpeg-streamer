#include "MJPEGStreamer.h"

#include <esp32-hal.h>
#include <esp_http_server.h>
#include <tcpip_adapter.h>

#include "image_converters/image_converters.h"

MjpegStreamer::~MjpegStreamer() {
  if (server) {
    httpd_stop(server);
  }
  free(frame_buffer);
  free(jpeg_buffer);
}

esp_err_t MjpegStreamer::init(void) {
  esp_err_t res = ESP_OK;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = server_port;

  httpd_uri_t stream_uri_handler = {
      .uri = stream_uri,
      .method = HTTP_GET,
      .handler = streaming_mode_chunked ? stream_chunked_httpd_handler : stream_httpd_handler,
      .user_ctx = this,
  };

  tcpip_adapter_ip_info_t ipInfo;

  // Get IP adress
  res = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
  if (res != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get IP adress");
    return res;
  }

  // Start server
  res = httpd_start(&server, &config);
  if (res == ESP_OK) {
    res = httpd_register_uri_handler(server, &stream_uri_handler);
  }

  if (res == ESP_OK) {
    ESP_LOGI(TAG, "MjpegStreamer started");
    ESP_LOGI(TAG, "MJPEG stream URI: " IPSTR ":%d%s", IP2STR(&ipInfo.ip), config.server_port, stream_uri_handler.uri);
  } else {
    ESP_LOGE(TAG, "MjpegStreamer failed to start");
  }

  return res;
}

esp_err_t MjpegStreamer::setFrame(pixformat_t format, Color **frame, uint32_t width, uint32_t height,
                                  bool line_format_2d) {
  esp_err_t res = ESP_OK;

  frame_buffer = (Color *)frame;
  frame_buffer_size = width * height * sizeof(Color);

  frame_buffer_line_format_2d = line_format_2d;

  frame_buffer_format = format;
  frame_buffer_width = width;
  frame_buffer_height = height;

  return res;
}

esp_err_t MjpegStreamer::stream_httpd_handler(httpd_req_t *req) {
  MjpegStreamer *_this = (MjpegStreamer *)req->user_ctx;

  esp_err_t res = ESP_OK;
  char *part_buf[64];
  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    if (!_this->frame_buffer) {
      ESP_LOGE(TAG, "Frame buffer is empty");
      res = ESP_FAIL;
      break;
    }

    if (_this->frame_buffer_format != PIXFORMAT_JPEG) {
      bool jpeg_converted = toJpeg((uint8_t *)_this->frame_buffer, _this->frame_buffer_size, _this->frame_buffer_width,
                                   _this->frame_buffer_height, _this->frame_buffer_format, 80, &_this->jpeg_buffer,
                                   &_this->jpeg_buffer_size, _this->frame_buffer_line_format_2d);
      if (!jpeg_converted) {
        ESP_LOGE(TAG, "JPEG compression failed");
        res = ESP_FAIL;
      }
    } else {
      _this->jpeg_buffer_size = _this->frame_buffer_size;
      _this->jpeg_buffer = (uint8_t *)_this->frame_buffer;
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _this->jpeg_buffer_size);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_this->jpeg_buffer, _this->jpeg_buffer_size);
    }

    if (_this->frame_buffer_format != PIXFORMAT_JPEG) {
      free(_this->jpeg_buffer);
    }

    if (res != ESP_OK) {
      break;
    }
    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
    ESP_LOGV(TAG, "MJPEG: %uKB (%u B) %ums (%.1ffps)", (uint32_t)(_this->jpeg_buffer_size / 1024),
             (uint32_t)_this->jpeg_buffer_size, (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
  }

  last_frame = 0;
  return res;
}

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

esp_err_t MjpegStreamer::stream_chunked_httpd_handler(httpd_req_t *req) {
  MjpegStreamer *_this = (MjpegStreamer *)req->user_ctx;

  esp_err_t res = ESP_OK;
  char *part_buf[64];
  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    if (!_this->frame_buffer) {
      ESP_LOGE(TAG, "Frame buffer is empty");
      res = ESP_FAIL;
      break;
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      if (_this->frame_buffer_format != PIXFORMAT_JPEG) {
        res = httpd_resp_send_chunk(req, _STREAM_PART_CHUNKED, strlen(_STREAM_PART_CHUNKED));
      } else {
        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _this->jpeg_buffer_size);
        res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
      }
    }

    if (_this->frame_buffer_format != PIXFORMAT_JPEG) {
      jpg_chunking_t jchunk = {req, 0};

      bool jpeg_converted = toJpeg_cb((uint8_t *)_this->frame_buffer, _this->frame_buffer_size,
                                      _this->frame_buffer_width, _this->frame_buffer_height, _this->frame_buffer_format,
                                      80, jpg_encode_stream, &jchunk, _this->frame_buffer_line_format_2d);
      _this->jpeg_buffer_size = jchunk.len;

      if (!jpeg_converted) {
        ESP_LOGE(TAG, "JPEG compression failed");
        res = ESP_FAIL;
      }

      httpd_resp_send_chunk(req, NULL, 0);
    } else {
      _this->jpeg_buffer_size = _this->frame_buffer_size;
      _this->jpeg_buffer = (uint8_t *)_this->frame_buffer;

      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, (const char *)_this->jpeg_buffer, _this->jpeg_buffer_size);
      }
    }

    // if (res == ESP_OK) {
    //   res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    // }

    if (_this->frame_buffer_format != PIXFORMAT_JPEG) {
      free(_this->jpeg_buffer);
    }

    if (res != ESP_OK) {
      break;
    }
    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
    ESP_LOGV(TAG, "MJPEG: %uKB (%u B) %ums (%.1ffps)", (uint32_t)(_this->jpeg_buffer_size / 1024),
             (uint32_t)_this->jpeg_buffer_size, (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
  }

  last_frame = 0;
  return res;
}