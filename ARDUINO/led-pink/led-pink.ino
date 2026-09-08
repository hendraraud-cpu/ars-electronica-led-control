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

  STATE 1 has 3 sections, named a/b/c, matching fixed timestamps in the
  43-second audio track (STATE1_SECTION_START_MS, all measured from the
  "state1" trigger = the track starting):
    a = 0ms (0:00)  b = 00:00:11:20 (11667ms)  c = 21000ms (0:21, LAST)
  Each section plays the SAME comet+glow+expand sequence below, from its
  own start. Sections a and b get cut off 1.5s before the NEXT section
  starts (STATE1_SECTION_FADE_MS) - regardless of which part of the
  sequence they've reached at that moment - fading whatever's currently
  lit to black over that 1.5s, then the next section starts fresh
  exactly on its timestamp. Section c has no section after it, so it
  just plays out its own natural sequence, then HOLDS its expand
  (doesn't fade) - solid at first, breathing from STATE1_BREATHE_START_MS
  onward - until STATE1_HOLD_FADEOUT_START_MS, when it finally fades to
  black (its own duration, STATE1_HOLD_FADEOUT_MS) and holds dark,
  matching the end of the 43s track.

  ONE SECTION'S SEQUENCE (timestamps relative to ITS OWN start):
    0ms                    - nothing shown yet (STATE1_START_DELAY_MS
                             = 1133ms)
    1133ms                 - the (single) comet fades in, longer trail
                             than usual (STATE1_CHUNK_SIZE), travels
                             toward pixel 66 at a CONSTANT speed
                             (STATE1_COMET_SPEED_PXPMS - no easing, no
                             acceleration, steady/hypnotic), fading over
                             the last STATE1_COMET_FADE_OUT_PX pixels.
    comet's TAIL (not just  - THIS is what triggers the next stage now
    its head) reaches pixel   (no fixed deadline) - it keeps
    66                        moving/drawing until the whole tail (not
                             just the head) has passed 66, so the whole
                             comet has visibly arrived first.
    -> pixel 66 glows solid for STATE1_GLOW_MS (300ms), then the segment
       expands from that same pixel outward, both ways, fast-then-slow,
       over STATE1_EXPAND_MS (1900ms)
    fully expanded (section c only) - holds/breathes (see doState1Hold()),
                             then eventually fades the whole segment to
                             black over STATE1_HOLD_FADEOUT_MS (4500ms),
                             then holds dark and WAITS for the next
                             "state1" trigger (live_control.py re-sends
                             it when the whole 43s track finishes and loops)

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
#define STATE1_START_DELAY_MS     133   // 00:00:01:04 shifted 1s earlier - nothing shown before the comet
// No easing, no acceleration, steady/hypnotic. Speed is derived from
// covering the full 66-pixel distance in this many ms.
#define STATE1_COMET_TRAVEL_MS    4038  // sped up ~2px/s (was 4600ms => ~14.3px/s, now ~16.3px/s)
#define STATE1_COMET_SPEED_PXPMS  ((float)STATE1_MID / STATE1_COMET_TRAVEL_MS)
// Section c only: faster comet, timed backward from a specific target -
// its full arrival (head+trail, 95px) needs to happen early enough that
// (arrival + STATE1_GLOW_MS) lands expand's start exactly at 00:00:26:00
// (26000ms absolute, 5000ms into section c, which starts at 21000ms).
#define STATE1_SECTION_C_COMET_TRAVEL_MS 3173
// Right when the comet ends, pixel 66 alone glows solid for a
// moment - then the expand continues from that exact same single
// pixel, so there's no gap/cut between "comet arrives" and "expand
// begins" (not an exact given duration - picked a quick, natural hold).
#define STATE1_GLOW_MS             300
#define STATE1_EXPAND_MS          1900  // the "both ways" reveal - sped up for more velocity
#define STATE1_EXPAND_SHORTEN_PX     6  // max reach is 6px short of the full segment, each side
#define STATE1_EXPAND_EDGE_FADE_PX   6  // soft fade at the growing edges (not specified
                                         // exactly - matched the 6px used elsewhere)
#define STATE1_FADEOUT_MS         3500  // used by doState1FadeOut() only (sections a/b's rare
                                         // natural-completion edge case, see below) - slowed
                                         // down from 2300ms now that it sparkles as it fades
#define STATE1_HOLD_FADEOUT_MS    4500  // doState1HoldFadeOut()'s own duration - the TRUE final
                                         // fade to black after section c's (the last section's)
                                         // hold ends. 1s longer than STATE1_FADEOUT_MS, decoupled
                                         // so that change doesn't also affect the a/b case above.
#define STATE1_COMET_FADE_IN_MS    400  // at the very start (time-based)
// Keeps moving/drawing until its WHOLE trail (tail included, not just
// the head) has passed pixel 66 - that's what actually fixes the
// "sucked into a hole" disappearance (each pixel gets to fade naturally
// as it individually nears 66, instead of the whole comet vanishing in
// one frame the instant the head crosses the line).
#define STATE1_COMET_FADE_OUT_PX    4
#define STATE1_CHUNK_SIZE           30  // "longer trail" - vs the usual CHUNK_SIZE (24)

// Section c's final expand holds solid (no breathing) until this exact
// absolute moment (00:00:28:00, measured from the "state1" trigger),
// then starts breathing for the rest of the track.
#define STATE1_BREATHE_START_MS  28000

// Sparkle/twinkle overlay on the comet's trail: each frame, each pixel
// in the trail has this % chance of overriding its normal fade-based
// brightness with a random one instead (can end up brighter OR dimmer
// than the smooth fade would give) - reads as glitter/twinkle riding on
// top of the trail rather than a smooth gradient.
#define STATE1_SPARKLE_CHANCE_PCT   15
#define STATE1_SPARKLE_MIN          80
#define STATE1_SPARKLE_MAX         255

// Section c's held expand starts fading out (with the same sparkle
// overlay as the comet) at this exact absolute moment (00:00:38:00,
// measured from the "state1" trigger).
#define STATE1_HOLD_FADEOUT_START_MS  38000

// Section timeline (a/b/c), all measured from the "state1" trigger.
// One full comet+glow+expand cycle takes ~9.7s on its own - shorter than
// sections a/b's windows here, so they always get force-faded early
// rather than ever completing naturally. Section c is now the LAST
// section (section d was removed) - no forced cutoff, so its comet+glow
// +expand plays out fully, then its expand HOLDS (not fades) for the
// rest of the track - see doState1Hold().
#define STATE1_SECTION_COUNT 3
const unsigned long STATE1_SECTION_START_MS[STATE1_SECTION_COUNT] = {
  0,      // a - 0:00
  11667,  // b - 00:00:11:20 at 30fps
  21000,  // c - 0:21 (last - holds expanded until the track ends and State 1 loops)
};
#define STATE1_SECTION_FADE_MS       1500  // how long the forced cut-to-black takes
#define STATE1_SECTION_FADE_STEP_MS  20    // how often the fade decay is applied
#define STATE1_SECTION_FADE_STEP_AMOUNT 18 // per-step decay amount (fadeToBlackBy) -
                                            // tuned so ~75 steps (1500ms / 20ms) reads
                                            // as fully faded to black

CRGB leds[NUM_LEDS];

CRGB PINK = CRGB(0xFF, 0x14, 0x5A); // #FF145A - State 1 color (and the comet)

enum Stage {
  STAGE_STATE0,
  STAGE_STATE1_DELAY, STAGE_STATE1_COMETS, STAGE_STATE1_GLOW,
  STAGE_STATE1_EXPAND, STAGE_STATE1_FADEOUT, STAGE_STATE1_HOLD, STAGE_STATE1_HOLD_FADEOUT,
  STAGE_STATE1_SECTION_FADE,
  STAGE_STATE2_FLICKER, STAGE_STATE2_BREATHE_INTRO, STAGE_STATE2_SOFT_FLICKER, STAGE_STATE2_HOLD,
  STAGE_STATE3_SHRINK, STAGE_STATE3_COMET, STAGE_STATE3_GLOW, STAGE_STATE3_EXPAND, STAGE_STATE3_HOLD
};
// Boots into State 0 (all LEDs off) by default - so any unplanned
// reset (power blip, USB replug, a control script closing its
// connection) lands on "off" instead of a random-looking animation.
Stage currentStage = STAGE_STATE0;

unsigned long state1StageStartTime = 0; // when the current State 1 sub-stage began
unsigned long state1TriggerTime = 0;    // when "state 1" was triggered - fixed
                                         // reference for the section timestamps,
                                         // never reset by a section restart
int state1SectionIndex = 0;             // 0=a, 1=b, 2=c, 3=d
unsigned long state1SectionFadeLastStepTime = 0;

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

  // While inside any normal State 1 sub-stage, check first whether it's
  // time to force-cut to the next section (a/b/c/d) - this can interrupt
  // ANY of them, mid-comet, mid-glow, or mid-expand, since one full cycle
  // (~9.7s) doesn't fit inside every section's window on its own.
  bool inState1Substage =
    currentStage == STAGE_STATE1_DELAY || currentStage == STAGE_STATE1_COMETS ||
    currentStage == STAGE_STATE1_GLOW || currentStage == STAGE_STATE1_EXPAND ||
    currentStage == STAGE_STATE1_FADEOUT;
  if (inState1Substage && checkState1SectionCutoff()) {
    return; // stage just changed to STAGE_STATE1_SECTION_FADE this frame
  }

  switch (currentStage) {
    case STAGE_STATE0:         break; // all LEDs off, nothing to do each frame
    case STAGE_STATE1_DELAY:       doState1Delay(); break;
    case STAGE_STATE1_COMETS:      doState1Comets(); break;
    case STAGE_STATE1_GLOW:        doState1Glow(); break;
    case STAGE_STATE1_EXPAND:      doState1Expand(); break;
    case STAGE_STATE1_FADEOUT:     doState1FadeOut(); break;
    case STAGE_STATE1_HOLD:        doState1Hold(); break;
    case STAGE_STATE1_HOLD_FADEOUT: doState1HoldFadeOut(); break;
    case STAGE_STATE1_SECTION_FADE: doState1SectionFade(); break;
    case STAGE_STATE2_FLICKER:      doState2Flicker(); break;
    case STAGE_STATE2_BREATHE_INTRO: doState2BreatheIntro(); break;
    case STAGE_STATE2_SOFT_FLICKER:  doState2SoftFlicker(); break;
    case STAGE_STATE2_HOLD:         doState2Hold(); break;
    case STAGE_STATE3_SHRINK:       doState3Shrink(); break;
    case STAGE_STATE3_COMET:        doState3Comet(); break;
    case STAGE_STATE3_GLOW:         doState3Glow(); break;
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
    FastLED.show(); // direct, unthrottled - this is a one-time transition, not a
                     // continuous loop, so there's no risk of it spamming the
                     // strip. showThrottled() here could silently skip pushing
                     // the blank frame if a prior animated state had just shown
                     // recently, leaving the strip frozen on that last frame.
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

// ---------- Start State 1 from section a (the very beginning) ----------
void startState1() {
  state1TriggerTime = millis();
  state1SectionIndex = 0;
  startState1Section();
  Serial.println(F("Manual trigger: State 1 - section a starting..."));
}

// ---------- Start (or restart) whichever section is current - shared by
// the very first section and every forced cut to the next one. ----------
void startState1Section() {
  FastLED.clear();
  showThrottled();
  state1StageStartTime = millis();
  currentStage = STAGE_STATE1_DELAY;
  Serial.print(F("State 1 section "));
  Serial.print((char)('a' + state1SectionIndex));
  Serial.println(F(" starting..."));
}

// ---------- Checks whether it's time to force-cut to the next section.
// Returns true (and switches to STAGE_STATE1_SECTION_FADE) if so - the
// caller must stop doing anything else with the old stage this frame. ----------
bool checkState1SectionCutoff() {
  if (state1SectionIndex + 1 >= STATE1_SECTION_COUNT) return false; // section d - nothing after it

  unsigned long absElapsed = millis() - state1TriggerTime;
  unsigned long nextSectionStart = STATE1_SECTION_START_MS[state1SectionIndex + 1];

  if (absElapsed >= nextSectionStart - STATE1_SECTION_FADE_MS) {
    currentStage = STAGE_STATE1_SECTION_FADE;
    state1StageStartTime = millis();
    state1SectionFadeLastStepTime = millis();
    Serial.println(F("State 1 section cutting to black early for the next section..."));
    return true;
  }
  return false;
}

// ---------- Forced cut-to-black between sections: fades out whatever's
// CURRENTLY lit (no matter which sub-stage it was) over
// STATE1_SECTION_FADE_MS, timed to land exactly on the next section's
// start, then starts that section fresh. ----------
void doState1SectionFade() {
  unsigned long elapsed = millis() - state1StageStartTime;

  if (elapsed >= STATE1_SECTION_FADE_MS) {
    FastLED.clear();
    showThrottled();
    state1SectionIndex++;
    startState1Section();
    return;
  }

  if (millis() - state1SectionFadeLastStepTime >= STATE1_SECTION_FADE_STEP_MS) {
    state1SectionFadeLastStepTime = millis();
    fadeToBlackBy(leds, NUM_LEDS, STATE1_SECTION_FADE_STEP_AMOUNT);
    showThrottled();
  }
}

// ---------- Nothing shown yet - just waiting out STATE1_START_DELAY_MS ----------
void doState1Delay() {
  if (millis() - state1StageStartTime >= STATE1_START_DELAY_MS) {
    state1StageStartTime = millis(); // the comet's t=0
    currentStage = STAGE_STATE1_COMETS;
    Serial.println(F("State 1 comet starting (fading in)..."));
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
// dimming as one block. fadeOutPx and trailLength are passed in so
// different comets (or the same comet in a different section) can use a
// different fade width / trail length. ----------
void drawState1Comet(float pos, uint8_t fadeInBrightness, int fadeOutPx, int trailLength) {
  int headPos = (int)pos;
  for (int offset = 0; offset < trailLength; offset++) {
    int px = headPos - offset;
    if (px < 0 || px >= NUM_LEDS) continue;

    // Tail taper: brightest at the head (offset 0), fading out toward
    // the back of the trail.
    uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (trailLength - 1));

    // Melt into pixel 66: this pixel's own distance to the endpoint,
    // regardless of where the head currently is.
    uint8_t endFade = 255;
    int distToEnd = STATE1_MID - px;
    if (distToEnd <= fadeOutPx) {
      float frac = (float)distToEnd / fadeOutPx;
      if (frac < 0) frac = 0;
      endFade = (uint8_t)(255.0 * frac);
    }

    // Sparkle overlay: some pixels randomly override their fade-based
    // brightness each frame, so the trail glitters instead of just
    // smoothly fading.
    uint8_t sparkleBrightness = tailFade;
    if (random8() < (uint8_t)((uint16_t)STATE1_SPARKLE_CHANCE_PCT * 255 / 100)) {
      sparkleBrightness = random8(STATE1_SPARKLE_MIN, STATE1_SPARKLE_MAX + 1);
    }

    CRGB c = PINK;
    c.nscale8(fadeInBrightness);
    c.nscale8(sparkleBrightness);
    c.nscale8(endFade);
    leds[px] = c;
  }
}

// ---------- One comet, at constant (non-accelerating) speed, in every
// section. Its own arrival at pixel 66 triggers the glow + expand - but
// not until its LAST pixel (the tail end, not just the head) has reached
// 66, so the whole comet has visibly arrived before expand begins.
// Section c uses a faster speed than the rest, timed to hit a specific
// expand-start target (see STATE1_SECTION_C_COMET_TRAVEL_MS above). ----------
void doState1Comets() {
  bool isSectionC = (state1SectionIndex == 2);
  unsigned long absElapsed = millis() - state1StageStartTime;

  FastLED.clear();

  float cometSpeed = isSectionC
    ? ((float)STATE1_MID / STATE1_SECTION_C_COMET_TRAVEL_MS)
    : STATE1_COMET_SPEED_PXPMS;
  float pos = cometSpeed * absElapsed;
  float tailPos = pos - (STATE1_CHUNK_SIZE - 1);
  if (tailPos < STATE1_MID) {
    drawState1Comet(pos, state1CometFadeIn(absElapsed, true), STATE1_COMET_FADE_OUT_PX, STATE1_CHUNK_SIZE);
    showThrottled();
  } else {
    state1StageStartTime = millis();
    currentStage = STAGE_STATE1_GLOW;
    Serial.println(F("State 1 comet's tail reached pixel 66 - glowing..."));
  }
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
    // Only section c (the LAST section) holds here permanently - a/b
    // still fade out as before if their own expand ever completes
    // naturally, so checkState1SectionCutoff() (which watches
    // STAGE_STATE1_FADEOUT but not STAGE_STATE1_HOLD) can still force
    // them onward to the next section. Without this check, a/b would
    // get stuck in the permanent hold and b/c would never play at all.
    bool isLastSection = (state1SectionIndex + 1 >= STATE1_SECTION_COUNT);
    if (isLastSection) {
      currentStage = STAGE_STATE1_HOLD;
      Serial.println(F("State 1 fully expanded - holding until the track loops..."));
    } else {
      state1StageStartTime = millis();
      currentStage = STAGE_STATE1_FADEOUT;
      Serial.println(F("State 1 fully expanded - fading out..."));
    }
  }
}

// ---------- Holds section c's fully-expanded look, solid until
// STATE1_BREATHE_START_MS (00:00:28:00, absolute from the "state1"
// trigger), then breathes for the rest of the track - no fade. State 1
// doesn't loop or auto-advance on its own; it just keeps holding/
// breathing here until the next external trigger (matches how State
// 2's/3's own holds behave). ----------
void doState1Hold() {
  int fullHalf = (SEGMENT_END - SEGMENT_START) / 2 - STATE1_EXPAND_SHORTEN_PX;
  int rangeStart = STATE1_MID - fullHalf;
  int rangeEnd = STATE1_MID + fullHalf;

  uint8_t breath = 255;
  if (millis() - state1TriggerTime >= STATE1_BREATHE_START_MS) {
    breath = beatsin8(9, 20, 255);
  }

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    int distToEdge = min(i - rangeStart, rangeEnd - i);
    uint8_t brightness = 255;
    if (distToEdge < STATE1_EXPAND_EDGE_FADE_PX) {
      brightness = (uint8_t)(255.0 * distToEdge / STATE1_EXPAND_EDGE_FADE_PX);
    }
    CRGB c = PINK;
    c.nscale8(brightness);
    c.nscale8(breath);
    leds[i] = c;
  }
  showThrottled();

  if (millis() - state1TriggerTime >= STATE1_HOLD_FADEOUT_START_MS) {
    state1StageStartTime = millis();
    currentStage = STAGE_STATE1_HOLD_FADEOUT;
    Serial.println(F("State 1 hold - starting sparkling fade-out..."));
  }
}

// ---------- Shared rendering by doState1HoldFadeOut() and doState1FadeOut()
// (each supplies its own duration via t) - fades the fully-expanded
// segment to black, sparkling as it goes (same random-override overlay
// as the comet's trail, so glints of light pop up here and there as it
// dies out instead of a plain, even dim-down). Returns true once the
// fade has fully completed. ----------
bool renderState1SparkleFadeOut(float t) {
  uint8_t fadeBrightness = (uint8_t)(255.0 * (1.0 - t));

  int fullHalf = (SEGMENT_END - SEGMENT_START) / 2 - STATE1_EXPAND_SHORTEN_PX;
  int rangeStart = STATE1_MID - fullHalf;
  int rangeEnd = STATE1_MID + fullHalf;

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    int distToEdge = min(i - rangeStart, rangeEnd - i);
    uint8_t edgeBrightness = 255;
    if (distToEdge < STATE1_EXPAND_EDGE_FADE_PX) {
      edgeBrightness = (uint8_t)(255.0 * distToEdge / STATE1_EXPAND_EDGE_FADE_PX);
    }

    uint8_t sparkleBrightness = fadeBrightness;
    if (random8() < (uint8_t)((uint16_t)STATE1_SPARKLE_CHANCE_PCT * 255 / 100)) {
      sparkleBrightness = random8(STATE1_SPARKLE_MIN, STATE1_SPARKLE_MAX + 1);
    }

    CRGB c = PINK;
    c.nscale8(edgeBrightness);
    c.nscale8(sparkleBrightness);
    leds[i] = c;
  }
  showThrottled();

  return t >= 1.0;
}

// ---------- Fades section c's held expand to black (sparkling), then
// holds dark waiting for the next trigger/loop. ----------
void doState1HoldFadeOut() {
  unsigned long elapsed = millis() - state1StageStartTime;
  float t = (float)elapsed / STATE1_HOLD_FADEOUT_MS;
  if (t > 1.0) t = 1.0;

  if (renderState1SparkleFadeOut(t)) {
    FastLED.clear();
    FastLED.show(); // direct, unthrottled - see goToState(0)'s comment on why
    currentStage = STAGE_STATE0;
    Serial.println(F("State 1 sparkling fade-out complete - holding dark, waiting..."));
  }
}

// ---------- Fade the fully-expanded segment to black (sparkling), then go
// dark (State 0) - reached if a/b's own expand ever completes naturally
// before being force-cut to the next section. ----------
void doState1FadeOut() {
  unsigned long elapsed = millis() - state1StageStartTime;
  float t = (float)elapsed / STATE1_FADEOUT_MS;
  if (t > 1.0) t = 1.0;

  if (renderState1SparkleFadeOut(t)) {
    FastLED.clear();
    FastLED.show(); // direct, unthrottled - see goToState(0)'s comment on why
    currentStage = STAGE_STATE0;
    Serial.println(F("State 1 sequence complete - holding dark, waiting for next trigger..."));
  }
}
