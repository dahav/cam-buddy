#include "camera_shared.h"

#include "esp_heap_caps.h"

const CamProfile camProfile = {
  /* name          */ "OV5640",
  /* frame_size    */ FRAMESIZE_FHD,
  /* jpeg_quality  */ 10,
  /* xclk_hz       */ 12000000,
  /* fb_count      */ 1,
  /* vflip         */ false,
  /* hmirror       */ true,
  /* expected_pid  */ OV5640_PID,
  /* exposure_ctrl */ 0,
  /* aec2          */ 0,
  /* ae_level      */ 0,
  /* aec_value     */ 22,
  /* gain_ctrl     */ 0,
  /* agc_gain      */ 6,
  /* gain_ceiling  */ GAINCEILING_8X,
  /* whitebal      */ 1,
  /* awb_gain      */ 1,
  /* wb_mode       */ 1,
  /* brightness    */ 0,
  /* contrast      */ 2,
  /* saturation    */ -2,
  /* sharpness     */ 2,
  /* denoise       */ 1,
  /* dcw           */ 1,
  /* bpc           */ 1,
  /* wpc           */ 1,
  /* raw_gma       */ 1,
  /* lenc          */ 1
};

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

void applySensorTuning(sensor_t* sensor) {
  sensor->set_vflip(sensor, camProfile.vflip ? 1 : 0);
  sensor->set_hmirror(sensor, camProfile.hmirror ? 1 : 0);

  sensor->set_colorbar(sensor, 0);
  sensor->set_special_effect(sensor, 0);
  sensor->set_brightness(sensor, camProfile.brightness);
  sensor->set_contrast(sensor, camProfile.contrast);
  sensor->set_saturation(sensor, camProfile.saturation);
  sensor->set_sharpness(sensor, camProfile.sharpness);
  sensor->set_denoise(sensor, camProfile.denoise);

  sensor->set_exposure_ctrl(sensor, camProfile.exposure_ctrl);
  sensor->set_aec2(sensor, camProfile.aec2);
  sensor->set_ae_level(sensor, camProfile.ae_level);
  sensor->set_aec_value(sensor, camProfile.aec_value);

  sensor->set_gain_ctrl(sensor, camProfile.gain_ctrl);
  sensor->set_agc_gain(sensor, camProfile.agc_gain);
  sensor->set_gainceiling(sensor, camProfile.gain_ceiling);

  sensor->set_whitebal(sensor, camProfile.whitebal);
  sensor->set_awb_gain(sensor, camProfile.awb_gain);
  sensor->set_wb_mode(sensor, camProfile.wb_mode);

  sensor->set_dcw(sensor, camProfile.dcw);
  sensor->set_bpc(sensor, camProfile.bpc);
  sensor->set_wpc(sensor, camProfile.wpc);
  sensor->set_raw_gma(sensor, camProfile.raw_gma);
  sensor->set_lenc(sensor, camProfile.lenc);
}

}  // namespace

void cameraSleep() {
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  Serial.println("Kamera -> Sleep (PWDN HIGH)");
}

void cameraWake() {
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(50);  // Sensor braucht kurz zum Aufwachen
  Serial.println("Kamera -> Wake (PWDN LOW)");
}

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
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
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
    Serial.println("  -> Erwartet wird ein OV5640-Modul. Trotzdem weiter.");
  }

  applySensorTuning(sensor);
  Serial.printf("OCR-Tuning: frame=%d quality=%d xclk=%lu fb=%d aec=%d agc=%d wb=%d\n",
                (int)camProfile.frame_size,
                camProfile.jpeg_quality,
                (unsigned long)camProfile.xclk_hz,
                camProfile.fb_count,
                camProfile.aec_value,
                camProfile.agc_gain,
                camProfile.wb_mode);

  // Kamera sofort in Standby schicken um Hitze zu vermeiden
  cameraSleep();

  return true;
}

int captureJpegCopy(CapturedJpeg* image) {
  if (!image) {
    return -10;
  }

  image->data = nullptr;
  image->len = 0;

  // Kamera aufwecken
  cameraWake();
  delay(100);  // Sensor stabilisieren lassen

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

    // initCamera() legt Kamera schlafen, also wieder aufwecken
    cameraWake();
    delay(100);

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Aufnahme nach Kamera-Reinit fehlgeschlagen");
      cameraSleep();
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
    cameraSleep();
    return -6;
  }

  memcpy(copy, fb->buf, len);
  esp_camera_fb_return(fb);

  image->data = copy;
  image->len = len;

  // Kamera wieder schlafen legen
  cameraSleep();

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
