#include "camera_shared.h"

#include "esp_heap_caps.h"

#define OV2640_PID 0x26
#define OV3660_PID 0x3660

#if SELECTED_CAM == CAM_OV2640
const CamProfile camProfile = {
  /* name         */ "OV2640",
  /* frame_size   */ FRAMESIZE_SVGA,
  /* jpeg_quality */ 12,
  /* xclk_hz      */ 20000000,
  /* fb_count     */ 2,
  /* vflip        */ false,
  /* hmirror      */ false,
  /* expected_pid */ OV2640_PID
};
#elif SELECTED_CAM == CAM_OV3660
const CamProfile camProfile = {
  /* name         */ "OV3660",
  /* frame_size   */ FRAMESIZE_QXGA,
  /* jpeg_quality */ 12,
  /* xclk_hz      */ 20000000,
  /* fb_count     */ 2,
  /* vflip        */ true,
  /* hmirror      */ false,
  /* expected_pid */ OV3660_PID
};
#else
#error "SELECTED_CAM ungueltig - CAM_OV2640 oder CAM_OV3660 waehlen"
#endif

namespace {

constexpr int PWDN_GPIO_NUM = 32;
constexpr int RESET_GPIO_NUM = -1;
constexpr int XCLK_GPIO_NUM = 0;
constexpr int SIOD_GPIO_NUM = 26;
constexpr int SIOC_GPIO_NUM = 27;
constexpr int Y9_GPIO_NUM = 35;
constexpr int Y8_GPIO_NUM = 34;
constexpr int Y7_GPIO_NUM = 39;
constexpr int Y6_GPIO_NUM = 36;
constexpr int Y5_GPIO_NUM = 21;
constexpr int Y4_GPIO_NUM = 19;
constexpr int Y3_GPIO_NUM = 18;
constexpr int Y2_GPIO_NUM = 5;
constexpr int VSYNC_GPIO_NUM = 25;
constexpr int HREF_GPIO_NUM = 23;
constexpr int PCLK_GPIO_NUM = 22;

void configureCameraPins(camera_config_t* config) {
  config->pin_d0 = Y2_GPIO_NUM;
  config->pin_d1 = Y3_GPIO_NUM;
  config->pin_d2 = Y4_GPIO_NUM;
  config->pin_d3 = Y5_GPIO_NUM;
  config->pin_d4 = Y6_GPIO_NUM;
  config->pin_d5 = Y7_GPIO_NUM;
  config->pin_d6 = Y8_GPIO_NUM;
  config->pin_d7 = Y9_GPIO_NUM;

  config->pin_xclk = XCLK_GPIO_NUM;
  config->pin_pclk = PCLK_GPIO_NUM;
  config->pin_vsync = VSYNC_GPIO_NUM;
  config->pin_href = HREF_GPIO_NUM;

  config->pin_sccb_sda = SIOD_GPIO_NUM;
  config->pin_sccb_scl = SIOC_GPIO_NUM;

  config->pin_pwdn = PWDN_GPIO_NUM;
  config->pin_reset = RESET_GPIO_NUM;
}

}  // namespace

bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  configureCameraPins(&config);

  config.xclk_freq_hz = camProfile.xclk_hz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = camProfile.frame_size;
  config.jpeg_quality = camProfile.jpeg_quality;
  config.fb_count = camProfile.fb_count;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  if (!psramFound()) {
    Serial.println("WARN: kein PSRAM - reduziere auf QVGA/DRAM");
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera-Init Fehler: 0x%x\n", err);
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) {
    Serial.println("Sensor nicht gefunden");
    return false;
  }

  Serial.printf("Profil: %s  | erkannter Sensor-PID: 0x%x\n",
                camProfile.name, sensor->id.PID);
  if (sensor->id.PID != camProfile.expected_pid) {
    Serial.printf("WARNUNG: Profil %s gewaehlt, aber Sensor-PID 0x%x erkannt!\n",
                  camProfile.name, sensor->id.PID);
    Serial.println("  -> SELECTED_CAM passt nicht zum Modul. Trotzdem weiter.");
  }

  sensor->set_vflip(sensor, camProfile.vflip ? 1 : 0);
  sensor->set_hmirror(sensor, camProfile.hmirror ? 1 : 0);
  sensor->set_sharpness(sensor, 2);
  sensor->set_exposure_ctrl(sensor, 0);
  sensor->set_aec_value(sensor, 60);
  sensor->set_gainceiling(sensor, GAINCEILING_16X);

  return true;
}

int captureJpegCopy(CapturedJpeg* image) {
  if (!image) {
    return -10;
  }

  image->data = nullptr;
  image->len = 0;

  camera_fb_t* fb = nullptr;
  for (int attempt = 1; attempt <= 3; attempt++) {
    fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      delay(30);
    }

    fb = esp_camera_fb_get();
    if (fb) {
      break;
    }

    Serial.printf("Aufnahme fehlgeschlagen, Versuch %d/3\n", attempt);
    delay(250);
  }

  if (!fb) {
    Serial.println("Kamera liefert keinen Frame, reinitialisiere");
    esp_camera_deinit();
    delay(300);

    if (!initCamera()) {
      Serial.println("Kamera-Reinit fehlgeschlagen");
      return -1;
    }

    delay(500);
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Aufnahme nach Kamera-Reinit fehlgeschlagen");
      return -1;
    }
  }

  const size_t len = fb->len;
  uint8_t* copy = nullptr;
  if (psramFound()) {
    copy = (uint8_t*)ps_malloc(len);
  }
  if (!copy) {
    Serial.println("PSRAM-Kopie fehlgeschlagen, versuche internen Speicher");
    copy = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_8BIT);
  }
  if (!copy) {
    Serial.println("Speicher fuer Bildkopie fehlgeschlagen");
    esp_camera_fb_return(fb);
    return -6;
  }

  memcpy(copy, fb->buf, len);
  esp_camera_fb_return(fb);

  image->data = copy;
  image->len = len;

  return 0;
}

void freeCapturedJpeg(CapturedJpeg* image) {
  if (!image || !image->data) {
    return;
  }

  free(image->data);
  image->data = nullptr;
  image->len = 0;
}
