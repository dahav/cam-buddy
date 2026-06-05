/*
 * ESP32-CAM  -  Fokus-Stream (OV2640 / OV3660)
 * --------------------------------------------
 * MJPEG-Stream zum Fokussieren. Der Browser zeigt ein Bild, das sich
 * automatisch alle N Sekunden aktualisiert (kein manuelles F5 noetig).
 *
 * Zwei zur Kompilierzeit waehlbare Tuning-Profile:
 *   SELECTED_CAM = CAM_OV2640   -> optimiert fuer OV2640 (2 MP)
 *   SELECTED_CAM = CAM_OV3660   -> optimiert fuer OV3660 (3 MP)
 *
 * Hinweis: Der Kameratreiber erkennt den Sensor automatisch ueber I2C.
 * Das Flag waehlt nur die Tuning-Parameter. Beim Start wird geprueft, ob
 * das Flag zum tatsaechlich erkannten Sensor passt (sonst Warnung).
 *
 * Bedienung:
 *   1. SELECTED_CAM unten setzen, flashen, RST druecken.
 *   2. Serielle IP ablesen, im Browser http://<IP>/ oeffnen.
 *   3. Am Objektiv drehen; das Bild aktualisiert sich automatisch.
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "lwip/sockets.h"

// ============================================================
//  KONFIGURATION
// ============================================================
#define CAM_OV2640  1
#define CAM_OV3660  2

// >>> HIER UMSTELLEN <<<
#define SELECTED_CAM  CAM_OV3660

const char* ssid     = "DIBO13";
const char* password = "b82@qPSt";

// Stream-Intervall: alle wie viele Millisekunden ein neues Bild
uint32_t streamIntervalMs = 3000;          // 3 s

// WLAN-Sendeleistung. Nah am AP: niedriger = weniger Reflexionen.
// Bei schlechtem Signal auf WIFI_POWER_19_5dBm (Maximum) setzen.
const wifi_power_t wifiTxPower = WIFI_POWER_19_5dBm;

// ============================================================
//  Kamera-Profile (Tuning je Sensor)
// ============================================================
struct CamProfile {
  const char*  name;
  framesize_t  frame_size;
  int          jpeg_quality;   // 10..63, kleiner = besser/groesser
  uint32_t     xclk_hz;
  int          fb_count;
  bool         vflip;
  bool         hmirror;
  int          expected_pid;   // zur Sensor-Pruefung
};

// Sensor-PIDs (aus der esp32-camera-Bibliothek)
#define OV2640_PID 0x26
#define OV3660_PID 0x3660

#if SELECTED_CAM == CAM_OV2640
const CamProfile camProfile = {
  /* name        */ "OV2640",
  /* frame_size  */ FRAMESIZE_SVGA,   // 800x600 - scharf genug zum Fokussieren
  /* jpeg_quality*/ 12,
  /* xclk_hz     */ 20000000,
  /* fb_count    */ 2,
  /* vflip       */ false,
  /* hmirror     */ false,
  /* expected_pid*/ OV2640_PID
};
#elif SELECTED_CAM == CAM_OV3660
const CamProfile camProfile = {
  /* name        */ "OV3660",
  /* frame_size  */ FRAMESIZE_SVGA,   // OV3660 kann mehr; SVGA haelt Stream leicht
  /* jpeg_quality*/ 12,
  /* xclk_hz     */ 20000000,
  /* fb_count    */ 2,
  /* vflip       */ true,             // OV3660 liefert am ESP32-CAM oft gespiegelt
  /* hmirror     */ false,
  /* expected_pid*/ OV3660_PID
};
#else
#error "SELECTED_CAM ungueltig - CAM_OV2640 oder CAM_OV3660 waehlen"
#endif

// ============================================================
//  Kamera-Pinbelegung AI-Thinker ESP32-CAM
// ============================================================
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

// ============================================================
//  Kamera initialisieren (nutzt camProfile)
// ============================================================
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

  config.xclk_freq_hz = camProfile.xclk_hz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = camProfile.frame_size;
  config.jpeg_quality = camProfile.jpeg_quality;
  config.fb_count     = camProfile.fb_count;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = CAMERA_FB_IN_PSRAM;

  if (!psramFound()) {
    Serial.println("WARN: kein PSRAM - reduziere auf QVGA/DRAM");
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
  if (!s) {
    Serial.println("Sensor nicht gefunden");
    return false;
  }

  // Pruefen, ob das gewaehlte Profil zum erkannten Sensor passt
  Serial.printf("Profil: %s  | erkannter Sensor-PID: 0x%x\n",
                camProfile.name, s->id.PID);
  if (s->id.PID != camProfile.expected_pid) {
    Serial.printf("WARNUNG: Profil %s gewaehlt, aber Sensor-PID 0x%x erkannt!\n",
                  camProfile.name, s->id.PID);
    Serial.println("  -> SELECTED_CAM passt nicht zum Modul. Trotzdem weiter.");
  }

  // Sensor-spezifische Ausrichtung / Feintuning
  s->set_vflip(s, camProfile.vflip ? 1 : 0);
  s->set_hmirror(s, camProfile.hmirror ? 1 : 0);
  // Leichte Schaerfung hilft beim Beurteilen des Fokus
  s->set_sharpness(s, 2);

  return true;
}

// ============================================================
//  MJPEG-Stream
// ============================================================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t streamHandler(httpd_req_t* req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part[64];
  uint32_t frameNr = 0;
  int failCount = 0;

  while (true) {
    // 1) Frame holen
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("fb_get lieferte NULL");
      if (++failCount >= 3) { res = ESP_FAIL; break; }
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    failCount = 0;

    // 2) Senden: Grenze + Teil-Header + JPEG
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len);
      res = httpd_resp_send_chunk(req, part, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    }

    const uint32_t len = fb->len;
    const int rssi = WiFi.RSSI();

    // 3) Puffer SOFORT zurueckgeben - nichts waehrend der Wartezeit halten
    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      // Browser hat die Verbindung geschlossen -> Handler sauber beenden
      Serial.println("Stream-Client getrennt");
      break;
    }

    Serial.printf("Frame %lu gesendet: %u Bytes  RSSI=%d dBm\n",
                  (unsigned long)++frameNr, (unsigned)len, rssi);

    // 4) Intervall warten - OHNE einen Framebuffer zu halten
    vTaskDelay(streamIntervalMs / portTICK_PERIOD_MS);
  }

  return res;
}

// ---- Startseite mit eingebettetem Stream ----
static esp_err_t indexHandler(httpd_req_t* req) {
  const char* html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32-CAM Fokus</title>"
    "<style>body{font-family:sans-serif;text-align:center;background:#111;color:#eee;margin:0;padding:12px}"
    "img{max-width:100%;height:auto;border:1px solid #444}"
    ".hint{color:#aaa;font-size:14px}</style></head><body>"
    "<h2>ESP32-CAM Fokus-Stream</h2>"
    "<img id='cam' src='/stream'>"
    "<p class='hint'>Das Bild aktualisiert sich automatisch im eingestellten "
    "Intervall. Objektiv drehen, bis der Text scharf ist.</p>"
    "</body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, strlen(html));
}

static esp_err_t faviconHandler(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, NULL, 0);
}

// TCP_NODELAY je Verbindung -> kleinere Pakete gehen sofort raus
static esp_err_t openHttpSession(httpd_handle_t hd, int sockfd) {
  (void)hd;
  int yes = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  return ESP_OK;
}

void startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port      = 80;
  config.ctrl_port        = 32768;
  config.lru_purge_enable = true;     // alte Sockets aufraeumen
  config.max_open_sockets = 3;        // Stream + Index + Reserve
  config.stack_size       = 8192;     // mehr Stack -> stabiler beim Senden
  config.open_fn          = openHttpSession;

  httpd_uri_t indexUri   = { "/",            HTTP_GET, indexHandler,   NULL };
  httpd_uri_t streamUri  = { "/stream",      HTTP_GET, streamHandler,  NULL };
  httpd_uri_t faviconUri = { "/favicon.ico", HTTP_GET, faviconHandler, NULL };

  if (httpd_start(&cameraHttpd, &config) == ESP_OK) {
    httpd_register_uri_handler(cameraHttpd, &indexUri);
    httpd_register_uri_handler(cameraHttpd, &streamUri);
    httpd_register_uri_handler(cameraHttpd, &faviconUri);
    Serial.println("HTTP-Server gestartet");
  } else {
    Serial.println("HTTP-Server-Start fehlgeschlagen");
  }
}

// ============================================================
//  Setup / Loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n=== ESP32-CAM Fokus-Stream (Profil: %s) ===\n", camProfile.name);

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

  Serial.printf("Stream bereit: http://%s/   (Intervall %lu ms)\n",
                WiFi.localIP().toString().c_str(),
                (unsigned long)streamIntervalMs);
  Serial.printf("SSID=%s RSSI=%d dBm\n", WiFi.SSID().c_str(), WiFi.RSSI());
}

void loop() {
  delay(1000);   // Arbeit laeuft im HTTP-Server-Task
}