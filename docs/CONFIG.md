# Config & API contract (v1)

Single source of truth shared by the firmware (`firmware/lib/core`) and the web UI (`firmware/data/www`).

## config.json

```jsonc
{
  "version": 1,
  "settings": {
    "apSsid": "ClaudePad",          // 1-31 chars
    "apPass": "claudepad1",         // 8-63 chars (WPA2)
    "brightness": 80,               // screens, 5-100 (%)
    "ledBrightness": 40,            // LEDs, 0-100 (%)
    "dimMinutes": 10,               // 0 = never dim
    "doubleMs": 250,                // double-press window
    "holdMs": 400,                  // hold threshold
    "revertMs": 1500,               // default after-hold / flash duration
    "activePage": 0,
    "ledHue": true,                 // default true = real LED hues (needs LilyGo's fixed STM32 firmware); false = controller ignores hue, white/pink only
    "ledZones": [3, 24, 192, 1536]  // 14-bit LED masks above keys 1..4 (bit0 = LED1); default = LED pairs 1-2, 4-5, 7-8, 10-11
  },
  "icons": [ { "id": "mic-red", "name": "Mic (red)" } ],   // files live at /icons/<id>.565
  "pages": [                         // 1..8 pages
    {
      "name": "Claude Code",         // 1-15 chars
      "led": { "effect": "breathe", "color": "#3355ff", "speed": 50 },
                                     // effect: off|solid|breathe|rainbow|chase|sparkle ; speed 1-100
      "keys": [ Key, Key, Key, Key ],   // exactly 4, left to right
      "encoder": Key                    // push button of the rotary encoder (visual fields ignored)
    }
  ]
}
```

### Key
```jsonc
{
  "idle":   { "icon": "mic-red", "color": "#ff0000" },   // icon id ("" = black) ; LED color ("" = none)
  "single": Gesture,    // optional
  "double": Gesture,    // optional
  "hold":   HoldGesture // optional
}
```
`Gesture` = `{ "steps": [Step...], "icon": "id", "color": "#rrggbb", "ms": 0 }`
(icon/color optional = keep idle visuals; shown for `ms` milliseconds, 0 = use `settings.revertMs`).

`HoldGesture` = `{ "start": [Step...], "end": [Step...], "icon": "id", "color": "#rrggbb",
"after": { "icon": "id", "color": "#rrggbb", "ms": 0 } }`
- `start` runs when the hold threshold is crossed, visuals `icon/color` are shown while the button is down.
- `end` runs on release. Afterwards any key still held by the key's steps is **automatically released**.
- `after` (optional) is shown after release for `ms` (0 = `revertMs`), then back to idle.

A key whose `hold` is absent treats a long press as an ordinary single press. `single` waits for a double press only if `double` is present.

### Step
```jsonc
{ "t": "key",   "mode": "tap" | "down" | "up", "mods": ["ctrl","shift","alt","gui"], "key": "k" }
{ "t": "text",  "text": "/clear\n" }            // ASCII, US layout; "\n" = Enter, "\t" = Tab
{ "t": "media", "key": "volup" }                // volup voldown mute playpause next prev stop
{ "t": "delay", "ms": 100 }                     // 0-5000
```
`key` names: `a`-`z`, `0`-`9`, `f1`-`f24`, `enter esc tab space backspace delete insert home end pageup pagedown up down left right printscreen scrolllock pause capslock menu`, and punctuation `- = [ ] \ ; ' , . / \``.
`mode:"down"` presses the modifiers + key and keeps them held until a matching `up` step (or automatic release, see above).
`key` may be empty for modifier-only steps (e.g. hold only `gui`).

## REST API (all JSON unless noted)

| Method | Path | Body | Result |
|---|---|---|---|
| GET | `/api/config` | | config.json |
| PUT | `/api/config` | config.json | `{ok:true}` or 400 `{ok:false,error}`; applies live |
| GET | `/api/status` | | `{fw, activePage, pageCount, heapFree, fsTotal, fsUsed, uptimeMs, ledCount:14}` |
| POST | `/api/page` | `{index}` | switch active page |
| POST | `/api/test` | `{steps:[Step...]}` | runs steps over USB now |
| GET | `/api/icons/<id>` | | raw 32768 byte RGB565 (big-endian per pixel, exactly as stored and uploaded) |
| POST | `/api/icons/<id>` | multipart/form-data, field `file` = 32768 bytes | `{ok:true}` (id: `[a-z0-9_-]{1,24}`) |
| DELETE | `/api/icons/<id>` | | `{ok:true}` |
| POST | `/api/leds/probe` | `{mask}` | light only those LEDs white (mask 0 = restore) |
| POST | `/api/factory-reset` | | wipes config + icons, reboots |

Export/import are composed in the browser (no firmware endpoints): export = `GET /api/config` + `GET /api/icons/<id>` per icon, bundled as `{config, icons:{id: base64}}`; import = `PUT /api/config` then `POST /api/icons/<id>` per icon.

Icon pixel format: 128x128, RGB565, row-major, **big-endian byte order per pixel** (high byte first) — the browser produces exactly what the panel expects; the device does not swap bytes.

Static UI is served from `/` (files in `data/www`). Unknown paths redirect to `/` (captive portal).
