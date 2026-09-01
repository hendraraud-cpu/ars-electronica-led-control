"""
send_state.py - trigger a state on demand: sends the LED command AND
plays that state's audio together, right when you run it.

This stands in for the real UI/sensor trigger until that's built -
for now, running this manually IS the trigger.

USAGE
-----
    py send_state.py 1        -> state1 + 02_DOOR OPEN.MP3
    py send_state.py 2        -> state2 + 03_STATION 1.MP3
    py send_state.py 3        -> state3 + 04_1 TRANSITION.MP3
    py send_state.py state2   -> also works, same as "2"

The script waits for the audio to finish before exiting. Press
Ctrl+C to stop early (cuts the audio, but the LEDs stay as they were
set - the Arduino keeps running independently of this script).

BEFORE RUNNING
--------------
Close the Arduino IDE's Serial Monitor first - only one program can
have the COM port open at a time.
"""

import sys
import time
import serial
import pygame

ARDUINO_PORT = "COM7"
BAUD_RATE = 115200
AUDIO_DIR = r"F:\03_ARS\READY"

# Which audio file goes with each state. Add more here as you build
# more states.
STATE_AUDIO = {
    "state0": "01_ON HOLD.MP3",
    "state1": "02_DOOR OPEN.MP3",
    "state2": "03_STATION 1.MP3",
    "state3": "04_1 TRANSITION.MP3",
}


def send_command_with_retry(arduino, command, max_attempts=3, reply_timeout=0.6):
    """Sends command and waits for any reply line, retrying if none comes
    back in time - papers over the rare case where a command gets dropped
    due to timing (very occasional, not a crash - the board recovers)."""
    for attempt in range(1, max_attempts + 1):
        arduino.reset_input_buffer()
        arduino.write((command + "\n").encode())
        end = time.time() + reply_timeout
        while time.time() < end:
            line = arduino.readline()
            if line:
                print(f"Board: {line.decode(errors='replace').strip()}")
                return True
        if attempt < max_attempts:
            print(f"(no reply, retrying - attempt {attempt + 1}/{max_attempts})")
    print("!!! board didn't respond after retries - check the connection !!!")
    return False


def main():
    if len(sys.argv) != 2:
        print("Usage: py send_state.py <1|2|3|state1|state2|state3>")
        sys.exit(1)

    arg = sys.argv[1].strip().lower()
    command = f"state{arg}" if arg.isdigit() else arg

    print(f"Connecting to Arduino on {ARDUINO_PORT}...")
    arduino = serial.Serial(ARDUINO_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)  # let the board finish resetting after the port opens

    print(f"Sending '{command}'...")
    send_command_with_retry(arduino, command)
    # IMPORTANT: don't close the connection yet - on this board, closing
    # the serial port resets it, same as opening one does. Closing early
    # (right after sending the command) was wiping out the state change
    # and restarting the board's power-up intro, which is why every
    # command looked like it played the same light. Stay connected for
    # the whole light sequence, close only at the very end.

    audio_file = STATE_AUDIO.get(command)
    if audio_file:
        path = AUDIO_DIR + "\\" + audio_file
        print(f"Playing {audio_file}...")
        pygame.mixer.init()
        sound = pygame.mixer.Sound(path)
        sound.play()
        try:
            while pygame.mixer.get_busy():
                time.sleep(0.1)
        except KeyboardInterrupt:
            sound.stop()
            print("Stopped early.")
    else:
        print(f"(No audio mapped to '{command}' - only the LED command was sent.)")

    arduino.close()
    print("Done.")


if __name__ == "__main__":
    main()
