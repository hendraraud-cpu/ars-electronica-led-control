/*
  pin-test - lights up pixels 0-59 (60 pixels, from the start of the
  strip) solid white on pin 7 - same color as State 2 on the main board
  (03_STATION 1.MP3). Useful for visually counting/confirming how many
  physical LEDs the strip actually has.

  NUM_LEDS is set generously high (200, same as strip 1) just so the
  array is big enough - only the first 60 pixels actually get lit.
*/

#include <FastLED.h>

#define DATA_PIN     7
#define NUM_LEDS     200
#define COLOR_ORDER  GRB
#define LIT_COUNT     60

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear();
  for (int i = 0; i < LIT_COUNT; i++) {
    leds[i] = CRGB::White;
  }
  FastLED.show();
}

void loop() {
}
