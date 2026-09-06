/*
  pin-test - lights up the whole strip solid white on pin 7, to confirm
  that's actually where the second LED strip (COM7) is wired.

  If the strip lights up: pin 7 is correct.
  If nothing happens: it's wired to a different pin - tell Claude and
  it'll try another one.

  NUM_LEDS is set generously high (200, same as strip 1) just for this
  test - doesn't need to match the real count yet, just needs to be
  enough to light up whatever's actually connected.
*/

#include <FastLED.h>

#define DATA_PIN     7
#define NUM_LEDS     200
#define COLOR_ORDER  GRB

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::White;
  }
  FastLED.show();
}

void loop() {
}
