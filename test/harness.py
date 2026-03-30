#!/usr/bin/env python3
"""
Test harness for CCS811 sensor firmware.

Builds, flashes, captures serial output, and computes metrics.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import serial

# Default settings
DEFAULT_PORT = "/dev/cu.usbmodem4401"  # macOS USB serial
DEFAULT_BAUD = 115200
DEFAULT_DURATION = 60  # seconds
BUILD_DIR = Path(__file__).parent.parent / "build"
PROJECT_DIR = Path(__file__).parent.parent


def build_firmware():
    """Build the firmware using cmake/make."""
    print("[HARNESS] Building firmware...")

    # Ensure build directory exists and configure if needed
    if not (BUILD_DIR / "Makefile").exists():
        print("[HARNESS] Configuring CMake...")
        subprocess.run(
            ["cmake", "-DCMAKE_BUILD_TYPE=Debug", ".."],
            cwd=BUILD_DIR,
            check=True
        )

    # Build
    result = subprocess.run(
        ["make", "-j4"],
        cwd=BUILD_DIR,
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"[HARNESS] Build failed:\n{result.stderr}")
        return False

    print("[HARNESS] Build successful")
    return True


def flash_firmware():
    """Flash firmware via OpenOCD debug probe."""
    print("[HARNESS] Flashing firmware...")

    elf_path = BUILD_DIR / "pico_test.elf"
    if not elf_path.exists():
        print(f"[HARNESS] ELF file not found: {elf_path}")
        return False

    result = subprocess.run([
        "openocd",
        "-f", "interface/cmsis-dap.cfg",
        "-f", "target/rp2040.cfg",
        "-c", "adapter speed 5000",
        "-c", f"program {elf_path} verify reset exit"
    ], capture_output=True, text=True)

    if result.returncode != 0:
        print(f"[HARNESS] Flash failed:\n{result.stderr}")
        return False

    print("[HARNESS] Flash successful")
    return True


def capture_serial(port, baud, duration):
    """Capture serial output for specified duration."""
    print(f"[HARNESS] Capturing serial on {port} for {duration}s...")

    lines = []
    try:
        with serial.Serial(port, baud, timeout=1) as ser:
            ser.reset_input_buffer()
            start_time = time.time()

            while time.time() - start_time < duration:
                if ser.in_waiting > 0:
                    try:
                        line = ser.readline().decode('utf-8', errors='replace').strip()
                        if line:
                            lines.append(line)
                            print(f"  {line}")
                    except Exception as e:
                        print(f"[HARNESS] Read error: {e}")
                else:
                    time.sleep(0.05)

    except serial.SerialException as e:
        print(f"[HARNESS] Serial error: {e}")
        return None

    print(f"[HARNESS] Captured {len(lines)} lines")
    return lines


def parse_metrics(lines):
    """Parse metrics from captured output."""
    metrics = {
        "reads": 0,
        "fails": 0,
        "eco2_values": [],
        "tvoc_values": [],
        "errors": [],
        "first_valid_ms": None,
    }

    metric_pattern = re.compile(
        r'\[METRIC\]\s+read_ok=(\d+)\s+eco2=(\d+)\s+tvoc=(\d+)\s+ts_ms=(\d+)'
    )
    fail_pattern = re.compile(
        r'\[METRIC\]\s+read_fail=(\d+)\s+err=(\w+)\s+ts_ms=(\d+)'
    )
    summary_pattern = re.compile(
        r'\[SUMMARY\]\s+reads=(\d+)\s+fails=(\d+)\s+fail_rate=([\d.]+)'
    )

    for line in lines:
        match = metric_pattern.search(line)
        if match:
            metrics["reads"] += 1
            eco2 = int(match.group(2))
            tvoc = int(match.group(3))
            ts_ms = int(match.group(4))
            metrics["eco2_values"].append(eco2)
            metrics["tvoc_values"].append(tvoc)
            if metrics["first_valid_ms"] is None:
                metrics["first_valid_ms"] = ts_ms
            continue

        match = fail_pattern.search(line)
        if match:
            metrics["fails"] += 1
            metrics["errors"].append(match.group(2))
            continue

    return metrics


def compute_statistics(metrics):
    """Compute statistics from raw metrics."""
    import math

    stats = {
        "reads": metrics["reads"],
        "fails": metrics["fails"],
        "fail_rate": 0.0,
        "eco2_mean": 0.0,
        "eco2_stddev": 0.0,
        "tvoc_mean": 0.0,
        "tvoc_stddev": 0.0,
        "first_valid_ms": metrics["first_valid_ms"] or 0,
    }

    total = metrics["reads"] + metrics["fails"]
    if total > 0:
        stats["fail_rate"] = (metrics["fails"] / total) * 100.0

    if metrics["eco2_values"]:
        eco2 = metrics["eco2_values"]
        stats["eco2_mean"] = sum(eco2) / len(eco2)
        if len(eco2) > 1:
            variance = sum((x - stats["eco2_mean"]) ** 2 for x in eco2) / (len(eco2) - 1)
            stats["eco2_stddev"] = math.sqrt(variance)

    if metrics["tvoc_values"]:
        tvoc = metrics["tvoc_values"]
        stats["tvoc_mean"] = sum(tvoc) / len(tvoc)
        if len(tvoc) > 1:
            variance = sum((x - stats["tvoc_mean"]) ** 2 for x in tvoc) / (len(tvoc) - 1)
            stats["tvoc_stddev"] = math.sqrt(variance)

    return stats


def save_metrics(stats, iteration):
    """Save metrics to JSON file."""
    metrics_dir = Path(__file__).parent / "metrics_log"
    metrics_dir.mkdir(exist_ok=True)

    output = {
        "iteration": iteration,
        "timestamp": datetime.now().isoformat(),
        "metrics": stats
    }

    filepath = metrics_dir / f"iteration_{iteration:02d}.json"
    with open(filepath, 'w') as f:
        json.dump(output, f, indent=2)

    print(f"[HARNESS] Metrics saved to {filepath}")
    return filepath


def print_summary(stats):
    """Print metrics summary."""
    print("\n" + "=" * 50)
    print("METRICS SUMMARY")
    print("=" * 50)
    print(f"  Total reads:     {stats['reads']}")
    print(f"  Total fails:     {stats['fails']}")
    print(f"  Fail rate:       {stats['fail_rate']:.2f}%")
    print(f"  eCO2 mean:       {stats['eco2_mean']:.1f} ppm")
    print(f"  eCO2 stddev:     {stats['eco2_stddev']:.1f} ppm")
    print(f"  TVOC mean:       {stats['tvoc_mean']:.1f} ppb")
    print(f"  TVOC stddev:     {stats['tvoc_stddev']:.1f} ppb")
    print(f"  First valid:     {stats['first_valid_ms']} ms")
    print("=" * 50)


def main():
    parser = argparse.ArgumentParser(description="CCS811 Test Harness")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate")
    parser.add_argument("--duration", type=int, default=DEFAULT_DURATION, help="Capture duration (s)")
    parser.add_argument("--iteration", type=int, default=1, help="Iteration number")
    parser.add_argument("--skip-build", action="store_true", help="Skip build step")
    parser.add_argument("--skip-flash", action="store_true", help="Skip flash step")
    args = parser.parse_args()

    # Build
    if not args.skip_build:
        if not build_firmware():
            sys.exit(1)

    # Flash
    if not args.skip_flash:
        if not flash_firmware():
            sys.exit(1)
        # Wait for device to boot
        print("[HARNESS] Waiting for device to boot...")
        time.sleep(3)

    # Capture
    lines = capture_serial(args.port, args.baud, args.duration)
    if lines is None:
        sys.exit(1)

    # Parse and compute
    metrics = parse_metrics(lines)
    stats = compute_statistics(metrics)

    # Save and print
    save_metrics(stats, args.iteration)
    print_summary(stats)

    return 0


if __name__ == "__main__":
    sys.exit(main())
