# USB device-mode (adb) - what the Type-C port does and how to use it

The board's Type-C port (USB1, the dwc2 OTG controller `ff740000.usb`) runs
in **USB device (gadget) mode**. In the running system it enumerates as a
**Rockchip adb device** - Windows names it "rk3xxx USB Device" until the
driver is installed.

## What enumerates (verified live)

| Item | Value |
|---|---|
| USB VID:PID | `0x2207:0x0006` (Rockchip) |
| Gadget | configfs (`/sys/kernel/config/usb_gadget/rockchip`), function `ffs.adb` |
| Kernel | dwc2 UDC driver, `functionfs` (endpoints backed by userspace) |
| Daemon | `/usr/bin/adbd` (Buildroot `android-adbd` package), runs **as root**, no auth |
| Set up by | `/etc/init.d/S50usbdevice.sh` -> `/usr/bin/usbdevice` |

Note: `adb` is just a protocol - nothing Android here. Rockchip's Buildroot
SDK ships adbd so the board inherits the whole mature adb toolchain
(drivers, shell, file sync, port forwarding) over USB with no network.

## Driver setup (Windows)

Install the adb driver once: Rockchip **DriverAssistant**, or Google's USB
driver, or any adb-compatible INF. After that the device shows as
"Android Composite ADB Interface" and `adb devices` lists it:

```
79a8bf5242d82e6f       device transport_id:1
```

Any other adb-capable board works with the same drivers (same INFs match
all Rockchip VIDs and the standard adb interface signature), and adb
handles multiple devices at once (`adb -s <serial> ...`).

## Verified capabilities (all tested on this board)

### 1. Root shell - no network, no password

```
adb shell id
  uid=0(root) gid=0(root)
adb shell uname -a
  Linux rk3506-buildroot 6.1.118 ... armv7l
```

Useful when eth1 is down and the serial console is on another machine.
(Caveat: it is a root shell with **no authentication** - physical USB
access to the port = root on the board. Don't plug it into untrusted hosts.)

### 2. File push/pull (sync protocol)

```
adb push local.txt /tmp/remote.txt
adb pull /tmp/log.txt .
```

Verified both directions. Good for dropping binaries when SSH is broken.

### 3. TCP port forward through USB (`adb forward`)

```
adb forward tcp:2222 tcp:22      # PC:2222 -> board sshd (over USB)
ssh -p 2222 root@127.0.0.1       # works; verified SSH banner exchange
adb forward tcp:8080 tcp:80      # would relay a web server the same way
adb forward --remove tcp:2222
```

End-to-end proof on this board: `adb forward tcp:2222 tcp:22` then opening
127.0.0.1:2222 returned `SSH-2.0-OpenSSH_9.6` from the board's sshd.

### 4. Reverse forward (`adb reverse`) - board -> PC

```
adb reverse tcp:8080 tcp:8080    # board's localhost:8080 -> PC's :8080
adb reverse --list / --remove-all
```

Verified supported (UsbFfs relay listed).

### 5. NOT supported: network adb (`adb tcpip`)

```
adb tcpip 5555     ->  error: closed
```

This Buildroot adbd is USB-only; `adb tcpip`/`adb connect` do not work.
For network debugging use the real network (SSH) - adb remains the
no-network fallback.

## Emergency workflow when the LAN is broken

```
adb forward tcp:2222 tcp:22        # relay SSH over USB
ssh -p 2222 root@127.0.0.1         # password root, or WSL key via port 2222
adb push rk3506-dev-binary /root/  # deploy without scp
```

## The same port in other boot states

| State | Device identity | Talked to by |
|---|---|---|
| Linux running (normal) | `0x2207:0x0006` adb gadget | adb clients |
| U-Boot "Loader" mode | rockusb device ("Loader") | RKDevTool / rkdeveloptool |
| MaskROM (H1 jumper / no boot media) | rockusb device ("MaskROM") | RKDevTool / DriverAssistant / rkdeveloptool |

**Loader-mode incident note:** RKDevTool's "Switch to Loader" reboots the
board into the rockusb server - Linux stops, SSH/LCD go silent. It is
volatile and harmless: power-cycle, or press Ctrl+C on the serial console
(U-Boot prompt) and type `reset`, or use RKDevTool's Advanced ->
Reset Device. Nothing is written unless you explicitly Download/Erase.

## Running adb from WSL

USB adb needs the device attached into WSL via `usbipd-win`
(`usbipd bind/attach`), or simply run `adb.exe` directly on Windows -
the latter is what was used for all tests above.
