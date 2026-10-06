# Programming the ATmega328PB Uno-compatible board

The `uno` environment in `platformio.ini` targets **ATmega328PB**, using MiniCore
and a 16 MHz clock. The environment name is retained for existing IDE tasks.
The ordinary `board = uno` definition targets ATmega328P instead.

The confirmed upload method is USB with a factory/Uno-style serial bootloader:
`upload_protocol = arduino`, `upload_speed = 115200`. A 512-byte bootloader region
is reserved. These settings assume the board has a 16 MHz oscillator and a
compatible bootloader already installed.

Use PlatformIO's **Upload** task for `uno`, or run in a PlatformIO terminal:

```powershell
pio run -e uno
pio run -e uno -t upload
```

If several serial devices are connected, select the board's port explicitly:

```powershell
pio device list
pio run -e uno -t upload --upload-port COM5
```

Replace COM5 with the actual port. Alternatively, add `upload_port = COM5` to
`platformio.ini`. No fixed port is committed because it varies across machines.

If upload fails to synchronize, verify the port and bootloader baud rate; some
factory bootloaders use 57600 instead of 115200. A board with MiniCore Urboot needs
`upload_protocol = urclock` instead. A blank chip needs a bootloader installed
using an ISP programmer before USB uploads can work.

Do not bypass a signature mismatch with `-F`. ATmega328PB's device signature is
different from ATmega328P's; investigate the reported signature and bootloader
if uploading still identifies the wrong device.

Changing the build environment does not burn a bootloader or alter hardware
fuses. This setup keeps the existing application pin assignments.

References:
- https://docs.platformio.org/en/latest/boards/atmelavr/ATmega328PB.html
- https://github.com/MCUdude/MiniCore/blob/master/PlatformIO.md
