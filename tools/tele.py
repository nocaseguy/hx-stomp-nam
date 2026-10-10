#!/usr/bin/env python3
"""Run a RAM image on the unit unattended and read back the number it reports.

usage: tele.py source.c [-DNAME=VALUE ...] [--extra file.c ...] [--raw]

Builds source.c (with src/startup.c, src/dfu.c and any --extra files) as a DFU
image, loads it with dfu-util, and times how long the unit stays off USB. An
image that ends in tele(n) (src/tele.h) stays away n seconds longer than one
that ends in tele(0), so the number is (time - BASE) / UNIT rounded; --raw
prints the seconds instead, which is what to use for an image that does
something lengthy first (compare with a run of the same image reporting 0). The unit must be in DFU mode to start with, and is again
afterwards.
"""
import os
import subprocess
import sys
import time

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 1.15                     # seconds off USB for an image that only does tele(0): measured
UNIT = 0.535                    # seconds each count adds: measured
CFLAGS = ("-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os -ffreestanding -Wall -Wextra -Isrc "
          "-T src/lpc4350_ram.ld -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections").split()


def present():
    return b"Found DFU" in subprocess.run(["dfu-util", "-l"], capture_output=True).stdout


def wait_dfu(limit=120):
    t0 = time.time()
    while not present():
        if time.time() - t0 > limit:
            sys.exit("the unit did not come back to DFU mode")
        time.sleep(0.15)


def run(src, defines=(), extra=(), build="build/tele"):
    os.makedirs(os.path.join(root, "build"), exist_ok=True)
    cmd = ["arm-none-eabi-gcc"] + CFLAGS + list(defines) + ["src/startup.c", src, "src/dfu.c"] + list(extra) + ["-o", build + ".elf"]
    subprocess.run(cmd, cwd=root, check=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", build + ".elf", build + ".bin"], cwd=root, check=True)
    subprocess.run([sys.executable, "src/mkbootimg.py", build + ".bin", build + ".dfu"], cwd=root, check=True, capture_output=True)
    wait_dfu()
    r = subprocess.run(["dfu-util", "-d", "1fc9:000c", "-a", "0", "-D", build + ".dfu"], cwd=root, capture_output=True)
    if b"Download done" not in r.stdout:
        sys.exit("download failed:\n" + (r.stdout + r.stderr).decode()[-400:])
    t0 = time.time()
    time.sleep(0.5)
    wait_dfu()
    return time.time() - t0


def main():
    a = sys.argv[1:]
    raw = "--raw" in a
    if raw:
        a.remove("--raw")
    extra = []
    while "--extra" in a:
        i = a.index("--extra")
        extra.append(a[i + 1])
        del a[i:i + 2]
    defines = [x for x in a if x.startswith("-D")]
    src = [x for x in a if not x.startswith("-D")]
    if len(src) != 1:
        sys.exit(__doc__)
    t = run(src[0], defines, extra)
    print(f"{t:.2f}" if raw else round((t - BASE) / UNIT))


if __name__ == "__main__":
    main()
