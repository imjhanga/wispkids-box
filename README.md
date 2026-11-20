# Wisp Box

A physical Wisp for WispKids on the M5Stack CoreS3. Experimental, kept separate from the main WispKids repo.

## Try it
1. Open the flash page (Cloudflare Pages project `wispkids-box`) in Chrome or Edge on a computer.
2. Plug in the CoreS3 with USB-C and click **Install Wisp**.

No flash page yet? Download the `wisp-box-firmware` artifact from the latest GitHub Actions run, open
https://espressif.github.io/esptool-js/ in Chrome, connect, and flash `wisp-box.bin` at address `0x0`.

## What Wisp does
| Do this | Wisp |
|---|---|
| Tap the screen | happy |
| Tap 3 times quickly | laughs |
| Stroke Wisp slowly | loved (eyes closed, hearts) |
| Shake gently / keep shaking | excited / dizzy |
| Tilt the box | looks that way |
| Hand near the top edge / very close | curious / shy |
| Dark room | drowsy, then asleep (screen dimmed); light wakes it |
| Turn it face-down | says goodnight, sound and screen off; turn it back up to wake |
| Leave it alone | drowsy, yawns, asleep, screen off (2/5/10 min or never, set in the grown-up menu) |
| Quick swipe left/right | other pages: Teach Wisp, Spells (coming soon) |
| Low battery / plugged in | hungry / eats sparks |

**Grown-up menu:** hold two opposite corners of the screen for 3 s (or the top-left corner alone for 5 s).
Volume, brightness, screen-off time and the mood word; the version and battery level are shown at the bottom.

## Regenerate the Wisp art
`NODE_PATH=<dir with @resvg/resvg-js> node tools/export_device_art.js ../wispkids/prototypes && python3 tools/make_sprites.py`

## Build locally
`pip install platformio && firmware/merge.sh` (output in `flash/firmware/`).
