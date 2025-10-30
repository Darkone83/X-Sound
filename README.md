<p align="center">
  <img src="images/X-Sound.png" width="180"/>
  <img src="images/DC logo.png" width="180"/>
</p>


<p align="center">
  <img src="https://github.com/Darkone83/X-Sound/blob/main/images/X-Sound.jpg">
</p>

# X-Sound

A compact ESP32-S3 project that adds **boot** and **eject** sounds to the **original Xbox**, blending classic hardware with modern web-managed sound control.  

Designed and developed by **Darkone83 / Darkone Customs**.

---

## ⚙️ Features

- **Boot & Eject Sound Playback**
  - Plays `/boot.mp3` automatically on power-up  
  - Plays `/eject.mp3` when the Xbox eject line is triggered
  - .mp3 should be encoded between **98 - 128kbps**, in 44.1kHz and in mono (keep sounds under 30s)
- **Web-Based File Manager**
  - Upload, delete, and play audio files via browser  
  - Built-in **volume slider** and playback test buttons
- **Automatic Wi-Fi Manager**
  - Captive portal for first-time setup  
  - Saves credentials for auto-reconnect
- **mDNS Access**
  - Reachable via **`http://xsound.local`**
- **Clean Power Handling**
  - 5 V switched only when 3.3 V rail is active

---

## 💵 Purchase Options

Full kit: <a href="https://www.darkonecustoms.com/store/p/x-sound">Darkone Customs</a>
___

## 🧰 Build Overview

1. Clone or open the project in **Arduino IDE**
2. Install all **required libraries** (see below)
3. Select correct **board settings** for ESP32-S3 Zero
4. Upload the sketch (`X-Sound.ino`)
5. Connect to Wi-Fi network **`X-Sound-Setup`**
6. Configure your home Wi-Fi through the setup page
7. After reboot, visit **`http://xsound.local`**
8. Open **File Manager** → upload `boot.mp3` and `eject.mp3`

---

## 📦 Required Libraries

- **ESP Async WebServer** (me-no-dev)  
- **AsyncTCP** (me-no-dev)  
- **ESP8266Audio** (Earle Philhower)  

> Ensure you’re using `AudioFileSourceFS` (not `AudioFileSourceSPIFFS`) for ESP32 compatibility.

---

## 🧩 Board Settings (Arduino IDE)

| Setting | Value |
|----------|-------|
| **Board** | ESP32S3 Dev Module |
| **Upload Speed** | 921600 |
| **Flash Freq** | 80 MHz |
| **PSRAM** | Disabled |
| **Partition Scheme** | 1.2 MB App / 1.5 MB SPIFFS |
| **Core Version** | ESP32 3.0.7 (Recommended) |

---

## 🪛 Pinout Reference

### Xbox Connector (CN2)
| Pin | Signal |
|------|--------|
| 1 | 5V |
| 2 | GND |
| 3 | X3V3 (3.3 V Detect) |
| 4 | GND |
| 5 | EJECT (Active LOW) |

> **EJECT** connected to **GPIO9** with `INPUT_PULLUP`.  
> **Optional** Add a **100–220 kΩ series resistor** on the tap for safety.

---

## 🧷 Hardware Installation (Overview)

> ⚠️ *Disconnect your Xbox from power before installation.*

This section outlines the **recommended connection points** on the **original Xbox motherboard** for integrating the X-Sound module.

- **Ground (GND):**  
  Tap any reliable chassis or board ground. Common choices include the **metal shielding**, **DVD power connector ground**, or **front panel ground pins**.

- **5V (Power Input):**  
  Pull from the **LPC** or from the **DVD power plug 5V line**.  

- **3.3V (Sense/Enable):**  
  Connect to the **3.3V rail** present on the LPC connector (CN2 pin 3).  
  Used as a logic reference and power-enable signal.

- **EJECT Signal:**  
  Tap the **EJECT line from the front panel connector (CN2 pin 5)** or the corresponding trace on the mainboard header. 

> 💡 Keep all wiring short and tidy. Use twisted pairs or shielded leads for audio and eject lines if possible to avoid interference.

---

## 🌐 Wi-Fi Setup & File Manager Access

1. **First Boot:**  
   - Device starts in **Access Point mode** as `X-Sound-Setup`.  
   - Connect using your phone or PC.

2. **Configuration Page:**  
   - Page opens automatically (or visit `192.168.4.1`).  
   - Enter your Wi-Fi SSID & password, then save.  
   - Device reboots and joins your network.

3. **Access Device:**  
   - Visit **`http://xsound.local`** (or check router IP).  
   - Use the **File Manager** to upload, delete, or test audio.  
   - Adjust **Volume** with the slider.

---

## 🔧 Troubleshooting (Quick)

| Issue | Solution |
|-------|-----------|
| **No web page** | Retry `http://xsound.local` or check LED status — Green = Wi-Fi connected, Red = no Wi-Fi |
| **No audio** | Verify `/boot.mp3` & `/eject.mp3` exist and are valid MP3s |
| **Distortion** | Lower volume or re-encode audio as 44.1 kHz mono @128 kbps |
| **Eject not working** | Check GPIO9 pull-up and Xbox signal polarity |
| **Fast blinking red LED** | Error with file playback, ensure file is the proper format (MP3 **96 - 128kpbs** 44.1kHz mono under 30s) |

---

## 🪙 Credits

**Hardware & Concept:** Darkone83 — *Darkone Customs*  
**Firmware:** Darkone83 — *Darkone Customs*  
**Libraries:**  
- ESP8266Audio — Earle Philhower  
- ESP Async WebServer / AsyncTCP — me-no-dev  

---

## 🧭 Hardware Overview

```
┌────────────────────────────────────────────────────┐
│                   ESP32-S3 ZERO                    │
│                                                    │
│  GPIO12 ────────► BCLK ───────► MAX98357A (I²S AMP)│
│  GPIO11 ────────► LRCLK ─────►                     │
│  GPIO10 ────────► DOUT ──────► DIN                 │
│  GPIO9  ◄───────► EJECT (Xbox CN2 Pin 5)           │
│                                                    │
│  3.3V (out) ─────► TPS22918 EN (ON pin)            │
│  5V (in)  ───────► TPS22918 VIN                    │
│  5VEN (switched) ─► MAX98357A VDD                  │
│                                                    │
│  SPIFFS: /boot.mp3 /eject.mp3                      │
│  Web: http://xsound.local/files                    │
└────────────────────────────────────────────────────┘

   CN2 (Xbox Front Panel)
   ┌─────┬────────────┐
   │ 1   │ 5V         │
   │ 2   │ GND        │
   │ 3   │ X3V3 Detect│
   │ 4   │ GND        │
   │ 5   │ EJECT      │
   └─────┴────────────┘
```

---

**X-Sound** — Bringing the *original Xbox* startup feel back to life.  
© 2025 **Darkone83 / Darkone Customs**
