/*
  Station 3 (V2) - just a red light, breathing, forever (until the next
  command). Runs on the same 60-pixel section State 2 and State 4 use
  (STATE2_SEGMENT_START..END). Breathing rate/depth match this project's
  other holds (beatsin8(9, 20, 255)) - not specified, easy to change below.
  This tab is merged into led-pink.ino automatically. Triggered by the
  "station3" Serial command.
*/

#define STATION3_BREATHE_BPM  9
#define STATION3_BREATHE_MIN  20
#define STATION3_BREATHE_MAX  255

CRGB STATION3_COLOR = CRGB::Red;

void startStation3() {
  FastLED.clear();
  FastLED.show();
  currentStage = STAGE_STATION3_HOLD;
  Serial.println(F("Manual trigger: Station 3 - red breathing..."));
}

void doStation3Hold() {
  uint8_t breath = beatsin8(STATION3_BREATHE_BPM, STATION3_BREATHE_MIN, STATION3_BREATHE_MAX);
  CRGB c = STATION3_COLOR;
  c.nscale8(breath);

  FastLED.clear();
  for (int i = STATE2_SEGMENT_START; i <= STATE2_SEGMENT_END; i++) {
    leds[i] = c;
  }
  showThrottled();
}
