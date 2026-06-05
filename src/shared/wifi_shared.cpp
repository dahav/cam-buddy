#include "wifi_shared.h"

#include "esp_wifi.h"

namespace {

const char* wifiSsid = "DIBO13";
const char* wifiPassword = "b82@qPSt";

// Auf {0,0,0,0,0,0} setzen, wenn der ESP32 den AP automatisch waehlen soll.
uint8_t wifiBssid[6] = {0xEC, 0x6C, 0x9A, 0x3C, 0xA1, 0x92};

WifiSettings configuredWifiSettings = {
  wifiSsid,
  wifiPassword,
  WIFI_POWER_19_5dBm,
  wifiBssid
};

bool hasBssid(const uint8_t* bssid) {
  if (!bssid) {
    return false;
  }

  for (int i = 0; i < 6; i++) {
    if (bssid[i] != 0) {
      return true;
    }
  }

  return false;
}

}  // namespace

const WifiSettings& defaultWifiSettings() {
  return configuredWifiSettings;
}

bool connectWifi(uint32_t timeoutMs) {
  return connectWifi(defaultWifiSettings(), timeoutMs);
}

bool connectWifi(const WifiSettings& settings, uint32_t timeoutMs) {
  if (!settings.ssid || settings.ssid[0] == '\0') {
    Serial.println("WLAN-SSID fehlt. Setze wifiSsid in wifi_shared.cpp.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(settings.txPower);

  if (settings.txPower == WIFI_POWER_19_5dBm) {
    esp_wifi_set_max_tx_power(78);
  }

  Serial.print("WLAN verbinden");

  if (hasBssid(settings.bssid)) {
    WiFi.begin(settings.ssid, settings.password, 0, settings.bssid);
  } else {
    WiFi.begin(settings.ssid, settings.password);
  }

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (timeoutMs > 0 && millis() - startedAt >= timeoutMs) {
      Serial.println();
      Serial.println("WLAN-Verbindung fehlgeschlagen");
      return false;
    }

    delay(300);
    Serial.print(".");
  }

  Serial.println();
  return true;
}

void printWifiStatus() {
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
