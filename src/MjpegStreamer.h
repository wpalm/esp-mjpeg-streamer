#ifndef MJPEGSTREAM_H_
#define MJPEGSTREAM_H_

#include <esp32-hal.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#define PART_BOUNDARY "123456789000000000000987654321"

/// MJPEG streamer
class MjpegStreamer {
 public:
  MjpegStreamer(uint16_t server_port = 80, bool streaming_mode_chunked = false)
      : server_port(server_port), streaming_mode_chunked(streaming_mode_chunked) {}
  ~MjpegStreamer();
  esp_err_t init(void);
  esp_err_t setFrame(pixformat_t format, uint8_t *frame, uint16_t width, uint16_t height, bool line_format_2d = false);
  esp_err_t setFrameJpeg(uint8_t *frame, uint16_t width, uint16_t height, size_t size);

  static esp_err_t stream_httpd_handler(httpd_req_t *req);
  static esp_err_t stream_chunked_httpd_handler(httpd_req_t *req);

 private:
  uint8_t *frame_buffer;
  size_t frame_buffer_size;
  pixformat_t frame_buffer_format;
  uint32_t frame_buffer_width;
  uint32_t frame_buffer_height;
  bool frame_buffer_line_format_2d;

  uint8_t *jpeg_buffer;
  size_t jpeg_buffer_size;

  bool initialized = false;
  httpd_handle_t server;
  uint16_t server_port;
  const char *stream_uri = "/mjpeg/1";
  bool streaming_mode_chunked;

  static const constexpr char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
  static const constexpr char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
  static const constexpr char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
  static const constexpr char *_STREAM_PART_CHUNKED = "Content-Type: image/jpeg\r\nTransfer-Encoding: chunked\r\n\r\n";
};

#endif