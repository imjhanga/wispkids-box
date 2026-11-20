// Wisp Box, milestone 1: Wisp lives on the CoreS3.
// Frames are rendered by the GAME's own Wisp art engine (tools/export_device_art.js), so the box matches the app.
//
// Child interactions
//   tap -> happy, 3 quick taps -> laugh, stroke Wisp slowly -> loved (eyes closed, hearts)
//   shake -> excited, long shake -> dizzy, tilt the box -> Wisp looks that way
//   hand near the top sensor -> curious, very close -> shy
//   dark room -> drowsy then asleep (screen dimmed); light wakes it
//   turn face-down -> "goodnight", sound off, screen off; turn back up to wake
//   left alone -> drowsy, yawn, asleep, screen off (time set in the grown-up menu)
//   quick swipe left/right -> other pages: Teach Wisp (game, teach_game.h), Spells (coming soon)
//   battery: icon top-right; low battery -> Wisp is hungry; plugged in -> Wisp eats sparks
// Grown-up menu: hold two opposite corners for 3 s (or hold the top-left corner alone for 5 s).
//   Volume, brightness, screen-off time, mood word on/off; version and battery shown. Settings are remembered.
// Fully offline. Nothing about the child is stored or sent. No camera or microphone used yet.

#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "wisp_sprites.h"

#ifndef WISP_VERSION
#define WISP_VERSION "dev"
#endif

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }
static uint16_t W_GOLD, STAR, AURA1, AURA2, UI, BTN, SOFT, HEART;

static M5Canvas canvas(&M5.Display);
static Preferences prefs;

// ---------- settings (grown-up menu) ----------
static const uint8_t VOLUMES[4] = {0, 40, 90, 160};
static const char* VOL_NAME[4] = {"Off", "Low", "Medium", "High"};
static const uint8_t BRIGHTS[3] = {60, 130, 210};
static const char* BRIGHT_NAME[3] = {"Low", "Medium", "High"};
static const uint32_t OFF_MS[4] = {120000, 300000, 600000, 0};  // 0 = never
static const char* OFF_NAME[4] = {"2 min", "5 min", "10 min", "Never"};
static uint8_t volLevel = 1, brightLevel = 1, offLevel = 1;
static bool showLabel = true;
static bool flipped = false;
static uint8_t baseRotation = 1;

static void applyVolume() { M5.Speaker.setVolume(VOLUMES[volLevel]); }
static uint8_t brightNormal() { return BRIGHTS[brightLevel]; }
static uint8_t brightSleepy() { return BRIGHTS[brightLevel] / 4 + 10; }
static void saveSettings() {
  prefs.putUChar("vol", volLevel); prefs.putUChar("bri", brightLevel);
  prefs.putUChar("off", offLevel); prefs.putBool("lbl", showLabel); prefs.putBool("flip", flipped);
}

// ---------- moods ----------
enum Mood { CALM, HAPPY, LAUGH, EXCITED, DIZZY, SHY, CURIOUS, DROWSY, YAWN, SLEEPY, LOVED, HUNGRY, MOOD_COUNT };
static const char* MOOD_NAME[MOOD_COUNT] = {"calm", "happy", "laugh", "excited", "dizzy", "shy", "curious",
                                            "drowsy", "yawn", "sleepy", "loved", "hungry"};
static const uint32_t* MOOD_ART[MOOD_COUNT] = {WISP_NEUTRAL, WISP_HAPPY, WISP_LAUGH, WISP_EXCITED, WISP_SILLY,
                                               WISP_SHY, WISP_CURIOUS, WISP_DROWSY, WISP_YAWN, WISP_SLEEPY,
                                               WISP_KIND, WISP_DROWSY};
static const uint32_t* BLINK_ART[MOOD_COUNT] = {WISP_B_NEUTRAL, WISP_B_HAPPY, nullptr, nullptr, nullptr,
                                                WISP_B_SHY, WISP_B_CURIOUS, nullptr, nullptr, nullptr,
                                                nullptr, nullptr};

static Mood mood = CALM;
static uint32_t moodUntil = 0;  // when a timed mood ends (0 = held by a sensor or a sequence)
static void setMood(Mood m, uint32_t ms) { mood = m; moodUntil = ms ? millis() + ms : 0; }

// ---------- state ----------
enum Page { HOME, TEACH, SPELLS, PAGE_COUNT };
static Page page = HOME;
static bool menuOpen = false;
static uint32_t lastActive = 0, bootAt = 0;
static bool screenOff = false, darkRoom = false, bedtime = false;
static uint32_t nextBlink = 0, blinkUntil = 0;
static float tiltX = 0, tiltY = 0;  // smoothed, -1..1
static int batt = -1;
static bool charging = false, wasCharging = false;

// ---------- sounds ----------
static void chirp(Mood m) {
  if (volLevel == 0 || bedtime) return;
  switch (m) {
    case HAPPY:   M5.Speaker.tone(1400, 60); delay(70); M5.Speaker.tone(1900, 80); break;
    case LAUGH:   for (int i = 0; i < 3; i++) { M5.Speaker.tone(1700 + i * 150, 50); delay(80); } break;
    case EXCITED: M5.Speaker.tone(2200, 100); break;
    case DIZZY:   M5.Speaker.tone(600, 180); break;
    case SHY:     M5.Speaker.tone(900, 50); break;
    case CURIOUS: M5.Speaker.tone(1200, 50); delay(60); M5.Speaker.tone(1500, 50); break;
    case YAWN:    M5.Speaker.tone(700, 120); delay(130); M5.Speaker.tone(450, 220); break;
    case SLEEPY:  M5.Speaker.tone(500, 140); break;
    case LOVED:   M5.Speaker.tone(1100, 90); delay(100); M5.Speaker.tone(1300, 140); break;
    case HUNGRY:  M5.Speaker.tone(400, 120); delay(140); M5.Speaker.tone(350, 160); break;
    default: break;
  }
}
static void goodnightSound() {
  if (volLevel == 0) return;
  M5.Speaker.tone(1300, 120); delay(140); M5.Speaker.tone(1000, 120); delay(140); M5.Speaker.tone(700, 260);
}

// ---------- light + proximity sensor (LTR-553ALS on the internal I2C bus) ----------
static const uint8_t LTR = 0x23;
static const uint32_t I2C_HZ = 400000;
static bool ltrOk = false;
static float psBase = -1, alsAvg = -1;

static void ltrBegin() {
  ltrOk = M5.In_I2C.writeRegister8(LTR, 0x80, 0x01, I2C_HZ) && M5.In_I2C.writeRegister8(LTR, 0x81, 0x03, I2C_HZ);
}
static int ltrProx() {
  uint8_t b[2];
  if (!ltrOk || !M5.In_I2C.readRegister(LTR, 0x8D, b, 2, I2C_HZ)) return -1;
  return ((b[1] & 0x07) << 8) | b[0];
}
static int ltrLight() {
  uint8_t b[4];  // CH1 low, CH1 high, CH0 low, CH0 high (read in this order)
  if (!ltrOk || !M5.In_I2C.readRegister(LTR, 0x88, b, 4, I2C_HZ)) return -1;
  return (b[3] << 8) | b[2];
}

// ---------- screen power ----------
static void screenOn() {
  if (screenOff) { screenOff = false; M5.Display.wakeup(); }
  M5.Display.setBrightness(brightNormal());
}
static void screenDown() {
  if (!screenOff) { screenOff = true; M5.Display.setBrightness(0); M5.Display.sleep(); }
}
static void wake(uint32_t now) {
  lastActive = now;
  if (bedtime) return;  // only turning the box face-up ends bedtime
  if (screenOff) {
    screenOn();
    setMood(CURIOUS, 1500); chirp(CURIOUS);
  } else if (mood == DROWSY || mood == YAWN || (mood == SLEEPY && !darkRoom)) {
    M5.Display.setBrightness(brightNormal());
    setMood(CURIOUS, 1500); chirp(CURIOUS);
  }
}

// ---------- drawing helpers ----------
struct Twinkle { int16_t x, y; uint8_t phase; };
static Twinkle stars[20];

static void drawBackground(uint32_t t, bool dim) {
  for (int y = 0; y < 240; y += 8) {
    uint8_t r = 26 + y / 12, g = 16 + y / 24, b = 51 + y / 10;
    if (dim) { r /= 2; g /= 2; b /= 2; }
    canvas.fillRect(0, y, 320, 8, C(r, g, b));
  }
  for (auto& s : stars) {
    float a = 0.5f + 0.5f * sinf(t / 400.0f + s.phase);
    if (a > 0.35f) canvas.fillCircle(s.x, s.y, a > 0.85f ? 2 : 1, STAR);
  }
}
static void sparkle(int x, int y, int r) {
  canvas.fillCircle(x, y, r / 2, W_GOLD);
  canvas.drawFastHLine(x - r, y, 2 * r + 1, W_GOLD);
  canvas.drawFastVLine(x, y - r, 2 * r + 1, W_GOLD);
}
static void heart(int x, int y, int r, uint16_t c) {
  canvas.fillCircle(x - r / 2, y, r / 2 + 1, c);
  canvas.fillCircle(x + r / 2, y, r / 2 + 1, c);
  canvas.fillTriangle(x - r - 1, y + 1, x + r + 1, y + 1, x, y + r + 2, c);
}
static void bolt(int x, int y, uint16_t c) {
  canvas.fillTriangle(x + 2, y - 8, x - 4, y + 1, x + 1, y + 1, c);
  canvas.fillTriangle(x - 1, y - 1, x + 4, y - 1, x - 2, y + 8, c);
}

static void drawBattery() {
  if (batt < 0) return;
  int x = 280, y = 8;
  uint16_t col = batt < 15 && !charging ? C(255, 110, 130) : UI;
  canvas.drawRoundRect(x, y, 30, 15, 3, col);
  canvas.fillRect(x + 30, y + 4, 3, 7, col);
  int w = (26 * (batt > 100 ? 100 : batt)) / 100;
  if (w > 0) canvas.fillRect(x + 2, y + 2, w, 11, col);
  if (charging) bolt(x + 15, y + 7, W_GOLD);
}

static void drawDots() {
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = 160 + (i - 1) * 18;
    if (i == page) canvas.fillCircle(x, 228, 5, UI);
    else canvas.drawCircle(x, 228, 4, SOFT);
  }
}

static void drawLabel(uint32_t now) {
  canvas.setTextSize(1);
  canvas.setTextColor(SOFT);
  canvas.setTextDatum(top_left);
  if (now - bootAt < 4000) canvas.drawString("Wisp Box v" WISP_VERSION, 8, 8);
  else if (showLabel && page == HOME) canvas.drawString(MOOD_NAME[mood], 8, 8);
}

// ---------- home page: Wisp ----------
static void drawWisp(uint32_t t) {
  float bobAmp = 5, bobSpeed = 600, shakeX = 0, lean = 0;
  if (mood == HAPPY)   { bobAmp = 10; bobSpeed = 220; }
  if (mood == LAUGH)   { bobAmp = 6;  bobSpeed = 90; shakeX = 3 * sinf(t / 40.0f); }
  if (mood == EXCITED) { bobAmp = 12; bobSpeed = 150; }
  if (mood == DIZZY)   { shakeX = 12 * sinf(t / 200.0f); }
  if (mood == DROWSY || mood == HUNGRY) { bobAmp = 3; bobSpeed = 1000; lean = 3; }
  if (mood == YAWN)    { bobAmp = 2;  bobSpeed = 1000; lean = -3; }
  if (mood == SLEEPY)  { bobAmp = 2;  bobSpeed = 1400; lean = 6; }
  if (mood == SHY)     { lean = 10; }
  if (mood == LOVED)   { bobAmp = 4;  bobSpeed = 400; }

  int cx = 160 + (int)shakeX + (int)(tiltX * 14);
  int cy = 122 + (int)(bobAmp * sinf(t / bobSpeed)) + (int)lean + (int)(tiltY * 8);

  if (mood != SLEEPY) {
    canvas.fillEllipse(cx, cy + 8, WISP_W / 2 + 6, WISP_H / 2 - 4, AURA1);
    canvas.fillEllipse(cx, cy + 8, WISP_W / 2 - 6, WISP_H / 2 - 14, AURA2);
  }
  const uint32_t* art = MOOD_ART[mood];
  if (mood == CALM) {  // tilt: Wisp looks the way the box is tipped
    if (tiltX > 0.35f) art = WISP_LOOK_R;
    else if (tiltX < -0.35f) art = WISP_LOOK_L;
    else if (tiltY < -0.35f) art = WISP_LOOK_U;
  }
  bool looking = (art == WISP_LOOK_R || art == WISP_LOOK_L);
  if (t < blinkUntil && (BLINK_ART[mood] || looking)) art = looking ? WISP_B_NEUTRAL : BLINK_ART[mood];
  canvas.pushAlphaImage(cx - WISP_W / 2, cy - WISP_H / 2, WISP_W, WISP_H, (const lgfx::argb8888_t*)art);

  if (mood == EXCITED || mood == LAUGH || mood == HAPPY) {
    for (int i = 0; i < 6; i++) {
      float a = t / 500.0f + i * 1.047f;
      sparkle(cx + (int)(cosf(a) * (WISP_W / 2 + 14)), cy + (int)(sinf(a) * (WISP_H / 2 + 6)), 5);
    }
  }
  if (mood == LOVED) {
    for (int i = 0; i < 4; i++) {
      float p = fmodf(t / 1200.0f + i / 4.0f, 1.0f);
      heart(cx - 70 + i * 46, cy - 40 - (int)(p * 60), 6, HEART);
    }
  }
  if (mood == SLEEPY) {
    canvas.setTextColor(STAR);
    for (int i = 0; i < 3; i++) {
      float p = fmodf(t / 900.0f + i / 3.0f, 1.0f);
      canvas.setTextSize(1 + i);
      canvas.setCursor(cx + 70 + (int)(p * 30), cy - 60 - (int)(p * 40));
      canvas.print("z");
    }
  }
  if (mood == HUNGRY) {  // thought bubble with an empty battery
    int bx = cx + 78, by = cy - 70;
    canvas.fillCircle(cx + 52, cy - 36, 4, UI);
    canvas.fillCircle(cx + 62, cy - 50, 6, UI);
    canvas.fillRoundRect(bx - 24, by - 16, 48, 30, 14, UI);
    canvas.drawRoundRect(bx - 12, by - 6, 22, 11, 2, C(200, 60, 90));
    canvas.fillRect(bx + 10, by - 3, 2, 5, C(200, 60, 90));
    canvas.fillRect(bx - 10, by - 4, 4, 7, C(200, 60, 90));
  }
  if (charging) {  // sparks fly into Wisp
    for (int i = 0; i < 5; i++) {
      float p = fmodf(t / 900.0f + i / 5.0f, 1.0f);
      float a = i * 1.256f;
      int r = (int)((1.0f - p) * 140);
      sparkle(cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r * 0.8f), 4);
    }
  }
}

// ---------- other pages (placeholders) ----------
static void drawLock(int x, int y) {
  canvas.drawArc(x, y - 6, 8, 6, 180, 360, SOFT);
  canvas.fillRoundRect(x - 10, y - 6, 20, 16, 3, SOFT);
}
static void drawComingSoon(uint32_t t, const char* title, bool camera) {
  int cx = 160, cy = 100 + (int)(4 * sinf(t / 600.0f));
  canvas.fillCircle(cx, cy, 52, AURA2);
  if (camera) {
    canvas.fillRoundRect(cx - 32, cy - 18, 64, 42, 8, UI);
    canvas.fillRect(cx - 12, cy - 26, 24, 10, UI);
    canvas.fillCircle(cx, cy + 3, 14, BTN);
    canvas.fillCircle(cx, cy + 3, 7, UI);
  } else {  // wand
    canvas.drawWideLine(cx - 28, cy + 28, cx + 14, cy - 14, 6, UI);
    sparkle(cx + 20, cy - 20, 12);
    sparkle(cx - 18, cy - 26, 6);
    sparkle(cx + 30, cy + 10, 5);
  }
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(UI);
  canvas.setTextSize(2);
  canvas.drawString(title, 160, 176);
  canvas.setTextSize(1);
  canvas.setTextColor(SOFT);
  canvas.drawString("Coming soon", 160, 200);
  canvas.setTextDatum(top_left);
  drawLock(cx + 44, cy - 40);
}

#include "teach_game.h"

// ---------- grown-up menu ----------
static const int MENU_ROWS = 6;
static void drawMenu() {
  canvas.fillScreen(C(20, 12, 40));
  canvas.setTextDatum(top_left);
  canvas.setTextColor(UI);
  canvas.setTextSize(2);
  canvas.drawString("Grown-up settings", 14, 8);
  const char* names[MENU_ROWS] = {"Volume", "Brightness", "Screen off", "Mood word", "Turn screen", "Done"};
  const char* vals[MENU_ROWS] = {VOL_NAME[volLevel], BRIGHT_NAME[brightLevel], OFF_NAME[offLevel],
                                 showLabel ? "On" : "Off", flipped ? "Upside down" : "Normal", ""};
  for (int i = 0; i < MENU_ROWS; i++) {
    int y = 30 + i * 32;
    bool done = (i == MENU_ROWS - 1);
    canvas.fillRoundRect(10, y, 300, 28, 8, done ? W_GOLD : BTN);
    canvas.setTextColor(done ? C(44, 26, 92) : UI);
    canvas.setTextDatum(middle_left);
    canvas.drawString(names[i], 22, y + 14);
    canvas.setTextDatum(middle_right);
    canvas.drawString(vals[i], 298, y + 14);
  }
  canvas.setTextDatum(bottom_center);
  canvas.setTextSize(1);
  canvas.setTextColor(SOFT);
  char foot[64];
  if (batt >= 0) snprintf(foot, sizeof(foot), "Wisp Box v%s  |  battery %d%%%s", WISP_VERSION, batt, charging ? " (charging)" : "");
  else snprintf(foot, sizeof(foot), "Wisp Box v%s", WISP_VERSION);
  canvas.drawString(foot, 160, 238);
  canvas.setTextDatum(top_left);
}

static void menuTap(int x, int y) {
  int row = (y - 30) / 32;
  if (y < 30 || row < 0 || row >= MENU_ROWS) return;
  switch (row) {
    case 0: volLevel = (volLevel + 1) % 4; applyVolume(); if (volLevel) M5.Speaker.tone(1500, 60); break;
    case 1: brightLevel = (brightLevel + 1) % 3; M5.Display.setBrightness(brightNormal()); break;
    case 2: offLevel = (offLevel + 1) % 4; break;
    case 3: showLabel = !showLabel; break;
    case 4: flipped = !flipped; M5.Display.setRotation(flipped ? (baseRotation + 2) % 4 : baseRotation); break;
    case 5: menuOpen = false; saveSettings(); setMood(HAPPY, 1200); break;
  }
}

// corner hold to open the menu: two opposite corners for 3 s, or the top-left corner alone for 5 s
static uint32_t holdStart = 0;
static float holdProgress = 0;
static bool inCorner(int x, int y, int cx, int cy) { return abs(x - cx) < 70 && abs(y - cy) < 70; }

static void checkMenuGesture(uint32_t now) {
  int n = M5.Touch.getCount();
  bool two = false, one = false;
  if (n >= 2) {
    auto a = M5.Touch.getDetail(0), b = M5.Touch.getDetail(1);
    if (a.isPressed() && b.isPressed()) {
      two = (inCorner(a.x, a.y, 0, 0) && inCorner(b.x, b.y, 320, 240)) ||
            (inCorner(b.x, b.y, 0, 0) && inCorner(a.x, a.y, 320, 240)) ||
            (inCorner(a.x, a.y, 320, 0) && inCorner(b.x, b.y, 0, 240)) ||
            (inCorner(b.x, b.y, 320, 0) && inCorner(a.x, a.y, 0, 240));
    }
  } else if (n == 1) {
    auto a = M5.Touch.getDetail(0);
    one = a.isPressed() && a.x < 50 && a.y < 50;
  }
  uint32_t need = two ? 3000 : 5000;
  if (two || one) {
    if (!holdStart) holdStart = now;
    holdProgress = (now - holdStart) / (float)need;
    if (holdProgress >= 1.0f) { menuOpen = true; holdStart = 0; holdProgress = 0; M5.Speaker.tone(1800, 80); }
  } else { holdStart = 0; holdProgress = 0; }
}
static void drawHoldRing() {
  if (holdProgress < 0.15f) return;
  canvas.fillCircle(160, 120, 34, C(20, 12, 40));
  canvas.fillArc(160, 120, 30, 24, 270, 270 + (int)(360 * holdProgress), W_GOLD);
}

// ---------- input ----------
static uint32_t tapTimes[3] = {0, 0, 0};
static float shakeEnergy = 0, petDistance = 0;
static uint32_t darkSince = 0, faceDownSince = 0, faceUpSince = 0;
static float faceUpSign = 1.0f;  // learned: which way "screen up" reads on the z axis
static bool upKnown = false;     // no bedtime until we know which way is up

static void handleTouch(uint32_t now) {
  auto d = M5.Touch.getDetail();
  if (d.isPressed()) {  // learn which way is "face up" (a child only touches the screen when it faces them)
    float ax, ay, az;
    if (M5.Imu.getAccel(&ax, &ay, &az) && fabsf(az) > 0.6f) { faceUpSign = az > 0 ? 1.0f : -1.0f; upKnown = true; }
  }
  if (menuOpen) { if (d.wasClicked()) menuTap(d.x, d.y); return; }
  if (holdProgress > 0) return;  // holding for the menu

  if (d.wasPressed()) {
    bool wasOff = screenOff;
    bool wasSleepy = (mood == SLEEPY || mood == DROWSY || mood == YAWN || mood == HUNGRY);
    wake(now);
    petDistance = 0;
    if (wasOff || wasSleepy) { tapTimes[2] = 0; return; }
  }
  if (screenOff) return;

  if (d.wasFlicked() && abs(d.distanceX()) > 50 && abs(d.distanceX()) > abs(d.distanceY())) {  // swipe pages
    int p = (int)page + (d.distanceX() < 0 ? 1 : -1);
    if (p >= 0 && p < PAGE_COUNT) {
      page = (Page)p;
      if (volLevel) M5.Speaker.tone(1000 + p * 200, 40);
      if (page == TEACH) teachEnter();
    }
    return;
  }
  if (page == TEACH) { teachTouch(d, now); return; }
  if (page != HOME) return;

  if (d.isDragging()) {  // slow strokes over Wisp = petting
    bool overWisp = abs(d.x - 160) < WISP_W / 2 && abs(d.y - 122) < WISP_H / 2;
    if (overWisp) petDistance += abs(d.deltaX()) + abs(d.deltaY());
    if (petDistance > 140) {
      if (mood != LOVED) chirp(LOVED);
      setMood(LOVED, 2500);
      petDistance = 0;
    }
    return;
  }
  if (d.wasClicked()) {
    tapTimes[0] = tapTimes[1]; tapTimes[1] = tapTimes[2]; tapTimes[2] = now;
    if (tapTimes[0] && now - tapTimes[0] < 1400) { setMood(LAUGH, 2500); chirp(LAUGH); tapTimes[0] = tapTimes[1] = 0; }
    else { setMood(HAPPY, 1800); chirp(HAPPY); }
  }
}

static void handleMotion(uint32_t now) {
  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

  // face-down = bedtime
  bool faceDown = upKnown && az * faceUpSign < -0.8f;
  if (faceDown) {
    faceUpSince = 0;
    if (!faceDownSince) faceDownSince = now;
    if (!bedtime && now - faceDownSince > 1500) {
      bedtime = true;
      goodnightSound();
      screenDown();
    }
    return;
  }
  faceDownSince = 0;
  if (bedtime) {
    if (!faceUpSince) faceUpSince = now;
    if (now - faceUpSince > 400) {
      bedtime = false;
      lastActive = now;
      screenOn();
      setMood(CURIOUS, 1500); chirp(CURIOUS);
    }
    return;
  }

  // tilt (only when held upright-ish, not lying flat)
  float tx = 0, ty = 0;
  if (fabsf(az) < 0.9f) { tx = constrain(ax * 1.6f, -1.0f, 1.0f); ty = constrain(ay * 1.6f, -1.0f, 1.0f); }
  if (flipped) { tx = -tx; ty = -ty; }
  tiltX += (tx - tiltX) * 0.1f;
  tiltY += (ty - tiltY) * 0.1f;

  // shake
  float jolt = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  shakeEnergy = shakeEnergy * 0.9f + (jolt > 0.35f ? jolt : 0);
  if (shakeEnergy > 1.5f) {
    bool wasOff = screenOff;
    wake(now);
    if (wasOff) return;
  }
  if (menuOpen || page != HOME) return;
  if (shakeEnergy > 6.0f) {
    if (mood != DIZZY) { setMood(DIZZY, 3000); chirp(DIZZY); }
    else moodUntil = now + 3000;
  } else if (shakeEnergy > 1.5f && mood != DIZZY && mood != EXCITED && mood != CURIOUS) {
    setMood(EXCITED, 2000); chirp(EXCITED);
  }
}

static void handleLight(uint32_t now) {
  if (bedtime) return;  // face-down on a table would look "near" and "dark"
  int ps = ltrProx(), als = ltrLight();
  if (ps >= 0) {
    if (psBase < 0) psBase = ps;
    if (ps < psBase) psBase = ps; else psBase += 0.02f;  // slowly track the "empty" level
    float near = ps - psBase;
    if (near > 80) {
      bool wasOff = screenOff;
      wake(now);
      if (wasOff) return;
    }
    if (!menuOpen && page == HOME) {
      bool timed = moodUntil && now < moodUntil;
      if (near > 400) { if (mood != SHY) { setMood(SHY, 0); chirp(SHY); } }
      else if (near > 80) { if (!timed && mood != CURIOUS) { setMood(CURIOUS, 0); chirp(CURIOUS); } }
      else if ((mood == SHY || mood == CURIOUS) && moodUntil == 0) setMood(CALM, 0);
    }
  }
  if (als >= 0) {
    alsAvg = alsAvg < 0 ? als : alsAvg * 0.9f + als * 0.1f;
    if (alsAvg < 8) {
      if (!darkSince) darkSince = now;
      uint32_t dark = now - darkSince;
      if (dark > 1500 && dark < 4000 && mood != DROWSY && mood != SLEEPY && !screenOff) setMood(DROWSY, 0);
      if (dark > 4000 && !darkRoom) {
        darkRoom = true;
        if (!screenOff) { setMood(SLEEPY, 0); chirp(SLEEPY); M5.Display.setBrightness(brightSleepy()); }
      }
    } else {
      darkSince = 0;
      if (darkRoom) { darkRoom = false; wake(now); }
      else if (mood == DROWSY && moodUntil == 0 && now - lastActive < 10000) setMood(CALM, 0);
    }
  }
}

static void handleBattery(uint32_t now) {
  static uint32_t next = 0, nextHungry = 0;
  if (now < next) return;
  next = now + 2000;
  batt = M5.Power.getBatteryLevel();
  charging = M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
  if (charging && !wasCharging && !screenOff && !bedtime) { setMood(HAPPY, 1500); chirp(HAPPY); }
  wasCharging = charging;
  if (batt >= 0 && batt < 15 && !charging && mood == CALM && now > nextHungry && !menuOpen) {
    setMood(HUNGRY, 3000); chirp(HUNGRY);
    nextHungry = now + 60000;  // remind once a minute
  }
}

static void handleIdle(uint32_t now) {
  uint32_t offMs = OFF_MS[offLevel];
  if (offMs == 0 || menuOpen || bedtime) return;
  uint32_t idle = now - lastActive;
  if (idle > offMs) { screenDown(); return; }
  if (darkRoom || page != HOME) return;
  if (idle > offMs - 10000) {
    if (mood != SLEEPY) { setMood(SLEEPY, 0); chirp(SLEEPY); M5.Display.setBrightness(brightSleepy()); }
  } else if (idle > offMs - 15000) {
    if (mood != YAWN) { setMood(YAWN, 0); chirp(YAWN); }
  } else if (idle > offMs - 30000) {
    if (mood != DROWSY && !(moodUntil && now < moodUntil)) setMood(DROWSY, 0);
  }
}

// ---------- main ----------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  // Pick a landscape rotation (320x240) whatever this board's default is, then apply the grown-up "flip" setting.
  prefs.begin("wispbox", false);
  for (int i = 0; i < 4 && M5.Display.width() < M5.Display.height(); i++) M5.Display.setRotation((M5.Display.getRotation() + 1) % 4);
  baseRotation = M5.Display.getRotation();
  flipped = prefs.getBool("flip", false);
  M5.Display.setRotation(flipped ? (baseRotation + 2) % 4 : baseRotation);
  Serial.printf("Wisp Box %s display %dx%d rotation %d\n", WISP_VERSION, M5.Display.width(), M5.Display.height(), M5.Display.getRotation());

  volLevel = prefs.getUChar("vol", 1); if (volLevel > 3) volLevel = 1;
  brightLevel = prefs.getUChar("bri", 1); if (brightLevel > 2) brightLevel = 1;
  offLevel = prefs.getUChar("off", 1); if (offLevel > 3) offLevel = 1;
  showLabel = prefs.getBool("lbl", true);
  applyVolume();
  M5.Display.setBrightness(brightNormal());

  W_GOLD = C(255, 194, 77); STAR = C(255, 237, 181); AURA1 = C(52, 30, 100); AURA2 = C(66, 38, 126);
  UI = C(255, 255, 255); BTN = C(110, 72, 210); SOFT = C(190, 170, 240); HEART = C(255, 120, 170);

  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvas.createSprite(320, 240);

  for (auto& s : stars) { s.x = random(320); s.y = random(240); s.phase = random(255); }
  ltrBegin();
  for (int i = 0; i < 20 && !upKnown; i++) {  // at power-on the screen faces the person (or lies face-up)
    float ax, ay, az;
    M5.update();
    if (M5.Imu.getAccel(&ax, &ay, &az) && fabsf(az) > 0.6f) { faceUpSign = az > 0 ? 1.0f : -1.0f; upKnown = true; }
    delay(20);
  }
  bootAt = lastActive = millis();
  setMood(HAPPY, 1500);
  chirp(HAPPY);
}

void loop() {
  M5.update();
  uint32_t now = millis();

  if (!menuOpen && !screenOff) checkMenuGesture(now);
  handleTouch(now);
  handleMotion(now);
  static uint32_t nextSense = 0;
  if (now > nextSense) { handleLight(now); nextSense = now + 100; }
  handleBattery(now);
  handleIdle(now);

  if (screenOff) { delay(30); return; }  // keep checking sensors so Wisp can wake up

  if (menuOpen) { drawMenu(); canvas.pushSprite(0, 0); delay(16); return; }

  if (moodUntil && now > moodUntil) setMood(CALM, 0);
  if (now > nextBlink) { blinkUntil = now + 130; nextBlink = now + 2500 + random(3000); }

  drawBackground(now, page == HOME && mood == SLEEPY);
  if (page == HOME) drawWisp(now);
  else if (page == TEACH) drawTeach(now);
  else drawComingSoon(now, "Spells", false);
  if (page != TEACH) drawBattery();
  drawDots();
  drawLabel(now);
  drawHoldRing();
  canvas.pushSprite(0, 0);
  delay(16);
}
