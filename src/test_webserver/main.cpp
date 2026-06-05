/*
 * ESP32-CAM - Fokus-Stream (OV2640 / OV3660)
 * ------------------------------------------
 * MJPEG-Stream zum Fokussieren. Der Browser zeigt ein Bild, das sich
 * automatisch alle N Sekunden aktualisiert.
 */

#include <Arduino.h>
#include "../shared/camera_shared.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "../shared/wifi_shared.h"

namespace {

uint32_t streamIntervalMs = 1000;

httpd_handle_t cameraHttpd = nullptr;

#define PART_BOUNDARY "123456789000000000000987654321"
const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t streamHandler(httpd_req_t* req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part[64];
  uint32_t frameNr = 0;
  int failCount = 0;

  while (true) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("fb_get lieferte NULL");
      if (++failCount >= 3) {
        res = ESP_FAIL;
        break;
      }
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    failCount = 0;

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
    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      Serial.println("Stream-Client getrennt");
      break;
    }

    Serial.printf("Frame %lu gesendet: %u Bytes  RSSI=%d dBm\n",
                  (unsigned long)++frameNr, (unsigned)len, rssi);

    vTaskDelay(streamIntervalMs / portTICK_PERIOD_MS);
  }

  return res;
}

esp_err_t indexHandler(httpd_req_t* req) {
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

esp_err_t faviconHandler(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, nullptr, 0);
}

esp_err_t openHttpSession(httpd_handle_t hd, int sockfd) {
  (void)hd;
  int yes = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  return ESP_OK;
}

void startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.lru_purge_enable = true;
  config.max_open_sockets = 3;
  config.stack_size = 8192;
  config.open_fn = openHttpSession;

  httpd_uri_t indexUri = { "/", HTTP_GET, indexHandler, nullptr };
  httpd_uri_t streamUri = { "/stream", HTTP_GET, streamHandler, nullptr };
  httpd_uri_t faviconUri = { "/favicon.ico", HTTP_GET, faviconHandler, nullptr };

  if (httpd_start(&cameraHttpd, &config) == ESP_OK) {
    httpd_register_uri_handler(cameraHttpd, &indexUri);
    httpd_register_uri_handler(cameraHttpd, &streamUri);
    httpd_register_uri_handler(cameraHttpd, &faviconUri);
    Serial.println("HTTP-Server gestartet");
  } else {
    Serial.println("HTTP-Server-Start fehlgeschlagen");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n=== ESP32-CAM Fokus-Stream (Profil: %s) ===\n", camProfile.name);

  if (!initCamera()) {
    Serial.println("Kamera-Init fehlgeschlagen - Neustart in 5 s");
    delay(5000);
    ESP.restart();
  }

  if (!connectWifi()) {
    Serial.println("WLAN-Init fehlgeschlagen - Neustart in 5 s");
    delay(5000);
    ESP.restart();
  }

  startServer();

  Serial.printf("Stream bereit: http://%s/   (Intervall %lu ms)\n",
                WiFi.localIP().toString().c_str(),
                (unsigned long)streamIntervalMs);
  printWifiStatus();
}

void loop() {
  delay(1000);
}
