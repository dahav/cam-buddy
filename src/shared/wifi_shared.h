#pragma once

#include <Arduino.h>
#include <WiFi.h>

struct WifiSettings {
  const char* ssid;
  const char* password;
  wifi_power_t txPower;
  const uint8_t* bssid;
};

const WifiSettings& defaultWifiSettings();
bool connectWifi(uint32_t timeoutMs = 0);
bool connectWifi(const WifiSettings& settings, uint32_t timeoutMs = 0);
void printWifiStatus();
