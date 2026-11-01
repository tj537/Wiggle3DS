# Wiggle3DS

Homebrew-App für den New 3DS (XL): Sie nimmt ein 3D-Foto mit beiden Außenkameras auf und macht daraus ein Wigglegram-GIF, eine kleine Kamerafahrt wie bei den Nimslo- und Nishika-Kameras mit mehreren Linsen.

## Plan

1. 2-Bild-Wiggle: Den Drehpunkt per Touch wählen, dann als GIF auf die SD-Karte exportieren.
2. Zwischenbilder aus einer Tiefenschätzung berechnen, damit die Kamerafahrt flüssig wird.
3. Icon, Banner und CIA für das HOME-Menü.

Aktueller Stand: Schritt 1 ist fertig. Die App nimmt in 640x480 auf, jedes GIF bekommt eine eigene Palette, die aus seinen Farben berechnet wird, und auf dem Startscreen ist die Galerie.

## Bedienung

Die App ist auf Englisch.

- **Startscreen:** Oben läuft die Kamera, unten ist immer die Galerie. A, L oder R macht ein Foto.
- **Durchblättern:** Mit dem Steuerkreuz oder durch Antippen eines Bildes wählst du eine Aufnahme. Sie erscheint oben als Standbild, Y spielt sie ab. A oder ein zweites Antippen öffnet sie zum Bearbeiten, X löscht sie nach einer Rückfrage, B bringt dich zurück zur Kamera.
- **Bearbeiten:** Tipp auf dein Motiv, um den Drehpunkt zu setzen. Mit L/R oder ◀/▶ justierst du fein, das Tempo stellst du mit dem Regler oder ▲/▼ ein. A speichert, B geht zurück.
- **3D-Regler:** Oben siehst du die Aufnahme dann in echtem 3D.

Akku: Die App zeichnet nur neu, wenn sich etwas ändert. Die Kamera läuft mit 20 fps und ist beim Durchblättern aus, und den schnelleren Takt des New 3DS nutzt die App nur beim Bearbeiten und Speichern.

## Dateien auf der SD-Karte

- `Wiggle3DS/GIF/WIG_0001.gif`: die fertigen Wigglegrams
- `Wiggle3DS/Photos/WIG_0001.w3d`: das 3D-Foto mit seinen Einstellungen, damit du es später in der Galerie neu bearbeiten kannst

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

## Werkzeuge

- `tools/genfont.swift` erzeugt `source/font_data.c` aus den Inter-Schriften in `fonts/` (SIL Open Font License):

  ```bash
  swift tools/genfont.swift source/font_data.c fontSmall:fonts/Inter-Medium.ttf:10.5 fontBody:fonts/Inter-SemiBold.ttf:12.5 fontTitle:fonts/Inter-Bold.ttf:19
  ```

- `tools/preview.c` rendert alle Screens am Mac als BMP, damit du die Oberfläche ohne 3DS prüfen kannst (Eingabe: ein rohes RGB565-Stereopaar mit 640x480):

  ```bash
  clang -O2 -I/opt/devkitpro/libctru/include -o preview tools/preview.c source/ui.c source/draw.c source/font_data.c source/wiggle.c -lm
  ./preview paar.raw ausgabe/
  ```
