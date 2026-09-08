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
    "state6"  - sent when State 6 (the bass drop) is triggered. Has two
                  different entry transitions depending what this strip
                  was doing, both landing on the same final section/
                  color:
                    - mid-state5z-hold (STAGE3_HOLD, the settled red
                      breathing section): quickly (STATE6_SHRINK_MS)
                      turns white and shrinks the 60px section in by
                      STATE6_SHRINK_PX (3) pixels on each side
                    - mid-state5x (STAGE3_X_WAIT/X_COMET/X_GLOW - no lit
                      section to shrink from there): expands OUT from
                      the midpoint (73) to that same target section/
                      color instead, over STATE6_EXPAND_MS
                  Either way it then holds there breathing white
                  forever. If state6 arrives at any other time, it just
                  blanks the strip like every other unhandled command.
    "state7"  - sent when State 7 is triggered, but only does anything
                  if this strip is currently mid-state6-hold
                  (STAGE3_STATE6_HOLD - state6's settled white section).
                  All timing measured from state7's own trigger, t=0:
                    0 - 00:00:00:15 (500ms) - that section shrinks down
                      to a single point at its own middle (73)
                    00:00:00:15 - 00:00:00:25 (833ms) - that point moves
                      30 pixels down, to pixel 43
                    00:00:00:25 - 00:00:01:27 (1900ms) - now a comet (a
                      solid 5px head plus a 5px fading trail), eased -
                      accelerates/decelerates rather than constant speed
                      - moving UP to pixel 135
                  Holds there (a single breathing white point at pixel
                  135) once it arrives. If state7 arrives at any other
                  time, it just blanks the strip like every other
                  unhandled command.
  Any other command just blanks this strip.

  NUM_LEDS is a placeholder (190) - hasn't been confirmed against the
  real physical LED count on this strip. Wiring: DATA_PIN 5 (was 7 - pin
  7 wasn't responding, still needs confirming that 5 actually works).
*/

// Without this, FastLED.show() briefly disables interrupts on every
// call (needed for WS2812's strict bit timing) - while doHold() is
// calling it every loop iteration, an incoming Serial command byte can
// land in that window and get corrupted (confirmed by testing: "state6"
// arriving as "stte6" or "st" while the state5z hold was running,
// failing to match and blanking the strip instead of triggering the
// shrink). This lets FastLED keep interrupts enabled and retry any bit
// an interrupt disrupts instead, trading a vanishingly small chance of
// a single pixel glitch for Serial actually being received reliably.
#define FASTLED_ALLOW_INTERRUPTS 1
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

// "state6" (bass drop) - only acts if triggered while mid-state5z-hold:
// shrinks the same 60px section 3px in on each side and switches it to
// white. Duration not specified - picked a quick, snappy transition to
// land with a bass hit rather than an eased crawl.
#define STATE6_SHRINK_PX    3
#define STATE6_SHRINK_MS  300
#define STATE6_TARGET_START  (SEGMENT_START + STATE6_SHRINK_PX)
#define STATE6_TARGET_END    (SEGMENT_END - STATE6_SHRINK_PX)
// If triggered instead while mid-state5x (no lit section to shrink from -
// it's always mid-comet or mid-glow), state6 expands OUT from the
// midpoint (73) to the same target section/color above, mirroring
// doExpand()'s own point-to-section expand (EXPAND_MS) rather than a
// shrink.
#define STATE6_EXPAND_MS  400

// "state7" - only reachable from STAGE3_STATE6_HOLD (continues from
// state6's own held white section). All timing measured from state7's
// own trigger, t=0:
//   0 - 00:00:00:15 (500ms): that section shrinks down to a single
//       point at its own middle (CENTER_PIXEL, 73)
//   00:00:00:15 - 00:00:00:25 (500-833ms): that point moves
//       STATE7_MOVE1_PX (30) pixels down, to pixel 43
//   00:00:00:25 - 00:00:01:27 (833-1900ms): now a comet - a solid 5px
//       head plus a 5px fading trail behind it - accelerates/decelerates
//       (eased, same easeInOutCubicFast() shape as state5z's comet, not
//       constant speed) moving UP to pixel 135
// Holds there (a single point at pixel 135) once it arrives.
#define STATE7_SHRINK_MS      500  // 00:00:00:15
#define STATE7_MOVE1_END_MS   833  // 00:00:00:25
#define STATE7_MOVE1_PX        30
#define STATE7_MOVE1_MS       (STATE7_MOVE1_END_MS - STATE7_SHRINK_MS)
#define STATE7_MOVE1_TARGET   (CENTER_PIXEL - STATE7_MOVE1_PX)  // 43
#define STATE7_MOVE2_END_MS  1900  // 00:00:01:27
#define STATE7_MOVE2_TARGET   135  // 15px further than pixel 120
#define STATE7_COMET_LENGTH     5  // solid head block, full brightness
#define STATE7_TRAIL_LENGTH     5  // fading tail appended behind the head
#define STATE7_MOVE2_MS       (STATE7_MOVE2_END_MS - STATE7_MOVE1_END_MS)

CRGB leds[NUM_LEDS];
CRGB STRIP3_COLOR = CRGB::Red;
CRGB STATE6_COLOR = CRGB::White;

enum Stage3 {
  STAGE3_IDLE, STAGE3_WAIT, STAGE3_COMET, STAGE3_EXPAND, STAGE3_HOLD,
  STAGE3_X_WAIT, STAGE3_X_COMET, STAGE3_X_GLOW,
  STAGE3_STATE6_SHRINK, STAGE3_STATE6_EXPAND, STAGE3_STATE6_HOLD,
  STAGE3_STATE7_SHRINK, STAGE3_STATE7_MOVE1, STAGE3_STATE7_MOVE2, STAGE3_STATE7_HOLD
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
    case STAGE3_STATE6_SHRINK: doState6Shrink(); break;
    case STAGE3_STATE6_EXPAND: doState6Expand(); break;
    case STAGE3_STATE6_HOLD:   doState6Hold(); break;
    case STAGE3_STATE7_SHRINK: doState7Shrink(); break;
    case STAGE3_STATE7_MOVE1:  doState7Move1(); break;
    case STAGE3_STATE7_MOVE2:  doState7Move2(); break;
    case STAGE3_STATE7_HOLD:   doState7Hold(); break;
  }
}

// ---------- Read "state5z" / "state5x" / "state6" / "state7" / anything else from Serial and react ----------
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
        } else if (strcmp(cmdBuffer, "state6") == 0 && stage == STAGE3_HOLD) {
          startState6Shrink();
        } else if (strcmp(cmdBuffer, "state6") == 0 &&
                   (stage == STAGE3_X_WAIT || stage == STAGE3_X_COMET || stage == STAGE3_X_GLOW)) {
          startState6ExpandFromMidpoint();
        } else if (strcmp(cmdBuffer, "state7") == 0 && stage == STAGE3_STATE6_HOLD) {
          startState7();
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

// ---------- "state6" (bass drop), only reachable from STAGE3_HOLD -
// quickly shrinks the same 60px section 3px in on each side and turns
// it white, over STATE6_SHRINK_MS ----------
void startState6Shrink() {
  stageStartTime = millis();
  stage = STAGE3_STATE6_SHRINK;
  Serial.println(F("State 6 (strip 3): bass drop - shrinking to white..."));
}

void doState6Shrink() {
  unsigned long elapsed = millis() - stageStartTime;
  float t = (float)elapsed / STATE6_SHRINK_MS;
  if (t > 1.0) t = 1.0;

  int rangeStart = SEGMENT_START + (int)(STATE6_SHRINK_PX * t);
  int rangeEnd = SEGMENT_END - (int)(STATE6_SHRINK_PX * t);

  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STATE6_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (t >= 1.0) {
    stage = STAGE3_STATE6_HOLD;
    Serial.println(F("State 6 (strip 3): holding white, shrunk 3px each side..."));
  }
}

// ---------- "state6" (bass drop), only reachable from the state5x
// stages (STAGE3_X_WAIT/X_COMET/X_GLOW) - there's no lit section to
// shrink from there (state5x is always mid-comet or mid-glow), so
// instead it expands OUT from the midpoint (73) to the same target
// section/color STAGE3_STATE6_HOLD settles on, over STATE6_EXPAND_MS ----------
void startState6ExpandFromMidpoint() {
  stageStartTime = millis();
  stage = STAGE3_STATE6_EXPAND;
  Serial.println(F("State 6 (strip 3): bass drop - expanding from midpoint to white..."));
}

void doState6Expand() {
  unsigned long elapsed = millis() - stageStartTime;
  float t = (float)elapsed / STATE6_EXPAND_MS;
  if (t > 1.0) t = 1.0;

  // Lerp each edge independently from CENTER_PIXEL to its own target -
  // the target section isn't perfectly centered on CENTER_PIXEL (same
  // off-by-one as the rest of this section's math), so this lands
  // exactly on STATE6_TARGET_START/END at t=1.0 either way.
  int rangeStart = CENTER_PIXEL - (int)((CENTER_PIXEL - STATE6_TARGET_START) * t);
  int rangeEnd   = CENTER_PIXEL + (int)((STATE6_TARGET_END - CENTER_PIXEL) * t);

  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STATE6_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (t >= 1.0) {
    stage = STAGE3_STATE6_HOLD;
    Serial.println(F("State 6 (strip 3): holding white, shrunk 3px each side..."));
  }
}

// ---------- Settled after the state6 shrink - same 60px section minus
// 3px each side, solid white, breathing, forever until the next command ----------
void doState6Hold() {
  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STATE6_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  for (int i = STATE6_TARGET_START; i <= STATE6_TARGET_END; i++) {
    leds[i] = c;
  }
  FastLED.show();
}

// ---------- "state7", only reachable from STAGE3_STATE6_HOLD - shrinks
// that same white section down to a single point at its own middle
// (CENTER_PIXEL) over STATE7_SHRINK_MS ----------
void startState7() {
  stageStartTime = millis();
  stage = STAGE3_STATE7_SHRINK;
  Serial.println(F("State 7 (strip 3): shrinking to midpoint..."));
}

void doState7Shrink() {
  unsigned long elapsed = millis() - stageStartTime;
  float t = (float)elapsed / STATE7_SHRINK_MS;
  if (t > 1.0) t = 1.0;

  int currentHalfLow  = (int)((CENTER_PIXEL - STATE6_TARGET_START) * (1.0 - t));
  int currentHalfHigh = (int)((STATE6_TARGET_END - CENTER_PIXEL) * (1.0 - t));
  int rangeStart = CENTER_PIXEL - currentHalfLow;
  int rangeEnd   = CENTER_PIXEL + currentHalfHigh;

  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STATE6_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (t >= 1.0) {
    stageStartTime = millis();
    stage = STAGE3_STATE7_MOVE1;
    Serial.println(F("State 7 (strip 3): moving down 30px..."));
  }
}

// ---------- The melted-down point moves STATE7_MOVE1_PX (30) pixels
// down, from CENTER_PIXEL to STATE7_MOVE1_TARGET (43), over
// STATE7_MOVE1_MS ----------
void doState7Move1() {
  unsigned long elapsed = millis() - stageStartTime;
  float t = (float)elapsed / STATE7_MOVE1_MS;
  if (t > 1.0) t = 1.0;

  int pos = CENTER_PIXEL - (int)(STATE7_MOVE1_PX * t);

  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STATE6_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  leds[pos] = c;
  FastLED.show();

  if (t >= 1.0) {
    stageStartTime = millis();
    stage = STAGE3_STATE7_MOVE2;
    Serial.println(F("State 7 (strip 3): moving to end of strip..."));
  }
}

// ---------- Now a comet - a solid STATE7_COMET_LENGTH (5px) head plus a
// STATE7_TRAIL_LENGTH (5px) fading tail behind it - eased (accelerates
// then decelerates, same easeInOutCubicFast() shape as state5z's own
// comet, not constant speed) from STATE7_MOVE1_TARGET (43) up to
// STATE7_MOVE2_TARGET (135), over STATE7_MOVE2_MS. Trail trails behind
// in the direction of travel (lower pixels), same convention as this
// file's other comets. ----------
void doState7Move2() {
  unsigned long elapsed = millis() - stageStartTime;
  float t = (float)elapsed / STATE7_MOVE2_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeInOutCubicFast(t);
  int headFront = STATE7_MOVE1_TARGET + (int)((STATE7_MOVE2_TARGET - STATE7_MOVE1_TARGET) * eased);

  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);

  FastLED.clear();

  // Solid head - full brightness, STATE7_COMET_LENGTH pixels
  for (int offset = 0; offset < STATE7_COMET_LENGTH; offset++) {
    int px = headFront - offset;
    if (px < STATE7_MOVE1_TARGET || px > STATE7_MOVE2_TARGET) continue;
    CRGB c = STATE6_COLOR;
    c.nscale8(breath);
    leds[px] = c;
  }

  // Fading tail - STATE7_TRAIL_LENGTH pixels appended behind the head
  for (int offset = 0; offset < STATE7_TRAIL_LENGTH; offset++) {
    int px = headFront - STATE7_COMET_LENGTH - offset;
    if (px < STATE7_MOVE1_TARGET || px > STATE7_MOVE2_TARGET) continue;
    uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (STATE7_TRAIL_LENGTH - 1));
    CRGB c = STATE6_COLOR;
    c.nscale8(breath);
    c.nscale8(tailFade);
    leds[px] = c;
  }

  FastLED.show();

  if (t >= 1.0) {
    stage = STAGE3_STATE7_HOLD;
    Serial.println(F("State 7 (strip 3): holding at pixel 135..."));
  }
}

// ---------- Settled at pixel 135 - a single breathing white point,
// forever until the next command ----------
void doState7Hold() {
  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STATE6_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  leds[STATE7_MOVE2_TARGET] = c;
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
