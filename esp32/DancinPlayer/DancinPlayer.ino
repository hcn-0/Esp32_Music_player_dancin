/*
 * Dancin OLED Disco Player  -  ESP32 Firmware (v5 - Clean, Cool & Borderless)
 * Board : ESP32 DevKit V1        Display: SSD1306 128x64 I2C (SDA=21, SCL=22)
 * Libs  : Adafruit SSD1306, Adafruit GFX, Wire
 *
 * Updates in v5:
 * - NO DIVIDER LINE: Clean, borderless, modern aesthetic.
 * - Clock-locked sync with PC: 0ms drift over the entire 3:18 song.
 * - Perfectly aligned side-by-side spacing: generous margins, zero crowding.
 * - Contextual character switches matching the real lyrics:
 *     * Romantic couple for "Every time when I look in your eyes", "smile with pride", "happy that you're mine"
 *     * Chibi shrug for "All the time", "don't know why", "can't you see"
 *     * Beat-dancing boy for "Get up on the floor", "Dancin' is what to do"
 * - Kinetic Typography for punchy drop hooks ("DANCIN'")
 * - Sleek audio soundwave at the bottom edge.
 * - Floating anime particles (notes, stars, hearts) with smooth sine sway.
 */
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "dancin_art.h"

// ------------------------------- Hardware ---------------------------------
#define OLED_SDA     21
#define OLED_SCL     22
#define OLED_ADDR    0x3C          // change to 0x3D if needed
#define SCREEN_W     128
#define SCREEN_H     64
#define STATUS_LED   2
#define INVERT_DISPLAY true        // Invert OLED display color (true: lit paper background with dark art/text)

// ------------------------------- Protocol ---------------------------------
#define SERIAL_BAUD        115200
#define PROTOCOL_VERSION   2
#define RX_BUF_SIZE        512
#define MAX_FIELDS         6
#define LINE_MAX           104
#define LINK_TIMEOUT_MS    3500
#define MAX_WORDS          24

static const uint32_t FRAME_MS = 30;     // ~33 fps render loop

// Latin-1 (U+00C0..U+00FF) -> ASCII fold table
static const char LATIN1_FOLD[] =
  "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
  "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
static_assert(sizeof(LATIN1_FOLD) == 65, "LATIN1_FOLD must have 64 entries");

// ------------------------------- Globals ----------------------------------
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1, 400000UL, 400000UL);
static bool oledOk = false;

// Serial receive buffer
static char     rxBuf[RX_BUF_SIZE];
static uint16_t rxLen = 0;
static bool     rxOverflow = false;

// Link & player state
static uint8_t  playState = 0;
static uint32_t lastValidRxAt = 0, lastEAt = 0, ledOffAt = 0;
static bool     everLinked = false, wasAlive = false;

// Clock-locked song & lyric time tracking (0ms cumulative drift)
static uint32_t currentSongPosMs = 0;
static uint32_t lineStartSongPosMs = 0;
static uint32_t lineDurationMs = 0;
static float    lineElapsed = 0.0f;

// Lyric parsing & layout
struct Word { uint8_t off, len, row, col; uint16_t at; };
static char     lineText[LINE_MAX];
static Word     words[MAX_WORDS];
static uint8_t  nWords = 0, nRows = 0, txtSize = 1, rowW[6], shownWords = 0;
static uint16_t lyricCount = 0;

// Visual modes
enum VisualMode {
  MODE_SIDE_ART_LEFT = 0,   // Art on Left (0..58), Lyrics on Right (62..127)
  MODE_SIDE_ART_RIGHT,      // Lyrics on Left (0..62), Art on Right (66..127)
  MODE_KINETIC_CENTER,      // Full-screen massive bold typography + audio bars
  MODE_FULL_ART             // Full 128x64 anime romance scene (instrumental breaks)
};
static VisualMode currentMode = MODE_SIDE_ART_LEFT;

// Character art types
enum CharacterArt {
  CHAR_DANCE = 0,           // Beat-switching dancing anime boy
  CHAR_SHRUG,               // Shrugging chibi boy
  CHAR_COUPLE               // Sweet romantic couple
};
static CharacterArt currentChar = CHAR_DANCE;

// Contextual visual themes
enum PropTheme {
  THEME_NONE = 0,
  THEME_MOON_STARS,         // Crescent moon and stars
  THEME_HEARTS,             // Floating anime hearts
  THEME_DANCE_BEATS         // Jump equalizer & audio bars
};
static PropTheme currentTheme = THEME_NONE;

// Audio energy, beat & physics
static float    lvl = 0.15f, lvlTarget = 0.15f;
static float    pulse = 0.0f;               // 1.0 on beat, decays to 0
static float    bounceY = 0.0f;             // vertical bounce offset
static float    swayX = 0.0f;               // horizontal groove sway
static float    phase = 0.0f;               // continuous smooth sine wave
static uint8_t  dancePose = 0;              // 0 = pose 1, 1 = pose 2
static uint16_t beatCount = 0;
static uint32_t nextFakeBeat = 0, lastRenderAt = 0;

// Floating anime particles
struct Particle { float x, y, vy, sway; uint8_t glyph; bool active; };
static const uint8_t MAX_PARTICLES = 6;
static Particle particles[MAX_PARTICLES];

// ------------------------------- Helpers ----------------------------------
static void copyStr(char* dst, const char* src, size_t cap) {
  size_t i = 0;
  while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

static bool linkAlive() {
  return everLinked && (uint32_t)(millis() - lastValidRxAt) < LINK_TIMEOUT_MS;
}

// ------------------------- UTF-8 -> printable ASCII -----------------------
static char mapCodepoint(uint32_t cp) {
  if (cp >= 0xC0 && cp <= 0xFF) return LATIN1_FOLD[cp - 0xC0];
  switch (cp) {
    case 0xA0:   return ' ';
    case 0xA1:   return '!';
    case 0xBF:   return '?';
    case 0x2018: case 0x2019: case 0x201A: return '\'';
    case 0x201C: case 0x201D: case 0x201E: return '"';
    case 0x2013: case 0x2014: return '-';
    case 0x2022: return '*';
    case 0x2026: return '.';
    default:     return '?';
  }
}

static void utf8ToDisplay(const char* in, char* out, size_t cap) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 1 < cap) {
    uint8_t c = *p;
    if (c < 0x80) { out[o++] = (c >= 32 && c != 127) ? (char)c : ' '; p++; continue; }
    uint32_t cp; int extra;
    if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { out[o++] = '?'; p++; continue; }
    p++;
    int i = 0;
    for (; i < extra && (*p & 0xC0) == 0x80; i++, p++) cp = (cp << 6) | (*p & 0x3F);
    out[o++] = (i < extra) ? '?' : mapCodepoint(cp);
  }
  out[o] = 0;
}

// ------------------------------- Serial TX --------------------------------
static void sendFrame(const char* payload) {
  uint8_t cs = 0;
  for (const char* p = payload; *p; p++) cs ^= (uint8_t)*p;
  Serial.write('$');
  Serial.print(payload);
  Serial.write('*');
  if (cs < 16) Serial.write('0');
  Serial.print(cs, HEX);
  Serial.write('\n');
}

// ------------------------- Particles & Beat -------------------------------
static void spawnParticle(int16_t originX, uint8_t type) {
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active) {
      particles[i].active = true;
      particles[i].x = originX + random(-14, 15);
      particles[i].y = random(44, 58);
      particles[i].vy = -random(12, 24) / 10.0f;
      particles[i].sway = random(0, 314) / 100.0f;
      particles[i].glyph = type; // 0=note, 1=star, 2=heart
      break;
    }
  }
}

static void triggerBeat() {
  pulse = 1.0f;
  bounceY = -3.5f;             // rhythmic jump on beat
  beatCount++;
  dancePose = 1 - dancePose;   // switch dance pose

  int16_t spawnX = (currentMode == MODE_SIDE_ART_LEFT) ? 30 : ((currentMode == MODE_SIDE_ART_RIGHT) ? 96 : 64);
  uint8_t glyph = (currentTheme == THEME_HEARTS) ? 2 : ((currentTheme == THEME_MOON_STARS) ? 1 : (beatCount % 2));
  spawnParticle(spawnX, glyph);
}

// ------------------------------- Word Layout ------------------------------
static bool layoutWords(const char* s, uint8_t cols, uint8_t maxRows) {
  nWords = 0; nRows = 0;
  for (uint8_t r = 0; r < 6; r++) rowW[r] = 0;
  uint8_t row = 0, col = 0;
  size_t i = 0;
  bool fit = true;
  while (s[i] && fit) {
    while (s[i] == ' ') i++;
    if (!s[i]) break;
    size_t st = i;
    while (s[i] && s[i] != ' ') i++;
    size_t wl = i - st;
    while (wl > 0) {
      uint8_t take = wl > cols ? cols : (uint8_t)wl;
      if (col > 0 && col + 1 + take > cols) { row++; col = 0; }
      else if (col > 0) col++;
      if (row >= maxRows || nWords >= MAX_WORDS) { fit = false; break; }
      words[nWords++] = { (uint8_t)st, take, row, col, 0 };
      col += take; rowW[row] = col;
      st += take; wl -= take;
    }
  }
  nRows = nWords ? words[nWords - 1].row + 1 : 0;
  return fit;
}

static bool hasWord(const char* str, const char* kw) {
  return strstr(str, kw) != nullptr;
}

static void setLine(const char* s, uint32_t durMs, uint32_t elapsedMs) {
  copyStr(lineText, s, LINE_MAX);
  for (char* p = lineText; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;

  size_t len = strlen(lineText);
  lineDurationMs = durMs;

  // Master Clock Lock: record line start timestamp in song time
  lineStartSongPosMs = (currentSongPosMs >= elapsedMs) ? (currentSongPosMs - elapsedMs) : 0;
  lineElapsed = (float)elapsedMs;

  // Instrumental gap -> Full anime romance scene
  if (len == 0) {
    currentMode = MODE_FULL_ART;
    currentTheme = THEME_NONE;
    nWords = 0;
    return;
  }

  // Thematic visual props & character selection based on user's exact lyrics
  if (hasWord(lineText, "MOON") || hasWord(lineText, "STAR")) {
    currentTheme = THEME_MOON_STARS;
    currentChar = CHAR_COUPLE;
  } else if (hasWord(lineText, "EYES") || hasWord(lineText, "LOVE") || hasWord(lineText, "HONEY") ||
             hasWord(lineText, "HAPPEN") || hasWord(lineText, "PRIDE") || hasWord(lineText, "MINE") ||
             hasWord(lineText, "WHOLE")) {
    currentTheme = THEME_HEARTS;
    currentChar = CHAR_COUPLE;
  } else if (hasWord(lineText, "WHY") || hasWord(lineText, "SEE") || hasWord(lineText, "MIND") || hasWord(lineText, "FEEL")) {
    currentTheme = THEME_NONE;
    currentChar = CHAR_SHRUG;
  } else {
    currentTheme = THEME_DANCE_BEATS;
    currentChar = CHAR_DANCE;
  }

  // Short impact words (<= 7 chars without spaces, like "DANCIN'") -> Huge kinetic center mode
  if (len <= 7 && !strchr(lineText, ' ')) {
    currentMode = MODE_KINETIC_CENTER;
    txtSize = (len <= 4) ? 3 : 2;
    layoutWords(lineText, (64 / (6 * txtSize)) * 2, 2);
  } else {
    // Dynamic alternating side-by-side mode (Art Left vs Art Right)
    currentMode = (lyricCount % 2 == 0) ? MODE_SIDE_ART_LEFT : MODE_SIDE_ART_RIGHT;
    lyricCount++;

    // Try text size 2 for very short phrases (up to 5 chars/line, 3 rows)
    txtSize = 2;
    if (len > 12 || !layoutWords(lineText, 5, 3)) {
      // Use text size 1 (up to 10 chars/line, up to 6 rows) - perfectly fits all lyrics
      txtSize = 1;
      layoutWords(lineText, 10, 6);
    }
  }

  // Reveal words progressively across 75% of line duration
  uint32_t span = durMs ? durMs * 3 / 4 : 2000;
  if (span < 250) span = 250;
  if (span > 3800) span = 3800;
  uint32_t totalChars = len > 0 ? len : 1;
  for (uint8_t i = 0; i < nWords; i++) {
    words[i].at = (uint16_t)(span * words[i].off / totalChars);
  }

  shownWords = 0;
  while (shownWords < nWords && words[shownWords].at <= elapsedMs) shownWords++;

  pulse = pulse > 0.6f ? pulse : 0.6f;
}

static void resetToIdle() {
  lineText[0] = 0; nWords = 0; nRows = 0; shownWords = 0;
  currentMode = MODE_FULL_ART;
  currentTheme = THEME_NONE;
  currentSongPosMs = 0;
  lineStartSongPosMs = 0;
  lineDurationMs = 0;
  playState = 0;
}

// ------------------------------- Frame Handling ---------------------------
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool verifyAndSplit(char* line, uint16_t len, char* fields[], uint8_t& n) {
  if (len < 4 || line[0] != '$') return false;
  char* star = strrchr(line, '*');
  if (!star || (line + len) - star != 3) return false;
  int hi = hexVal(star[1]), lo = hexVal(star[2]);
  if (hi < 0 || lo < 0) return false;
  uint8_t calc = 0;
  for (char* p = line + 1; p < star; p++) calc ^= (uint8_t)*p;
  if (calc != (uint8_t)((hi << 4) | lo)) return false;
  *star = 0;
  n = 0;
  char* p = line + 1;
  fields[n++] = p;
  for (; *p; p++) {
    if (*p == '|') {
      *p = 0;
      if (n < MAX_FIELDS) fields[n++] = p + 1;
    }
  }
  return true;
}

static void handleFrame(char* f[], uint8_t n) {
  uint32_t now = millis();
  switch (f[0][0]) {
    case 'H': {
      char reply[16];
      snprintf(reply, sizeof(reply), "A|%d", PROTOCOL_VERSION);
      sendFrame(reply);
      break;
    }
    case 'T':
      resetToIdle();
      break;
    case 'L':
      if (n >= 4) {
        char c[LINE_MAX];
        utf8ToDisplay(f[1], c, LINE_MAX);
        setLine(c, strtoul(f[2], nullptr, 10), strtoul(f[3], nullptr, 10));
      }
      break;
    case 'P':
      if (n >= 4) {
        uint32_t posMs = strtoul(f[1], nullptr, 10);
        uint32_t st    = strtoul(f[3], nullptr, 10);
        playState = (st > 2) ? 0 : (uint8_t)st;
        currentSongPosMs = posMs;

        // HARDWARE CLOCK-LOCK:
        // Every 200 ms, resynchronize lineElapsed from the PC's audio clock
        if (playState == 1 && lineDurationMs > 0 && posMs >= lineStartSongPosMs) {
          int32_t trueElapsed = (int32_t)(posMs - lineStartSongPosMs);
          if (trueElapsed >= 0 && trueElapsed < (int32_t)lineDurationMs + 4000) {
            lineElapsed = (float)trueElapsed;
          }
        }
      }
      break;
    case 'E':
      if (n >= 3) {
        uint32_t v = strtoul(f[1], nullptr, 10);
        lvlTarget = (v > 100 ? 100 : v) / 100.0f;
        lastEAt = now;
        if (f[2][0] == '1') triggerBeat();
      }
      break;
    case 'I':
      if (n >= 2) {
        bool inv = (f[1][0] == '1' || f[1][0] == 't' || f[1][0] == 'T');
        display.invertDisplay(inv);
      }
      break;
    default: break;
  }
}

static void handleLine(char* line, uint16_t len) {
  char* fields[MAX_FIELDS];
  uint8_t n = 0;
  if (!verifyAndSplit(line, len, fields, n)) return;
  lastValidRxAt = millis();
  everLinked = true;
  digitalWrite(STATUS_LED, HIGH);
  ledOffAt = lastValidRxAt + 30;
  handleFrame(fields, n);
}

static void pollSerial() {
  uint16_t budget = 512;
  while (budget-- && Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\n') {
      if (!rxOverflow && rxLen > 0) { rxBuf[rxLen] = 0; handleLine(rxBuf, rxLen); }
      rxLen = 0; rxOverflow = false;
    } else if (c == '\r' || rxOverflow) {
      // ignore
    } else if (rxLen < RX_BUF_SIZE - 1) {
      rxBuf[rxLen++] = (char)c;
    } else {
      rxOverflow = true;
    }
  }
}

// ============================== DRAWING ===================================

// Draw the active 64x64 character into its box (NO CUTOFFS)
static void drawCharacter(int16_t xPos, int16_t yPos) {
  const uint8_t* art = nullptr;

  switch (currentChar) {
    case CHAR_DANCE:
      art = (dancePose == 0) ? ART_BOY_DANCE1 : ART_BOY_DANCE2;
      break;
    case CHAR_SHRUG:
      art = ART_BOY_SHRUG;
      break;
    case CHAR_COUPLE:
      art = ART_COUPLE_SIDE;
      break;
  }

  if (art != nullptr) {
    display.drawBitmap(xPos, yPos, art, 64, 64, WHITE);
  }
}

// Draw formatted lyric words inside a clean padding area
static void drawLyricBlock(int16_t areaX, int16_t areaW, int16_t areaY, int16_t areaH) {
  if (nWords == 0) return;

  int16_t cw = 6 * txtSize;
  int16_t pitch = (txtSize == 3) ? 26 : ((txtSize == 2) ? 18 : 10);
  int16_t totalH = nRows * pitch - 2;
  int16_t startY = areaY + (areaH - totalH) / 2;
  if (startY < areaY) startY = areaY;

  uint8_t vis = 0;
  for (uint8_t i = 0; i < nWords; i++) {
    if (lineElapsed < (float)words[i].at) break;
    vis++;

    float age = lineElapsed - (float)words[i].at;
    int16_t yoff = 0;
    if (age < 180.0f) {
      float q = 1.0f - age / 180.0f;
      yoff = -(int16_t)(q * q * 6.0f);  // drop-in bounce
    }

    int16_t x = areaX + (areaW - rowW[words[i].row] * cw) / 2 + words[i].col * cw;
    int16_t y = startY + words[i].row * pitch + yoff;

    const char* wText = lineText + words[i].off;
    uint8_t wLen = words[i].len;

    // Word highlight on appear (inverted badge)
    if (age < 85.0f) {
      display.fillRect(x - 1, y - 1, wLen * cw + 2, 8 * txtSize + 2, WHITE);
      for (uint8_t c = 0; c < wLen; c++) {
        display.drawChar(x + c * cw, y, wText[c], BLACK, BLACK, txtSize);
      }
    } else {
      // Double-struck bold white lettering
      for (uint8_t c = 0; c < wLen; c++) {
        display.drawChar(x + c * cw,     y, wText[c], WHITE, WHITE, txtSize);
        display.drawChar(x + c * cw + 1, y, wText[c], WHITE, WHITE, txtSize);
      }
    }
  }
  shownWords = vis;
}

// Draw animated floating anime particles (notes, stars, hearts)
static void updateAndDrawParticles(float dt) {
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active) continue;
    particles[i].y += particles[i].vy * (dt * 60.0f);
    particles[i].sway += dt * 4.0f;
    if (particles[i].y < -4) {
      particles[i].active = false;
      continue;
    }
    int16_t px = (int16_t)(particles[i].x + sinf(particles[i].sway) * 2.5f);
    int16_t py = (int16_t)particles[i].y;

    if (particles[i].glyph == 0) {
      // Musical note (♪)
      display.drawPixel(px, py, WHITE);
      display.drawFastVLine(px + 1, py - 3, 4, WHITE);
      display.drawPixel(px + 2, py - 3, WHITE);
    } else if (particles[i].glyph == 1) {
      // Twinkle sparkle star (✦)
      display.drawPixel(px, py - 1, WHITE);
      display.drawPixel(px - 1, py, WHITE);
      display.drawPixel(px, py, WHITE);
      display.drawPixel(px + 1, py, WHITE);
      display.drawPixel(px, py + 1, WHITE);
    } else {
      // Anime heart (♥)
      display.drawPixel(px - 1, py - 1, WHITE);
      display.drawPixel(px + 1, py - 1, WHITE);
      display.drawPixel(px - 1, py, WHITE);
      display.drawPixel(px,     py, WHITE);
      display.drawPixel(px + 1, py, WHITE);
      display.drawPixel(px,     py + 1, WHITE);
    }
  }
}

// Draw contextual props (moon, stars, equalizer wave)
static void drawContextualProps() {
  if (currentTheme == THEME_MOON_STARS) {
    // Cute crescent moon in top corner
    int16_t mx = (currentMode == MODE_SIDE_ART_RIGHT) ? 8 : 118;
    display.drawCircle(mx, 7, 4, WHITE);
    display.fillCircle(mx + 2, 6, 3, BLACK); // cut into crescent

    // Twinkling stars
    if ((beatCount % 2) == 0) {
      display.drawPixel(mx - 6, 3, WHITE);
      display.drawPixel(mx + 5, 12, WHITE);
    } else {
      display.drawPixel(mx - 5, 11, WHITE);
      display.drawPixel(mx + 6, 2, WHITE);
    }
  }
}

// Sleek bottom audio wave line (clean, modern 1px height with dancing blips)
static void drawBottomAudioWave() {
  int16_t y = SCREEN_H - 1;
  // A clean hairline across the screen with wave ripples based on music level
  for (int16_t x = 0; x < SCREEN_W; x += 4) {
    float wave = lvl * sinf(phase * 2.0f + x * 0.15f);
    int16_t h = (int16_t)(wave * 3.5f);
    if (h > 0) {
      display.drawFastVLine(x, y - h, h + 1, WHITE);
    } else {
      display.drawPixel(x, y, WHITE);
    }
  }
}

// Dynamic equalizer bars for kinetic center mode
static void drawEnergyBars(int16_t x, int16_t y, int16_t w, int16_t maxH) {
  uint8_t numBars = w / 4;
  for (uint8_t i = 0; i < numBars; i++) {
    float barLvl = lvl * (0.6f + 0.4f * sinf(phase * 2.0f + i * 0.8f));
    int16_t h = (int16_t)(barLvl * maxH);
    if (h < 1) h = 1;
    if (h > maxH) h = maxH;
    display.drawFastVLine(x + i * 4, y + maxH - h, h, WHITE);
    display.drawFastVLine(x + i * 4 + 1, y + maxH - h, h, WHITE);
  }
}

// ------------------------------- Main Render ------------------------------
static void render(uint32_t now) {
  float dt = (now - lastRenderAt) / 1000.0f;
  lastRenderAt = now;
  if (dt < 0.001f) dt = 0.001f;
  if (dt > 0.08f) dt = 0.08f;

  bool alive = linkAlive();
  if (wasAlive && !alive) resetToIdle();
  wasAlive = alive;
  bool playing = alive && playState == 1;

  // Spring physics & continuous sine groove
  pulse -= dt / 0.25f;
  if (pulse < 0) pulse = 0;
  bounceY += (-bounceY) * (dt * 14.0f);   // spring back from beat jump
  phase += dt * (2.2f + lvl * 5.0f);
  swayX = sinf(phase) * 1.6f;             // rhythmic character sway

  // Local clock integration between 200ms P frames
  if (playing) {
    lineElapsed += dt * 1000.0f;
  }

  // Energy & beat simulation fallback if PC energy packets stall
  bool fresh = (uint32_t)(now - lastEAt) < 600;
  if (playing && !fresh) {
    lvlTarget = 0.5f + 0.2f * sinf(now / 280.0f);
    if ((int32_t)(now - nextFakeBeat) >= 0) {
      nextFakeBeat = now + 480;
      triggerBeat();
    }
  } else if (!playing) {
    lvlTarget = 0.15f;
    if ((int32_t)(now - nextFakeBeat) >= 0) {
      nextFakeBeat = now + 1200;
      triggerBeat();
    }
  }
  float k = dt * 10.0f;
  lvl += (lvlTarget - lvl) * (k > 1 ? 1 : k);

  // Clear display buffer
  display.clearDisplay();

  int16_t bobY = (int16_t)(sinf(phase * 1.2f) * 1.2f + bounceY);
  int16_t charXOffset = (int16_t)swayX;

  switch (currentMode) {
    case MODE_SIDE_ART_LEFT: {
      // Character on left (0..60) with groove motion - NO DIVIDER LINE
      drawCharacter(charXOffset, bobY);

      // Clean, borderless lyrics on right (64..127) with proper vertical centering
      drawLyricBlock(64, 64, 0, SCREEN_H - 4);
      break;
    }

    case MODE_SIDE_ART_RIGHT: {
      // Clean, borderless lyrics on left (0..62) with proper vertical centering
      drawLyricBlock(0, 62, 0, SCREEN_H - 4);

      // Character on right (64..127) with groove motion - NO DIVIDER LINE
      drawCharacter(64 + charXOffset, bobY);
      break;
    }

    case MODE_KINETIC_CENTER: {
      // High-impact bold kinetic typography + audio equalizer
      int16_t shakeX = 0, shakeY = 0;
      if (pulse > 0.6f) {
        shakeX = random(-1, 2);
        shakeY = random(-1, 2);
      }
      drawLyricBlock(shakeX, SCREEN_W, shakeY, 44);

      // Audio equalizer bars at bottom
      drawEnergyBars(12, 48, 104, 14);

      // Animated sleek corner brackets that pulse on the beat
      int16_t bSize = (pulse > 0.4f) ? 10 : 8;
      display.drawFastHLine(0, 0, bSize, WHITE);
      display.drawFastVLine(0, 0, bSize, WHITE);
      display.drawFastHLine(SCREEN_W - bSize, 0, bSize, WHITE);
      display.drawFastVLine(SCREEN_W - 1, 0, bSize, WHITE);
      display.drawFastHLine(0, SCREEN_H - 1, bSize, WHITE);
      display.drawFastVLine(0, SCREEN_H - 1, bSize, WHITE);
      display.drawFastHLine(SCREEN_W - bSize, SCREEN_H - 1, bSize, WHITE);
      display.drawFastVLine(SCREEN_W - 1, SCREEN_H - 1, bSize, WHITE);
      break;
    }

    case MODE_FULL_ART: {
      // Full-screen anime romance pinky-promise couple scene!
      display.drawBitmap(0, bobY / 2, ART_COUPLE_FULL, 128, 64, WHITE);

      // Animated energy-beating heart in center (around x=64, y=21)
      if (pulse > 0.2f) {
        display.drawPixel(63, 21, WHITE);
        display.drawPixel(65, 21, WHITE);
      }
      if (pulse > 0.6f) {
        display.drawPixel(64, 20, WHITE);
        display.drawPixel(64, 22, WHITE);
      }
      break;
    }
  }

  // Contextual props, sleek bottom audio wave, and floating particles
  drawContextualProps();
  if (currentMode != MODE_KINETIC_CENTER && currentMode != MODE_FULL_ART) {
    drawBottomAudioWave();
  }
  updateAndDrawParticles(dt);

  // Push to OLED
  display.display();
}

// ------------------------------- Setup & Loop -----------------------------
void setup() {
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  Serial.setRxBufferSize(1024);
  Serial.begin(SERIAL_BAUD);

  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false);

  randomSeed(micros());
  lineText[0] = 0;

  if (oledOk) {
    #if INVERT_DISPLAY
    display.invertDisplay(true);
    #else
    display.invertDisplay(false);
    #endif
    display.clearDisplay();
    display.display();
    char hello[16];
    snprintf(hello, sizeof(hello), "R|%d", PROTOCOL_VERSION);
    sendFrame(hello);
  }
}

void loop() {
  uint32_t now = millis();

  if (!oledOk) {
    static uint32_t lastMsg = 0;
    if ((uint32_t)(now - lastMsg) >= 1000) {
      lastMsg = now;
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
      Serial.println("ERROR: SSD1306 not found (check wiring / address 0x3C or 0x3D)");
    }
    return;
  }

  pollSerial();

  if (ledOffAt && (int32_t)(now - ledOffAt) >= 0) {
    digitalWrite(STATUS_LED, LOW);
    ledOffAt = 0;
  }

  if ((uint32_t)(now - lastRenderAt) >= FRAME_MS) {
    render(now);
  }

  yield();
}
