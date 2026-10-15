# Updating the STM32 firmware (enables real LED colours)

Why: the factory STM32 firmware ignores LED hue (LilyGo issue
[T-Keyboard-S3-Pro#7](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3-Pro/issues/7)). LilyGo's V2 firmware fixes it.
The pad works fine without this; you only gain per-key colours and a real rainbow.

Risk: low. SWD programming can always reflash the chip, so a bad flash is recoverable with the same cable.
Do not use the UART route unless you have to (it needs BOOT0 handling).

## What you need
- **ST-Link V2** (genuine or clone) with SWD pins `SWDIO`, `SWCLK`, `GND` (a "3V3" pin is optional, see step 4).
- A cable from the pad's SWD connector to the ST-Link. The pad has two small 4-pin connectors on the **back of the
  keyboard PCB**; they look like 1.0 mm-pitch JST-SH (the same plug as Qwiic / STEMMA QT cables). Measure the
  pitch before buying anything. Cutting the end off a Qwiic cable and using Dupont crimps/solder works.
- A PC with **STM32CubeProgrammer** (free from st.com, needs an ST account) and the ST-Link driver it installs.
- The firmware file: `tools/stm32/stm32g030_0x01_V2_202501231722.hex`
  (sha256 `71d820b2d5c138e3440b83fea025c20061832b8621f58c91bb74ba5087c64bf7`).
  Official source (GPL-3.0, so it is not committed here): the `firmware` folder of
  https://github.com/Xinyuan-LilyGO/T-Keyboard-S3-Pro/tree/stm32cubeg0-firmware-package_V1.6.2
  — use the file with `STM32G030F6P6_Drive_0x01` in its name (the main board is address 0x01).

## Steps
1. **Unplug the pad** and open it far enough to see the back of the keyboard PCB. Do not pull the screen ribbon
   cables off; you only need access to the two 4-pin connectors.
2. **Find the SWD connector.** LilyGo's photo (back of the board) shows two connectors side by side:
   - left: **RX  TX  +3.3V  GND** (UART, not used)
   - right: **GND  +3.3V  SWCLK  SWDIO** (SWD, this one)

   Pin order is the silkscreen order as printed; confirm with a multimeter before connecting: GND should beep to the
   USB-C shell, +3.3V should read 3.3 V when the pad is powered.
3. **Wire it:** ST-Link `GND` → GND, `SWCLK` → SWCLK, `SWDIO` → SWDIO.
   Never connect any 5 V pin.
4. **Power:** power the pad from its USB-C port as usual and plug the ST-Link into the PC.
   Start without the 3.3V wire. Only if CubeProgrammer cannot connect or shows a target voltage of 0 V, add the
   ST-Link 3.3V (genuine V2: sense input; clones: it is an output, so try it only briefly).
5. Open **STM32CubeProgrammer** → right side: select **ST-LINK**, **Port: SWD**, **Mode: Normal**,
   **Reset mode: Software reset** → click **Connect**. You should see an STM32G030 device and a target voltage near 3.3 V.
   (LilyGo's own screenshot: https://github.com/Xinyuan-LilyGO/T-Keyboard-S3-Pro/blob/stm32cubeg0-firmware-package_V1.6.2/image/8.jpg)
6. **Optional but recommended: back up the current firmware.** *Device memory* tab, address `0x08000000`, size
   `0x8000`, **Read → Save as** `stm32_factory_backup.bin`. If the read is refused, the chip has read protection; that is
   fine, programming still works (it will mass-erase when protection is removed).
7. **Erasing & programming** tab (the download icon): *File path* → the `.hex`, tick **Verify programming** and
   **Run after programming**, click **Start Programming**. Wait for "File download complete".
8. **Disconnect** in CubeProgrammer, unplug USB and the ST-Link, remove the wires, reassemble, plug the pad back in.

## Verify and enable
1. Tell Claude "done". It runs the LED hue sweep against the pad. Every step should now show a different colour.
2. Enable real colours: either the web UI (Settings → *STM32 firmware is updated*) or the serial console command
   `ledhue 1`. This sets `settings.ledHue`; the firmware then sends real hues and the rainbow effect becomes a true rainbow.

## If something goes wrong
- *Cannot connect:* swap SWCLK/SWDIO, re-check GND, try **Mode: Hot plug**, add the 3.3V wire.
- *Pad dead after flashing:* reconnect the ST-Link and flash again; SWD is independent of the firmware.
- *Want the old firmware back:* flash `stm32_factory_backup.bin` at `0x08000000` from step 6.
