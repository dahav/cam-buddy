#pragma once

#include <Arduino.h>
#include "esp_camera.h"

struct CamProfile {
  const char* name;
  framesize_t frame_size;
  int jpeg_quality;
  uint32_t xclk_hz;
  int fb_count;
  bool vflip;
  bool hmirror;
  int expected_pid;
  int exposure_ctrl;
  int aec2;
  int ae_level;
  int aec_value;
  int gain_ctrl;
  int agc_gain;
  gainceiling_t gain_ceiling;
  int whitebal;
  int awb_gain;
  int wb_mode;
  int brightness;
  int contrast;
  int saturation;
  int sharpness;
  int denoise;
  int dcw;
  int bpc;
  int wpc;
  int raw_gma;
  int lenc;
};

struct CapturedJpeg {
  uint8_t* data;
  size_t len;
};

extern const CamProfile camProfile;

bool initCamera();
void cameraSleep();
void cameraWake();
int captureJpegCopy(CapturedJpeg* image);
void freeCapturedJpeg(CapturedJpeg* image);
