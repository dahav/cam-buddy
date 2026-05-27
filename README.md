# Spy Boy ESP32-CAM

Small ESP32-CAM firmware that starts a Wi-Fi HTTP server. Calling `/trigger`
takes a photo and uploads it as JPEG to the configured target URL.

## Setup

Edit `src/main.cpp` before flashing:

```cpp
constexpr char kWifiSsid[] = "DEIN_WLAN";
constexpr char kWifiPassword[] = "DEIN_PASSWORT";

constexpr char kTargetHost[] = "192.168.1.100";
constexpr uint16_t kTargetPort = 8001;
constexpr char kTargetPath[] = "/upload.php";
```

## Build

```sh
pio run -e esp32cam
```

## Flash

Connect the ESP32-CAM through a USB-to-serial adapter.

For many ESP32-CAM boards, flashing requires bootloader mode:

1. Connect `GPIO0` to `GND`.
2. Reset or power-cycle the board.
3. Run the upload command.
4. Disconnect `GPIO0` from `GND`.
5. Reset the board again.

Upload:

```sh
pio run -e esp32cam -t upload
```

The project uses `upload_speed = 460800` from `platformio.ini`.

## Serial Debug

Open the serial monitor:

```sh
pio device monitor -b 115200
```

Or build, flash, and monitor in one command:

```sh
pio run -e esp32cam -t upload -t monitor
```

On startup, the board prints the trigger URL, for example:

```text
Bereit. Trigger-URL: http://192.168.1.42/trigger
```

## Trigger Photo

Call the trigger endpoint from a browser or terminal:

```sh
curl http://<ESP32-IP>/trigger
```

The firmware then captures one image and posts it to the configured target.
