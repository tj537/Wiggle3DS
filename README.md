# Wiggle3DS

Retro-Foto- und Wiggle-Workstation für den New 3DS (XL). Sie nimmt mit beiden Außenkameras ein 3D-Foto auf und macht daraus ein Wigglegram-GIF, eine kleine Kamerafahrt wie bei den Nimslo- und Nishika-Kameras mit mehreren Linsen. Dazu kommen normale Fotos, Selfies, ein Nachtmodus und 50 Filter von Classic bis Datamosh.

## Was sie kann

- **Wigglegrams:** Beide Außenkameras lösen gleichzeitig aus, du tippst auf dein Motiv, und die App macht ein wackelndes GIF, das sich um diesen Punkt dreht. Übergänge: Hard cut, Smooth, Extra smooth.
- **Normale Fotos:** Steht die Transition auf **Off**, wird die Aufnahme ein normales Foto (JPG). Beide Kamerabilder bleiben gespeichert, beim Bearbeiten machst du aus einem Foto jederzeit wieder ein Wiggle und umgekehrt.
- **50 Filter in Gruppen:** Classic, Color, RGB & Ghost, Tape & Screen, Data Mosh, Bits & Pixels. Die Glitch-Filter verändern das Bild selbst (RGB Split, VHS, Datamosh, Pixel Sort, Melt, Keyframe …), viele davon bewegen sich, im Sucher genauso wie im GIF.
- **Nachtmodus:** längere Belichtung, und vier Bilder werden zu einem gemittelt, das rauscht deutlich weniger. Bewegte Stellen bleiben vom ersten Bild, so gibt es keine Geisterbilder.
- **Selfie:** die Innenkamera, gespiegelt, für normale Fotos.
- **Sauberes 3D:** Beide Sensoren sind synchronisiert, Helligkeit und Farbe des rechten Auges werden ans linke angeglichen, damit das GIF nicht flackert.
- **Galerie:** drei Reihen, die seitwärts scrollen, Mehrfachauswahl per Gedrückthalten, Löschen mit Rückgängig.
- **Aufs Handy:** QR-Code scannen, dann lädst du GIFs und Fotos im WLAN direkt aufs Handy.
- **Schnell:** GIFs und JPGs entstehen im Hintergrund, die Kamera ist sofort wieder bereit.

## Bedienung

Die App ist auf Englisch.

### Startscreen

Oben läuft der Sucher, unten sind deine Standards und die letzten Bilder.

- **A, L, R, ZL oder ZR:** auslösen
- **Y:** Filterauswahl. Antippen zeigt den Filter sofort im Sucher, nochmal Antippen oder A übernimmt ihn.
- **X:** Nachtmodus an oder aus
- **B oder „All“:** Galerie
- **SELECT:** aufs Handy teilen
- **START:** beenden

Die Buttons oben, von links: Selfie, Nachtmodus, Share, Einstellungen. Die LED links zeigt den Akku: grün, orange oder rot, mit Ring beim Laden.

Die Kacheln darunter sind die Standards für neue Aufnahmen: Filter, Transition (Off heißt Foto), Speed und Quick capture. Mit Quick capture wird jede Aufnahme sofort gespeichert, ohne vorher den Bearbeiten-Screen zu öffnen.

### Bearbeiten

- **Tippen aufs Motiv:** setzt den Drehpunkt, beim ersten Mal erklärt ein Hinweis das.
- **◀ ▶ oder der Regler:** Speed
- **▲ ▼:** Filter
- **X:** Transition, auch auf Off, dann wird es ein Foto
- **Y halten:** zeigt das Original ohne Filter
- **A:** speichern, **B:** zurück

Das Original bleibt immer ungefiltert gespeichert, du kannst also alles später ändern.

### Galerie

- **Wischen:** scrollt mit Schwung
- **Steuerkreuz:** wählt ein Bild, **L R:** springt 4 Spalten
- **Antippen:** wählt ein Bild, **nochmal Antippen oder A:** öffnet es
- **Y:** abspielen oder pausieren
- **X:** löschen, danach ein paar Sekunden lang **Y** für Rückgängig
- **Bild gedrückt halten:** startet die Mehrfachauswahl, dann tippst du weitere an und löschst sie mit X

### Einstellungen

Farbschema, Selbstauslöser, Gitterlinien, Auslöseton, Blitz-Effekt, Sucher-fps, welche Kamera für Fotos (Standard rechts), Galerie-Autoplay, GIF-Größe (voll oder halb) und GIF-Form (4:3 oder 1:1). Oben stehen die Version, die Anzahl der Aufnahmen und der freie Speicher. Alles landet in `Wiggle3DS/settings.txt`.

### Aufs Handy

SELECT oder Share zeigt oben einen QR-Code. Scannst du ihn mit dem Handy im selben WLAN, öffnet sich eine Seite mit allen GIFs und Fotos. Dort hältst du ein Bild gedrückt und wählst „Zu Fotos hinzufügen“, oder du tippst auf „Save“. Der Webserver (Port 8080) läuft nur, solange dieser Screen offen ist.

## Dateien auf der SD-Karte

- `Wiggle3DS/GIF/WIG_0001.gif`: ein fertiges Wigglegram, es läuft endlos
- `Wiggle3DS/GIF/WIG_0001.jpg`: ein Foto (Transition Off)
- `Wiggle3DS/Photos/WIG_0001.w3d`: beide Kamerabilder mit allen Einstellungen, für die Galerie und zum neu Bearbeiten
- `Wiggle3DS/settings.txt`: die Einstellungen

## Bauen

Voraussetzung ist [devkitPro](https://devkitpro.org) mit dem Paket `3ds-dev`.

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
make
```

Wichtig: Im Pfad zum Projektordner dürfen keine Leerzeichen sein, sonst bricht `make` ab.

## Zum Testen über den Homebrew Launcher

Öffne auf dem 3DS den Homebrew Launcher und drück **Y**. Dann am Mac:

```bash
/opt/devkitpro/tools/bin/3dslink Wiggle3DS.3dsx
```

Falls der 3DS nicht automatisch gefunden wird, häng die angezeigte IP-Adresse mit `-a <IP>` an. Für den Alltag ist die CIA unten besser.

## Als CIA ins HOME-Menü

Fertige Versionen installierst du als CIA mit [FBI](https://github.com/lifehackerhansol/FBI). Dann startet Wiggle3DS direkt aus dem HOME-Menü, mit eigenem Icon, Banner und Ton.

Zusätzlich brauchst du `makerom` ([Project_CTR](https://github.com/3DSGuy/Project_CTR/releases)) und `bannertool` ([3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool)) im `PATH`, zum Beispiel in `~/.local/bin`. Dann:

```bash
make cia
```

Das erzeugt `Wiggle3DS.cia`. Die Title-ID ist fest (Unique ID `0xF57E3`, Product Code `CTR-H-WGLS`). Eine neue CIA ersetzt also die installierte Version, statt ein zweites Icon anzulegen. Für eine neue Version erhöhst du `APP_VERSION` im `Makefile`.

Vor dem Installieren musst du Wiggle3DS schließen. Deine Fotos und GIFs in `Wiggle3DS/` auf der SD-Karte bleiben dabei unverändert, auch beim Ersetzen oder Deinstallieren.

### Über WLAN

```bash
make send-cia
```

Der Mac baut die CIA, stellt sie im WLAN bereit (Port 8080) und zeigt im Terminal einen QR-Code mit der Adresse. Auf dem 3DS öffnest du FBI, wählst **Remote Install → Scan QR Code** und hältst die Kamera auf den QR-Code. Mac und 3DS müssen im selben WLAN sein. Der Server beendet sich, sobald der 3DS die Datei geladen hat.

Wenn der QR-Code im Terminal zu klein ist, öffnet `python3 tools/send-cia.py Wiggle3DS.cia --open` ihn groß im Browser.

Ohne QR-Code: Wähl in FBI **Remote Install → Receive URLs over the network**. FBI zeigt dann die IP des 3DS an. Am Mac:

```bash
make send-cia IP=192.168.x.y
```

### Über die SD-Karte

Kopier `Wiggle3DS.cia` auf die SD-Karte, zum Beispiel nach `cias/`. In FBI öffnest du **SD → cias**, wählst die Datei und dann **Install and delete CIA** (oder **Install CIA**).

## Fremder Code

- `source/qrcodegen.c/.h`: [QR Code generator](https://github.com/nayuki/QR-Code-generator) von Project Nayuki (MIT-Lizenz)
- `fonts/`: [Inter](https://github.com/rsms/inter) (SIL Open Font License)

## Werkzeuge

- `tools/genfont.swift` erzeugt `source/font_data.c` aus den Inter-Schriften in `fonts/` (SIL Open Font License):

  ```bash
  swift tools/genfont.swift source/font_data.c fontSmall:fonts/Inter-Medium.ttf:10.5 fontBody:fonts/Inter-SemiBold.ttf:12.5 fontTitle:fonts/Inter-Bold.ttf:19
  ```

- `tools/genmeta.swift` erzeugt Icon (48x48), Banner (256x128) und Banner-Ton in `meta/` aus den Inter-Schriften:

  ```bash
  swift tools/genmeta.swift meta
  ```

- `tools/send-cia.py` stellt die CIA im WLAN bereit (siehe oben). Den QR-Code baut `tools/qrtext.c` mit demselben qrcodegen wie die App.

- `tools/preview.c` rendert alle Screens am Mac als BMP, damit du die Oberfläche ohne 3DS prüfen kannst (Eingabe: ein rohes RGB565-Stereopaar mit 640x480):

  ```bash
  clang -O2 -I/opt/devkitpro/libctru/include -o preview tools/preview.c source/ui.c source/draw.c source/font_data.c source/filter.c source/qrcodegen.c source/wiggle.c -lm
  ./preview paar.raw ausgabe/
  ```
