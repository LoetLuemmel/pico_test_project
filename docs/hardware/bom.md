# Bill of Materials (BOM)
## CCS811 Air Quality Sensor Research Platform

### Required Components

| # | Component | Description | Qty | Part Number | Supplier | Est. Price |
|---|-----------|-------------|-----|-------------|----------|------------|
| 1 | Raspberry Pi Pico | RP2040 MCU board | 1 | SC0915 | Raspberry Pi | ~$4.00 |
| 2 | CCS811 Sensor Module | Air quality sensor (Joy-IT) | 1 | SEN-CCS811V1 | Joy-IT | ~$15.00 |
| 3 | Debug Probe | Raspberry Pi Debug Probe | 1 | SC0889 | Raspberry Pi | ~$12.00 |
| 4 | USB Cable | Micro-USB to USB-A | 1 | - | Generic | ~$3.00 |
| 5 | Jumper Wires | Female-Female, 10cm | 5 | - | Generic | ~$2.00 |
| 6 | Breadboard | Half-size (optional) | 1 | - | Generic | ~$5.00 |

**Estimated Total: ~$41.00**

### Component Details

#### 1. Raspberry Pi Pico

| Parameter | Specification |
|-----------|---------------|
| MCU | RP2040 (dual-core Cortex-M0+) |
| Clock | 133 MHz |
| RAM | 264 KB |
| Flash | 2 MB |
| GPIO | 26 multi-function |
| I2C | 2x controllers |
| USB | 1.1 device/host |

**Suppliers:**
- [Raspberry Pi Official](https://www.raspberrypi.com/products/raspberry-pi-pico/)
- [Reichelt (DE)](https://www.reichelt.de/raspberry-pi-pico)
- [Mouser](https://www.mouser.com/ProductDetail/Raspberry-Pi/SC0915)

#### 2. Joy-IT CCS811 Sensor Module (SEN-CCS811V1)

| Parameter | Specification |
|-----------|---------------|
| Sensor IC | ams CCS811 |
| Interface | I2C (0x5A) |
| Voltage | 3.3V - 5V |
| eCO2 Range | 400 - 8192 ppm |
| TVOC Range | 0 - 1187 ppb |
| Dimensions | 21 x 18 x 3 mm |
| EAN | 4250236817989 |

**Suppliers:**
- [Joy-IT Direct](https://joy-it.net/en/products/SEN-CCS811V1)
- [Reichelt (DE)](https://www.reichelt.de/de/de/luftqualitaetssensor-ccs811-i2c-joy-it-sen-ccs811v1-p291473.html)
- [Conrad (DE)](https://www.conrad.de/)

#### 3. Raspberry Pi Debug Probe

| Parameter | Specification |
|-----------|---------------|
| Interface | CMSIS-DAP v2 |
| Protocol | SWD |
| Connector | 3-pin JST-SH |
| USB | USB 1.1 |
| Features | UART passthrough |

**Suppliers:**
- [Raspberry Pi Official](https://www.raspberrypi.com/products/debug-probe/)
- [Reichelt (DE)](https://www.reichelt.de/raspberry-pi-debug-probe)

#### 4-6. Generic Components

| Component | Specifications |
|-----------|----------------|
| USB Cable | Micro-USB, data capable, 1m minimum |
| Jumper Wires | Female-Female, 22AWG, colors recommended |
| Breadboard | 400 tie-points minimum (optional) |

### Wire Color Convention

| Wire Color | Function |
|------------|----------|
| Red | VCC / 3.3V |
| Black | GND |
| Yellow | SCL (I2C Clock) |
| Blue | SDA (I2C Data) |
| Orange | SWCLK (Debug) |
| Green | SWDIO (Debug) |

### Optional Components

| Component | Description | Purpose |
|-----------|-------------|---------|
| OLED Display | 0.96" I2C SSD1306 | Local display (future) |
| SD Card Module | SPI SD card adapter | Data logging (future) |
| Enclosure | 3D printed case | Protection |
| Header Pins | 2.54mm male headers | If Pico unsoldered |

### Tools Required

| Tool | Purpose |
|------|---------|
| Soldering Iron | Solder headers to Pico (if needed) |
| Wire Strippers | Prepare jumper wires |
| Multimeter | Verify connections |
| Computer | Programming and monitoring |

### Software Requirements

| Software | Version | Purpose |
|----------|---------|---------|
| Pico SDK | 2.0+ | Firmware development |
| CMake | 3.13+ | Build system |
| arm-none-eabi-gcc | 10.3+ | Compiler |
| OpenOCD | 0.12+ | Debugging |
| VS Code | Latest | IDE (optional) |

### Procurement Notes

1. **Pico Variants**: Standard Pico (not Pico W) is sufficient; Pico W adds WiFi but not needed for USB serial output
2. **Sensor Module**: Ensure Joy-IT SEN-CCS811V1 specifically; other CCS811 modules may have different pinouts
3. **Debug Probe**: Official Raspberry Pi Debug Probe recommended; Picoprobe on second Pico is alternative
4. **Quality**: Use quality jumper wires to avoid intermittent connections
