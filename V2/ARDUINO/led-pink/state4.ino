/*
  State 4 (V2) - the old COM7 "station2" light show, now running on COM8's
  single strip. A 60-pixel section (STATE2_SEGMENT_START..END, the same
  pixels State 2 uses) in State 2's white (STATE2_COLOR):
    - 2 mild flickers at the start (50% dip, at 667ms and 900ms)
    - then a slow idle breathe, forever: dims to 80% over 20s, recovers
      over 3s, repeating
  This tab is combined into the same program as led-pink.ino automatically
  (Arduino merges every .ino in the folder), so it uses leds[], currentStage,
  showThrottled() etc. from there. Triggered by the "state4" Serial command.
*/

#define STATE4_FLICKER_BLIP_MS 60
const unsigned long STATE4_FLICKER_TIMES_MS[] = { 667, 900 };
const uint8_t STATE4_FLICKER_DIP_BRIGHTNESS[] = { 128, 128 };  // 50% dips
#define STATE4_FLICKER_COUNT (sizeof(STATE4_FLICKER_TIMES_MS) / sizeof(unsigned long))

#define STATE4_BREATHE_DIM_MS       20000
#define STATE4_BREATHE_RECOVER_MS    3000
#define STATE4_BREATHE_CYCLE_MS     (STATE4_BREATHE_DIM_MS + STATE4_BREATHE_RECOVER_MS)
#define STATE4_BREATHE_DIM_FRACTION  0.8f

unsigned long state4StartTime = 0;
int state4FlickerIndex = 0;
bool state4FlickerBlipActive = false;
unsigned long state4FlickerBlipStartElapsed = 0;

// ---------- Start (or restart) State 4 ----------
void startState4() {
  FastLED.clear();
  FastLED.show();
  state4StartTime = millis();
  state4FlickerIndex = 0;
  state4FlickerBlipActive = false;
  currentStage = STAGE_STATE4_FLICKER;
  Serial.println(F("Manual trigger: State 4 - flicker intro..."));
}

// ---------- 2 mild flickers on the section at full brightness, then the breathing hold ----------
void doState4Flicker() {
  unsigned long elapsed = millis() - state4StartTime;
  bool inBlip = false;

  if (state4FlickerBlipActive) {
    if (elapsed - state4FlickerBlipStartElapsed >= STATE4_FLICKER_BLIP_MS) {
      state4FlickerBlipActive = false;
      state4FlickerIndex++;
    } else {
      inBlip = true;
    }
  } else if (state4FlickerIndex < STATE4_FLICKER_COUNT && elapsed >= STATE4_FLICKER_TIMES_MS[state4FlickerIndex]) {
    state4FlickerBlipActive = true;
    state4FlickerBlipStartElapsed = elapsed;
    inBlip = true;
  }

  CRGB c = STATE2_COLOR;
  if (inBlip) {
    c.nscale8(STATE4_FLICKER_DIP_BRIGHTNESS[state4FlickerIndex]);
  }

  FastLED.clear();
  for (int i = STATE2_SEGMENT_START; i <= STATE2_SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();

  if (!state4FlickerBlipActive && state4FlickerIndex >= STATE4_FLICKER_COUNT) {
    state4StartTime = millis();  // the breathe cycle starts fresh from here
    currentStage = STAGE_STATE4_HOLD;
    Serial.println(F("State 4 flicker done - section breathing..."));
  }
}

// ---------- Slow breathe cycle (dims to 80% over 20s, recovers over 3s), forever ----------
void doState4Hold() {
  unsigned long cyclePos = (millis() - state4StartTime) % STATE4_BREATHE_CYCLE_MS;
  float breatheFraction;
  if (cyclePos < STATE4_BREATHE_DIM_MS) {
    float t = (float)cyclePos / STATE4_BREATHE_DIM_MS;
    breatheFraction = 1.0f - (1.0f - STATE4_BREATHE_DIM_FRACTION) * t;
  } else {
    float t = (float)(cyclePos - STATE4_BREATHE_DIM_MS) / STATE4_BREATHE_RECOVER_MS;
    breatheFraction = STATE4_BREATHE_DIM_FRACTION + (1.0f - STATE4_BREATHE_DIM_FRACTION) * t;
  }

  CRGB c = STATE2_COLOR;
  c.nscale8((uint8_t)(breatheFraction * 255));

  FastLED.clear();
  for (int i = STATE2_SEGMENT_START; i <= STATE2_SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();
}
