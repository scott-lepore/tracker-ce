#!/usr/bin/env python3
"""Host side of the Tracker CE developer monitor (USB serial).

Usage examples:
  trackerdev.py ping
  trackerdev.py stat
  trackerdev.py mon 500                 # periodic stats until Ctrl-C
  trackerdev.py tracks
  trackerdev.py midi 1 60 100           # note on, ch 1, C4, vel 100 (USB path)
  trackerdev.py midi 1 60 0             # note off
  trackerdev.py key play t              # tap Play (p=press, r=release, h=hold, t=tap)
  trackerdev.py pad 0 t                 # tap pad 1
  trackerdev.py jog 3                   # jog wheel +3
  trackerdev.py put local.wav /Samples/x.wav
  trackerdev.py flash build/PolyendTracker_CE_1.0.0.ptf
  trackerdev.py run "key config t; jog 2; stat"
  trackerdev.py shell

Needs pyserial. Port auto-detected by USB id 16d0:0e9f; override with --port or $TRACKER_PORT.
Use --linux (or --port socket://127.0.0.1:9170) to drive the Linux build instead;
`shot out.png` saves its emulated screen; `bridge` mirrors the physical Tracker's\ncontrols onto the Linux build.
"""
import argparse
import os
import sys
import time
import zlib

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial missing: pip install pyserial")

VID, PID = 0x16D0, 0x0E9F
BUTTONS = {
    "play": 23, "copy": 24, "insert": 25, "up": 26, "delete": 27, "rec": 28, "shift": 29,
    "left": 30, "down": 31, "right": 32, "params": 8, "perform": 9, "file": 10, "config": 11,
    "master": 12, "sload": 13, "splay": 14, "sedit": 15, "srec": 16, "song": 17, "note": 18,
    "instr": 19, "vol": 20, "fx2": 20, "fx": 21, "fx1": 21, "pattern": 22,
    "s1": 0, "s2": 1, "s3": 2, "s4": 3, "s5": 4, "s6": 5, "s7": 6, "s8": 7,  # screen buttons
}


LINUX_PORT_URL = "socket://127.0.0.1:9170"  # tracker-linux dev monitor (platforms/linux)


def find_port(explicit=None):
    if explicit:
        return explicit
    if os.environ.get("TRACKER_PORT"):
        return os.environ["TRACKER_PORT"]
    if os.path.exists("/dev/tracker"):
        return "/dev/tracker"
    for p in list_ports.comports():
        if p.vid == VID and p.pid == PID:
            return p.device
    return None


class Tracker:
    def __init__(self, port, timeout=3.0):
        # serial_for_url accepts both device paths and URLs such as socket://host:port
        self.ser = serial.serial_for_url(port, baudrate=115200, timeout=0.05, write_timeout=10)
        self.timeout = timeout
        time.sleep(0.1)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def readline(self, timeout=None):
        deadline = time.monotonic() + (timeout or self.timeout)
        buf = b""
        while time.monotonic() < deadline:
            b = self.ser.read(1)
            if not b:
                continue
            if b == b"\n":
                return buf.decode(errors="replace").rstrip("\r")
            buf += b
        return None

    def cmd(self, line, timeout=None, echo=True):
        """Send one command, return list of reply lines (last is 'ok' or 'err ...')."""
        self.ser.write((line.strip() + "\n").encode())
        lines = []
        while True:
            l = self.readline(timeout)
            if l is None:
                lines.append("err timeout (no reply)")
                break
            lines.append(l)
            if l == "ok" or l.startswith("err "):
                break
        if echo:
            for l in lines:
                if l != "ok":
                    print(l)
        return lines

    def ok(self, lines):
        return bool(lines) and lines[-1] == "ok"

    def get(self, remote, local):
        self.ser.write(f"get {remote}\n".encode())
        first = self.readline(timeout=10)
        if not first or not first.startswith("data "):
            print(first or "err timeout")
            return False
        size = int(first.split()[1])
        data = b""
        t0 = time.monotonic()
        while len(data) < size:
            chunk = self.ser.read(min(65536, size - len(data)))
            if chunk:
                data += chunk
                el = time.monotonic() - t0
                print(f"\r  {len(data)}/{size} bytes  {len(data)/el/1024 if el else 0:6.0f} KB/s", end="", flush=True)
            elif time.monotonic() - t0 > 120:
                print("\nerr timeout")
                return False
        print()
        # trailing ok/err line
        l = self.readline(timeout=5)
        if l != "ok":
            print(l)
            return False
        open(local, "wb").write(data)
        return True

    def put(self, local, remote):
        data = open(local, "rb").read()
        crc = zlib.crc32(data) & 0xFFFFFFFF
        self.ser.write(f"put {remote} {len(data)}\n".encode())
        first = self.readline()
        if first != "go":
            print(first or "err timeout")
            return False
        sent, t0, chunk = 0, time.monotonic(), 4096
        while sent < len(data):
            self.ser.write(data[sent:sent + chunk])
            sent += chunk
            done = min(sent, len(data))
            el = time.monotonic() - t0
            rate = done / el / 1024 if el > 0 else 0
            print(f"\r  {done}/{len(data)} bytes  {rate:6.0f} KB/s", end="", flush=True)
        print()
        lines = []
        while True:
            l = self.readline(timeout=30)
            if l is None:
                print("err timeout waiting for done")
                return False
            lines.append(l)
            if l == "ok" or l.startswith("err "):
                break
        for l in lines:
            if l != "ok":
                print(l)
        if not self.ok(lines):
            return False
        devcrc = next((l.split("crc=")[1] for l in lines if "crc=" in l), None)
        if devcrc is None or int(devcrc, 16) != crc:
            print(f"err crc mismatch: host {crc:08x} device {devcrc}")
            return False
        print(f"  crc ok {crc:08x}")
        return True


def do_key(t, kind, which, action):
    idx = BUTTONS.get(which, None) if kind == "key" else None
    if idx is None:
        try:
            idx = int(which)
        except ValueError:
            sys.exit(f"unknown {kind} '{which}'")
    if action == "t":
        t.cmd(f"{kind} {idx} p")
        time.sleep(0.08)
        t.cmd(f"{kind} {idx} r")
    else:
        t.cmd(f"{kind} {idx} {action}")


def run_script(t, text):
    for part in text.split(";"):
        part = part.strip()
        if not part:
            continue
        toks = part.split()
        if toks[0] in ("key", "pad") and len(toks) == 3:
            do_key(t, toks[0], toks[1], toks[2])
        elif toks[0] == "sleep":
            time.sleep(float(toks[1]))
        else:
            t.cmd(part)


def wait_for_reboot(port_hint, timeout=240):
    print("waiting for device to go away...")
    t0 = time.monotonic()
    while find_port() and time.monotonic() - t0 < 30:
        time.sleep(0.5)
    print("waiting for device to come back (bootloader is flashing)...")
    while time.monotonic() - t0 < timeout:
        p = find_port()
        if p:
            time.sleep(2)
            try:
                t = Tracker(p)
                lines = t.cmd("ping", echo=False)
                t.close()
                if t and lines and lines[0].startswith("pong"):
                    print(lines[0])
                    return True
            except serial.SerialException:
                pass
        time.sleep(1)
    print("device did not come back in time")
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device or pyserial URL (default: auto-detect USB)")
    ap.add_argument("--linux", action="store_true",
                    help="talk to a local tracker-linux instance (%s)" % LINUX_PORT_URL)
    ap.add_argument("--no-wait", action="store_true", help="flash: don't wait for the reboot")
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args()
    if not a.cmd:
        ap.print_help()
        return 1

    port = find_port(LINUX_PORT_URL if a.linux else a.port)
    if not port:
        sys.exit("Tracker serial port not found (is it plugged in and powered on?)")
    t = Tracker(port)
    c, args = a.cmd[0], a.cmd[1:]

    try:
        if c in ("key", "pad"):
            if len(args) != 2:
                sys.exit(f"usage: {c} <name|idx> <p|r|h|t>")
            do_key(t, c, args[0], args[1])
        elif c == "mon":
            ms = args[0] if args else "500"
            t.cmd(f"mon {ms}", echo=False)
            try:
                while True:
                    l = t.readline(timeout=5)
                    if l:
                        print(l)
            except KeyboardInterrupt:
                pass
            finally:
                t.cmd("mon 0", echo=False)
        elif c == "put":
            if len(args) != 2:
                sys.exit("usage: put <local> <remote path>")
            return 0 if t.put(args[0], args[1]) else 1
        elif c == "get":
            if len(args) not in (1, 2):
                sys.exit("usage: get <remote path> [local]")
            local = args[1] if len(args) == 2 else os.path.basename(args[0])
            return 0 if t.get(args[0], local) else 1
        elif c == "flash":
            if len(args) != 1:
                sys.exit("usage: flash <file.ptf>")
            name = os.path.basename(args[0])
            if len(name) > 31:
                sys.exit("ptf filename must be 31 characters or fewer (bootloader limit)")
            if not t.put(args[0], f"/Firmware/{name}"):
                return 1
            lines = t.cmd(f"flash {name}")
            if not t.ok(lines):
                return 1
            t.close()
            if not a.no_wait:
                wait_for_reboot(port)
            return 0
        elif c == "eeprom-pull":
            # Clone the device configuration into the Linux port's emulated EEPROM.
            out = args[0] if args else os.path.expanduser("~/.local/share/tracker-linux/eeprom.bin")
            CONFIG_OFFSET, TOTAL = 10, 4096
            image = bytearray(open(out, "rb").read()) if os.path.exists(out) else bytearray(b"\xff" * TOTAL)
            image = image[:TOTAL] + bytearray(b"\xff" * (TOTAL - len(image)))
            lines = t.cmd(f"eeprom 0 {TOTAL - CONFIG_OFFSET}", echo=False, timeout=30)
            if not t.ok(lines):
                print("\n".join(lines))
                return 1
            got = 0
            for l in lines:
                if l == "ok":
                    continue
                off, hexdata = l.split(" ", 1)
                data = bytes.fromhex(hexdata)
                image[CONFIG_OFFSET + int(off):CONFIG_OFFSET + int(off) + len(data)] = data
                got += len(data)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, "wb").write(image)
            print(f"{got} bytes of config written to {out} (restart tracker-linux to apply)")
        elif c == "bridge":
            # Mirror the physical Tracker's buttons, pads and jog onto a running tracker-linux.
            # 'bridge [mirror] [target]': default = controller mode (the Tracker stops reacting
            # locally); 'mirror' keeps it working normally while also forwarding.
            mirror = "mirror" in args
            rest = [x for x in args if x != "mirror"]
            target = rest[0] if rest else LINUX_PORT_URL
            dst = Tracker(target)
            mode_cmd = "events 1" if mirror else "controller 1"
            if not t.ok(t.cmd(mode_cmd, echo=False)):
                sys.exit(f"device refused '{mode_cmd}' (old firmware?)")
            print(f"bridging {port} -> {target} ({'mirror' if mirror else 'controller mode'}); Ctrl-C to stop")
            n = 0
            try:
                while True:
                    l = t.readline(timeout=1.0)
                    if not l or not l.startswith("ev "):
                        continue
                    _, kind, idx, val = l.split()
                    if kind == "b":
                        dst.cmd(f"key {idx} {'p' if val != '0' else 'r'}", echo=False, timeout=1.0)
                    elif kind == "p":
                        dst.cmd(f"pad {idx} {'p' if val != '0' else 'r'}", echo=False, timeout=1.0)
                    elif kind == "j":
                        dst.cmd(f"jog {val}", echo=False, timeout=1.0)
                    n += 1
            except KeyboardInterrupt:
                print(f"\n{n} events forwarded")
            finally:
                t.cmd("controller 0", echo=False)
                t.cmd("events 0", echo=False)
                dst.close()
        elif c == "shot":
            # Linux build only: dump the emulated panel (PPM, converted to PNG when PIL is available)
            out = args[0] if args else "tracker-screen.png"
            ppm = out[:-4] + ".ppm" if out.lower().endswith(".png") else out
            lines = t.cmd(f"shot {os.path.abspath(ppm)}", echo=False)
            if not t.ok(lines):
                print("\n".join(lines))
                return 1
            if out.lower().endswith(".png"):
                try:
                    from PIL import Image
                    Image.open(ppm).save(out)
                    os.remove(ppm)
                    print(out)
                except ImportError:
                    print(ppm, "(install pillow for PNG)")
            else:
                print(ppm)
        elif c == "run":
            run_script(t, " ".join(args))
        elif c == "shell":
            print("type commands, 'help' for list, Ctrl-D to exit")
            try:
                while True:
                    line = input("tracker> ").strip()
                    if line:
                        run_script(t, line)
            except (EOFError, KeyboardInterrupt):
                print()
        else:
            lines = t.cmd(" ".join(a.cmd))
            return 0 if t.ok(lines) else 1
    finally:
        try:
            t.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
