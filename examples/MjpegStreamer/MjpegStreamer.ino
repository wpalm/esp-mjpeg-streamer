/*
  Demonstrates setting up a server that serves a MJPEG stream over WiFi.
  Needs to be compiled with PSRAM enabled.
*/

#include <Arduino.h>
#include <WiFi.h>

#include "MjpegStreamer.h"

const constexpr char* SSID = "SSID";
const constexpr char* PASSWORD = "PASSWORD";

const uint16_t WIDTH = 320;
const uint16_t HEIGHT = 240;

MjpegStreamer mjpegStreamer;

/// Generate RGB888 frame with random noise
void generateFrame(uint16_t width, uint16_t height, uint8_t** output) {
  size_t pixel_size = sizeof(uint32_t);
  size_t frame_size = width * height * pixel_size;

  *output = (uint8_t*)ps_malloc(frame_size);

  esp_fill_random(*output, frame_size);
}

void setup() {
  Serial.begin(9600);

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println(F("WiFi connected"));

  mjpegStreamer.init();
  Serial.print(F("Stream Link: http://"));
  Serial.print(WiFi.localIP());
  Serial.println(F("/mjpeg/1"));

  uint8_t* frame;
  generateFrame(WIDTH, HEIGHT, &frame);

  mjpegStreamer.setFrame(PIXFORMAT_RGB888, frame, WIDTH, HEIGHT);
}

void loop() {}