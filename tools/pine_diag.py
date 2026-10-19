#!/usr/bin/env python3
"""Read-only PINE diagnostic client for ARMSX2 / PCSX2.

Phase 2 steps 7-9: prove that a workstation can reach ARMSX2's PINE server
(through `adb forward tcp:28011 tcp:28011`), identify the running game, and
time memory reads. It never writes guest memory.

Wire format (pcsx2/PINE.cpp, PINEServer::ClientLoop / ParseCommand):
  request: [u32 total_len incl. these 4 bytes][u8 opcode][args...]   (little-endian)
  reply:   [u32 total_len][u8 result 0x00=OK 0xFF=FAIL][payload...]
String replies carry [u32 len incl. NUL][bytes...NUL].

The server serves ONE client at a time. A second client's TCP connect succeeds
(listen backlog) but it gets no answer until the first disconnects, so close the
camera mod before running this, and vice versa.

Usage:
  python pine_diag.py                       # identify emulator + game
  python pine_diag.py --read 0x00100000     # also read a u32 there
  python pine_diag.py --bench 1000          # time 1000 Read32 round trips
"""
import argparse
import socket
import struct
import sys
import time

MSG_READ32 = 0x02
MSG_VERSION = 0x08
MSG_TITLE = 0x0B
MSG_ID = 0x0C
MSG_UUID = 0x0D  # disc CRC as 8 hex chars on ARMSX2/PCSX2
MSG_GAMEVERSION = 0x0E
MSG_STATUS = 0x0F

STATUS_NAMES = {0: "running", 1: "paused", 2: "shutdown"}
EE_RAM_END = 0x02000000  # 32 MiB EE main RAM


class PineError(Exception):
    pass


class Pine:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def close(self):
        self.sock.close()

    def _recv_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise PineError("server closed the connection")
            buf += chunk
        return bytes(buf)

    def call(self, opcode, args=b""):
        """One request/reply transaction. Returns the payload after the result code."""
        body = bytes([opcode]) + args
        self.sock.sendall(struct.pack("<I", len(body) + 4) + body)
        (total,) = struct.unpack("<I", self._recv_exact(4))
        if total < 5:
            raise PineError(f"malformed reply length {total}")
        rest = self._recv_exact(total - 4)
        if rest[0] != 0x00:
            raise PineError(f"opcode 0x{opcode:02X} failed (code 0x{rest[0]:02X})")
        return rest[1:]

    def string(self, opcode):
        payload = self.call(opcode)
        (n,) = struct.unpack_from("<I", payload)
        return payload[4:4 + n].split(b"\0", 1)[0].decode("utf-8", "replace")

    def status(self):
        (s,) = struct.unpack("<I", self.call(MSG_STATUS)[:4])
        return STATUS_NAMES.get(s, f"unknown({s})")

    def read32(self, addr):
        (v,) = struct.unpack("<I", self.call(MSG_READ32, struct.pack("<I", addr))[:4])
        return v


BENCH_ADDR = 0x00100000  # start of the usual ELF load area; always backed RAM


def bench(pine, n, addr=BENCH_ADDR):
    """Time n serial Read32 round trips. Returns latency percentiles in ms."""
    samples = []
    for _ in range(n):
        t0 = time.perf_counter()
        pine.read32(addr)
        samples.append(time.perf_counter() - t0)
    samples.sort()
    pct = lambda q: samples[min(n - 1, int(q * n))] * 1000
    return {"n": n, "addr": f"0x{addr:08X}", "p50_ms": pct(0.5), "p95_ms": pct(0.95),
            "p99_ms": pct(0.99), "max_ms": samples[-1] * 1000,
            "round_trips_per_s": n / sum(samples)}


def identify(pine):
    """Everything PINE can tell us about the emulator and the booted game. None = unavailable."""
    info = {"version": pine.string(MSG_VERSION), "status": pine.status()}
    for key, op in (("title", MSG_TITLE), ("id", MSG_ID), ("crc", MSG_UUID), ("game_version", MSG_GAMEVERSION)):
        try:
            info[key] = pine.string(op)
        except PineError:
            info[key] = None
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=28011, help="PINE slot (ARMSX2 default 28011)")
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--read", type=lambda s: int(s, 0), metavar="ADDR", help="EE address to read as u32")
    ap.add_argument("--bench", type=int, metavar="N", help="time N Read32 round trips")
    a = ap.parse_args(argv)

    try:
        pine = Pine(a.host, a.port, a.timeout)
    except OSError as e:
        print(f"connect {a.host}:{a.port} failed: {e}\n"
              "  - is PINE enabled in ARMSX2 (Settings > Fixes > PINE)?\n"
              "  - did you run: adb forward tcp:28011 tcp:28011 ?")
        return 1

    try:
        # Version and status answer without a booted game; the rest need a valid VM.
        info = identify(pine)
        print(f"emulator version : {info['version']}")
        print(f"status           : {info['status']}")
        for label, key in (("game title", "title"), ("game id", "id"), ("disc CRC", "crc"), ("game version", "game_version")):
            print(f"{label:<17}: {info[key] if info[key] is not None else '(unavailable - no game booted?)'}")

        if a.read is not None:
            if not 0 <= a.read < EE_RAM_END:
                print("refusing: address outside EE main RAM")
                return 2
            print(f"u32 @ 0x{a.read:08X} : 0x{pine.read32(a.read):08X}")

        if a.bench:
            r = bench(pine, a.bench, a.read if a.read is not None else BENCH_ADDR)
            print(f"Read32 x{r['n']}: p50 {r['p50_ms']:.3f} ms  p95 {r['p95_ms']:.3f} ms  "
                  f"p99 {r['p99_ms']:.3f} ms  max {r['max_ms']:.3f} ms  "
                  f"(~{r['round_trips_per_s']:.0f} serial round trips/s)")
    except (PineError, OSError) as e:
        print(f"PINE error: {e}")
        return 1
    finally:
        pine.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
