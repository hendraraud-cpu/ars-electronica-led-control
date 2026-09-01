/*
  State 2 - scripted timeline, timed from the moment State 2 starts
  --------------------------------------------------------------------
  This tab is combined into the same program as led-pink.ino
  automatically (Arduino sketches merge every .ino file in the
  folder) - it can freely use anything defined over there (leds[],
  SEGMENT_START/END, currentStage, the Stage enum, etc) with no
  #include needed.

  TIMELINE (all times relative to State 2 starting, i.e. "state 2"
  being triggered) - fast, sub-2-second sequence, timestamps given as
  video-editor timecode HH:MM:SS:FF at 30fps and converted to ms:
    00:00:00.000            - turns on abruptly, full brightness, no fade
    00:00:00:20 (667ms) - 00:00:01:04 (1133ms) - 3 brief flickers
                          spread across this window (667, 900, 1133ms)
    (1133ms - 1700ms)   - pause, steady on, no flicker
    00:00:01:21 (1700ms) - 1 final flicker (end of the "hard" flicker
                          timeline - full on/off blips)
    +15s                - soft flickering for 15 seconds: very fast,
                          continuous, each flick only dips brightness
                          by 20% (never goes darker than that)
    +10s more           - a quick smooth breathing pulse plays for
                          10 seconds (dips ~20%, continuous sine-like
                          pulse, no fixed cadence like the idle one)
    after that          - settles into an idle hold: NOT perfectly
                          static, two things layered on top of the
                          base #F2F7FF color, forever, until a real
                          trigger, later:
                            - a slow breathe: dims 20% over 20s, then
                              recovers back to full over 3s, repeating
                            - an occasional quick flick every 40-50s
                              (randomized), just a brief 20% brightness
                              dip on top of whatever the breathe is
                              doing at that moment
*/

#define STATE2_FLICKER_BLIP_MS 120

// Quick breathing pulse right after the flicker timeline, before
// settling into the slower idle breathe below.
#define BREATHE_INTRO_DURATION_MS 10000
#define BREATHE_INTRO_BPM         18     // pace of the pulse (beatsin8 "beats per minute")
#define BREATHE_INTRO_MIN         204    // 80% of 255 - same 20% dip depth
#define BREATHE_INTRO_MAX         255

// Soft flickering, right after the breathing pulse: fast, continuous,
// each flick only dips 20% (never goes darker than that).
#define SOFT_FLICKER_DURATION_MS 15000
#define SOFT_FLICKER_BPM         480   // fast - about 8 flickers per second
#define SOFT_FLICKER_MIN         204   // 80% of 255 - the -20% floor
#define SOFT_FLICKER_MAX         255

// Idle hold: slow breathe cycle
#define BREATHE_DIM_MS        20000  // fades down to 80% over this long
#define BREATHE_RECOVER_MS    3000   // then back up to 100% over this long
#define BREATHE_CYCLE_MS      (BREATHE_DIM_MS + BREATHE_RECOVER_MS)
#define BREATHE_DIM_FRACTION  0.8f   // 20% decrease at the bottom of the breathe

// Idle hold: occasional quick flick (just a dip, not a full off-blip)
#define IDLE_FLICK_MIN_MS       40000
#define IDLE_FLICK_MAX_MS       50000
#define IDLE_FLICK_DURATION_MS  150
#define IDLE_FLICK_FRACTION     0.8f  // 20% decrease during the flick

// Exact moments (ms since State 2 started) each flicker happens.
const unsigned long STATE2_FLICKER_TIMES_MS[] = {
  667,   // 00:00:00:20 at 30fps - 1st of the 3
  900,   // midpoint of the 667ms-1133ms window - 2nd of the 3
  1133,  // 00:00:01:04 at 30fps - 3rd of the 3
  1700,  // 00:00:01:21 at 30fps - final single flicker, after the pause
};
#define STATE2_FLICKER_COUNT (sizeof(STATE2_FLICKER_TIMES_MS) / sizeof(unsigned long))

CRGB STATE2_COLOR = CRGB::White; // #FFFFFF - State 2 color

unsigned long state2StartTime = 0;
int nextFlickerIndex = 0;
bool flickerBlipActive = false;
unsigned long flickerBlipStartElapsed = 0;

unsigned long state2HoldStartTime = 0;
unsigned long nextIdleFlickTime = 0;
bool idleFlickActive = false;
unsigned long idleFlickStartTime = 0;

unsigned long breatheIntroStartTime = 0;
unsigned long softFlickerStartTime = 0;

// ---------- Start (or restart) State 2's timeline ----------
void startState2Flicker() {
  FastLED.clear();
  currentStage = STAGE_STATE2_FLICKER;
  state2StartTime = millis();
  nextFlickerIndex = 0;
  flickerBlipActive = false;

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = STATE2_COLOR;
  }
  showThrottled();

  Serial.println(F("State 2 on - abrupt, full brightness. Timeline running..."));
}

// ---------- Runs the whole scripted timeline, then hands off to the steady hold ----------
void doState2Flicker() {
  unsigned long elapsed = millis() - state2StartTime;
  bool showOff = false;

  if (flickerBlipActive) {
    if (elapsed - flickerBlipStartElapsed >= STATE2_FLICKER_BLIP_MS) {
      flickerBlipActive = false;
      nextFlickerIndex++;
    } else {
      showOff = true;
    }
  } else if (nextFlickerIndex < STATE2_FLICKER_COUNT && elapsed >= STATE2_FLICKER_TIMES_MS[nextFlickerIndex]) {
    flickerBlipActive = true;
    flickerBlipStartElapsed = elapsed;
    showOff = true;
  }

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = showOff ? CRGB::Black : STATE2_COLOR;
  }
  showThrottled();

  if (!flickerBlipActive && nextFlickerIndex >= STATE2_FLICKER_COUNT) {
    currentStage = STAGE_STATE2_SOFT_FLICKER;
    softFlickerStartTime = millis();
    Serial.println(F("State 2 hard flicker timeline complete - 15s soft flicker..."));
  }
}

// ---------- Soft flicker, 15 seconds, right after the hard flicker timeline ----------
void doState2SoftFlicker() {
  if (millis() - softFlickerStartTime >= SOFT_FLICKER_DURATION_MS) {
    currentStage = STAGE_STATE2_BREATHE_INTRO;
    breatheIntroStartTime = millis();
    Serial.println(F("State 2 soft flicker complete - 10s breathing pulse..."));
    return;
  }

  uint8_t brightness = beatsin8(SOFT_FLICKER_BPM, SOFT_FLICKER_MIN, SOFT_FLICKER_MAX);
  CRGB c = STATE2_COLOR;
  c.nscale8(brightness);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();
}

// ---------- Quick smooth breathing pulse, 10 seconds, right after the soft flicker ----------
void doState2BreatheIntro() {
  if (millis() - breatheIntroStartTime >= BREATHE_INTRO_DURATION_MS) {
    enterState2Hold();
    return;
  }

  uint8_t brightness = beatsin8(BREATHE_INTRO_BPM, BREATHE_INTRO_MIN, BREATHE_INTRO_MAX);
  CRGB c = STATE2_COLOR;
  c.nscale8(brightness);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();
}

// ---------- Switch into State 2's idle hold and arm the breathe + next quick flick ----------
void enterState2Hold() {
  currentStage = STAGE_STATE2_HOLD;
  state2HoldStartTime = millis();
  idleFlickActive = false;
  nextIdleFlickTime = millis() + random(IDLE_FLICK_MIN_MS, IDLE_FLICK_MAX_MS + 1);
  Serial.println(F("State 2 timeline complete - idle hold: breathing, occasional flick..."));
}

// ---------- State 2 idle hold: slow breathe + occasional quick dip, forever ----------
void doState2Hold() {
  unsigned long now = millis();

  // Slow breathe: 100% -> 80% over 20s, then 80% -> 100% over 3s, repeating
  unsigned long cyclePos = (now - state2HoldStartTime) % BREATHE_CYCLE_MS;
  float breatheFraction;
  if (cyclePos < BREATHE_DIM_MS) {
    float t = (float)cyclePos / BREATHE_DIM_MS;
    breatheFraction = 1.0f - (1.0f - BREATHE_DIM_FRACTION) * t;
  } else {
    float t = (float)(cyclePos - BREATHE_DIM_MS) / BREATHE_RECOVER_MS;
    breatheFraction = BREATHE_DIM_FRACTION + (1.0f - BREATHE_DIM_FRACTION) * t;
  }

  // Occasional quick flick: brief 20% dip layered on top of the breathe, every 40-50s
  if (idleFlickActive) {
    if (now - idleFlickStartTime >= IDLE_FLICK_DURATION_MS) {
      idleFlickActive = false;
      nextIdleFlickTime = now + random(IDLE_FLICK_MIN_MS, IDLE_FLICK_MAX_MS + 1);
    }
  } else if (now >= nextIdleFlickTime) {
    idleFlickActive = true;
    idleFlickStartTime = now;
  }

  float finalFraction = breatheFraction * (idleFlickActive ? IDLE_FLICK_FRACTION : 1.0f);
  uint8_t brightnessByte = (uint8_t)(finalFraction * 255);

  CRGB c = STATE2_COLOR;
  c.nscale8(brightnessByte);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();
}
