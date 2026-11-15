# Pico RCM Injector

Turn a Waveshare RP2350-USB-CM board into an RCM payload injector for the Nintendo Switch.

The board acts as a USB host: it waits for a Switch in RCM mode, then sends a payload. You can build the payload into the firmware, or just drag a `payload.bin` onto the board like a USB stick.

## How it works

The board uses its two USB ports:

- **Host port** (the PIO-USB port) connects to the Switch and sends the payload.
- **Flashing port** (the native USB) does everything else: flashing the firmware, dropping a payload, and powering the board.

A WS2812 RGB LED shows the status by color. Keep the board powered through the flashing port the whole time.

## Set the payload

**Drag and drop (easiest).** Plug the board into your computer with the flashing port. A drive called `RCM Payload Drive` appears. Copy your `payload.bin` onto it; the board saves it to flash and keeps it after you unplug. A dropped payload replaces the built-in one. Max size is 1 MiB.

**Built-in default.** The firmware ships with a hekate payload baked in, used when no file has been dropped. See [Build](#build) to change it.

## Use it

1. Flash the latest `.uf2` (see [Build](#build), or grab it from Releases).
2. Set your payload (above).
3. Put the Switch in RCM mode.
4. Connect the Switch to the board's host port.
5. The LED turns green when the payload has been sent.
6. Unplug the Switch.

## LED status

- **Blue, slow blink** — waiting for a Switch.
- **Amber, solid** — sending the payload.
- **Green, solid** — done.
- **Red, solid** — the USB host failed to start.
- **Red, blinking** — an error; count the blinks:
  1. no payload
  2. could not open the Switch's USB endpoints
  3. reserved (skipped)
  4. payload failed to send
  5. final trigger failed

At power-up the LED blinks three times to show the active payload: green for a dropped file, blue for the built-in default. Logs are printed over UART (USB logging is off because the host port is busy).

## Build

You need the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk), [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), and the Arm GNU toolchain (the build needs newlib). By default the build looks for `pico-sdk` and `Pico-PIO-USB` as sibling folders next to this repo; set `PICO_SDK_PATH` / `PICO_PIO_USB_PATH` to point elsewhere.

macOS / Linux:

```bash
./build.sh
```

Windows:

```powershell
.\build.ps1
```

The result is `build/pico_rcm_injector.uf2`. To flash it, hold `BOOTSEL`, plug the board into the flashing port, and copy the `.uf2` onto the `RP2350` drive.

### Change the built-in payload

```bash
python tools/bin2payload_h.py path/to/payload.bin include/payload.h
./build.sh
```

`include/payload.example.h` shows the header format.

## Hardware

- Use a known-good USB-C data cable from the host port to the Switch.
- The board powers the Switch side, so give it a stable 5 V supply through the flashing port.
- Do not connect two powered USB hosts together.

## Lawful use only

Use this only for lawful homebrew, recovery, repair, research, and interoperability, and only on hardware you own or are allowed to service. Do not use it to pirate games, bypass payment or online-service rules, or share console keys or copyrighted firmware. Follow the laws, licenses, and platform rules where you live. If you are not sure your use is legal, check first. Use at your own risk — I am not responsible for any damage to your board or your Switch.

## Credits and license

Licensed under GPL-3.0-only. The launch sequence is ported from public microcontroller versions of the Fusée launcher flow, especially [blockfeed/sam-fusee-launcher-internal](https://github.com/blockfeed/sam-fusee-launcher-internal). The bundled payload is [CTCaer/hekate](https://github.com/CTCaer/hekate); see `NOTICE.md` and `third_party/` for sources, credits, and licenses.
