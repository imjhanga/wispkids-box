# Wisp Box

A physical Wisp for WispKids on the M5Stack CoreS3. Experimental, kept separate from the main WispKids repo.

## Try it
1. Open the flash page (Cloudflare Pages project `wispkids-box`) in Chrome or Edge on a computer.
2. Plug in the CoreS3 with USB-C and click **Install Wisp**.

No flash page yet? Download the `wisp-box-firmware` artifact from the latest GitHub Actions run, open
https://espressif.github.io/esptool-js/ in Chrome, connect, and flash `wisp-box.bin` at address `0x0`.

## What Wisp does (milestone 1)
| Do this | Wisp |
|---|---|
| Tap the screen | happy |
| Tap 3 times quickly | laughs |
| Shake gently | excited |
| Keep shaking | dizzy |
| Hold your hand near the top edge sensor | curious |
| Hand very close | shy |
| Cover it or turn off the light (3 s) | falls asleep; light or a tap wakes it |

## Build locally
`pip install platformio && firmware/merge.sh` (output in `flash/firmware/`).
