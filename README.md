# Claude Macro Pad: LilyGo T-Keyboard S3 Pro

Turns the T-Keyboard S3 Pro into a USB macro pad you configure from your phone, with no recompiling.

- 4 keys with 128x128 screens, each with single / double / hold gestures, custom icons per state
- Rotary encoder: turn to switch **pages**, push is a 5th button (hold it 3 s to show the Wi-Fi password)
- RGB strip: per-page animation and per-key lighting over the key that is pressed (white / pink only, see below)
- Config lives on the device; the device hosts its own Wi-Fi network and web UI

See [docs/CONFIG.md](docs/CONFIG.md) for the config/API contract and [docs/CONTEXT.md](docs/CONTEXT.md) for the vocabulary.

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
and again whenever you change `firmware/data/www`.

## Use

1. Plug the pad into your PC. Join Wi-Fi **ClaudePad** (password `claudepad1`; change it in Settings).
2. Open `http://192.168.4.1` (a captive portal normally opens it for you).
3. Tap **Generate starter icons**, then **Save**. Use **LED calibration** once so key colors light the right LEDs.
4. Edit keys: tap a key, pick a gesture, add steps, **Test** sends them to the PC, **Save** applies live.

### Claude Code push-to-talk
The starter "Claude Code" page holds **Ctrl+D** while key 1 is held (the VS Code extension shows Ctrl+D). For the CLI, bind it in `~/.claude/keybindings.json`
(`voice:pushToTalk` -> `ctrl+d`) for the CLI; confirm what the VS Code extension uses and change the key in the UI if needed.

### Recovery
- Forgot the Wi-Fi password: hold the encoder button while plugging in USB (resets SSID/password to defaults), or hold it 3 s any time to display them.
- Factory reset is in the web UI under Backup.

## LED limitation (hardware)

The pad's STM32 co-processor ignores the **hue** registers in free LED mode. This was verified with LilyGo's own
unmodified `RGB.ino`, a red/green/blue test, register sweeps, and both Arduino-ESP32 core 3.x and 2.0.14: every
hue paints the same colour. What works is per-LED **brightness** and **saturation**, so colours render as
white (pale) up to one fixed magenta-pink (saturated), and the "rainbow" effect is a rotating white/pink wave.
The STM32's own built-in red/green/blue demo cycle does work (all LEDs together), but cannot be steered.
Getting real colours would need the STM32 firmware replaced, which this project does not attempt (a failed
flash would disable keys, LEDs and screen selection).

Diagnostic builds used for this live in `firmware/diag` (`pio run -e rgbdemo|ledexamples|rgbcycle|rgbdemo_core2|rgbcycle_core2 -t upload`);
flash `esp32s3` again afterwards. The serial console (`help`, `ledprobe`, `huesweep`, `type`, `tap`, `wipe`) is on the pad's
USB serial port; after closing a terminal on it the pad may fall into ROM download mode, so reset it with esptool or replug.

## Tests

Host-side unit tests for `firmware/lib/core` (gesture engine, config, key runner, LED effects):

```
cd firmware
pio test -e native      # needs a host C++ compiler (gcc/clang) on PATH
```

Web UI without hardware: serve `firmware/data/www` (for example `python -m http.server`) and open `index.html?mock`.
