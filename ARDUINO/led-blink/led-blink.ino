/*
 * led-blink — whole strip blinks pink on/off, forever
 * Ars Electronica
 *
 * Needs the FastLED library:
 *   Tools > Manage Libraries... > search "FastLED" > Install
 *
 * WIRING
 *   Strip DIN  ->  330 ohm resistor  ->  Arduino pin 6
 *   Strip GND  ->  Arduino GND
 *   Strip +5V  ->  Arduino 5V   (short test strip only)
 *
 *   Use the DIN end — the end the arrows point AWAY from.
 */

#include <FastLED.h>

// ---- set these to match your strip -------------------------------
#define COLOR_ORDER   GBR   // GRB came out blue, so try GBR next, then RBG if still wrong
#define NUM_LEDS      30      // how many pixels you actually have
#define DATA_PIN       6
#define BRIGHTNESS    60      // 0-255. Keep low while USB-powered.
#define MAX_MILLIAMPS 450     // USB-safe ceiling. Raise with a real PSU.
#define BLINK_MS      500     // time each on/off phase lasts
// ------------------------------------------------------------------

// #FF4076  ->  R 255, G 64, B 118
const CRGB PINK = CRGB(0xFF, 0x40, 0x76);

CRGB leds[NUM_LEDS];

void setup() {
  Serial.begin(9600);
  Serial.println("led-blink ready — strip should start blinking pink");

  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
}

void loop() {
  fill_solid(leds, NUM_LEDS, PINK);
  FastLED.show();
  delay(BLINK_MS);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(BLINK_MS);
}
