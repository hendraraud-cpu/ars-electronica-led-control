r"""
dry_run.py - walks through every state in live_control.py WITHOUT touching
hardware: no serial ports opened, no audio played, nothing uploaded.

For each state it prints:
  - the key that triggers it
  - what command each Arduino (COM8/COM7/COM9) would receive, and whether
    that board's sketch actually has a handler for that command
  - which audio file would play (and whether the file exists), plus
    hard-cut vs crossfade, auto-advance and loop behaviour

Usage (PowerShell):
    py dry_run.py            # checks F:\03_ARS\live_control.py (V1)
    py dry_run.py V2         # checks F:\03_ARS\V2\live_control.py
"""
import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
LIVE = "--live" in args
SILENT = "--silent" in args
keys_arg = next((a.split("=", 1)[1] for a in args if a.startswith("--keys=")), None)
positional = [a for a in args if not a.startswith("--")]
version = positional[0] if positional else "V1"
base = ROOT if version.upper() == "V1" else os.path.join(ROOT, version)

if SILENT:
    os.environ["SDL_AUDIODRIVER"] = "dummy"  # no sound, timing still real

spec = importlib.util.spec_from_file_location("lc", os.path.join(base, "live_control.py"))
lc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lc)  # defines everything; main() is NOT run

if LIVE:
    # ---- LIVE DRY RUN: the real live_control.main() with FAKE Arduinos ----
    import time

    t0 = time.time()

    class FakeSerial:
        def __init__(self, port, *a, **k):
            self.port = port
            print(f"[fake] opened {port} (no real port touched)")

        def write(self, data):
            print(f"   [{time.time() - t0:7.1f}s] {self.port} <- {data.decode().strip()}")
            return len(data)

        def readline(self):
            return f"dry-run ack from {self.port}\n".encode()

        def reset_input_buffer(self):
            pass

        def close(self):
            pass

    lc.serial.Serial = FakeSerial  # SerialException is left untouched

    if keys_arg:  # scripted keys for unattended checks, e.g. --keys=3,4,q
        import msvcrt
        queue = keys_arg.split(",")
        state = {"last": time.time()}

        def fake_kbhit():
            return bool(queue) and time.time() - state["last"] > 2.5

        def fake_getch():
            state["last"] = time.time()
            return queue.pop(0).encode()

        msvcrt.kbhit, msvcrt.getch = fake_kbhit, fake_getch

    print("\n=== LIVE DRY RUN: real keys, real audio" + (" (muted)" if SILENT else "") +
          ", FAKE boards. Nothing goes to COM7/8/9. 'q' quits. ===\n")
    lc.main()
    sys.exit(0)


def handlers(sketch_dir):
    """Commands a sketch reacts to, scraped from its strcmp(cmdBuffer, "...") calls."""
    found = set()
    d = os.path.join(base, "ARDUINO", sketch_dir)
    if not os.path.isdir(d):
        return None
    for f in os.listdir(d):
        if f.endswith(".ino"):
            text = open(os.path.join(d, f), encoding="utf-8", errors="replace").read()
            found |= set(re.findall(r'strcmp\(\s*cmdBuffer\s*,\s*"([^"]+)"', text))
            found |= set(re.findall(r'strcmp\(\s*cmd\s*,\s*"([^"]+)"', text))
    return found


# Boards are built from whatever command tables the chosen version defines:
# V1 has COM8 (the state name itself) + COM7 + COM9; V2 has only COM8 (COM8_COMMAND).
if hasattr(lc, "COM8_COMMAND"):
    boards = {"COM8": ("led-pink", lambda s: lc.COM8_COMMAND.get(s, "state0"))}
else:
    boards = {"COM8": ("led-pink", lambda s: "state0" if s == "state4" else s)}
if hasattr(lc, "ARDUINO2_COMMAND"):
    boards["COM7"] = ("led-strip2", lambda s: lc.ARDUINO2_COMMAND.get(s, "state0"))
if hasattr(lc, "ARDUINO3_COMMAND"):
    boards["COM9"] = ("led-strip3", lambda s: lc.ARDUINO3_COMMAND.get(s, "state0"))
board_handlers = {name: handlers(sk) for name, (sk, _) in boards.items()}

# main board (COM8) also accepts the "state 1"-style commands via its own parser;
# anything it doesn't list is reported as "no explicit handler" rather than an error.
key_for = {v: k for k, v in lc.KEY_TO_STATE.items()}

print(f"\n=== DRY RUN of {version}: {os.path.join(base, 'live_control.py')} ===")
print("(no serial ports opened, no audio played, nothing uploaded)\n")

problems = 0
for state in sorted(lc.STATE_AUDIO):
    key = key_for.get(state, "-")
    print(f"--- {state}   [key: {key}] ---")

    for name, (sketch, mapper) in boards.items():
        cmd = mapper(state)
        hs = board_handlers[name]
        if hs is None:
            note = "sketch folder missing"
        elif cmd in hs:
            note = "handler found"
        elif name == "COM8":
            note = "IGNORED - prints 'That state isn't built yet', strip KEEPS whatever it was doing"
        else:
            note = "no handler - any unknown command BLANKS the strip"
        print(f"   {name} <- '{cmd}'   ({note})")

    audio = lc.STATE_AUDIO[state]
    path = os.path.join(lc.AUDIO_DIR, audio)
    exists = os.path.exists(path)
    if not exists:
        problems += 1
    mode = "hard cut" if state in lc.HARD_CUT_STATES else \
        f"crossfade {lc.STATE_CROSSFADE_SECONDS.get(state, lc.CROSSFADE_SECONDS)}s"
    print(f"   audio: {audio}  [{'OK' if exists else 'MISSING!'}]  ({mode})")

    if state in lc.STATE_ADVANCES_TO:
        print(f"   when audio ends -> auto-advances to {lc.STATE_ADVANCES_TO[state]}")
    if state in lc.STATE_LOOP_FROM_SECONDS:
        start = lc.STATE_LOOP_FROM_SECONDS[state]
        end = getattr(lc, "STATE_LOOP_WINDOW_END_SECONDS", {}).get(state)
        window = f"{start:.2f}s -> {end}s window" if end else f"from {start:.2f}s to end of track"
        how = "hard cut" if state in lc.STATE_LOOP_HARD_CUT else "crossfade"
        print(f"   loops forever: {window} ({how})")
    print()

print("Keys with no state:", [k for k in lc.KEY_TO_STATE if lc.KEY_TO_STATE[k] not in lc.STATE_AUDIO] or "none")
print(f"Missing audio files: {problems}")
print("Done. Nothing was sent to any board.")
