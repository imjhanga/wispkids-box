// Wisp Box, milestone 1: Wisp lives on the CoreS3.
// Frames are rendered by the GAME's own Wisp art engine (tools/export_device_art.js), so the box matches the app.
// Wisp floats, blinks and reacts to:
//   tap -> happy, 3 quick taps -> laugh, shake -> excited (long shake -> silly/dizzy),
//   hand close to the top sensor -> shy, hand a little further -> curious,
//   dark room -> drowsy, then asleep (wakes when the light comes back or on a tap).
// Left alone for 5 minutes: drowsy, yawns, falls asleep, then the screen turns off.
// A tap, a shake or a hand near the sensor wakes it.
// Speaker button (top right) cycles the volume: off, low, medium, high. The setting is remembered.
// Fully offline. Nothing about the child is stored or sent. No camera or microphone used yet.

#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "wisp_sprites.h"

#ifndef WISP_VERSION
#define WISP_VERSION "dev"
#endif

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }
static uint16_t W_GOLD, STAR, AURA1, AURA2, UI, BTN;

static M5Canvas canvas(&M5.Display);
static Preferences prefs;

// ---------- timing ----------
static const uint32_t IDLE_DROWSY_MS = 270000;  // 4.5 min: getting drowsy
static const uint32_t IDLE_YAWN_MS   = 285000;  // yawn
static const uint32_t IDLE_SLEEP_MS  = 290000;  // asleep
static const uint32_t IDLE_OFF_MS    = 300000;  // 5 min: screen off
static const uint8_t  BRIGHT_NORMAL = 140, BRIGHT_SLEEPY = 40;

// ---------- moods ----------
enum Mood { CALM, HAPPY, LAUGH, EXCITED, DIZZY, SHY, CURIOUS, DROWSY, YAWN, SLEEPY, MOOD_COUNT };
static const char* MOOD_NAME[MOOD_COUNT] = {"calm", "happy", "laugh", "excited", "dizzy", "shy",
                                            "curious", "drowsy", "yawn", "sleepy"};
static const uint32_t* MOOD_ART[MOOD_COUNT] = {WISP_NEUTRAL, WISP_HAPPY, WISP_LAUGH, WISP_EXCITED, WISP_SILLY,
                                               WISP_SHY, WISP_CURIOUS, WISP_DROWSY, WISP_YAWN, WISP_SLEEPY};
// closed-eye frame used for blinking (nullptr = this face doesn't blink)
static const uint32_t* BLINK_ART[MOOD_COUNT] = {WISP_B_NEUTRAL, WISP_B_HAPPY, nullptr, nullptr, nullptr,
                                                WISP_B_SHY, WISP_B_CURIOUS, nullptr, nullptr, nullptr};

static Mood mood = CALM;
static uint32_t moodUntil = 0;  // when a timed mood ends (0 = held by a sensor or the sleep sequence)

static void setMood(Mood m, uint32_t ms) {
  mood = m;
  moodUntil = ms ? millis() + ms : 0;
}

// ---------- volume ----------
static const uint8_t VOLUMES[4] = {0, 40, 90, 160};  // off, low, medium, high
static uint8_t volLevel = 1;                         // default: low

static void applyVolume() { M5.Speaker.setVolume(VOLUMES[volLevel]); }

static void chirp(Mood m) {
  if (volLevel == 0) return;
  switch (m) {
    case HAPPY:   M5.Speaker.tone(1400, 60); delay(70); M5.Speaker.tone(1900, 80); break;
    case LAUGH:   for (int i = 0; i < 3; i++) { M5.Speaker.tone(1700 + i * 150, 50); delay(80); } break;
    case EXCITED: M5.Speaker.tone(2200, 100); break;
    case DIZZY:   M5.Speaker.tone(600, 180); break;
    case SHY:     M5.Speaker.tone(900, 50); break;
    case CURIOUS: M5.Speaker.tone(1200, 50); delay(60); M5.Speaker.tone(1500, 50); break;
    case YAWN:    M5.Speaker.tone(700, 120); delay(130); M5.Speaker.tone(450, 220); break;
    case SLEEPY:  M5.Speaker.tone(500, 140); break;
    default: break;
  }
}

// ---------- light + proximity sensor (LTR-553ALS on the internal I2C bus) ----------
static const uint8_t LTR = 0x23;
static const uint32_t I2C_HZ = 400000;
static bool ltrOk = false;
static float psBase = -1;  // adaptive "nothing nearby" level
static float alsAvg = -1;

static void ltrBegin() {
  ltrOk = M5.In_I2C.writeRegister8(LTR, 0x80, 0x01, I2C_HZ)    // ALS active
       && M5.In_I2C.writeRegister8(LTR, 0x81, 0x03, I2C_HZ);   // PS active
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

// ---------- state ----------
static uint32_t lastActive = 0;   // last time the child did something
static bool screenOff = false;
static bool darkRoom = false;
static uint32_t nextBlink = 0, blinkUntil = 0;
static uint32_t bootAt = 0;

static void wake(uint32_t now) {
  lastActive = now;
  if (screenOff) {
    screenOff = false;
    M5.Display.wakeup();
    M5.Display.setBrightness(BRIGHT_NORMAL);
    setMood(CURIOUS, 1500);
    chirp(CURIOUS);
  } else if (mood == DROWSY || mood == YAWN || (mood == SLEEPY && !darkRoom)) {
    M5.Display.setBrightness(BRIGHT_NORMAL);
    setMood(CURIOUS, 1500);
    chirp(CURIOUS);
  }
}

// ---------- drawing ----------
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

static void drawWisp(uint32_t t) {
  float bobAmp = 5, bobSpeed = 600, shakeX = 0, lean = 0;
  if (mood == HAPPY)   { bobAmp = 10; bobSpeed = 220; }
  if (mood == LAUGH)   { bobAmp = 6;  bobSpeed = 90; shakeX = 3 * sinf(t / 40.0f); }
  if (mood == EXCITED) { bobAmp = 12; bobSpeed = 150; }
  if (mood == DIZZY)   { shakeX = 12 * sinf(t / 200.0f); }
  if (mood == DROWSY)  { bobAmp = 3;  bobSpeed = 1000; lean = 3; }
  if (mood == YAWN)    { bobAmp = 2;  bobSpeed = 1000; lean = -3; }
  if (mood == SLEEPY)  { bobAmp = 2;  bobSpeed = 1400; lean = 6; }
  if (mood == SHY)     { lean = 10; }

  int cx = 160 + (int)shakeX;
  int cy = 124 + (int)(bobAmp * sinf(t / bobSpeed)) + (int)lean;

  if (mood != SLEEPY) {  // soft aura behind Wisp
    canvas.fillEllipse(cx, cy + 8, WISP_W / 2 + 6, WISP_H / 2 - 4, AURA1);
    canvas.fillEllipse(cx, cy + 8, WISP_W / 2 - 6, WISP_H / 2 - 14, AURA2);
  }
  const uint32_t* art = MOOD_ART[mood];
  if (BLINK_ART[mood] && t < blinkUntil) art = BLINK_ART[mood];
  canvas.pushAlphaImage(cx - WISP_W / 2, cy - WISP_H / 2, WISP_W, WISP_H, (const lgfx::argb8888_t*)art);

  if (mood == EXCITED || mood == LAUGH || mood == HAPPY) {
    for (int i = 0; i < 6; i++) {
      float a = t / 500.0f + i * 1.047f;
      sparkle(cx + (int)(cosf(a) * (WISP_W / 2 + 14)), cy + (int)(sinf(a) * (WISP_H / 2 + 6)), 5);
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
}

static void drawVolumeButton() {
  // big, high-contrast button in the top-right corner
  const int bx = 262, by = 6, bw = 52, bh = 42;
  canvas.fillRoundRect(bx, by, bw, bh, 12, BTN);
  canvas.drawRoundRect(bx, by, bw, bh, 12, UI);
  int x = bx + 10, y = by + 21;  // speaker centre-left
  canvas.fillRect(x, y - 6, 7, 12, UI);
  canvas.fillTriangle(x + 5, y, x + 16, y - 12, x + 16, y + 12, UI);
  if (volLevel == 0) {
    uint16_t red = C(255, 110, 130);
    for (int k = -1; k <= 1; k++) {
      canvas.drawLine(x + 22 + k, y - 7, x + 34 + k, y + 7, red);
      canvas.drawLine(x + 34 + k, y - 7, x + 22 + k, y + 7, red);
    }
  } else {
    for (int i = 0; i < volLevel; i++) canvas.fillArc(x + 16, y, 7 + i * 6, 5 + i * 6, 310, 50, UI);
  }
}

static void drawLabel(uint32_t now) {
  // small mood word for testing (version for the first 4 s); remove before kids' trials if it distracts
  canvas.setTextSize(1);
  canvas.setTextColor(C(190, 170, 240));
  canvas.setTextDatum(bottom_center);
  if (now - bootAt < 4000) canvas.drawString("Wisp Box v" WISP_VERSION, 160, 236);
  else canvas.drawString(MOOD_NAME[mood], 160, 236);
  canvas.setTextDatum(top_left);
}

// ---------- input ----------
static uint32_t tapTimes[3] = {0, 0, 0};
static float shakeEnergy = 0;
static uint32_t darkSince = 0;

static void handleTouch(uint32_t now) {
  auto d = M5.Touch.getDetail();
  if (!d.wasPressed()) return;
  bool wasOff = screenOff;
  bool wasSleepy = (mood == SLEEPY || mood == DROWSY || mood == YAWN);
  wake(now);
  if (wasOff) return;  // first tap only wakes Wisp
  if (d.x > 250 && d.y < 60) {  // volume button (generous touch area)
    volLevel = (volLevel + 1) % 4;
    applyVolume();
    prefs.putUChar("vol", volLevel);
    if (volLevel) M5.Speaker.tone(1500, 60);
    return;
  }
  if (wasSleepy) { darkSince = 0; return; }
  tapTimes[0] = tapTimes[1]; tapTimes[1] = tapTimes[2]; tapTimes[2] = now;
  if (tapTimes[0] && now - tapTimes[0] < 1200) { setMood(LAUGH, 2500); chirp(LAUGH); tapTimes[0] = tapTimes[1] = 0; }
  else { setMood(HAPPY, 1800); chirp(HAPPY); }
}

static void handleMotion(uint32_t now) {
  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  float jolt = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  shakeEnergy = shakeEnergy * 0.9f + (jolt > 0.35f ? jolt : 0);
  if (shakeEnergy > 1.5f) {
    bool wasOff = screenOff;
    wake(now);
    if (wasOff) return;
  }
  if (shakeEnergy > 6.0f) {
    if (mood != DIZZY) { setMood(DIZZY, 3000); chirp(DIZZY); }
    else moodUntil = now + 3000;
  } else if (shakeEnergy > 1.5f && mood != DIZZY && mood != EXCITED && mood != CURIOUS) {
    setMood(EXCITED, 2000); chirp(EXCITED);
  }
}

static void handleLight(uint32_t now) {
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
    bool timed = moodUntil && now < moodUntil;
    if (near > 400) { if (mood != SHY) { setMood(SHY, 0); chirp(SHY); } }
    else if (near > 80) { if (!timed && mood != CURIOUS) { setMood(CURIOUS, 0); chirp(CURIOUS); } }
    else if ((mood == SHY || mood == CURIOUS) && moodUntil == 0) setMood(CALM, 0);
  }
  if (als >= 0) {
    alsAvg = alsAvg < 0 ? als : alsAvg * 0.9f + als * 0.1f;
    if (alsAvg < 8) {
      if (!darkSince) darkSince = now;
      uint32_t dark = now - darkSince;
      if (dark > 1500 && dark < 4000 && mood != DROWSY && mood != SLEEPY && !screenOff) setMood(DROWSY, 0);
      if (dark > 4000 && !darkRoom) {
        darkRoom = true;
        if (!screenOff) { setMood(SLEEPY, 0); chirp(SLEEPY); M5.Display.setBrightness(BRIGHT_SLEEPY); }
      }
    } else {
      darkSince = 0;
      if (darkRoom) { darkRoom = false; wake(now); }
      else if (mood == DROWSY && now - lastActive < IDLE_DROWSY_MS) setMood(CALM, 0);
    }
  }
}

static void handleIdle(uint32_t now) {
  uint32_t idle = now - lastActive;
  if (idle > IDLE_OFF_MS) {
    if (!screenOff) { screenOff = true; M5.Display.setBrightness(0); M5.Display.sleep(); }
    return;
  }
  if (darkRoom) return;
  if (idle > IDLE_SLEEP_MS) {
    if (mood != SLEEPY) { setMood(SLEEPY, 0); chirp(SLEEPY); M5.Display.setBrightness(BRIGHT_SLEEPY); }
  } else if (idle > IDLE_YAWN_MS) {
    if (mood != YAWN) { setMood(YAWN, 0); chirp(YAWN); }
  } else if (idle > IDLE_DROWSY_MS) {
    if (mood != DROWSY && !(moodUntil && now < moodUntil)) setMood(DROWSY, 0);
  }
}

// ---------- main ----------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(BRIGHT_NORMAL);

  prefs.begin("wispbox", false);
  volLevel = prefs.getUChar("vol", 1);
  if (volLevel > 3) volLevel = 1;
  applyVolume();

  W_GOLD = C(255, 194, 77); STAR = C(255, 237, 181);
  AURA1 = C(52, 30, 100); AURA2 = C(66, 38, 126); UI = C(255, 255, 255); BTN = C(110, 72, 210);

  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvas.createSprite(320, 240);

  for (auto& s : stars) { s.x = random(320); s.y = random(240); s.phase = random(255); }
  ltrBegin();
  bootAt = lastActive = millis();
  setMood(HAPPY, 1500);
  chirp(HAPPY);
}

void loop() {
  M5.update();
  uint32_t now = millis();

  handleTouch(now);
  handleMotion(now);
  static uint32_t nextSense = 0;
  if (now > nextSense) { handleLight(now); nextSense = now + 100; }
  handleIdle(now);

  if (screenOff) { delay(30); return; }  // keep checking sensors so Wisp can wake up

  if (moodUntil && now > moodUntil) setMood(CALM, 0);
  if (now > nextBlink) { blinkUntil = now + 130; nextBlink = now + 2500 + random(3000); }

  drawBackground(now, mood == SLEEPY);
  drawWisp(now);
  drawVolumeButton();
  drawLabel(now);
  canvas.pushSprite(0, 0);
  delay(16);
}
