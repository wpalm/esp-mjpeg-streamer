/*
  Demonstrates setting up a server that serves a MJPEG (chunked) stream over WiFi.
  Each frame of the MJPEG stream is streamed in chunks.
*/

#include <Arduino.h>
#include <WiFi.h>

#include "MjpegStreamer.h"

const constexpr char* SSID = "SSID";
const constexpr char* PASSWORD = "PASSWORD";

const uint16_t WIDTH = 240;
const uint16_t HEIGHT = 180;

MjpegStreamer mjpegStreamer = MjpegStreamer(80, true);
uint8_t* frame;

/// Generate RGB565 frame with random noise
void generateFrame(uint16_t width, uint16_t height, uint8_t** output) {
  size_t pixel_size = sizeof(uint16_t);

  size_t frame_size = width * height * pixel_size;

  if (!*output) {
    *output = (uint8_t*)malloc(frame_size);
    if (!*output) {
      Serial.println("Failed to allocate memory for frame buffer");
      return;
    }
  }

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
}

void loop() {
  generateFrame(WIDTH, HEIGHT, &frame);
  mjpegStreamer.setFrame(PIXFORMAT_RGB565, frame, WIDTH, HEIGHT);
}