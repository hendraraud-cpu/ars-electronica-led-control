/*
  led-strip2 - the SECOND Arduino (COM7), driving a separate LED strip.
  Reacts to two commands from the main control script:
    "state3"   - the expansion effect that used to live at the end of
                 State 3 on the main board (COM8), moved here to play
                 on its own strip.
    "station2" - once State 3's audio hands off to 05_STATION 2.MP3,
                 this strip switches to a 60-pixel section (like State
                 2's on the main board) with the same slow breathing
                 idle hold - no flicker yet, that can be layered in
                 once there's a specific timeline for it.
  Any other command ("state0"/"state1"/"state2") just blanks this strip.

  The active/lit section for "state3" starts at pixel 0 (SEGMENT_START) -
  back to where it was before it got shifted 50 pixels and then reverted.
  SEGMENT_WIDTH below is a placeholder (200) - it hasn't been confirmed
  against the real physical LED count on this strip yet. If the real
  width is different, the expand will finish short of (or try to go
  past) the actual strip - update SEGMENT_WIDTH once that's confirmed.
  COLOR_ORDER is also assumed GRB, matching strip 1 - confirm if colors
  look wrong.

  "station2"'s own 60-pixel section (70-129) is centered on the same
  CENTER_PIXEL (100) as State 3's expansions - a separate, narrower
  range within the same address space.

  TIMELINE for "state3" (from this board's own trigger, t=0):
    0 - 00:00:12:11 (12367ms)   - dark
    00:00:12:11                 - single center pixel starts breathing
    00:00:13:20 - 00:00:14:20   - expands from that center point to the
                                   whole strip over 1 second, breathing
                                   continues throughout
    00:00:14:20 - 00:00:23:22   - stays fully expanded, keeps breathing
    00:00:23:22 - 00:00:24:22   - "expansion2": shrinks from the full
                                   width down to a 60-pixel core, back
                                   toward the SAME center point as the
                                   first expansion - the rest of the
                                   strip dims (not off) instead of going
                                   fully black
    00:00:24:22 onward          - holds there forever: 60px core
                                   breathing, everything else dimmed

  Wiring: same DATA_PIN (7) as pin-test.ino, confirmed working.
*/

#include <FastLED.h>
#include <string.h>

#define DATA_PIN     7
#define COLOR_ORDER  GRB   // assumed - confirm if colors look wrong

// Back to starting at pixel 0 (was shifted 50 pixels further, then
// reverted back here).
#define SEGMENT_START  0
#define SEGMENT_WIDTH  200   // PLACEHOLDER - confirm against the real strip
#define SEGMENT_END    (SEGMENT_START + SEGMENT_WIDTH - 1)
#define NUM_LEDS       (SEGMENT_END + 1)

#define CENTER_PIXEL  100  // fixed midpoint for both expansion and expansion2

// Timeline constants (ms since this board's own "state3" trigger).
#define BREATHE_START_MS  12367  // 00:00:12:11
#define EXPAND_START_MS   13667  // 00:00:13:20
#define EXPAND_MS          1000  // 1 second

// "expansion2" - shrinks from the full width back down to a 60-pixel
// core, toward the same CENTER_PIXEL as the first expansion.
#define SHRINK2_START_MS  23733  // 00:00:23:22
#define SHRINK2_MS          2500  // 2.5 seconds
#define SHRINK2_TARGET_HALF   30  // half-width of the 60px final core
#define FULL_HALF  (CENTER_PIXEL - SEGMENT_START)  // half-width at full expand
#define EXPANSION2_EDGE_FADE_PX  6  // soft fade at both the core's and the
                                     // dim halo's edges, instead of a hard edge
// How dim the "halo" pixels around the 60px core get, once expansion2
// lands - not specified exactly, picked to read as clearly dimmed but
// still visible (not fully off).
#define DIM_FRACTION  0.15f
// The dim halo isn't the whole rest of the strip - it's a band this
// many pixels wide on each side of the core, and it shrinks inward
// together with the core instead of staying fixed at the full width.
// Beyond the halo, pixels go fully black. Width not specified - picked
// as a visually clear "halo" without being too wide.
#define DIM_HALO_EXTRA_PX  20

// Same idea for the FIRST expansion (see doExpand()) - a soft fade at
// its growing edges instead of a hard edge, fading to black (there's no
// halo concept yet at that point, the rest of the strip is just off).
#define EXPAND_EDGE_FADE_PX  6

// Breathing pauses (holds solid, no pulsing) for the whole of expansion2
// - both the shrink itself and the settled-in hold after it - resuming
// once the audio hits 00:00:28:26.
#define RESUME_BREATHE_MS  28867  // 00:00:28:26

// Breathing pulse (same shape as State 3's old hold on the main board).
#define BREATHE_BPM    9
#define BREATHE_MIN   20
#define BREATHE_MAX  255

// "station2" - a 60-pixel section (matching State 2's width on the main
// board), centered on the same CENTER_PIXEL (100) as State 3's
// expansions, same slow breathe cycle as State 2's idle hold there
// (dims to 80% over 20s, recovers over 3s, repeating).
#define STATION2_SEGMENT_START  (CENTER_PIXEL - 30)
#define STATION2_SEGMENT_WIDTH  60
#define STATION2_SEGMENT_END    (STATION2_SEGMENT_START + STATION2_SEGMENT_WIDTH - 1)
#define STATION2_BREATHE_DIM_MS       20000
#define STATION2_BREATHE_RECOVER_MS    3000
#define STATION2_BREATHE_CYCLE_MS     (STATION2_BREATHE_DIM_MS + STATION2_BREATHE_RECOVER_MS)
#define STATION2_BREATHE_DIM_FRACTION  0.8f

// station2's flicker intro, right at the start - 2 mild (50% dip)
// flickers, same shape as State 2's own intro flickers on the main
// board.
#define STATION2_FLICKER_BLIP_MS 60
const unsigned long STATION2_FLICKER_TIMES_MS[] = {
  667,  // 00:00:00:20 at 30fps - 50% dip
  900,  // midpoint of the 667-1133ms window - 50% dip
};
const uint8_t STATION2_FLICKER_DIP_BRIGHTNESS[] = {
  128,  // 667ms - 50%
  128,  // 900ms - 50%
};
#define STATION2_FLICKER_COUNT (sizeof(STATION2_FLICKER_TIMES_MS) / sizeof(unsigned long))

CRGB leds[NUM_LEDS];
CRGB STRIP2_COLOR = CRGB::White;

enum Stage2 {
  STAGE2_IDLE, STAGE2_DARK_WAIT, STAGE2_BREATHE_POINT, STAGE2_EXPAND, STAGE2_HOLD,
  STAGE2_SHRINK2, STAGE2_HOLD2,
  STAGE2_STATION2_FLICKER, STAGE2_STATION2_HOLD
};
Stage2 stage = STAGE2_IDLE;

unsigned long triggerTime = 0;
unsigned long shrink2StartTime = 0;
unsigned long station2StartTime = 0;
int station2FlickerIndex = 0;
bool station2FlickerBlipActive = false;
unsigned long station2FlickerBlipStartElapsed = 0;

char cmdBuffer[16];
uint8_t cmdLength = 0;

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();
  Serial.println(F("led-strip2 ready."));
}

void loop() {
  checkSerialCommand();

  switch (stage) {
    case STAGE2_IDLE:         break; // nothing triggered yet - stay dark
    case STAGE2_DARK_WAIT:    doDarkWait(); break;
    case STAGE2_BREATHE_POINT: doBreathePoint(); break;
    case STAGE2_EXPAND:       doExpand(); break;
    case STAGE2_HOLD:         doHold(); break;
    case STAGE2_SHRINK2:      doShrink2(); break;
    case STAGE2_HOLD2:        doHold2(); break;
    case STAGE2_STATION2_FLICKER: doStation2Flicker(); break;
    case STAGE2_STATION2_HOLD:    doStation2Hold(); break;
  }
}

// ---------- Read "state3" / "station2" / anything else from Serial and react ----------
void checkSerialCommand() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdLength > 0) {
        cmdBuffer[cmdLength] = '\0';
        if (strcmp(cmdBuffer, "state3") == 0) {
          startState3();
        } else if (strcmp(cmdBuffer, "station2") == 0) {
          startStation2();
        } else {
          goDark();
        }
        Serial.print(F("led-strip2 got: "));
        Serial.println(cmdBuffer);
        cmdLength = 0;
      }
    } else if (cmdLength < sizeof(cmdBuffer) - 1) {
      cmdBuffer[cmdLength++] = c;
    }
    // else: buffer full, silently drop extra characters rather than overrun
  }
}

// ---------- Any command other than "state3"/"station2" just blanks this strip ----------
void goDark() {
  FastLED.clear();
  FastLED.show();
  stage = STAGE2_IDLE;
}

// ---------- Start (or restart) the State 3 sequence on this strip ----------
void startState3() {
  FastLED.clear();
  FastLED.show();
  triggerTime = millis();
  stage = STAGE2_DARK_WAIT;
  Serial.println(F("State 3 (strip 2): waiting..."));
}

// ---------- Dark until BREATHE_START_MS ----------
void doDarkWait() {
  if (millis() - triggerTime >= BREATHE_START_MS) {
    stage = STAGE2_BREATHE_POINT;
    Serial.println(F("State 3 (strip 2): center point breathing..."));
  }
}

// ---------- Single center pixel, breathing, until EXPAND_START_MS ----------
void doBreathePoint() {
  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STRIP2_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  leds[CENTER_PIXEL] = c;
  FastLED.show();

  if (millis() - triggerTime >= EXPAND_START_MS) {
    stage = STAGE2_EXPAND;
    Serial.println(F("State 3 (strip 2): expanding..."));
  }
}

// ---------- Expands from the center point to the whole strip over EXPAND_MS, breathing throughout ----------
void doExpand() {
  unsigned long elapsed = millis() - triggerTime - EXPAND_START_MS;
  float t = (float)elapsed / EXPAND_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);
  int currentHalf = (int)(CENTER_PIXEL * eased);

  int rangeStart = CENTER_PIXEL - currentHalf;
  int rangeEnd = CENTER_PIXEL + currentHalf;
  if (rangeStart < SEGMENT_START) rangeStart = SEGMENT_START;
  if (rangeEnd > SEGMENT_END) rangeEnd = SEGMENT_END;

  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    // Soft edges: dims toward black near the growing front (rangeStart/
    // rangeEnd) instead of a hard-edged block.
    int distToEdge = min(i - rangeStart, rangeEnd - i);
    uint8_t brightness = breath;
    if (distToEdge < EXPAND_EDGE_FADE_PX) {
      brightness = (uint8_t)(breath * ((float)distToEdge / EXPAND_EDGE_FADE_PX));
    }
    CRGB c = STRIP2_COLOR;
    c.nscale8(brightness);
    leds[i] = c;
  }
  FastLED.show();

  if (t >= 1.0) {
    stage = STAGE2_HOLD;
    Serial.println(F("State 3 (strip 2): fully expanded - breathing until next command..."));
  }
}

// ---------- Whole strip lit, breathing, until SHRINK2_START_MS ----------
void doHold() {
  uint8_t breath = beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
  CRGB c = STRIP2_COLOR;
  c.nscale8(breath);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (millis() - triggerTime >= SHRINK2_START_MS) {
    shrink2StartTime = millis();
    stage = STAGE2_SHRINK2;
    Serial.println(F("State 3 (strip 2): expansion2 - shrinking to 60px core..."));
  }
}

// ---------- Breathing brightness for expansion2's shrink + settled hold -
// holds solid at full brightness (no pulsing) until RESUME_BREATHE_MS,
// then breathes normally. ----------
uint8_t expansion2Breath() {
  if (millis() - triggerTime < RESUME_BREATHE_MS) {
    return BREATHE_MAX;
  }
  return beatsin8(BREATHE_BPM, BREATHE_MIN, BREATHE_MAX);
}

// ---------- Renders the bright core + dim halo + black-beyond split,
// each with its own soft edge fade instead of a hard cutoff - shared by
// doShrink2() (moving boundaries) and doHold2() (fixed boundaries). The
// halo shrinks together with the core (see doShrink2()/doHold2() below),
// it's not a static full-width background. ----------
void renderCoreWithFade(int innerStart, int innerEnd, int outerStart, int outerEnd, uint8_t breath) {
  uint8_t dimLevel = (uint8_t)(DIM_FRACTION * 255);

  for (int i = SEGMENT_START; i <= SEGMENT_END; i++) {
    uint8_t brightness;

    if (i >= innerStart && i <= innerEnd) {
      // Bright core - fades toward the dim level near its own edges.
      int distToEdge = min(i - innerStart, innerEnd - i);
      if (distToEdge < EXPANSION2_EDGE_FADE_PX) {
        float blend = (float)distToEdge / EXPANSION2_EDGE_FADE_PX;
        int val = dimLevel + (int)(((int)breath - (int)dimLevel) * blend);
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        brightness = (uint8_t)val;
      } else {
        brightness = breath;
      }
    } else if (i >= outerStart && i <= outerEnd) {
      // Dim halo - fades toward black near its own edges.
      int distToEdge = min(i - outerStart, outerEnd - i);
      if (distToEdge < EXPANSION2_EDGE_FADE_PX) {
        float blend = (float)distToEdge / EXPANSION2_EDGE_FADE_PX;
        brightness = (uint8_t)(dimLevel * blend);
      } else {
        brightness = dimLevel;
      }
    } else {
      brightness = 0; // beyond the halo - fully black
    }

    CRGB c = STRIP2_COLOR;
    c.nscale8(brightness);
    leds[i] = c;
  }
}

// ---------- expansion2: shrinks from the full width down to a 60px core
// centered on the same CENTER_PIXEL as the first expansion, over
// SHRINK2_MS - the dim halo around the core shrinks right along with
// it (not staying fixed at the full width), fading to black beyond it. ----------
void doShrink2() {
  unsigned long elapsed = millis() - shrink2StartTime;
  float t = (float)elapsed / SHRINK2_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);
  int innerHalf = FULL_HALF - (int)((FULL_HALF - SHRINK2_TARGET_HALF) * eased);
  int outerHalf = innerHalf + DIM_HALO_EXTRA_PX;

  // innerEnd/outerEnd use half-1 (not half) so the final core width
  // lands on exactly 60 pixels (70-129) instead of 61 - CENTER_PIXEL
  // (100) sits just inside the start half, not dead center, since 60 is
  // an even width and can't be perfectly centered on a single pixel.
  int innerStart = CENTER_PIXEL - innerHalf;
  int innerEnd = CENTER_PIXEL + innerHalf - 1;
  int outerStart = CENTER_PIXEL - outerHalf;
  int outerEnd = CENTER_PIXEL + outerHalf - 1;
  if (innerStart < SEGMENT_START) innerStart = SEGMENT_START;
  if (innerEnd > SEGMENT_END) innerEnd = SEGMENT_END;
  if (outerStart < SEGMENT_START) outerStart = SEGMENT_START;
  if (outerEnd > SEGMENT_END) outerEnd = SEGMENT_END;

  uint8_t breath = expansion2Breath();
  renderCoreWithFade(innerStart, innerEnd, outerStart, outerEnd, breath);
  FastLED.show();

  if (t >= 1.0) {
    stage = STAGE2_HOLD2;
    Serial.println(F("State 3 (strip 2): expansion2 complete - 60px core breathing, halo dimmed, rest black..."));
  }
}

// ---------- 60px core breathing + shrunk dim halo, everything beyond black, forever until the next command ----------
void doHold2() {
  int innerHalf = SHRINK2_TARGET_HALF;
  int outerHalf = innerHalf + DIM_HALO_EXTRA_PX;

  int innerStart = CENTER_PIXEL - innerHalf;
  int innerEnd = CENTER_PIXEL + innerHalf - 1;  // exactly 60px total
  int outerStart = CENTER_PIXEL - outerHalf;
  int outerEnd = CENTER_PIXEL + outerHalf - 1;
  if (innerStart < SEGMENT_START) innerStart = SEGMENT_START;
  if (innerEnd > SEGMENT_END) innerEnd = SEGMENT_END;
  if (outerStart < SEGMENT_START) outerStart = SEGMENT_START;
  if (outerEnd > SEGMENT_END) outerEnd = SEGMENT_END;

  uint8_t breath = expansion2Breath();
  renderCoreWithFade(innerStart, innerEnd, outerStart, outerEnd, breath);
  FastLED.show();
}

// ---------- Start (or restart) the station2 light show on this strip ----------
void startStation2() {
  FastLED.clear();
  FastLED.show();
  station2StartTime = millis();
  station2FlickerIndex = 0;
  station2FlickerBlipActive = false;
  stage = STAGE2_STATION2_FLICKER;
  Serial.println(F("Station 2 (strip 2): flicker intro..."));
}

// ---------- station2's flicker intro: 2 mild (50% dip) flickers on the
// 60px section at full brightness, then hands off to the breathing hold. ----------
void doStation2Flicker() {
  unsigned long elapsed = millis() - station2StartTime;
  bool inBlip = false;

  if (station2FlickerBlipActive) {
    if (elapsed - station2FlickerBlipStartElapsed >= STATION2_FLICKER_BLIP_MS) {
      station2FlickerBlipActive = false;
      station2FlickerIndex++;
    } else {
      inBlip = true;
    }
  } else if (station2FlickerIndex < STATION2_FLICKER_COUNT && elapsed >= STATION2_FLICKER_TIMES_MS[station2FlickerIndex]) {
    station2FlickerBlipActive = true;
    station2FlickerBlipStartElapsed = elapsed;
    inBlip = true;
  }

  CRGB c = STRIP2_COLOR;
  if (inBlip) {
    c.nscale8(STATION2_FLICKER_DIP_BRIGHTNESS[station2FlickerIndex]);
  }

  FastLED.clear();
  for (int i = STATION2_SEGMENT_START; i <= STATION2_SEGMENT_END; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (!station2FlickerBlipActive && station2FlickerIndex >= STATION2_FLICKER_COUNT) {
    station2StartTime = millis();  // reset - the breathe cycle starts fresh from here
    stage = STAGE2_STATION2_HOLD;
    Serial.println(F("Station 2 (strip 2): flicker done - 60px section breathing..."));
  }
}

// ---------- station2: 60-pixel section, slow breathe cycle (same shape as
// State 2's idle hold on the main board: dims to 80% over 20s, recovers
// over 3s, repeating), forever until the next command ----------
void doStation2Hold() {
  unsigned long cyclePos = (millis() - station2StartTime) % STATION2_BREATHE_CYCLE_MS;
  float breatheFraction;
  if (cyclePos < STATION2_BREATHE_DIM_MS) {
    float t = (float)cyclePos / STATION2_BREATHE_DIM_MS;
    breatheFraction = 1.0f - (1.0f - STATION2_BREATHE_DIM_FRACTION) * t;
  } else {
    float t = (float)(cyclePos - STATION2_BREATHE_DIM_MS) / STATION2_BREATHE_RECOVER_MS;
    breatheFraction = STATION2_BREATHE_DIM_FRACTION + (1.0f - STATION2_BREATHE_DIM_FRACTION) * t;
  }

  CRGB c = STRIP2_COLOR;
  c.nscale8((uint8_t)(breatheFraction * 255));

  FastLED.clear();
  for (int i = STATION2_SEGMENT_START; i <= STATION2_SEGMENT_END; i++) {
    leds[i] = c;
  }
  FastLED.show();
}

// ---------- Ease-out cubic: fast start, sudden slow-down near the end ----------
float easeOutCubic(float t) {
  float f = t - 1.0;
  return f * f * f + 1.0;
}
