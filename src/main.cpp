/*
 * ESP32-CAM  -  Extern getriggerter Foto-Upload (minimal)
 * --------------------------------------------------------
 *  - Der ESP32-CAM laeuft als kleiner HTTP-Server.
 *  - Ein einfacher GET-Request von aussen  ->  loest ein Foto aus.
 *  - Das Foto wird sofort per HTTP-POST an eine Ziel-URL gesendet.
 *
 *  Kein Taster, kein Vibrationsmotor, kein Deep Sleep, kein Ton.
 *
 *  Trigger (z. B. vom Rechner / einer anderen Quelle):
 *      curl http://<IP-DES-ESP>/trigger
 *  oder einfach die Adresse im Browser oeffnen.
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ============================================================
//  ANPASSEN
// ============================================================
const char* ssid       = "DIBO13";
const char* password   = "b82@qPSt";

// Ziel, an das das Foto per POST geschickt wird:
const char* targetHost = "192.168.0.10";
const uint16_t targetPort = 8001;
const char* targetPath = "/solve/togaf";
const char* targetUrl  = "http://192.168.0.10:8001/solve/togaf";

// ============================================================
//  Kamera-Pinbelegung AI-Thinker ESP32-CAM
// ============================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WebServer server(80);

// ============================================================
//  Netzwerk-Diagnose
// ============================================================
void printWiFiStatus() {
  Serial.printf("WLAN verbunden: SSID=%s RSSI=%d dBm\n",
                WiFi.SSID().c_str(), WiFi.RSSI());
  Serial.printf("IP=%s Gateway=%s Subnet=%s DNS=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str(),
                WiFi.dnsIP().toString().c_str());
  Serial.printf("MAC=%s BSSID=%s\n",
                WiFi.macAddress().c_str(),
                WiFi.BSSIDstr().c_str());
}

bool probeTargetTcp() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WLAN nicht verbunden, Status=%d\n", WiFi.status());
    return false;
  }

  WiFiClient client;
  client.setTimeout(5000);
  Serial.printf("TCP-Test: verbinde zu %s:%u ...\n", targetHost, targetPort);

  bool connected = client.connect(targetHost, targetPort);
  Serial.println(connected ? "TCP-Test OK" : "TCP-Test fehlgeschlagen");
  client.stop();
  return connected;
}

// ============================================================
//  Kamera initialisieren
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
  config.xclk_freq_hz=20000000;     // bei langem Kamerakabel auf 10000000 senken
  config.pixel_format=PIXFORMAT_JPEG;
  config.grab_mode=CAMERA_GRAB_LATEST;
  config.fb_location=CAMERA_FB_IN_PSRAM;
  config.jpeg_quality=10;           // 10..15 sinnvoll; kleiner = besser/groesser
  config.fb_count=2;
  config.frame_size=FRAMESIZE_UXGA; // 1600x1200 (2 MP)
  if (!psramFound()) {
    config.frame_size  = FRAMESIZE_SVGA;   // 800x600 ohne PSRAM
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 1;
  }
  return esp_camera_init(&config) == ESP_OK;
}

// ============================================================
//  Foto aufnehmen und per HTTP-POST senden
//  Rueckgabe: HTTP-Statuscode der Ziel-Antwort, oder <0 bei Fehler
// ============================================================
int captureAndSend() {
  printWiFiStatus();
  if (!probeTargetTcp()) {
    return -3;
  }

  // Erstes (evtl. altes) Bild verwerfen
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Aufnahme fehlgeschlagen"); return -1; }

  Serial.printf("Bild: %u Bytes\n", fb->len);

  HTTPClient http;
  http.setTimeout(15000);
  http.setReuse(false);

  Serial.printf("HTTP Begin: %s\n", targetUrl);

  if (!http.begin(targetUrl)) {
    Serial.println("http.begin fehlgeschlagen");
    esp_camera_fb_return(fb);
    return -2;
  }
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("accept", "application/json");
  http.addHeader("X-API-Key", "1234567");
  http.addHeader("X-Filename", "esp32cam_" + String(millis()) + ".jpg");

  Serial.println("HTTP Post...");

  // Sendet den JPEG-Buffer als rohen Binary-Body, wie curl --data-binary.
  int code = http.sendRequest("POST", fb->buf, fb->len);
  Serial.printf("Upload-HTTP %d\n", code);
  if (code < 0) {
    Serial.printf("HTTP-Fehler: %s\n", http.errorToString(code).c_str());
  }

  http.end();
  esp_camera_fb_return(fb);
  return code;
}

// ============================================================
//  HTTP-Handler
// ============================================================
void handleTrigger() {
  Serial.println("Trigger empfangen -> Foto");
  int code = captureAndSend();

  if (code >= 200 && code < 300) {
    server.send(200, "text/plain", "OK: Foto gesendet (" + String(code) + ")\n");
  } else {
    server.send(500, "text/plain", "FEHLER beim Senden (Code " + String(code) + ")\n");
  }
}

void handleRoot() {
  server.send(200, "text/plain",
              "ESP32-CAM bereit.\n"
              "Foto ausloesen: GET /trigger\n"
              "Netz testen: GET /net-test\n");
}

void handleNetTest() {
  Serial.println("Netztest angefordert");
  printWiFiStatus();
  bool ok = probeTargetTcp();

  if (ok) {
    server.send(200, "text/plain", "OK: TCP-Verbindung zu " + String(targetHost) +
                ":" + String(targetPort) + " moeglich\n");
  } else {
    server.send(500, "text/plain", "FEHLER: TCP-Verbindung zu " + String(targetHost) +
                ":" + String(targetPort) + " fehlgeschlagen\n");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found\n");
}

// ============================================================
//  Setup / Loop
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.printf("Version 1");
  delay(50);

  if (!initCamera()) {
    Serial.println("Kamera-Init fehlgeschlagen - Neustart in 5 s");
    delay(5000);
    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("WLAN");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.printf("\nBereit. Trigger-URL: http://%s/trigger\n",
                WiFi.localIP().toString().c_str());
  printWiFiStatus();
  Serial.printf("Ziel: %s:%u%s\n", targetHost, targetPort, targetPath);

  server.on("/",        HTTP_GET, handleRoot);
  server.on("/trigger", HTTP_GET, handleTrigger);
  server.on("/net-test", HTTP_GET, handleNetTest);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();
}
