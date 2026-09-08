/*
  led-strip3 - the THIRD Arduino (COM9), driving a separate LED strip.
  Reacts to one command:
    "state5z" - sent when State 5 mode Z is triggered:
                  0 - 5700ms: dark
                  5700ms onward - close to when COM7's own mode Z comet
                    lands (00:00:05:25, 5833ms) - a comet launches from
                    pixel 0 and travels up to the midpoint (pixel 73),
                    using the SAME eased motion/speed as COM7's mode Z
                    comet (scaled to this strip's shorter distance)
                  once the comet's tail melts into pixel 73: expands out
                    from that point to fill the existing 60px section
                    (43-102)
                  then holds there, solid red, breathing (fading in and
                    out slowly), forever
    "state5x" - sent when State 5 mode X is triggered:
                  0 - 2000ms: dark (matches when COM7's own mode X comet
                    first reaches pixel 0)
                  2000ms onward - a comet repeats forever, launching from
                    pixel 0 and racing up to the midpoint (pixel 73),
                    using the SAME constant speed as COM7 mode X's comet
                    (0.48 px/ms), with a brief 7px-wide glow (3px each
                    side of the midpoint) between passes, same rhythm as
                    the comets landing
  Any other command just blanks this strip.

  NUM_LEDS is a placeholder (190) - hasn't been confirmed against the
  real physical LED count on this strip. Wiring: DATA_PIN 5 (was 7 - pin
  7 wasn't responding, still needs confirming that 5 actually works).
*/

#include <FastLED.h>
#include <string.h>

#define DATA_PIN     5
#define NUM_LEDS     190   // PLACEHOLDER - confirm against the real strip
#define COLOR_ORDER  GRB   // assumed - confirm if colors look wrong

#define CENTER_PIXEL     73  // moved 7px more down (was 80)
#define SEGMENT_HALF     30  // 60 pixels total: 43-102
#define SEGMENT_START   (CENTER_PIXEL - SEGMENT_HALF)
#define SEGMENT_END     (CENTER_PIXEL + SEGMENT_HALF - 1)

#define COMET_START_MS    5700  // close to when COM7 mode Z's own comet reaches pixel 0 (00:00:05:25, 5833ms)
// Matches COM7 mode Z's comet speed: that one covers a virtual distance
// of 140+14=154px in 2666ms (~0.0578 px/ms). This comet's virtual
// distance is 73+14=87px, so at the same speed it takes
// 87 * 2666 / 154 = ~1506ms.
#define COMET_TRAVEL_MS    1506
#define COMET_TRAIL_LENGTH   15  // same as COM7 mode Z's comet
#define COMET_FADE_OUT_PX     4  // melt-into-target width, same as other comets
#define EXPAND_MS           400  // not specified - quick expand from the point to the full section

// Slow breathing fade in/out for the final hold - not specified exactly,
// picked a gentle slow pulse.
#define BREATHE_BPM    6
#define BREATHE_MIN   20
#define BREATHE_MAX  255

// "state5x" - repeating comet, matching COM7 mode X's speed (144px in
// 300ms = 0.48 px/ms) and landing glow (80ms), targeting this strip's
// own midpoint (73) instead of mode X's (144).
#define XCOMET_START_MS       2000  // matches when COM7 mode X's first comet reaches pixel 0
#define XCOMET_SPEED_PXPMS    0.48f
#define XCOMET_TRAIL_LENGTH     15  // same as COM7 mode X's comet
#define XCOMET_FADE_OUT_PX       4
#define XCOMET_GLOW_MS          80  // same as COM7 mode X's landing glow

CRGB leds[NUM_LEDS];
CRGB STRIP3_COLOR = CRGB::Red;

enum Stage3 {
  STAGE3_IDLE, STAGE3_WAIT, STAGE3_COMET, STAGE3_EXPAND, STAGE3_HOLD,
  STAGE3_X_WAIT, STAGE3_X_COMET, STAGE3_X_GLOW
};
Stage3 stage = STAGE3_IDLE;

unsigned long triggerTime = 0;
unsigned long stageStartTime = 0;

char cmdBuffer[16];
uint8_t cmdLength = 0;

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();
  Serial.println(F("led-strip3 ready."));
}

void loop() {
  checkSerialCommand();

  switch (stage) {
    case STAGE3_IDLE:    break; // nothing triggered yet - stay dark
    case STAGE3_WAIT:    doWait(); break;
    case STAGE3_COMET:   doComet(); break;
    case STAGE3_EXPAND:  doExpand(); break;
    case STAGE3_HOLD:    doHold(); break;
    case STAGE3_X_WAIT:  doXWait(); break;
    case STAGE3_X_COMET: doXComet(); break;
    case STAGE3_X_GLOW:  doXGlow(); break;
  }
}

// ---------- Read "state5z" / "state5x" / anything else from Serial and react ----------
void checkSerialCommand() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdLength > 0) {
        cmdBuffer[cmdLength] = '\0';
        if (strcmp(cmdBuffer, "state5z") == 0) {
          startState5z();
        } else if (strcmp(cmdBuffer, "state5x") == 0) {
          startState5x();
        } else {
          goDark();
        }
        Serial.print(F("led-strip3 got: "));
        Serial.println(cmdBuffer);
        cmdLength = 0;
      }
    } else if (cmdLength < sizeof(cmdBuffer) - 1) {
      cmdBuffer[cmdLength++] = c;
    }
    // else: buffer full, silently drop extra characters rather than overrun
  }
}

// ---------- Any command other than "state5z"/"state5x" just blanks this strip ----------
void goDark() {
  FastLED.clear();
  FastLED.show();
  stage = STAGE3_IDLE;
}

// ---------- Start (or restart) the sequence on this strip ----------
void startState5z() {
  FastLED.clear();
  FastLED.show();
  triggerTime = millis();
  stage = STAGE3_WAIT;
  Serial.println(F("State 5z (strip 3): waiting..."));
}

// ---------- Dark until COMET_START_MS (5700ms) ----------
void doWait() {
  if (millis() - triggerTime >= COMET_START_MS) {
    stageStartTime = millis();
    stage = STAGE3_COMET;
    Serial.println(F("State 5z (strip 3): comet to midpoint..."));
  }
}

// ---------- Ease-in-out cubic, biased toward a faster initial ramp-up -
// same shape used by COM7 mode Z's comet. ----------
float easeInOutCubicFast(float t) {
  float e;
  if (t < 0.5) {
    e = 4.0 * t * t * t;
  } else {
    float f = (2.0 * t) - 2.0;
    e = 0.5 * f * f * f + 1.0;
  }
  return pow(e, 0.6f);
}

// ---------- Comet launches from pixel 0 and travels UP to the midpoint
// (73), melting in as its tail arrives (mirrors COM7 mode Z's comet,
// just the opposite direction - toward higher pixels instead of down to
// 0). ----------
void doComet() {
  unsigned long elapsed = millis() - stageStartTime;
  float t = (float)elapsed / COMET_TRAVEL_MS;
  bool done = (t >= 1.0);
  if (t > 1.0) t = 1.0;

  float eased = easeInOutCubicFast(t);
  float virtualTravel = CENTER_PIXEL + (COMET_TRAIL_LENGTH - 1);
  float pos = virtualTravel * eased;

  FastLED.clear();
  if (!done) {
    int headPos = (int)pos;
    for (int offset = 0; offset < COMET_TRAIL_LENGTH; offset++) {
      int px = headPos - offset;
      if (px < 0 || px > CENTER_PIXEL) continue;

      uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (COMET_TRAIL_LENGTH - 1));

      uint8_t endFade = 255;
      int distToEnd = CENTER_PIXEL - px;
      if (distToEnd <= COMET_FADE_OUT_PX) {
        float frac = (float)distToEnd / COMET_FADE_OUT_PX;
        if (frac < 0) frac = 0;
        endFade = (uint8_t)(255.0 * frac);
      }

      CRGB c = STRIP3_COLOR;
      c.nscale8(tailFade);
      c.nscale8(endFade);
      leds[px] = c;
    }
    FastLED.show();
  } else {
    stageStartTime = millis();
    stage = STAGE3_EXPAND;
    Serial.println(F("State 5z (strip 3): expanding to full section..."));
  }
}

// ---------- Expands from the melted point (73) out to fill the existing
// 60px section (43-102), over EXPAND_MS - breathing starts right here,
// not just once it lands in doHold(). ----------
void doExpand() {
  unsigned long elapsed = millis() - stageStartTime;
  float t = (float)elapsed / EXPAND_MS;
  if (t > 1.0) t = 1.0;

  int currentHalf = (int)(SEGMENT_HALF * t);
  int rangeStart = CENTER_PIXEL - currentHalf;
  int rangeEnd = CENTER_PIXEL + currentHalf;
  if (rangeStart < SEGMENT_START) rangeStart = SEGMENT_START;
  if (rangeEnd > SEGMENT_END) rangeEnd = SEGMENT_END;

  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STRIP3_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (t >= 1.0) {
    stage = STAGE3_HOLD;
    Serial.println(F("State 5z (strip 3): fully lit - holding..."));
  }
}

// ---------- 60px section centered on pixel 73, solid red, breathing
// (fading in and out slowly), forever until the next command ----------
void doHold() {
  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STRIP3_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  FastLED.show();
}

// ---------- Start (or restart) the state5x sequence on this strip ----------
void startState5x() {
  FastLED.clear();
  FastLED.show();
  triggerTime = millis();
  stage = STAGE3_X_WAIT;
  Serial.println(F("State 5x (strip 3): waiting..."));
}

// ---------- Dark until XCOMET_START_MS (2000ms) ----------
void doXWait() {
  if (millis() - triggerTime >= XCOMET_START_MS) {
    stageStartTime = millis();
    stage = STAGE3_X_COMET;
    Serial.println(F("State 5x (strip 3): comet repeating..."));
  }
}

// ---------- Comet, at COM7 mode X's constant speed, launches from pixel
// 0 and races UP to the midpoint (73), melting in as its tail arrives,
// then a brief landing glow, then repeats forever. ----------
void doXComet() {
  unsigned long elapsed = millis() - stageStartTime;

  float pos = XCOMET_SPEED_PXPMS * elapsed;
  float tailPos = pos - (XCOMET_TRAIL_LENGTH - 1);

  FastLED.clear();
  if (tailPos < CENTER_PIXEL) {
    int headPos = (int)pos;
    for (int offset = 0; offset < XCOMET_TRAIL_LENGTH; offset++) {
      int px = headPos - offset;
      if (px < 0 || px > CENTER_PIXEL) continue;

      uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (XCOMET_TRAIL_LENGTH - 1));

      uint8_t endFade = 255;
      int distToEnd = CENTER_PIXEL - px;
      if (distToEnd <= XCOMET_FADE_OUT_PX) {
        float frac = (float)distToEnd / XCOMET_FADE_OUT_PX;
        if (frac < 0) frac = 0;
        endFade = (uint8_t)(255.0 * frac);
      }

      CRGB c = STRIP3_COLOR;
      c.nscale8(tailFade);
      c.nscale8(endFade);
      leds[px] = c;
    }
    FastLED.show();
  } else {
    stageStartTime = millis();
    stage = STAGE3_X_GLOW;
  }
}

// ---------- Brief glow at the midpoint, expanded 3 pixels on each side
// (7 pixels total) so it reads as a glowing point rather than a single
// pixel - bridges one comet pass into the next repeat, same rhythm as
// the comets hitting the midpoint (once per landing). ----------
void doXGlow() {
  FastLED.clear();
  for (int i = CENTER_PIXEL - 3; i <= CENTER_PIXEL + 3; i++) {
    leds[i] = STRIP3_COLOR;
  }
  FastLED.show();

  if (millis() - stageStartTime >= XCOMET_GLOW_MS) {
    stageStartTime = millis();
    stage = STAGE3_X_COMET; // repeats forever
  }
}
