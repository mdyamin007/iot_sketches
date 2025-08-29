# iot_sketches

This repo belongs to my personal IoT projects code.

# 🐟 Automatic Fish Feeder (ESP8266)

This project is an **IoT-enabled automatic fish feeder** built with **NodeMCU (ESP8266)** and a **180° servo motor**.  
It feeds your fish at scheduled times, lets you trigger manual feeds from a built-in web page, and survives **Wi-Fi cuts** or **power outages** thanks to EEPROM persistence.

---

## ✨ Features
- 🕒 **Scheduled Feeding**: Default at **8:00 AM** and **8:00 PM** (Bangladesh time).  
- 🌐 **Web Interface**:
  - View current time, Wi-Fi status, and feeding history.  
  - Change feed times, servo angles, and duration.  
  - Manual “Feed Now” button.  
- 🔁 **Persistence**:
  - Settings stored in EEPROM (survive restarts).  
  - Prevents double-feeding (stores last feed date & time).  
- 🛡 **Resilient**:
  - Recovers after Wi-Fi loss or power failure.  
  - “Catch-up” feeding if rebooted after a missed schedule.  
- ⚙️ **Customizable**: Servo rotation angle, duration, and feeding times adjustable via web UI.  
- 📜 **REST API**:
  - `/api/status` endpoint returns JSON with config & last feed logs.  

---

## 🖼️ System Overview
**Hardware:**
- NodeMCU Lolin V3 (ESP8266)
- SG90 or MG90 180° servo motor
- External **5V 2A power supply** for servo
- Common ground between servo and NodeMCU

**Connections:**
- Servo **VCC** → 5V external supply  
- Servo **GND** → Common ground (with NodeMCU)  
- Servo **Signal** → NodeMCU `D4 (GPIO2)`

---

## 🔧 Setup

### 1. Prerequisites
- [Arduino IDE](https://www.arduino.cc/en/software) with **ESP8266 Board Support**  
- Libraries:
  - `ESP8266WiFi`
  - `ESP8266WebServer`
  - `time`
  - `EEPROM`
  - `Servo`

### 2. Flash the Code
1. Clone this repository:
   ```bash
   git clone https://github.com/your-username/esp8266-fish-feeder.git
   ```
2. Open the `.ino` file in Arduino IDE.  
3. Update your Wi-Fi SSID & Password:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI";
   const char* WIFI_PASSWORD = "YOUR_PASSWORD";
   ```
4. Upload to NodeMCU.

### 3. Access the Web UI
- Check your router or serial monitor for the NodeMCU’s IP address.  
- Open it in a browser:  
  ```
  http://<esp-ip-address>/
  ```

---

## 🌍 Remote Access
For worldwide access:
- **Port Forwarding + Dynamic DNS** (simple, but less secure), or  
- **Ngrok / Cloudflare Tunnel / WireGuard VPN** (recommended).  

---

## 📡 API
- `GET /api/status` → JSON with:
  ```json
  {
    "now": "2025-08-29 20:15:32",
    "wifi": "connected",
    "ip": "192.168.0.101",
    "feed1": "8:0",
    "feed2": "20:0",
    "last1_date": 20250829,
    "last2_date": 20250828,
    "last1_ts": 1693314600,
    "last2_ts": 1693258200,
    "last_any_ts": 1693314600
  }
  ```

---

## 🛠️ Customization
- Change default feed times:
  ```cpp
  uint8_t DEFAULT_FEED1_H = 8,  DEFAULT_FEED1_M = 0;
  uint8_t DEFAULT_FEED2_H = 20, DEFAULT_FEED2_M = 0;
  ```
- Adjust servo angles or open duration via Web UI or code defaults.

---

## 📷 Screenshots
> _(Add images of the feeder hardware + web UI here)_

---

## 📜 License
GNU GENERAL PUBLIC LICENSE – free to use, modify, and share.  
