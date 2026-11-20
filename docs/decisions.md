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

## H-005 — Same Wisp as the game, and a volume button
- 21 Nov 2026: the device now shows the exact Spark art from the game (`wisp_s1_*.png` from the main repo, copied to `art/`
  and converted by `tools/make_sprites.py`), not a drawn approximation. Moods map to art: dizzy uses "silly", sleepy uses
  "calm" with a dimmed scene and z's (no sleeping art exists yet). The drawn eyes lost blinking; add blink frames later if missed.
- Speaker button in the top-right corner cycles off / low / medium / high; default is low and the choice is remembered on the
  device (a setting only, nothing about the child).

## H-006 — Blinking, sleepy poses, screen off after 5 minutes
- 21 Nov 2026: device frames are now rendered by the game's own art engine (`tools/export_device_art.js`, reads the main repo's
  `season-one/art-characters.js` + `season-one-v2/src/wisp2.js` + `champion.js`), so faces match the app exactly. Device-only
  faces are built from the game's existing eye and mouth shapes: blink (closed lids), drowsy (half eyes), yawn.
- Idle face is the game's "neutral" (eyes open). The game's "calm" has closed eyes, which looked lifeless as a resting face.
- Left alone: drowsy at 4.5 min, yawn, asleep, screen off at 5 min. Tap, shake or a hand near the sensor wakes Wisp.
  Dark room: drowsy, then asleep with the screen dimmed; light coming back wakes it.
- Volume button made bigger and brighter (top right); version shows at the bottom for 4 s after start.

## H-007 — Device UI with almost no buttons
- 21 Nov 2026: six interactions added, all without physical buttons:
  1. Grown-up menu: hold two opposite corners for 3 s (or the top-left corner alone for 5 s). Volume, brightness, screen-off
     time (2/5/10 min/never), mood word on/off; version and battery shown. Replaces the on-screen volume button.
  2. Battery icon (top right); low battery (<15%) makes Wisp "hungry" (thought bubble, once a minute); charging shows sparks
     flying into Wisp.
  3. Face-down = bedtime: goodnight sound, sound off, screen off; turning it face-up wakes Wisp. Which way is "up" is learned
     at power-on and whenever the screen is touched, so it works whatever the sensor orientation.
  4. Petting: slow strokes over Wisp give the game's "kind" face (closed happy eyes) with hearts.
  5. Tilt: Wisp looks left, right or up (new look faces from the game's eye shapes; a left-looking copy of the "side" eye was
     added in the export script). Tilt directions are a first guess and may need flipping on the real device.
  6. Quick swipe left/right moves between pages: Wisp, Teach Wisp, Spells (the last two are "coming soon"), dots at the bottom.
