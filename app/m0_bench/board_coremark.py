#!/usr/bin/env python3
# M0 CoreMark runner v2: poll the MAILBOX result register (kernel-proof)
import subprocess, time, sys

DEVMEM = "/usr/sbin/devmem"

def mbox():
    out = subprocess.check_output([DEVMEM, "0xFF29001C"]).decode().strip()
    return int(out, 16)

print("pre-start mailbox:", hex(mbox()))
t0 = time.time()
subprocess.run(["insmod", "rk3506_m0_loader.ko"], check=True)
print("insmod ok - M0 released, CoreMark running (5000 iterations)...")

deadline = time.time() + 180
v = 0
while time.time() < deadline:
    v = mbox()
    if v & 0x80000000:
        t1 = time.time()
        break
    time.sleep(0.1)
else:
    print("TIMEOUT - mailbox word:", hex(v))
    sys.exit(1)

elapsed = t1 - t0
iters = v & 0x3FFFFFFF
print("DONE flag seen after %.2f s" % elapsed)
print("CoreMark score = %d / %.2f = %.1f iterations/sec" % (iters, elapsed, iters / elapsed))
