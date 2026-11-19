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
    "state5"   - State 5 mode Z sends this. A scripted sequence on the
                 SAME 60-pixel section as expansion2's final core
                 (110-169, centered on 140):
                   0 - 00:00:01:06 (1200ms) - glitch flickers that get
                       FASTER each time (shrinking interval), not dimmer
                   00:00:01:06 - 00:00:03:05 (1967ms) - the 60px section
                       swings left/right by 30px around its center
                   00:00:03:05 - a comet launches right at this moment
                       (no separate shrink stage) from the midpoint (140)
                       and races to pixel 0 ONCE (no repeat, eased in-out,
                       melted in by 00:00:05:25), then holds dark
    "state5x"  - State 5 mode X sends this. A scripted sequence over the
                 WHOLE physical strip (pixels 0-SEGMENT_END, not just the
                 20-cut active segment State 3 uses):
                   0 - 700ms - glitch flickers, each one dimmer than the
                       last, fading toward black
                   700ms - 00:00:01:21 (1000ms, a full second) - instant
                       red flash across the whole strip, then shrinks
                       down to a single point at STATE5X_CENTER_PIXEL
                       (144, its own midpoint - separate from
                       EXPANSION2_CENTER_PIXEL) - this last second of the
                       whole lead-in is red throughout
                   00:00:01:21 onward - fast red comets repeat forever,
                       launching from pixel 144 and racing down to pixel
                       0 (reversed direction from every other comet in
                       this project), with a brief landing glow between
                       each pass
    "b"        - DEBUG/TEST ONLY (not a real state) - jumps straight
                 ahead so expansion2 begins 4 seconds after this is
                 pressed (STATE3_TEST_B_LEAD_MS), instead of waiting
                 through the ~12.4s dark lead-in + full hold every time.
  Any other command ("state0"/"state1"/"state2") just blanks this strip.

  The active/lit section for "state3" starts at pixel 0 (SEGMENT_START) -
  back to where it was before it got shifted 50 pixels and then reverted.
  SEGMENT_WIDTH below is a placeholder (240 - widened from 200 so
  station2's section fits) - it hasn't been confirmed against the real
  physical LED count on this strip yet. If the real width is different,
  the expand will finish short of (or try to go past) the actual strip -
  update SEGMENT_WIDTH once that's confirmed. COLOR_ORDER is also
  assumed GRB, matching strip 1 - confirm if colors look wrong.

  "station2"'s own 60-pixel section is at pixels 110-169 - moved back 8
  then down 2 more from 120-179, no longer exactly matching expansion2's
  core or State 5 mode Z's section - a separate, narrower range within
  the same address space. Expansion1 is centered on CENTER_PIXEL (160);
  expansion2 has its own, separate center (EXPANSION2_CENTER_PIXEL, 140) -
  same midpoint as State 4/station2's section (also 140) - and State 5
  mode Z's section follows it too, so all three now land on 140 (110-169).

  TIMELINE for "state3" (from this board's own trigger, t=0) - shifted
  2634ms earlier across the board to match the updated State 3 soundtrack:
    00:00:02:18 (2600ms)        - pre-expansion1 comets start: 5 passes,
                                   same schedule/speed/trail as COM8's
                                   own state3 comets, launching from
                                   SEGMENT_END (250) and converging
                                   inward to CENTER_PIXEL (160) - see
                                   EXPANSION1_COMET_* below. NOTE: as
                                   currently scheduled these can still
                                   be running past 00:00:09:22/00:00:11:01
                                   below - see the comment on
                                   EXPANSION1_COMET_REPEATS.
    0 - 00:00:09:22 (9733ms)    - dark (aside from the comets above)
    00:00:09:22                 - single center pixel starts breathing
    00:00:11:01 - 00:00:12:01   - expands from that center point to the
                                   whole strip over 1 second, breathing
                                   continues throughout
    00:00:12:01 - 00:00:21:03   - stays fully expanded, keeps breathing
    00:00:21:03 - 00:00:23:18   - "expansion2": shrinks from the full
                                   width down to a 60-pixel core, back
                                   toward the SAME center point as the
                                   first expansion - the rest of the
                                   strip dims (not off) instead of going
                                   fully black
    00:00:23:18 onward          - holds there forever: 60px core
                                   breathing resumes at 00:00:26:07,
                                   everything else dimmed

  Wiring: same DATA_PIN (7) as pin-test.ino, confirmed working.
*/

#include <FastLED.h>
#include <string.h>

#define DATA_PIN     7
#define COLOR_ORDER  GRB   // assumed - confirm if colors look wrong

// Cut 20 pixels off the beginning (was 0) - the far end (250) stays
// exactly where it was, only the start retracted, so SEGMENT_WIDTH
// shrank instead of the whole strip shifting.
#define SEGMENT_START  20
// PLACEHOLDER - confirm against the real strip. SEGMENT_END stays 250
// (SEGMENT_START + SEGMENT_WIDTH - 1 = 20 + 231 - 1 = 250).
#define SEGMENT_WIDTH  231
#define SEGMENT_END    (SEGMENT_START + SEGMENT_WIDTH - 1)
#define NUM_LEDS       (SEGMENT_END + 1)

#define CENTER_PIXEL  160  // fixed midpoint for expansion (expansion1) - reverted, the 25px-forward move was undone

// Pre-expansion1 comets - mirrors State 3's own comets on COM8
// (state3.ino) exactly: same repeat count (5), same schedule interval
// (2067ms), same trail length (30), same fade-out width (4px), same
// glow bridge between passes (100ms). Runs here from the strip's OTHER
// end (SEGMENT_END, 250) converging INWARD to expansion1's own center
// (CENTER_PIXEL, 160) instead of diverging outward from a point. Speed
// is matched to COM8's own comet speed (66->199 over 1200ms = 133px/
// 1200ms), not its duration, since the distance here (90px) is shorter -
// same motion feel, naturally quicker to cover less ground.
#define EXPANSION1_COMET_START_MS       2600  // phase starts here - the exact moment COM8's own
                                                // first state3 comet lands at the end of ITS strip
#define EXPANSION1_COMET1_START_MS       400  // first comet launches this long after the phase starts
#define EXPANSION1_COMET_INTERVAL_MS    2067
#define EXPANSION1_COMET_REPEATS           5
#define EXPANSION1_COMET_TRAIL_LENGTH      30
#define EXPANSION1_COMET_FADE_OUT_PX        4
#define EXPANSION1_COMET_FADE_IN_MS       400
#define EXPANSION1_COMET_GLOW_MS          100
#define EXPANSION1_COMET_SPEED_PXPMS  (133.0 / 1200.0)  // = COM8 state3's own comet speed
// FLAGGED, not resolved: at this schedule (phase start 2600ms + 400ms +
// 4*2067ms interval = comet 5 launches at 11268ms, lands ~811ms later
// around 12080ms + 100ms glow), the comets finish around 00:00:12:18 -
// AFTER both BREATHE_START_MS (00:00:09:22) and EXPAND_START_MS
// (00:00:11:01) below. Since this stage only hands off to
// STAGE2_DARK_WAIT once all 5 passes finish, doBreathePoint()/doExpand()
// won't even get evaluated until then - their own elapsed-time checks
// will find themselves already past due and skip straight through,
// compressing/skipping the breathe-then-expand look rather than the
// comets cleanly finishing "before expansion1" as intended. Left as-is
// pending a decision - either push BREATHE_START_MS/EXPAND_START_MS/
// etc. ~2.5s later to give all 5 passes room, or trim the comet count/
// interval to fit the current ~7.1s window (2600-9733ms).

// Timeline constants (ms since this board's own "state3" trigger).
#define BREATHE_START_MS  9733  // 00:00:09:22
#define EXPAND_START_MS   11033  // 00:00:11:01
#define EXPAND_MS          1000  // 1 second

// "expansion2" - shrinks from the full width back down to a 60-pixel
// core, toward its OWN center point - 10 pixels back from expansion1's
// (150 instead of 160), independent of it.
#define SHRINK2_START_MS  21099  // 00:00:21:03
#define SHRINK2_MS          2500  // 2.5 seconds
#define SHRINK2_TARGET_HALF   30  // half-width of the 60px final core
// Moved 10 more pixels down (was CENTER_PIXEL-10=150) to land exactly on
// pixel 140 - matches State 4/station2's midpoint (110-169, centered on
// 140). State 5 mode Z's section also follows this constant, so it
// lands on 140 too now.
#define EXPANSION2_CENTER_PIXEL  (CENTER_PIXEL - 20)
#define FULL_HALF  (EXPANSION2_CENTER_PIXEL - SEGMENT_START)  // half-width at full expand, from expansion2's own center
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
// once the audio hits 00:00:26:07.
#define RESUME_BREATHE_MS  26233  // 00:00:26:07

// Breathing pulse (same shape as State 3's old hold on the main board).
#define BREATHE_BPM    9
#define BREATHE_MIN   20
#define BREATHE_MAX  255

// "station2" - a 60-pixel section (matching State 2's width on the main
// board), now centered on pixel 135 (105-164, was 110-169) - same slow
// breathe cycle as State 2's idle hold there (dims to 80% over 20s,
// recovers over 3s, repeating).
#define STATION2_SEGMENT_START  105
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

// "state5" (mode Z) - glitch/swing/comet sequence, on the same 60-pixel
// section as expansion2's final core (same position and length). Tied to
// expansion2's own constants so they stay in sync if that position ever
// changes again.
#define STATE5_SEGMENT_START  (EXPANSION2_CENTER_PIXEL - SHRINK2_TARGET_HALF)
#define STATE5_SEGMENT_END    (EXPANSION2_CENTER_PIXEL + SHRINK2_TARGET_HALF - 1)

// Glitches get FASTER each time (not dimmer, unlike state5x) - interval
// shrinks by STATE5Z_GLITCH_DECAY each time, down to a floor.
#define STATE5Z_GLITCH_END_MS             1200  // 00:00:01:06
#define STATE5Z_GLITCH_START_INTERVAL_MS   300
#define STATE5Z_GLITCH_MIN_INTERVAL_MS      40
#define STATE5Z_GLITCH_DECAY               0.7f
#define STATE5Z_GLITCH_BLIP_MS              30
// Swings left/right by 30px around the midpoint (140).
#define STATE5Z_SWING_END_MS               3167  // 00:00:02:05 + 1 second
#define STATE5Z_SWING_AMPLITUDE_PX           30
#define STATE5Z_SWING_BPM                   180  // not specified - ~2 full swings in the window
// From 00:00:01:07 to 00:00:01:29, the right half of the swinging window
// (center to its right edge) glitches on/off instead of staying solid.
#define STATE5Z_SWING_RIGHT_GLITCH_START_MS  1233  // 00:00:01:07
#define STATE5Z_SWING_RIGHT_GLITCH_END_MS    1967  // 00:00:01:29
#define STATE5Z_SWING_GLITCH_BLIP_MS           60  // not specified - fast on/off rate
// Comet starts right at STATE5Z_SWING_END_MS (3167ms) - no separate
// shrink stage anymore, the swing just runs until this exact moment.
// Needs to be fully melted into pixel 0 by 00:00:05:25 (5833ms) - so it
// travels for 5833-3167 = 2666ms, eased in-out (accelerates at the
// start, decelerates approaching pixel 0) instead of the constant speed
// every other comet in this project uses.
// STATE5Z_SHRINK_MS/doState5zShrink() are unused now (kept in case a
// shrink transition is wanted again later).
#define STATE5Z_SHRINK_MS                   300
#define STATE5Z_COMET_TRAVEL_MS            2666
#define STATE5Z_COMET_TRAIL_LENGTH           15
#define STATE5Z_COMET_FADE_OUT_PX             4

// "state5x" (mode X) - glitch/red-shrink/fast-comet sequence over the
// WHOLE physical strip (0-SEGMENT_END), not the 20-cut segment State 3
// uses. Timings not specified beyond the two given timestamps - picked
// reasonable paces, flagged for adjustment.
// Glitch now ends at 700ms (not 00:00:01:09/1300ms as originally given)
// so the red shrink phase below is a full second long (700-1700ms),
// making "the last second of the full expansion" - the whole 0-1700ms
// lead-in - actually be a full second, landing at the same 00:00:01:21
// point as before.
#define STATE5X_GLITCH_END_MS        700
#define STATE5X_GLITCH_INTERVAL_MS   150  // gap between glitch flickers
#define STATE5X_GLITCH_BLIP_MS        40  // how long each flicker blip stays on
#define STATE5X_SHRINK_END_MS       1700  // 00:00:01:21 - unchanged, still when comets start
// State 5x's own midpoint - separate from EXPANSION2_CENTER_PIXEL (State
// 3's expansion2 and State 5 mode Z both still use that one), 6 pixels
// further down.
#define STATE5X_CENTER_PIXEL  (EXPANSION2_CENTER_PIXEL - 6)
#define STATE5X_COMET_TRAVEL_MS      300  // "very fast" - 150px in 300ms
#define STATE5X_COMET_TRAIL_LENGTH    15  // shorter/snappier than the usual 30
#define STATE5X_COMET_FADE_OUT_PX      4  // melt-into-target width, same as other comets
#define STATE5X_COMET_GLOW_MS         80  // brief landing glow between repeats
CRGB STATE5_RED_COLOR = CRGB::Red; // shared by both State 5 modes X and Z

CRGB leds[NUM_LEDS];
CRGB STRIP2_COLOR = CRGB::White; // station2 (State 4) and state5x's glitch phase
// State 3's own color on this strip - a cool blue-white, more blue than
// STRIP2_COLOR's plain white. Used by expansion1 (doBreathePoint/
// doExpand/doHold) and expansion2 (renderCoreWithFade) only.
CRGB STATE3_COLOR = CRGB(150, 180, 255);

enum Stage2 {
  STAGE2_IDLE,
  STAGE2_EXPANSION1_COMET_WAIT, STAGE2_EXPANSION1_COMET, STAGE2_EXPANSION1_COMET_GLOW,
  STAGE2_DARK_WAIT, STAGE2_BREATHE_POINT, STAGE2_EXPAND, STAGE2_HOLD,
  STAGE2_SHRINK2, STAGE2_HOLD2,
  STAGE2_STATION2_FLICKER, STAGE2_STATION2_HOLD,
  STAGE2_STATE5X_GLITCH, STAGE2_STATE5X_SHRINK, STAGE2_STATE5X_COMET, STAGE2_STATE5X_GLOW,
  STAGE2_STATE5Z_GLITCH, STAGE2_STATE5Z_SWING, STAGE2_STATE5Z_SHRINK, STAGE2_STATE5Z_COMET
};
Stage2 stage = STAGE2_IDLE;

unsigned long triggerTime = 0;
unsigned long expansion1CometPhaseStartTime = 0; // t=0 for the pre-expansion1 comets' own schedule
unsigned long expansion1CometStageStartTime = 0; // t=0 for the CURRENT comet's travel
int expansion1CometRepeatIndex = 0;
bool expansion1CometLaunched = false; // true once the CURRENT comet's travel has begun
unsigned long shrink2StartTime = 0;
unsigned long station2StartTime = 0;
int station2FlickerIndex = 0;
bool station2FlickerBlipActive = false;
unsigned long station2FlickerBlipStartElapsed = 0;

unsigned long state5xStartTime = 0;      // t=0 for the whole state5x sequence (glitch+shrink)
unsigned long state5xStageStartTime = 0; // t=0 for the current comet/glow sub-stage

unsigned long state5zStartTime = 0;      // t=0 for the whole state5z sequence (glitch+swing)
unsigned long state5zStageStartTime = 0; // t=0 for the current shrink/comet sub-stage
unsigned long state5zNextGlitchTime = 0; // ms since state5zStartTime when the next glitch fires
unsigned long state5zGlitchInterval = 0; // current (shrinking) gap between glitches
bool state5zGlitchBlipActive = false;
unsigned long state5zGlitchBlipStart = 0;
int state5zShrinkStartCenter = 0;        // wherever the swing was when the shrink began

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
    case STAGE2_EXPANSION1_COMET_WAIT: doExpansion1CometWait(); break;
    case STAGE2_EXPANSION1_COMET:      doExpansion1Comet(); break;
    case STAGE2_EXPANSION1_COMET_GLOW: doExpansion1CometGlow(); break;
    case STAGE2_DARK_WAIT:    doDarkWait(); break;
    case STAGE2_BREATHE_POINT: doBreathePoint(); break;
    case STAGE2_EXPAND:       doExpand(); break;
    case STAGE2_HOLD:         doHold(); break;
    case STAGE2_SHRINK2:      doShrink2(); break;
    case STAGE2_HOLD2:        doHold2(); break;
    case STAGE2_STATION2_FLICKER: doStation2Flicker(); break;
    case STAGE2_STATION2_HOLD:    doStation2Hold(); break;
    case STAGE2_STATE5X_GLITCH:   doState5xGlitch(); break;
    case STAGE2_STATE5X_SHRINK:   doState5xShrink(); break;
    case STAGE2_STATE5X_COMET:    doState5xComet(); break;
    case STAGE2_STATE5X_GLOW:     doState5xGlow(); break;
    case STAGE2_STATE5Z_GLITCH:   doState5zGlitch(); break;
    case STAGE2_STATE5Z_SWING:    doState5zSwing(); break;
    case STAGE2_STATE5Z_SHRINK:   doState5zShrink(); break;
    case STAGE2_STATE5Z_COMET:    doState5zComet(); break;
  }
}

// ---------- Read "state3" / "station2" / "state5" / "state5x" / anything else from Serial and react ----------
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
        } else if (strcmp(cmdBuffer, "state5") == 0) {
          startState5();
        } else if (strcmp(cmdBuffer, "state5x") == 0) {
          startState5x();
        } else if (strcmp(cmdBuffer, "b") == 0) {
          startState3TestFromBreathe();
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

// ---------- Any command other than "state3"/"station2"/"state5" just blanks this strip ----------
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
  stage = STAGE2_EXPANSION1_COMET_WAIT;
  Serial.println(F("State 3 (strip 2): waiting..."));
}

// ---------- Dark until EXPANSION1_COMET_START_MS (2600ms) ----------
void doExpansion1CometWait() {
  if (millis() - triggerTime >= EXPANSION1_COMET_START_MS) {
    expansion1CometPhaseStartTime = millis();
    expansion1CometRepeatIndex = 0;
    expansion1CometLaunched = false;
    stage = STAGE2_EXPANSION1_COMET;
    Serial.println(F("State 3 (strip 2): pre-expansion1 comets starting..."));
  }
}

// ---------- The scheduled start time (ms since the comet phase began) for the Nth comet ----------
unsigned long expansion1CometScheduledStart(int index) {
  return EXPANSION1_COMET1_START_MS + (unsigned long)index * EXPANSION1_COMET_INTERVAL_MS;
}

// ---------- Fade-in only (each comet's own start, time-based) - same
// shape as COM8's state1CometFadeIn() ----------
uint8_t expansion1CometFadeIn(unsigned long elapsedSinceStart) {
  if (elapsedSinceStart < EXPANSION1_COMET_FADE_IN_MS) {
    return (uint8_t)(255.0 * elapsedSinceStart / EXPANSION1_COMET_FADE_IN_MS);
  }
  return 255;
}

// ---------- Draws the comet's trail at a given (fractional) head
// position - mirrors COM8's drawState3Comet(), just travelling the
// opposite direction (decreasing pixels, so the trail sits AHEAD in
// index terms, toward SEGMENT_END) and melting into CENTER_PIXEL
// instead of the far end. ----------
void drawExpansion1Comet(float pos, uint8_t fadeInBrightness) {
  int headPos = (int)pos;
  for (int offset = 0; offset < EXPANSION1_COMET_TRAIL_LENGTH; offset++) {
    int px = headPos + offset;
    if (px > SEGMENT_END || px < CENTER_PIXEL) continue;

    uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (EXPANSION1_COMET_TRAIL_LENGTH - 1));

    uint8_t endFade = 255;
    int distToEnd = px - CENTER_PIXEL;
    if (distToEnd <= EXPANSION1_COMET_FADE_OUT_PX) {
      float frac = (float)distToEnd / EXPANSION1_COMET_FADE_OUT_PX;
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

// ---------- One comet pass, at constant speed (matching COM8's own
// comet speed), from SEGMENT_END down to CENTER_PIXEL - mirrors COM8's
// doState3Comet(), waiting for its own scheduled launch first. ----------
void doExpansion1Comet() {
  if (!expansion1CometLaunched) {
    unsigned long phaseElapsed = millis() - expansion1CometPhaseStartTime;
    if (phaseElapsed < expansion1CometScheduledStart(expansion1CometRepeatIndex)) {
      FastLED.clear();
      FastLED.show();
      return; // waiting for this comet's scheduled start
    }
    expansion1CometLaunched = true;
    expansion1CometStageStartTime = millis();
  }

  unsigned long absElapsed = millis() - expansion1CometStageStartTime;
  FastLED.clear();

  float pos = SEGMENT_END - EXPANSION1_COMET_SPEED_PXPMS * absElapsed;
  float tailPos = pos + (EXPANSION1_COMET_TRAIL_LENGTH - 1);
  if (tailPos > CENTER_PIXEL) {
    drawExpansion1Comet(pos, expansion1CometFadeIn(absElapsed));
    FastLED.show();
  } else {
    expansion1CometStageStartTime = millis();
    expansion1CometLaunched = false; // reset - gates the NEXT comet's scheduled wait
    stage = STAGE2_EXPANSION1_COMET_GLOW;
  }
}

// ---------- Brief solid glow at CENTER_PIXEL, bridging one comet pass
// into the next (or into the existing dark-wait-for-breathe, on the
// last pass) - mirrors COM8's doState3Glow(). ----------
void doExpansion1CometGlow() {
  FastLED.clear();
  leds[CENTER_PIXEL] = STATE3_COLOR;
  FastLED.show();

  if (millis() - expansion1CometStageStartTime >= EXPANSION1_COMET_GLOW_MS) {
    expansion1CometRepeatIndex++;

    if (expansion1CometRepeatIndex >= EXPANSION1_COMET_REPEATS) {
      stage = STAGE2_DARK_WAIT;
      Serial.println(F("State 3 (strip 2): pre-expansion1 comets done - waiting..."));
    } else {
      expansion1CometStageStartTime = millis();
      stage = STAGE2_EXPANSION1_COMET;
      Serial.print(F("State 3 (strip 2): pre-expansion1 comet pass "));
      Serial.println(expansion1CometRepeatIndex + 1);
    }
  }
}

// Debug/test only ("b" command) - how many ms after pressing "b"
// expansion2 should already be starting. Revealed starting POINT is now
// 00:00:09:11 (9366ms) - so the wait until expansion2 shows up is still
// 21099-9366 = 11733ms (~11.7s), unchanged by the state3 timeline shift.
#define STATE3_TEST_B_LEAD_MS  11733

// ---------- Debug/test only ("b" command) - skips straight ahead so
// expansion2 begins STATE3_TEST_B_LEAD_MS (~11.7s) after this is pressed,
// instead of waiting through the ~12.4s dark lead-in + full hold every
// time. Backdating triggerTime (rather than jumping the stage alone)
// means breathe/expand/hold still cascade through correctly first (all
// within a few ms, since they're already "in the past") before landing
// on the hold with 4s left to go - every absolute-timed transition after
// this point (expansion2, breathe resume) still lands correctly too. ----------
void startState3TestFromBreathe() {
  FastLED.clear();
  FastLED.show();
  triggerTime = millis() - (SHRINK2_START_MS - STATE3_TEST_B_LEAD_MS);
  stage = STAGE2_BREATHE_POINT;
  Serial.println(F("TEST: State 3 (strip 2) - jumped ahead, expansion2 in 4s..."));
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
  CRGB c = STATE3_COLOR;
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
    CRGB c = STATE3_COLOR;
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
  CRGB c = STATE3_COLOR;
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

    CRGB c = STATE3_COLOR;
    c.nscale8(brightness);
    leds[i] = c;
  }
}

// ---------- expansion2: shrinks from the full width down to a 60px core
// centered on EXPANSION2_CENTER_PIXEL (its own center, 10px back from
// expansion1's), over SHRINK2_MS - the dim halo around the core shrinks
// right along with it (not staying fixed at the full width), fading to
// black beyond it. ----------
void doShrink2() {
  unsigned long elapsed = millis() - shrink2StartTime;
  float t = (float)elapsed / SHRINK2_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);
  int innerHalf = FULL_HALF - (int)((FULL_HALF - SHRINK2_TARGET_HALF) * eased);
  int outerHalf = innerHalf + DIM_HALO_EXTRA_PX;

  // innerEnd/outerEnd use half-1 (not half) so the final core width
  // lands on exactly 60 pixels (110-169) instead of 61 -
  // EXPANSION2_CENTER_PIXEL (140) sits just inside the start half, not
  // dead center, since 60 is an even width and can't be perfectly
  // centered on a single pixel.
  int innerStart = EXPANSION2_CENTER_PIXEL - innerHalf;
  int innerEnd = EXPANSION2_CENTER_PIXEL + innerHalf - 1;
  int outerStart = EXPANSION2_CENTER_PIXEL - outerHalf;
  int outerEnd = EXPANSION2_CENTER_PIXEL + outerHalf - 1;
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

  int innerStart = EXPANSION2_CENTER_PIXEL - innerHalf;
  int innerEnd = EXPANSION2_CENTER_PIXEL + innerHalf - 1;  // exactly 60px total
  int outerStart = EXPANSION2_CENTER_PIXEL - outerHalf;
  int outerEnd = EXPANSION2_CENTER_PIXEL + outerHalf - 1;
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

// ---------- Start (or restart) State 5 mode Z ----------
void startState5() {
  FastLED.clear();
  FastLED.show();
  state5zStartTime = millis();
  state5zGlitchInterval = STATE5Z_GLITCH_START_INTERVAL_MS;
  state5zNextGlitchTime = 0;
  state5zGlitchBlipActive = false;
  stage = STAGE2_STATE5Z_GLITCH;
  Serial.println(F("State 5z (strip 2): glitching (accelerating)..."));
}

// ---------- Mode Z phase 1: glitches on the 60px section, each one
// FASTER than the last (shrinking interval, not dimming), until
// STATE5Z_GLITCH_END_MS (00:00:01:06). ----------
void doState5zGlitch() {
  unsigned long elapsed = millis() - state5zStartTime;

  if (state5zGlitchBlipActive) {
    if (millis() - state5zGlitchBlipStart >= STATE5Z_GLITCH_BLIP_MS) {
      state5zGlitchBlipActive = false;
    }
  } else if (elapsed >= state5zNextGlitchTime) {
    state5zGlitchBlipActive = true;
    state5zGlitchBlipStart = millis();
    state5zNextGlitchTime = elapsed + state5zGlitchInterval;
    state5zGlitchInterval = (unsigned long)(state5zGlitchInterval * STATE5Z_GLITCH_DECAY);
    if (state5zGlitchInterval < STATE5Z_GLITCH_MIN_INTERVAL_MS) {
      state5zGlitchInterval = STATE5Z_GLITCH_MIN_INTERVAL_MS;
    }
  }

  CRGB c = state5zGlitchBlipActive ? STATE5_RED_COLOR : CRGB::Black;

  FastLED.clear();
  for (int i = STATE5_SEGMENT_START; i <= STATE5_SEGMENT_END; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (elapsed >= STATE5Z_GLITCH_END_MS) {
    stage = STAGE2_STATE5Z_SWING;
    Serial.println(F("State 5z (strip 2): swinging..."));
  }
}

// ---------- Mode Z phase 2: the 60px section swings left/right by 30px
// around its resting center (140), until STATE5Z_SWING_END_MS
// (00:00:03:05). From STATE5Z_SWING_RIGHT_GLITCH_START_MS to _END_MS
// (00:00:01:07-00:00:01:29), the right half of the swinging window
// (center to its right edge) glitches on/off instead of staying solid. ----------
void doState5zSwing() {
  unsigned long elapsed = millis() - state5zStartTime;

  int offset = beatsin16(STATE5Z_SWING_BPM, -STATE5Z_SWING_AMPLITUDE_PX, STATE5Z_SWING_AMPLITUDE_PX);
  int center = EXPANSION2_CENTER_PIXEL + offset;
  int rangeStart = center - SHRINK2_TARGET_HALF;
  int rangeEnd = center + SHRINK2_TARGET_HALF - 1;
  if (rangeStart < 0) rangeStart = 0;
  if (rangeEnd > SEGMENT_END) rangeEnd = SEGMENT_END;

  bool inRightGlitchWindow = elapsed >= STATE5Z_SWING_RIGHT_GLITCH_START_MS
                           && elapsed <= STATE5Z_SWING_RIGHT_GLITCH_END_MS;
  bool rightSideGlitchOn = (millis() / STATE5Z_SWING_GLITCH_BLIP_MS) % 2 == 0;

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    bool isRightHalf = i >= center;
    if (isRightHalf && inRightGlitchWindow && !rightSideGlitchOn) {
      continue; // right half glitching "off" this blip
    }
    leds[i] = STATE5_RED_COLOR;
  }
  FastLED.show();

  if (elapsed >= STATE5Z_SWING_END_MS) {
    // Comet starts right at 00:00:03:05 - no separate shrink stage
    // anymore, the swing just keeps going right up until this moment.
    state5zStageStartTime = millis();
    stage = STAGE2_STATE5Z_COMET;
    Serial.println(F("State 5z (strip 2): comet to pixel 0..."));
  }
}

// ---------- Mode Z phase 3a: shrinks from wherever the swing left off
// back to a single point at the midpoint (140), over STATE5Z_SHRINK_MS. ----------
void doState5zShrink() {
  unsigned long elapsed = millis() - state5zStageStartTime;
  float t = (float)elapsed / STATE5Z_SHRINK_MS;
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);
  int currentCenter = state5zShrinkStartCenter + (int)((EXPANSION2_CENTER_PIXEL - state5zShrinkStartCenter) * eased);
  int currentHalf = (int)(SHRINK2_TARGET_HALF * (1.0 - eased));

  int rangeStart = currentCenter - currentHalf;
  int rangeEnd = currentCenter + currentHalf;

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    leds[i] = STATE5_RED_COLOR;
  }
  FastLED.show();

  if (t >= 1.0) {
    state5zStageStartTime = millis();
    stage = STAGE2_STATE5Z_COMET;
    Serial.println(F("State 5z (strip 2): comet to pixel 0..."));
  }
}

// ---------- Mode Z phase 3b: single comet (no repeat), launches from the
// midpoint (140) and races DOWN to pixel 0, melting in as its tail
// arrives, then holds dark - the comet goes only once. Eased in-out
// (accelerates at the start, decelerates near pixel 0) instead of
// constant speed, over STATE5Z_COMET_TRAVEL_MS. The "virtual" travel
// distance includes the trail length so the WHOLE comet (not just the
// head) finishes exactly at t=1.0, landing precisely on 00:00:05:25. ----------
void doState5zComet() {
  unsigned long elapsed = millis() - state5zStageStartTime;
  float t = (float)elapsed / STATE5Z_COMET_TRAVEL_MS;
  bool done = (t >= 1.0);
  if (t > 1.0) t = 1.0;

  // Biased toward a faster initial ramp-up (pow < 1 pulls the curve up
  // earlier) while still landing on exactly 0 and 1 at the endpoints, so
  // the total travel time/landing point don't change.
  float eased = pow(easeInOutCubic(t), 0.6f);
  float virtualTravel = EXPANSION2_CENTER_PIXEL + (STATE5Z_COMET_TRAIL_LENGTH - 1);
  float pos = EXPANSION2_CENTER_PIXEL - virtualTravel * eased;

  FastLED.clear();
  if (!done) {
    int headPos = (int)pos;
    for (int offset = 0; offset < STATE5Z_COMET_TRAIL_LENGTH; offset++) {
      int px = headPos + offset;
      if (px < 0 || px > EXPANSION2_CENTER_PIXEL) continue;

      uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (STATE5Z_COMET_TRAIL_LENGTH - 1));

      uint8_t endFade = 255;
      int distToEnd = px;
      if (distToEnd <= STATE5Z_COMET_FADE_OUT_PX) {
        float frac = (float)distToEnd / STATE5Z_COMET_FADE_OUT_PX;
        if (frac < 0) frac = 0;
        endFade = (uint8_t)(255.0 * frac);
      }

      CRGB c = STATE5_RED_COLOR;
      c.nscale8(tailFade);
      c.nscale8(endFade);
      leds[px] = c;
    }
    FastLED.show();
  } else {
    FastLED.clear();
    FastLED.show();
    stage = STAGE2_IDLE; // comet goes only once - done, holds dark, waits for next command
    Serial.println(F("State 5z (strip 2): comet complete - holding dark, waiting..."));
  }
}

// ---------- Start (or restart) State 5 mode X ----------
void startState5x() {
  FastLED.clear();
  FastLED.show();
  state5xStartTime = millis();
  stage = STAGE2_STATE5X_GLITCH;
  Serial.println(F("State 5x (strip 2): glitching..."));
}

// ---------- Mode X phase 1: glitch flickers across the whole physical
// strip, each one dimmer than the last, until STATE5X_GLITCH_END_MS
// (00:00:01:09). ----------
void doState5xGlitch() {
  unsigned long elapsed = millis() - state5xStartTime;

  unsigned long posInCycle = elapsed % STATE5X_GLITCH_INTERVAL_MS;
  bool inBlip = posInCycle < STATE5X_GLITCH_BLIP_MS;

  uint8_t brightness = 0;
  if (inBlip) {
    float progress = (float)elapsed / STATE5X_GLITCH_END_MS;
    if (progress > 1.0) progress = 1.0;
    brightness = (uint8_t)(255.0 * (1.0 - progress));
  }

  CRGB c = STRIP2_COLOR;
  c.nscale8(brightness);

  for (int i = 0; i <= SEGMENT_END; i++) {
    leds[i] = c;
  }
  FastLED.show();

  if (elapsed >= STATE5X_GLITCH_END_MS) {
    state5xStartTime = millis();
    stage = STAGE2_STATE5X_SHRINK;
    Serial.println(F("State 5x (strip 2): red flash - shrinking to midpoint..."));
  }
}

// ---------- Mode X phase 2: instant red flash across the whole strip,
// then shrinks down to a single point at STATE5X_CENTER_PIXEL (144),
// landing by STATE5X_SHRINK_END_MS (00:00:01:21). ----------
void doState5xShrink() {
  unsigned long elapsed = millis() - state5xStartTime;
  float t = (float)elapsed / (STATE5X_SHRINK_END_MS - STATE5X_GLITCH_END_MS);
  if (t > 1.0) t = 1.0;

  float eased = easeOutCubic(t);
  int fullHalfLow = STATE5X_CENTER_PIXEL;
  int fullHalfHigh = SEGMENT_END - STATE5X_CENTER_PIXEL;
  int currentHalfLow = (int)(fullHalfLow * (1.0 - eased));
  int currentHalfHigh = (int)(fullHalfHigh * (1.0 - eased));

  int rangeStart = STATE5X_CENTER_PIXEL - currentHalfLow;
  int rangeEnd = STATE5X_CENTER_PIXEL + currentHalfHigh;

  FastLED.clear();
  for (int i = rangeStart; i <= rangeEnd; i++) {
    leds[i] = STATE5_RED_COLOR;
  }
  FastLED.show();

  if (t >= 1.0) {
    state5xStageStartTime = millis();
    stage = STAGE2_STATE5X_COMET;
    Serial.println(F("State 5x (strip 2): fast comets starting..."));
  }
}

// ---------- Mode X phase 3: fast red comet, launches from
// STATE5X_CENTER_PIXEL (144) and races DOWN to pixel 0 (reversed
// direction from every other comet in this project) - melts into pixel
// 0 as its tail arrives, then a brief landing glow, then repeats
// forever. ----------
void doState5xComet() {
  unsigned long elapsed = millis() - state5xStageStartTime;

  float speed = (float)STATE5X_CENTER_PIXEL / STATE5X_COMET_TRAVEL_MS; // px/ms
  float pos = STATE5X_CENTER_PIXEL - speed * elapsed;
  float tailPos = pos + (STATE5X_COMET_TRAIL_LENGTH - 1);

  FastLED.clear();
  if (tailPos > 0) {
    int headPos = (int)pos;
    for (int offset = 0; offset < STATE5X_COMET_TRAIL_LENGTH; offset++) {
      int px = headPos + offset; // trail extends BEHIND the head, toward higher pixels (back toward the midpoint)
      if (px < 0 || px > STATE5X_CENTER_PIXEL) continue;

      uint8_t tailFade = 255 - (uint8_t)(255.0 * offset / (STATE5X_COMET_TRAIL_LENGTH - 1));

      uint8_t endFade = 255;
      int distToEnd = px; // distance to pixel 0
      if (distToEnd <= STATE5X_COMET_FADE_OUT_PX) {
        float frac = (float)distToEnd / STATE5X_COMET_FADE_OUT_PX;
        if (frac < 0) frac = 0;
        endFade = (uint8_t)(255.0 * frac);
      }

      CRGB c = STATE5_RED_COLOR;
      c.nscale8(tailFade);
      c.nscale8(endFade);
      leds[px] = c;
    }
    FastLED.show();
  } else {
    state5xStageStartTime = millis();
    stage = STAGE2_STATE5X_GLOW;
  }
}

// ---------- Brief solid glow at pixel 0, bridging one comet pass into
// the next repeat. ----------
void doState5xGlow() {
  FastLED.clear();
  leds[0] = STATE5_RED_COLOR;
  FastLED.show();

  if (millis() - state5xStageStartTime >= STATE5X_COMET_GLOW_MS) {
    state5xStageStartTime = millis();
    stage = STAGE2_STATE5X_COMET;
  }
}

// ---------- Ease-out cubic: fast start, sudden slow-down near the end ----------
float easeOutCubic(float t) {
  float f = t - 1.0;
  return f * f * f + 1.0;
}

// ---------- Ease-in-out cubic: slow start that accelerates, then
// decelerates again toward the end - unlike easeOutCubic above, which is
// already fast from the very first frame. ----------
float easeInOutCubic(float t) {
  if (t < 0.5) {
    return 4.0 * t * t * t;
  }
  float f = (2.0 * t) - 2.0;
  return 0.5 * f * f * f + 1.0;
}
