#!/usr/bin/env bash
# Build a single flashable image (bootloader + partitions + boot_app0 + app),
# writable at flash offset 0x0.  Output: dist/gridiron-esp32s3.bin
set -euo pipefail
cd "$(dirname "$0")/.."

ENV=esp32-s3-devkitc-1
BUILD=".pio/build/$ENV"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
BOOT_APP0="$(ls "$HOME"/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin)"

echo ">> building"
"$PIO" run -e "$ENV"

mkdir -p dist
echo ">> merging"
"$HOME/.platformio/penv/bin/python" -m esptool --chip esp32s3 merge_bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  -o dist/gridiron-esp32s3.bin \
  0x0     "$BUILD/bootloader.bin" \
  0x8000  "$BUILD/partitions.bin" \
  0xe000  "$BOOT_APP0" \
  0x10000 "$BUILD/firmware.bin"

echo ">> dist/gridiron-esp32s3.bin"
ls -l dist/gridiron-esp32s3.bin
echo ">> flash with:  esptool.py --chip esp32s3 -p <port> write_flash 0x0 dist/gridiron-esp32s3.bin"
