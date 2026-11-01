# Wiggle3DS

Homebrew-App für den New 3DS (XL): Sie nimmt ein 3D-Foto mit beiden Außenkameras auf und macht daraus ein Wigglegram-GIF, eine kleine Kamerafahrt wie bei den Nimslo- und Nishika-Kameras mit mehreren Linsen.

## Plan

1. 2-Bild-Wiggle: Den Drehpunkt per Touch wählen, dann als GIF auf die SD-Karte exportieren.
2. Zwischenbilder aus einer Tiefenschätzung berechnen, damit die Kamerafahrt flüssig wird.
3. Filter, Icon und Banner.

Aktueller Stand: Schritt 1 ist fertig. Die App nimmt in 640x480 auf, und jedes GIF bekommt eine eigene Palette, die aus seinen Farben berechnet wird. Dazu gibt es eine Galerie.

## Bedienung

- **Kamera:** A, L oder R macht ein Foto, Y öffnet die Galerie, START beendet die App.
- **Bearbeiten:** Tipp auf den Drehpunkt, mit ◀/▶ justierst du fein, mit ▲/▼ stellst du das Tempo ein. A speichert, B geht zurück.
- **Galerie:** Mit dem Steuerkreuz oder per Touch wählst du eine Aufnahme aus. A öffnet sie, zweimal X löscht sie, B bringt dich zur Kamera.

## Dateien auf der SD-Karte

- `Wiggle3DS/GIF/WIG_0001.gif`: die fertigen Wigglegrams
- `Wiggle3DS/Fotos/WIG_0001.w3d`: das 3D-Foto mit seinen Einstellungen, damit du es später in der Galerie neu bearbeiten kannst

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
