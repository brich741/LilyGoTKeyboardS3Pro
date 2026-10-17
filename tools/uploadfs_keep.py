#!/usr/bin/env python3
"""Flash the pad's filesystem (web UI + starter icons) WITHOUT losing the user's saved config and custom icons.

`pio run -t uploadfs` rewrites the whole LittleFS partition, which also holds /config.json and uploaded icons. This tool:
  1. reads the config and every non-starter icon off the running pad over its USB serial console (`dumpconfig`, `dumpicon`),
  2. stages them into firmware/data/ (git-ignored for config.json; removed again afterwards),
  3. runs `pio run -e esp32s3 -t uploadfs`,
  4. verifies the config on the pad is byte-for-byte identical and the icons are present,
  5. resets the pad (closing the serial port can drop it into ROM download mode).

Run it with PlatformIO's interpreter, which has pyserial and esptool's dependencies:
    %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe tools\\uploadfs_keep.py
Needs the pad plugged in and running the firmware (VID:PID 303A:4B50). Add --dry-run to only back up and stage nothing.
"""
import glob
import json
import os
import subprocess
import sys
import time

import serial
import serial.tools.list_ports as lp

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")
DATA = os.path.join(FW, "data")
ESPTOOL = os.path.join(os.path.expanduser("~"), ".platformio", "packages", "tool-esptoolpy", "esptool.py")
BACKUP = os.path.join(ROOT, ".padbackup")   # git-ignored scratch copy of the last backup


def find_port(vid, pid):
    for p in lp.comports():
        if p.vid == vid and p.pid == pid:
            return p.device
    return None


def wait_port(vid, pid, seconds):
    end = time.time() + seconds
    while time.time() < end:
        port = find_port(vid, pid)
        if port:
            return port
        time.sleep(0.5)
    return None


def reset_if_download_mode():
    """After a serial session the pad may sit in ROM download mode (enumerates as 303A:1001)."""
    port = find_port(0x303A, 0x1001)
    if port:
        print(f"pad is in download mode on {port}; resetting")
        subprocess.run([sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", port, "--after", "hard-reset", "chip-id"],
                       check=False, stdout=subprocess.DEVNULL)


def ensure_app_port():
    port = find_port(0x303A, 0x4B50)
    if not port:
        reset_if_download_mode()
        port = wait_port(0x303A, 0x4B50, 20)
    if not port:
        sys.exit("pad not found (expected USB 303A:4B50). Plug it in and let it boot.")
    return port


def command(ser, cmd, end_marker, seconds):
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    buf = ""
    end = time.time() + seconds
    while time.time() < end:
        buf += ser.read(4096).decode("utf-8", "replace")
        if end_marker in buf:
            time.sleep(0.3)
            buf += ser.read(4096).decode("utf-8", "replace")
            break
    return buf


def open_pad(port):
    ser = serial.Serial(port, 115200, timeout=0.3)
    ser.dtr = True
    time.sleep(0.7)
    return ser


def read_config(ser):
    raw = command(ser, "dumpconfig", "}\n", 10)
    for line in raw.splitlines():
        if line.strip().startswith("{"):
            return line.strip()
    sys.exit("could not read config from the pad:\n" + raw[:300])


def read_icon(ser, icon_id):
    raw = command(ser, "dumpicon " + icon_id, "ICON END", 30)
    lines = [l.strip() for l in raw.splitlines()]
    try:
        begin = next(i for i, l in enumerate(lines) if l.startswith("ICON ") and l.endswith(" BEGIN"))
        end = lines.index("ICON END")
    except (StopIteration, ValueError):
        sys.exit(f"could not read icon {icon_id}:\n" + raw[:300])
    data = bytes.fromhex("".join(lines[begin + 1:end]))
    if len(data) != 32768:
        sys.exit(f"icon {icon_id} came back {len(data)} bytes, expected 32768")
    return data


def main():
    dry = "--dry-run" in sys.argv
    port = ensure_app_port()
    print("pad on", port)
    ser = open_pad(port)
    try:
        cfg_text = read_config(ser)
        cfg = json.loads(cfg_text)
        starter = {os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(DATA, "icons", "*.565"))}
        custom = [i["id"] for i in cfg.get("icons", []) if i["id"] not in starter]
        icons = {cid: read_icon(ser, cid) for cid in custom}
    finally:
        ser.close()
    os.makedirs(BACKUP, exist_ok=True)
    with open(os.path.join(BACKUP, "config.json"), "w", encoding="utf-8") as f:
        f.write(cfg_text)
    for cid, data in icons.items():
        with open(os.path.join(BACKUP, cid + ".565"), "wb") as f:
            f.write(data)
    print(f"backed up config ({len(cfg_text)} bytes) and {len(icons)} custom icon(s) to {BACKUP}")
    if dry:
        reset_if_download_mode()
        return

    staged = [os.path.join(DATA, "config.json")] + [os.path.join(DATA, "icons", c + ".565") for c in icons]
    try:
        with open(staged[0], "w", encoding="utf-8", newline="") as f:
            f.write(cfg_text)
        for cid, data in icons.items():
            with open(os.path.join(DATA, "icons", cid + ".565"), "wb") as f:
                f.write(data)
        print("running uploadfs ...")
        r = subprocess.run([sys.executable, "-m", "platformio", "run", "-e", "esp32s3", "-t", "uploadfs"], cwd=FW)
        if r.returncode:
            sys.exit("uploadfs failed; the backup is in " + BACKUP)
    finally:
        for p in staged:
            if os.path.exists(p):
                os.remove(p)

    port = wait_port(0x303A, 0x4B50, 40)
    if not port:
        sys.exit("pad did not come back after the flash; the backup is in " + BACKUP)
    time.sleep(3)
    ser = open_pad(port)
    try:
        now = read_config(ser)
        status = command(ser, "status", "icon", 4)
    finally:
        ser.close()
    same = now == cfg_text
    missing = [c for c in icons if f"icon {c} file=yes" not in status]
    print("config identical after flash:", same)
    print("custom icons present:", not missing, missing or "")
    time.sleep(1)
    reset_if_download_mode()
    if not same or missing:
        sys.exit("verification failed; restore from " + BACKUP)
    print("done: web UI updated, settings and custom icons preserved")


if __name__ == "__main__":
    main()
