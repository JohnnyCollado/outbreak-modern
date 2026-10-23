"""Client for the ob_diag live mock controller (PS2X_PAD_PIPE; docs/ops/boot.md "Live pad").

    python tools/ob/pad.py [--pipe NAME] [--wait S] [--timeout S] <command ...>
    python tools/ob/pad.py [--pipe NAME] < commands.txt      (one command per line, '#' comments)

Connects to \\\\.\\pipe\\NAME (default: $OB_PAD_PIPE or ob_pad), sends the command(s), prints one reply line per
command. Exit 0 = every reply "ok ...", 1 = some "err ...", 2 = no pipe / no reply in time.
Commands: press <btn> [frames], hold <btn> <frames>, release <btn>|all, stick <left|right> <x> <y> <frames>,
wait <frames>, tick, shot [name], peek <addr|*ptr+off> <1|2|4>, quit.
"""

import argparse
import os
import sys
import threading
import time


def open_pipe(path, wait_s):
    deadline = time.monotonic() + wait_s
    while True:
        try:
            return open(path, "r+b", buffering=0)
        except OSError as e:  # not there yet (2) or busy with another client (231)
            if time.monotonic() >= deadline:
                print(f"pad: cannot open {path}: {e}", file=sys.stderr)
                return None
            time.sleep(0.2)


def read_line(f, buf, timeout_s):
    """Reads one '\\n'-terminated reply; None on timeout or a closed pipe."""
    result = {}

    def worker():
        while b"\n" not in buf:
            try:
                chunk = f.read(4096)
            except OSError:
                chunk = b""
            if not chunk:
                result["eof"] = True
                return
            buf.extend(chunk)

    t = threading.Thread(target=worker, daemon=True)
    t.start()
    t.join(timeout_s)
    if b"\n" not in buf:
        return None
    i = buf.index(b"\n")
    line = bytes(buf[:i]).decode("utf-8", "replace").rstrip("\r")
    del buf[: i + 1]
    return line


def main(argv):
    ap = argparse.ArgumentParser(description="ob_diag live pad client")
    ap.add_argument("--pipe", default=os.environ.get("OB_PAD_PIPE", "ob_pad"))
    ap.add_argument("--wait", type=float, default=10.0, help="seconds to wait for the pipe (default 10)")
    ap.add_argument("--timeout", type=float, default=600.0, help="seconds to wait for each reply (default 600)")
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args(argv)

    if args.command:
        commands = [" ".join(args.command)]
    else:
        commands = [ln.split("#", 1)[0].strip() for ln in sys.stdin]
        commands = [c for c in commands if c]
    if not commands:
        ap.print_usage(sys.stderr)
        return 2

    f = open_pipe(r"\\.\pipe" + "\\" + args.pipe, args.wait)
    if f is None:
        return 2
    rc = 0
    buf = bytearray()
    with f:
        for cmd in commands:
            f.write((cmd + "\n").encode("utf-8"))
            reply = read_line(f, buf, args.timeout)
            if reply is None:
                print(f"pad: no reply to '{cmd}' within {args.timeout:g} s", file=sys.stderr)
                return 2
            print(reply, flush=True)
            if not reply.startswith("ok"):
                rc = 1
            if cmd.split()[0].lower() == "quit":
                break
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
