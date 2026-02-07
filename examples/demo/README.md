# GDEY0154D67 1.54" E-Ink Display Demo

This project serves as a comprehensive demonstration for the GDEY0154D67 1.54-inch Electronic Paper Display (EPD) using the ESP32 and the ESP-IDF framework.
It leverages a custom-built library designed around the standard esp_lcd panel driver architecture, ensuring high performance and a clean API for embedded developers.

Electronic Paper Displays are unique because they are bi-stable, meaning they require no power to maintain an image once it has been set.
This makes them ideal for low-power applications like weather stations, digital badges, and e-readers.

## 🛠 Hardware Configuration & Wiring

The GDEY0154D67 connects via a 4-wire Serial Peripheral Interface (SPI).

> Note: For library implementation, the MISO pin is unused.

### Definitions

| Signal | Board Label | Purpose |
|--------|-------------|---------|
|  SCK   |  SCK        | Serial Clock signal for SPI communication. |
|  MOSI  |  SDI        | Serial Data Input; sends pixel data and commands to the display. |
|  DC    |  DC         | Data/Command selection; tells the SSD1681 EPD driver if the byte is an instruction or data. |
|  CS    |  CS         | Chip Select; enables the display for communication on the SPI bus. |
|  BUSY  |  BUSY       | High when the display is performing a hardware refresh; used for flow control. |
|  RES   | RES         | Hardware Reset; used to initialize the SSD1681 driver state. |

### Navigation in menuconfig

To tailor the hardware setup to your specific board (like an ESP32-DevKit or an ESP32-S3-LCD-EV-Board), follow these steps:

1. Open your terminal and run `idf.py menuconfig.`
2. Locate the main category: `GDEY0154D67 Basic example configuration --->`
3. Enter the sub-menu: `GDEY0154D67 pinout --->`
4. Assign the GPIO numbers to match your physical wiring.
This configuration is stored in your project's sdkconfig file.

> Note: SPI pins may differ across ESP32 MCU series. Please refer to the MCU-specific datasheet. Defaults are set for ESP32-S3.

## ⚠️ Important Notes on Operation

### 1. Understanding Screen Flickering

If you notice the screen "flashing" or "blinking" during an update, do not be alarmed—this is the intended behavior of the hardware.
Inside an E-Ink display, millions of tiny microcapsules contain charged white and black pigment particles.
To move these particles accurately, the SSD1681 driver applies precise sequences of positive and negative voltages (called waveforms, stored in SSD1681 LUTs).
The flickering is the result of these rapid transitions, which ensure that every pixel is fully cleared of its previous state to prevent "burn-in" or ghosting.

- **Full Updates**:
  ~2s.
  The most robust method. It cycles through multiple inversion phases to ensure the highest contrast.
- **Fast Updates**:
  ~1.5 s.
  Uses a shortened waveform sequence. This is much quicker and less distracting, but may result in slightly lower contrast over time.
- **Partial Updates**:
  ~0.5 s.
  Updates only the changed pixels. This is the fastest mode (often used for clocks or status bars) and typically has no visible flickering.

> Note: Ambient temperature significantly affects the viscosity of the ink. In very cold environments, updates may take longer or appear less crisp.

### 2. Ghosting Mitigation Strategy

*Ghosting* occurs when a faint remnant of a previous image remains visible after an update.
This is particularly common when using partial updates repeatedly.
To maintain a professional-looking display:

- The 5-Update Rule:
  We recommend performing a Full Update after every 5 Partial Updates.

- Deep Sleep:
  Always use esp_lcd_panel_disp_sleep() when the display is idle.
  This protects the internal circuitry and the longevity of the E-Ink film.

## 🚀 Getting Started

### Prerequisites

ESP-IDF:
  Version v4.1.0 or newer is required to support the component manager features used here.

Component Path:
  Ensure the gdey0154d67 library folder is accessible to your project as defined in the idf_component.yml.

### Build & Execution

#### 1. Define your hardware target
```bash
idf.py set-target esp32xx
```

#### 2. Configure your GPIOs as described in the pinout section
```bash
idf.py menuconfig
```

#### 3. Compile the code, flash the binary, and open the serial monitor
```bash
idf.py build flash monitor
```
