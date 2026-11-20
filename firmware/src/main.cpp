// Wisp Box, milestone 1: Wisp lives on the CoreS3.
// Wisp (The Spark) idles, blinks and looks around, and reacts to:
//   tap -> happy, 3 quick taps -> laugh, shake -> excited (long shake -> dizzy),
//   hand close to the top sensor -> shy, hand a little further -> curious,
//   dark room -> sleepy (wakes when the light comes back).
// Fully offline. Nothing is stored or sent. No camera or microphone used yet.

#include <M5Unified.h>
#include <math.h>

// ---------- palette (from prototypes/wisp-art/spark.svg in the main repo) ----------
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }
static uint16_t BG, BG2, BODY_DARK, BODY, BODY_LIGHT, BELLY, W_GOLD, W_GOLD_DARK, FLAME, INK, WHITE_, BLUSH, STAR;

static M5Canvas canvas(&M5.Display);

// ---------- moods ----------
enum Mood { CALM, HAPPY, LAUGH, EXCITED, DIZZY, SHY, CURIOUS, SLEEPY, MOOD_COUNT };
static const char* MOOD_NAME[MOOD_COUNT] = {"calm", "happy", "laugh", "excited", "dizzy", "shy", "curious", "sleepy"};

static Mood mood = CALM;
static uint32_t moodUntil = 0;     // when a timed mood ends (0 = held by a sensor)
static uint32_t moodStart = 0;

static void setMood(Mood m, uint32_t ms) {
  if (m != mood) moodStart = millis();
  mood = m;
  moodUntil = ms ? millis() + ms : 0;
}

// ---------- sounds (short chirps, no recordings) ----------
static void chirp(Mood m) {
  switch (m) {
    case HAPPY:   M5.Speaker.tone(1400, 60); delay(70); M5.Speaker.tone(1900, 80); break;
    case LAUGH:   for (int i = 0; i < 3; i++) { M5.Speaker.tone(1700 + i * 150, 50); delay(80); } break;
    case EXCITED: M5.Speaker.tone(2200, 120); break;
    case DIZZY:   M5.Speaker.tone(600, 200); break;
    case SHY:     M5.Speaker.tone(900, 60); break;
    case CURIOUS: M5.Speaker.tone(1200, 50); delay(60); M5.Speaker.tone(1500, 50); break;
    case SLEEPY:  M5.Speaker.tone(500, 150); break;
    default: break;
  }
}

// ---------- light + proximity sensor (LTR-553ALS on the internal I2C bus) ----------
static const uint8_t LTR = 0x23;
static const uint32_t I2C_HZ = 400000;
static bool ltrOk = false;
static float psBase = -1;   // adaptive "nothing nearby" level
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
  uint8_t b[4];  // CH1 low, CH1 high, CH0 low, CH0 high (must be read in this order)
  if (!ltrOk || !M5.In_I2C.readRegister(LTR, 0x88, b, 4, I2C_HZ)) return -1;
  return (b[3] << 8) | b[2];
}

// ---------- drawing ----------
static float lookX = 0, lookY = 0, lookTX = 0, lookTY = 0;
static uint32_t nextLook = 0, nextBlink = 0, blinkUntil = 0;
struct Twinkle { int16_t x, y; uint8_t phase; };
static Twinkle stars[18];

static void drawBackground(uint32_t t) {
  for (int y = 0; y < 240; y += 8) {
    uint8_t k = 26 + y / 12;
    canvas.fillRect(0, y, 320, 8, C(k, 16 + y / 24, 51 + y / 10));
  }
  for (auto& s : stars) {
    float a = 0.5f + 0.5f * sinf(t / 400.0f + s.phase);
    if (a > 0.35f) canvas.fillCircle(s.x, s.y, a > 0.85f ? 2 : 1, STAR);
  }
}

static void drawFlame(int cx, int top, float sway) {
  // three-tip flame tuft on Wisp's head
  for (int i = -1; i <= 1; i++) {
    int bx = cx + i * 14;
    int tipX = bx + (int)(sway * 6) + i * 4;
    int tipY = top - (i == 0 ? 34 : 22);
    canvas.fillTriangle(bx - 12, top + 6, bx + 12, top + 6, tipX, tipY, FLAME);
    canvas.fillTriangle(bx - 6, top + 6, bx + 6, top + 6, tipX, tipY + 12, W_GOLD);
  }
}

static void drawEye(int ex, int ey, int r, float open, Mood m, float lx, float ly, uint32_t t) {
  if (m == LAUGH || m == HAPPY) {             // happy arcs ^ ^
    for (int w = 0; w < 4; w++) canvas.drawArc(ex, ey + 6, r - w, r - w - 1, 200, 340, INK);
    return;
  }
  if (m == SLEEPY || open < 0.15f) {          // closed line
    canvas.fillRoundRect(ex - r, ey - 2, r * 2, 5, 2, INK);
    return;
  }
  if (m == DIZZY) {                           // spinning spiral
    float a0 = t / 120.0f;
    for (float a = 0; a < 12.0f; a += 0.25f) {
      float rr = a * r / 12.0f;
      canvas.fillCircle(ex + cosf(a + a0) * rr, ey + sinf(a + a0) * rr, 2, INK);
    }
    return;
  }
  int h = (int)(r * 1.15f * open);
  canvas.fillEllipse(ex, ey, r, h, WHITE_);
  float ps = (m == EXCITED || m == CURIOUS) ? 0.72f : 0.6f;
  int pr = (int)(r * ps);
  int px = ex + (int)(lx * (r - pr));
  int py = ey + (int)(ly * (h - pr > 0 ? h - pr : 0));
  canvas.fillCircle(px, py, pr, INK);
  canvas.fillCircle(px - pr / 3, py - pr / 3, pr / 3 + 1, WHITE_);   // sparkle
  if (m == EXCITED) canvas.fillCircle(px + pr / 3, py + pr / 4, 2, W_GOLD);
  if (open < 0.95f)                                                    // lid for half-open eyes
    canvas.fillRect(ex - r - 1, ey - h - 2, r * 2 + 2, (int)(h * (1.0f - open)) + 2, BODY);
}

static void drawMouth(int mx, int my, Mood m, uint32_t t) {
  switch (m) {
    case LAUGH:
    case EXCITED:
      canvas.fillEllipse(mx, my + 4, 13, 10, INK);
      canvas.fillEllipse(mx, my + 9, 8, 4, C(240, 110, 150));
      break;
    case CURIOUS: canvas.fillCircle(mx, my + 3, 5, INK); break;
    case SHY:     canvas.drawLine(mx - 6, my + 3, mx + 6, my + 3, INK); break;
    case DIZZY:
      for (int x = -12; x < 12; x++) canvas.fillCircle(mx + x, my + 3 + (int)(3 * sinf(x / 2.0f + t / 90.0f)), 1, INK);
      break;
    case SLEEPY:  canvas.fillCircle(mx, my + 3, 4, INK); break;
    default:
      for (int w = 0; w < 3; w++) canvas.drawArc(mx, my - 6, 12 - w, 11 - w, 30, 150, INK);
  }
}

static void drawWisp(uint32_t t) {
  float bobAmp = 5, bobSpeed = 600;
  float shakeX = 0, lean = 0;
  if (mood == HAPPY)   { bobAmp = 10; bobSpeed = 220; }
  if (mood == LAUGH)   { bobAmp = 6;  bobSpeed = 90; shakeX = 3 * sinf(t / 40.0f); }
  if (mood == EXCITED) { bobAmp = 14; bobSpeed = 150; }
  if (mood == DIZZY)   { shakeX = 10 * sinf(t / 200.0f); }
  if (mood == SLEEPY)  { bobAmp = 3;  bobSpeed = 1400; }
  if (mood == SHY)     { lean = 14; }
  if (mood == CURIOUS) { lean = -8; }

  int cx = 160 + (int)shakeX + (int)(lean * 0.6f);
  int cy = 128 + (int)(bobAmp * sinf(t / bobSpeed)) + (int)lean;
  int R = 62;

  // aura
  canvas.fillCircle(cx, cy + 6, R + 16, C(60, 34, 110));
  canvas.fillCircle(cx, cy + 6, R + 8, C(74, 42, 140));
  // flame tuft
  drawFlame(cx, cy - R + 8, sinf(t / 300.0f));
  // body: shaded sphere (light from upper left)
  for (int i = 0; i < 14; i++) {
    float f = i / 13.0f;
    int r = (int)(R * (1.0f - f * 0.82f));
    int ox = (int)(-f * R * 0.28f), oy = (int)(-f * R * 0.32f);
    uint8_t rr = 90 + (uint8_t)(f * 104), gg = 46 + (uint8_t)(f * 125), bb = 214 + (uint8_t)(f * 41);
    canvas.fillCircle(cx + ox, cy + oy, r, C(rr, gg, bb));
  }
  // belly
  canvas.fillEllipse(cx, cy + 30, 34, 22, BELLY);
  // gold chest star
  canvas.fillTriangle(cx, cy + 18, cx - 6, cy + 30, cx + 6, cy + 30, W_GOLD);
  canvas.fillTriangle(cx - 9, cy + 24, cx + 9, cy + 24, cx, cy + 36, W_GOLD);

  // eyes
  float open = 1.0f;
  if (millis() < blinkUntil) open = 0.05f;
  if (mood == SHY) open = min(open, 0.55f);
  float lx = lookX, ly = lookY;
  if (mood == SHY) { lx = -0.6f; ly = 0.8f; }
  if (mood == CURIOUS) { lx = 0; ly = -0.2f; }
  int er = (mood == EXCITED || mood == CURIOUS) ? 17 : 15;
  drawEye(cx - 24, cy - 6, er, open, mood, lx, ly, t);
  drawEye(cx + 24, cy - 6, er, open, mood, lx, ly, t);

  // cheeks
  int blush = (mood == SHY) ? 13 : 9;
  canvas.fillEllipse(cx - 42, cy + 12, blush, blush / 2 + 2, BLUSH);
  canvas.fillEllipse(cx + 42, cy + 12, blush, blush / 2 + 2, BLUSH);

  drawMouth(cx, cy + 14, mood, t);

  // effects
  if (mood == SLEEPY) {
    canvas.setTextColor(STAR);
    for (int i = 0; i < 3; i++) {
      float p = fmodf(t / 900.0f + i / 3.0f, 1.0f);
      canvas.setTextSize(1 + i);
      canvas.setCursor(cx + 50 + (int)(p * 30), cy - 50 - (int)(p * 50));
      canvas.print("z");
    }
  }
  if (mood == EXCITED || mood == LAUGH || mood == HAPPY) {
    for (int i = 0; i < 6; i++) {
      float a = t / 500.0f + i * 1.047f;
      int sx = cx + (int)(cosf(a) * (R + 26)), sy = cy + (int)(sinf(a) * (R + 22));
      canvas.fillCircle(sx, sy, 3, W_GOLD);
      canvas.drawFastHLine(sx - 6, sy, 13, W_GOLD);
      canvas.drawFastVLine(sx, sy - 6, 13, W_GOLD);
    }
  }
}

static void drawLabel() {
  // small mood word for testing; turn off later for kids
  canvas.setTextSize(1);
  canvas.setTextColor(C(190, 170, 240));
  canvas.setTextDatum(bottom_center);
  canvas.drawString(MOOD_NAME[mood], 160, 236);
  canvas.setTextDatum(top_left);
}

// ---------- input handling ----------
static uint32_t tapTimes[3] = {0, 0, 0};
static float shakeEnergy = 0;
static uint32_t darkSince = 0;

static void handleTouch(uint32_t now) {
  auto d = M5.Touch.getDetail();
  if (!d.wasPressed()) return;
  tapTimes[0] = tapTimes[1]; tapTimes[1] = tapTimes[2]; tapTimes[2] = now;
  if (mood == SLEEPY) { setMood(CURIOUS, 1500); chirp(CURIOUS); darkSince = 0; return; }
  if (tapTimes[0] && now - tapTimes[0] < 1200) { setMood(LAUGH, 2500); chirp(LAUGH); tapTimes[0] = tapTimes[1] = 0; }
  else { setMood(HAPPY, 1800); chirp(HAPPY); }
  lookTX = (d.x - 160) / 160.0f; lookTY = (d.y - 120) / 120.0f;
}

static void handleMotion() {
  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
  float g = sqrtf(ax * ax + ay * ay + az * az);
  float jolt = fabsf(g - 1.0f);
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
    if (ps < psBase) psBase = ps; else psBase += 0.02f;     // slowly track "empty" level
    float near = ps - psBase;
    bool timed = moodUntil && now < moodUntil;
    if (near > 400) { if (mood != SHY) { setMood(SHY, 0); chirp(SHY); } }
    else if (near > 80) { if (!timed && mood != CURIOUS) { setMood(CURIOUS, 0); chirp(CURIOUS); } }
    else if ((mood == SHY || mood == CURIOUS) && moodUntil == 0) setMood(CALM, 0);
  }
  if (als >= 0) {
    alsAvg = alsAvg < 0 ? als : alsAvg * 0.9f + als * 0.1f;
    bool dark = alsAvg < 8;
    if (dark) {
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
  M5.Speaker.setVolume(90);

  BG = C(26, 16, 51); BG2 = C(40, 24, 80);
  BODY_DARK = C(90, 46, 214); BODY = C(134, 87, 255); BODY_LIGHT = C(194, 171, 255);
  BELLY = C(240, 232, 255); W_GOLD = C(255, 194, 77); W_GOLD_DARK = C(232, 146, 15);
  FLAME = C(255, 154, 61); INK = C(30, 20, 60); WHITE_ = C(255, 255, 255);
  BLUSH = C(255, 160, 200); STAR = C(255, 237, 181);

  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvas.createSprite(320, 240);

  for (auto& s : stars) { s.x = random(320); s.y = random(150); s.phase = random(255); }
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

  // idle life: look around and blink
  if (now > nextLook) {
    lookTX = random(-100, 101) / 100.0f; lookTY = random(-60, 61) / 100.0f;
    nextLook = now + 1200 + random(2500);
  }
  if (now > nextBlink) { blinkUntil = now + 140; nextBlink = now + 2500 + random(3000); }
  lookX += (lookTX - lookX) * 0.15f;
  lookY += (lookTY - lookY) * 0.15f;

  drawBackground(now);
  drawWisp(now);
  drawLabel();
  canvas.pushSprite(0, 0);
  delay(16);
}
