"""
live_control.py - press 1/2/3 on your keyboard to trigger States 1/2/3
live, with the music crossfading between them.

WHAT THIS DOES
--------------
Keeps ONE connection open to the Arduino for the whole session (no
repeated resets like running send_state.py fresh each time - opening
AND closing this board's serial port both reset it, so staying
connected is what keeps things stable). Reacts instantly to
keypresses, no need to press Enter:
    0 -> State 0 (all LEDs off, crossfades music to 01_ON HOLD.MP3). If
                  nothing else gets triggered, State 0 keeps looping on
                  its own: every time that track finishes, it crossfades
                  right back into itself, starting from the 00:05 mark
                  each time (not the very beginning) - so it becomes a
                  continuous idle ambience until another state interrupts.
    1 -> State 1 (crossfades music to 02_DOOR OPEN.MP3). Plays through its
                  a/b/c/d sections once, then holds dark and waits - it
                  does NOT loop, and does NOT auto-advance to any other
                  state. Nothing further happens until another key (or a
                  real trigger, later) is pressed.
    2 -> State 2 (audio + light cut abruptly to 03_STATION 1.MP3, no fade)
    3 -> State 3 (audio + light cut abruptly to 04 1 TRANSITION.MP3, no fade)
    q -> quit

Note: a crossfade blocks key detection while it runs - a key pressed
mid-crossfade won't register. Wait for the current one to finish
before pressing the next.

BEFORE RUNNING
--------------
Close the Arduino IDE's Serial Monitor first - only one program can
have the COM port open at a time.

HOW TO RUN
----------
    py live_control.py
"""

import os
import time
import subprocess
import msvcrt
import serial
import pygame
import imageio_ffmpeg

ARDUINO_PORT = "COM8"
BAUD_RATE = 115200
AUDIO_DIR = r"F:\03_ARS\READY"
CROSSFADE_SECONDS = 5.0  # default, used unless a state overrides it below

# Per-state crossfade duration overrides. Any state not listed here uses
# CROSSFADE_SECONDS. (States in HARD_CUT_STATES below ignore this entirely -
# they cut instantly instead of crossfading.)
STATE_CROSSFADE_SECONDS = {
    "state1": 3.0,
}

STATE_AUDIO = {
    "state0": "01_ON HOLD.MP3",
    "state1": "02_DOOR OPEN.MP3",
    "state2": "03_STATION 1.MP3",
    "state3": "04 1 TRANSITION.MP3",
}

# States listed here auto-loop when idle: once the audio finishes, if
# nothing else has been triggered since, it crossfades right back into
# itself, starting from this many seconds in (not the very beginning).
# State 1 is NOT here - it doesn't loop, it hands off to State 3 (see
# STATE_ADVANCES_TO below) once its audio finishes.
STATE_LOOP_FROM_SECONDS = {
    "state0": 5.0,
}

# States listed here ALSO re-send their LED command at that same moment
# they loop (see STATE_LOOP_FROM_SECONDS above).
STATES_THAT_RESEND_LED_ON_LOOP = set()

# States listed here hand off to a DIFFERENT state once their audio
# finishes (a one-time move forward, not a loop). No LED command gets
# sent for this - it's purely an audio hand-off, matched to whatever the
# board is doing on its own. Empty for now - State 1 no longer
# auto-advances to anything on the board, so the audio doesn't either;
# it just stops when the track ends.
STATE_ADVANCES_TO = {}

# Cache folder for the trimmed "starts partway in" versions used by the
# loop above.
LOOP_CACHE_DIR = os.path.join(AUDIO_DIR, "_loop_cache")

KEY_TO_STATE = {
    "0": "state0",
    "1": "state1",
    "2": "state2",
    "3": "state3",
}

current_channel = None  # the Channel actually playing right now - volume
                         # must be controlled on the Channel, not the Sound:
                         # Sound.set_volume() does NOT affect a channel that's
                         # already playing, only channels started after the
                         # change. That was the bug - both tracks stayed at
                         # full volume the whole "fade", then the old one
                         # just cut to silence via .stop().
current_state = None    # which state is currently active, for the idle-loop check


def crossfade_to(path, duration_seconds):
    global current_channel
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
        time.sleep(duration_seconds / steps)

    if old_channel is not None:
        old_channel.stop()
    current_channel = new_channel


def hard_cut_to(path):
    global current_channel
    if current_channel is not None:
        current_channel.stop()
    sound = pygame.mixer.Sound(path)
    current_channel = sound.play()
    current_channel.set_volume(1.0)


# States listed here cut abruptly (no fade) instead of crossfading.
HARD_CUT_STATES = {"state2", "state3"}


def get_loop_clip_path(audio_file, start_seconds):
    """Returns a cached copy of audio_file trimmed to start at start_seconds
    in, creating it with ffmpeg the first time it's needed."""
    os.makedirs(LOOP_CACHE_DIR, exist_ok=True)
    name, ext = os.path.splitext(audio_file)
    cached_path = os.path.join(LOOP_CACHE_DIR, f"{name}_from{int(start_seconds)}s{ext}")

    if not os.path.exists(cached_path):
        source_path = os.path.join(AUDIO_DIR, audio_file)
        ffmpeg_exe = imageio_ffmpeg.get_ffmpeg_exe()
        print(f"   (building loop clip from {start_seconds}s in - one-time step)")
        subprocess.run(
            [ffmpeg_exe, "-y", "-i", source_path, "-ss", str(start_seconds), "-c", "copy", cached_path],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    return cached_path


def send_command_with_retry(arduino, command, max_attempts=3, reply_timeout=0.6):
    """Sends command and waits for any reply line, retrying if none comes
    back in time. The board is normally reliable now, but a command sent
    at the exact wrong instant can still occasionally get dropped - this
    papers over that instead of leaving the state stuck out of sync."""
    if arduino is None:
        print("   (no board connected - skipping LED command, audio still plays)")
        return False
    for attempt in range(1, max_attempts + 1):
        arduino.reset_input_buffer()
        arduino.write((command + "\n").encode())
        end = time.time() + reply_timeout
        while time.time() < end:
            line = arduino.readline()
            if line:
                print(f"   board: {line.decode(errors='replace').strip()}")
                return True
        if attempt < max_attempts:
            print(f"   (no reply, retrying - attempt {attempt + 1}/{max_attempts})")
    print("   !!! board didn't respond after retries - check the connection !!!")
    return False


def trigger(state, arduino):
    global current_state
    current_state = state
    print(f"\n>> {state}")
    send_command_with_retry(arduino, state)

    audio_file = STATE_AUDIO.get(state)
    if audio_file:
        path = AUDIO_DIR + "\\" + audio_file
        if state in HARD_CUT_STATES:
            print(f"   cutting abruptly to {audio_file}")
            hard_cut_to(path)
        else:
            duration = STATE_CROSSFADE_SECONDS.get(state, CROSSFADE_SECONDS)
            print(f"   crossfading to {audio_file} over {duration}s")
            crossfade_to(path, duration)


def main():
    global current_state
    print(f"Connecting to Arduino on {ARDUINO_PORT}...")
    arduino = None
    try:
        arduino = serial.Serial(ARDUINO_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)  # let the board finish resetting after the port opens
        print("Connected.")
    except serial.SerialException as e:
        print(f"!!! Couldn't open {ARDUINO_PORT}: {e}")
        print("!!! Continuing WITHOUT the Arduino - audio-only mode. Lights won't respond.")

    pygame.mixer.init()

    print("\nReady. Press 0, 1, 2, or 3 to trigger a state. Press q to quit.\n")

    try:
        while True:
            if msvcrt.kbhit():
                key = msvcrt.getch().decode(errors="ignore").lower()
                if key == "q":
                    break
                state = KEY_TO_STATE.get(key)
                if state:
                    trigger(state, arduino)
                else:
                    print(f"(no state mapped to '{key}')")

            # Idle auto-loop: if the active state's track has finished and
            # nothing else has been triggered since, crossfade back into
            # itself from STATE_LOOP_FROM_SECONDS instead of just stopping.
            if (
                current_state in STATE_LOOP_FROM_SECONDS
                and current_channel is not None
                and not current_channel.get_busy()
            ):
                if current_state in STATES_THAT_RESEND_LED_ON_LOOP and arduino is not None:
                    print(f"\n>> {current_state} audio finished - restarting the light in sync")
                    send_command_with_retry(arduino, current_state)

                audio_file = STATE_AUDIO[current_state]
                start_seconds = STATE_LOOP_FROM_SECONDS[current_state]
                loop_path = get_loop_clip_path(audio_file, start_seconds)
                duration = STATE_CROSSFADE_SECONDS.get(current_state, CROSSFADE_SECONDS)
                print(f"   crossfading audio back in (from {start_seconds}s) over {duration}s")
                crossfade_to(loop_path, duration)

            # One-time hand-off: some states move forward into a different
            # state once their audio finishes, instead of looping. No LED
            # command is sent - the board already advances itself; this
            # just brings the audio along with it.
            elif (
                current_state in STATE_ADVANCES_TO
                and current_channel is not None
                and not current_channel.get_busy()
            ):
                next_state = STATE_ADVANCES_TO[current_state]
                path = AUDIO_DIR + "\\" + STATE_AUDIO[next_state]
                print(f"\n>> {current_state} audio finished - advancing to {next_state}")
                if next_state in HARD_CUT_STATES:
                    print(f"   cutting abruptly to {STATE_AUDIO[next_state]}")
                    hard_cut_to(path)
                else:
                    duration = STATE_CROSSFADE_SECONDS.get(next_state, CROSSFADE_SECONDS)
                    print(f"   crossfading to {STATE_AUDIO[next_state]} over {duration}s")
                    crossfade_to(path, duration)
                current_state = next_state

            time.sleep(0.05)
    except KeyboardInterrupt:
        pass

    if arduino is not None:
        arduino.close()
    print("\nClosed connection. Bye.")


if __name__ == "__main__":
    main()
