# Wiggle3DS

Homebrew-App für den New 3DS (XL): Sie nimmt ein 3D-Foto mit beiden Außenkameras auf und macht daraus ein Wigglegram-GIF, eine kleine Kamerafahrt wie bei den Nimslo- und Nishika-Kameras mit mehreren Linsen.

## Plan

1. 2-Bild-Wiggle: Den Drehpunkt per Touch wählen, dann als GIF auf die SD-Karte exportieren.
2. Zwischenbilder aus einer Tiefenschätzung berechnen, damit die Kamerafahrt flüssig wird.
3. Filter, Icon und Banner.

Aktueller Stand: Toolchain-Test (Text auf dem oberen Screen und Anzeige des 3D-Reglers).

## Bauen

Voraussetzung ist [devkitPro](https://devkitpro.org) mit dem Paket `3ds-dev`.

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
make
```

Wichtig: Im Pfad zum Projektordner dürfen keine Leerzeichen sein, sonst bricht `make` ab.

## Auf den 3DS schicken

Öffne auf dem 3DS den Homebrew Launcher und drück **Y**. Dann am Mac:

```bash
/opt/devkitpro/tools/bin/3dslink Wiggle3DS.3dsx
```

Falls der 3DS nicht automatisch gefunden wird, häng die angezeigte IP-Adresse mit `-a <IP>` an.
