# IoT-embedded-system-for-vehicle-telemetry
IoT embedded system for real-time vehicle telemetry: an ESP32 reads OBD-II diagnostic data (RPM, speed, engine load, coolant temp) via CAN bus through an MCP2551 transceiver, then transmits the data remotely over 4G (SIM7600) using MQTT/TCP for cloud storage, monitoring, and further analysis.

# 🚗 OBD-II Vehicle Telemetry System

![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Protocol](https://img.shields.io/badge/protocol-CAN%20%7C%20OBD--II-orange)
![Connectivity](https://img.shields.io/badge/connectivity-4G%20%7C%20MQTT-green)
![License](https://img.shields.io/badge/license-MIT-yellow)
![Status](https://img.shields.io/badge/status-in%20development-red)

> An IoT embedded system for real-time vehicle data acquisition and remote monitoring, built around the ESP32 and the OBD-II/CAN bus protocol.

---

## 📖 Overview

This project implements a connected embedded system that plugs directly into a vehicle's standard **OBD-II port** to capture real-time diagnostic data from the **ECU (Engine Control Unit)** via the **CAN bus**. The data is processed locally on an **ESP32** microcontroller and transmitted remotely over **4G (SIM7600)** using the **MQTT** protocol, enabling continuous telemetry, remote monitoring, and post-trip analysis.

The system is designed for applications such as **fleet management**, **predictive maintenance**, **driver behavior analysis**, and **automotive performance monitoring**.

---

## ✨ Features

- 🔌 **Direct OBD-II / CAN bus access** — reads ECU data without intermediary chips (ELM327-free)
- ⚡ **Real-time processing** — dual-core ESP32 separates CAN reading from 4G transmission
- 📡 **Remote telemetry** — MQTT over 4G LTE with latency under 100 ms
- 💾 **Local storage** — data persistence even with no network coverage
- 🔋 **Self-powered** — runs from the vehicle's 12V OBD-II line, no external battery needed
- 📊 **Multi-parameter capture** — RPM, vehicle speed, coolant temperature, engine load, throttle position, intake air temperature, and DTCs

---

## 🛠️ Hardware Architecture

| Component | Role |
|-----------|------|
| **ESP32 DevKit** | Main microcontroller (dual-core, native TWAI/CAN controller) |
| **MCP2551 / SN65HVD230** | High-speed CAN bus transceiver (physical layer) |
| **SIM7600 4G Module** | Cellular communication via UART/AT commands |
| **DC-DC Buck Converter** | Steps 12V automotive supply down to 5V |
| **LDO Regulator** | Provides stable 3.3V rail for the ESP32 |
| **OBD-II Connector** | Standard SAE J1962 16-pin automotive interface |

### Power Chain
`12V (OBD-II) → Buck Converter → 5V (SIM7600) → LDO → 3.3V (ESP32)`

---

## 📡 Communication Protocols

- **CAN bus (ISO 11898)** — up to 1 Mbps, differential signaling (CAN-H / CAN-L)
- **OBD-II (ISO 15765)** — standardized diagnostic layer running over CAN
- **MQTT over TCP/IP** — lightweight publish/subscribe protocol for IoT
- **UART + AT commands** — host-to-modem communication with the SIM7600

### Supported OBD-II PIDs (Mode 01)

| PID | Parameter | Formula | Unit |
|-----|-----------|---------|------|
| `0C` | Engine RPM | `(256·A + B) / 4` | RPM |
| `0D` | Vehicle Speed | `A` | km/h |
| `05` | Coolant Temperature | `A − 40` | °C |
| `04` | Calculated Engine Load | `(100/255) · A` | % |
| `11` | Throttle Position | `(100/255) · A` | % |
| `0F` | Intake Air Temperature | `A − 40` | °C |

---

## 🔄 System Flow

```
[Vehicle ECU] → [CAN Bus] → [MCP2551] → [ESP32 / TWAI]
                                              ↓
                                    [Decode + Filter + JSON]
                                              ↓
                                    [SIM7600 / MQTT / 4G]
                                              ↓
                                       [Remote Server]
```

Core 0 handles CAN frame acquisition; Core 1 manages the 4G uplink — maximizing throughput and keeping real-time guarantees.

---

## 📁 Repository Structure

```
.
├── src/              # Firmware source code (ESP32)
├── docs/             # Project documentation & diagrams
├── hardware/         # Schematics and PCB files
└── README.md
```

---

## 🚀 Getting Started

> ⚠️ Work in progress — setup instructions will be added as the firmware evolves.

### Prerequisites
- Arduino IDE or PlatformIO
- ESP32 board support package
- Libraries: `driver/twai.h` (built-in), `PubSubClient`, `ArduinoJson`

---

## 📚 References

- ISO 15765 — Diagnostic Communication over CAN
- ISO 9141-2 / KWP2000 — Legacy OBD protocols
- Microchip MCP2551 / TI SN65HVD230 datasheets
- Espressif ESP32 TWAI driver documentation

---

## 👥 Authors

**Giovana Marra e Pimenta**
[![GitHub](https://img.shields.io/badge/GitHub-giovanamarra-181717?logo=github)](https://github.com/giovanamarra)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-giovanamarraepimenta-0A66C2?logo=linkedin)](https://linkedin.com/in/giovanamarraepimenta)

**Lucas Tergilene Furtado**
[![GitHub](https://img.shields.io/badge/GitHub-lucastergi-181717?logo=github)](https://github.com/lucastergi)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-lucastergilene-0A66C2?logo=linkedin)](https://linkedin.com/in/lucastergilene)

Academic project — Embedded Systems & IoT
Computer Engineering

---

## 📄 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
