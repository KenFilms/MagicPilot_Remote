import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


BOARD_BY_USB_ID = {
    ("303A", "1001"): "m5stack-cores3",
    ("303A", "8119"): "m5stack-cores3",
    ("10C4", "EA60"): "m5stack-core2",
}


def find_platformio():
    executable = shutil.which("pio")
    if executable:
        return executable

    candidates = [
        Path.home() / ".platformio" / "penv" / "bin" / "pio",
        Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    return None


def read_usb_id(port):
    vendor_id = str(port.get("vendorId", "")).upper().removeprefix("0X")
    product_id = str(port.get("productId", "")).upper().removeprefix("0X")
    if vendor_id and product_id:
        return vendor_id, product_id

    match = re.search(r"VID:PID=([0-9A-F]{4}):([0-9A-F]{4})", port.get("hwid", ""), re.I)
    return (match.group(1).upper(), match.group(2).upper()) if match else None


def main():
    parser = argparse.ArgumentParser(description="Detect and upload to an M5Stack Core2 or CoreS3.")
    parser.add_argument("--dry-run", action="store_true", help="detect the board without building or flashing")
    args = parser.parse_args()

    pio = find_platformio()
    if not pio:
        print("PlatformIO CLI (pio) was not found. Install PlatformIO or add pio to PATH.", file=sys.stderr)
        return 1

    result = subprocess.run(
        [pio, "device", "list", "--json-output"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        print(result.stderr or "PlatformIO could not list serial devices.", file=sys.stderr)
        return result.returncode

    try:
        ports = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        print(f"Could not parse PlatformIO device list: {error}", file=sys.stderr)
        return 1

    matches = []
    for port in ports:
        usb_id = read_usb_id(port)
        environment = BOARD_BY_USB_ID.get(usb_id)
        if environment:
            matches.append((environment, port.get("port")))

    if len(matches) != 1:
        print("Connect exactly one supported M5Stack Core2 or CoreS3 USB serial device.", file=sys.stderr)
        print("For manual upload, run: pio run -e m5stack-core2 -t upload", file=sys.stderr)
        print("or: pio run -e m5stack-cores3 -t upload", file=sys.stderr)
        return 1

    environment, port = matches[0]
    print(f"Detected {environment} on {port}.")
    if args.dry_run:
        print(f"Dry run: would upload environment {environment}.")
        return 0

    return subprocess.call([pio, "run", "-e", environment, "-t", "upload", "--upload-port", port])


if __name__ == "__main__":
    raise SystemExit(main())