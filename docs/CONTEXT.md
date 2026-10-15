# Context: shared vocabulary

Use these words consistently in code, UI and docs.

| Term | Meaning |
|---|---|
| **Pad** | The LilyGo T-Keyboard S3 Pro running this firmware. |
| **Page** | A named set of 4 key configs plus an encoder-push config and an LED effect. Up to 8. The encoder knob switches pages. |
| **Key** | One of the 4 physical keys, each with its own 128x128 screen. The **encoder push** is a 5th input with no screen. |
| **Gesture** | How a key was pressed: **single**, **double**, or **hold** (with a start on crossing the threshold and an end on release). |
| **Action** | What a gesture does: an ordered list of **Steps**. |
| **Step** | One unit of output: `key` (tap/down/up with modifiers), `text`, `media`, or `delay`. |
| **Held key** | A key a Step list pressed with `down` and has not yet released. The firmware always releases these (after a hold ends, on page switch, on config save, on USB suspend). |
| **Icon** | A 128x128 RGB565 image in the shared icon library, referenced by id. |
| **Look** | The icon + LED color a key shows right now (idle, a gesture override, or the after-hold state). |
| **Zone** | The set of LEDs (14-bit mask) above a key. Set once by LED calibration. |
| **Effect** | The page's idle LED animation (off, solid, breathe, rainbow, chase, sparkle). A key's Look color overrides its Zone. |

Architecture: `firmware/lib/core` is hardware-independent and unit-tested on the host; `firmware/src` is the thin device layer (USB, displays, LEDs, Wi-Fi); `firmware/data/www` is the phone UI. The contract between UI and firmware is `docs/CONFIG.md`.
