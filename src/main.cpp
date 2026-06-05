/*
 * ESP32-CAM  -  Snapshot zum Fokussieren
 * ---------------------------------------
 * Temporaere Firmware: bei jedem Browser-Reload wird genau ein JPEG
 * aufgenommen und direkt ausgeliefert.
 *
 * Bedienung:
 *   1. Flashen, RST druecken, seriellen Monitor oeffnen.
 *   2. Angezeigte IP im Browser oeffnen:  http://<IP>/
 *   3. Mit F5 ein neues Bild anfordern und am Objektiv drehen.
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"

// ===== ANPASSEN =====
const char* ssid     = "DIBO13";
const char* password = "b82@qPSt";

const framesize_t cameraFrameSize = FRAMESIZE_XGA; // hoeher = mehr Details, aber laengerer Capture-Zeit
const int cameraJpegQuality = 10;                   // niedriger = bessere Qualitaet (10..63)
const uint32_t cameraXclkHz = 20000000;             // 20 MHz: schnelle Zeilenauslese, weniger Rolling-Shutter
const wifi_power_t wifiTxPower = WIFI_POWER_8_5dBm; // nah am AP: weniger Retransmits

// ===== Kamera-Pinbelegung AI-Thinker ESP32-CAM =====
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

httpd_handle_t cameraHttpd = NULL;

bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0=Y2_GPIO_NUM; config.pin_d1=Y3_GPIO_NUM;
  config.pin_d2=Y4_GPIO_NUM; config.pin_d3=Y5_GPIO_NUM;
  config.pin_d4=Y6_GPIO_NUM; config.pin_d5=Y7_GPIO_NUM;
  config.pin_d6=Y8_GPIO_NUM; config.pin_d7=Y9_GPIO_NUM;
  config.pin_xclk=XCLK_GPIO_NUM; config.pin_pclk=PCLK_GPIO_NUM;
  config.pin_vsync=VSYNC_GPIO_NUM; config.pin_href=HREF_GPIO_NUM;
  config.pin_sccb_sda=SIOD_GPIO_NUM; config.pin_sccb_scl=SIOC_GPIO_NUM;
  config.pin_pwdn=PWDN_GPIO_NUM; config.pin_reset=RESET_GPIO_NUM;
  config.xclk_freq_hz=cameraXclkHz;
  config.pixel_format=PIXFORMAT_JPEG;
  // LATEST + fb_count=2: Treiber ueberschreibt staendig den aelteren Puffer,
  // sodass fb_get() immer den juengsten fertigen Frame liefert.
  config.grab_mode=CAMERA_GRAB_LATEST;
  config.fb_location=CAMERA_FB_IN_PSRAM;
  config.jpeg_quality=cameraJpegQuality;
  config.fb_count=2;
  config.frame_size=cameraFrameSize;
  if (!psramFound()) {
    config.frame_size  = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 1;
    config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
  }
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera-Init Fehler: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, config.frame_size);
    s->set_quality(s, cameraJpegQuality);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_sharpness(s, 2);          // schaerfere Kanten fuer Fokus-Beurteilung
    s->set_denoise(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);            // 0 = auto
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);               // bessere Auto-Belichtung
    s->set_ae_level(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 0);
    s->set_gainceiling(s, GAINCEILING_8X); // mehr Gain -> kuerzere Belichtung -> weniger Bewegungsunschaerfe
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_dcw(s, 1);
    s->set_colorbar(s, 0);
    s->set_special_effect(s, 2);     // 2 = B&W: spart ~30-40 % JPEG-Groesse (Chroma weg)
  }

  // Mehrere Warmup-Frames: AEC/AGC braucht ein paar Frames bis Belichtung stabil.
  for (int i = 0; i < 4; ++i) {
    camera_fb_t* warmup = esp_camera_fb_get();
    if (warmup) esp_camera_fb_return(warmup);
  }
  return true;
}

// ---- Ein Bild pro Request ----
static esp_err_t snapshotHandler(httpd_req_t* req) {
  uint32_t requestStartedAt = millis();
  Serial.println("Snapshot angefragt");

  uint32_t captureStartedAt = millis();
  // Alten/halb-gefuellten Frame verwerfen, dann den naechsten frisch
  // belichteten Frame zurueckgeben. Garantiert: Bild entstand NACH dem Request.
  camera_fb_t* stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);
  camera_fb_t* fb = esp_camera_fb_get();
  uint32_t captureMs = millis() - captureStartedAt;
  if (!fb) {
    Serial.println("Kamera liefert kein Bild");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Kamera liefert kein Bild");
    return ESP_FAIL;
  }

  char header[256];
  int headerLen = snprintf(header, sizeof(header),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: image/jpeg\r\n"
                           "Content-Length: %u\r\n"
                           "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
                           "Pragma: no-cache\r\n"
                           "Expires: 0\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           (unsigned int)fb->len);

  uint32_t sendStartedAt = millis();
  esp_err_t res = ESP_OK;
  int headerWritten = httpd_send(req, header, headerLen);
  int bodyWritten = headerWritten == headerLen
                      ? httpd_send(req, (const char*)fb->buf, fb->len)
                      : -1;
  if (bodyWritten != (int)fb->len) {
    res = ESP_FAIL;
  }
  uint32_t sendMs = millis() - sendStartedAt;
  uint32_t totalMs = millis() - requestStartedAt;
  Serial.printf("Snapshot: %d/%u Bytes capture=%lu ms send=%lu ms total=%lu ms RSSI=%d dBm res=%d\n",
                bodyWritten,
                (unsigned int)fb->len,
                (unsigned long)captureMs,
                (unsigned long)sendMs,
                (unsigned long)totalMs,
                WiFi.RSSI(),
                (int)res);

  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t openHttpSession(httpd_handle_t hd, int sockfd) {
  (void)hd;
  int yes = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  return ESP_OK;
}

static esp_err_t faviconHandler(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, NULL, 0);
}

void startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.lru_purge_enable = true;
  config.open_fn = openHttpSession;

  httpd_uri_t snapshotUri = { "/", HTTP_GET, snapshotHandler, NULL };
  httpd_uri_t faviconUri = { "/favicon.ico", HTTP_GET, faviconHandler, NULL };

  if (httpd_start(&cameraHttpd, &config) == ESP_OK) {
    httpd_register_uri_handler(cameraHttpd, &snapshotUri);
    httpd_register_uri_handler(cameraHttpd, &faviconUri);
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n=== ESP32-CAM Snapshot (Fokus) ===");

  if (!initCamera()) {
    Serial.println("Kamera-Init fehlgeschlagen - Neustart in 5 s");
    delay(5000);
    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(wifiTxPower);

  WiFi.begin(ssid, password);

  Serial.print("WLAN");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println();

  startServer();

  Serial.printf("Snapshot bereit: http://%s/  (F5 erzeugt neues Bild)\n",
                WiFi.localIP().toString().c_str());
  Serial.printf("SSID=%s RSSI=%d dBm\n", WiFi.SSID().c_str(), WiFi.RSSI());
}

void loop() {
  delay(1000);   // Arbeit laeuft im HTTP-Server-Task
}
