/*
  State 1 - manually triggered via Serial commands
  --------------------------------------------------------------
  CONTROL: type a command into the Arduino IDE's Serial Monitor send
  box (top of the Serial Monitor panel) and hit Enter/Send:
    state 1   -> runs the whole State 1 sequence below
  Baud rate must be set to 115200 in the Serial Monitor.

  No timers, no automatic advancing on power-up anymore - the whole
  sequence only runs when "state 1" is sent. This is standing in for
  the real hardware trigger (sensor/button) until that's wired in -
  see checkSerialCommand() and goToState() below, that's the one
  place to change when the real trigger exists.

  STATE 1 SEQUENCE (timestamps relative to the "state 1" trigger):
    0ms                    - nothing shown yet (STATE1_START_DELAY_MS
                             = 1133ms)
    1133ms                 - comet 1 fades in, longer trail than usual
                             (STATE1_CHUNK_SIZE), travels toward pixel 66
                             at a CONSTANT speed (STATE1_COMET_SPEED_PXPMS
                             - no easing, no acceleration, steady/
                             hypnotic), fading out over the last
                             STATE1_COMET12_FADE_OUT_PX pixels (6, wider
                             than comet 3's 4, for a more gradual look)
                             before it gets there. Keeps going even
                             after comets 2 and 3 enter - they can all
                             be on screen together.
    3433ms                  - comet 2 enters (STATE1_COMET2_TRIGGER_MS),
                             same speed/trail/fade-out, no fade-in
    5733ms                  - comet 3 enters (STATE1_COMET3_TRIGGER_MS,
                             same +2300ms interval as comet 1->2), same
                             speed/trail/fade-out, no fade-in
    comet 3 reaches pixel 66 - THIS is what triggers the next stage now
                             (no fixed deadline) - comets 1 and 2 always
                             finish naturally well before comet 3 gets
                             there, so nothing needs cutting off
    -> pixel 66 glows solid for STATE1_GLOW_MS (300ms), then the segment
       expands from that same pixel outward, both ways, fast-then-slow,
       over STATE1_EXPAND_MS (2700ms)
    fully expanded          - fades the whole segment to black over
                             STATE1_FADEOUT_MS (2300ms), then the ENTIRE
                             sequence loops again from the start delay -
                             repeats forever until a different state
                             command interrupts it

  PIXEL MAPPING (12.5mm per pixel)
  -----------------------------------
  459mm-1184mm -> pixels 37-95 (SEGMENT_START/END), middle = pixel 66
  (recentered again from 29-87/58, same 58-pixel width)

  WIRING
  -------
  Strip DATA IN   -> Arduino pin 6
  Strip 5V / GND  -> power board (not the Arduino)
  Arduino GND     -> power board's GND rail
*/

#include <FastLED.h>
#include <string.h>

#define DATA_PIN      6
#define NUM_LEDS      200
#define COLOR_ORDER   GRB

#define SEGMENT_START  37
#define SEGMENT_END    95
#define STATE1_MID    66

#define CHUNK_SIZE          24

#define EAT_DURATION_MS     600    // time for the chunk to shrink away at center

#define STATE_EXPAND_MS     3200   // State 3's expand duration (unchanged)

// State 1 timing - converted from HH:MM:SS:FF timecode at 30fps.
#define STATE1_START_DELAY_MS     1133  // 00:00:01:04 - nothing shown before comet 1
// Both comets move at the SAME constant speed - no easing, no
// acceleration, steady/hypnotic. Speed is derived from covering the
// full 88-pixel distance in this many ms (the original "2x slower"
// duration) - comet 2 uses the identical speed, not its own duration.
#define STATE1_COMET_TRAVEL_MS    4600
#define STATE1_COMET_SPEED_PXPMS  ((float)STATE1_MID / STATE1_COMET_TRAVEL_MS)
// Comet 2 enters at its own fixed timestamp - comet 1 is NOT cut off by
// it, it just keeps going, finishing its own trail while comet 2
// travels alongside it.
#define STATE1_COMET2_TRIGGER_MS  3433  // 00:00:03:13
// Comet 3 enters after comet 2, same +2300ms interval as comet 1->2.
// No fixed deadline anymore - comet 3's OWN arrival at pixel 66 is
// what triggers the glow + expand. Comets 1 and 2 always finish
// naturally well before comet 3 gets there (they don't need cutting off).
#define STATE1_COMET3_TRIGGER_MS  5733
// Right when the last comet ends, pixel 66 alone glows solid for a
// moment - then the expand continues from that exact same single
// pixel, so there's no gap/cut between "comet arrives" and "expand
// begins" (not an exact given duration - picked a quick, natural hold).
#define STATE1_GLOW_MS             300
#define STATE1_EXPAND_MS          2700  // 8700 - 6000 = the "both ways" reveal (unchanged)
#define STATE1_EXPAND_SHORTEN_PX     6  // max reach is 6px short of the full segment, each side
#define STATE1_EXPAND_EDGE_FADE_PX   6  // soft fade at the growing edges (not specified
                                         // exactly - matched the 6px used elsewhere)
#define STATE1_FADEOUT_MS         2300  // fade to black at the very end (unchanged)
#define STATE1_COMET_FADE_IN_MS    400  // comet 1 only, at the very start (time-based)
// Position-based fade-out width (not time-based) - comets 1 and 2 use a
// wider zone so their disappearance near pixel 66 looks more gradual;
// comet 3 keeps the tighter original width since its arrival there is
// the actual trigger for the glow + expand.
#define STATE1_COMET12_FADE_OUT_PX   6
#define STATE1_COMET3_FADE_OUT_PX    4
#define STATE1_CHUNK_SIZE           40  // "longer trail" - vs the usual CHUNK_SIZE (24)

CRGB leds[NUM_LEDS];

CRGB PINK = CRGB(0xFF, 0x14, 0x5A); // #FF145A - State 1 color (and the comet)

enum Stage {
  STAGE_STATE0,
  STAGE_STATE1_DELAY, STAGE_STATE1_COMETS, STAGE_STATE1_GLOW,
  STAGE_STATE1_EXPAND, STAGE_STATE1_FADEOUT,
  STAGE_STATE2_FLICKER, STAGE_STATE2_BREATHE_INTRO, STAGE_STATE2_SOFT_FLICKER, STAGE_STATE2_HOLD,
  STAGE_STATE3_CHUNK_MOVE, STAGE_STATE3_CHUNK_EAT, STAGE_STATE3_EXPAND, STAGE_STATE3_HOLD
};
// Boots into State 0 (all LEDs off) by default - so any unplanned
// reset (power blip, USB replug, a control script closing its
// connection) lands on "off" instead of a random-looking animation.
Stage currentStage = STAGE_STATE0;

unsigned long state1StageStartTime = 0; // when the current State 1 sub-stage began
unsigned long state1TriggerTime = 0;    // when "state 1" was triggered (or last looped) -
                                         // fixed reference for the absolute deadlines
bool state1Comet2Started = false;
unsigned long state1Comet2StartTime = 0;
bool state1Comet3Started = false;
unsigned long state1Comet3StartTime = 0;

// FastLED.show() briefly disables interrupts on AVR while it bit-bangs
// the WS2812B timing - called back-to-back with no gap (as every
// continuously-animating hold/flicker state does), that was blocking
// serial reception often enough for whole commands to get silently
// dropped. Capping how often it actually runs leaves gaps for the
// UART to receive bytes reliably. ~50fps is still plenty smooth.
#define SHOW_INTERVAL_MS 20
unsigned long lastShowTime = 0;
void showThrottled() {
  unsigned long now = millis();
  if (now - lastShowTime < SHOW_INTERVAL_MS) return;
  lastShowTime = now;
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(A0));

  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(90);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 4000);

  FastLED.clear();
  FastLED.show(); // direct call here (not throttled) - this is the one-time
                   // boot clear, and the throttle's timer starting at 0
                   // would otherwise skip this very first call entirely.

  Serial.println(F("Ready. All LEDs off (State 0). Send 'state 0/1/2/3' to switch."));
}

void loop() {
  checkSerialCommand();

  switch (currentStage) {
    case STAGE_STATE0:         break; // all LEDs off, nothing to do each frame
    case STAGE_STATE1_DELAY:       doState1Delay(); break;
    case STAGE_STATE1_COMETS:      doState1Comets(); break;
    case STAGE_STATE1_GLOW:        doState1Glow(); break;
    case STAGE_STATE1_EXPAND:      doState1Expand(); break;
    case STAGE_STATE1_FADEOUT:     doState1FadeOut(); break;
    case STAGE_STATE2_FLICKER:      doState2Flicker(); break;
    case STAGE_STATE2_BREATHE_INTRO: doState2BreatheIntro(); break;
    case STAGE_STATE2_SOFT_FLICKER:  doState2SoftFlicker(); break;
    case STAGE_STATE2_HOLD:         doState2Hold(); break;
    case STAGE_STATE3_CHUNK_MOVE:   doState3ChunkMove(); break;
    case STAGE_STATE3_CHUNK_EAT:    doState3ChunkEat(); break;
    case STAGE_STATE3_EXPAND:       doState3Expand(); break;
    case STAGE_STATE3_HOLD:         doState3Hold(); break;
  }
}

// ---------- Read "state 0" / "state 1" / "state 2" / "state 3" from Serial Monitor and jump there ----------
// Uses a fixed-size char buffer instead of the Arduino String class on
// purpose - String does dynamic heap allocation on every command, which
// was crashing/hanging the board (this chip only has ~150 bytes of free
// RAM left, and that allocation was enough to break things silently -
// no response to any command, even right after a fresh reset).
#define CMD_BUFFER_SIZE 16
void checkSerialCommand() {
  static char cmdBuffer[CMD_BUFFER_SIZE];
  static uint8_t cmdLen = 0;

  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmdLen == 0) continue; // ignore blank lines (e.g. the \r before \n)

      // normalize in place: strip spaces, lowercase
      uint8_t writeIndex = 0;
      for (uint8_t i = 0; i < cmdLen; i++) {
        char ch = cmdBuffer[i];
        if (ch == ' ') continue;
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        cmdBuffer[writeIndex++] = ch;
      }
      cmdBuffer[writeIndex] = '\0';

      if (strcmp(cmdBuffer, "state0") == 0) {
        goToState(0);
      } else if (strcmp(cmdBuffer, "state1") == 0) {
        goToState(1);
      } else if (strcmp(cmdBuffer, "state2") == 0) {
        goToState(2);
      } else if (strcmp(cmdBuffer, "state3") == 0) {
        goToState(3);
      } else if (strncmp(cmdBuffer, "state", 5) == 0) {
        Serial.println(F("That state isn't built yet."));
      }

      cmdLen = 0;
      return; // handle one command per loop() call, same as before
    } else if (cmdLen < CMD_BUFFER_SIZE - 1) {
      cmdBuffer[cmdLen++] = c;
    }
    // else: buffer full, silently drop extra characters rather than overrun
  }
}

// ---------- Jump straight to a state ----------
void goToState(int stateNum) {
  if (stateNum == 0) {
    FastLED.clear();
    showThrottled();
    currentStage = STAGE_STATE0;
    Serial.println(F("Manual trigger: State 0 (all LEDs off)"));
  } else if (stateNum == 1) {
    startState1();
  } else if (stateNum == 2) {
    startState2Flicker();
  } else if (stateNum == 3) {
    startState3();
  }
}

// ---------- Ease-out cubic: fast start, sudden slow-down near the end ----------
float easeOutCubic(float t) {
  float f = t - 1.0;
  return f * f * f + 1.0;
}

// ---------- Start (or restart) State 1's whole sequence ----------
void startState1() {
  FastLED.clear();
  showThrottled();
  state1StageStartTime = millis();
  state1TriggerTime = state1StageStartTime;
  state1Comet2Started = false;
  state1Comet3Started = false;
  currentStage = STAGE_STATE1_DELAY;
  Serial.println(F("Manual trigger: State 1 - waiting before first comet..."));
}

// ---------- Nothing shown yet - just waiting out STATE1_START_DELAY_MS ----------
void doState1Delay() {
  if (millis() - state1StageStartTime >= STATE1_START_DELAY_MS) {
    state1StageStartTime = millis(); // comet 1's t=0
    state1Comet2Started = false;
    state1Comet3Started = false;
    currentStage = STAGE_STATE1_COMETS;
    Serial.println(F("State 1 comet 1 starting (fading in)..."));
  }
}

// ---------- Fade-in only (comet 1's own start, time-based) ----------
uint8_t state1CometFadeIn(unsigned long elapsedSinceStart, bool fadesIn) {
  if (fadesIn && elapsedSinceStart < STATE1_COMET_FADE_IN_MS) {
    return (uint8_t)(255.0 * elapsedSinceStart / STATE1_COMET_FADE_IN_MS);
  }
  return 255;
}

// ---------- Draws one comet's trail at a given (fractional) head position.
// Each pixel fades based on its OWN distance to pixel 66 - not the
// head's - so the tail visibly melts into that fixed point as pixels
// pass through the last fadeOutPx pixels, rather than the whole comet
// dimming as one block. fadeOutPx is passed in so different comets can
// use a different fade width. ----------
void drawState1Comet(float pos, uint8_t fadeInBrightness, int fadeOutPx) {
  int headPos = (int)pos;
  for (int offset = 0; offset < STATE1_CHUNK_SIZE; offset++) {
    int px = headPos - offset;
    if (px < 0 || px >= NUM_LEDS) continue;

    // Tail taper: brightest at the head (offset 0), fading out toward
    // the back of the trail.
    uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (STATE1_CHUNK_SIZE - 1));

    // Melt into pixel 66: this pixel's own distance to the endpoint,
    // regardless of where the head currently is.
    uint8_t endFade = 255;
    int distToEnd = STATE1_MID - px;
    if (distToEnd <= fadeOutPx) {
      float frac = (float)distToEnd / fadeOutPx;
      if (frac < 0) frac = 0;
      endFade = (uint8_t)(255.0 * frac);
    }

    CRGB c = PINK;
    c.nscale8(fadeInBrightness);
    c.nscale8(tailFade);
    c.nscale8(endFade);
    leds[px] = c;
  }
}

// ---------- Three comets, at constant (non-accelerating) speed. Each
// keeps finishing its own trail independently of the others - they can
// overlap on screen. Comet 3's own arrival at pixel 66 is what triggers
// the glow + expand (no fixed deadline anymore) - comets 1 and 2 always
// finish naturally well before comet 3 gets there. ----------
void doState1Comets() {
  unsigned long absElapsed = millis() - state1TriggerTime;

  FastLED.clear();

  // Comet 1: running since this stage began.
  unsigned long elapsed1 = millis() - state1StageStartTime;
  float pos1 = STATE1_COMET_SPEED_PXPMS * elapsed1;
  if (pos1 < STATE1_MID) {
    drawState1Comet(pos1, state1CometFadeIn(elapsed1, true), STATE1_COMET12_FADE_OUT_PX);
  }

  // Comet 2: enters at its own fixed timestamp, independent of comet 1.
  if (absElapsed >= STATE1_COMET2_TRIGGER_MS) {
    if (!state1Comet2Started) {
      state1Comet2Started = true;
      state1Comet2StartTime = millis();
      Serial.println(F("State 1 comet 2 entering (comet 1 still finishing)..."));
    }
    unsigned long elapsed2 = millis() - state1Comet2StartTime;
    float pos2 = STATE1_COMET_SPEED_PXPMS * elapsed2;
    if (pos2 < STATE1_MID) {
      drawState1Comet(pos2, state1CometFadeIn(elapsed2, false), STATE1_COMET12_FADE_OUT_PX);
    }
  }

  // Comet 3: enters at its own fixed timestamp - its OWN arrival at
  // pixel 66 is what triggers the glow + expand.
  if (absElapsed >= STATE1_COMET3_TRIGGER_MS) {
    if (!state1Comet3Started) {
      state1Comet3Started = true;
      state1Comet3StartTime = millis();
      Serial.println(F("State 1 comet 3 entering..."));
    }
    unsigned long elapsed3 = millis() - state1Comet3StartTime;
    float pos3 = STATE1_COMET_SPEED_PXPMS * elapsed3;
    if (pos3 < STATE1_MID) {
      drawState1Comet(pos3, state1CometFadeIn(elapsed3, false), STATE1_COMET3_FADE_OUT_PX);
    } else {
      state1StageStartTime = millis();
      currentStage = STAGE_STATE1_GLOW;
      Serial.println(F("State 1 comet 3 reached pixel 66 - glowing..."));
      return;
    }
  }

  showThrottled();
}

// ---------- Pixel 66 alone glows solid for a moment right as the last comet
// arrives - bridges the gap so "comet ends" and "expand begins" read as one
// continuous motion instead of a cut ----------
void doState1Glow() {
  FastLED.clear();
  leds[STATE1_MID] = PINK;
  showThrottled();

  if (millis() - state1StageStartTime >= STATE1_GLOW_MS) {
    state1StageStartTime = millis();
    currentStage = STAGE_STATE1_EXPAND;
    Serial.println(F("State 1 glow done - expanding..."));
  }
}

// ---------- State 1 lights up from the center outward, both ways, fast then slow ----------
void doState1Expand() {
  unsigned long elapsed = millis() - state1StageStartTime;
  float t = (float)elapsed / STATE1_EXPAND_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);

  // 6 pixels shorter on each side than the full segment.
  int fullHalf = (SEGMENT_END - SEGMENT_START) / 2 - STATE1_EXPAND_SHORTEN_PX;
  int currentHalf = (int)(fullHalf * eased);

  int rangeStart = STATE1_MID - currentHalf;
  int rangeEnd = STATE1_MID + currentHalf;
  if (rangeStart < SEGMENT_START) rangeStart = SEGMENT_START;
  if (rangeEnd > SEGMENT_END) rangeEnd = SEGMENT_END;

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    // Soft edges: dimmer near the growing front (rangeStart/rangeEnd),
    // full brightness toward the middle - not a hard-edged block.
    int distToEdge = min(i - rangeStart, rangeEnd - i);
    uint8_t brightness = 255;
    if (distToEdge < STATE1_EXPAND_EDGE_FADE_PX) {
      brightness = (uint8_t)(255.0 * distToEdge / STATE1_EXPAND_EDGE_FADE_PX);
    }
    CRGB c = PINK;
    c.nscale8(brightness);
    leds[i] = c;
  }
  showThrottled();

  if (t >= 1.0) {
    state1StageStartTime = millis();
    currentStage = STAGE_STATE1_FADEOUT;
    Serial.println(F("State 1 fully expanded - fading out..."));
  }
}

// ---------- Fade the fully-expanded segment to black, then go dark (State 0) ----------
void doState1FadeOut() {
  unsigned long elapsed = millis() - state1StageStartTime;
  float t = (float)elapsed / STATE1_FADEOUT_MS;
  if (t > 1.0) t = 1.0;

  uint8_t brightness = (uint8_t)(255.0 * (1.0 - t));
  CRGB c = PINK;
  c.nscale8(brightness);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();

  if (t >= 1.0) {
    // Loop the whole sequence again instead of going dark - same
    // delay, three comets, glow, expand, fade out, repeating until a
    // different state command interrupts it.
    FastLED.clear();
    showThrottled();
    state1StageStartTime = millis();
    state1TriggerTime = state1StageStartTime;
    state1Comet2Started = false;
    state1Comet3Started = false;
    currentStage = STAGE_STATE1_DELAY;
    Serial.println(F("State 1 sequence complete - looping..."));
  }
}
