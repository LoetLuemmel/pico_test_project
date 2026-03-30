# CLAUDE.md — Pico CCS811 Hardware-in-the-Loop Demo

## Project purpose

This project demonstrates **gradual, measurable firmware improvement** driven by Claude Code in an automated hardware-in-the-loop cycle. The target is a Joy-IT SEN-CCS811V1 air quality sensor (eCO2 + TVOC) connected to a Raspberry Pi Pico, with a Raspberry Pi Debug Probe for flashing and serial capture.

Each iteration produces a PR whose description contains before/after metrics proving the improvement. The git history **is** the demo artifact.

---

## Hardware setup

### Wiring

| CCS811 Pin | Pico Pin            | Notes                                  |
|------------|---------------------|----------------------------------------|
| VCC        | 3V3 OUT (pin 36)    | 3.3 V only — never 5 V                |
| GND        | GND (pin 38)        | Common ground                          |
| SDA        | GP4 (pin 6)         | I2C0 data                              |
| SCL        | GP5 (pin 7)         | I2C0 clock                             |
| WAKE       | GND (pin 8)         | Must be grounded for I2C communication |
| INT        | GP6 (pin 9)         | Optional — wire now, use in later iterations |

### Debug probe connections

| Probe Pin | Pico Pin             |
|-----------|----------------------|
| SWCLK     | SWCLK (SWD header)   |
| SWDIO     | SWDIO (SWD header)   |
| GND       | GND (SWD header)     |
| UART TX   | GP1 / UART0 RX (pin 2) |
| UART RX   | GP0 / UART0 TX (pin 1) |

### Sensor characteristics (CCS811)

- I2C address: `0x5A`
- Requires 48-hour initial burn-in, 20-minute warm-up on every power cycle
- Measurement modes: Mode 1 (1 s), Mode 2 (10 s), Mode 3 (60 s), Mode 4 (250 ms raw)
- eCO2 range: 400–8192 ppm; TVOC range: 0–1187 ppb
- Uses I2C clock stretching — the RP2040 hardware I2C peripheral supports this
- Baseline register can be read/written for calibration persistence
- nINT pin signals data-ready (active low, optional)

---

## Repository structure

```
pico-ccs811-hil/
├── CLAUDE.md              ← you are here
├── CMakeLists.txt         ← top-level CMake (Pico SDK project)
├── src/
│   ├── main.c             ← entry point, sensor init, main loop
│   ├── ccs811.h           ← driver header
│   ├── ccs811.c           ← driver implementation (evolves each iteration)
│   ├── metrics.h          ← metric collection helpers
│   └── metrics.c          ← computes stddev, failure rate, etc.
├── test/
│   ├── harness.py         ← Python script: flash → capture serial → parse metrics
│   └── metrics_log/       ← per-iteration JSON metric snapshots
├── docs/
│   └── iterations.md      ← human-readable log of what changed and why
└── .github/
    └── workflows/
        └── claude.yml     ← GitHub Actions workflow for @claude mentions
```

---

## Build and flash commands

```bash
# Configure (first time)
mkdir build && cd build
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH ..

# Build
cd build && make -j$(nproc)

# Flash via debug probe (OpenOCD)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program pico-ccs811-hil.elf verify reset exit"

# Alternatively, flash via picotool over SWD
picotool load -f pico-ccs811-hil.uf2
```

---

## Test harness (test/harness.py)

The harness is the backbone of the automated loop. It:

1. Builds the firmware (`make`)
2. Flashes it via the debug probe
3. Opens the debug probe's UART serial port (typically `/dev/ttyACM0`, 115200 baud)
4. Captures 60 seconds of structured output lines
5. Parses metrics from the output
6. Writes a JSON snapshot to `test/metrics_log/iteration_NN.json`
7. Prints a summary to stdout

### Expected serial output format

Every firmware iteration **must** print metrics in this parseable format on UART0:

```
[METRIC] read_ok=1 eco2=412 tvoc=3 ts_ms=12345
[METRIC] read_fail=1 err=I2C_TIMEOUT ts_ms=12400
[SUMMARY] reads=120 fails=3 fail_rate=2.50 eco2_mean=438 eco2_stddev=24.7 tvoc_mean=8 tvoc_stddev=3.1 uptime_s=60
```

- `[METRIC]` lines: one per read attempt (success or failure)
- `[SUMMARY]` line: emitted once at the end of the capture window
- All values are plain integers or fixed-point decimals — no floats with exponents

### Metrics we track across iterations

| Metric            | Unit    | Goal       | Notes                                    |
|-------------------|---------|------------|------------------------------------------|
| `fail_rate`       | %       | → 0        | I2C read failures / total attempts       |
| `eco2_stddev`     | ppm     | ↓ lower    | Noise on the eCO2 stream (60 s window)   |
| `tvoc_stddev`     | ppb     | ↓ lower    | Noise on the TVOC stream (60 s window)   |
| `first_valid_ms`  | ms      | ↓ lower    | Time from boot to first valid reading    |
| `eco2_drift`      | ppm     | ↓ lower    | Max − min of 10-sample moving average    |
| `power_ua`        | µA avg  | ↓ lower    | Estimated average current draw           |
| `boot_self_test`  | pass/fail | → pass   | Hardware self-test on startup            |

---

## Iteration plan

Each iteration is a separate branch and PR. The PR description **must** include a metrics table comparing before/after.

### Iteration 1 — Naive driver
- Bare-bones I2C init + read of ALG_RESULT_DATA (register 0x02)
- No error handling, no warm-up logic, no filtering
- Expect: high `fail_rate`, high `eco2_stddev`, no `boot_self_test`

### Iteration 2 — Robust I2C communication
- Add status register check before reading data
- Add I2C retry with exponential backoff (3 attempts, 10/50/200 ms)
- Validate the DATA_READY flag in the status register
- Check for hardware/firmware errors via ERROR_ID register (0xE0)
- Expect: `fail_rate` drops dramatically

### Iteration 3 — Sensor lifecycle management
- Implement correct APP_START sequence (check STATUS, write 0xF4)
- Verify HW_ID (0x81) and HW_VERSION on boot
- Add configurable mode selection (default Mode 1)
- Implement 20-minute warm-up awareness: flag readings as `warming_up=true`
- Expect: `first_valid_ms` becomes meaningful, early readings less erratic

### Iteration 4 — Signal conditioning
- Add moving-average filter (configurable window, default N=5)
- Add outlier rejection (discard readings > 3σ from running mean)
- Optional: simple 1D Kalman filter on eCO2 stream
- Expect: `eco2_stddev` and `tvoc_stddev` drop significantly
- Also measure step-response latency (breathe on sensor → time to peak)

### Iteration 5 — Baseline calibration
- Read baseline register after 20-min warm-up
- Store baseline to Pico flash (use flash_range_program)
- Restore baseline on boot if stored value exists and is < 24 h old
- Feed environmental compensation data (ENV_DATA register 0x05) if available
- Expect: `eco2_drift` improves across power cycles

### Iteration 6 — Power optimization
- Use WAKE pin (rewire from GND to GP6): assert low before I2C, release after
- Switch to Mode 3 (60 s interval) for low-power duty cycling
- Sleep Pico between measurements using `sleep_ms()` or dormant mode
- Expect: `power_ua` drops, `fail_rate` stays at 0

### Iteration 7 — Diagnostics and self-test
- Automated boot self-test: verify HW_ID, FW_BOOT_VERSION, FW_APP_VERSION
- Structured error logging with timestamps and error codes
- Track cumulative error statistics (histogram of error types)
- Add a `[DIAG]` output section for the test harness
- Expect: `boot_self_test` = pass, full error coverage

---

## Rules for Claude Code

### Do
- Always run the test harness after flashing and include metrics in your PR description
- Keep each iteration focused on one improvement area — small, reviewable diffs
- Use the Pico SDK's hardware I2C API (`hardware/i2c.h`), not bit-banged I2C
- Use `printf()` over UART0 for all output (the debug probe captures this)
- Write C (not C++) — this is a bare-metal Pico SDK project
- Commit with descriptive messages: `iter-03: add exponential backoff on I2C retry`
- Always check that the firmware compiles before committing

### Don't
- Don't skip iterations or combine multiple improvement areas into one PR
- Don't use `stdio_init_all()` with USB — we use UART only (debug probe)
- Don't hardcode I2C pins — use `#define` constants in the header
- Don't assume the sensor is ready immediately after power-on
- Don't write to flash on every boot — only update baseline when it's stale
- Don't use floating-point in ISRs or time-critical paths

### PR description template

```markdown
## Iteration N: [Short title]

### What changed
[1–3 sentences describing the improvement]

### Metrics (60 s capture window)

| Metric         | Before (iter N-1) | After (iter N) | Change   |
|----------------|--------------------:|---------------:|---------:|
| fail_rate      |              X.XX% |          X.XX% | ↓ X.XX%  |
| eco2_stddev    |              XX.X  |          XX.X  | ↓ XX.X   |
| tvoc_stddev    |              XX.X  |          XX.X  | ↓ XX.X   |
| first_valid_ms |              XXXX  |          XXXX  | ↓ XXXX   |

### Test stimulus
[Describe test conditions: ambient air, breath test, marker pen, etc.]

### Next iteration plan
[What should iteration N+1 tackle?]
```

---

## Quick reference: CCS811 register map

| Register      | Addr | R/W | Description                        |
|---------------|------|-----|------------------------------------|
| STATUS        | 0x00 | R   | Sensor status (DATA_READY, etc.)   |
| MEAS_MODE     | 0x01 | R/W | Measurement mode and interrupt cfg |
| ALG_RESULT_DATA | 0x02 | R | eCO2 (2B) + TVOC (2B) + status + error + raw |
| RAW_DATA      | 0x03 | R   | Raw ADC current and voltage        |
| ENV_DATA      | 0x05 | W   | Temperature and humidity compensation |
| BASELINE      | 0x11 | R/W | Algorithm baseline value           |
| HW_ID         | 0x20 | R   | Should read 0x81                   |
| HW_VERSION    | 0x21 | R   | Hardware version                   |
| FW_BOOT_VER   | 0x23 | R   | Firmware bootloader version        |
| FW_APP_VER    | 0x24 | R   | Firmware application version       |
| ERROR_ID      | 0xE0 | R   | Error source identification        |
| APP_START     | 0xF4 | W   | Transition from boot to app mode   |
| SW_RESET      | 0xFF | W   | Software reset (write 0x11 0xE5 0x72 0x8A) |

---

## Environment assumptions

- Pico SDK installed, `PICO_SDK_PATH` set
- `arm-none-eabi-gcc` toolchain installed
- OpenOCD or picotool installed and on PATH
- Debug probe appears as `/dev/ttyACM0` (UART) — adjust in harness.py if needed
- Python 3.8+ with `pyserial` installed for the test harness
- `gh` CLI authenticated for PR creation



## Serial port safety rules (macOS)

### CRITICAL — read this before any serial access

Claude Code has crashed repeatedly due to unsafe serial port access. Follow these rules strictly.

### Finding the correct port

The debug probe exposes TWO USB interfaces:
- **CMSIS-DAP** (SWD flashing) — do NOT open this as a serial port
- **UART bridge** — this is the one that carries `[METRIC]` lines

To identify the correct port:

```bash
# Step 1: List all available serial ports
ls /dev/cu.usbmodem* 2>/dev/null || echo "No USB serial ports found"

# Step 2: Check which port is the UART bridge (non-destructive)
for port in /dev/cu.usbmodem*; do
  echo "=== $port ==="
  stty -f "$port" 2>/dev/null && echo "accessible" || echo "busy/locked"
done
```

The correct UART port on this Mac is likely: `/dev/cu.usbmodem4302` or similar.
Update this line once confirmed: **UART_PORT=/dev/cu.usbmodemXXXX**

### Safe serial capture — NEVER use plain `cat`

Plain `cat /dev/cu.usbmodemXXXX` does NOT set the baud rate and will hang or crash.

**Always use Python with pyserial for serial capture:**

```bash
python3 -c "
import serial, time, sys
try:
    ser = serial.Serial('PORT_HERE', 115200, timeout=2)
    end = time.time() + 65
    while time.time() < end:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            print(line)
            sys.stdout.flush()
    ser.close()
except serial.SerialException as e:
    print(f'Serial error: {e}', file=sys.stderr)
    sys.exit(1)
except KeyboardInterrupt:
    ser.close()
"
```

### Rules

1. **NEVER** use `cat`, `timeout cat`, or `head` on serial ports — they don't set baud rate and cause hangs
2. **NEVER** open a port without first checking it exists: `test -e /dev/cu.usbmodemXXXX`
3. **ALWAYS** use pyserial with a `timeout` parameter on the Serial object
4. **ALWAYS** wrap serial access in try/except — the port may be busy, disconnected, or locked
5. **ALWAYS** close the port in a finally block
6. If `pyserial` is not installed, run: `pip3 install pyserial`
7. If a serial capture returns zero lines, do NOT retry more than twice — report the issue instead
8. The baud rate is **115200** — never omit this

### test/harness.py requirements

The test harness must:
- Accept the port as a command-line argument with a sensible default
- Set a hard timeout (65 seconds) using pyserial's timeout parameter
- NOT use subprocess `timeout` wrapping — it doesn't reliably kill serial reads on macOS
- Catch `serial.SerialException` and print a human-readable error
- Exit cleanly on Ctrl+C
