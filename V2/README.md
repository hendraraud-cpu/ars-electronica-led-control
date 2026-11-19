# V2 - Ars Electronica LED + audio installation (single Arduino)

Press a key on the keyboard, a track plays and a light show runs on ONE LED strip,
driven by ONE Arduino Uno. (V1, the 3-board version, is the rest of this repo.)

## What it does

| Key | Audio (`../NOVEMBER-ARS-AUDIO/`) | Light show on the strip |
|---|---|---|
| `1` | `01_DOOR OPEN.MP3` (crossfades in over 3s, loops from 0:00) | Door Open: pink comet, expand, breathe, fade (3 sections that restart in sync with the loop) |
| `2` | `02_STATION 1.MP3` | Station 1: scripted flickers, then a slow breathe |
| `4` | `05_STATION 2.MP3` | Station 2: two mild flickers, then a 60-pixel white section breathing slowly |
| `9` | `06_STATION 3.MP3` | Station 3: a red light breathing |
| `8` | `05_REVEALING CHAMBER.mp3` (plays 0:00-2:10, then loops 0:20-2:10) | none yet - the strip is blanked |
| `q` | - | quit |

Audio hard-cuts between states except Door Open, which crossfades.

## Run it

Needs Windows, Python 3 and `pip install pyserial pygame-ce imageio-ffmpeg`.

1. Flash the Arduino once (see below), plug it in by USB.
2. Check the COM port in Device Manager. If it is not `COM8`, change `ARDUINO_PORT` near the top of `live_control.py` (and `PORT` in `light_test.py`).
3. From this repo:

```powershell
py V2\live_control.py
```

Only one program can hold the COM port at a time - close `light_test.py` / the Arduino IDE serial monitor first.

## Flash the Arduino

Open `V2/ARDUINO/led-pink/` in the Arduino IDE (board: Arduino Uno, library: **FastLED**), or:

```powershell
arduino-cli compile --fqbn arduino:avr:uno V2\ARDUINO\led-pink
arduino-cli upload  -p COM8 --fqbn arduino:avr:uno V2\ARDUINO\led-pink
```

Sketch files (all merged into one program by the Arduino build):
- `led-pink.ino` - serial command parser, State 1 (Door Open), shared helpers
- `state2.ino` - State 2 (Station 1)
- `state4.ino` - State 4 (Station 2)
- `station3.ino` - Station 3 (red breathing)

`V2/ARDUINO/led-strip2/` and `led-strip3/` are copies of V1's other two boards and are NOT used in V2.

Commands the sketch understands over serial (115200 baud, newline-terminated):
`state0` (all off), `state1`, `state2`, `state4`, `station3`.

## Wiring

- Strip data -> Arduino **pin 6** (`DATA_PIN` in `led-pink.ino`), WS2812B / `GRB`.
- Strip 5V/GND from a separate power supply (1000uF capacitor across it); the **Arduino GND, the strip GND and the supply V- must all be tied together**.
- `NUM_LEDS` is **150** - approximate, confirm against the real strip.
- The lit pixel ranges used: State 1 uses pixels ~37-95 (centre 66); States 2/4/Station 3 use pixels 37-96.

## Testing without hardware

```powershell
py dry_run.py V2            # prints every key -> command -> audio, checks files exist, touches nothing
py dry_run.py V2 --live     # real keyboard + real audio, FAKE Arduino (add --silent to mute)
py light_test.py            # lights only, on the real Arduino: press 1/2/4/9, 0=off, q=quit
```

## Known state / open items

- **The strip did not respond when last tested** (Nov 2026). The Arduino accepted every command and the firmware ran correctly (verified over serial, and with plain colour-cycle tests on pin 6 and on all pins), but the physical strip showed a static random-colour pattern that never changed. Suspected hardware: a damaged first pixel, a missing common ground, the data wire not reaching the strip's `DIN`, or a strip/controller that is not a plain addressable WS2812B. Try a known-good strip first.
- Revealing Chamber (key `8`) has no light show decided.
- `NUM_LEDS` and the pixel ranges need confirming on the real strip.
- V1 (the previous 3-Arduino version) is kept untouched in the repo root as the backup.
