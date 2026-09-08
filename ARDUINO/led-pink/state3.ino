/*
  State 3 - opens with State 2's full 60-pixel segment lit, shrinking
  ("de-expanding") down into a single pixel at 66, then a comet that
  moves like State 1's (fades in, tapered trail, melts into its target
  point) instead of State 1's short middle-segment comet - here it
  launches from pixel 66, first swings DOWN 10 pixels (a quick wind-up,
  STATE3_SWING_DOWN_PX/STATE3_SWING_MS) then heads forward all the way
  to the physical end of the strip - still arriving there at exactly
  the same moment as before the swing was added (STATE3_COMET_TRAVEL_MS,
  1200ms after launch) - repeating 5 times, then expand + breathing hold
  like State 1. Has its own cool blue-white color (STATE3_COLOR), more
  blue than State 2's plain white - no longer tied to STATE2_COLOR.
  --------------------------------------------------------------------
  This tab is combined into the same program as led-pink.ino
  automatically (Arduino sketches merge every .ino file in the
  folder) - it reuses State 1's own timing constants and helpers
  (SEGMENT_START/END, STATE1_MID, EAT_DURATION_MS, STATE_EXPAND_MS,
  easeOutCubic(), state1CometFadeIn()), plus State 2's segment
  (STATE2_SEGMENT_START/END) for the intro shrink.

  Triggered by the "state 3" Serial command.
*/

#define STATE3_CHUNK_REPEATS 5

#define STATE3_START_PIXEL    66  // where each comet pass launches from - the
                                   // same point the intro shrink converges to
#define STATE3_TRAIL_LENGTH   30  // same length as State 1's STATE1_CHUNK_SIZE
#define STATE3_COMET_END       (NUM_LEDS - 1)  // comet travels all the way to the
                                                // physical end of the strip, past
                                                // SEGMENT_END (95) - only the comet
                                                // goes out this far; the expand
                                                // afterward still fills SEGMENT_START..SEGMENT_END
#define STATE3_COMET_FADE_OUT_PX  4            // melt-into-target width (State 1 uses 4)

// Constant-speed comet, same motion shape as State 1's (see
// STATE1_COMET_SPEED_PXPMS) - just covering the longer 66-to-end-of-strip
// distance in less time, to keep State 3 feeling faster/punchier.
#define STATE3_COMET_TRAVEL_MS  1200

// Before launching forward, the comet now winds up with a quick swing
// DOWN by STATE3_SWING_DOWN_PX (10) pixels - the head still reaches
// STATE3_COMET_END at exactly STATE3_COMET_TRAVEL_MS (1200ms) elapsed
// from launch, same as before this was added, so the forward phase's
// speed is recomputed to cover the extra distance (the 10px dip-back,
// on top of the original 66->199 span) in whatever time is left over
// after the dip. Dip duration not specified - picked a quick, snappy
// wind-up.
#define STATE3_SWING_DOWN_PX      10
#define STATE3_SWING_MS          150
#define STATE3_SWING_TARGET      (STATE3_START_PIXEL - STATE3_SWING_DOWN_PX)  // 56
#define STATE3_FORWARD_MS        (STATE3_COMET_TRAVEL_MS - STATE3_SWING_MS)   // 1050ms
#define STATE3_FORWARD_SPEED_PXPMS  ((float)(STATE3_COMET_END - STATE3_SWING_TARGET) / STATE3_FORWARD_MS)

// Brief solid glow at the target point bridging one comet pass into the
// next (same idea as State 1's doState1Glow(), just shorter since this
// repeats 7 times and needs to stay snappy).
#define STATE3_GLOW_MS  100

// Each comet launches on a fixed schedule, measured from the moment the
// comet sequence begins (right after the shrink completes) - not back-
// to-back. Comet 1 starts at 00:00:00:12 (400ms) and comet 2 at
// 00:00:02:14 (2467ms); that ~2067ms gap is then reused as the interval
// between every comet after, all 6 evenly spaced. If a pass's travel +
// glow (1300ms) finishes before the next scheduled start, the strip
// just goes dark and waits.
#define STATE3_COMET1_START_MS    400
#define STATE3_COMET_INTERVAL_MS  2067

// Intro: starts lit across State 2's full 60-pixel segment, then shrinks
// ("de-expands") down into a single pixel at STATE3_START_PIXEL (66) -
// that's where the first comet then launches from.
#define STATE3_SHRINK_MS  1000
#define STATE3_SHRINK_EDGE_FADE_PX  6  // soft dim at the retreating edges (same idea as
                                        // State 1's STATE1_EXPAND_EDGE_FADE_PX), so the
                                        // outermost pixels look like they're dimming out
                                        // rather than snapping off

CRGB STATE3_COLOR = CRGB(150, 180, 255); // more blue than State 2's white

int state3ChunkRepeatIndex = 0;
unsigned long state3StageStartTime = 0;
unsigned long state3ExpandStartTime = 0;
unsigned long state3ShrinkStartTime = 0;
unsigned long state3CometPhaseStartTime = 0; // t=0 for the fixed comet launch schedule
bool state3CometLaunched = false;            // true once the CURRENT comet's travel has begun

// ---------- Start (or restart) State 3 ----------
void startState3() {
  FastLED.clear();
  showThrottled();
  state3ChunkRepeatIndex = 0;
  state3ShrinkStartTime = millis();
  currentStage = STAGE_STATE3_SHRINK;
  Serial.println(F("Manual trigger: State 3 - shrinking to center..."));
}

// ---------- The scheduled start time (ms since the comet phase began) for the Nth comet ----------
unsigned long state3CometScheduledStart(int index) {
  return STATE3_COMET1_START_MS + (unsigned long)index * STATE3_COMET_INTERVAL_MS;
}

// ---------- Intro: all 60 of State 2's pixels lit, shrinking down into a
// single pixel at STATE3_START_PIXEL (66) ----------
void doState3Shrink() {
  unsigned long elapsed = millis() - state3ShrinkStartTime;
  float t = (float)elapsed / STATE3_SHRINK_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);

  // The 60-pixel segment isn't perfectly centered on 66 (29 on one side,
  // 30 on the other) - shrink each side down proportionally so both
  // reach exactly 66 at the same time.
  int halfLow  = STATE3_START_PIXEL - STATE2_SEGMENT_START;
  int halfHigh = STATE2_SEGMENT_END - STATE3_START_PIXEL;
  int currentHalfLow  = (int)(halfLow  * (1.0 - eased));
  int currentHalfHigh = (int)(halfHigh * (1.0 - eased));

  int rangeStart = STATE3_START_PIXEL - currentHalfLow;
  int rangeEnd   = STATE3_START_PIXEL + currentHalfHigh;

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    // Soft edges: dimmer near the retreating ends (rangeStart/rangeEnd),
    // full brightness toward the middle - looks like dimming lights
    // rather than a hard cutoff as the segment shrinks.
    int distToEdge = min(i - rangeStart, rangeEnd - i);
    uint8_t brightness = 255;
    if (distToEdge < STATE3_SHRINK_EDGE_FADE_PX) {
      brightness = (uint8_t)(255.0 * distToEdge / STATE3_SHRINK_EDGE_FADE_PX);
    }
    CRGB c = STATE3_COLOR;
    c.nscale8(brightness);
    leds[i] = c;
  }
  showThrottled();

  if (t >= 1.0) {
    state3CometPhaseStartTime = millis();
    state3CometLaunched = false;
    currentStage = STAGE_STATE3_COMET;
    Serial.println(F("State 3 shrink complete - comet x5 starting..."));
  }
}

// ---------- Draws the comet's trail at a given (fractional) head
// position - same shape as State 1's drawState1Comet(), just melting
// into STATE3_COMET_END (the far end of the strip) instead of pixel 66,
// and using STATE3_COLOR instead of PINK. Lower bound is
// STATE3_SWING_TARGET (56), not STATE3_START_PIXEL (66) - the comet now
// dips 10px below its launch point during the swing-down wind-up
// before heading forward, so the trail needs room to show there too. ----------
void drawState3Comet(float pos, uint8_t fadeInBrightness) {
  int headPos = (int)pos;
  for (int offset = 0; offset < STATE3_TRAIL_LENGTH; offset++) {
    int px = headPos - offset;
    if (px < STATE3_SWING_TARGET || px >= NUM_LEDS) continue;

    uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (STATE3_TRAIL_LENGTH - 1));

    uint8_t endFade = 255;
    int distToEnd = STATE3_COMET_END - px;
    if (distToEnd <= STATE3_COMET_FADE_OUT_PX) {
      float frac = (float)distToEnd / STATE3_COMET_FADE_OUT_PX;
      if (frac < 0) frac = 0;
      endFade = (uint8_t)(255.0 * frac);
    }

    CRGB c = STATE3_COLOR;
    c.nscale8(fadeInBrightness);
    c.nscale8(tailFade);
    c.nscale8(endFade);
    leds[px] = c;
  }
}

// ---------- Comet's head position at a given elapsed time since launch -
// first swings DOWN from STATE3_START_PIXEL to STATE3_SWING_TARGET over
// STATE3_SWING_MS (a quick wind-up), then heads forward from there to
// STATE3_COMET_END (and beyond, uncapped, same as before - the tail
// catching up is what ends the pass) at STATE3_FORWARD_SPEED_PXPMS,
// timed so the head reaches STATE3_COMET_END at exactly
// STATE3_COMET_TRAVEL_MS (1200ms) total elapsed, same as before the
// swing was added. ----------
float state3CometPos(unsigned long elapsedMs) {
  if (elapsedMs < STATE3_SWING_MS) {
    float t = (float)elapsedMs / STATE3_SWING_MS;
    return STATE3_START_PIXEL - STATE3_SWING_DOWN_PX * t;
  }
  unsigned long forwardElapsed = elapsedMs - STATE3_SWING_MS;
  return STATE3_SWING_TARGET + STATE3_FORWARD_SPEED_PXPMS * forwardElapsed;
}

// ---------- One comet pass - fades in, swings down then travels forward
// (see state3CometPos()), and melts into the target as its tail arrives
// (not just the head), so the whole comet has visibly arrived before the
// glow bridges to the next pass. Doesn't launch until its own scheduled
// moment on the fixed comet timeline (see STATE3_COMET1_START_MS/
// STATE3_COMET_INTERVAL_MS) - stays dark until then. ----------
void doState3Comet() {
  if (!state3CometLaunched) {
    unsigned long phaseElapsed = millis() - state3CometPhaseStartTime;
    if (phaseElapsed < state3CometScheduledStart(state3ChunkRepeatIndex)) {
      FastLED.clear();
      showThrottled();
      return; // waiting for this comet's scheduled start
    }
    state3CometLaunched = true;
    state3StageStartTime = millis();
  }

  unsigned long absElapsed = millis() - state3StageStartTime;

  FastLED.clear();

  float pos = state3CometPos(absElapsed);
  float tailPos = pos - (STATE3_TRAIL_LENGTH - 1);
  if (tailPos < STATE3_COMET_END) {
    drawState3Comet(pos, state1CometFadeIn(absElapsed, true));
    showThrottled();
  } else {
    state3StageStartTime = millis();
    state3CometLaunched = false; // reset - gates the NEXT comet's scheduled wait
    currentStage = STAGE_STATE3_GLOW;
  }
}

// ---------- Brief solid glow at the target point, bridging one comet
// pass into the next (or into the expand, on the last pass) - same idea
// as State 1's doState1Glow(). ----------
void doState3Glow() {
  FastLED.clear();
  leds[STATE3_COMET_END] = STATE3_COLOR;
  showThrottled();

  if (millis() - state3StageStartTime >= STATE3_GLOW_MS) {
    state3ChunkRepeatIndex++;

    if (state3ChunkRepeatIndex >= STATE3_CHUNK_REPEATS) {
      // Expand is intentionally skipped here - it now plays on a second
      // Arduino/strip (COM7) instead. Going dark and holding, same as
      // how State 1 settles after its own sequence finishes.
      FastLED.clear();
      FastLED.show();
      currentStage = STAGE_STATE0;
      Serial.println(F("All 5 State 3 comets done - expansion runs on the 2nd strip now, going dark..."));
    } else {
      state3StageStartTime = millis();
      currentStage = STAGE_STATE3_COMET;
      Serial.print(F("State 3 comet pass "));
      Serial.println(state3ChunkRepeatIndex + 1);
    }
  }
}

// ---------- State 3 lights up from the center outward, fast then slow (same as State 1) ----------
void doState3Expand() {
  unsigned long elapsed = millis() - state3ExpandStartTime;
  float t = (float)elapsed / STATE_EXPAND_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);

  int fullHalf = (SEGMENT_END - SEGMENT_START) / 2;
  int currentHalf = (int)(fullHalf * eased);

  int rangeStart = STATE1_MID - currentHalf;
  int rangeEnd = STATE1_MID + currentHalf;
  if (rangeStart < SEGMENT_START) rangeStart = SEGMENT_START;
  if (rangeEnd > SEGMENT_END) rangeEnd = SEGMENT_END;

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    leds[i] = STATE3_COLOR;
  }
  showThrottled();

  if (t >= 1.0) {
    currentStage = STAGE_STATE3_HOLD;
    Serial.println(F("State 3 fully lit - breathing until next command..."));
  }
}

// ---------- State 3: solid color that breathes, holds forever (same rhythm as State 1) ----------
void doState3Hold() {
  uint8_t breath = beatsin8(9, 20, 255);
  CRGB c = STATE3_COLOR;
  c.nscale8(breath);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();
}
