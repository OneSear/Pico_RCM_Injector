# Pico RCM Injector

A pocket-sized RCM payload injector for the Nintendo Switch, built from a cheap RP2350 board. No computer needed at the console — just plug and go.

## What it does

The board *is* the injector. It waits for a Switch in RCM mode, sends your payload, and turns green when it's done. No PC, no app — it all runs on the board.

You load a payload two ways: drop a file onto the board, or bake one into the firmware. More on both below.

Built for the **Waveshare RP2350-USB-CM**.

## Two ways to load a payload

**1. Drag and drop (easiest).** Plug the board into your computer. A small USB drive named `RCMINJECT` appears. Copy your `payload.bin` onto it — done. The board saves it to flash and keeps it after you unplug. Max size is 1 MiB.

**2. Built-in default.** The firmware ships with [hekate](https://github.com/CTCaer/hekate) baked in. It runs whenever no file has been dropped. A dropped file always takes priority.

Want to check which one is active? At power-up the LED blinks three times — **green** for your dropped file, **blue** for the built-in default.

## How it works

The board has two USB ports. Each has one job:

- **Host port** (PIO-USB) — connects to the Switch and sends the payload.
- **Flashing port** (native USB) — flashes firmware, loads payloads, and powers the board.

A WS2812 RGB LED shows the status by color. Keep the board powered through the flashing port the whole time.

## Use it

1. Flash the latest `.uf2` — grab it from Releases, or [build it](#build).
2. Load a payload (see above).
3. Put your Switch in RCM mode.
4. Plug the Switch into the board's host port.
5. Wait for green.
6. Unplug the Switch.

## LED status

- **Blue, slow blink** — waiting for a Switch.
- **Amber, solid** — sending the payload.
- **Green, solid** — done.
- **Red, solid** — the USB host failed to start.
- **Red, blinking** — an error. Count the blinks:
  1. no payload
  2. couldn't open the Switch's USB endpoints
  3. reserved (skipped)
  4. payload failed to send
  5. final trigger failed

Logs print over UART. (USB logging is off, since the host port is busy.)

## Build

You need the [Pico SDK](https://github.com/raspberrypi/pico-sdk), [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), and the Arm GNU toolchain (the build needs newlib). By default the build looks for `pico-sdk` and `Pico-PIO-USB` as sibling folders next to this repo. To point elsewhere, set `PICO_SDK_PATH` / `PICO_PIO_USB_PATH`.

macOS / Linux:

```bash
./build.sh
```

Windows:

```powershell
.\build.ps1
```

You'll get `build/pico_rcm_injector.uf2`. To flash it, hold `BOOTSEL`, plug the board into the flashing port, and drop the `.uf2` onto the `RP2350` drive.

### Change the built-in payload

```bash
python tools/bin2payload_h.py path/to/payload.bin include/payload.h
./build.sh
```

See `include/payload.example.h` for the header format.

## Hardware

- Use a known-good USB-C data cable from the host port to the Switch.
- The board powers the Switch side, so give it a stable 5 V through the flashing port.
- Never connect two powered USB hosts together.

## Lawful use only

Use this only for lawful homebrew, recovery, repair, research, and interoperability, and only on hardware you own or are allowed to service. Don't use it to pirate games, bypass payment or online-service rules, or share console keys or copyrighted firmware. Follow the laws, licenses, and platform rules where you live. Not sure your use is legal? Check first. Use at your own risk — I'm not responsible for any damage to your board or your Switch.

## Credits and license

Licensed under GPL-3.0-only. The launch sequence is ported from public microcontroller versions of the Fusée launcher flow, especially [blockfeed/sam-fusee-launcher-internal](https://github.com/blockfeed/sam-fusee-launcher-internal). The bundled payload is [CTCaer/hekate](https://github.com/CTCaer/hekate). See `NOTICE.md` and `third_party/` for sources, credits, and licenses.
