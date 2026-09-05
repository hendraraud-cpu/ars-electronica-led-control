"""
show_sync.py - plays audio and triggers the Arduino LED states together

WHAT THIS DOES
--------------
Runs on this laptop, plugged into the Arduino over the same USB cable
you use for the Serial Monitor. At each scheduled moment it can:
  - play an audio file through this computer's speakers
  - crossfade from whatever's currently playing into a new track
  - send a command ("state 1", "state 2", "state 3") to the Arduino,
    which switches what the LED strip is doing
All of this happens from this one script on one clock, so it stays
in sync - no more manually hitting play and typing a command.

BEFORE RUNNING
--------------
1. Close the Arduino IDE's Serial Monitor (and ideally the whole IDE).
   Only one program can have the Arduino's COM port open at a time -
   if the Serial Monitor has it open, this script can't connect.
2. Check ARDUINO_PORT below matches Tools > Port in the Arduino IDE
   (e.g. "COM7") - it can change if you unplug/replug the board.
3. Check AUDIO_DIR below points at your actual audio folder.

HOW TO RUN IT
-------------
    py show_sync.py

HOW TO EDIT THE SHOW
---------------------
SHOW is a list of cues: (time_in_seconds, action, value).
  ("audio",     "filename.mp3")  -> starts playing that file (no fade)
  ("crossfade", "filename.mp3")  -> fades out whatever's currently
                                     playing while fading in this new
                                     file, over CROSSFADE_SECONDS
  ("led",       "state2")        -> sends that command to the Arduino
All times are seconds from when you run the script (t=0). When two
cues share the same time, list the "led" one first - a crossfade
blocks the script for CROSSFADE_SECONDS while it runs, so anything
listed after it at the same timestamp would fire late.
"""

import time
import serial
import pygame

# ---- EDIT THESE TO MATCH YOUR SETUP -------------------------------
ARDUINO_PORT = "COM8"
BAUD_RATE = 115200
AUDIO_DIR = r"F:\03_ARS\READY"
CROSSFADE_SECONDS = 5.0

# ---- EDIT THE SHOW ITSELF ------------------------------------------
# (time_seconds, action, value)
SHOW = [
    (0.0, "audio", "02_DOOR OPEN.MP3"),
    (0.0, "led",   "state1"),
    # When State 2 starts, crossfade the music over to 03_STATION 1.MP3.
    # Set TIME below to whenever State 2 should actually begin:
    # (TIME, "led",       "state2"),
    # (TIME, "crossfade", "03_STATION 1.MP3"),
    # When State 3 starts, 04_1 TRANSITION.MP3 begins playing.
    # Set TIME below to whenever State 3 should actually begin:
    # (TIME, "led",   "state3"),
    # (TIME, "audio", "04_1 TRANSITION.MP3"),
]
# ---------------------------------------------------------------------


def main():
    print(f"Connecting to Arduino on {ARDUINO_PORT}...")
    arduino = serial.Serial(ARDUINO_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)  # let the board finish resetting after the port opens
    print("Connected.")

    pygame.mixer.init()
    current_channel = None  # the Channel actually playing, for crossfade to
                             # fade FROM - must control the Channel's volume,
                             # not the Sound's: Sound.set_volume() does NOT
                             # affect a channel that's already playing, so
                             # that approach silently did nothing (both
                             # tracks stayed full volume, then the old one
                             # just cut to silence via .stop()).

    cues = sorted(SHOW, key=lambda c: c[0])
    start = time.time()

    for cue_time, action, value in cues:
        wait = cue_time - (time.time() - start)
        if wait > 0:
            time.sleep(wait)

        elapsed = time.time() - start

        if action == "audio":
            path = AUDIO_DIR + "\\" + value
            print(f"[{elapsed:6.2f}s] playing {value}")
            sound = pygame.mixer.Sound(path)
            current_channel = sound.play()
            current_channel.set_volume(1.0)

        elif action == "crossfade":
            path = AUDIO_DIR + "\\" + value
            print(f"[{elapsed:6.2f}s] crossfading to {value} over {CROSSFADE_SECONDS}s")
            new_sound = pygame.mixer.Sound(path)
            new_channel = new_sound.play()
            new_channel.set_volume(0.0)

            old_channel = current_channel
            steps = 40
            for i in range(1, steps + 1):
                frac = i / steps
                if old_channel is not None:
                    old_channel.set_volume(1.0 - frac)
                new_channel.set_volume(frac)
                time.sleep(CROSSFADE_SECONDS / steps)

            if old_channel is not None:
                old_channel.stop()
            current_channel = new_channel

        elif action == "led":
            print(f"[{elapsed:6.2f}s] sending '{value}'")
            arduino.write((value + "\n").encode())

        else:
            print(f"[{elapsed:6.2f}s] unknown action '{action}', skipping")

    print("All cues sent.")
    arduino.close()
    print("Done.")


if __name__ == "__main__":
    main()
