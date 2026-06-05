/*
 * ESP32-CAM - Pic Sender
 * ---------------------
 * Ein Taster an GPIO13/GND loest ein Foto aus. Das JPEG wird per HTTP POST
 * an den konfigurierten Solve-Endpunkt hochgeladen.
 */

#include <Arduino.h>
#include "../shared/camera_shared.h"
#include <WiFi.h>
#include "../shared/wifi_shared.h"

namespace {

const char* targetHost = "192.168.1.100";
const uint16_t targetPort = 8001;
const char* targetPath = "/upload.php";
const char* apiKey = "";

constexpr int kButtonPin = 13;
constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kMinTriggerGapMs = 1500;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
uint32_t lastDebounceAt = 0;
uint32_t lastTriggerAt = 0;
bool jobRunning = false;
int lastUploadCode = 0;

bool buttonPressedEvent() {
  const bool reading = digitalRead(kButtonPin);

  if (reading != lastButtonReading) {
    lastDebounceAt = millis();
    lastButtonReading = reading;
  }

  if (millis() - lastDebounceAt < kDebounceMs) {
    return false;
  }

  if (reading == stableButtonState) {
    return false;
  }

  stableButtonState = reading;
  return stableButtonState == LOW;
}

bool ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("WLAN getrennt, verbinde neu");
  return connectWifi(20000);
}

int readHttpStatusCode(WiFiClient* client) {
  String statusLine;
  uint32_t responseDeadline = millis() + 10000;

  while (millis() < responseDeadline) {
    while (client->available()) {
      char c = (char)client->read();
      if (c == '\n') {
        responseDeadline = millis();
        break;
      }

      if (c != '\r' && statusLine.length() < 120) {
        statusLine += c;
      }
    }

    if (statusLine.length() > 0 && millis() >= responseDeadline) {
      break;
    }

    if (!client->connected() && !client->available()) {
      break;
    }

    delay(1);
  }

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

void drainHttpResponse(WiFiClient* client) {
  uint32_t drainUntil = millis() + 500;
  while (millis() < drainUntil) {
    while (client->available()) {
      Serial.print((char)client->read());
      drainUntil = millis() + 50;
    }

    if (!client->connected()) {
      break;
    }

    delay(1);
  }
}

int postJpegFixedLength(const uint8_t* data, size_t len) {
  if (!ensureWifiConnected()) {
    return -11;
  }

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
  if (apiKey[0] != '\0') {
    client.printf("X-API-Key: %s\r\n", apiKey);
  }
  client.printf("X-Filename: esp32cam_%lu.jpg\r\n", millis());
  client.printf("Content-Length: %u\r\n", (unsigned int)len);
  client.print("Connection: close\r\n");
  client.print("\r\n");

  Serial.println("Header gesendet, Body startet...");

  static const size_t txChunkSize = 1436;
  size_t remaining = len;
  const uint8_t* p = data;
  const uint32_t startedAt = millis();
  uint32_t lastProgress = millis();
  bool firstWriteLogged = false;

  while (remaining > 0) {
    if (!client.connected()) {
      Serial.println("TCP getrennt waehrend Upload");
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

    if (millis() - lastProgress > 15000) {
      Serial.println("Upload-Stall - Abbruch");
      client.stop();
      return -3;
    }

    delay(1);
  }

  Serial.printf("Upload gesendet in %lu ms (%u Bytes)\n",
                millis() - startedAt,
                (unsigned int)len);

  int code = readHttpStatusCode(&client);
  drainHttpResponse(&client);
  client.stop();

  return code;
}

int captureAndUpload() {
  printWifiStatus();

  CapturedJpeg image = {};
  int captureCode = captureJpegCopy(&image);
  if (captureCode != 0) {
    return captureCode;
  }

  Serial.printf("Bild: %u Bytes\n", (unsigned int)image.len);
  Serial.println("HTTP POST mit fester Content-Length...");

  int uploadCode = postJpegFixedLength(image.data, image.len);
  freeCapturedJpeg(&image);

  return uploadCode;
}

void runButtonJob() {
  if (jobRunning) {
    return;
  }

  if (lastTriggerAt != 0 && millis() - lastTriggerAt < kMinTriggerGapMs) {
    Serial.println("Taster ignoriert: Mindestabstand noch nicht erreicht");
    return;
  }

  jobRunning = true;
  lastTriggerAt = millis();

  Serial.println("Taster gedrueckt -> Foto");
  int code = captureAndUpload();
  lastUploadCode = code;

  if (code >= 200 && code < 300) {
    Serial.printf("Foto-Job OK (%d)\n", code);
  } else {
    Serial.printf("Foto-Job FEHLER (%d)\n", code);
  }

  jobRunning = false;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(kButtonPin, INPUT_PULLUP);
  lastButtonReading = digitalRead(kButtonPin);
  stableButtonState = lastButtonReading;

  Serial.printf("\n=== ESP32-CAM Pic Sender (Profil: %s) ===\n", camProfile.name);
  Serial.println("Taster: GPIO13 nach GND, gedrueckt = LOW");

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

  printWifiStatus();
  Serial.printf("Ziel: %s:%u%s\n", targetHost, targetPort, targetPath);
  Serial.printf("Bereit. Letzter Upload-Code: %d\n", lastUploadCode);
}

void loop() {
  if (buttonPressedEvent()) {
    runButtonJob();
  }

  delay(5);
}
