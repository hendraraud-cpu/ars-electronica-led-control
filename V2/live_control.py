"""
live_control.py (V2) - press 1/2/4/8/9 on your keyboard to trigger states
live, with the music crossfading between them.

WHAT THIS DOES
--------------
Keeps ONE connection open to the Arduino for the whole session (no
repeated resets like running send_state.py fresh each time - opening
AND closing this board's serial port both reset it, so staying
connected is what keeps things stable). Reacts instantly to
keypresses, no need to press Enter:
    1 -> Door Open  (crossfades to 01_DOOR OPEN.MP3 over 3s). COM8 State 1.
                  Loops from 0:00 when the track ends and restarts the LEDs.
    2 -> Station 1 (hard cut to 02_STATION 1.MP3). COM8 State 2.
    4 -> Station 2 (hard cut to 05_STATION 2.MP3). COM8 State 4: 2 mild
                  flickers then a 60px section breathing slowly.
    9 -> Station 3 (hard cut to 06_STATION 3.MP3). COM8: a red light breathing.
    8 -> Revealing Chamber (hard cut to 05_REVEALING CHAMBER.mp3). Plays
                  0:00-2:10 the first time, then loops the 0:20-2:10 window.
                  LEDs: none decided yet (strip is blanked).
    q -> quit

V2: ONE Arduino (COM8) / ONE strip does everything. No COM7, no COM9.
Audio comes ONLY from the NOVEMBER-ARS-AUDIO folder next to V2/.

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
# V2: ONE Arduino / ONE strip - everything runs on COM8 (no COM7, no COM9).
BAUD_RATE = 115200
# V2 plays ONLY the audios in the November folder (the backup set).
# Relative to this file so it works wherever the repo is cloned:
# <repo>/V2/live_control.py  ->  <repo>/NOVEMBER-ARS-AUDIO/
AUDIO_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "NOVEMBER-ARS-AUDIO"))
CROSSFADE_SECONDS = 5.0  # default, used unless a state overrides it below

# Per-state crossfade duration overrides. Any state not listed here uses
# CROSSFADE_SECONDS. (States in HARD_CUT_STATES below ignore this entirely -
# they cut instantly instead of crossfading.) This applies to the initial
# trigger only - see STATE_LOOP_CROSSFADE_SECONDS below for the crossfade
# used specifically when a state loops back into itself.
STATE_CROSSFADE_SECONDS = {
    "state1": 3.0,
}

# Per-state crossfade duration overrides used ONLY when a state in
# STATE_LOOP_FROM_SECONDS loops back into itself - falls back to
# STATE_CROSSFADE_SECONDS/CROSSFADE_SECONDS for any state not listed here.
# (Doesn't apply to states in STATE_LOOP_HARD_CUT below - those skip the
# crossfade entirely.)
STATE_LOOP_CROSSFADE_SECONDS = {}

# States listed here cut abruptly (no fade) specifically when they loop
# back into themselves - separate from HARD_CUT_STATES below, which is
# only about a state's INITIAL trigger. State 1 still crossfades in the
# first time it's triggered, but restarts instantly with no fade once it
# loops back to 0:00. State 5 mode X already hard-cuts on its initial
# trigger (see HARD_CUT_STATES) - it's listed here too so its loop-back
# (from 00:00:03:14) doesn't crossfade either.
STATE_LOOP_HARD_CUT = {"state1"}

STATE_AUDIO = {
    "state1": "01_DOOR OPEN.MP3",
    "state2": "02_STATION 1.MP3",
    "station3": "06_STATION 3.MP3",  # hotkey 9 - red light breathing
    "state4": "05_STATION 2.MP3",
    "state8": "05_REVEALING CHAMBER.mp3",
}

# States listed here auto-loop when idle: once the audio finishes, if
# nothing else has been triggered since, it crossfades right back into
# itself, starting from this many seconds in (not the very beginning).
# State 1 restarts from the true beginning (0.0s) once its track ends,
# resetting the LEDs back to section a in sync (see
# STATES_THAT_RESEND_LED_ON_LOOP below).
STATE_LOOP_FROM_SECONDS = {
    "state1": 0.0,
    "state8": 20.0,  # 00:00:20
}

# States listed here loop a fixed WINDOW of their own audio forever,
# instead of playing from STATE_LOOP_FROM_SECONDS through to the
# track's true end like the states above - the file's content after
# this timestamp never plays. State 8 plays from 0:00 the first time,
# but only up to 00:02:10 - from then on it repeats the 00:00:20 -
# 00:02:10 window forever (crossfading back to 00:00:20 each time it
# hits 00:02:10, same as any other STATE_LOOP_FROM_SECONDS loop-back -
# it's just not in STATE_LOOP_HARD_CUT, so that loop-back fades in).
STATE_LOOP_WINDOW_END_SECONDS = {
    "state8": 130.0,  # 00:02:10
}


def get_state_audio_path(state, first_play):
    """Path to play for `state`'s audio - the raw file normally, or (for
    states in STATE_LOOP_WINDOW_END_SECONDS) a clip trimmed to end at
    that state's loop window instead of the file's true end, so it
    naturally finishes there and the idle-loop-back below (driven by
    STATE_LOOP_FROM_SECONDS) picks up the repeat. The very first play
    still starts from 0:00; every loop after that starts from
    STATE_LOOP_FROM_SECONDS[state]."""
    audio_file = STATE_AUDIO[state]
    if state in STATE_LOOP_WINDOW_END_SECONDS:
        start_seconds = 0.0 if first_play else STATE_LOOP_FROM_SECONDS[state]
        end_seconds = STATE_LOOP_WINDOW_END_SECONDS[state]
        return get_loop_clip_path(audio_file, start_seconds, end_seconds)
    return AUDIO_DIR + "\\" + audio_file

# States listed here ALSO re-send their LED command at that same moment
# they loop (see STATE_LOOP_FROM_SECONDS above).
STATES_THAT_RESEND_LED_ON_LOOP = {"state1"}

# States listed here hand off to a DIFFERENT state once their audio
# finishes (a one-time move forward, not a loop). State 3's track hands
# off to State 4 once it ends - State 4 can also still be triggered
# manually by pressing "4" at any other time. State 7's track hands off
# to State 8 the same way.
STATE_ADVANCES_TO = {}  # V2: no auto-advance - everything is manual

# What to send COM8 (the one and only board) for each state. Revealing Chamber
# (state8) has no light show decided yet - it just blanks the strip for now.
COM8_COMMAND = {
    "state1": "state1",
    "state2": "state2",
    "station3": "station3",
    "state4": "state4",
    "state8": "state0",
}

# Cache folder for the trimmed "starts partway in" versions used by the
# loop above.
# V2: keep the trimmed-clip cache next to this script, NOT inside the backup audio folder.
LOOP_CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_loop_cache")

KEY_TO_STATE = {
    "1": "state1",
    "2": "state2",
    "4": "state4",
    "8": "state8",
    "9": "station3",  # Station 3 - red light breathing, new 06_STATION 3 audio
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
HARD_CUT_STATES = {"state2", "station3", "state4", "state8"}


def get_loop_clip_path(audio_file, start_seconds, end_seconds=None):
    """Returns a cached copy of audio_file trimmed to start at start_seconds
    in (and, if given, cut off at end_seconds instead of playing through to
    the file's true end), creating it with ffmpeg the first time it's
    needed."""
    os.makedirs(LOOP_CACHE_DIR, exist_ok=True)
    name, ext = os.path.splitext(audio_file)
    suffix = f"_from{int(start_seconds)}s"
    if end_seconds is not None:
        suffix += f"_to{int(end_seconds)}s"
    cached_path = os.path.join(LOOP_CACHE_DIR, f"{name}{suffix}{ext}")

    if not os.path.exists(cached_path):
        source_path = os.path.join(AUDIO_DIR, audio_file)
        ffmpeg_exe = imageio_ffmpeg.get_ffmpeg_exe()
        end_note = f" to {end_seconds}s" if end_seconds is not None else ""
        print(f"   (building loop clip from {start_seconds}s{end_note} - one-time step)")
        cmd = [ffmpeg_exe, "-y", "-i", source_path, "-ss", str(start_seconds)]
        if end_seconds is not None:
            cmd += ["-to", str(end_seconds)]
        cmd += ["-c", "copy", cached_path]
        subprocess.run(
            cmd,
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
        print("   (this board isn't connected - skipping)")
        return False
    for attempt in range(1, max_attempts + 1):
        try:
            arduino.reset_input_buffer()
            arduino.write((command + "\n").encode())
            end = time.time() + reply_timeout
            while time.time() < end:
                line = arduino.readline()
                if line:
                    print(f"   board: {line.decode(errors='replace').strip()}")
                    return True
        except serial.SerialException as e:
            # The connection can drop or glitch mid-session (a loose USB
            # cable, the board resetting) even though it opened fine at
            # startup - treat that the same as a dropped command instead
            # of crashing the whole script.
            print(f"   !!! serial error talking to the board: {e}")
        if attempt < max_attempts:
            print(f"   (no reply, retrying - attempt {attempt + 1}/{max_attempts})")
    print("   !!! board didn't respond after retries - check the connection !!!")
    return False


def trigger(state, arduino):
    global current_state
    current_state = state
    print(f"\n>> {state}")

    send_command_with_retry(arduino, COM8_COMMAND.get(state, "state0"))

    audio_file = STATE_AUDIO.get(state)
    if audio_file:
        path = get_state_audio_path(state, first_play=True)
        if state in HARD_CUT_STATES:
            print(f"   cutting abruptly to {audio_file}")
            hard_cut_to(path)
        else:
            duration = STATE_CROSSFADE_SECONDS.get(state, CROSSFADE_SECONDS)
            print(f"   crossfading to {audio_file} over {duration}s")
            crossfade_to(path, duration)


def main():
    global current_state
    # Each board connects independently - either one can be missing right
    # now (e.g. only COM7 plugged in) and the script just skips sending
    # it commands, no special "mode" around it.
    print(f"Connecting to Arduino on {ARDUINO_PORT}...")
    arduino = None
    try:
        arduino = serial.Serial(ARDUINO_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)  # let the board finish resetting after the port opens
        print("Connected.")
    except serial.SerialException as e:
        print(f"!!! Couldn't open {ARDUINO_PORT}: {e}")

    pygame.mixer.init()

    print("\nReady. Press 1, 2, 4, 8, or 9 to trigger a state. Press q to quit.\n")

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
                end_seconds = STATE_LOOP_WINDOW_END_SECONDS.get(current_state)
                loop_path = get_loop_clip_path(audio_file, start_seconds, end_seconds)
                if current_state in STATE_LOOP_HARD_CUT:
                    print(f"   cutting abruptly back to {audio_file} (from {start_seconds}s)")
                    hard_cut_to(loop_path)
                else:
                    duration = STATE_LOOP_CROSSFADE_SECONDS.get(
                        current_state, STATE_CROSSFADE_SECONDS.get(current_state, CROSSFADE_SECONDS)
                    )
                    print(f"   crossfading audio back in (from {start_seconds}s) over {duration}s")
                    crossfade_to(loop_path, duration)

            # One-time hand-off: some states move forward into a different
            # state once their audio finishes, instead of looping. State
            # 3 hands off to State 4 this way (see STATE_ADVANCES_TO).
            elif (
                current_state in STATE_ADVANCES_TO
                and current_channel is not None
                and not current_channel.get_busy()
            ):
                next_state = STATE_ADVANCES_TO[current_state]
                path = get_state_audio_path(next_state, first_play=True)
                print(f"\n>> {current_state} audio finished - advancing to {next_state}")

                # Same cancellation as in trigger(): State 4 doesn't touch
                # the main board (COM8), so explicitly cancel whatever
                # State 3 was doing there instead of leaving it running.
                send_command_with_retry(arduino, COM8_COMMAND.get(next_state, "state0"))

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
