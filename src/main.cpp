/*
 * ESP32-CAM  -  Extern getriggerter Foto-Upload
 * --------------------------------------------------------
 *  - Der ESP32-CAM läuft als kleiner HTTP-Server.
 *  - GET /trigger löst ein Foto aus.
 *  - Das Foto wird per HTTP-POST mit fester Content-Length gesendet.
 *  - Fix: Body wird direkt per client.write() gesendet,
 *    ohne blockierende availableForWrite()-Vorprüfung.
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "esp_heap_caps.h"
#include "esp_wifi.h"

// ============================================================
//  ANPASSEN
// ============================================================
const char* ssid       = "DIBO13";
const char* password   = "b82@qPSt";

// Ziel, an das das Foto per POST geschickt wird:
const char* targetHost = "192.168.0.10";
const uint16_t targetPort = 8001;
const char* targetPath = "/solve/togaf";
const char* apiKey     = "1234567";

// Fester AP / Mesh-Knoten.
// Auf {0,0,0,0,0,0} setzen, um automatisch wählen zu lassen.
uint8_t targetBssid[6] = {0xEC, 0x6C, 0x9A, 0x3C, 0xA1, 0x92};

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
//  Kamera initialisieren
// ============================================================
bool initCamera() {
  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

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
  config.fb_location = CAMERA_FB_IN_PSRAM;

  // Für Diagnose erstmal konservativ.
  // Wenn stabil: wieder XGA/SXGA testen.
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 15;   // größer = kleinere Datei / schlechtere Qualität
  config.fb_count = 2;

  if (!psramFound()) {
    Serial.println("PSRAM nicht gefunden, verwende VGA/DRAM");
    config.frame_size  = FRAMESIZE_VGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera-Init Fehler: 0x%x\n", err);
    return false;
  }

  return true;
}

// ============================================================
//  JPEG per rohem WiFiClient senden
//  Rückgabe: HTTP-Statuscode der Antwort, oder <0 bei Fehler
// ============================================================
int postJpegFixedLength(const uint8_t* data, size_t len) {
  WiFiClient client;
  client.setTimeout(60000);

  Serial.printf("HTTP Connect: %s:%u%s\n", targetHost, targetPort, targetPath);

  if (!client.connect(targetHost, targetPort)) {
    Serial.println("TCP-Verbindung fehlgeschlagen");
    return -2;
  }

  client.setNoDelay(true);

  // ----------------------------------------------------------
  // Header senden
  // ----------------------------------------------------------
  client.printf("POST %s HTTP/1.1\r\n", targetPath);
  client.printf("Host: %s:%u\r\n", targetHost, targetPort);
  client.print("Content-Type: image/jpeg\r\n");
  client.print("Accept: application/json\r\n");
  client.printf("X-API-Key: %s\r\n", apiKey);
  client.printf("X-Filename: esp32cam_%lu.jpg\r\n", millis());
  client.printf("Content-Length: %u\r\n", (unsigned int)len);
  client.print("Connection: close\r\n");
  client.print("\r\n");

  Serial.println("Header gesendet, Body startet...");

  // ----------------------------------------------------------
  // Body senden
  //
  // Wichtig:
  // Keine availableForWrite()-Vorprüfung.
  // Die war sehr wahrscheinlich der Grund, warum 0 Body-Bytes ankamen.
  // ----------------------------------------------------------
  static const size_t txChunkSize = 1436;

  size_t remaining = len;
  const uint8_t* p = data;

  const uint32_t startedAt = millis();
  uint32_t lastProgress = millis();

  bool firstWriteLogged = false;

  while (remaining > 0) {
    if (!client.connected()) {
      Serial.println("TCP getrennt während Upload");
      client.stop();
      return -7;
    }

    size_t chunkSize = min(remaining, txChunkSize);

    size_t written = client.write(p, chunkSize);

    if (written > 0) {
      if (!firstWriteLogged) {
        Serial.printf("Erster Body-Write: %u Bytes\n", (unsigned int)written);
        firstWriteLogged = true;
      }

      p += written;
      remaining -= written;
      lastProgress = millis();
      continue;
    }

    if (millis() - lastProgress > 30000) {
      Serial.println("Upload-Stall - Abbruch");
      client.stop();
      return -3;
    }

    delay(1);
  }

  client.flush();

  Serial.printf("Upload gesendet in %lu ms (%u Bytes)\n",
                millis() - startedAt,
                (unsigned int)len);

  // ----------------------------------------------------------
  // Antwort lesen
  // ----------------------------------------------------------
  client.setTimeout(15000);

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();

  Serial.printf("Antwort: %s\n", statusLine.c_str());

  int firstSpace = statusLine.indexOf(' ');
  if (firstSpace < 0) {
    client.stop();
    return -4;
  }

  int secondSpace = statusLine.indexOf(' ', firstSpace + 1);

  String codeText = secondSpace > firstSpace
                      ? statusLine.substring(firstSpace + 1, secondSpace)
                      : statusLine.substring(firstSpace + 1);

  int code = codeText.toInt();

  // Optional: Rest der Antwort ausgeben
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line.length() == 0) break;
    Serial.println(line);
  }

  client.stop();

  return code > 0 ? code : -5;
}

// ============================================================
//  Foto aufnehmen, kopieren, Kamera freigeben, senden
// ============================================================
int captureAndSend() {
  printWiFiStatus();

  // Erstes evtl. altes Bild verwerfen
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Aufnahme fehlgeschlagen");
    return -1;
  }

  const size_t len = fb->len;
  Serial.printf("Bild: %u Bytes\n", (unsigned int)len);

  uint8_t* copy = nullptr;

  if (psramFound()) {
    copy = (uint8_t*)ps_malloc(len);
  }

  if (!copy) {
    Serial.println("PSRAM-Kopie fehlgeschlagen, versuche internen Speicher");
    copy = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_8BIT);
  }

  if (!copy) {
    Serial.println("Speicher für Bildkopie fehlgeschlagen");
    esp_camera_fb_return(fb);
    return -6;
  }

  memcpy(copy, fb->buf, len);

  // Kamera sofort freigeben
  esp_camera_fb_return(fb);

  Serial.println("HTTP POST mit fester Content-Length...");
  int code = postJpegFixedLength(copy, len);

  Serial.printf("Upload-HTTP %d\n", code);

  free(copy);

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
              "Foto auslösen: GET /trigger\n");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found\n");
}

// ============================================================
//  Setup / Loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("ESP32-CAM Upload Version 3");

  if (!initCamera()) {
    Serial.println("Kamera-Init fehlgeschlagen - Neustart in 5 s");
    delay(5000);
    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);

  // WLAN-Stromsparen aus
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Maximale Sendeleistung
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_max_tx_power(78);  // 19.5 dBm, Einheit: 0.25 dBm

  bool bssidSet = false;
  for (int i = 0; i < 6; i++) {
    if (targetBssid[i] != 0) {
      bssidSet = true;
      break;
    }
  }

  Serial.print("WLAN verbinden");

  if (bssidSet) {
    WiFi.begin(ssid, password, 0, targetBssid);
  } else {
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.printf("Bereit. Trigger-URL: http://%s/trigger\n",
                WiFi.localIP().toString().c_str());

  printWiFiStatus();

  Serial.printf("Ziel: %s:%u%s\n", targetHost, targetPort, targetPath);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/trigger", HTTP_GET, handleTrigger);
  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println("HTTP-Server gestartet");
}

void loop() {
  server.handleClient();
}
