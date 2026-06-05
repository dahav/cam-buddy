#pragma once

#include <Arduino.h>
#include "esp_camera.h"

#define CAM_OV2640 1
#define CAM_OV3660 2

#ifndef SELECTED_CAM
#define SELECTED_CAM CAM_OV3660
#endif

struct CamProfile {
  const char* name;
  framesize_t frame_size;
  int jpeg_quality;
  uint32_t xclk_hz;
  int fb_count;
  bool vflip;
  bool hmirror;
  int expected_pid;
};

struct CapturedJpeg {
  uint8_t* data;
  size_t len;
};

extern const CamProfile camProfile;

bool initCamera();
int captureJpegCopy(CapturedJpeg* image);
void freeCapturedJpeg(CapturedJpeg* image);
