/*
 * led-toggle — fresh start
 * Ars Electronica: light + sound
 *
 * One LED, one toggle switch. Flip the switch, the LED follows.
 *
 * WIRING (change the pin numbers below if yours differ)
 *   LED    pin 9  ->  resistor (220R) -> LED long leg
 *                     LED short leg   -> GND
 *   TOGGLE pin 2  ->  one leg of the switch
 *                     other leg       -> GND
 *
 * The switch needs no resistor: INPUT_PULLUP uses the one inside
 * the Arduino. That means the pin reads HIGH when open and LOW
 * when the switch is closed — hence the == LOW test below.
 */

const int LED_PIN    = 9;
const int TOGGLE_PIN = 2;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(TOGGLE_PIN, INPUT_PULLUP);

  Serial.begin(9600);
  Serial.println("led-toggle ready");
}

void loop() {
  bool switchClosed = (digitalRead(TOGGLE_PIN) == LOW);

  digitalWrite(LED_PIN, switchClosed ? HIGH : LOW);

  // Watch this in Tools > Serial Monitor to confirm the switch is read.
  static bool last = !switchClosed;
  if (switchClosed != last) {
    Serial.println(switchClosed ? "ON" : "OFF");
    last = switchClosed;
  }

  delay(20);  // crude debounce
}
