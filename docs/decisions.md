# Wisp Box decisions log

## H-001 — Separate repo for the hardware experiment
- 21 Nov 2026: the hardware track lives in imjhanga/wispkids-box, separate from imjhanga/WispKids, so the current
  Season One curriculum is untouched while interest is gauged. If it works, a hidden switch in season-one-v2 links to
  the Field Missions (one reversible change, logged in the main repo). If not, this repo is archived.

## H-002 — Curriculum link: Field Missions, module by module
- Each Season One module gets a device "Field Mission" in the 10-minute extension slot: the same AI idea, tested with
  real, messy data the child collects (does it still work in the real world?). Not a repeat of the app game.
- Story reason: the box is how Wisp crosses into the child's world; each island keeper gives a Field Quest.
  Still to match against the chapter beats in the story doc.
- Every Field Mission needs an in-app fallback so the curriculum never depends on owning the hardware.

## H-003 — Toolchain and flashing
- Arduino framework + M5Unified on PlatformIO, built by GitHub Actions. Flashed from the browser with ESP Web Tools
  (Chrome/Edge on a computer). Offline voice (ESP-SR) may later need Espressif's own toolkit (ESP-IDF).

## H-004 — Milestone 1: Wisp lives on the device
- Spark Wisp drawn in code (no image files yet) with 8 moods: calm, happy, laugh, excited, dizzy, shy, curious, sleepy.
- Triggers: tap, 3 quick taps, shake, long shake, hand very close / near the top sensor, dark room.
- A small mood word shows at the bottom for testing; remove before kids' trials if it distracts.
