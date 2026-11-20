#!/usr/bin/env bash
# Builds one flashable image (offset 0x0) for the web flasher.
set -euo pipefail
cd "$(dirname "$0")"
pio run -e cores3
B=.pio/build/cores3
BOOT_APP0=$(find ~/.platformio/packages/framework-arduinoespressif32 -name boot_app0.bin | head -1)
mkdir -p ../flash/firmware
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 merge_bin \
  -o ../flash/firmware/wisp-box.bin --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xe000 "$BOOT_APP0" 0x10000 $B/firmware.bin
VER=$(grep -o 'WISP_VERSION=\\"[^\\]*' platformio.ini | sed 's/.*"//')
cat > ../flash/firmware/manifest.json <<JSON
{
  "name": "Wisp Box",
  "version": "${VER:-dev}",
  "new_install_prompt_erase": true,
  "builds": [{ "chipFamily": "ESP32-S3", "parts": [{ "path": "wisp-box.bin", "offset": 0 }] }]
}
JSON
echo "merged: $(du -h ../flash/firmware/wisp-box.bin | cut -f1)"
