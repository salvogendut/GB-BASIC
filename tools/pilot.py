#!/usr/bin/env python3
"""pilot.py - drive 1984's --pilot PTY for scripted GEOBENCH GUI testing.

Runs the emulator, connects to the pilot PTY, executes a command list with
delays, captures every pilot reply, and (optionally) leaves the emulator to
take --screenshot-at shots. Commands are read from argv as a ';'-separated
list: "sleep 8; send help; send 100 0; sleep 1; send press 1".
"""
import os, re, subprocess, sys, time, select

def main():
    emu_args = []
    script = []
    seen_sep = False
    for a in sys.argv[1:]:
        if a == "--":
            seen_sep = True
            continue
        (script if seen_sep else emu_args).append(a)
    script = " ".join(script).split(";")

    proc = subprocess.Popen(emu_args, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=False)
    pty_path = None
    err_acc = b""
    t0 = time.time()
    while time.time() - t0 < 30 and pty_path is None:
        line = proc.stderr.readline()
        if not line:
            time.sleep(0.1); continue
        err_acc += line
        sys.stdout.write("[emu] " + line.decode(errors="replace"))
        m = re.search(rb"pilot PTY: (/dev/pts/\d+)", line)
        if m:
            pty_path = m.group(1).decode()
    if pty_path is None:
        print("no pilot PTY found"); proc.kill(); return 1

    fd = os.open(pty_path, os.O_RDWR | os.O_NONBLOCK)

    def drain(tag, dur=0.5):
        end = time.time() + dur
        buf = b""
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                try:
                    buf += os.read(fd, 4096)
                except OSError:
                    break
        if buf:
            for ln in buf.decode(errors="replace").splitlines():
                print("[%s] %s" % (tag, ln))
        return buf

    for cmd in script:
        cmd = cmd.strip()
        if not cmd:
            continue
        if cmd.startswith("sleep "):
            time.sleep(float(cmd[6:]))
            drain("rx")
        elif cmd.startswith("send "):
            payload = cmd[5:]
            os.write(fd, (payload + "\n").encode())
            print("[tx] " + payload)
            drain("rx", 0.8)
        elif cmd == "wait":            # wait for the emulator to exit by itself
            while proc.poll() is None:
                time.sleep(0.5)
                drain("rx", 0.1)
    # let --exit-after end it, or kill after script
    try:
        proc.wait(timeout=240)
    except subprocess.TimeoutExpired:
        proc.kill()
    # flush remaining stderr (emu messages)
    rest = proc.stderr.read()
    if rest:
        for ln in rest.decode(errors="replace").splitlines():
            print("[emu] " + ln)
    return 0

if __name__ == "__main__":
    sys.exit(main())
