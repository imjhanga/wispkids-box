# Wisp Box — CLAUDE.md

## What this is
A parallel experiment: a physical Wisp on the M5Stack CoreS3 that extends the WispKids Season One curriculum
module by module with "Field Missions" (same AI idea as the app module, but with real data the child collects).
Kept in its own repo on purpose: if the hardware idea doesn't work, archive this repo and nothing in the
main repo (imjhanga/WispKids) changes. This repo never writes to the main repo.

Hardware plan: project doc "WispKids Hardware Roadmap" (claude.ai Project). Decisions: `docs/decisions.md` (H-001 onward).

## Current milestone
M1 done — Wisp lives on the device (game art, moods, blink, sleep, grown-up menu, battery, face-down bedtime, tilt, pages).
M2 in test — Game 1 Teach Wisp (`firmware/src/teach_game.h`, Module 1 Field Mission). Next: confirm camera on device,
then the Field Missions for Modules 2-8 (see project doc map), QR handshake test with the app.

## Rules — same spirit as the main repo
- Season 1 hardware is fully offline: no Wi-Fi, no cloud, no audio or images stored or sent.
- Only used with the founder's own children and trusted friends' children, parent present.
- No live AI calls. Any learning on the device is on-device logic.
- Wisp art and palette come from the main repo (`prototypes/wisp-art/`); copies here may drift, re-sync before any merge.

## Build and flash
- Firmware: `firmware/` (PlatformIO, Arduino + M5Unified). `firmware/merge.sh` builds one image at offset 0x0.
- CI: `.github/workflows/build-and-flash-page.yml` builds on every push (artifact "wisp-box-firmware") and publishes
  `flash/` to Cloudflare Pages project `wispkids-box` once the CLOUDFLARE_API_TOKEN secret is added.
- Flashing: open the flash page in Chrome or Edge on a computer, plug in the CoreS3 by USB-C, click Install.

## Working style — save tokens (Jhang's preference)
Batch multi-step work into one script; print short summaries, never large raw output.
