/*
  State 3 - same shape as State 1 (comet + expand + breathing hold),
  but the comet runs 6 times instead of 2, and it's purple instead
  of pink.
  --------------------------------------------------------------------
  This tab is combined into the same program as led-pink.ino
  automatically (Arduino sketches merge every .ino file in the
  folder) - it reuses State 1's own timing constants for everything
  except comet speed (SEGMENT_START/END, STATE1_MID, CHUNK_SIZE,
  EAT_DURATION_MS, STATE_EXPAND_MS, easeOutCubic()), but has its own
  color (STATE3_COLOR) and its own, faster comet pace
  (STATE3_SLOW_DELAY_MS / STATE3_FAST_DELAY_MS below) instead of
  reusing State 1's PINK / SLOW_DELAY_MS / FAST_DELAY_MS.

  Triggered by the "state 3" Serial command.
*/

#define STATE3_CHUNK_REPEATS 6

#define STATE3_SLOW_DELAY_MS 25  // pace at the very start (State 1 uses 90)
#define STATE3_FAST_DELAY_MS  4  // pace near the center (State 1 uses 14)

#define STATE3_START_PIXEL    88  // where each comet pass launches from
#define STATE3_TRAIL_LENGTH    9  // short trail (vs the usual CHUNK_SIZE of 24)
#define STATE3_COMET_END       (NUM_LEDS - 1)  // comet travels all the way to the
                                                // physical end of the strip, past
                                                // SEGMENT_END (95) - only the comet
                                                // goes out this far; the expand
                                                // afterward still fills SEGMENT_START..SEGMENT_END

CRGB STATE3_COLOR = CRGB(0x9B, 0x30, 0xFF); // vivid purple

int state3HeadPixel = 0;
int state3ChunkRepeatIndex = 0;
unsigned long state3LastMoveStep = 0;
unsigned long state3EatStartTime = 0;
unsigned long state3ExpandStartTime = 0;

// ---------- Start (or restart) State 3 ----------
void startState3() {
  FastLED.clear();
  showThrottled();
  state3HeadPixel = STATE3_START_PIXEL;
  state3ChunkRepeatIndex = 0;
  currentStage = STAGE_STATE3_CHUNK_MOVE;
  Serial.println(F("Manual trigger: State 3 (comet x6)"));
}

// ---------- Moving chunk: starts at pixel 88 and travels outward all the
// way to the physical end of the strip (STATE3_COMET_END), slow -> fast as
// it nears the end (same pace shape as State 1, just flipped so the comet
// launches FROM its start point instead of arriving at it), with a short
// trail. ----------
void doState3ChunkMove() {
  float progress = (float)(state3HeadPixel - STATE3_START_PIXEL) / (float)(STATE3_COMET_END - STATE3_START_PIXEL);
  if (progress > 1.0) progress = 1.0;

  int stepDelay = STATE3_SLOW_DELAY_MS - (int)((STATE3_SLOW_DELAY_MS - STATE3_FAST_DELAY_MS) * progress);

  if (millis() - state3LastMoveStep < (unsigned long)stepDelay) return;
  state3LastMoveStep = millis();

  FastLED.clear();

  if (state3HeadPixel < STATE3_COMET_END) {
    for (int offset = 0; offset < STATE3_TRAIL_LENGTH; offset++) {
      int px = state3HeadPixel - offset;
      if (px < 0 || px >= NUM_LEDS) continue;
      leds[px] = STATE3_COLOR;
    }
    showThrottled();
    state3HeadPixel++;
  } else {
    state3EatStartTime = millis();
    currentStage = STAGE_STATE3_CHUNK_EAT;
    Serial.println(F("State 3 chunk reached the end - being eaten..."));
  }
}

// ---------- Chunk shrinks from the tail while front stays anchored at the strip's end ----------
void doState3ChunkEat() {
  unsigned long elapsed = millis() - state3EatStartTime;
  float t = (float)elapsed / EAT_DURATION_MS;
  if (t > 1.0) t = 1.0;

  int currentLength = STATE3_TRAIL_LENGTH - (int)(STATE3_TRAIL_LENGTH * t);

  FastLED.clear();
  for (int offset = 0; offset < currentLength; offset++) {
    int px = STATE3_COMET_END - offset;
    if (px < 0 || px >= NUM_LEDS) continue;
    leds[px] = STATE3_COLOR;
  }
  showThrottled();

  if (t >= 1.0) {
    state3HeadPixel = STATE3_START_PIXEL;
    state3ChunkRepeatIndex++;

    if (state3ChunkRepeatIndex >= STATE3_CHUNK_REPEATS) {
      state3ExpandStartTime = millis();
      currentStage = STAGE_STATE3_EXPAND;
      Serial.println(F("All 6 State 3 chunks consumed - expanding..."));
    } else {
      currentStage = STAGE_STATE3_CHUNK_MOVE;
      Serial.print(F("State 3 chunk consumed - starting pass "));
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

// ---------- State 3: solid purple that breathes, holds forever (same rhythm as State 1) ----------
void doState3Hold() {
  uint8_t breath = beatsin8(9, 20, 255);
  CRGB c = STATE3_COLOR;
  c.nscale8(breath);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();
}
