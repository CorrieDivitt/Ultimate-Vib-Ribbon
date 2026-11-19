#!/usr/bin/env python3
"""Record the obstacle events the original game generates, from DuckStation.

Connects to DuckStation's GDB server, stops the emulated PlayStation each time
the game adds a script event (a road part, an effect or a camera change) and
writes one JSON line per event. These recordings are the reference our own
game logic is tested against. They describe your own disc; keep them out of git.

Setup (once):
  1. In DuckStation's settings, enable the GDB server (an advanced/debug
     setting; default port 1234) and restart the game.
  2. Extract your disc once with vibtool so MAIN_G.EXE is on your PC.

Use:
  python capture_events.py --exe F:\\VibRibbonVR\\research\\extracted\\disc\\MAIN_G.EXE --out events.jsonl
  Then play a song in DuckStation. Press Ctrl+C here when the song is over.
  Add --keep-alive to make game over impossible while recording.

  python capture_events.py --selftest     checks this script against a fake server
"""

import argparse
import json
import os
import socket
import struct
import sys
import threading
import time

# Addresses in MAIN_G.EXE of SCES-02873, from the revib project's symbol list.
ADD_EVENT = 0x800466D0        # Gameplay::AddEvent(ScriptEvent*)
GAMEPLAY_START = 0x8004549C   # Gameplay::Start(course, seconds, score, ...)
SCRIPT_START_TIME = 0x80076468
SCRIPT_OFFSET = 0x8007646C
SCRIPT_SEED = 0x800763FC
VIBRI_LEVEL = 0x800764CC       # Gameplay's level: 3 is the starting rabbit, 11 is one step from game over
EXE_LOAD_ADDRESS = 0x80019000
EXE_HEADER_SIZE = 0x800
GUARD_BYTES = 16              # code bytes compared before a stop is trusted

EVENT_TYPES = {0: "part", 1: "camera", 2: "effect"}
EFFECTS = {0: "screw", 1: "reverse", 2: "tempo"}
ROADS = ["block", "pit", "loop", "wave", "block+pit", "block+loop", "block+wave", "pit+loop", "pit+wave", "loop+wave"]


class Gdb:
    """Just enough of the GDB remote protocol: breakpoints, continue, registers, memory."""

    def __init__(self, host, port, verbose=False):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(None)
        self.buf = b""
        self.verbose = verbose
        self.no_ack = False

    def _send(self, payload):
        data = payload.encode("ascii")
        packet = b"$" + data + b"#" + ("%02x" % (sum(data) & 0xFF)).encode()
        if self.verbose:
            print("  ->", payload[:80])
        self.sock.sendall(packet)

    def _read_packet(self):
        while True:
            start = self.buf.find(b"$")
            if start >= 0:
                end = self.buf.find(b"#", start)
                if end >= 0 and len(self.buf) >= end + 3:
                    payload = self.buf[start + 1:end]
                    self.buf = self.buf[end + 3:]
                    if not self.no_ack:
                        self.sock.sendall(b"+")
                    text = payload.decode("ascii", "replace")
                    if self.verbose:
                        print("  <-", text[:80])
                    return text
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("DuckStation closed the connection")
            self.buf += chunk

    def command(self, payload):
        self._send(payload)
        return self._read_packet()

    def set_breakpoint(self, address):
        for kind in ("Z1", "Z0"):  # hardware first: it survives the game loading new code
            reply = self.command("%s,%x,4" % (kind, address))
            if reply == "OK":
                return kind
        raise RuntimeError("the emulator refused a breakpoint at %08X (reply %r)" % (address, reply))

    def resume(self):
        self._send("c")

    def wait_stop(self):
        return self._read_packet()

    def interrupt(self):
        self.sock.sendall(b"\x03")

    def registers(self):
        reply = self.command("g")
        if reply.startswith("E") or len(reply) < 38 * 8:
            raise RuntimeError("unexpected register reply: %r" % reply[:60])
        raw = bytes.fromhex(reply[:38 * 8])
        values = struct.unpack("<38I", raw)
        # MIPS order: r0..r31, status, lo, hi, badvaddr, cause, pc
        return {"a0": values[4], "a1": values[5], "a2": values[6], "ra": values[31], "pc": values[37]}

    def read(self, address, length):
        reply = self.command("m%x,%x" % (address, length))
        if reply.startswith("E") or len(reply) != length * 2:
            raise RuntimeError("could not read %d bytes at %08X: %r" % (length, address, reply[:40]))
        return bytes.fromhex(reply)

    def write(self, address, data):
        reply = self.command("M%x,%x:%s" % (address, len(data), data.hex()))
        if reply != "OK":
            raise RuntimeError("could not write %d bytes at %08X: %r" % (len(data), address, reply[:40]))

    def read_u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]


def exe_bytes(exe, address, length):
    offset = EXE_HEADER_SIZE + (address - EXE_LOAD_ADDRESS)
    return exe[offset:offset + length]


def describe(event):
    kind = event["type"]
    if kind == "part":
        road = event["value"]
        return "part   t=%9d  %-10s length=%6d scale=%.3f" % (
            event["time"], ROADS[road] if 0 <= road < 10 else road, event["length"], event["scale"] / 65536.0)
    if kind == "camera":
        return "camera t=%9d  id=%d loops=%d" % (event["time"], event["value"] % 10, event["value"] // 10)
    return "effect t=%9d  %-7s length=%d" % (event["time"], EFFECTS.get(event["value"], event["value"]), event["length"])


def capture(args):
    exe = open(args.exe, "rb").read()
    if exe[:8] != b"PS-X EXE":
        sys.exit("%s is not a PlayStation executable" % args.exe)
    guards = {a: exe_bytes(exe, a, GUARD_BYTES) for a in (ADD_EVENT, GAMEPLAY_START)}

    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)  # before touching the emulator, so a bad path cannot leave it paused

    gdb = Gdb(args.host, args.port, args.verbose)
    print("Connected to DuckStation at %s:%d" % (args.host, args.port))
    reply = gdb.command("?")
    print("Emulator state: %s" % reply)
    kinds = [gdb.set_breakpoint(a) for a in guards]
    print("Breakpoints set (%s). Play a song now. Ctrl+C to finish." % ", ".join(kinds))

    out = open(args.out, "a", encoding="utf-8")
    count = skipped = 0
    stopping = False
    try:
        gdb.resume()
        while True:
            stop = gdb.wait_stop()
            if stopping:
                break
            if stop.startswith("W") or stop.startswith("X"):
                print("The emulated machine ended: %s" % stop)
                break
            regs = gdb.registers()
            pc = regs["pc"]
            if pc not in guards or gdb.read(pc, GUARD_BYTES) != guards[pc]:
                skipped += 1  # another program of the disc is loaded at this address, or a stop we did not ask for
                gdb.resume()
                continue
            record = {"wall": round(time.time(), 3)}
            if pc == GAMEPLAY_START:
                record.update(type="round_start", course=regs["a0"], seconds=regs["a1"], score=regs["a2"])
                print("--- round start: course %d, %d s" % (regs["a0"], regs["a1"]))
            else:
                t, when, value, length, scale = struct.unpack("<5i", gdb.read(regs["a0"], 20))
                record.update(type=EVENT_TYPES.get(t, t), time=when, value=value, length=length, scale=scale,
                              script_start=struct.unpack("<i", gdb.read(SCRIPT_START_TIME, 4))[0],
                              script_offset=struct.unpack("<i", gdb.read(SCRIPT_OFFSET, 4))[0],
                              script_seed=gdb.read_u32(SCRIPT_SEED))
                count += 1
                if args.keep_alive:  # obstacles do not depend on how well the round is played
                    gdb.write(VIBRI_LEVEL, struct.pack("<i", 3))
                print("%5d  %s" % (count, describe(record)))
            out.write(json.dumps(record) + "\n")
            out.flush()
            gdb.resume()
    except KeyboardInterrupt:
        stopping = True
        print("\nStopping.")
        try:
            gdb.interrupt()
        except OSError:
            pass
    finally:
        out.close()
    print("%d events written to %s (%d unrelated stops skipped)" % (count, args.out, skipped))


# ---------------------------------------------------------------- self-test

def _fake_server(listener, exe):
    """A tiny stand-in for the emulator: two events, then a stop at a foreign address."""
    conn, _ = listener.accept()
    buf = b""
    event = struct.pack("<5i", 0, 123456, 7, 11025, 65536)
    memory = {0x80100000: event, SCRIPT_START_TIME: struct.pack("<i", 1000), SCRIPT_OFFSET: struct.pack("<i", 0),
              SCRIPT_SEED: struct.pack("<I", 42)}
    stops = [ADD_EVENT, 0x80030000, GAMEPLAY_START, ADD_EVENT]
    pc = [0]

    def send(text):
        data = text.encode()
        conn.sendall(b"$" + data + b"#" + ("%02x" % (sum(data) & 0xFF)).encode())

    def read_mem(addr, length):
        for base, blob in memory.items():
            if base <= addr and addr + length <= base + len(blob):
                return blob[addr - base:addr - base + length]
        off = EXE_HEADER_SIZE + addr - EXE_LOAD_ADDRESS
        if addr == 0x80030000:
            return b"\xEE" * length
        return exe[off:off + length]

    while True:
        chunk = conn.recv(4096)
        if not chunk:
            return
        buf += chunk
        while b"#" in buf and len(buf) >= buf.find(b"#") + 3:
            start, end = buf.find(b"$"), buf.find(b"#")
            cmd = buf[start + 1:end].decode()
            buf = buf[end + 3:]
            conn.sendall(b"+")
            if cmd == "?":
                send("S05")
            elif cmd.startswith("Z1"):
                send("")  # pretend hardware breakpoints are unsupported
            elif cmd.startswith("Z0"):
                send("OK")
            elif cmd == "c":
                if stops:
                    pc[0] = stops.pop(0)
                    send("S05")
                else:
                    send("W00")
            elif cmd == "g":
                regs = [0] * 38
                regs[4], regs[5], regs[37] = 0x80100000, 150, pc[0]
                if pc[0] == GAMEPLAY_START:
                    regs[4] = 2
                send(struct.pack("<38I", *regs).hex())
            elif cmd.startswith("M"):
                head, data = cmd[1:].split(":")
                addr = int(head.split(",")[0], 16)
                memory[addr] = bytes.fromhex(data)
                send("OK")
            elif cmd.startswith("m"):
                addr, length = (int(x, 16) for x in cmd[1:].split(","))
                send(read_mem(addr, length).hex())
            else:
                send("")


def selftest():
    import os
    import tempfile
    exe = bytearray(b"PS-X EXE" + bytes(EXE_HEADER_SIZE - 8) + bytes(0x40000))
    for a, fill in ((ADD_EVENT, 0xA1), (GAMEPLAY_START, 0xB2)):
        off = EXE_HEADER_SIZE + a - EXE_LOAD_ADDRESS
        exe[off:off + GUARD_BYTES] = bytes([fill]) * GUARD_BYTES
    folder = tempfile.mkdtemp()
    exe_path, out_path = os.path.join(folder, "MAIN_G.EXE"), os.path.join(folder, "events.jsonl")
    open(exe_path, "wb").write(exe)
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    threading.Thread(target=_fake_server, args=(listener, bytes(exe)), daemon=True).start()
    capture(argparse.Namespace(exe=exe_path, out=out_path, host="127.0.0.1", port=listener.getsockname()[1], verbose=False, keep_alive=True))
    rows = [json.loads(line) for line in open(out_path)]
    kinds = [r["type"] for r in rows]
    assert kinds == ["part", "round_start", "part"], kinds
    assert rows[0]["time"] == 123456 and rows[0]["value"] == 7 and rows[0]["length"] == 11025 and rows[0]["script_seed"] == 42
    assert rows[1]["course"] == 2 and rows[1]["seconds"] == 150
    print("self-test passed")


def main():
    p = argparse.ArgumentParser(description="Record Vib-Ribbon's script events from DuckStation's GDB server.")
    p.add_argument("--exe", help="MAIN_G.EXE extracted from your own disc (used to recognise the game's code)")
    p.add_argument("--out", default="events.jsonl", help="file to append events to")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p.add_argument("--keep-alive", action="store_true",
                   help="reset Vibri to her starting form at every event so a song cannot end in game over (changes the game's memory)")
    p.add_argument("--verbose", action="store_true", help="print every protocol packet")
    p.add_argument("--selftest", action="store_true")
    args = p.parse_args()
    if args.selftest:
        return selftest()
    if not args.exe:
        p.error("--exe is required")
    try:
        capture(args)
    except ConnectionRefusedError:
        sys.exit("Could not connect to %s:%d. Is DuckStation running with its GDB server enabled?" % (args.host, args.port))


if __name__ == "__main__":
    main()
