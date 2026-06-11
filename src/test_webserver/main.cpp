/*
 * ESP32-CAM - Fokus-Ansicht (OV5640)
 * --------------------------------
 * Einzelbild-Aktualisierung zum Fokussieren. Der Browser fordert jedes Bild
 * neu an, damit ein abgebrochener Transfer nicht dauerhaft einfriert.
 */

#include <Arduino.h>
#include "../shared/camera_shared.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "../shared/wifi_shared.h"

namespace {

uint32_t captureIntervalMs = 1000;

httpd_handle_t cameraHttpd = nullptr;

esp_err_t captureHandler(httpd_req_t* req) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Snapshot fehlgeschlagen: fb_get lieferte NULL");
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "capture failed");
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  const uint32_t len = fb->len;
  const int rssi = WiFi.RSSI();
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);

  if (res == ESP_OK) {
    Serial.printf("Snapshot gesendet: %u Bytes  RSSI=%d dBm\n", (unsigned)len, rssi);
  } else {
    Serial.println("Snapshot-Client getrennt oder Sendefehler");
  }

  return res;
}

esp_err_t indexHandler(httpd_req_t* req) {
  const char* html = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM Fokus</title>
<style>body{font-family:sans-serif;text-align:center;background:#111;color:#eee;margin:0;padding:12px}img{max-width:100%;height:auto;border:1px solid #444}.hint{color:#aaa;font-size:14px}</style>
</head><body>
<h2>ESP32-CAM Fokus</h2>
<img id="cam" alt="">
<p class="hint">Objektiv drehen, bis der Text scharf ist.</p>
<script>
const intervalMs = 1000;
const timeoutMs = 8000;
const img = document.getElementById("cam");
let objectUrl = null;
async function refresh() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch("/capture?t=" + Date.now(), { cache: "no-store", signal: controller.signal });
    if (response.ok) {
      const blob = await response.blob();
      const nextUrl = URL.createObjectURL(blob);
      const oldUrl = objectUrl;
      objectUrl = nextUrl;
      img.onload = () => { if (oldUrl) { URL.revokeObjectURL(oldUrl); } };
      img.src = nextUrl;
    }
  } catch (e) {
  }
  clearTimeout(timeout);
  setTimeout(refresh, intervalMs);
}
refresh();
</script>
</body></html>)HTML";

  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
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
  config.max_open_sockets = 4;
  config.send_wait_timeout = 8;
  config.stack_size = 8192;
  config.open_fn = openHttpSession;

  httpd_uri_t indexUri = { "/", HTTP_GET, indexHandler, nullptr };
  httpd_uri_t captureUri = { "/capture", HTTP_GET, captureHandler, nullptr };
  httpd_uri_t faviconUri = { "/favicon.ico", HTTP_GET, faviconHandler, nullptr };

  if (httpd_start(&cameraHttpd, &config) == ESP_OK) {
    httpd_register_uri_handler(cameraHttpd, &indexUri);
    httpd_register_uri_handler(cameraHttpd, &captureUri);
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
  Serial.printf("\n=== ESP32-CAM Fokus-Ansicht (Profil: %s) ===\n", camProfile.name);

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

  Serial.printf("Fokus-Ansicht bereit: http://%s/   (Intervall %lu ms)\n",
                WiFi.localIP().toString().c_str(),
                (unsigned long)captureIntervalMs);
  printWifiStatus();
}

void loop() {
  delay(1000);
}
