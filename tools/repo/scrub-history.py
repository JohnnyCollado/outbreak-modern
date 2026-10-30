#!/usr/bin/env python3
"""A `git fast-export` stream with personal values replaced and paths dropped (docs/plans/2026-10-30-security-queue-2.md,
P1). Reads the stream on stdin, writes it to stdout; run by scrub-history.sh on a mirror, never on the working repo.
Values come from the environment (OLD_EMAIL, NEW_EMAIL, OLD_ADDRESS, NEW_ADDRESS), so none is stored in the repo."""
import os
import sys

DROP = (b"game_files/", b".claude-state/", b"tools/__pycache__/")
PAIRS = [(os.environ["OLD_EMAIL"].encode(), os.environ["NEW_EMAIL"].encode())]
if os.environ.get("OLD_ADDRESS"):
    PAIRS.append((os.environ["OLD_ADDRESS"].encode(), os.environ.get("NEW_ADDRESS", "<odin-ip>").encode()))


def scrub(data):
    for old, new in PAIRS:
        data = data.replace(old, new)
    return data


def path_of(line):
    """The path of an `M mode ref path` or `D path` line, unquoted enough for prefix checks."""
    parts = line.rstrip(b"\n").split(b" ", 3 if line.startswith(b"M ") else 1)
    path = parts[-1]
    return path[1:] if path.startswith(b'"') else path


def dropped(path):
    return path.startswith(DROP) or path.lower().endswith(b".exe") or path.lower().endswith(b'.exe"')


def main():
    src, out = sys.stdin.buffer, sys.stdout.buffer
    while True:
        line = src.readline()
        if not line:
            break
        if line.startswith(b"data "):
            data = src.read(int(line[5:]))
            data = scrub(data)
            out.write(b"data %d\n" % len(data))
            out.write(data)
            continue
        if line.startswith((b"M ", b"D ")) and dropped(path_of(line)):
            continue
        if line.startswith((b"author ", b"committer ", b"tagger ")):
            line = scrub(line)
        out.write(line)


if __name__ == "__main__":
    main()
