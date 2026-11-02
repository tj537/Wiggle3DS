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
- **Filter:** Der Filter-Button oben in der Galerie (oder Y) öffnet die Filterauswahl. Antippen oder das Steuerkreuz zeigt den Filter sofort als Vorschau im Sucher, A oder ein zweites Antippen übernimmt ihn, B bricht ab. Der Filter wirkt live im Sucher und gilt für neue Fotos. Zur Auswahl stehen Original, Vivid, Warm, Cool, Dream, Cinema, Vintage, Lomo, Sepia, Mono und Noir.
- **Durchblättern:** Mit dem Steuerkreuz oder durch Antippen eines Bildes wählst du eine Aufnahme. Sie erscheint oben als Standbild, Y spielt sie ab. A oder ein zweites Antippen öffnet sie zum Bearbeiten, X löscht sie nach einer Rückfrage, B bringt dich zurück zur Kamera.
- **Bearbeiten:** Tipp auf dein Motiv, um den Drehpunkt zu setzen, mit L/R justierst du fein. Das Tempo stellst du mit ◀/▶ oder dem Regler ein, den Filter mit ▲/▼ oder durch Antippen der Filteranzeige auf dem Foto. X (oder Antippen) schaltet die Übergänge um: "Hard cut" springt hart zwischen den Augen hin und her (2 Frames), "Smooth" blendet dazwischen über (4 Frames, wie bei 3ds-mpo-gif), "Extra smooth" nutzt zwei Zwischenstufen (6 Frames). Standard ist Hard cut, das lässt sich in den Einstellungen ändern. Mehr Frames machen das GIF größer. A speichert das GIF mit Filter, B geht zurück. Das Originalfoto bleibt ungefiltert gespeichert, du kannst den Filter also jederzeit ändern.
- **3D-Regler:** Oben siehst du die Aufnahme dann in echtem 3D.
- **Einstellungen (Zahnrad oben):** Hier wählst du das Farbschema (Purple, Neon, Ocean, Mint, Sunset, Mono), den Standard-Übergang und das Standard-Tempo für neue Fotos, die GIF-Größe (voll oder halb für Messenger), die Bildrate des Suchers (15/20/30 fps, weniger spart Akku) und den Blitz-Effekt beim Auslösen. Die Einstellungen landen in `Wiggle3DS/settings.txt`.
- **Aufs Handy (SELECT oder "Share" oben rechts):** Oben erscheint ein QR-Code. Scannst du ihn mit der Handy-Kamera (Handy im selben WLAN), öffnet sich eine Seite mit allen GIFs. Dort hältst du ein GIF gedrückt und wählst "Zu Fotos hinzufügen", oder du tippst auf "Save". Der kleine Webserver (Port 8080) läuft nur, solange dieser Screen offen ist.

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

## Fremder Code

- `source/qrcodegen.c/.h`: [QR Code generator](https://github.com/nayuki/QR-Code-generator) von Project Nayuki (MIT-Lizenz)
- `fonts/`: [Inter](https://github.com/rsms/inter) (SIL Open Font License)

## Werkzeuge

- `tools/genfont.swift` erzeugt `source/font_data.c` aus den Inter-Schriften in `fonts/` (SIL Open Font License):

  ```bash
  swift tools/genfont.swift source/font_data.c fontSmall:fonts/Inter-Medium.ttf:10.5 fontBody:fonts/Inter-SemiBold.ttf:12.5 fontTitle:fonts/Inter-Bold.ttf:19
  ```

- `tools/preview.c` rendert alle Screens am Mac als BMP, damit du die Oberfläche ohne 3DS prüfen kannst (Eingabe: ein rohes RGB565-Stereopaar mit 640x480):

  ```bash
  clang -O2 -I/opt/devkitpro/libctru/include -o preview tools/preview.c source/ui.c source/draw.c source/font_data.c source/wiggle.c source/qrcodegen.c source/filter.c -lm
  ./preview paar.raw ausgabe/
  ```
