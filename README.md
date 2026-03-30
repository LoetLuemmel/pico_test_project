# Pico Test Project

A test project for Raspberry Pi Pico with Debug Probe support. Tests basic GPIO and LED functionality with full SWD debugging capabilities.

## Project Status

| Feature | Status |
|---------|--------|
| LED Blink (GPIO 25) | Verified |
| GPIO Output (GPIO 16) | Verified |
| GPIO Input (GPIO 17) | Verified |
| USB Serial Output | Verified |
| SWD Debug Connection | Verified |
| Hardware Breakpoints | Verified |
| Single-Step Debugging | Verified |
| Memory/Register Read | Verified |

### Verified Debug Probe

```
Device: Raspberry Pi Debug Probe (CMSIS-DAP)
VID:PID: 0x2e8a:0x000c
Protocol: CMSIS-DAPv2 + SWD
Speed: 5000 kHz
```

## Prerequisites

### 1. Install Homebrew (if not installed)
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 2. Install ARM Toolchain and Build Tools
```bash
brew install cmake
brew install armmbed/formulae/arm-none-eabi-gcc
brew install openocd
```

If you get linking errors with arm-none-eabi-gcc:
```bash
brew link --overwrite arm-none-eabi-gcc
```

### 3. Install Pico SDK
```bash
cd ~
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
```

Add to your shell profile (`~/.zshrc`):
```bash
echo 'export PICO_SDK_PATH=~/pico-sdk' >> ~/.zshrc
source ~/.zshrc
```

### 4. VS Code Extensions (for debugging)
Install these VS Code extensions:
- **Cortex-Debug** (marus25.cortex-debug) - Required for debugging
- **C/C++** (ms-vscode.cpptools) - IntelliSense support
- **CMake Tools** (ms-vscode.cmake-tools) - Optional

## Hardware Setup

### Debug Probe Connections (SWD)

Connect the Pico Debug Probe to your target Pico:

| Debug Probe | Target Pico | Pin |
|-------------|-------------|-----|
| SWCLK | SWCLK | Debug header |
| GND | GND | Any GND |
| SWDIO | SWDIO | Debug header |

Optional UART for serial debugging:

| Debug Probe | Target Pico | Pin |
|-------------|-------------|-----|
| UART TX | GP1 | Pin 2 |
| UART RX | GP0 | Pin 1 |

### Test GPIO Wiring (Optional)

For loopback GPIO testing, connect:
- GPIO 16 (output) to GPIO 17 (input)

## Building the Project

### Command Line
```bash
cd pico_test_project

# Configure (Debug build for breakpoints)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build
```

### Output Files
After building, you'll find in the `build/` directory:
- `pico_test.uf2` - For drag-and-drop flashing
- `pico_test.elf` - For debug probe flashing (includes symbols)
- `pico_test.bin` - Raw binary

## Flashing

### Method 1: Debug Probe (Recommended)
```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
    -c "adapter speed 5000" \
    -c "program build/pico_test.elf verify reset exit"
```

### Method 2: UF2 Drag-and-Drop
1. Hold BOOTSEL button on target Pico
2. Connect USB while holding BOOTSEL
3. Release BOOTSEL - Pico mounts as RPI-RP2
4. Copy `build/pico_test.uf2` to the drive

## Debugging

### VS Code (Recommended)

1. Open the project folder in VS Code
2. Ensure the debug probe is connected
3. Press **F5** or go to Run > Start Debugging
4. Select "Pico Debug (Cortex-Debug)"

#### Debug Features
- **Breakpoints**: Click in gutter or set on `toggle_led()` function
- **Watch Variables**: Add `loop_counter` and `led_state`
- **Step Through**: F10 (step over), F11 (step into)
- **Registers**: View ARM Cortex-M0+ registers
- **Peripheral Viewer**: View GPIO/Timer registers (requires SVD)

### Command Line (OpenOCD + GDB)

Start OpenOCD server:
```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000"
```

In another terminal, connect with GDB:
```bash
arm-none-eabi-gdb build/pico_test.elf
(gdb) target extended-remote localhost:3333
(gdb) monitor reset halt
(gdb) break toggle_led
(gdb) continue
```

### Debug Symbols

Key symbols and their addresses (may vary with build):

| Symbol | Type | Address |
|--------|------|---------|
| `toggle_led` | Function | 0x1000030c |
| `loop_counter` | Variable | 0x2000198c |
| `led_state` | Variable | 0x20001bdb |

Get current addresses:
```bash
arm-none-eabi-nm build/pico_test.elf | grep -E "toggle_led|loop_counter|led_state"
```

## Test Program Features

The test program (`main.c`) provides:

1. **LED Blink** - Onboard LED (GPIO 25) toggles at 1Hz
2. **GPIO Test** - GPIO 16 output, GPIO 17 input with pull-down
3. **USB Serial** - Prints status every 10 loop iterations
4. **Debug Points** - `toggle_led()` function marked `noinline` for breakpoints

### USB Serial Monitor
```bash
# Find the USB serial port
ls /dev/cu.usbmodem*

# Connect with screen
screen /dev/cu.usbmodem* 115200
```
Press `Ctrl+A`, then `K` to exit screen.

### Expected Serial Output
```
================================
  Pico Test Project Started
  Debug Probe Ready
================================

Initializing GPIO...
GPIO initialized successfully.
LED Pin: GPIO 25
Test Output: GPIO 16
Test Input: GPIO 17

Starting main loop...

=== Pico Test Status ===
Loop count: 10
LED state: OFF
GPIO 17 (input): 0
========================
```

## Troubleshooting

### PICO_SDK_PATH not set
```bash
echo $PICO_SDK_PATH
# Should output: /Users/yourusername/pico-sdk
```

### Debug Probe Not Found
```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "exit"
```

### IntelliSense Errors in VS Code
1. Ensure you've built the project at least once
2. Reload VS Code window: `Cmd+Shift+P` -> "Reload Window"
3. Check that `~/pico-sdk` exists

### Check USB Devices
```bash
system_profiler SPUSBDataType | grep -A8 "Debug Probe"
```

## Project Structure

```
pico_test_project/
├── CMakeLists.txt            # CMake build configuration
├── pico_sdk_import.cmake     # Pico SDK import helper
├── main.c                    # Test application source
├── README.md                 # This documentation
├── .gitignore                # Git ignore rules
└── .vscode/
    ├── launch.json           # Debug configurations (Cortex-Debug)
    ├── tasks.json            # Build tasks
    ├── settings.json         # Project settings
    └── c_cpp_properties.json # IntelliSense configuration
```

## License

This project is provided as-is for testing and educational purposes.
