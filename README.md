# ankerfix

A small macOS command-line tool for controlling the Anker PowerConf C300 webcam directly over USB, without AnkerWork.

It exists because AnkerWork keeps zoom and Solo-Frame apart. Solo-Frame is a preset, and zoom lives on a different tab. The camera firmware accepts both at once, so you can have a tight zoom and face tracking together.

Not affiliated with or endorsed by Anker.

## Build

Needs the Xcode command-line tools. No other dependencies, and no root.

```
clang -O2 -o uvczoom uvczoom.c -framework IOKit -framework CoreFoundation
```

## Usage

```
./uvczoom                  show frame mode, zoom, pan and tilt
./uvczoom solo 44          turn on Solo-Frame, then set zoom to 44
./uvczoom solo             turn on Solo-Frame, leave zoom alone
./uvczoom set 44           set zoom only
./uvczoom fixed 78         fixed field of view: 78, 90 or 115
./uvczoom pan 5            pan in degrees, -10 to 10
./uvczoom tilt -5          tilt in degrees, -10 to 10
./uvczoom xu               dump the vendor extension unit controls, read-only
```

Zoom runs from 10 to 70 in steps of 2. Higher is tighter.

Changing the frame mode changes the zoom value in firmware, so set the mode first and the zoom second. `solo N` does this in the right order. If you pick a mode in AnkerWork, run `./uvczoom set N` afterwards.

## Protocol

USB ID `291A:3361`, video control interface 2.

| Control | Unit | Selector | Format |
| --- | --- | --- | --- |
| Zoom | camera terminal 1 | `CT_ZOOM_ABSOLUTE` (0x0B) | 16-bit, 10 to 70, step 2 |
| Pan and tilt | camera terminal 1 | `CT_PANTILT_ABSOLUTE` (0x0D) | two signed 32-bit values in arcseconds, ±36000, step 3600 |
| Frame mode | extension unit 6 | 16 | 60 bytes, starting `mode, 1, fov` |

Frame mode payloads, as sent by AnkerWork:

| Mode | Bytes |
| --- | --- |
| Fixed 78° | `00 01 4e` |
| Fixed 90° | `00 01 5a` |
| Fixed 115° | `00 01 73` (by pattern, not seen in logs) |
| Auto-Frame | `01 01 73` (not exposed by this tool) |
| Solo-Frame | `02 01 73` |

## Status

Tested on one camera, firmware 1.68, with AnkerWork 3.0.4 installed and running, on an Apple Silicon Mac.

- Zoom during Solo-Frame works: the picture changes and tracking continues.
- The camera accepts and holds pan and tilt during Solo-Frame, but whether they move the tracked framing is not confirmed.
- Whether settings survive unplugging the camera or sleeping the Mac is not tested.
