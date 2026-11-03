#!/usr/bin/env python3
# M0 milestone-1 runner: load, release, time the calibration loop
import subprocess, time, sys

DEVMEM = "/usr/sbin/devmem"

def read_status():
    out = subprocess.check_output([DEVMEM, "0x03C00004"]).decode().strip()
    return int(out, 16)

print("pre-start DDR shm word0:", subprocess.check_output([DEVMEM, "0x03C00000"]).decode().strip())

t0 = time.time()
subprocess.run(["insmod", "rk3506_m0_loader.ko", "fw=/root/m0.bin"], check=True)
print("insmod ok (M0 released), t0 set")

deadline = time.time() + 120
n = 0
while time.time() < deadline:
    st = read_status()
    n += 1
    if st == 2:
        t1 = time.time()
        break
    time.sleep(0.02)
else:
    print("TIMEOUT waiting for DONE")
    sys.exit(1)

cycles = 0x04000000 * 3
elapsed = t1 - t0
print("status DONE after %.3f s (%d polls)" % (elapsed, n))
print("cal loop: %d iters x 3 cyc = %d cycles" % (0x04000000, cycles))
print("=> M0 core clock ~ %.1f MHz" % (cycles / elapsed / 1e6))
