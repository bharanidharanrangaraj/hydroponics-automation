# Hydroponics Automation System

[![ESP32](https://img.shields.io/badge/ESP32-Dev%20Board-E7352C?logo=espressif)](https://www.espressif.com/)
[![Arduino](https://img.shields.io/badge/Arduino-Framework-00979D?logo=arduino)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Build-FF7F00?logo=platformio)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

An ESP32-based smart hydroponics monitoring and control system featuring real-time sensor readings, relay-driven actuator control, a 20×4 LCD menu with rotary encoder navigation, and a modern web dashboard with live updates.

## Features

- **Multi-Sensor Monitoring** - Air temperature (BMP180), humidity (DHT11), water temperature (DS18B20), light intensity (BH1750), pH level (analog), and barometric pressure (BMP180).
- **Relay Control** - Independently control a water pump, grow light, and ventilation fan via relays.
- **Auto-Cycle Modes** - Water pump and fan support configurable automatic on/off cycling (default: 15 min ON / 45 min OFF for the pump).
- **LCD Menu System** - Navigate sensor readings and relay settings on a 20×4 I2C LCD using a rotary encoder (rotate to scroll, press to select, long-press to go back).
- **Web Dashboard** - A responsive, sci-fi-themed control panel served directly from the ESP32. Real-time data via Server-Sent Events (SSE) - no page reloads required.
- **Remote Relay Control** - Toggle relays and auto modes from any device on the local network through the web UI.


## Hardware Requirements

| Component | Description |
|---|---|
| **ESP32 Dev Board** | Main microcontroller |
| **DHT11** | Temperature & humidity sensor |
| **BMP180 / BMP085** | Barometric pressure & temperature sensor (I2C) |
| **BH1750** | Digital light intensity sensor (I2C) |
| **DS18B20** | Waterproof temperature probe (simulated in firmware) |
| **pH Sensor Module** | Analog pH probe connected to ADC |
| **20×4 I2C LCD** | Display (address `0x27`) |
| **Rotary Encoder** | With push-button for menu navigation |
| **3-Channel Relay Module** | Controls water pump, grow light, and fan |


## Wiring Reference

| Signal | ESP32 Pin |
|---|---|
| DHT11 Data | GPIO 2 |
| pH Sensor (Analog) | GPIO 34 |
| Encoder CLK | GPIO 33 |
| Encoder DT | GPIO 25 |
| Encoder SW (Button) | GPIO 26 |
| Relay - Water Pump | GPIO 23 |
| Relay - Grow Light | GPIO 18 |
| Relay - Fan | GPIO 19 |
| I2C SDA (LCD, BMP, BH1750) | GPIO 21 |
| I2C SCL (LCD, BMP, BH1750) | GPIO 22 |

> **Note:** Relays are active-LOW (motor and light) and active-LOW (fan).

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- USB cable for flashing the ESP32

### Build & Upload

```bash
# Clone the repository
git clone https://github.com/bharanidharanrangaraj/hydroponics-automation.git
cd hydroponics-automation

# Build the firmware
pio run

# Upload to the ESP32
pio run --target upload

# Open the serial monitor
pio device monitor --baud 115200
```

### Wi-Fi Configuration

Edit the credentials in `src/main.cpp` before uploading:

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

Once connected, the ESP32 prints its IP address to the serial monitor. Open that IP in a browser to access the dashboard.

## Web Dashboard

The embedded web UI is served directly from the ESP32 at `http://<ESP32_IP>/`.

**Sensor Panel** - Displays live readings for all six sensors, updated every 2 seconds via SSE.

**Relay Controls:**

| Actuator | Manual Toggle | Auto Mode |
|---|---|---|
| Water Pump | ✅ | ✅ (cycle timer) |
| Grow Light | ✅ | - |
| Ventilation Fan | ✅ | ✅ |

### API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Serves the web dashboard |
| `/relay` | POST | Controls relays and auto modes |
| `/events` | GET (SSE) | Real-time sensor data stream |

**POST `/relay` parameters** (form-encoded):

- `device` - `motor`, `light`, `fan`, `motorAuto`, or `fanAuto`
- `state` - `1` (on) or `0` (off)

## Project Structure

```
hydroponics-automation/
├── src/
│   └── main.cpp          # Application firmware (sensors, relays, LCD, web server)
├── include/              # Header files
├── lib/                  # Project-specific libraries
├── data/                 # SPIFFS data (currently unused)
├── test/                 # Unit tests
├── platformio.ini        # PlatformIO build configuration & dependencies
├── LICENSE               # MIT License
└── README.md
```

## Dependencies

All libraries are managed automatically by PlatformIO:

| Library | Purpose |
|---|---|
| [LiquidCrystal_I2C](https://github.com/johnrickman/LiquidCrystal_I2C) | 20×4 I2C LCD driver |
| [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor) | Sensor abstraction layer |
| [DHT sensor library](https://github.com/adafruit/DHT-sensor-library) | DHT11 temperature & humidity |
| [BH1750](https://github.com/claws/BH1750) | Light intensity sensor |
| [Adafruit BMP085 Library](https://github.com/adafruit/Adafruit-BMP085-Library) | Barometric pressure sensor |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | JSON serialization for SSE |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | Async HTTP & SSE server |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | Async TCP for ESP32 |

## License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

## Author

**Bharani Dharan Rangaraj**


---
Built with 💚 for smarter, greener growing.
