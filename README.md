# Pico RCM Injector

Turn a Waveshare RP2350-USB-CM board into an RCM payload injector for the Nintendo Switch.

The board acts as a USB host. It waits for a Switch in RCM mode, then sends the payload. You can build the payload into the firmware. Or you can just drag a `payload.bin` onto the board, like a USB stick.

## How it works

The board uses two USB ports:

- **Host port** connects to the Switch. The board sends the payload here.
- **Flashing port** (the native USB) does the rest. You flash the board, drop a payload, and power it here.

A WS2812 RGB LED shows the status with colors.

## Set the payload

You have two options.

### Option 1: Drag and drop (easiest)

1. Plug the board into your computer with the flashing port.
2. A drive called `RCM Payload Drive` shows up.
3. Copy your `payload.bin` onto it.
4. The board saves it to flash. It stays there after you unplug.

A dropped payload replaces the built-in one. The max size is 1 MiB.

### Option 2: Build it in

The firmware ships with a hekate payload baked in. The board uses it if you haven't dropped a file. See [Build](#build) to change it.

## Use it

1. Flash the latest `.uf2` (see [Build](#build), or get it from Releases).
2. Set your payload (see above).
3. Put the Switch in RCM mode.
4. Connect the Switch to the board's host port.
5. The board sends the payload. The LED turns green when it is done.
6. Unplug the Switch.

Keep the board powered the whole time. Power it through the flashing port.

## LED status

- **Blue, slow blink:** waiting for a Switch.
- **Amber, solid:** sending the payload.
- **Green, solid:** done.
- **Red, solid:** the USB host failed to start.
- **Red, blinking:** an error happened. Count the blinks:
  - 1: no payload.
  - 2: could not open the Switch's USB endpoints.
  - 3: reserved (the firmware skips this check).
  - 4: the payload failed to send.
  - 5: the final trigger failed.

The board also prints logs over UART. USB logging is off because the host port is busy.

## Build

Install the Raspberry Pi Pico SDK and the Arm GNU toolchain (the one with newlib). Set `PICO_SDK_PATH` to your SDK folder.

On macOS or Linux:

```bash
./build.sh
```

On Windows:

```powershell
.\build.ps1
```

The build makes `build/pico_rcm_injector.uf2`.

To flash it, hold `BOOTSEL`, plug the board into your PC, and copy the `.uf2` to the `RP2350` drive. The build makes the UF2 on its own, so you do not need `picotool`.

### Change the built-in payload

Turn your `.bin` into a C header, then build again:

```bash
python tools/bin2payload_h.py path/to/payload.bin include/payload.h
./build.sh
```

`include/payload.example.h` shows the header format.

## Hardware

- You need a USB-C cable to connect the Switch to the host port.
- The board powers the Switch side, so give it a stable 5 V supply.
- Do not connect two powered USB hosts together.
- Use a known-good cable.

There is a 3D-printable case here: https://www.printables.com/model/1762449-pico-case-for-re-chargeable-battery-and-onoff-tac/files

## Disclaimer

Use at your own risk. I am not responsible for any damage to your board or your Switch.

## Lawful use only

Use this project only for lawful work. That means homebrew, recovery, repair, research, and interoperability. Use it only on hardware you own or are allowed to service.

Do not use it to pirate games or other copyrighted works. Do not use it to bypass payment or break online service rules. Do not share console keys or copyrighted firmware. Do not help anyone else do these things.

You must follow the laws, licenses, and platform rules where you live. This project does not give you permission to break copyright protection.

If you are not sure your use is legal, do not use it yet. Check the rules first, or get legal advice.

This project includes a generated hekate payload. See `NOTICE.md` and `third_party/` for the source, credits, and license.

## Project layout

- `src/main.c`: USB host loop, Switch detection, LED, and drag-and-drop setup.
- `src/rcm_injector.c`: the RCM injection sequence.
- `src/msc_disk.c`: the fake USB drive that catches a dropped file.
- `src/payload_store.c`: saves a dropped payload to flash.
- `include/payload.h`: the built-in payload (generated).
- `tools/bin2payload_h.py`: turns a `.bin` into a C header.
- `tools/bin2uf2.py`: turns a `.bin` into a UF2 file.

## Sources and prior art

- [kleo/fusee-alpestre](https://github.com/kleo/fusee-alpestre)
- [blockfeed/sam-fusee-launcher-internal](https://github.com/blockfeed/sam-fusee-launcher-internal)
- [Raspberry Pi Pico SDK TinyUSB host support](https://www.raspberrypi.com/documentation/pico-sdk/third_party.html#tinyusb_host)
- [TinyUSB USB concepts](https://docs.tinyusb.org/en/latest/reference/usb_concepts.html)

## License

GPL-3.0-only. The launch sequence comes from GPL microcontroller versions of the public Fusee launcher flow.
