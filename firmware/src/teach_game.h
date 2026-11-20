// Teach Wisp: Field Mission for Season One Module 1 ("AI learns from examples").
// Included once from main.cpp after the shared globals and drawing helpers.
//
// TEACH: hold a thing in the middle of the camera view and tap a box (Star, Heart, Moon) to give Wisp an example.
// TEST:  Wisp guesses live which box the thing belongs to. The Sure Meter shows how sure it is.
//        Far from everything it has seen -> "I don't know this one".
// Hold a box for 2 s to empty it. Examples live in memory only: never saved, gone when the box turns off.
// Wisp only learns colours, brightness and how busy the picture is (no shapes yet), so kids discover that
// Wisp is easily fooled by colour and light: the Module 1 lesson (biased or narrow examples make AI wrong).

#include "esp_camera.h"

// ---------- camera (GC0308, settings from M5Stack's CoreS3 camera example) ----------
static camera_config_t camCfg = {
  .pin_pwdn = -1, .pin_reset = -1, .pin_xclk = -1,
  .pin_sccb_sda = 12, .pin_sccb_scl = 11,
  .pin_d7 = 47, .pin_d6 = 48, .pin_d5 = 16, .pin_d4 = 15,
  .pin_d3 = 42, .pin_d2 = 41, .pin_d1 = 40, .pin_d0 = 39,
  .pin_vsync = 46, .pin_href = 38, .pin_pclk = 45,
  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
  .pixel_format = PIXFORMAT_RGB565, .frame_size = FRAMESIZE_QQVGA,  // 160x120
  .jpeg_quality = 0, .fb_count = 2,
  .fb_location = CAMERA_FB_IN_PSRAM, .grab_mode = CAMERA_GRAB_LATEST,
  .sccb_i2c_port = -1,
};
enum CamState { CAM_OFF, CAM_OK, CAM_FAIL };
static CamState camState = CAM_OFF;
static uint16_t* camImg = nullptr;  // last frame, 160x120, big-endian RGB565 as the camera sends it
static const int CAM_W = 160, CAM_H = 120;

static bool camBegin() {
  if (camState != CAM_OFF) return camState == CAM_OK;
  M5.In_I2C.release();  // the camera driver takes the internal I2C bus for its setup
  esp_err_t err = esp_camera_init(&camCfg);
  Serial.printf("camera init: %s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
  camState = err == ESP_OK ? CAM_OK : CAM_FAIL;
  if (camState == CAM_OK) {
    camImg = (uint16_t*)heap_caps_malloc(CAM_W * CAM_H * 2, MALLOC_CAP_SPIRAM);
    if (!camImg) camState = CAM_FAIL;
  }
  return camState == CAM_OK;
}

static bool camGrab() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;
  bool ok = fb->width == CAM_W && fb->height == CAM_H && fb->len >= (size_t)CAM_W * CAM_H * 2;
  if (ok) memcpy(camImg, fb->buf, CAM_W * CAM_H * 2);
  esp_camera_fb_return(fb);
  return ok;
}

// ---------- features: what Wisp "notices" ----------
// 12 colour bins (hue, for colourful pixels) + 3 grey bins (dark/mid/light, for dull pixels) = colour histogram,
// plus overall brightness and "busyness" (edges). Taken from the middle of the picture, where the child holds the thing.
static const int NF = 17;
struct Feat { float v[NF]; };

static Feat extractFeatures() {
  Feat f; memset(&f, 0, sizeof(f));
  float bright = 0, edges = 0; int n = 0;
  const int x0 = 32, x1 = 128, y0 = 12, y1 = 108;  // centre 96x96
  for (int y = y0; y < y1; y += 2) {
    for (int x = x0; x < x1; x += 2) {
      uint16_t p = camImg[y * CAM_W + x]; p = (p >> 8) | (p << 8);  // big-endian -> native
      float r = ((p >> 11) & 31) / 31.0f, g = ((p >> 5) & 63) / 63.0f, b = (p & 31) / 31.0f;
      float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn;
      float sat = mx > 0 ? d / mx : 0;
      if (sat > 0.28f && mx > 0.18f) {
        float h;
        if (mx == r) h = fmodf((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        if (h < 0) h += 6.0f;
        int bin = (int)(h * 2.0f) % 12;  // 12 bins of 30 degrees
        f.v[bin] += 1;
      } else {
        f.v[12 + (mx < 0.3f ? 0 : mx < 0.7f ? 1 : 2)] += 1;
      }
      bright += mx;
      uint16_t q = camImg[y * CAM_W + x + 2]; q = (q >> 8) | (q << 8);
      uint16_t s = camImg[(y + 2) * CAM_W + x]; s = (s >> 8) | (s << 8);
      float l0 = ((p >> 5) & 63) / 63.0f, l1 = ((q >> 5) & 63) / 63.0f, l2 = ((s >> 5) & 63) / 63.0f;
      edges += fabsf(l0 - l1) + fabsf(l0 - l2);
      n++;
    }
  }
  for (int i = 0; i < 15; i++) f.v[i] /= n;
  f.v[15] = bright / n;
  f.v[16] = min(1.0f, edges / n * 4.0f);
  return f;
}

static float featDist(const Feat& a, const Feat& b) {
  float d = 0;
  for (int i = 0; i < 15; i++) d += fabsf(a.v[i] - b.v[i]);  // colour histogram (0..2)
  d += 0.8f * fabsf(a.v[15] - b.v[15]);                     // brightness
  d += 0.8f * fabsf(a.v[16] - b.v[16]);                     // busyness
  return d;
}

// ---------- the three boxes ----------
static const int NBOX = 3, MAX_EX = 8;
static const char* BOX_NAME[NBOX] = {"Star", "Heart", "Moon"};
static Feat examples[NBOX][MAX_EX];
static int exCount[NBOX] = {0, 0, 0};

static int boxesWithExamples() { int k = 0; for (int i = 0; i < NBOX; i++) if (exCount[i]) k++; return k; }

// nearest example per box -> guess + how sure
static const float UNKNOWN_DIST = 0.9f;
static int guessBox = -1;
static float sure = 0, sureShown = 0;
static bool unknownThing = false;

static void classify(const Feat& f) {
  float best[NBOX];
  for (int i = 0; i < NBOX; i++) {
    best[i] = 99;
    for (int j = 0; j < exCount[i]; j++) best[i] = min(best[i], featDist(f, examples[i][j]));
  }
  int b = -1; float d1 = 99, d2 = 99;
  for (int i = 0; i < NBOX; i++) {
    if (best[i] < d1) { d2 = d1; d1 = best[i]; b = i; }
    else if (best[i] < d2) d2 = best[i];
  }
  unknownThing = d1 > UNKNOWN_DIST;
  guessBox = unknownThing ? -1 : b;
  float margin = d2 >= 99 ? 1.0f : constrain((d2 - d1) / (d2 + 0.05f) * 1.6f, 0.0f, 1.0f);
  float close = constrain(1.0f - d1 / UNKNOWN_DIST, 0.0f, 1.0f);
  sure = unknownThing ? 0 : constrain(margin * (0.4f + 0.6f * close), 0.0f, 1.0f);
}

// ---------- game state ----------
enum TeachMode { MODE_TEACH, MODE_TEST };
static TeachMode tmode = MODE_TEACH;
static uint32_t flashUntil = 0, sayUntil = 0, nextGrab = 0;
static int flashBox = -1;
static char sayText[40] = "Show me something!";
static int pressBox = -1;
static uint32_t pressStart = 0;
static bool havePic = false;

static void say(const char* t, uint32_t ms) { strncpy(sayText, t, sizeof(sayText) - 1); sayUntil = millis() + ms; }

static void teachEnter() {
  if (!camBegin()) return;
  if (boxesWithExamples() == 0) { tmode = MODE_TEACH; say("Show me something!", 0); }
}

// ---------- layout ----------
static const int TILE_Y = 140, TILE_H = 72, TILE_W = 96;
static int tileX(int i) { return 10 + i * (TILE_W + 8); }
static const int MODE_X = 178, MODE_Y = 6, MODE_W = 134, MODE_H = 24;

static void drawStar(int cx, int cy, int r, uint16_t c) {
  float ro = r, ri = r * 0.45f;
  for (int i = 0; i < 5; i++) {
    float a0 = -PI / 2 + i * 2 * PI / 5, a1 = a0 + PI / 5, am = a0 - PI / 5;
    int ox = cx + cosf(a0) * ro, oy = cy + sinf(a0) * ro;
    canvas.fillTriangle(cx, cy, ox, oy, cx + cosf(a1) * ri, cy + sinf(a1) * ri, c);
    canvas.fillTriangle(cx, cy, ox, oy, cx + cosf(am) * ri, cy + sinf(am) * ri, c);
  }
}
static void drawBoxIcon(int i, int cx, int cy, int r, uint16_t bg) {
  if (i == 0) drawStar(cx, cy, r, W_GOLD);
  else if (i == 1) heart(cx, cy - r / 3, r * 3 / 4, HEART);
  else { canvas.fillCircle(cx, cy, r * 3 / 4, C(150, 200, 255)); canvas.fillCircle(cx + r / 3, cy - r / 4, r * 3 / 5, bg); }
}

static void drawTeach(uint32_t now) {
  if (camState == CAM_FAIL) {
    canvas.setTextDatum(middle_center); canvas.setTextColor(UI); canvas.setTextSize(2);
    canvas.drawString("Wisp can't see", 160, 100);
    canvas.setTextSize(1); canvas.setTextColor(SOFT);
    canvas.drawString("The camera didn't start. Try turning the box off and on.", 160, 130);
    canvas.setTextDatum(top_left);
    return;
  }
  // grab and think (~8 times a second)
  if (camState == CAM_OK && now > nextGrab) {
    nextGrab = now + 120;
    if (camGrab()) {
      havePic = true;
      if (tmode == MODE_TEST && boxesWithExamples() >= 2) classify(extractFeatures());
    }
  }
  sureShown += (sure - sureShown) * 0.25f;

  // camera view
  canvas.fillRoundRect(4, 4, CAM_W + 8, CAM_H + 8, 8, flashUntil > now ? UI : BTN);
  if (havePic) canvas.pushImage(8, 8, CAM_W, CAM_H, (const lgfx::swap565_t*)camImg);
  else { canvas.setTextColor(SOFT); canvas.setTextDatum(middle_center); canvas.drawString("Opening Wisp's eyes...", 88, 68); canvas.setTextDatum(top_left); }
  canvas.drawRect(8 + 32, 8 + 12, 96, 96, flashUntil > now ? W_GOLD : C(255, 255, 255));  // "hold it here" square

  // mode switch
  bool canTest = boxesWithExamples() >= 2;
  canvas.fillRoundRect(MODE_X, MODE_Y, MODE_W, MODE_H, 12, C(44, 28, 90));
  int half = MODE_W / 2;
  canvas.fillRoundRect(MODE_X + (tmode == MODE_TEACH ? 0 : half), MODE_Y, half, MODE_H, 12, tmode == MODE_TEACH ? W_GOLD : BTN);
  canvas.setTextDatum(middle_center); canvas.setTextSize(1);
  canvas.setTextColor(tmode == MODE_TEACH ? C(44, 26, 92) : SOFT); canvas.drawString("TEACH", MODE_X + half / 2, MODE_Y + 12);
  canvas.setTextColor(tmode == MODE_TEST ? UI : (canTest ? SOFT : C(110, 90, 150))); canvas.drawString("TEST", MODE_X + half + half / 2, MODE_Y + 12);

  // small Wisp
  const uint32_t* face = WISPS_NEUTRAL;
  if (now < flashUntil + 600) face = WISPS_HAPPY;
  else if (tmode == MODE_TEST && canTest) {
    if (unknownThing) face = WISPS_LOOK_U;
    else if (sureShown > 0.7f) face = WISPS_EXCITED;
    else if (sureShown > 0.35f) face = WISPS_HAPPY;
    else face = WISPS_CURIOUS;
  }
  if (now < blinkUntil && face == WISPS_NEUTRAL) face = WISPS_B_NEUTRAL;
  int wx = 236 - WISPS_W / 2, wy = 33 + (int)(2 * sinf(now / 500.0f));
  canvas.pushAlphaImage(wx, wy, WISPS_W, WISPS_H, (const lgfx::argb8888_t*)face);

  // what Wisp says / Sure Meter
  canvas.setTextColor(UI); canvas.setTextDatum(middle_center);
  if (tmode == MODE_TEST && canTest) {
    if (unknownThing) canvas.drawString("I don't know this one!", 240, 133);
    else if (guessBox >= 0) {
      char t[32]; snprintf(t, sizeof(t), sureShown > 0.35f ? "I think: %s" : "Maybe %s?", BOX_NAME[guessBox]);
      canvas.drawString(t, 240, 133);
    }
    // Sure Meter: vertical bar right of Wisp, fills from the bottom
    int mx = 296, my = 38, mh = 78;
    canvas.fillRoundRect(mx, my, 11, mh, 5, C(44, 28, 90));
    int fh = (int)(mh * sureShown); if (fh > 4) canvas.fillRoundRect(mx, my + mh - fh, 11, fh, 5, W_GOLD);
    canvas.setTextColor(SOFT); canvas.drawString("sure", mx + 6, 123);
  } else {
    const char* t = sayText;
    if (now > sayUntil && sayUntil) t = boxesWithExamples() < 2 ? "Teach me 2 boxes!" : "Ready! Tap TEST";
    canvas.drawString(t, 240, 133);
  }
  canvas.setTextDatum(top_left);

  // the three boxes
  for (int i = 0; i < NBOX; i++) {
    int x = tileX(i);
    bool glow = (tmode == MODE_TEST && canTest && guessBox == i && !unknownThing);
    bool flash = (flashBox == i && now < flashUntil + 300);
    uint16_t bg = glow ? C(120, 84, 220) : C(60, 38, 120);
    canvas.fillRoundRect(x, TILE_Y, TILE_W, TILE_H, 12, bg);
    if (glow || flash) for (int k = 0; k < 3; k++) canvas.drawRoundRect(x - k, TILE_Y - k, TILE_W + 2 * k, TILE_H + 2 * k, 12 + k, W_GOLD);
    drawBoxIcon(i, x + TILE_W / 2, TILE_Y + 26, 18, bg);
    for (int j = 0; j < MAX_EX; j++) {  // one dot per example
      int dx = x + 13 + j * 10, dy = TILE_Y + 58;
      if (j < exCount[i]) canvas.fillCircle(dx, dy, 3, UI); else canvas.drawCircle(dx, dy, 3, C(110, 90, 160));
    }
    if (pressBox == i && now - pressStart > 400) {  // emptying ring
      float p = min(1.0f, (now - pressStart - 400) / 1600.0f);
      canvas.fillArc(x + TILE_W / 2, TILE_Y + 26, 24, 20, 270, 270 + (int)(360 * p), C(255, 110, 130));
    }
  }
}

static void teachSound(int kind) {
  if (volLevel == 0 || bedtime) return;
  if (kind == 0) { M5.Speaker.tone(1600, 50); delay(60); M5.Speaker.tone(2100, 70); }  // got it
  else if (kind == 1) { M5.Speaker.tone(700, 80); delay(90); M5.Speaker.tone(500, 120); }  // forgot
  else M5.Speaker.tone(1200, 40);  // switch
}

static void teachTouch(const m5::touch_detail_t& d, uint32_t now) {
  if (camState != CAM_OK) return;
  int hit = -1;
  for (int i = 0; i < NBOX; i++) if (d.x >= tileX(i) && d.x < tileX(i) + TILE_W && d.y >= TILE_Y && d.y < TILE_Y + TILE_H) hit = i;

  if (d.wasPressed()) { pressBox = hit; pressStart = now; }
  if (d.isPressed() && pressBox >= 0 && pressBox == hit && now - pressStart > 2000) {  // hold 2 s: empty the box
    exCount[pressBox] = 0;
    char t[40]; snprintf(t, sizeof(t), "I forgot %s!", BOX_NAME[pressBox]); say(t, 2500);
    teachSound(1);
    if (boxesWithExamples() < 2) tmode = MODE_TEACH;
    pressBox = -1;
    return;
  }
  if (d.wasReleased()) {
    bool shortPress = now - pressStart < 400;
    int b = pressBox; pressBox = -1;
    if (!shortPress) return;
    if (d.y < MODE_Y + MODE_H + 6 && d.x >= MODE_X) {  // mode switch
      if (boxesWithExamples() >= 2) { tmode = tmode == MODE_TEACH ? MODE_TEST : MODE_TEACH; sure = sureShown = 0; teachSound(2); }
      else { say("Teach me 2 boxes first!", 2000); teachSound(1); }
      return;
    }
    if (b >= 0 && b == hit) {
      if (tmode == MODE_TEST) { tmode = MODE_TEACH; teachSound(2); }
      if (!havePic) return;
      Feat f = extractFeatures();
      if (exCount[b] < MAX_EX) examples[b][exCount[b]++] = f;
      else { memmove(&examples[b][0], &examples[b][1], sizeof(Feat) * (MAX_EX - 1)); examples[b][MAX_EX - 1] = f; }
      flashBox = b; flashUntil = now + 250;
      char t[40]; snprintf(t, sizeof(t), "Got it! %s x%d", BOX_NAME[b], exCount[b]); say(t, 1800);
      teachSound(0);
    }
  }
}
