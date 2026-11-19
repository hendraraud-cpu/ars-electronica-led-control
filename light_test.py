r"""
light_test.py - test the V2 light shows on COM8 from PowerShell, no audio.

    py F:\03_ARS\light_test.py             # menu: press a key to start a show, q to quit
    py F:\03_ARS\light_test.py station3    # run one show and hold it (Ctrl+C to stop)

Holds the serial port open the whole time - closing it resets the Uno and
blanks the strip, so the lights only stay on while this window is running.
Quit live_control.py first (only one program can hold COM8).
"""
import msvcrt
import sys
import time

import serial

PORT = "COM8"
# Same keys as the V2 live_control.py, so the muscle memory carries over.
SHOWS = {
    "1": ("state1", "Door Open          (01_DOOR OPEN)       - comet, expand, breathe"),
    "2": ("state2", "Station 1          (02_STATION 1)       - flickers, soft breathe"),
    "4": ("state4", "Station 2          (05_STATION 2)       - 2 flickers, 60px white breathing"),
    "9": ("station3", "Station 3          (06_STATION 3)       - red breathing"),
    "8": ("state0", "Revealing Chamber  (05_REVEALING CHAMBER) - no light show yet, strip blank"),
    "0": ("state0", "OFF (test only - not a live_control key)"),
}

try:
    board = serial.Serial(PORT, 115200, timeout=0.2)
except serial.SerialException as e:
    sys.exit(f"Can't open {PORT}: {e}\n(Is live_control.py or another program holding it? Is the board plugged in?)")
time.sleep(2.2)  # the Uno resets when the port opens
board.reset_input_buffer()


def send(cmd):
    print(f"\n>>> sending '{cmd}'")
    board.write((cmd + "\n").encode())


def drain():
    while True:
        line = board.readline()
        if not line:
            break
        print("   board:", line.decode(errors="replace").strip())


try:
    print("Light test on COM8. Same keys as V2 live_control.py.")
    print("PRESS A KEY to start a show (q to quit):")
    for k, (_, label) in SHOWS.items():
        print(f"   {k} -> {label}")
    if len(sys.argv) > 1:
        # optional: a show name typed after the filename starts that show right away
        send(sys.argv[1])
    while True:
        drain()
        if msvcrt.kbhit():
            key = msvcrt.getch().decode(errors="ignore").lower()
            if key == "q":
                break
            if key in SHOWS:
                send(SHOWS[key][0])
except KeyboardInterrupt:
    pass
finally:
    board.close()
    print("\nPort closed (the board resets and the strip goes dark).")
