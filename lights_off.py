"""
lights_off.py - turns all LEDs off and holds that state.

Sends "state0" then keeps the connection open indefinitely - closing
it would reset the board back to its power-up animation, undoing the
"off". Leave this window open for as long as you want the lights to
stay off. Press Ctrl+C to release it (the board will then reset back
to its normal power-up sequence).
"""

import time
import serial

ARDUINO_PORT = "COM7"
BAUD_RATE = 115200

print(f"Connecting to Arduino on {ARDUINO_PORT}...")
arduino = serial.Serial(ARDUINO_PORT, BAUD_RATE, timeout=1)
time.sleep(2)

arduino.write(b"state0\n")
print("Lights off. Keep this window open to hold that state.")

try:
    while True:
        time.sleep(60)
except KeyboardInterrupt:
    pass

arduino.close()
print("Released - board will reset now.")
