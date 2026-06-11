# Spy Boy ESP32-CAM

PlatformIO-Projekt fuer AI-Thinker ESP32-CAM.

## Environments

- `test_webserver`: Fokus-Ansicht per Browser unter `http://<ESP32-IP>/`
- `pic_sender`: Taster an GPIO13 nimmt ein Foto auf und sendet es per HTTP POST

## Konfiguration

WLAN steht zentral in `src/shared/wifi_shared.cpp`:

```cpp
const char* wifiSsid = "DEIN_WLAN";
const char* wifiPassword = "DEIN_PASSWORT";
uint8_t wifiBssid[6] = {0, 0, 0, 0, 0, 0};
```

Upload-Ziel steht in `src/pic_sender/main.cpp`:

```cpp
const char* targetHost = "192.168.1.100";
const uint16_t targetPort = 8001;
const char* targetPath = "/upload.php";
const char* apiKey = "";
```

Kamera- und WLAN-Helfer liegen in `src/shared/`.

Die Firmware ist fest auf OV5640 ausgelegt. Das Profil ist auf OCR mit fester
Belichtungszeit optimiert und nutzt FHD (`FRAMESIZE_FHD`, 1920x1080).
Zur Waermereduktion laeuft der ESP32 mit 160 MHz und der Kamera-XCLK mit
12 MHz. Ohne PSRAM faellt die Firmware automatisch auf QVGA/DRAM zurueck.

## Hardware

`pic_sender` nutzt GPIO13 mit internem Pullup. Taster zwischen GPIO13 und GND
anschliessen. GPIO13 nicht parallel mit SD-Karte nutzen.

## Build / Flash

```sh
pio run -e test_webserver
pio run -e pic_sender

pio run -e test_webserver -t upload
pio run -e pic_sender -t upload
```

Monitor:

```sh
pio device monitor -e test_webserver
pio device monitor -e pic_sender
```
