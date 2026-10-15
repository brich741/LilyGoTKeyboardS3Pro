# Claude Macro Pad (LilyGo T-Keyboard S3 Pro)

PlatformIO/Arduino firmware + phone web UI that turns the pad into a phone-configurable USB macro keyboard for Claude Code.

**Read `docs/DESIGN.md` first** (architecture, decisions and why, hardware facts, gotchas, open items). Config/API contract: `docs/CONFIG.md`.
Vocabulary: `docs/CONTEXT.md`. Keep these docs current when behaviour or decisions change.

## Layout
- `firmware/lib/core/` hardware-independent logic (host-tested, no Arduino headers) — put new logic here when possible
- `firmware/src/` device layer; `firmware/data/www/` phone UI; `firmware/data/icons/` starter icons; `firmware/diag/` diagnostics
- `tools/` icon generator, host test runner, STM32 hex (git-ignored, GPL), `docs/` notes

## Commands (run from `firmware/`, PowerShell)
- Build: `pio run -e esp32s3` | flash: `pio run -e esp32s3 -t upload` | UI+icons+defaults: `pio run -e esp32s3 -t uploadfs`
- Host tests: `python tools/host_tests.py` (no system compiler needed)

## Rules learned the hard way
- `uploadfs` **erases saved config and uploaded icons**; ask before doing it if the user may have customised the pad.
- Opening/closing the pad's serial port can leave it in ROM download mode (dark screens): reset with esptool `--after hard-reset` (see DESIGN.md section 6).
- Lock order: `cfgMtx` before `hwMtx`; all I2C/SPI under `hwMtx`.
- Never write a literal newline into a C string through a shell heredoc/patch script; edit files with real file tools.
- The STM32 co-processor firmware is the factory V1 until the user flashes LilyGo's V2 (hue bug). Do not flash the STM32 from the ESP32. Do not commit the GPL hex.
- Commit only when asked; commit messages end with the Co-Authored-By trailer from the session reminder.
