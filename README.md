# Pico RCM Injector

A pocket-sized RCM payload injector for the Nintendo Switch, built on the **Waveshare RP2350-USB-CM**. No computer needed at the console — just plug and go.

## What it does

The board *is* the injector. It waits for a Switch in RCM mode, sends your payload, and turns green when it's done. No PC, no app — it all runs on the board.

It comes with hekate built in, so it works right out of the box. Want a different payload? Drop your own onto the board. More on both below.

## Two ways to load a payload

**1. Built-in hekate (easiest).** The firmware ships with [hekate](https://github.com/CTCaer/hekate) baked in. Flash it and you're ready — nothing else to set up. This is all most people need.

**2. Drop in your own (for advanced users).** Want a different payload? Plug the board into your computer. A small USB drive named `RCMINJECT` appears. Copy your `payload.bin` onto it — done. The board saves it to flash and keeps it after you unplug. A dropped file always beats the built-in one. Max size is 1 MiB.

Want to check which one is active? At power-up the LED blinks three times — **blue** for the built-in hekate, **green** for your dropped file.

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

## Credits and license

Licensed under GPL-3.0-only. The launch sequence is ported from public microcontroller versions of the Fusée launcher flow, especially [blockfeed/sam-fusee-launcher-internal](https://github.com/blockfeed/sam-fusee-launcher-internal). The bundled payload is [CTCaer/hekate](https://github.com/CTCaer/hekate). See `NOTICE.md` and `third_party/` for sources, credits, and licenses.
