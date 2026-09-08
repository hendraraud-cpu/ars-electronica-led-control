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
    1 -> State 1 (crossfades music to 02_DOOR OPEN.MP3 over 3s). Plays
                  through its a/b/c sections once - section c's expand
                  holds and breathes (not fading out) for the rest of the
                  track. Once the track ends, the whole thing starts over
                  from 0:00 automatically, cutting instantly (no crossfade,
                  unlike the 3s fade used the first time it's triggered),
                  and a fresh "state1" is sent to reset the LEDs to section a.
    2 -> State 2 (audio + light cut abruptly to 03_STATION 1.MP3, no fade)
    3 -> State 3 (audio + light cut abruptly to 04 1 TRANSITION(2).MP3, no fade).
                  Drives BOTH Arduinos together - the main strip (COM8)
                  does its comet sequence, and the second strip (COM7)
                  runs its own breathe + expand sequence at the same time.
                  Once this track finishes, it automatically hands off to
                  State 4 (see below) - no key press needed.
    4 -> State 4 (audio cut abruptly to 05_STATION 2 (1).MP3). Only the 2nd
                  strip (COM7) reacts - it switches to the station2 60px
                  breathing section. The main strip (COM8) is untouched.
                  Triggers automatically when State 3's track ends, or
                  any time by pressing "4" directly.
                  Any state other than 3/4/5 blanks the 2nd strip (COM7),
                  since it's only ever active during States 3, 4, and 5.
    x -> State 5, mode X (audio cut abruptly to 05_x STATION2.MP3). The 2nd
                  strip (COM7) runs a scripted sequence over its whole
                  physical range: glitch flickers (each dimmer than the
                  last) until 700ms, an instant red flash that shrinks to
                  a point at pixel 144 by 00:00:01:21 (the last second of
                  that lead-in is red), then fast red comets repeating
                  from pixel 144 down to pixel 0 forever (first lands at
                  2000ms). The 3rd strip (COM9) also reacts - from that
                  same 2000ms mark, its own repeating comet (same speed)
                  races from pixel 0 up to its own midpoint (73), forever.
                  The main strip (COM8) is untouched.
    z -> State 5, mode Z (audio cut abruptly to 05_z STATION2.MP3). The 2nd
                  strip (COM7) runs a scripted sequence on a 60px section
                  (same position as expansion2's core): glitches that get
                  FASTER each time until 00:00:01:06, then swings left/
                  right by 30px until 00:00:03:05 (right side glitching
                  00:00:01:07-00:00:01:29), then a single eased comet to
                  pixel 0, done by 00:00:05:25. The 3rd strip (COM9) also
                  reacts - from 6 seconds in, it holds a solid 60px red
                  section (centered on its own pixel 100), breathing
                  slowly. The main strip (COM8) is untouched.
    6 -> State 6 (audio cut abruptly to
                  850133__cat-fox_alex__sub-pitch-down-sub-3.wav - a bass
                  drop). Audio only - no LED behavior defined yet.
    7 -> State 7 (audio cut abruptly to 07_STATION 3.MP3). Audio only for
                  now - no LED behavior defined yet.
    8 -> State 8 (audio cut abruptly to 08_REVEALING CHAMBER.mp3). Audio
                  only for now - no LED behavior defined yet.
    b -> DEBUG/TEST ONLY, not a real state. Jumps COM7 ahead so
                  expansion2 begins ~11.7 seconds after this is pressed
                  (instead of the ~11s wait after the normal breathe
                  point), and starts State 3's audio seeked to that same
                  point so they land together. Doesn't touch COM8.
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
ARDUINO2_PORT = "COM7"  # second Arduino/strip - only ever active during State 3
ARDUINO3_PORT = "COM9"  # third Arduino/strip - only ever active during State 5 mode Z
BAUD_RATE = 115200
AUDIO_DIR = r"F:\03_ARS\READY"
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
STATE_LOOP_HARD_CUT = {"state1", "state5_x"}

STATE_AUDIO = {
    "state0": "01_ON HOLD.MP3",
    "state1": "02_DOOR OPEN.MP3",
    "state2": "03_STATION 1.MP3",
    "state3": "04 1 TRANSITION(2).MP3",
    "state4": "05_STATION 2 (1).MP3",
    "state5_x": "05_x STATION2.MP3",  # State 5, mode X
    "state5_z": "05_z STATION2.MP3",  # State 5, mode Z
    "state6": "850133__cat-fox_alex__sub-pitch-down-sub-3.wav",
    "state7": "07_STATION 3.MP3",
    "state8": "08_REVEALING CHAMBER.mp3",
}

# States listed here auto-loop when idle: once the audio finishes, if
# nothing else has been triggered since, it crossfades right back into
# itself, starting from this many seconds in (not the very beginning).
# State 1 restarts from the true beginning (0.0s) once its track ends,
# resetting the LEDs back to section a in sync (see
# STATES_THAT_RESEND_LED_ON_LOOP below).
STATE_LOOP_FROM_SECONDS = {
    "state0": 5.0,
    "state1": 0.0,
    "state5_x": 3 + 14 / 30,  # 00:00:03:14 at 30fps
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
STATE_ADVANCES_TO = {
    "state3": "state4",
    "state7": "state8",
}

# What to send the 2nd Arduino (COM7) for each state - it only ever
# reacts to State 3 (its own breathe+expand sequence), State 4 (the
# station2 60px section), State 5 mode Z (a solid 60px section, same
# position/length as expansion2's core), and State 5 mode X (the
# glitch/red-shrink/fast-comet sequence); anything else blanks its strip.
ARDUINO2_COMMAND = {
    "state3": "state3",
    "state4": "station2",
    "state5_x": "state5x",
    "state5_z": "state5",
}

# What to send the 3rd Arduino (COM9) for each state - it reacts to
# State 5 mode Z (comet + expand + breathing hold), State 5 mode X
# (repeating comet, mirroring COM7 mode X's), State 6 (only does
# anything if it's currently mid-state5z-hold or mid-state5x), and
# State 7 (only does anything if it's currently mid-state6-hold) - see
# led-strip3.ino for the exact conditions; anything else blanks its strip.
ARDUINO3_COMMAND = {
    "state5_z": "state5z",
    "state5_x": "state5x",
    "state6": "state6",
    "state7": "state7",
}

# Cache folder for the trimmed "starts partway in" versions used by the
# loop above.
LOOP_CACHE_DIR = os.path.join(AUDIO_DIR, "_loop_cache")

KEY_TO_STATE = {
    "0": "state0",
    "1": "state1",
    "2": "state2",
    "3": "state3",
    "4": "state4",
    "6": "state6",
    "x": "state5_x",
    "z": "state5_z",
    "7": "state7",
    "8": "state8",
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
HARD_CUT_STATES = {"state2", "state3", "state4", "state5_x", "state5_z", "state6", "state7", "state8"}


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


def trigger(state, arduino, arduino2, arduino3):
    global current_state
    current_state = state
    print(f"\n>> {state}")

    # State 4 only involves the 2nd Arduino (COM7) - the main board
    # (COM8) doesn't recognize "state4" at all, so if State 3's comet
    # sequence is still running there when State 4 is triggered manually
    # (before State 3's own sequence has finished on its own), it would
    # otherwise just keep playing in the background. Send "state0"
    # instead to explicitly cancel it.
    main_command = "state0" if state == "state4" else state
    send_command_with_retry(arduino, main_command)

    # The second Arduino (COM7) only ever reacts to States 3, 4, and 5
    # (see ARDUINO2_COMMAND above) - anything else blanks its strip
    # instead of staying lit from a previous one.
    send_command_with_retry(arduino2, ARDUINO2_COMMAND.get(state, "state0"))

    # The third Arduino (COM9) only ever reacts to State 5 mode Z (see
    # ARDUINO3_COMMAND above) - anything else blanks its strip.
    send_command_with_retry(arduino3, ARDUINO3_COMMAND.get(state, "state0"))

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

    print(f"Connecting to 2nd Arduino on {ARDUINO2_PORT}...")
    arduino2 = None
    try:
        arduino2 = serial.Serial(ARDUINO2_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)  # let the board finish resetting after the port opens
        print("Connected.")
    except serial.SerialException as e:
        print(f"!!! Couldn't open {ARDUINO2_PORT}: {e}")

    print(f"Connecting to 3rd Arduino on {ARDUINO3_PORT}...")
    arduino3 = None
    try:
        arduino3 = serial.Serial(ARDUINO3_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)  # let the board finish resetting after the port opens
        print("Connected.")
    except serial.SerialException as e:
        print(f"!!! Couldn't open {ARDUINO3_PORT}: {e}")

    pygame.mixer.init()

    print("\nReady. Press 0, 1, 2, 3, 4, 6, 7, x, or z to trigger a state. Press q to quit.\n")

    try:
        while True:
            if msvcrt.kbhit():
                key = msvcrt.getch().decode(errors="ignore").lower()
                if key == "q":
                    break
                if key == "b":
                    # DEBUG/TEST ONLY - not a real state. Jumps COM7
                    # ahead so expansion2 begins 4s after this is pressed
                    # (STATE3_TEST_B_LEAD_MS in led-strip2.ino) - and
                    # starts State 3's audio seeked to that same point,
                    # so they land together. Doesn't touch COM8, and
                    # doesn't change current_state (so State 3's normal
                    # auto-advance to State 4 doesn't fire off this test).
                    seek_seconds = 12.0  # matches SHRINK2_START_MS - STATE3_TEST_B_LEAD_MS (23733 - 11733 = 12000ms)
                    print(f"\n>> [debug] jumping COM7 ahead (expansion2 in ~11.7s), audio seeked to {seek_seconds}s")
                    send_command_with_retry(arduino2, "b")
                    clip_path = get_loop_clip_path(STATE_AUDIO["state3"], seek_seconds)
                    hard_cut_to(clip_path)
                    continue
                state = KEY_TO_STATE.get(key)
                if state:
                    trigger(state, arduino, arduino2, arduino3)
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
                path = AUDIO_DIR + "\\" + STATE_AUDIO[next_state]
                print(f"\n>> {current_state} audio finished - advancing to {next_state}")

                # Same cancellation as in trigger(): State 4 doesn't touch
                # the main board (COM8), so explicitly cancel whatever
                # State 3 was doing there instead of leaving it running.
                main_command = "state0" if next_state == "state4" else next_state
                send_command_with_retry(arduino, main_command)
                send_command_with_retry(arduino2, ARDUINO2_COMMAND.get(next_state, "state0"))
                send_command_with_retry(arduino3, ARDUINO3_COMMAND.get(next_state, "state0"))

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
    if arduino2 is not None:
        arduino2.close()
    if arduino3 is not None:
        arduino3.close()
    print("\nClosed connection. Bye.")


if __name__ == "__main__":
    main()
