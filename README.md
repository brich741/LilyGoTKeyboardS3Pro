# Claude Macro Pad: LilyGo T-Keyboard S3 Pro

Turns the T-Keyboard S3 Pro into a USB macro pad you configure from your phone, with no recompiling.

- 4 keys with 128x128 screens, each with single / double / hold gestures, custom icons per state
- Rotary encoder: turn to switch **pages**, push is a 5th button (hold it 3 s to show the Wi-Fi password)
- RGB strip: per-page animation and per-key colours over the key that is pressed (**real colours need a one-time STM32 firmware update, see below**)
- Config lives on the device; the device hosts its own Wi-Fi network and web UI

Start with [docs/DESIGN.md](docs/DESIGN.md) (architecture, decisions and rationale, hardware findings, open items). See also
[docs/CONFIG.md](docs/CONFIG.md) (config/API contract), [docs/CONTEXT.md](docs/CONTEXT.md) (vocabulary) and
[docs/STM32_UPDATE.md](docs/STM32_UPDATE.md) (co-processor firmware update for real LED colours).

## Build and flash

Requires PlatformIO Core 6.2+ (the pioarduino platform needs it).

```
cd firmware
pio run -e esp32s3                 # build
pio run -e esp32s3 -t upload       # flash firmware
pio run -e esp32s3 -t uploadfs     # upload the web UI (data/www) to the device
```

The firmware runs USB in TinyUSB mode (HID keyboard + serial). If upload cannot find the port, put the board in
download mode: hold **BOOT**, tap **RESET**, release **BOOT**, then upload again. Run `uploadfs` once after the first flash
and again whenever you change `firmware/data/www`. **On a pad you have customised, update the web UI with `tools/uploadfs_keep.py` (run it with PlatformIO's
python: `%USERPROFILE%/.platformio/penv/Scripts/python.exe tools/uploadfs_keep.py`)**: plain `uploadfs` erases your saved settings and uploaded icons.

## Use

1. Plug the pad into your PC and switch its Wi-Fi on (hold the knob 3 s, then push it). Join Wi-Fi **ClaudePad** (password `claudepad1`; change it in Settings).
2. Open `http://192.168.4.1` (a captive portal normally opens it for you).
3. Tap **Generate starter icons**, then **Save**. Use **LED calibration** once so key colors light the right LEDs.
4. Edit keys: tap a key, pick a gesture, add steps, **Test** sends them to the PC, **Save** applies live.

### Claude Code push-to-talk
The starter "Claude Code" page holds **Ctrl+D** while key 1 is held (the VS Code extension shows Ctrl+D). For the CLI, bind it in `~/.claude/keybindings.json`
(`voice:pushToTalk` -> `ctrl+d`) for the CLI; confirm what the VS Code extension uses and change the key in the UI if needed.

### Wi-Fi on the pad
**Wi-Fi is off when the pad powers up.** Hold the **knob for 3 seconds**: the four screens show Wi-Fi ON/OFF, the **network name**, the
**password**, the **address** (`http://192.168.4.1`) and how many devices are connected. **Push the knob** to switch Wi-Fi on (or off again);
press any key or turn the knob to close the screen. The choice lasts until the next power-up. To have Wi-Fi start at power-up, tick
*Start Wi-Fi when the pad powers on* in the web UI settings.

### Recovery
- Forgot the Wi-Fi password or switched Wi-Fi off: hold the knob while plugging in USB (resets SSID/password to defaults and starts Wi-Fi for that session), or hold it 3 s any time to see them and switch Wi-Fi.
- Factory reset is in the web UI under Backup.

## Required for LED colours: update the pad's STM32 firmware (one-time, needs a programmer)

> **Heads-up:** the T-Keyboard S3 Pro ships with a bug in the firmware of its small **STM32** co-processor (the chip that drives the LEDs, keys and screen
> selects): LED **hue cannot be set** ([LilyGo issue #7](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3-Pro/issues/7)). Everything in this project works
> without the fix **except coloured LEDs**; until you update it the strip shows a garbled pink. LilyGo published a fixed firmware (V2), but it can only be
> installed with a small hardware programmer plugged into the pad's SWD socket. This is a one-time job (about 15 minutes) and is recoverable (SWD can always
> reflash the chip). If you would rather not, turn **LED colors: real hue** off in the web UI Settings (or run `ledhue 0` on the serial console) to get the
> white / pink-only mode (brightness and saturation work on the old firmware).

**What you need** (about US$5-15 in total; prices vary, these are examples, not endorsements):

| Item | What to buy | Cheap example |
|---|---|---|
| SWD programmer | An **ST-LINK/V2** USB dongle (the little aluminium "mini" version, a clone is fine; it must have `SWDIO`, `SWCLK`, `GND`). Clones cost a few dollars; genuine ones cost more. | [Waveshare ST-LINK/V2 (mini), about $9](https://www.waveshare.com/product/st-link-v2-mini-stm32.htm), or search for the cheapest clone: [AliExpress](https://www.aliexpress.com/w/wholesale-st-link-v2.html), [Amazon](https://www.amazon.com/s?k=ST-Link+V2+programmer+mini) |
| Cable | A **4-pin, 1.0 mm pitch JST-SH** cable (the "Qwiic / STEMMA QT" plug) with male Dupont pins on the other end, to go from the pad's small SWD socket to the programmer's pins. **Measure the socket before ordering**: the four slots should be about 3 mm apart end to end. | [Adafruit STEMMA QT / Qwiic JST-SH 4-pin to male headers, 150 mm, about $1](https://www.adafruit.com/products/4209) |
| Software | **STM32CubeProgrammer** (free, needs a free ST account) | [st.com/en/development-tools/stm32cubeprog.html](https://www.st.com/en/development-tools/stm32cubeprog.html) |
| Firmware file | LilyGo's fixed STM32 firmware: the `STM32G030F6P6_Drive_0x01 ... V2` `.hex` (the main board is address `0x01`). It is GPL-3.0, so it is not stored in this repo. | [LilyGo `firmware` folder](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3-Pro/tree/stm32cubeg0-firmware-package_V1.6.2/firmware) |

**How to do it** (full walkthrough with pin order, a multimeter check and the exact commands: [docs/STM32_UPDATE.md](docs/STM32_UPDATE.md)):

1. Open the pad enough to see the back of the keyboard PCB. Two small 4-pin sockets sit side by side: the **right-hand one is SWD** (silkscreen `GND  +3.3V  SWCLK  SWDIO`);
   the left one is UART, leave it.
2. Wire the programmer: `GND` to pin 1, `SWCLK` to pin 3, `SWDIO` (printed `SWIO` on many clones) to pin 4. **Never connect a 5 V pin.** If the pad is powered over its own USB,
   leave the programmer's 3.3 V wire off (pin 2).
3. Plug the pad into USB (or let the programmer power it) and the programmer into your PC, then in STM32CubeProgrammer choose **ST-LINK**, port **SWD**, mode **Normal**, and **Connect**.
   You should see an `STM32G03x` with 32 KB flash and a target voltage near 3.3 V.
4. Back up the chip first (read `0x08000000`, size `0x8000`, save a `.bin`), then program LilyGo's `.hex` with **Verify** ticked and **Run after programming**.
5. Unplug everything, plug the pad back into USB, and the LEDs now take real colours.

Diagnostic builds used for this live in `firmware/diag` (`pio run -e rgbdemo|ledexamples|rgbcycle|rgbdemo_core2|rgbcycle_core2 -t upload`);
flash `esp32s3` again afterwards. The serial console (`help`, `ledprobe`, `huesweep`, `type`, `tap`, `wipe`) is on the pad's
USB serial port; after closing a terminal on it the pad may fall into ROM download mode, so reset it with esptool or replug.

## Tests

Host-side unit tests for `firmware/lib/core` (gesture engine, config, key runner, LED effects), no system compiler needed:

```
python tools/host_tests.py      # bootstraps zig + Unity + ArduinoJson into .hosttest/, then runs 28 tests
```

(`pio test -e native` also works if gcc/clang is on PATH.)

Web UI without hardware: serve `firmware/data/www` (for example `python -m http.server`) and open `index.html?mock`.
