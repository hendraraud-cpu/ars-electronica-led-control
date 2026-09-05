// Quick test: lights the first 100 pixels solid white.
// Standalone sketch, unrelated to the main led-pink project - just
// for checking a strip/board works.

#include <FastLED.h>

#define DATA_PIN     6
#define NUM_LEDS     100
#define COLOR_ORDER  GRB
#define BRIGHTNESS   90

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 4000);

  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
}

void loop() {
  // Nothing to do - stays solid.
}
