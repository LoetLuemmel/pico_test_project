# Functional Specification Document
## CCS811 Air Quality Sensor Research Platform

| Document Info | |
|---------------|---|
| **Project** | Pico CCS811 Environment Sensor |
| **Version** | 1.0 |
| **Date** | 2026-03-30 |
| **Status** | Draft |
| **Branch** | embedded-device |

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [System Requirements](#2-system-requirements)
3. [Hardware Design](#3-hardware-design)
4. [Software Architecture](#4-software-architecture)
5. [I2C Protocol Specification](#5-i2c-protocol-specification)
6. [Data Format Specification](#6-data-format-specification)
7. [Test Plan](#7-test-plan)
8. [Appendices](#8-appendices)

---

## 1. Introduction

### 1.1 Purpose

This document specifies the functional requirements for a Raspberry Pi Pico-based air quality monitoring research platform using the Joy-IT CCS811 sensor. The platform is designed for sensor evaluation, algorithm testing, and environmental data collection.

### 1.2 Scope

This specification covers:
- Hardware interface between Pico and CCS811 sensor
- I2C communication protocol implementation
- Sensor initialization and data acquisition
- USB serial output for data logging
- Debug and testing capabilities

### 1.3 Definitions and Acronyms

| Term | Definition |
|------|------------|
| **eCO2** | Equivalent Carbon Dioxide - calculated CO2 concentration based on VOC readings |
| **TVOC** | Total Volatile Organic Compounds - sum of all organic gases detected |
| **ppm** | Parts per million |
| **ppb** | Parts per billion |
| **I2C** | Inter-Integrated Circuit - two-wire serial communication protocol |
| **SDA** | Serial Data line (I2C) |
| **SCL** | Serial Clock line (I2C) |
| **MOX** | Metal Oxide - gas sensing technology used in CCS811 |
| **FSD** | Functional Specification Document |

### 1.4 References

- [Joy-IT SEN-CCS811V1 Product Page](https://joy-it.net/en/products/SEN-CCS811V1)
- [CCS811 Datasheet (ams AG)](https://cdn.sparkfun.com/assets/learn_tutorials/1/4/3/CCS811_Datasheet-DS000459.pdf)
- [Raspberry Pi Pico Datasheet](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)

---

## 2. System Requirements

### 2.1 Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| **FR-01** | System shall read eCO2 values from CCS811 sensor (400-8192 ppm) | High |
| **FR-02** | System shall read TVOC values from CCS811 sensor (0-1187 ppb) | High |
| **FR-03** | System shall communicate with sensor via I2C at address 0x5A | High |
| **FR-04** | System shall output measurements via USB serial at 115200 baud | High |
| **FR-05** | System shall indicate sensor status via onboard LED | Medium |
| **FR-06** | System shall support configurable measurement intervals (1s, 10s, 60s) | Medium |
| **FR-07** | System shall detect and report sensor errors | High |
| **FR-08** | System shall verify sensor hardware ID on startup | High |
| **FR-09** | System shall support SWD debugging via debug probe | Medium |
| **FR-10** | System shall track sensor warm-up status | Medium |

### 2.2 Non-Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| **NFR-01** | System shall initialize sensor within 5 seconds of power-on | High |
| **NFR-02** | System shall operate continuously without memory leaks | High |
| **NFR-03** | I2C communication shall operate at 100 kHz (standard mode) | Medium |
| **NFR-04** | Serial output shall be human-readable and machine-parseable | Medium |
| **NFR-05** | Code shall be modular with separate driver and application layers | Medium |
| **NFR-06** | System shall compile with Pico SDK and arm-none-eabi-gcc | High |

### 2.3 Hardware Requirements

| Component | Specification |
|-----------|---------------|
| **MCU** | Raspberry Pi Pico (RP2040) |
| **Sensor** | Joy-IT SEN-CCS811V1 |
| **Debug** | Raspberry Pi Debug Probe (CMSIS-DAP) |
| **Power** | USB 5V or 3.3V regulated |
| **I2C Pull-ups** | 4.7kΩ (included on sensor module) |

### 2.4 Software Requirements

| Component | Version |
|-----------|---------|
| **Pico SDK** | 2.0+ |
| **CMake** | 3.13+ |
| **ARM Toolchain** | arm-none-eabi-gcc 10.3+ |
| **OpenOCD** | 0.12+ |

---

## 3. Hardware Design

### 3.1 System Block Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     SYSTEM OVERVIEW                          │
└─────────────────────────────────────────────────────────────┘

    ┌─────────────┐         I2C          ┌─────────────┐
    │             │◄───────SDA──────────►│             │
    │  Raspberry  │◄───────SCL──────────►│   CCS811    │
    │    Pi       │                       │   Sensor    │
    │   Pico      │───────3.3V──────────►│  (Joy-IT)   │
    │             │───────GND───────────►│             │
    │  (RP2040)   │                       │             │
    └──────┬──────┘                       └─────────────┘
           │
           │ USB
           ▼
    ┌─────────────┐         SWD          ┌─────────────┐
    │    Host     │◄────────────────────►│   Debug     │
    │     PC      │                       │   Probe     │
    │  (Serial)   │                       │ (CMSIS-DAP) │
    └─────────────┘                       └─────────────┘
```

### 3.2 Pin Assignments

#### 3.2.1 Pico to CCS811 Connections

| Pico Pin | GPIO | Function | CCS811 Pin | Notes |
|----------|------|----------|------------|-------|
| Pin 6 | GP4 | I2C0 SDA | SDA | Data line |
| Pin 7 | GP5 | I2C0 SCL | SCL | Clock line |
| Pin 36 | 3V3(OUT) | Power | VIN | 3.3V supply |
| Pin 38 | GND | Ground | GND | Common ground |
| Pin 38 | GND | Ground | WAKE | Tie low (always active) |
| - | - | - | RST | Leave floating (has pull-up) |
| - | - | - | INT | Not used (optional) |

#### 3.2.2 Debug Probe Connections

| Debug Probe | Pico Pin | Function |
|-------------|----------|----------|
| SWCLK | SWCLK | Debug clock |
| SWDIO | SWDIO | Debug data |
| GND | GND | Ground |

### 3.3 Power Supply

| Parameter | Value |
|-----------|-------|
| Pico Supply | 5V USB or VSYS |
| CCS811 Supply | 3.3V from Pico 3V3(OUT) |
| CCS811 Current (idle) | ~0.6 mA |
| CCS811 Current (active) | ~30 mA peak |
| Total System Current | < 100 mA |

### 3.4 Wiring Diagram

See: [docs/hardware/wiring_diagram.md](hardware/wiring_diagram.md)

---

## 4. Software Architecture

### 4.1 Module Structure

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                         │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  main.c - Main application loop                      │    │
│  │  - Initialization                                    │    │
│  │  - Measurement scheduling                            │    │
│  │  - Serial output formatting                          │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     DRIVER LAYER                             │
│  ┌─────────────────────┐    ┌─────────────────────────┐     │
│  │  ccs811.c/.h        │    │  (future sensors)       │     │
│  │  - Sensor init      │    │                         │     │
│  │  - Read eCO2/TVOC   │    │                         │     │
│  │  - Mode config      │    │                         │     │
│  │  - Error handling   │    │                         │     │
│  └─────────────────────┘    └─────────────────────────┘     │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   HARDWARE ABSTRACTION                       │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  Pico SDK (hardware/i2c, hardware/gpio, pico/stdio) │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 File Structure

```
pico_test_project/
├── CMakeLists.txt
├── main.c                    # Application entry point
├── src/
│   └── drivers/
│       ├── ccs811.c          # CCS811 driver implementation
│       └── ccs811.h          # CCS811 driver header
├── docs/
│   ├── FSD_CCS811_Sensor.md  # This document
│   └── hardware/
│       ├── wiring_diagram.md
│       └── bom.md
└── .vscode/                  # Debug configuration
```

### 4.3 Driver API (ccs811.h)

```c
// Initialization
int ccs811_init(i2c_inst_t *i2c, uint8_t addr);

// Configuration
int ccs811_set_mode(uint8_t mode);  // 0=idle, 1=1s, 2=10s, 3=60s, 4=250ms

// Data acquisition
int ccs811_read_data(uint16_t *eco2, uint16_t *tvoc);
bool ccs811_data_ready(void);

// Status
uint8_t ccs811_get_status(void);
uint8_t ccs811_get_error(void);
bool ccs811_is_warmed_up(void);

// Calibration (optional)
int ccs811_set_env_data(float temperature, float humidity);
```

### 4.4 Main Application Flow

```
┌─────────────┐
│   START     │
└──────┬──────┘
       ▼
┌─────────────────┐
│ Initialize:     │
│ - stdio (USB)   │
│ - I2C bus       │
│ - CCS811 sensor │
└────────┬────────┘
         ▼
    ┌────────────┐     No
    │ Init OK?   │─────────► Error LED + retry
    └─────┬──────┘
          │ Yes
          ▼
┌─────────────────┐
│ Set measurement │
│ mode (1 second) │
└────────┬────────┘
         ▼
    ┌─────────────────┐
    │   MAIN LOOP     │◄───────────────┐
    └────────┬────────┘                │
             ▼                         │
    ┌────────────────┐    No           │
    │ Data ready?    │─────────────────┤
    └────────┬───────┘                 │
             │ Yes                     │
             ▼                         │
    ┌────────────────┐                 │
    │ Read eCO2/TVOC │                 │
    └────────┬───────┘                 │
             ▼                         │
    ┌────────────────┐                 │
    │ Output to USB  │                 │
    │ serial         │                 │
    └────────┬───────┘                 │
             ▼                         │
    ┌────────────────┐                 │
    │ Toggle LED     │                 │
    └────────┬───────┘                 │
             ▼                         │
    ┌────────────────┐                 │
    │ Delay / yield  │─────────────────┘
    └────────────────┘
```

---

## 5. I2C Protocol Specification

### 5.1 I2C Configuration

| Parameter | Value |
|-----------|-------|
| Mode | Standard (100 kHz) |
| Address | 0x5A (ADDR pin low) |
| Pull-ups | 4.7kΩ (on module) |

### 5.2 CCS811 Register Map

| Address | Name | R/W | Size | Description |
|---------|------|-----|------|-------------|
| 0x00 | STATUS | R | 1 | Status register |
| 0x01 | MEAS_MODE | R/W | 1 | Measurement mode |
| 0x02 | ALG_RESULT_DATA | R | 8 | Algorithm results (eCO2, TVOC, status, error, raw) |
| 0x03 | RAW_DATA | R | 2 | Raw ADC data |
| 0x05 | ENV_DATA | W | 4 | Temperature/humidity compensation |
| 0x10 | THRESHOLDS | W | 4 | Interrupt thresholds |
| 0x11 | BASELINE | R/W | 2 | Algorithm baseline |
| 0x20 | HW_ID | R | 1 | Hardware ID (must be 0x81) |
| 0x21 | HW_VERSION | R | 1 | Hardware version |
| 0x23 | FW_BOOT_VERSION | R | 2 | Bootloader version |
| 0x24 | FW_APP_VERSION | R | 2 | Application version |
| 0xE0 | ERROR_ID | R | 1 | Error code |
| 0xF4 | APP_START | W | 0 | Start application mode |
| 0xFF | SW_RESET | W | 4 | Software reset |

### 5.3 STATUS Register (0x00)

| Bit | Name | Description |
|-----|------|-------------|
| 7 | FW_MODE | 0=Boot mode, 1=Application mode |
| 6 | - | Reserved |
| 5 | APP_VALID | Valid application firmware loaded |
| 4 | DATA_READY | New data available |
| 3 | - | Reserved |
| 2:0 | - | Reserved |
| 0 | ERROR | Error occurred (read ERROR_ID) |

### 5.4 MEAS_MODE Register (0x01)

| Bit | Name | Description |
|-----|------|-------------|
| 7 | - | Reserved |
| 6:4 | DRIVE_MODE | Measurement mode (see below) |
| 3 | INT_DATARDY | Enable interrupt on data ready |
| 2 | INT_THRESH | Enable interrupt on threshold |
| 1:0 | - | Reserved |

**DRIVE_MODE values:**
| Value | Mode | Measurement Interval |
|-------|------|---------------------|
| 0 | Idle | No measurements |
| 1 | Constant Power | Every 1 second |
| 2 | Pulse Heating | Every 10 seconds |
| 3 | Low Power Pulse | Every 60 seconds |
| 4 | Constant Power | Every 250ms (RAW only) |

### 5.5 ALG_RESULT_DATA (0x02) - 8 bytes

| Byte | Content |
|------|---------|
| 0-1 | eCO2 (ppm) - MSB first |
| 2-3 | TVOC (ppb) - MSB first |
| 4 | STATUS |
| 5 | ERROR_ID |
| 6-7 | RAW_DATA |

### 5.6 Initialization Sequence

```
1. Wait 20ms after power-on
2. Read HW_ID (0x20) → verify == 0x81
3. Read STATUS (0x00) → verify APP_VALID bit set
4. Write APP_START (0xF4) → 0 bytes (trigger boot to app)
5. Wait 1ms
6. Read STATUS (0x00) → verify FW_MODE bit set
7. Write MEAS_MODE (0x01) → 0x10 (mode 1, 1 second)
8. Sensor is now measuring
```

### 5.7 Data Read Sequence

```
1. Read STATUS (0x00)
2. If DATA_READY bit set:
   a. Read ALG_RESULT_DATA (0x02) → 8 bytes
   b. Extract eCO2 = (byte[0] << 8) | byte[1]
   c. Extract TVOC = (byte[2] << 8) | byte[3]
   d. Check byte[4] for status
   e. Check byte[5] for errors
```

### 5.8 Error Codes (ERROR_ID - 0xE0)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | WRITE_REG_INVALID | Invalid register write |
| 1 | READ_REG_INVALID | Invalid register read |
| 2 | MEASMODE_INVALID | Invalid measurement mode |
| 3 | MAX_RESISTANCE | Sensor resistance too high |
| 4 | HEATER_FAULT | Heater supply fault |
| 5 | HEATER_SUPPLY | Heater current fault |

---

## 6. Data Format Specification

### 6.1 Serial Output Format

Output shall be CSV-compatible with header:

```
# CCS811 Sensor Data Log
# Format: timestamp,eco2_ppm,tvoc_ppb,status,warm_up
timestamp,eco2_ppm,tvoc_ppb,status,warm_up
00:00:01,400,0,OK,false
00:00:02,412,3,OK,false
00:00:03,425,5,OK,false
...
00:20:01,450,12,OK,true
```

### 6.2 Field Definitions

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| timestamp | string | HH:MM:SS | Time since boot |
| eco2_ppm | uint16 | 400-8192 | eCO2 in ppm |
| tvoc_ppb | uint16 | 0-1187 | TVOC in ppb |
| status | string | OK/ERROR | Sensor status |
| warm_up | bool | true/false | Warm-up complete (>20 min) |

### 6.3 Status Messages

```
[INFO] CCS811 initialized successfully
[INFO] Hardware ID: 0x81
[INFO] Firmware version: X.X.X
[INFO] Measurement mode: 1 (1 second interval)
[WARN] Sensor warming up - data may be inaccurate
[INFO] Warm-up complete - data is now valid
[ERROR] I2C communication failed
[ERROR] Sensor error: <error_code>
```

---

## 7. Test Plan

### 7.1 Unit Tests

| Test ID | Description | Expected Result |
|---------|-------------|-----------------|
| UT-01 | I2C bus initialization | I2C0 configured at 100kHz |
| UT-02 | CCS811 hardware ID read | Returns 0x81 |
| UT-03 | CCS811 status register read | Returns valid status byte |
| UT-04 | CCS811 mode configuration | Mode register updated |
| UT-05 | Data ready flag polling | Flag set after measurement |

### 7.2 Integration Tests

| Test ID | Description | Expected Result |
|---------|-------------|-----------------|
| IT-01 | Full initialization sequence | Sensor enters app mode |
| IT-02 | Continuous data reading | Data received every interval |
| IT-03 | Serial output formatting | Valid CSV output |
| IT-04 | Error recovery | System recovers from I2C error |
| IT-05 | Mode switching | Measurement interval changes |

### 7.3 Validation Tests

| Test ID | Description | Expected Result |
|---------|-------------|-----------------|
| VT-01 | Baseline reading (clean air) | eCO2 ~400 ppm, TVOC ~0 ppb |
| VT-02 | Breath test (elevated CO2) | eCO2 increases significantly |
| VT-03 | VOC source test | TVOC increases with source |
| VT-04 | 24-hour stability | Readings stable after warm-up |
| VT-05 | Warm-up timing | Valid data after 20 minutes |

### 7.4 Debug Probe Tests

| Test ID | Description | Expected Result |
|---------|-------------|-----------------|
| DT-01 | SWD connection | OpenOCD connects successfully |
| DT-02 | Breakpoint in ccs811_read | Execution halts at breakpoint |
| DT-03 | Variable inspection | eCO2/TVOC values readable |
| DT-04 | Single-step through I2C | I2C transactions visible |

---

## 8. Appendices

### 8.1 Bill of Materials

See: [docs/hardware/bom.md](hardware/bom.md)

### 8.2 Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-03-30 | Claude | Initial draft |

### 8.3 Approval

| Role | Name | Signature | Date |
|------|------|-----------|------|
| Author | | | |
| Reviewer | | | |
| Approver | | | |
