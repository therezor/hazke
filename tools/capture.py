#!/usr/bin/env python3
"""Drive Hazke over the USB serial console in lockstep and save frames.

usage: capture.py CLIP TOKEN...
  TOKEN  keys*N    hold keys for N frames without recording; keys '_' = none
         keys*Nr   same, saving every frame to frames/CLIP/NNNNN.png
         !cmd      raw console command, ~ for spaces (e.g. !credits~5000)
         shot      save the current frame to frames/CLIP/
Keys are the characters the keyboard reports, plus T = Tab and N = Enter.
The game stays paused between runs. The last frame is also written to
last.png at 3x for a quick look. Send !cap~off to resume normal play.

example: capture.py autolock 'T*1' 'f*1r' '_*80r'
needs:   pip install pyserial pillow numpy;  HAZKE_PORT=/dev/cu.usbmodem1101
"""
import os, sys, time, termios
import numpy as np
import serial
from PIL import Image

PORT = os.environ.get("HAZKE_PORT", "/dev/cu.usbmodem1101")
HERE = os.getcwd()
W, H = 240, 135


def open_port():
    s = serial.Serial()
    s.port = PORT
    s.baudrate = 115200
    s.timeout = 60
    s.dtr = True
    s.rts = True
    s.open()
    attrs = termios.tcgetattr(s.fd)
    attrs[2] &= ~termios.HUPCL
    termios.tcsetattr(s.fd, termios.TCSANOW, attrs)
    time.sleep(0.05)
    s.reset_input_buffer()
    return s


def read_line(s):
    line = s.readline()
    if not line:
        raise TimeoutError("no reply")
    return line.decode("latin1").rstrip("\r\n")


def read_frame(s):
    n = W * H
    px = np.empty(n, dtype=">u2")
    i = 0
    while i < n:
        h = s.read(1)[0]
        if h >= 128:
            cnt = h - 126
            v = s.read(2)
            px[i:i + cnt] = (v[0] << 8) | v[1]
            i += cnt
        else:
            cnt = h + 1
            b = s.read(2 * cnt)
            if len(b) != 2 * cnt:
                raise TimeoutError("short frame")
            px[i:i + cnt] = np.frombuffer(b, dtype=">u2")
            i += cnt
    px = px.astype(np.uint32).reshape(H, W)
    r = (px >> 11) & 0x1F
    g = (px >> 5) & 0x3F
    b = px & 0x1F
    rgb = np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1)
    return Image.fromarray(rgb.astype(np.uint8), "RGB")


def until_ok(s, frames=None):
    """Read lines until ok/err, collecting any frames on the way."""
    while True:
        line = read_line(s)
        if line.startswith("FRM "):
            img = read_frame(s)
            if frames is not None:
                frames.append(img)
            continue
        if line.startswith("ok") or line.startswith("err"):
            return line


def cmd(s, text, frames=None):
    s.write((text + "\n").encode())
    return until_ok(s, frames)


def main():
    clip = sys.argv[1]
    outdir = os.path.join(HERE, "frames", clip)
    os.makedirs(outdir, exist_ok=True)
    idx = len([f for f in os.listdir(outdir) if f.endswith(".png")])
    s = open_port()
    print(cmd(s, "cap on"))
    last = None
    for tok in sys.argv[2:]:
        if tok.startswith("!"):
            print(tok, "->", cmd(s, tok[1:].replace("~", " ")))
            continue
        if tok == "shot":
            fr = []
            cmd(s, "shot", fr)
            if fr:
                last = fr[-1]
                last.save(os.path.join(outdir, f"shot_{int(time.time()*1000)}.png"))
            continue
        keys, _, n = tok.partition("*")
        rec = n.endswith("r")
        n = int(n.rstrip("r") or 1)
        cmd(s, "keys" + ("" if keys == "_" else " " + keys))
        fr = []
        line = cmd(s, ("rec " if rec else "step ") + str(n), fr if rec else None)
        if line.startswith("err"):
            print(tok, line)
        for img in fr:
            img.save(os.path.join(outdir, f"{idx:05d}.png"))
            idx += 1
        if fr:
            last = fr[-1]
    cmd(s, "keys")
    fr = []
    cmd(s, "shot", fr)
    if fr:
        last = fr[-1]
    if last is not None:
        last.resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(HERE, "last.png"))
    print(cmd(s, "status"), f"| {idx} frames in {clip}")
    s.close()


if __name__ == "__main__":
    main()
