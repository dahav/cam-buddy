/*
 * ESP32-CAM - per HTTP getriggerter Foto-Upload
 *
 * GET /trigger:
 *   - nimmt ein Foto auf
 *   - sendet es per HTTP POST an den Zielserver
 *   - antwortet erst, wenn der Upload fertig ist
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "esp_heap_caps.h"
#include "esp_wifi.h"

// ============================================================
//  Konfiguration
// ============================================================
const char* wifiSsid = "DIBO13";
const char* wifiPassword = "b82@qPSt";

const char* targetHost = "192.168.0.10";
const uint16_t targetPort = 8001;
const char* targetPath = "/solve/togaf";
const char* apiKey = "1234567";

// Auf {0,0,0,0,0,0} setzen, um den Access Point automatisch zu waehlen.
uint8_t targetBssid[6] = {0xEC, 0x6C, 0x9A, 0x3C, 0xA1, 0x92};

// ============================================================
//  AI-Thinker ESP32-CAM Pins
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
//  Diagnose
// ============================================================
void printWiFiStatus() {
  Serial.printf("WLAN verbunden: SSID=%s RSSI=%d dBm Kanal=%d\n",
                WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.channel());

  Serial.printf("IP=%s Gateway=%s Subnet=%s DNS=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str(),
                WiFi.dnsIP().toString().c_str());

  Serial.printf("MAC=%s BSSID=%s\n",
                WiFi.macAddress().c_str(),
                WiFi.BSSIDstr().c_str());
}

// ============================================================
//  Kamera
// ============================================================
bool initCamera() {
  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.jpeg_quality = 15;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;
  } else {
    Serial.println("PSRAM nicht gefunden, verwende VGA/DRAM");
    config.frame_size = FRAMESIZE_VGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera-Init Fehler: 0x%x\n", err);
    return false;
  }

  return true;
}

camera_fb_t* getFrameWithRetry() {
  for (int attempt = 1; attempt <= 3; attempt++) {
    camera_fb_t* stale = esp_camera_fb_get();
    if (stale) {
      esp_camera_fb_return(stale);
      delay(30);
    }

    camera_fb_t* frame = esp_camera_fb_get();
    if (frame) {
      return frame;
    }

    Serial.printf("Aufnahme fehlgeschlagen, Versuch %d/3\n", attempt);
    delay(250);
  }

  Serial.println("Kamera liefert keinen Frame, reinitialisiere");
  esp_camera_deinit();
  delay(300);

  if (!initCamera()) {
    Serial.println("Kamera-Reinit fehlgeschlagen");
    return nullptr;
  }

  delay(500);
  return esp_camera_fb_get();
}

int captureJpegCopy(uint8_t** outData, size_t* outLen) {
  *outData = nullptr;
  *outLen = 0;

  camera_fb_t* frame = getFrameWithRetry();
  if (!frame) {
    Serial.println("Aufnahme fehlgeschlagen");
    return -1;
  }

  uint8_t* copy = psramFound()
                    ? (uint8_t*)ps_malloc(frame->len)
                    : nullptr;

  if (!copy) {
    copy = (uint8_t*)heap_caps_malloc(frame->len, MALLOC_CAP_8BIT);
  }

  if (!copy) {
    Serial.println("Speicher fuer Bildkopie fehlgeschlagen");
    esp_camera_fb_return(frame);
    return -6;
  }

  memcpy(copy, frame->buf, frame->len);
  *outData = copy;
  *outLen = frame->len;

  Serial.printf("Bild: %u Bytes\n", (unsigned int)frame->len);
  esp_camera_fb_return(frame);
  return 0;
}

// ============================================================
//  Upload
// ============================================================
int readHttpStatusCode(WiFiClient& client) {
  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  Serial.printf("Antwort: %s\n", statusLine.c_str());

  int firstSpace = statusLine.indexOf(' ');
  if (firstSpace < 0) {
    return -4;
  }

  int secondSpace = statusLine.indexOf(' ', firstSpace + 1);
  String codeText = secondSpace > firstSpace
                      ? statusLine.substring(firstSpace + 1, secondSpace)
                      : statusLine.substring(firstSpace + 1);

  int code = codeText.toInt();
  return code > 0 ? code : -5;
}

int uploadJpeg(const uint8_t* data, size_t len) {
  WiFiClient client;
  client.setTimeout(10000);

  Serial.printf("HTTP Connect: %s:%u%s\n", targetHost, targetPort, targetPath);
  if (!client.connect(targetHost, targetPort)) {
    Serial.println("TCP-Verbindung fehlgeschlagen");
    return -2;
  }

  client.setNoDelay(true);
  client.printf("POST %s HTTP/1.1\r\n", targetPath);
  client.printf("Host: %s:%u\r\n", targetHost, targetPort);
  client.print("Content-Type: image/jpeg\r\n");
  client.print("Accept: application/json\r\n");
  client.printf("X-API-Key: %s\r\n", apiKey);
  client.printf("X-Filename: esp32cam_%lu.jpg\r\n", millis());
  client.printf("Content-Length: %u\r\n", (unsigned int)len);
  client.print("Connection: close\r\n");
  client.print("\r\n");

  const size_t chunkSize = 1436;
  const uint8_t* cursor = data;
  size_t remaining = len;
  uint32_t lastProgress = millis();
  uint32_t startedAt = millis();

  while (remaining > 0) {
    if (!client.connected()) {
      client.stop();
      Serial.println("TCP getrennt waehrend Upload");
      return -7;
    }

    size_t written = client.write(cursor, min(remaining, chunkSize));
    if (written > 0) {
      cursor += written;
      remaining -= written;
      lastProgress = millis();
      continue;
    }

    if (millis() - lastProgress > 15000) {
      client.stop();
      Serial.println("Upload-Stall - Abbruch");
      return -3;
    }

    delay(1);
  }

  Serial.printf("Upload gesendet in %lu ms (%u Bytes)\n",
                millis() - startedAt,
                (unsigned int)len);

  int code = readHttpStatusCode(client);
  client.stop();
  return code;
}

// ============================================================
//  WLAN / HTTP-Server
// ============================================================
bool hasTargetBssid() {
  for (int i = 0; i < 6; i++) {
    if (targetBssid[i] != 0) {
      return true;
    }
  }

  return false;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_max_tx_power(78);

  Serial.print("WLAN verbinden");

  if (hasTargetBssid()) {
    WiFi.begin(wifiSsid, wifiPassword, 0, targetBssid);
  } else {
    WiFi.begin(wifiSsid, wifiPassword);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  printWiFiStatus();
}

void handleTrigger() {
  Serial.println("Trigger empfangen");

  uint8_t* image = nullptr;
  size_t imageLen = 0;
  int captureCode = captureJpegCopy(&image, &imageLen);

  if (captureCode != 0) {
    server.send(500, "text/plain", "FEHLER: Aufnahme fehlgeschlagen\n");
    return;
  }

  int uploadCode = uploadJpeg(image, imageLen);
  free(image);

  if (uploadCode >= 200 && uploadCode < 300) {
    server.send(200, "text/plain", "OK: Foto gesendet (" + String(uploadCode) + ")\n");
  } else {
    server.send(500, "text/plain", "FEHLER: Upload fehlgeschlagen (" + String(uploadCode) + ")\n");
  }
}

void handleRoot() {
  server.send(200, "text/plain",
              "ESP32-CAM bereit.\n"
              "Foto ausloesen: GET /trigger\n");
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("ESP32-CAM Upload");

  if (!initCamera()) {
    Serial.println("Kamera-Init fehlgeschlagen - Neustart in 5 s");
    delay(5000);
    ESP.restart();
  }

  connectWiFi();

  Serial.printf("Trigger-URL: http://%s/trigger\n",
                WiFi.localIP().toString().c_str());
  Serial.printf("Ziel: %s:%u%s\n", targetHost, targetPort, targetPath);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/trigger", HTTP_GET, handleTrigger);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found\n");
  });

  server.begin();
  Serial.println("HTTP-Server gestartet");
}

void loop() {
  server.handleClient();
}
