/*
  pin-test - lights up pixels 0-59 (60 pixels, from the start of the
  strip) solid white on pin 7 - same color as State 2 on the main board
  (03_STATION 1.MP3). Useful for visually counting/confirming how many
  physical LEDs the strip actually has.

  NUM_LEDS is set generously high (200, same as strip 1) just so the
  array is big enough - only the first 60 pixels actually get lit.

  DEBUG: prints a heartbeat over Serial every second and calls
  FastLED.show() continuously (not just once) - so we can tell apart
  "board isn't running this code" from "board IS running it but the
  strip isn't responding" (wiring/power issue on the data line).
*/

#include <FastLED.h>

#define DATA_PIN     5
#define NUM_LEDS     200
#define COLOR_ORDER  GRB
#define LIT_COUNT     60

CRGB leds[NUM_LEDS];
unsigned long lastHeartbeat = 0;

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear();
  for (int i = 0; i < LIT_COUNT; i++) {
    leds[i] = CRGB::White;
  }
  FastLED.show();
  Serial.println(F("pin-test running - first 60 pixels should be white."));
}

void loop() {
  FastLED.show(); // repeated continuously, not just once at startup

  if (millis() - lastHeartbeat >= 1000) {
    lastHeartbeat = millis();
    Serial.println(F("pin-test heartbeat - still running, still showing first 60 white."));
  }
}
