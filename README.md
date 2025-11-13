<p align="center">
  <img src="images/X-Sound.png" width="180"/>
  <img src="images/DC logo.png" width="180"/>
</p>


<p align="center">
  <img src="https://github.com/Darkone83/X-Sound/blob/main/images/X-Sound.jpg">
</p>

# X-Sound

Ever wanted an Xbox startup sound? X-Sound brings it to the OG XBOX, with a compact ESP32-S3 module that adds custom **boot** and **eject** sounds to your original Xbox. No more silent power-ups – now you get that nostalgic audio experience with modern web-based control.

Built by **Darkone83 / Darkone Customs**.

---

## What it does

**Plays the sounds you remember**
- Triggers `/boot.mp3` automatically when you power on
- Plays `/eject.mp3` when you hit the eject button
- Works with .mp3 files (98-128kbps, 44.1kHz mono, keep them under 30 seconds)

**Easy file management through your browser**
- Upload, delete, and test audio files from any device on your network
- Built-in volume control and playback testing
- No need to fiddle with SD cards or complex file transfers

**Smart Wi-Fi setup**
- First boot creates a setup network you connect to
- Configure once, then it automatically connects to your home Wi-Fi
- Access everything at `http://xsound.local`

**Clean installation**
- Uses Xbox's own 3.3V rail to control power switching
- Only draws power when your Xbox is actually on

---

## Get one

Full kit available at: <a href="https://www.darkonecustoms.com/store/p/x-sound">Darkone Customs</a>

---

## Building the firmware

Want to customize or build your own? Here's what you need:

1. Grab the project files and open in Arduino IDE
2. Install the libraries listed below
3. Set up your board configuration (see settings table)
4. Upload `X-Sound.ino` to your ESP32-S3
5. Connect to the `X-Sound-Setup` network that appears
6. Enter your home Wi-Fi details
7. Once it reboots, go to `http://xsound.local`
8. Upload your `boot.mp3` and `eject.mp3` files

---

## Libraries you'll need

Install these through the Arduino IDE Library Manager:

- **ESP Async WebServer** (me-no-dev)
- **AsyncTCP** (me-no-dev)
- **ESP8266Audio** (Earle Philhower)

> Note: Make sure you're using `AudioFileSourceFS` instead of `AudioFileSourceSPIFFS` for ESP32 compatibility.

---

## Arduino IDE setup

| Setting | Value |
|----------|-------|
| **Board** | ESP32S3 Dev Module |
| **Upload Speed** | 921600 |
| **Flash Freq** | 80 MHz |
| **PSRAM** | Disabled |
| **Partition Scheme** | 1.2 MB App / 1.5 MB SPIFFS |
| **Core Version** | ESP32 3.0.7 (Recommended) |

---

## Connecting to your Xbox

### Xbox connector pinout (CN2)
| Pin | What it is |
|------|--------|
| 1 | 5V |
| 2 | GND |
| 3 | X3V3 (3.3V detection) |
| 4 | GND |
| 5 | EJECT (goes low when pressed) |

---

## Installation points

> ⚠️ **Unplug your Xbox first!**

Here's where to tap into your Xbox motherboard:

**Ground (GND):** Any solid ground point works. The LPC header ground is convenient, or you can use the metal shielding or DVD connector ground.

**5V:** Grab this from the LPC connector or the DVD power plug's 5V line.

**3.3V:** Connect to the 3.3V rail on the LPC connector (pin 3). This tells X-Sound when your Xbox is actually powered up.

**EJECT:** Tap into the eject signal from the front panel connector or find the trace on the mainboard.

Keep your wires short and neat. Twisted pairs help reduce interference if you're having audio issues.

---

## Getting connected

**First time setup:**
1. Power up and look for the `X-Sound-Setup` Wi-Fi network
2. Connect with your phone or computer
3. The setup page should open automatically (or go to `192.168.4.1`)
4. Enter your home Wi-Fi name and password
5. Hit save and wait for it to reboot

**Normal use:**
- Visit `http://xsound.local` from any device on your network
- Use the File Manager to upload your sounds
- Test playback and adjust volume as needed

---

## When things go wrong

| Problem | Try this |
|-------|-----------|
| **Can't reach the web page** | Check that you're going to `xsound.local` and look at the LED: Green = connected, Red = no Wi-Fi |
| **No sound plays** | Make sure you have `/boot.mp3` and `/eject.mp3` uploaded and they're proper MP3 files |
| **Audio sounds terrible** | Turn down the volume or re-encode your files as 44.1kHz mono at 128kbps |
| **Eject button doesn't work** | Double-check your GPIO9 connection and make sure the Xbox signal is wired correctly |
| **LED blinks red rapidly** | File playback error - check that your MP3 is 96-128kbps, 44.1kHz mono, under 30 seconds |
| **Won't power on at all** | Verify the 3.3V connection. Without it, X-Sound won't boot |

---

## Credits

**Concept:** Darkone83 / LD50 II   
**Hardware design:** Darkone83  
**Firmware:** Darkone83  
**Libraries we couldn't live without:**
- ESP8266Audio by Earle Philhower
- ESP Async WebServer / AsyncTCP by me-no-dev

---

## How it all works

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

**X-Sound** — Because your Xbox deserves to sound as good as it looks.  
© 2025 **Darkone83 / Darkone Customs**
