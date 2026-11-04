# Wiggle3DS

**English** · [Deutsch](README.de.md)

A retro photo and wiggle workstation for the New 3DS (XL). It takes a 3D photo with both outer cameras and turns it into a wigglegram GIF, a small camera move like the multi-lens Nimslo and Nishika cameras made. On top of that: plain photos, selfies, auto exposure, an automatic night mode and 81 filters, from classic film looks to datamosh and circuit bending.

## Install

Scan the code on your 3DS with [FBI](https://github.com/lifehackerhansol/FBI): **Remote Install → Scan QR Code**. It always points to the newest [release](https://github.com/tj537/Wiggle3DS/releases/latest); FBI downloads the CIA straight from GitHub and installs it.

<img src="docs/install-qr.svg" width="200" alt="QR code: newest Wiggle3DS.cia">

Or download `Wiggle3DS.cia` from the [latest release](https://github.com/tj537/Wiggle3DS/releases/latest), copy it to the SD card and install it in FBI. Your photos in `Wiggle3DS/` are kept across updates.

## What it does

- **Wigglegrams:** both outer cameras fire at the same moment, you tap your subject, and the app makes a wiggling GIF that turns around that point. Transitions: Hard cut, Smooth, Extra smooth.
- **Plain photos:** with the transition set to **Off**, a shot becomes a normal photo (JPG). Both camera images are kept, so in edit you can turn a photo into a wiggle again, and back.
- **81 filters in groups:** Classic, Color, RGB & Ghost, Tape & Screen, Data Mosh, Circuit Bending, Print & Pixel, Warp, Chaos (Deep Fried, Disco, Slit Scan, Kaleido, Droste, Shatter, Toxic, Hellfire, X-Ray …). Every filter shows a live preview of your viewfinder in the menu; X marks favorites, which have their own tab (All / ★ Favs, or SELECT). "Original" at the top goes back to no filter from anywhere in the list. The glitch filters change the image itself (RGB split, VHS, datamosh, pixel sort, melt, keyframe …), and many of them move. In the GIF that is two wiggle loops with patterns of their own, exactly what the preview in edit shows.
- **Filter strength:** every filter from 0 to 100 %, with L and R in steps of 10 %.
- **Auto exposure and white balance:** every shot gets one tone curve and white balance for both eyes: the midtones lifted toward middle gray, a soft shoulder so lamps and sky don't clip, colors pulled most of the way to neutral (only a nudge when nothing in the picture is gray, like a lawn under a blue sky).
- **Night mode, automatic:** when the viewfinder stays dark, the camera switches to long exposures by itself (a moon shows at the top left) and each shot averages four frames into one for much less noise. Parts that move keep the first frame, so nothing ghosts. It turns off again once there is light to spare; "Auto night" in the settings switches it off for good.
- **Selfie:** the inner camera, mirrored, for plain photos.
- **Clean 3D:** both sensors are synced, and the right eye's brightness and color are matched to the left, so the GIF doesn't flicker.
- **Gallery:** three rows that scroll sideways, hold to select several, delete with undo.
- **To your phone:** scan a QR code and download GIFs and photos over Wi-Fi.
- **Fast:** GIFs and JPGs are made in the background; the camera is ready again right away.

## Controls

### Home

The viewfinder is on top; below are your capture defaults and the newest shots.

- **A, L, R, ZL or ZR:** take a shot
- **Y:** filter menu. Touching a filter previews it in the viewfinder, touching it again or A applies it, L R sets the strength, X makes it a favorite.
- **B or "All":** gallery
- **▶:** to the newest shots; choose with ◀ ▶, open with A
- **▼:** reset the capture defaults (filter, strength, transition, speed, quick capture, selfie). Also the button at the top left, and "Capture defaults" in the settings.
- **SELECT:** share to your phone
- **START:** quit

The buttons at the top, from the left: reset to defaults, selfie, share, settings. The LED on the left shows the battery: green, orange or red, with a ring while charging; a moon next to it while auto night is on.

The tiles below are the defaults for new shots: filter, transition (Off means photo), speed and quick capture. With quick capture every shot is saved right away, without the edit screen.

### Edit

- **Tap your subject:** sets the pivot; the first time, a hint explains it.
- **◀ ▶ or the slider:** speed
- **▲ ▼:** filter, **L R:** filter strength
- **X:** transition, including Off, which makes it a photo
- **Hold Y:** shows the original without the filter
- **A:** save, **B:** back (it asks first if something would be lost)

The original is always kept unfiltered, so you can change everything later.

### Gallery

- **Swipe:** scrolls with momentum
- **D-pad:** chooses a shot, **L R:** jumps 4 columns
- **Tap:** chooses a shot, **tap again or A:** opens it
- **Y:** play or pause
- **X:** delete, then **Y** for a few seconds to undo
- **Hold a shot:** starts selecting several; tap more, then delete them with X

### Settings

Color theme, self-timer, grid lines, shutter sound, flash effect, viewfinder fps, which camera takes photos (right by default), gallery autoplay, GIF size (full or half) and GIF shape (4:3 or 1:1), and resetting the capture defaults. The top line shows the version, the number of shots and the free space. Everything is saved in `Wiggle3DS/settings.txt`.

### To your phone

SELECT or Share shows a QR code on top. Scan it with your phone on the same Wi-Fi and a page with all GIFs and photos opens. Hold a picture and choose "Add to Photos", or tap "Save". The web server (port 8080) only runs while this screen is open.

## Files on the SD card

- `Wiggle3DS/GIF/WIG_0001.gif`: a finished wigglegram, it loops forever
- `Wiggle3DS/GIF/WIG_0001.jpg`: a photo (transition Off)
- `Wiggle3DS/Photos/WIG_0001.w3d`: both camera images with all settings, for the gallery and to edit again
- `Wiggle3DS/settings.txt`: the settings

## Building

You need [devkitPro](https://devkitpro.org) with the `3ds-dev` package.

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
make
```

The path to the project folder must not contain spaces, or `make` fails.

## Testing with the Homebrew Launcher

Open the Homebrew Launcher on the 3DS and press **Y**. Then on the computer:

```bash
/opt/devkitpro/tools/bin/3dslink Wiggle3DS.3dsx
```

If the 3DS isn't found, add the IP address it shows with `-a <IP>`. For everyday use the CIA below is better.

## As a CIA in the HOME Menu

Install CIAs with [FBI](https://github.com/lifehackerhansol/FBI). Wiggle3DS then starts from the HOME Menu, with its own icon, banner and sound.

You also need `makerom` ([Project_CTR](https://github.com/3DSGuy/Project_CTR/releases)) and `bannertool` ([3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool)) in your `PATH`, for example in `~/.local/bin`. Then:

```bash
make cia
```

This makes `Wiggle3DS.cia`. The title ID is fixed (unique ID `0xF57E3`, product code `CTR-H-WGLS`), so a new CIA replaces the installed version instead of adding a second icon. For a new version, raise `APP_VERSION` in the `Makefile` (and in `source/app.h`).

Close Wiggle3DS before installing. Your photos and GIFs in `Wiggle3DS/` on the SD card stay as they are, also when replacing or uninstalling.

### Over Wi-Fi

```bash
make send-cia
```

The computer builds the CIA, serves it on the local network (port 8080) and shows a QR code with its address in the terminal. On the 3DS, open FBI, choose **Remote Install → Scan QR Code** and point the camera at it. Both need to be on the same Wi-Fi. The server stops once the 3DS has the file.

If the QR code in the terminal is too small, `python3 tools/send-cia.py Wiggle3DS.cia --open` opens it large in the browser.

Without a QR code: in FBI choose **Remote Install → Receive URLs over the network**; FBI shows the 3DS's IP. Then:

```bash
make send-cia IP=192.168.x.y
```

### From the SD card

Copy `Wiggle3DS.cia` to the SD card, for example to `cias/`. In FBI open **SD → cias**, choose the file and **Install and delete CIA** (or **Install CIA**).

### Releasing

Build with `make cia` and attach `Wiggle3DS.cia` to a new GitHub release. The QR code above needs no change: it always points to the latest release. `make qr` writes it again (`docs/install-qr.svg`), should the repository ever move.

## License

MIT, see [LICENSE](LICENSE). The parts by others have their own licenses (below).

## Code by others

- `source/qrcodegen.c/.h`: [QR Code generator](https://github.com/nayuki/QR-Code-generator) by Project Nayuki (MIT license)
- `fonts/`: [Inter](https://github.com/rsms/inter) (SIL Open Font License)

## Tools

- `tools/genfont.swift` makes `source/font_data.c` from the Inter fonts in `fonts/`:

  ```bash
  swift tools/genfont.swift source/font_data.c fontSmall:fonts/Inter-Medium.ttf:10.5 fontBody:fonts/Inter-SemiBold.ttf:12.5 fontTitle:fonts/Inter-Bold.ttf:19
  ```

- `tools/genmeta.swift` makes the icon (48x48), banner (256x128) and banner sound in `meta/`:

  ```bash
  swift tools/genmeta.swift meta
  ```

- `tools/send-cia.py` serves the CIA on the local network (see above); `tools/qrtext.c` makes its QR code with the same qrcodegen as the app. `tools/release-qr.py` writes the install QR code.

- `tools/preview.c` renders every screen on the computer as BMP files, to check the interface without a 3DS (input: a raw 640x480 RGB565 stereo pair):

  ```bash
  clang -O2 -I/opt/devkitpro/libctru/include -o preview tools/preview.c source/ui.c source/draw.c source/font_data.c source/filter.c source/qrcodegen.c source/wiggle.c -lm
  ./preview pair.raw out/
  ```
