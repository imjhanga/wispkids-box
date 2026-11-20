// Wisp Box, milestone 1: Wisp lives on the CoreS3.
// Uses the exact Spark Wisp art from the game (prototypes/content-producer/assets/wisp/wisp_s1_*.png,
// converted by tools/make_sprites.py). Wisp floats and reacts to:
//   tap -> happy, 3 quick taps -> laugh, shake -> excited (long shake -> silly/dizzy),
//   hand close to the top sensor -> shy, hand a little further -> curious,
//   dark room -> sleepy (wakes when the light comes back).
// Speaker button (top right) cycles the volume: off, low, medium, high. The setting is remembered.
// Fully offline. Nothing about the child is stored or sent. No camera or microphone used yet.

#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "wisp_sprites.h"

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }
static uint16_t W_GOLD, STAR, AURA1, AURA2, UI;

static M5Canvas canvas(&M5.Display);
static Preferences prefs;

// ---------- moods ----------
enum Mood { CALM, HAPPY, LAUGH, EXCITED, DIZZY, SHY, CURIOUS, SLEEPY, MOOD_COUNT };
static const char* MOOD_NAME[MOOD_COUNT] = {"calm", "happy", "laugh", "excited", "dizzy", "shy", "curious", "sleepy"};
static const uint32_t* MOOD_ART[MOOD_COUNT] = {WISP_CALM, WISP_HAPPY, WISP_LAUGH, WISP_EXCITED,
                                               WISP_SILLY, WISP_SHY, WISP_CURIOUS, WISP_CALM};

static Mood mood = CALM;
static uint32_t moodUntil = 0;  // when a timed mood ends (0 = held by a sensor)

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
  if (mood == SLEEPY)  { bobAmp = 2;  bobSpeed = 1400; lean = 6; }
  if (mood == SHY)     { lean = 10; }

  int cx = 160 + (int)shakeX;
  int cy = 124 + (int)(bobAmp * sinf(t / bobSpeed)) + (int)lean;

  if (mood != SLEEPY) {  // soft aura behind Wisp
    canvas.fillEllipse(cx, cy + 8, WISP_W / 2 + 6, WISP_H / 2 - 4, AURA1);
    canvas.fillEllipse(cx, cy + 8, WISP_W / 2 - 6, WISP_H / 2 - 14, AURA2);
  }
  canvas.pushAlphaImage(cx - WISP_W / 2, cy - WISP_H / 2, WISP_W, WISP_H,
                        (const lgfx::argb8888_t*)MOOD_ART[mood]);

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
  int x = 284, y = 10;
  canvas.fillRoundRect(x - 4, y - 4, 34, 30, 8, C(44, 28, 90));
  canvas.fillRect(x + 2, y + 7, 5, 8, UI);
  canvas.fillTriangle(x + 6, y + 11, x + 13, y + 3, x + 13, y + 19, UI);
  if (volLevel == 0) {
    canvas.drawLine(x + 16, y + 6, x + 24, y + 16, C(255, 120, 140));
    canvas.drawLine(x + 24, y + 6, x + 16, y + 16, C(255, 120, 140));
  } else {
    for (int i = 0; i < volLevel; i++) canvas.drawArc(x + 13, y + 11, 5 + i * 4, 4 + i * 4, 300, 60, UI);
  }
}

static void drawLabel() {
  // small mood word for testing; remove before kids' trials if it distracts
  canvas.setTextSize(1);
  canvas.setTextColor(C(190, 170, 240));
  canvas.setTextDatum(bottom_center);
  canvas.drawString(MOOD_NAME[mood], 160, 236);
  canvas.setTextDatum(top_left);
}

// ---------- input ----------
static uint32_t tapTimes[3] = {0, 0, 0};
static float shakeEnergy = 0;
static uint32_t darkSince = 0;

static void handleTouch(uint32_t now) {
  auto d = M5.Touch.getDetail();
  if (!d.wasPressed()) return;
  if (d.x > 270 && d.y < 50) {  // volume button
    volLevel = (volLevel + 1) % 4;
    applyVolume();
    prefs.putUChar("vol", volLevel);
    if (volLevel) M5.Speaker.tone(1500, 60);
    return;
  }
  tapTimes[0] = tapTimes[1]; tapTimes[1] = tapTimes[2]; tapTimes[2] = now;
  if (mood == SLEEPY) { setMood(CURIOUS, 1500); chirp(CURIOUS); darkSince = 0; return; }
  if (tapTimes[0] && now - tapTimes[0] < 1200) { setMood(LAUGH, 2500); chirp(LAUGH); tapTimes[0] = tapTimes[1] = 0; }
  else { setMood(HAPPY, 1800); chirp(HAPPY); }
}

static void handleMotion() {
  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  float jolt = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  shakeEnergy = shakeEnergy * 0.9f + (jolt > 0.35f ? jolt : 0);
  if (shakeEnergy > 6.0f) {
    if (mood != DIZZY) { setMood(DIZZY, 3000); chirp(DIZZY); }
    else moodUntil = millis() + 3000;
  } else if (shakeEnergy > 1.5f && mood != DIZZY && mood != EXCITED) {
    setMood(EXCITED, 2000); chirp(EXCITED);
  }
}

static void handleLight(uint32_t now) {
  int ps = ltrProx(), als = ltrLight();
  if (ps >= 0) {
    if (psBase < 0) psBase = ps;
    if (ps < psBase) psBase = ps; else psBase += 0.02f;  // slowly track the "empty" level
    float near = ps - psBase;
    bool timed = moodUntil && now < moodUntil;
    if (near > 400) { if (mood != SHY) { setMood(SHY, 0); chirp(SHY); } }
    else if (near > 80) { if (!timed && mood != CURIOUS) { setMood(CURIOUS, 0); chirp(CURIOUS); } }
    else if ((mood == SHY || mood == CURIOUS) && moodUntil == 0) setMood(CALM, 0);
  }
  if (als >= 0) {
    alsAvg = alsAvg < 0 ? als : alsAvg * 0.9f + als * 0.1f;
    if (alsAvg < 8) {
      if (!darkSince) darkSince = now;
      if (now - darkSince > 3000 && mood != SLEEPY) { setMood(SLEEPY, 0); chirp(SLEEPY); }
    } else {
      darkSince = 0;
      if (mood == SLEEPY) { setMood(CURIOUS, 1500); chirp(CURIOUS); }
    }
  }
}

// ---------- main ----------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  prefs.begin("wispbox", false);
  volLevel = prefs.getUChar("vol", 1);
  if (volLevel > 3) volLevel = 1;
  applyVolume();

  W_GOLD = C(255, 194, 77); STAR = C(255, 237, 181);
  AURA1 = C(52, 30, 100); AURA2 = C(66, 38, 126); UI = C(235, 225, 255);

  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvas.createSprite(320, 240);

  for (auto& s : stars) { s.x = random(320); s.y = random(240); s.phase = random(255); }
  ltrBegin();
  setMood(HAPPY, 1500);
  chirp(HAPPY);
}

void loop() {
  M5.update();
  uint32_t now = millis();

  handleTouch(now);
  handleMotion();
  static uint32_t nextSense = 0;
  if (now > nextSense) { handleLight(now); nextSense = now + 100; }
  if (moodUntil && now > moodUntil) setMood(CALM, 0);

  drawBackground(now, mood == SLEEPY);
  drawWisp(now);
  drawVolumeButton();
  drawLabel();
  canvas.pushSprite(0, 0);
  delay(16);
}
