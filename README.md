# Ars Electronica LED + Audio Control

An interactive light + sound installation: an Arduino Uno drives a WS2812B
LED strip through several "states," each with its own animation, synced to
audio playback on a laptop.

## Structure

- **`ARDUINO/led-pink/`** - the Arduino sketch (three tabs):
  - `led-pink.ino` - shared framework (setup/loop, serial command
    routing) and State 1 (a pair of comets that fade into a fixed point,
    then an expand/fade-out sequence, looping)
  - `state2.ino` - State 2 (scripted flicker intro, then an idle hold
    with breathing + occasional flicker)
  - `state3.ino` - State 3 (a faster, repeated comet effect in purple)
- **`READY/`** - the audio tracks used by the control scripts
- **`live_control.py`** - press 0/1/2/3 to trigger a state live, with
  the matching audio crossfading in
- **`send_state.py`** - trigger one state + its audio from the command
  line, once
- **`show_sync.py`** - a fixed, timed sequence of audio + state cues
- **`lights_off.py`** - holds the strip off (mostly unnecessary now -
  the board defaults to off on boot)

## Setup

**Arduino side** - compile/upload via
[`arduino-cli`](https://github.com/arduino/arduino-cli/releases) (not
included in this repo):

```
arduino-cli core install arduino:avr
arduino-cli lib install FastLED
arduino-cli compile --fqbn arduino:avr:uno ARDUINO/led-pink
arduino-cli upload -p COM7 --fqbn arduino:avr:uno ARDUINO/led-pink
```

(Swap `COM7` for whatever `arduino-cli board list` shows on your
machine.)

**Python side**:

```
pip install pyserial pygame-ce
python live_control.py
```

Edit `ARDUINO_PORT` at the top of each `.py` file if your board isn't
on `COM7`.

## Notes

- Serial commands are `state0`, `state1`, `state2`, `state3` (also
  accepted with a space: `state 1`).
- Only one program can hold the Arduino's serial port at a time - close
  the Arduino IDE's Serial Monitor before running any of the Python
  scripts, and vice versa.
- On this board, opening *or* closing the serial connection resets it -
  the Python scripts are written with that in mind (they stay connected
  for as long as they need the board to hold a state).
