# Anleitung zu fastchess-gui

Diese Anleitung beschreibt jede Seite, jede Einstellung und jede Datei von fastchess-gui. Das [README](../README.md)
sagt, was das Programm ist, wo man es herunterlädt und wie man es baut. Im Programm öffnet F1 diese Anleitung neben
der Seite, beim Abschnitt zu dem, was unter der Maus liegt, in der Sprache der Oberfläche (siehe
[Hilfe im Programm](#hilfe-im-programm)).

## Schnellstart
<!-- en: quick-start 92353f2c -->

### Dein erstes Turnier
<!-- en: your-first-tournament 75e9a891 -->

1. **Engines.** In der Leiste links *Engines*, dann *Hinzufügen…*, und wähle das Programm einer Engine (eine
   ausführbare UCI- oder WinBoard-Datei): Es wird einmal gestartet, um seinen Namen und seine Optionen zu lesen.
   *Importieren…* > *Eine Engine-Liste…* übernimmt die Engines eines anderen Programms (cutechess, Banksia, Arena,
   ChessGUI), *Importieren…* > *Alle Engines eines Ordners…* alle Programme eines Ordners. *Prüfen* sagt dir, ob eine
   Engine funktioniert.
2. **Neues Turnier.** *Turniere* > *Neues Turnier*. Wähle ein *Format* aus seiner Liste (*Rundenturnier*,
   *Gauntlet*…) und eine *Bedenkzeit* (*10+0.1* für einen ersten Versuch), und lass die anderen Zeilen, wie sie sind.
3. **Teilnehmer.** Doppelklicke Engines in der *Bibliothek* neben den Teilnehmern, oder wähle mehrere aus und drücke
   Enter.
4. **Start.** Das Turnier öffnet sich in seinem Arbeitsbereich: *Übersicht* für die Tabelle und die verbleibende
   Zeit, *Live* für die laufenden Partien.

Die Zusammenfassung rechts in der Einrichtung sagt, wie viele Partien gespielt werden und wie lange sie dauern, und was
schiefgehen würde (ein Programm, das nicht gefunden wird, zu viele Threads, ein fehlendes Buch), jeweils mit einem
Knopf, der es behebt.

### Eine neue Version einer Engine testen
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Füge beide Versionen zur Bibliothek hinzu.
2. *Neues Turnier*, *Format*: *SPRT Verbesserung [0, 3]* (bringt die neue Version etwas?) oder
   *SPRT keine Verschlechterung [-5, 0]* (ist sie nicht schwächer?).
3. Wähle die neue Version als **A** und die alte als **B**, eine kurze Bedenkzeit (*10+0.1*) und unter *Eröffnungen*
   ein Buch mit ausgeglichenen Stellungen (EPD), dann **Start**.

Das Match endet von selbst, sobald der SPRT entscheidet: H1 angenommen (A ist stärker, gemessen an den Grenzen) oder
H0 angenommen (es ist nicht stärker). Die *Übersicht* zeigt die Elo, die LLR und ihre Grenzen, während die Partien
eintreffen. Die Einrichtung bleibt erhalten: Wähle beim nächsten Mal die nächste Version als A und **Start**, oder
klicke beim beendeten Match auf *Neu starten mit…*.

### Wie es weitergeht
<!-- en: where-to-go-next 2cd9b9f7 -->

- [Neues Turnier](#neues-turnier): jede Einstellung, und [Tipps](#tipps-für-die-einstellungen), wie du sie wählst;
- [Ein Turnier durchführen](#ein-turnier-durchführen): was die Tabs zeigen, Pausieren, ein Turnier ändern, während es
  läuft;
- [Elo-Listen](#elo-listen): eine Rangliste über alle gespielten Partien;
- [Übertragung](#übertragung): die Partien live auf einer Webseite, in TLCV oder auf lichess;
- [Fehlerbehebung](#fehlerbehebung), wenn etwas schiefgeht, und das [Glossar](#glossar) für die Begriffe der Tester.

## Das Fenster
<!-- en: the-window 3a5230d4 -->

### Die Seiten
<!-- en: the-pages b13b4de2 -->

Eine Navigationsleiste links führt zu acht Seiten:

| Seite | Inhalt |
|---|---|
| Turniere | Die Startseite: was läuft und was zu erledigen ist, die Warteschlange, die zuletzt beendeten Turniere, alle Turniere des Profils, *Neues Turnier* |
| Engines | Die Engine-Bibliothek: Optionen, Prüfungen, Gruppen, Referenz-Elo, Import der Listen anderer Programme |
| Bücher | Die Bibliothek der Eröffnungsbücher, und die Eröffnungen jedes Buchs mit dem, was an ihnen nicht stimmt |
| Brett | Eine Partie oder eine Stellung von irgendwoher, analysiert von einer Engine der Bibliothek |
| Suiten | Testsuiten (STS, WAC, ERET…): Engines gegen die Stellungen einer EPD-Datei |
| Elo-Listen | Eine Rangliste über alle Partien aller Turniere |
| Protokoll | Die eigene Ausgabe von fastchess und die Meldungen des Programms, und *Problem melden…* |
| Einstellungen | Sprache, Design, Größe der Oberfläche, Benachrichtigungen, das Aussehen der Bretter, der Profil- und der Turnierordner, die entfernten Maschinen, das lichess-Token, die Version, die Art, wie das Fenster gezeichnet wird |

### Hilfe im Programm
<!-- en: help-in-the-program 351be20d -->

- **Tooltips.** Lass die Maus auf einem Bedienelement ruhen, um zu sehen, was es tut. Der Tooltip eines Elements, das
  diese Anleitung weiter erklärt, endet mit *F1: mehr in der Anleitung*.
- **F1** öffnet diese Anleitung in einem Bereich rechts der Seite, beim Abschnitt zu dem, was unter der Maus liegt
  (eine Zeile der Einrichtung, eine Karte der Startseite, ein Tab, eine Spalte der Tabelle); liegt nichts unter der
  Maus, beim Abschnitt des zuletzt angeklickten Bereichs, sonst der Seite. Das runde **?** neben dem Titel einer Seite
  öffnet den Abschnitt der Seite. F1 noch einmal oder Esc im Bereich schließt ihn.
- **Der Anleitungsbereich** zeigt einen Abschnitt mit seinen Unterabschnitten, und die Seite daneben bleibt bedienbar:
  Du kannst die Erklärung lesen, während du die Einstellung änderst. Sein Kopf hat *Zurück* und *Vorwärts* (auch
  Alt+Links und Alt+Rechts oder die Seitentasten der Maus), *Inhalt* (alle Abschnitte), das Suchfeld (Strg+F im
  Bereich) und *Im Browser öffnen* (diese Anleitung auf GitHub, für die laufende Version). Die Suche findet jedes
  eingetippte Wort, in beliebiger Reihenfolge, unabhängig von Groß-/Kleinschreibung und Akzenten; Titel kommen zuerst,
  und die Treffer werden hervorgehoben. Links zu anderen Abschnitten öffnen sich im Bereich, Links zu Webseiten im
  Browser, und ein Codeblock hat *Kopieren*. Zieh am linken Rand des Bereichs, um ihn zu verbreitern; seine Breite
  bleibt erhalten.
- **Sprachen.** Die Anleitung ist in das Programm eingebaut, in jeder Sprache der Oberfläche, und passt zur laufenden
  Version; nichts wird aus dem Netz geholt. Ein Abschnitt, dessen Übersetzung fehlt oder älter ist als der englische
  Text, wird auf Englisch gezeigt, mit einer Zeile, die darauf hinweist. Die Release-Archive enthalten sie auch
  (`guide.md`, und `guide.<language>.md` für jede Übersetzung).
- **Strg+K**, die Befehlspalette, findet die Abschnitte der Anleitung nach ihren Titeln (*Anleitung: …*), und
  *Anleitung nach „…“ durchsuchen* durchsucht sie nach dem Eingetippten.
- **Meldungen.** Ein Problem, das das Programm meldet, nennt die Engine, die Datei oder die Ressource, mit der
  Behebung gleich daneben; ein **?** daneben öffnet seinen Eintrag in der [Fehlerbehebung](#fehlerbehebung).
- **Erste Schritte.** Bis ein erstes Turnier gestartet wurde, zeigt die Startseite die Schritte des
  [Schnellstarts](#schnellstart), jeden mit seinem Knopf und abgehakt, sobald er erledigt ist; *Ausblenden* räumt sie
  weg.
- [Melde ein Problem](#ein-problem-melden), wenn das Programm nicht tut, was es soll.

### Die Befehlspalette
<!-- en: the-command-palette 725e7934 -->

**Strg+K** öffnet die Befehlspalette: die Seiten und Tabs, die Engines der Bibliothek (sie öffnet die Karte), die
Bücher, die Turniere der Startseite, die Abschnitte der Anleitung und die gerade möglichen Aktionen (*Pause*,
*Fortsetzen*, *Alles pausieren*, *Stopp*, *Start*, *Zur Warteschlange hinzufügen*, *… neu starten mit…*, die Einträge
von *Importieren…*, *Die noch nicht geprüften prüfen*, *Für eine Rangliste exportieren…*, *Kopieren* der Tabelle,
*Version kopieren*, *Problem melden…*, die Brettfarben und Figurensätze, die Zugtöne ein oder aus…).
Groß-/Kleinschreibung spielt keine Rolle, und die eingetippten Buchstaben müssen nicht nebeneinander stehen („rl“
findet *Rangliste*); Namen, die mit ihnen beginnen oder mit einem Wort, das mit ihnen beginnt, kommen zuerst. Hoch und
Runter wählen, Enter führt aus, Esc schließt; Strg+K wird ignoriert, solange in ein Textfeld getippt wird.

### Sprache, Design und Größe
<!-- en: language-theme-and-size 954fe416 -->

Die Oberfläche gibt es auf Englisch, Französisch, Deutsch, Spanisch, Italienisch, brasilianischem Portugiesisch,
Russisch und vereinfachtem Chinesisch. Beim Start folgt sie der ersten bevorzugten Sprache des Systems (Englisch, wenn
es für diese keine Übersetzung gibt); in den **Einstellungen** lässt sie sich fest einstellen. Chinesisch verwendet
eine Schrift des Systems: Microsoft YaHei unter Windows, Noto Sans CJK oder WenQuanYi unter Linux. Das Design ist hell,
außer du wählst in den **Einstellungen** dunkel oder die Vorgabe des Systems.

Die spanische, italienische, portugiesische, russische und chinesische Übersetzung sind neu: Korrekturen von Spielern
dieser Sprachen sind willkommen. Die der Oberfläche sind einfache `.po`-Dateien in `gui/i18n`, die Poedit und
ähnliche Programme öffnen; die dieser Anleitung sind `docs/guide.<language>.md`.

Die Seiten nutzen das ganze Fenster, wie groß es auch ist: Die Bretter des Tabs Live wachsen mit, das Formular für ein
neues Turnier teilt sich in einem breiten Fenster in zwei Spalten, und die Liste der Seiten Engines, Bücher und
Elo-Listen lässt sich verbreitern, indem du ihren rechten Rand ziehst. Alles (Text, Knöpfe, Bretter) lässt sich
außerdem vergrößern oder verkleinern, von 80 bis 250 %: **Einstellungen** > Größe der Oberfläche, oder Strg + Mausrad,
Strg + und Strg -, und Strg 0 für die Rückkehr zu 100 %. F11 schaltet das Fenster auf Vollbild und zurück.

Ein Zähler auf dem Tab *Live* und auf dem Knopf *Turniere* gibt die Zahl der laufenden Partien an. Ein Zahlenfeld nimmt
Rechenausdrücke an: `40*60`, `15+10`, mit `+ - * /` und Klammern, ausgerechnet bei Enter oder wenn du das Feld
verlässt; Esc gibt dem Feld seinen Wert zurück.

### Bretter und ihre Knöpfe
<!-- en: boards-and-their-buttons 1ed997f4 -->

Unter jedem Brett, das eine Partie zeigt (das Nachspielen im Tab *Partien*, die Partie im Fokus in *Live*, ein
abgetrenntes Brett, die Seite Brett), führt eine Reihe kleiner Knöpfe zur Ausgangsstellung, zum vorherigen Zug, zum
nächsten und zum letzten Zug und dreht das Brett um; ihre Tooltips nennen die Tasten, die dasselbe tun: ← → Pos1 Ende,
und F zum Drehen. Wer die Tasten vorzieht, blendet die Knöpfe aus: mit **Einstellungen** > *Darstellung* >
*Navigationsknöpfe unter den Brettern*, mit einem Rechtsklick auf sie oder mit der Befehlspalette (*Navigationsknöpfe
unter den Brettern ausblenden*, und *Navigationsknöpfe unter den Brettern anzeigen*, um sie zurückzuholen). Das
Mausrad über einer Zugliste scrollt die Liste, nie die Züge. **A** auf einem beliebigen Brett öffnet seine Stellung auf
der Seite [Brett](#brett) und analysiert sie.

### Version und neue Versionen
<!-- en: version-and-new-versions 70e3e9a2 -->

Die Version steht im Fenstertitel und in den **Einstellungen**, deren *Kopieren* sie zusammen mit dem System in die
Zwischenablage legt, für einen Fehlerbericht. *Mir sagen, wenn eine neue Version erscheint* (standardmäßig aus) fragt
GitHub beim Start nach der neuesten Version, höchstens einmal am Tag und im Hintergrund; ist sie neuer, sagt die
Startseite es dir, mit *Download-Seite* (die Seite der Version auf GitHub, im Browser), *Neuerungen* (ihre Hinweise)
und *Ausblenden* (bis zur nächsten Version). Das Programm lädt nichts herunter und installiert nichts, und ein
Fehlschlag (kein Netz, GitHub nicht erreichbar) wird nur ins Protokoll geschrieben.

### Ein Problem melden
<!-- en: reporting-a-problem 3e4e5989 -->

*Problem melden…* (in den **Einstellungen**, auf der Seite **Protokoll**, in der Befehlspalette und in der Meldung
eines Fehlschlags beim Start) öffnet im Browser ein neues Issue auf GitHub, aus dem Formular für Fehlerberichte, schon
ausgefüllt mit der Version, dem System (Betriebssystem, Prozessor, Speicher, Grafikkarte und Treiber) und den letzten
Zeilen des Protokolls. Lies es vor dem Absenden, entferne, was du nicht zeigen willst (Pfade, Engine-Namen), und
schreib, was du getan hast, was du erwartet hast und was passiert ist. Das Programm selbst sendet nichts. Ideen
gehören an denselben Ort: *New issue*, *Idea*.

### Größe und Lage des Fensters
<!-- en: window-size-and-place 892d41c4 -->

Das Fenster öffnet sich so, wie du es verlassen hast: seine Größe, seine Lage auf den Bildschirmen, maximiert oder
nicht. Ist der Bildschirm, auf dem es war, nicht mehr da (ein abgesteckter Monitor), öffnet es sich in der Mitte des
Hauptbildschirms; passt seine Größe nicht mehr auf den Bildschirm (eine geringere Auflösung), in der Standardgröße. Die
herausgezogenen Bereiche, etwa ein abgetrenntes Brett, kommen ebenfalls dorthin zurück, wo sie waren (`layout.ini`),
und ins Fenster, wenn ihr Bildschirm fehlt. Wayland lässt ein Programm die Lage seines Fensters nicht wählen: Dort
kehren nur die Größe und der maximierte Zustand zurück.

### Wie das Fenster gezeichnet wird
<!-- en: drawing-the-window caff7bef -->

Das Fenster wird mit OpenGL 3.2 gezeichnet. Wo es das nicht gibt (eine virtuelle Maschine ohne 3D-Beschleunigung, ein
Remotedesktop, ein alter Server), zeichnet das Programm es stattdessen mit dem eigenen Renderer von SDL: Direct3D 11
unter Windows, OpenGL 2.1 oder allein mit dem Prozessor. Das Protokoll nennt den gewählten Weg und warum OpenGL nicht
verwendet wurde; **Einstellungen** > Grafikausgabe zeigt es auch, mit dem Namen der Grafikkarte, wenn OpenGL zeichnet.
*Software-Rendering* dort (oder `--software` in der Befehlszeile, für einen Start) lässt ab dem nächsten Start den
Prozessor das Fenster zeichnen, für einen Grafiktreiber, der versagt oder abstürzt. Das kostet mehr Prozessorzeit,
zeichnet nur neu, was sich ändert (und was sich bewegt, etwa Uhren, 15-mal pro Sekunde statt 30, solange Maus und
Tasten ruhen), zeichnet eckige Ecken, und die Bereiche des Arbeitsbereichs schweben im Hauptfenster statt auf einem
anderen Bildschirm. Unter Wayland (und unter macOS) wird das Bild des Prozessors trotzdem über den Grafiktreiber
gezeigt; gibt es keinen, starte das Programm mit `SDL_VIDEODRIVER=x11`. Funktioniert keine Art des Zeichnens, sagt
eine Meldung das, bevor das Programm endet.

### Darstellung
<!-- en: appearance decda352 -->

**Einstellungen** > *Darstellung* legt fest, wie jedes Brett des Programms aussieht (das Live-Mosaik und die
verfolgte Partie, die abgetrennten Bretter, das Großbild, eine in *Partien* nachgespielte Partie, der Tab
*Eröffnungen*, die Eröffnungen eines Buchs), mit einem kleinen Brett neben der Auswahl, das sie zeigt:

![Einstellungen > Darstellung: das braune Brett und die Figuren Celtic](screenshots/appearance.png)

- **Brett**: die Farben der Felder, jeweils für das helle und für das dunkle Design (gezeichnet wird das Brett des
  verwendeten Designs). Eine Vorgabe (*Blaugrau*, die Farben der früheren Versionen; *Braun*; *Blau*; *Grün*; *Grau*)
  oder *Eigene*: Ein Klick auf *Helle Felder*, *Dunkle Felder* oder *Letzter Zug* öffnet eine Farbauswahl, und wer
  eine Farbe ändert, macht das Brett zu einem eigenen, ausgehend von den Farben, die es hatte. *Letzter Zug* liegt über
  den beiden Feldern des letzten Zugs, und seine Transparenz lässt die Farbe des Felds durchscheinen.
- **Figuren**: ein in das Programm eingebauter Satz (*Cburnett*, der Standard; *Chessnut*; *Celtic*; *Fantasy*; ihre
  Autoren und Lizenzen im Tooltip und in `ATTRIBUTION.md`) oder *Dein eigener Ordner…*: ein Ordner mit 12 SVG-Bildern
  namens `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` und `bK.svg` … `bP.svg` (weißer und schwarzer
  König, Dame, Turm, Läufer, Springer, Bauer). Die Bilder werden wie die eingebauten mit nanosvg gezeichnet: einfache
  Formen, Füllungen, Linien und Verläufe; kein Text, keine Filter, keine eingebetteten Bilder. Fehlt eine Datei, ist
  sie größer als 1 MB oder kein Bild, das sich zeichnen lässt, sagt die Seite, welche und warum; bis dahin werden die
  Standardfiguren gezeichnet, und *Neu einlesen* liest den Ordner, sobald die Dateien in Ordnung sind. Die Webseite
  der Übertragung behält die Standardfiguren.
- **Zugtöne** (standardmäßig aus): *Ein Ton für jeden Zug* spielt ein kurzes Klopfen für jeden Zug der verfolgten
  Partie, in *Live* und auf dem Großbild, und ein schwereres für ein Schlagen; nicht für jede Partie des Mosaiks.
  *Lautstärke*, und *Zug anhören* / *Schlagen anhören*, um sie zu hören. Die Töne erzeugt das Programm selbst (keine
  Tondatei); das Audiogerät wird nur geöffnet, solange sie eingeschaltet sind, und gibt es keines, sagt das Protokoll,
  warum, und das Programm bleibt stumm.

- **Figurinen in der Notation** (standardmäßig an): Die Züge der Seite Brett, in ihren Zügen und in den Linien ihrer
  Analyse, werden mit den Figuren des oben gewählten Satzes geschrieben (aus Nf3 werden ein Springer und f3); aus, mit
  Buchstaben.

Die Befehlspalette bietet diese Wahl auch (*Brettfarben: Braun*, *Figuren: Celtic*, *Zugtöne einschalten*…). Sie wird
in `settings.json` im Profil gespeichert; der Figurenordner wird im portablen Modus relativ zum Programmordner
gespeichert.

**Engine-Logos**: Die Karte einer Engine auf der Seite **Engines** hat *Logo* > *Wählen…*, ein PNG-, JPEG- oder
SVG-Bild von höchstens 2 MB (höchstens 4096 Pixel in jeder Richtung), gezeigt neben dem Namen der Engine in *Live*, auf
dem Großbild und auf der Webseite der Übertragung. Das Bild wird in den Ordner `logos` des Profils kopiert, so dass das
Original verschoben oder gelöscht und das Profil als Ganzes verschoben werden kann; *Entfernen* nimmt es weg (und
seine Kopie, sofern keine andere Engine sie hat). Das Logo lässt sich ändern, während die Engine spielt.

### Das 3D-Brett
<!-- en: the-3d-board cc0dd596 -->

**Einstellungen** > *Darstellung* > *3D-Brett* > *Das Brett der Seite Brett in 3D* zeichnet das Brett der Seite
[Brett](#brett) dreidimensional, wie das Foto eines echten Bretts (standardmäßig aus; alle anderen Bretter bleiben
flach). Die Grafikkarte berechnet es durch Pfadverfolgung (Path Tracing): Das Bild wird einige Sekunden lang feiner,
solange sich nichts bewegt, mit einigen Millisekunden der Karte pro Bild, dann ruht die Karte; während sich das Brett
dreht, steht ein gröberes Bild an seiner Stelle. Das kleine Brett der *Darstellung* wird dann ebenfalls in 3D
gezeichnet, sodass sich die Einstellungen daneben zeigen, während sie sich ändern:

- die Beleuchtung, wie Fotografen ein Stillleben ausleuchten: *Studio*, *Low Key*, *High Key*, *Ausstellungsraum*,
  *Fensterlicht*, *Goldene Stunde* oder *Turniersaal*;
- *Belichtung*: heller oder dunkler, als die Beleuchtung es macht, in Blendenstufen, jede verdoppelt oder halbiert
  das Licht;
- *Qualität*: die Samples, aus denen das Bild besteht, bevor es ruht, standardmäßig 512; weniger schonen die Grafik
  eines Prozessors während Turnieren;
- *Lack*, von gewachstem, mattem Holz bis zu einer Schicht, in der sich die Lichter zeigen, und *Glanz*, von
  seidenmatt bis zum Spiegel;
- *Standard* stellt Belichtung, Qualität, Lack und Glanz wieder her, wie sie anfangs waren; die Beleuchtung bleibt.

Was das flache Brett zeigt, ist darauf: der Bewertungsbalken, der letzte Zug, die aufgenommene Figur und wohin sie
ziehen kann, ein König im Schach, die Pfeile der Analyse, die schwach durch die Figuren hindurch zu sehen sind, die
Antwort, die die Engine des Zuges erwartete, gestrichelt, das Urteil über den Zug als Plakette über seinem Feld und der
Rahmen einer Variante, die statt der Partie gezeigt wird. Ein Linksklick wählt ein Feld wie auf dem flachen Brett, auf
einer Figur oder auf dem Brett dahinter, und eine Figur lässt sich ziehen, ihr Abbild folgt dem Zeiger (siehe
[Brett](#brett)). Hältst du die rechte Maustaste gedrückt und bewegst die Maus, kreist die Kamera um das Brett (ein
Rechtsklick ohne Bewegung legt die aufgenommene Figur weiterhin ab), das Mausrad geht näher heran, und ein Klick mit dem
Mausrad holt die Ansicht zurück; das Brett zu drehen (F) lässt die Kamera eine halbe Runde machen.

Das 3D-Brett braucht OpenGL 3.3. Wo das Fenster ohne es gezeichnet wird (der Software-Renderer aus
[Wie das Fenster gezeichnet wird](#wie-das-fenster-gezeichnet-wird), manche virtuellen Maschinen und Remotedesktops),
bleiben die Bretter flach, und die *Darstellung* sagt, warum. Die Figuren sind clarkerubbers Staunton-Pieces, unter
der MIT-Lizenz; die *Darstellung* verlinkt sie.

## Tastenkürzel
<!-- en: keyboard-shortcuts e0d74f26 -->

| Tasten | Wo | Was sie tun |
|---|---|---|
| <kbd>Strg</kbd>+<kbd>K</kbd> | Überall | Die Befehlspalette |
| <kbd>F1</kbd> | Überall | Diese Anleitung, beim Abschnitt zu dem, was unter der Maus liegt; noch einmal: schließt sie |
| <kbd>F11</kbd> | Überall | Vollbild und zurück (das eigene Fenster eines abgetrennten Bretts, wenn es die Tastatur hat) |
| <kbd>Strg</kbd>+Mausrad, <kbd>Strg</kbd>+<kbd>+</kbd>, <kbd>Strg</kbd>+<kbd>-</kbd> | Überall | Größe der Oberfläche; <kbd>Strg</kbd>+<kbd>0</kbd>: zurück auf 100 % |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Ein Zahlenfeld | Den Wert ausrechnen; dem Feld seinen Wert zurückgeben |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | Der Anleitungsbereich | Zurück, vorwärts |
| <kbd>Strg</kbd>+<kbd>F</kbd> | Der Anleitungsbereich | Die Anleitung durchsuchen |
| <kbd>Esc</kbd> | Der Anleitungsbereich | Ihn schließen |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Bild↑</kbd> <kbd>Bild↓</kbd> | Die Befehlspalette | Wählen |
| <kbd>Enter</kbd>, <kbd>Esc</kbd> | Die Befehlspalette | Ausführen, schließen |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Pos1</kbd> <kbd>Ende</kbd> | Ein Brett, das eine Partie zeigt | Vorheriger Zug, nächster Zug, Anfang, letzter Zug |
| <kbd>F</kbd> | Ein Brett | Es umdrehen |
| <kbd>A</kbd> | Ein Brett | Die Stellung auf der Seite Brett öffnen und analysieren; auf der Seite Brett: die Analyse starten oder beenden |
| <kbd>Ende</kbd> | Die Partie im Fokus in *Live* | Zurück zum aktuellen Zug |
| <kbd>Strg</kbd>+<kbd>V</kbd> | Die Seite Brett | Eine FEN, eine EPD-Zeile, ein PGN oder eine Zugliste einfügen |
| <kbd>G</kbd> | Die Seite Brett | Die Partie analysieren, Durchgang um Durchgang; noch einmal: anhalten |
| <kbd>X</kbd> | Die Seite Brett | Die Drohung zeigen oder ausblenden |
| <kbd>N</kbd>, <kbd>Umschalt</kbd>+<kbd>N</kbd> | Die Seite Brett | Der nächste, der vorherige von der Analyse markierte Zug |
| <kbd>Esc</kbd> | Die Seite Brett | Die Partie zurückholen, nachdem eine Linie gezeigt wurde; die aufgenommene Figur wieder ablegen |
| <kbd>←</kbd> <kbd>→</kbd> | Das Großbild | Die vorherige oder nächste Partie |
| <kbd>+</kbd> <kbd>-</kbd> | Das Großbild | Größerer oder kleinerer Text |
| <kbd>Esc</kbd> | Das Großbild | Es verlassen |
| <kbd>Tab</kbd>, Pfeiltasten, <kbd>Enter</kbd>, <kbd>Leertaste</kbd> | Neues Turnier | Zwischen den Zeilen, Listen und Knöpfen wechseln; eine Zeile oder eine Liste öffnen |
| <kbd>Strg</kbd>+<kbd>↑</kbd>, <kbd>Strg</kbd>+<kbd>↓</kbd> | Die Teilnehmer | Die ausgewählten Teilnehmer ganz nach oben, ganz nach unten schicken |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | Die Teilnehmer | Sie um einen Platz verschieben |
| <kbd>Entf</kbd> | Die Teilnehmer | Sie entfernen |
| <kbd>Menü</kbd>, <kbd>Umschalt</kbd>+<kbd>F10</kbd> | Die Teilnehmer | Das Menü des Teilnehmers öffnen (seine eigenen Einstellungen) |
| <kbd>Strg</kbd>+<kbd>F</kbd> | Die Bibliothek der Einrichtung | Zu ihrem Filter springen |
| <kbd>Enter</kbd> | Die Bibliothek der Einrichtung | Die ausgewählten Engines hinzufügen, oder die erste angezeigte |
| <kbd>Strg</kbd>+<kbd>A</kbd> | Die Bibliothek der Einrichtung | Alle angezeigten Engines auswählen |
| <kbd>Strg</kbd>+Klick, <kbd>Umschalt</kbd>+Klick | Listen | Mehrere auswählen |
| <kbd>Entf</kbd> | Die Seite Engines | Die ausgewählte Engine entfernen (nach einer Bestätigung) |
| <kbd>Enter</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | Die Konsole einer Engine | Die Zeile senden; die früheren Zeilen zurückholen |
| <kbd>Strg</kbd>+<kbd>C</kbd> | Der Headless-Modus | Nach den laufenden Partien anhalten |

## Die Startseite
<!-- en: the-home-page 40031a16 -->

Die Seite **Turniere** ist ein Kontrollraum: Was läuft und was dich braucht, kommt zuerst, danach der Rest.

- **Erste Schritte**, bis ein erstes Turnier gestartet wurde: die Schritte des [Schnellstarts](#schnellstart)
  (Engines hinzufügen, ein Turnier einrichten, es starten), jeder mit seinem Knopf und abgehakt, sobald er erledigt
  ist, und *Schnellstart lesen*, das ihn im Anleitungsbereich öffnet. *Ausblenden* räumt die Karte weg.
- **Alles pausieren**, neben *Ordner öffnen…* und *Neues Turnier*, pausiert die laufenden Turniere (kein neues
  Partiepaar beginnt, die laufenden Partien werden zu Ende gespielt) und hält die Warteschlange an: Kein Turnier der
  Warteschlange startet bis **Alles fortsetzen**, das sie fortsetzt und die Warteschlange weiterlaufen lässt, sofort,
  falls inzwischen ein Turnier zu Ende gegangen ist.
- **Kerne**: die Hardware-Threads der Maschine, und die davon, die die laufenden Partien belegen, der Rest frei. Eine
  Partie belegt die `Threads` derjenigen ihrer beiden Engines, die die meisten hat, da die beiden Engines abwechselnd
  denken; der Balken zeigt den Namen jedes Turniers und seine Zahl, der Mauszeiger die Einzelheiten.
- **Zu erledigen**, wenn etwas zu erledigen ist, jeweils mit der Behebung gleich daneben: eine Engine, die nicht
  startet (siehe [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann): *Erneut versuchen*,
  *Zurückziehen…*, *Ersetzen…*, *Engine-Log*); das Turnier, das ein Fehler oder eine Engine angehalten hat, mit dem,
  was es angehalten hat, *Engine öffnen*, *Protokoll öffnen*, *Fortsetzen*, *Öffnen* und *Ausblenden*; ein Laufwerk,
  das für eine Nacht voller Partien zu voll ist (unter 1 GB frei, oder unter 2 % bei einem Laufwerk unter 500 GB), das
  des Turnierordners oder eines laufenden Turniers, jede Minute geprüft, mit *Ordner öffnen* und *Turnierordner
  ändern…* (wohin neue Turniere kommen, wie in den **Einstellungen**).
- **Läuft**: eine Karte für jedes laufende Turnier: sein Name, Format, Bedenkzeit und Zustand, ein Fortschrittsbalken
  mit den gespielten von den geplanten Partien und den laufenden Partien, wann es endet, und wer führt: der Erste der
  Tabelle mit seiner Wertung gegen das Feld und seinen Punkten (bei einem K.-o.-Turnier: die verbliebenen Spieler; bei
  einem Mannschaftskampf: der Stand der Mannschaften und seine Elo), die vorne liegende Engine eines Matches mit ihrer
  Elo und dem Stand, oder die LLR eines SPRT mit ihren Grenzen, dann die Elo und der Stand. *Öffnen* führt zu seinem
  Arbeitsbereich, *Pause* pausiert es. Ein Turnier, das fortgesetzt werden kann, erscheint stattdessen unter
  *Unterbrochen*, mit *Fortsetzen*. Siehe [Mehrere Turniere gleichzeitig](#mehrere-turniere-gleichzeitig) für die
  Gewichte und die Kerne jedes Turniers.
- **Warteschlange**: die wartenden Turniere, jedes mit seiner Dauer und wann es startet, und wann alles endet (siehe
  [Warteschlange und Neustart](#warteschlange-und-neustart)).
- **Zuletzt beendet**: die letzten fünf beendeten Turniere mit ihrem Ergebnis: der Stand eines Matches, die
  Entscheidung des SPRT, der Sieger eines K.-o.-Turniers (seines Finales), der Stand eines Mannschaftskampfs, oder der
  Erste der Tabelle mit seinen Punkten aus seinen Partien. *Neu starten mit…* für ein Match, *Klonen* (siehe
  [Warteschlange und Neustart](#warteschlange-und-neustart)) und *Öffnen*.
- **Zustand**: die Abstürze oder Hänger, Zeitverluste und illegalen Züge der letzten 24 Stunden, der freie und der
  eingebaute Speicher und die laufenden Engine-Prozesse; dann die Engines, die diese Partien verloren haben, die drei
  mit den meisten, mit *Engine öffnen* und *Ordner öffnen* (der Ordner `logs` des Turniers bewahrt das Engine-Log jeder
  Partie auf, die schlecht endete). Ein Zeitverlust vor dem 20. Zug wird gesondert ausgewiesen: Er liegt häufiger an
  einer falsch eingerichteten Engine (zu viele Threads, ein Hash, der zu groß ist, um rechtzeitig reserviert zu werden,
  ein Netz, das bei jeder Partie geladen wird, ein langsamer Start) als an ihrem Zeitmanagement.
- **Alle Turniere**: jedes Turnier des Profils, das neueste zuerst, mit *Öffnen*, *Klonen* und *Ordner*; ein Klick
  auf den Titel klappt die Liste zu. Das Ergebnis eines beendeten Turniers erscheint unter dem Mauszeiger, über seinem
  Zustand.

Die Befehlspalette hat diese Aktionen auch: *Alles pausieren*, *Alles fortsetzen*, *… neu starten mit…* für die
zuletzt beendeten Matches, *… klonen* für das angezeigte Turnier und die zuletzt beendeten, *Turnierordner öffnen*
und *Turnierordner ändern…*.

## Engines
<!-- en: engines 4f15155a -->

Die Seite **Engines** ist die Engine-Bibliothek: die Liste links, die Karte der gewählten Engine rechts.

### Engines hinzufügen und prüfen
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Hinzufügen…** nimmt eine ausführbare UCI-Datei auf. Die Engine wird einmal gestartet, um ihren Namen, ihren Autor und
ihre Optionen zu lesen. Die Optionen werden in einer Tabelle bearbeitet: Geänderte Werte sind hervorgehoben, und nur
Werte, die vom Standard der Engine abweichen, werden zu Beginn jeder Partie mit `setoption` gesendet. `Threads` und
`Hash` stehen oben.

- **Duplizieren** legt einen zweiten Eintrag für dasselbe Programm an, mit anderen Optionen (ein anderer `Hash`, eine
  Spielstärke, eine andere Netzdatei…). Einträge werden beim ersten Hinzufügen nach dem UCI-`id name` benannt und
  lassen sich umbenennen.
- **Prüfen** startet die Engine, misst, wie schnell sie auf `uci` und `isready` antwortet, und lässt sie einen Zug
  spielen; dann führt es die UCI-Konformitätsschritte von fastchess aus (`id name` und `id author`, Stellungen aus
  einer FEN und mit Zügen, `go` mit jeder Uhr, Info-Zeilen mit einer ganzzahligen Bewertung…), bis zum ersten, der
  scheitert. Das Ergebnis (✓ oder der Fehler) bleibt mit seinem Datum neben der Engine stehen, und
  *UCI-Konformitätsbericht* listet die Schritte auf, mit dem, was die Engine nach einem gescheiterten geschrieben hat.
  Eine Engine, mit der in xboard gesprochen wird, erhält nur den ersten Teil.
- **Konsole…** spricht mit der Engine, außerhalb jedes Turniers: Sie wird mit dem Programm, den Argumenten und dem
  Arbeitsverzeichnis der Bibliothek gestartet, und eine UCI-Engine erhält `uci` und dann die Optionswerte der
  Bibliothek. Eingetippte Zeilen werden so gesendet, wie sie sind (Enter; Hoch und Runter holen die früheren zurück),
  und das Log zeigt jede Zeile mit ihrer Uhrzeit, die gesendeten mit `>` markiert und die Fehlerausgabe mit `!`. Knöpfe
  senden die üblichen Befehle (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`…; die von WinBoard
  bei einer WinBoard-Engine, mit der in ihrem eigenen Protokoll gesprochen wird, sofern nicht *Über den Adapter*
  angehakt ist) und die Stellung der Seite Brett. *Kopieren* und *Speichern…* bewahren das Log auf. Die Konsole
  erscheint anstelle der Einstellungen der Engine (*Einstellungen* führt zu ihnen zurück, die Konsole bleibt offen);
  *Schließen* beendet die Engine. Während ein Turnier läuft, nimmt das, was die Engine sucht, seinen Partien Kerne weg:
  Die Konsole weist darauf hin. Ihre Engine ist keiner der Prozesse des Turniers: Das Ende eines Turniers beendet sie
  nicht.
- **Schnelltest** spielt ein kurzes Match gegen die Engine selbst oder eine andere: 10 Partien bei 10+0.1 aus 5
  Stellungen (eine mit einem En-passant-Schlag), über denselben Code wie ein Turnier, in seinem Arbeitsbereich gezeigt,
  während er läuft. Das Ergebnis bleibt neben der Engine stehen: ihre Punkte, ihre Zeitverluste, Abstürze und illegalen
  Züge, und der Anteil ihrer Uhr, den sie verbraucht hat, woran sich eine Engine erkennen lässt, die viel zu schnell
  oder zu nah am Zeitlimit spielt. Ihre Matt-PVs werden geprüft (siehe *Matt-PVs prüfen* in
  [PGN und Sonstiges](#pgn-und-sonstiges)). Nur der letzte Schnelltest wird aufbewahrt, in `tests/` im Profil (mit den
  Engine-Logs seiner Partien, die schlecht endeten).

### Eine große Bibliothek
<!-- en: a-large-library beb231ed -->

- **Gruppen**: freie, durch Kommas getrennte Bezeichnungen (`pool A`, `list 40/15`…). Die Liste lässt sich nach
  Gruppe filtern oder durchsuchen, und eine Gruppe lässt sich mit einem Klick zu einem Turnier hinzufügen. *Notizen*:
  alles, was man sich über die Engine merken will. *Logo*: ein Bild, gezeigt neben ihrem Namen in *Live*, auf dem
  Großbild und auf der Webseite der Übertragung (siehe [Darstellung](#darstellung)).
- **Suche und Filter**: Jede Engine-Suche (die Bibliothek und die Engine-Listen der anderen Seiten) findet einen Namen,
  den Namen, den die Engine meldet, ihren Autor, ihren Programmpfad oder ihre Notizen; der Zustandsfilter zeigt die
  Engines, die funktionieren, fehlgeschlagen sind oder sich geändert haben, nie geprüft wurden, ihr Programm verloren
  haben, WinBoard sprechen oder inaktiv sind. Strg- oder Umschalt-Klick wählt mehrere Engines aus: *Prüfen* (eine nach
  der anderen, nie während ein Turnier läuft), *Zu einer Gruppe hinzufügen…*, *Entfernen…*. *Mehr…* prüft die nie
  geprüften (*Die noch nicht geprüften prüfen*) und entfernt die fehlenden Programme. Eine Zeichenketten-Option, die
  eine Datei oder einen Ordner enthält (`EvalFile`, `SyzygyPath`…), hat einen Knopf *…*.
- **Inaktiv** (auf der Karte, oder per Rechtsklick für alle ausgewählten Engines): Die Engine bleibt ausgegraut in der
  Bibliothek, fehlt aber in den Engine-Auswahllisten (Einrichtung, Suiten, Schnelltest, Neustart, Hinzufügen einer
  Engine zu einem laufenden Turnier) und bei *Die noch nicht geprüften prüfen*. Ein Turnier oder eine Einrichtung, die
  sie schon enthält, behält sie.
- **Nutzt eine GPU** (auf der Karte): eine Netzwerk-Engine, die auf einer Grafikkarte spielt, etwa Lc0. Es spielen nie
  mehr solche Engines gleichzeitig, als die **Einstellungen** unter *GPUs* angeben, in einem Turnier und über Turniere
  hinweg (siehe [Mehrere Turniere gleichzeitig](#mehrere-turniere-gleichzeitig)); ab dem nächsten Turnier, an dem sie
  teilnimmt.
- **Referenz-Elo**: die Wertung der Engine auf einer Liste, der du vertraust. Ein Turnier oder die Rangliste lässt
  sich daran verankern, und die Partien eines Turniers tragen sie in ihren Tags `WhiteElo` und `BlackElo` (kopiert,
  wenn die Engine dem Turnier beitritt). *Importieren…* > *Eine Rangliste, für die Referenz-Elo…* setzt sie für viele
  Engines auf einmal (siehe [Engines und Wertungen importieren](#engines-und-wertungen-importieren)).

### Engines starten
<!-- en: starting-engines 09d107ba -->

- **Das Lesen der Optionen** blockiert nie einen Start. *Fortsetzen* startet sofort: Ein Turnier spielt die in seinen
  Ordner kopierten Engines. *Start*, die Warteschlange und der Schnelltest warten nur auf das Lesen einer Engine, die
  sie spielen, und dieses wird vorgezogen; jedes andere Lesen oder Prüfen wird unterbrochen und später wiederholt. Ein Start, der wartet, zeigt es auf seinem Knopf (*Warte auf …*, mit *Abbrechen*), und ein weiterer Start
  wird so lange abgelehnt. Ein Start wartet auch auf eine laufende Testsuite, und eine Testsuite startet nicht, während
  der Schnelltest läuft oder ein Start wartet. Lesen, Prüfen und Testsuiten laufen neben den Turnieren, die in eigenen
  Prozessen spielen. Eine Engine in der Warteschlange lässt sich über ihr Menü als nächste lesen.
- *Die Engine für jede Partie neu starten* startet für jede Partie einen neuen Prozess, statt ihn wiederzuverwenden.
  Andernfalls hält ein Turnier einen wartenden Prozess pro Engine bereit, die noch Partien zu spielen hat, bis zu 32
  (plus zwei pro laufender Partie), und schließt eine Engine nach ihrer letzten Partie: in einem K.-o.-Turnier, sobald
  ihr Match verloren ist, im Schweizer System, sobald sie in der letzten Runde keine Partie mehr hat.
- *Start-Timeout (s)* (standardmäßig 60) ist die Zeit, die eine Engine bekommt, um beim Start mit `uciok` und zu
  Beginn jeder Partie auf `isready` zu antworten; wer langsam startet (ein großes Netz zu laden), braucht mehr. Eine
  Engine, die nicht startet, wird noch dreimal im Abstand von 10 s mit demselben Timeout versucht; eine Pause hält die
  Versuche an, ohne sie zu verbrauchen. Danach werden ihre Partien zurückgestellt, und die anderen gehen weiter (siehe
  [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann)). Ein danach angelegtes Turnier kopiert den
  Timeout in seine Teilnehmer.

### Neu gebaute Programme
<!-- en: rebuilt-programs 549daa6e -->

- **Verschobene oder neu gebaute Programme**: Die Bibliothek merkt sich jedes Programm (Größe, Datum, CRC32). Ein
  Programm, das nicht mehr da ist, wird mit **!** und *Suchen…* markiert, und ein Turnier startet nicht mit ihm. Bei
  einem Programm, das sich geändert hat (ein neuer Build oder eine andere Datei unter demselben Pfad), werden die
  Optionen automatisch neu gelesen.
- **Was ein neuer Build geändert hat**: Weicht das neue Lesen vom vorigen ab, wird die Engine mit **Δ** markiert, und
  ihre Karte listet die Unterschiede auf, bis *Schließen*: ihr neuer `id name` (mit *In „…“ umbenennen*, wenn die
  Engine nach dem alten benannt war) und Autor, jede Option, deren Standard, Grenzen oder Auswahlwerte sich geändert
  haben (vorher und jetzt), die neuen und die weggefallenen Optionen, und dein Wert für jede (der Wert einer
  weggefallenen Option entfällt; einer, den der neue Build nicht mehr annimmt, steht in Orange). Für eine Option, deren
  Standard sich geändert hat oder deren Wert nicht mehr passt, wähle *Meinen Wert behalten* (oder *Alten Standard
  behalten*, wo du keinen hattest: Sie spielt wie bisher) oder *Neuen Standard nehmen*; *Meine Werte behalten* und
  *Neue Standards nehmen* wählen für alle. Der Hinweis verschwindet, sobald jede Wahl getroffen ist, es sei denn, er
  hat noch mehr mitzuteilen. Mehrere Builds, bevor er geschlossen wird, ergeben einen einzigen Hinweis, verglichen mit
  dem Lesen vor dem ersten. *Fehlgeschlagen oder geändert* im Zustandsfilter zeigt diese Engines ebenfalls, und die
  Befehlspalette hat für jede *Was sich geändert hat*, *Meine Werte behalten*, *Neue Standards nehmen* und *Hinweis zu
  den Änderungen schließen*.
- **Die Programme der Turniere**: Jedes Turnier hält die Programme fest, mit denen es gestartet wurde. Die
  Zusammenfassung der Einrichtung warnt vor einer Engine, deren Programm sich seit ihrem letzten Turnier geändert hat
  (dem neuesten Turnier des Turnierordners, das dieses Programm gespielt hat), mit *Was sich geändert hat*, wenn ihre
  Karte einen Hinweis hat. Wird ein Turnier fortgesetzt, dessen Programm seither neu gebaut wurde oder fehlt, sagt sein
  Kopf das, mit *Weiter* (der Hinweis schließt sich) und *Pause*; das Turnier hält nicht von selbst an.

### Engines und Wertungen importieren
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importieren…** > *Eine Engine-Liste…* liest die Engine-Liste eines anderen Programms: ChessGUI (`ChsGUI_Eng.TXT`,
mit den Optionen aus der daneben liegenden `ChsGUI_Cfg.ini`), cutechess und Banksia (`engines.json`), Arena
(`ArenaENG.cfg`, im Ordner von Arena: Programm, Argumente und Protokoll jeder Engine und, bei UCI-Engines, die in Arena
gesetzten Optionswerte) und andere Listen im INI-Stil. Eine Liste, die keine Engine liefert, sagt das, mit den
gelesenen Formaten und *Alle Engines eines Ordners…* als Alternative.

*Alle Engines eines Ordners…* übernimmt jedes in einem Ordner gefundene Programm und merkt sich den Ordner:
*Aktualisieren: die neuen Engines des Ordners* bietet dann nur die seither hinzugekommenen Engines an. Die gefundenen
Engines werden zuerst aufgelistet, damit du bei denen, die du weglassen willst, das Häkchen entfernst; Engines, die
schon in der Bibliothek sind (gleiches Programm und gleiche Argumente), werden übersprungen; die Optionen der anderen
werden gelesen; WinBoard-Engines werden ebenfalls importiert.

*Importieren…* > *Eine Rangliste, für die Referenz-Elo…* liest eine Rangliste, eingefügt oder geöffnet: die
Textversion einer Rangliste oder ihre Webseite (gespeichert oder kopiert), die Ausgabe von Ordo (Text oder CSV) oder
eine CSV-Datei mit Zeilen `name,elo`. Ein Name der Liste ist eine Engine der Bibliothek mit demselben Namen, oder die
einzige, deren Name von Groß-/Kleinschreibung und Leerzeichen abgesehen derselbe ist (wie bei den Spielern
importierter PGN-Dateien, siehe [Elo-Listen](#elo-listen)). Die Vorschau zeigt die alte und die neue Referenz-Elo jeder
Engine, zum Setzen angehakt, und die Namen der Liste, die keine Engine der Bibliothek sind. Die Namen einer Liste
enthalten oft ihren Build (`64-bit 4CPU`): Benenne die Engines bei Bedarf passend um.

### WinBoard-Engines
<!-- en: winboard-engines 079e4849 -->

**WinBoard-Engines** (xboard-Protokoll, CECP) werden wie die anderen gespielt. Das *Protokoll* einer Engine wird beim
Hinzufügen ermittelt (*Automatisch*) oder gewählt: UCI oder WinBoard. fastchess spricht nur UCI: Eine WinBoard-Engine
wird über eine in fastchess-gui eingebaute Übersetzung gestartet (`fastchess-gui --wb2uci ENGINE [ARGS]`, das
fastchess anstelle der Engine startet), und ihre Optionen, `memory`, `cores` und der Syzygy-Pfad werden zu
UCI-Optionen. Sie steuert die Engine so, wie WinBoard es tut: Features von Protokoll 2, oder Protokoll 1 für Engines,
die keine senden; Stellungen mit `setboard` oder im Edit-Modus; Züge in Koordinaten oder SAN; `level`,
`time`/`otim`, `st`, `sd`, `nps`; Denkausgabe, Aufgabe. Engines, die in der Liste mit ↔ markiert sind, sind
WinBoard-Engines.

- Eine Engine mit Protokoll 1 ohne `setboard` und ohne Edit-Modus (TSCP zum Beispiel) kann nur aus der
  Ausgangsstellung beginnen: Gib ihr die Eröffnungen als Züge (ein PGN-Buch), nicht als Stellungen (EPD).
- Chess960 für eine Engine mit der Variante `fischerandom`: `variant fischerandom`, Stellungen in X-FEN, Rochade als
  `O-O`/`O-O-O`, so wie WinBoard sie sendet.
- *Scores from White's side* unter den Optionen der Engine: für die alten Engines, die ihre Bewertungen aus der Sicht
  von Weiß angeben statt aus der Sicht der Seite am Zug, wie es das Protokoll verlangt; ihre Bewertungen als Schwarz
  werden umgekehrt.
- Eine Engine, die verlangt, für jede Partie neu gestartet zu werden (`feature reuse=0`), bekommt beim Hinzufügen
  *Die Engine für jede Partie neu starten* angehakt.
- `FCG_WB2UCI_LOG=FILE` in der Umgebung schreibt jede mit WinBoard-Engines ausgetauschte Zeile in `FILE`.

## Bücher
<!-- en: books 4cf13005 -->

**Hinzufügen…** nimmt ein Eröffnungsbuch (EPD, PGN oder Polyglot `.bin`) unter einem Namen auf, mit einer
Beschreibung; bei einem PGN-Buch die Zahl der übernommenen Halbzüge, bei einem Polyglot-Buch die Tiefe, bis zu der
seinen Linien gefolgt wird. Der Abschnitt *Eröffnungen* der Einrichtung bietet dann die Bücher der Bibliothek nach Namen
an, mit ihrer Zahl an Eröffnungen (jede andere Datei lässt sich weiterhin wählen). *Entfernen* nimmt ein Buch aus der
Bibliothek und lässt die Datei unberührt.

### Ein Buch prüfen
<!-- en: checking-a-book 96eee229 -->

Das gewählte Buch wird im Hintergrund gelesen, mit den eigenen Lesern von fastchess (seine Eröffnungen sind also
genau die, die ein Turnier spielt), und geprüft:

- **illegale Stellungen**: nicht genau ein König jeder Farbe, Bauern auf der ersten oder letzten Reihe, die Seite,
  die nicht am Zug ist, im Schach;
- **Duplikate**: dieselbe Stellung wie eine frühere Eröffnung, auch per Zugumstellung;
- **vertauschte Farben**: eine frühere Eröffnung mit getauschten Farben (ein Buch, das zweimal mit vertauschten Farben
  gespielt werden soll, braucht nicht beide);
- **bereits beendete Partien**: kein legaler Zug, oder nicht genug Material zum Mattsetzen.

fastchess lässt die Eröffnungen, deren Stellung sich nicht spielen lässt, aus einem Turnier weg (eine FEN- oder
EPD-Zeile ohne König oder sonst fehlerhaft, ein König, der geschlagen werden könnte, eine PGN-Partie mit ungültigem
FEN-Tag), mit einer Warnung pro Buch; sie werden hier als illegale Stellungen gezählt. Eine PGN-Partie, deren Züge sich
ab einer Stelle nicht mehr lesen lassen (ein fehlerhafter oder mehrdeutiger Zug), behält die Züge davor. Die Zeilen
eines PGN-Buchs, die mit `%` beginnen (Escape-Zeilen, die der PGN-Standard zu Kommentaren macht; die AVT-Bücher haben
welche), werden ignoriert, auch von den Turnieren, und die Seite Bücher zählt sie (*Zeilen, die mit % beginnen,
ignoriert*).

Seine Eröffnungen werden darunter aufgelistet, alle oder nur die mit einem Problem, durchsuchbar nach einem Teil einer
FEN oder nach Zügen (ein PGN-Buch zeigt seine Züge in SAN); ein Klick zeigt die Stellung auf einem Brett. *Für das
nächste Turnier verwenden* übernimmt es in die Einrichtung (mit Chess960 bei einem Chess960-Buch). *Bereinigte Kopie
speichern…* schreibt ein EPD-Buch ohne die illegalen Stellungen, die Duplikate und die beendeten Partien (und, falls
angehakt, ohne die Stellungen mit vertauschten Farben) und fügt es zur Bibliothek hinzu. *Als Testsuite ausführen…*
öffnet ein EPD-Buch in **Suiten**.

### Bücher erstellen
<!-- en: creating-books b9c072ab -->

**Erstellen…** schreibt Bücher für die Bibliothek (auch in der Befehlspalette):

- *Chess324-Buch erzeugen*: Chess324 von Larry Kaufman. Die Könige und die Türme bleiben auf ihren üblichen Feldern,
  die Rochade ist also die übliche; Dame, Läufer und Springer jeder Seite werden auf die anderen Felder ihrer
  Grundreihe gemischt (die Linien b, c, d, f und g), die Läufer auf Feldern verschiedener Farbe. Das ergibt 18
  Aufstellungen pro Seite, die von Weiß und die von Schwarz unabhängig gewählt: *Alle 324 Stellungen…* oder *Nur die
  18 symmetrischen…*, bei denen die Figuren von Schwarz auf denselben Linien stehen wie die von Weiß. Es sind
  gewöhnliche Stellungen, in eine EPD-Datei geschrieben (angeboten neben den Büchern der Bibliothek oder in `books/` im
  Profil) und von jeder Engine spielbar, ohne Chess960.
- *Polyglot-Buch erstellen…*: ein Polyglot-Buch (`.bin`) aus den Partien einer PGN-Datei, so wie `make-book` von
  Polyglot es macht, mit der Umwandlung, die die PGN-Bücher der Teilnehmer zu Polyglot-Büchern macht. Sein Formular,
  das anstelle der Angaben zum Buch erscheint, erfasst die PGN-Datei (das in der Liste gewählte PGN-Buch, falls
  vorhanden), die
  **Tiefe** (die Halbzüge jeder Partie, die ins Buch eingehen), die **Mindestzahl an Partien**, die ein Zug in seiner
  Stellung braucht, um zu bleiben (`-min-game` von Polyglot; die anderen werden als zu selten gespielt weggelassen),
  die **Gewichte**, *Nach Punkten* (die Punkte der Seite, die den Zug gespielt hat: ein Sieg 2, ein Remis 1, eine
  Niederlage 0, wie Polyglot zählt; Partien ohne Ergebnis werden weggelassen, ebenso ein Zug, dessen Partien alle
  verloren wurden) oder *Nach Anzahl* (die Zahl der Partien, die ihn spielen), und, falls angehakt, eine **Wertung**:
  Nur die Züge eines Spielers mit mindestens dieser Wertung zählen, nach dem Tag `WhiteElo` oder `BlackElo` der Partie
  (ein Spieler ohne diesen Tag wird weggelassen). *Erzeugen…* fragt, wohin das Buch geschrieben werden soll
  (standardmäßig neben die PGN-Datei), und liest dann die Partien im Hintergrund, mit seinem Fortschritt und
  *Abbrechen*, neben dem Formular und am Ende der Liste; das Buch, nach Schlüssel sortiert, wie das Format es
  verlangt, wird dann zur Bibliothek hinzugefügt, seine Optionen in seiner Beschreibung. Die Konsole sagt, wie viele
  Partien eingegangen sind, wie viele die Optionen weggelassen haben und wie viele nicht gelesen werden konnten.

## Brett
<!-- en: board 0bbd00c9 -->

Die Seite Brett zeigt eine Partie oder eine Stellung, von irgendwoher: **A** auf einem beliebigen Brett des Programms
(eine Partie des Mosaiks im Tab *Live* oder die verfolgte, das Brett des Tabs *Partien*, des Tabs *Eröffnungen*, einer
Eröffnung eines Buchs) öffnet seine Stellung hier und analysiert sie; auf der Seite Brett startet und beendet A die
Analyse. *Zurück zum Turnier* (oder *Zurück*) kehrt zu der Seite zurück, von der aus sie geöffnet wurde. Ihr Brett kann
in 3D gezeichnet werden: siehe [Das 3D-Brett](#das-3d-brett).

Das Brett steht links, mit den Spielern darüber und darunter; rechts, von oben nach unten, die Analyse, die Züge (mit
der Tabelle der Züge, der Auswertung der Partie und den Partien der Datei in Tabs) und die Partiegrafik. Der
Untertitel der Seite sagt, was für eine Partie es ist: ihre Veranstaltung und Runde, ihr Ergebnis, ihre Bedenkzeit,
ihr ECO-Code und ihre Eröffnung, und wie sie endete.

Ein Zug wird mit der Maus auf dem Brett gespielt: ein Klick auf die Figur, dann einer auf ihr Feld, oder die Figur
dorthin gezogen. Während sie gezogen wird, folgt sie dem Zeiger, und das Feld, auf dem sie landen würde, ist getönt;
woanders losgelassen, kehrt sie zurück, weiterhin aufgenommen, um mit einem Klick gezogen zu werden. Auf dem 3D-Brett
folgt ein durchscheinendes Abbild der Figur dem Zeiger, über dem Brett schwebend und hinter den Figuren vor ihm vorbei,
während das Bild bleibt, wie es ist; die Figur selbst bewegt sich, wenn sie losgelassen wird.

### Eine Stellung öffnen, einfügen und aufbauen
<!-- en: opening-pasting-and-setting-up-a-position 3778ddb5 -->

- **PGN öffnen…** liest eine beliebige PGN-Datei (eine Datei lässt sich auch auf das Fenster ziehen), im Hintergrund
  mit einem Fortschrittsbalken: Eine Datei mit Millionen Partien braucht ein paar Sekunden, und ihre Partien werden in
  einem Tab neben den Zügen aufgelistet, gefiltert nach dem Eingetippten (jedes Wort, gefunden in den Spielern, der
  Veranstaltung, dem Ort, dem Datum, der Runde, der Eröffnung, dem ECO-Code oder dem Ergebnis). Ein Klick zeigt eine
  Partie. Die Bewertungen ihrer Züge zeichnen die Grafik: Gelesen werden die Kommentare von cutechess und fastchess
  (`{+0.35/18 1.2s}`) und das `[%eval 0.35]` von lichess und ChessBase (aus der Sicht von Weiß, `#-3` für ein Matt),
  ebenso die Uhren von `[%clk 0:01:23]`. Ihre Varianten bleiben erhalten (siehe [Varianten](#varianten)). Eine
  Partie, deren FEN-Tag sich nicht spielen lässt, wird abgelehnt, und die Züge einer Partie enden beim ersten, der
  sich nicht lesen lässt.
- **Einfügen** (Strg+V auf der Seite) liest die Zwischenablage: eine FEN oder die vier Felder einer EPD-Zeile; ein PGN
  mit einer oder mehreren Partien (aufgelistet wie die einer Datei); eine Zugliste ab der Ausgangsstellung, in SAN mit
  oder ohne Zugnummern (`1. e4 e5 2. Nf3`) oder in UCI (`e2e4 e7e5`). Eine Stellung, die sich nicht spielen lässt,
  wird mit dem Grund abgelehnt (ein fehlender König, ein Bauer auf der ersten Reihe, die Seite, die nicht am Zug ist,
  im Schach). Rochaderechte, deren König oder Turm nicht auf seinem üblichen Feld steht, oder die als Linien
  geschrieben sind (`HAha`), machen sie zu einer Chess960-Stellung. Auch eine Datei `.fen` oder `.epd` lässt sich auf
  das Fenster ziehen.
- **Stellung bearbeiten** baut eine Stellung auf dem Brett auf: Ein Klick setzt die gewählte Figur (ein zweiter Klick
  entfernt sie), ein Rechtsklick entfernt sie; dazu die Seite am Zug, die Zugnummer, die Rochaderechte (nur angeboten,
  wenn König und Turm stehen, wo sie stehen müssen), das En-passant-Feld (nur hinter einem Bauern, der gerade zwei
  Felder gezogen ist) und Chess960. Die FEN darunter folgt dem Brett, und eine dort eingetippte FEN stellt es ein.
  *Fertig* wird angeboten, sobald sich die Stellung spielen lässt, und bringt sie auf das Brett.
- *PGN kopieren* (rechts von den Tabs der Züge) kopiert die gezeigte Partie mit ihren Tags, ihren Zügen (die
  Bewertungen der Engines als Kommentare, wenn sie welche hat) und ihren Varianten.
- **Das Brett**: Die Knöpfe darunter und ← → Pos1 Ende gehen durch die Züge, und F (oder der letzte Knopf) dreht es
  um; ein Klick auf eine Figur und auf ein Feld spielt einen Zug (ein Bauer, der umwandelt, fragt nach seiner Figur).
  *FEN kopieren* kopiert die Stellung.

### Die Spieler
<!-- en: the-players 1f4f551f -->

Über und unter dem Brett, wenn die Partie Spieler hat: der Name jedes Spielers, die Bewertung seines letzten Zugs
durch seine Engine (die Bewertung aus der Sicht von Weiß und die Tiefe, als Chip), der Zug, den er gespielt hat, und
die Zeit, die er dafür gebraucht hat, oder *am Zug*, und rechts seine Uhr nach seinem letzten Zug, umrahmt, solange er
am Zug ist. Die Uhren sind die der `[%clk]` der Züge, wenn die Partie sie hat; sonst werden sie aus ihrem Tag
`TimeControl` (`300+2`, `10+0.1`: Sekunden und Inkrement) und der Zeit jedes Zugs rekonstruiert, wobei Buchzüge keine
Zeit verbrauchen.

### Die Analyse
<!-- en: the-analysis 1e6120de -->

Eine Engine der Bibliothek durchsucht die gezeigte Stellung und folgt dem Brett. Die Karte zeigt ihre Engine, dann
einen Knopf mit ihren *Threads*, ihrem *Hash* (0: der eigene der Engine) und ihren *Linien* (MultiPV), den ein Klick
öffnet: die Einstellungen der Analyse selbst, die für das nächste Mal erhalten bleiben und weder die Engine der
Bibliothek noch irgendein Turnier ändern (die übrigen Optionswerte der Bibliothek werden so gesendet, wie eine Partie
sie sendet). Rechts die Threads, die die Seite Brett von denen der Maschine belegt, in der Warnfarbe, während ein
Turnier läuft (ihr Tooltip sagt, wie viele Threads die Partien des Turniers belegen: Die Analyse konkurriert mit
ihnen).

Die Suche erscheint als Kacheln (Tiefe und seldepth, Knoten, Geschwindigkeit, Zeit, Tablebase-Treffer, wie voll der
Hash ist), dazu die Gewinn-, Remis- und Verlustchancen, wenn die Engine sie angibt (`UCI_ShowWDL`), und jede Linie:
ihre Bewertung (aus der Sicht von Weiß, die beste hervorgehoben), ihre Tiefe und ihre Züge auf zwei Zeilen, mit
Figurinen (siehe [Darstellung](#darstellung)). Steht die Maus über einem Zug einer Linie, zeigt das Brett die
Stellung danach, umrahmt; ein Klick spielt die Linie bis zu diesem Zug. Der erste Zug jeder Linie wird als Pfeil
gezeichnet, der beste dunkler (**Einstellungen** > *Darstellung* > *Die Pfeile der Analyse auf der Seite Brett*
blendet sie aus; die Linien zeigen die Züge weiterhin).

**Drohung** (X) lässt einen weiteren Prozess der Engine die Stellung mit übergebenem Zugrecht durchsuchen: den besten
Zug des Gegners, dürfte er noch einmal ziehen, als roter Pfeil gezeichnet und unter die Linien geschrieben (keiner,
solange die Seite am Zug im Schach steht).

Die Analyse wartet, während eine andere Seite gezeigt wird, und geht weiter, wenn die Seite Brett wiederkommt;
*Analyse beenden* (oder A) beendet sie, und ein Punkt auf dem Knopf *Brett* der Leiste zeigt an, dass sie läuft. Ihre
Engine ist keiner der Prozesse des Turniers: Das Ende eines Turniers beendet sie nicht. Nach dem Beenden bleibt die
Engine für die nächste Suche gestartet, mit ihrer Hashtabelle (und dem GPU-Speicher einer Netz-Engine): Der
Ein/Aus-Knopf neben der Engine, *Engine entladen* (auch in der Befehlspalette), beendet sie und die der Drohung.

### Eine ganze Partie analysieren
<!-- en: analysing-a-whole-game a7e6166c -->

**Partie analysieren** (G, in den Werkzeugen der Seite oder in der Kopfzeile der Grafik) lässt die Analyse-Engine
(ihre Threads und ihr Hash, eine Linie) jede Stellung der Partie im Hintergrund durchsuchen, zuerst je 20 ms, dann bei
jedem Durchgang erneut viermal so lange (80 ms, 0,3 s, 1,3 s, 5 s…), bis zu der Zeit, die mit dem Pfeil neben dem
Knopf gewählt wird (0,3 s, 1,3 s, 5 s, 20 s pro Stellung oder *bis zum Stoppen*): Die Grafik ist nach ein, zwei
Sekunden da und wird mit jedem Durchgang schärfer. Die Stellungen werden von der letzten zur ersten durchlaufen,
sodass der Hash der Engine festhält, was nach einer Stellung kommt (gemessen: bei gleicher Zeit etwa 16 % näher an
langen Suchen als von der ersten zur letzten), die Stellung auf dem Brett zuerst. Die Kopfzeile der Grafik nennt den
Durchgang, die Zeit pro Stellung und den Fortschritt, und ein schmales Band unter ihrem Zeitstreifen, was jede
Stellung bekommen hat; *Stopp* (oder G) hält sie an.

Jeder Zug wird nach den Gewinnchancen beurteilt, die die ziehende Seite mit ihm verloren hat, aus den Bewertungen der
Analyse davor und danach (das Maß von lichess: 50 % bei 0.00, etwa 59 % bei +1.00): 10 verlorene Punkte sind eine
Ungenauigkeit (`?!`), 20 ein Fehler (`?`), 30 ein grober Fehler (`??`), markiert hinter dem Zug, auf der Grafik und
auf dem Brett; der Zug, den die Engine gespielt hätte, wird nie markiert. Ein Zug wird nach dem letzten vollständigen
Durchgang beurteilt; eine Markierung, die der letzte Durchgang geändert hat, ist hohl, bis der nächste sie bestätigt.
N und Umschalt+N (oder die Knöpfe neben der Navigation) gehen zum nächsten und zum vorherigen markierten Zug.

Jeder vollständige Durchgang wird mit der Partie aufbewahrt (in `analyses/` im Profil): Dieselbe Partie, von irgendwo
erneut geöffnet, zeigt die Analyse sofort, und G macht bei den erledigten Durchgängen weiter. Die Analyse läuft in
einem eigenen Thread mit einem eigenen Engine-Prozess, ihre Threads zählen zum Kern-Budget; die Analyse des Bretts (A)
pausiert sie, und wenn A eine Stellung der Partie länger durchsucht, als der Durchgang es tat, tritt ihr Wert an die
Stelle des Werts dieser Stellung.

**Mehrere Analysen**: *+ Engine* in der Legende der Grafik lässt eine weitere Engine der Bibliothek die Partie
ebenfalls analysieren, bei jedem Durchgang gleich lang. Jede Analyse hat ihre Farbe auf der Grafik und auf dem Brett
(ihr bester Zug als Pfeil, solange die Analyse des Bretts aus ist), ihre Zeile in *Dieser Zug* und ihre Genauigkeiten
in der Auswertung. Sie gehen gemeinsam Durchgang für Durchgang vor, alle gleichzeitig, wenn die Kerne es erlauben,
sonst eine nach der anderen. Eine Analyse beurteilt die Züge (die erste; ein Rechtsklick auf eine andere in der
Legende macht sie zum Richter oder entfernt sie); eine Markierung ist hohl, wenn die Analysen den Zug nicht alle
gleich beurteilen. Gewinnchancen vergleichen Engines besser als Centipawns, die sich von einer Engine zur anderen
unterscheiden: Sie nehmen das WDL jeder Engine, wenn sie eines angibt.

### Die Partiegrafik
<!-- en: the-game-graph 14e02bad -->

Unter den Zügen die Grafik der Partie, dieselbe wie in *Live* und *Partien*:

- **Bewertung**: die beurteilende Analyse als weiße und schwarze Fläche (der Vorteil von Weiß über der Linie, der von
  Schwarz darunter), jede Engine, die gespielt hat, als Kurve in ihrer Farbe bei ihren eigenen Zügen, die anderen
  Analysen als gestrichelte Kurven; die markierten Züge auf der Kurve; Werte jenseits von ±6 am Rand entlang in der
  Warnfarbe gezeichnet. Ohne Analyse ist die Fläche der Mittelwert der Bewertungen der beiden Engines.
- Die Buchzüge schraffiert, die Phasen unter der Achse (die Eröffnung; das Mittelspiel ab der ersten Stellung mit
  höchstens 10 Figuren außer Königen und Bauern oder weniger als 4 auf einer Grundreihe; das Endspiel ab höchstens 6,
  wie lichess eine Partie einteilt), die Zone, die die Tablebases kennen, eingefärbt, die Zugnummern am unteren Rand.
- Unter der Achse die Zeit jedes Zugs: die von Weiß nach oben, die von Schwarz nach unten.

Ihre Tabs: *Gewinnchancen* (aus dem WDL, wenn die Engines es angeben, sonst nach der Formel von lichess), *S/R/N* (die
Anteile, die die Engines gemeldet haben, die Siege von Weiß unten), *Zeit* (mit der Entscheidungszeit), *Tiefe* (mit
der seldepth), *Knoten*, *Geschwindigkeit*, *TB-Treffer*, *Übereinstimmung* (wie weit die Bewertungen der beiden
Spieler auseinanderliegen, jeder Zug gegen den vorherigen, und die der Analysen in jeder Stellung) und
*PV-Übereinstimmung* (wie viele Halbzüge der PV einer Engine wiederholen, was die der anderen vorhergesagt hat); die,
für die die Partie keine Werte hat, werden weggelassen, und die, die keinen Platz haben, kommen in das Menü `⋯`. Steht
die Maus darüber, nennt die Grafik den Zug und was jede Engine sagte; ein Klick oder ein Ziehen geht zu dem Zug. Ein
Klick auf eine Kurve in der Legende blendet sie aus oder ein; der Pfeil rechts klappt die Grafik ein.

### Dieser Zug
<!-- en: this-move a4a908f8 -->

Unter den Zügen, zum gezeigten Zug: was die Engine, die ihn gespielt hat, sagte (ihre Bewertung, Tiefe und seldepth,
Zeit, Knoten, Geschwindigkeit, Tablebase-Treffer, so wie das PGN sie hat), die Linie, die sie danach erwartete, der
Befund jeder Analyse (der Anteil der verlorenen Gewinnchancen, das Urteil, der beste Zug und seine Bewertung), und mit
den Tablebases das Ergebnis nach dem Zug und ob der Zug das vorherige gehalten hat. *Zeigen* legt eine Linie auf das
Brett, ohne die Partie zu berühren: die Stellung danach, das Brett umrahmt, darüber geschrieben, was es ist; Esc, ein
Klick auf das Brett oder ein Schritt durch die Partie holt die Partie zurück. Auf dem Brett ist das Urteil über den
Zug ein Abzeichen auf seinem Feld, und die Antwort, die seine Engine erwartete, ein gestrichelter Pfeil.

### Varianten
<!-- en: variations 7932794c -->

Ein Zug, der auf dem Brett gespielt wird, wo die Partie anders weiterging, beginnt eine Variante: Die Partie bleibt,
wie sie war, mit ihren Analysen und ihrer Grafik. Die Züge zeigen die Varianten in Klammern nach dem Zug, den sie
ersetzen, verschachtelt und nummeriert; ein Klick auf einen Zug geht dorthin, und *Zurück zur Partie* kehrt zu der
Stellung zurück, an der die Variante abzweigt. Ein Rechtsklick auf einen Zug einer Variante stuft sie hoch (sie wird
zur Linie, von der sie abzweigt, deren Züge danach zur Variante werden), löscht sie samt den von ihr abzweigenden
oder kopiert sie. *PGN kopieren* schreibt die Varianten, und die einer geöffneten PGN-Datei werden gelesen
(Kommentare, Zugnummern, Anmerkungen und Ergebnisse übersprungen; ein Zug, der sich nicht spielen lässt, beendet nur
seine Variante). Die Grafik, die Analysen und die Urteile gehören zur Partie; auf einer Variante markiert die Grafik,
wo sie die Partie verlässt.

### Die Züge, die Tabelle und die Auswertung
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Züge** schreibt die Partie als fließenden Text: Figurinen (siehe [Darstellung](#darstellung)), die Buchzüge grau
und das Ende des Buchs markiert, die Urteile farbig, der aktuelle Zug umrahmt; steht die Maus über einem Zug, nennt
er, was seine Engine sagte, und sein Urteil. **Tabelle** listet jeden Zug mit der Bewertung seiner Engine (aus der
Sicht von Weiß), Tiefe und Zeit, einem Balken für die Zeit, die markierten Züge farbig.

**Auswertung**, sobald die Partie analysiert ist: für jede Seite ihre Genauigkeit (die von lichess: die jedes Zugs aus
den Gewinnchancen, die er verloren hat, wobei die Züge der scharfen Momente mehr zählen), ihre Ungenauigkeiten, Fehler
und groben Fehler, ihr durchschnittlicher Verlust in Centipawns, der Anteil ihrer Züge, die auch die Analyse spielen
würde, die durchschnittliche Tiefe und Zeit ihrer Engine; ihre Genauigkeit in der Eröffnung, im Mittelspiel und im
Endspiel; mit mehreren Analysen die Genauigkeiten, die jede angibt. Dann die Schlüsselmomente, jeder einen Klick
entfernt: der Wendepunkt (der Zug, der am meisten verloren hat, mit dem besten Zug), die anderen Fehler und groben
Fehler, die Stellen, an denen die beiden Engines zwei Züge in Folge um 1,5 Bauern oder mehr uneinig waren, der Beginn
des Endspiels und der der Tablebases. *Wer es zuerst sah* sagt für den Wendepunkt, bei welchem Zug jede Engine, die
gespielt hat, und jede Analyse der Seite, die gewinnt, zum ersten Mal einen Bauern oder mehr gab.
*PGN mit der Analyse kopieren* schreibt die Partie mit der Bewertung jeder Stellung (`[%eval]`), den Urteilen als NAGs
und, nach einem markierten Zug, der Linie, die die Analyse vorzog; *Als BBCode kopieren* liefert die Auswertung für ein
Forum.

### Tablebases auf der Seite Brett
<!-- en: tablebases-on-the-board-page 721b71dd -->

Mit den Syzygy-Ordnern der Einrichtung (*Abschätzung* > Tablebases) fragt die Seite Brett jede Stellung der Partie
ab: Die Grafik färbt die Zone ein, die die Tabellen kennen, *Dieser Zug* nennt das Ergebnis nach dem Zug und ob der
Zug das vorherige gehalten oder verdorben hat, und die Auswertung markiert, wo die Tabellen übernehmen. Jede Stellung
wird abgefragt, als wäre gerade ein Schlagzug oder ein Bauernzug gespielt worden, was genau ist, außer wo die
50-Züge-Regel nahe ist. Die Tabellen sind die des Programmprozesses: Sie werden nicht abgefragt, während ein Turnier
darin spielt.

### Eine Partie aus einer Stellung spielen
<!-- en: playing-a-game-from-a-position 7e83c318 -->

**Eine Partie spielen…** spielt eine Partie aus der Stellung auf dem Brett zwischen zwei Engines der Bibliothek oder
zwischen dir und einer davon (siehe *Mensch* weiter unten) (*Weiß* und *Schwarz*, `<->` tauscht sie samt ihren Uhren),
mit einer Uhr für jede (Minuten und ein Inkrement in Sekunden; *Für beide gleich* oder je eine eigene für Zeitvorgabe)
und, falls angehakt, eine zweite Partie mit vertauschten Farben. Die Engines werden mit den Optionswerten der Bibliothek
gestartet, eine WinBoard-Engine über den Adapter. Das Brett folgt der Partie, während sie gespielt wird (geht man in ihr
zurück, folgt es ihr nicht mehr, bis ihr Ende wieder erreicht ist oder *Zurück zum aktuellen Zug* angeklickt wird), mit
den Uhren, der Suche der Seite am Zug und *Stopp*, das die Partie ohne Ergebnis beendet. Eine Partie endet durch Matt,
Patt, ungenügendes Material, die 50-Züge-Regel, eine dreifache Wiederholung, einen illegalen Zug, eine Engine, die
stehen bleibt, oder durch Zeitverlust (mit 0,1 s Spielraum auf der Uhr, damit der Zug ankommt). Jede Partie wird an
ihrem Ende an eine PGN-Datei in `games/` im Profil angehängt (benannt nach dem Datum und den Engines, mit einem Tag
`Termination`), die *Partien öffnen* auf dem Brett auflistet.

Die Partien werden vom eigenen Engine-Client von fastchess-gui gespielt, unabhängig von jedem Turnier: Sie laufen,
während eines spielt, ohne es zu berühren. Ihre Threads (die der größeren der beiden Engines, da sie abwechselnd
ziehen) zählen zum Kern-Budget; füllen die Partien der laufenden Turniere und die Analyse die Maschine schon aus, sagt
die Karte das und bietet *Trotzdem spielen* statt *Spielen* an. Die Analyse endet, wenn eine Partie beginnt, da sie
mit deren Engines konkurrieren würde.

**Mensch**, als Erstes in den Listen *Weiß* und *Schwarz*, bist du: Du spielst diese Seite selbst; Deine Züge machst du
mit der Maus auf dem Brett, auf dem flachen wie auf dem 3D-Brett (ein Bauer, der die letzte Reihe erreicht, fragt nach
seiner Figur). Das Brett dreht sich bei jeder Partie so, dass deine Seite unten ist (*Drehen* dreht es weiterhin), nimmt
deine Figuren nur an, wenn du am Zug bist (die Karte zeigt *Du bist am Zug*), und ein Klick, während du dir einen
früheren Zug ansiehst, bringt es zur Partie zurück. Deine Uhr läuft wie die einer Engine, Zeitverlust inbegriffen: Gib
dir mehr Zeit als der Engine, indem du *Für beide gleich* abwählst. Während du spielst, wird zurückgehalten, was die
Engine denkt (ihre Bewertung, Tiefe und Variante, also auch der Bewertungsbalken und die Kurve des Graphen); die
beendete Partie hat es, bereit für *Partie analysieren*. *Partie aufgeben* beendet sie jederzeit, gewonnen von der
Engine; mit einer zweiten Partie folgt diese, mit vertauschter Farbe. Deine Partien werden unter deinem Anmeldenamen
gespeichert, und nur die Engine zählt im Kernbudget. Zurücknehmen geht nicht, und die Engine spielt mit voller Stärke,
außer ihre Karte stellt eine geringere ein (`UCI_LimitStrength` und `UCI_Elo` oder `Skill Level`, wo sie diese hat).

## Testsuiten
<!-- en: test-suites 42c2b2ba -->

Die Seite **Suiten** führt eine EPD-Testsuite aus: Stellungen mit ihrem besten Zug (`bm`), den zu vermeidenden Zügen
(`am`) oder, bei der Strategic Test Suite, den Punkten mehrerer Züge (`c0 "f5=10, Be5+=2"`).

- **Suite**: eine EPD-Datei oder ein EPD-Buch der Bibliothek. Die Zeilen ohne legale Stellung oder ohne eine dieser
  Operationen werden weggelassen (ihre Zahl wird angezeigt).
- **Engines**: beliebig viele Engines der Bibliothek mit ihren Optionen; *Hash* und *Threads* erzwingen einen Wert für
  alle (0 = der eigene jeder Engine).
- **Zeit**, **Knoten** oder **Tiefe** pro Stellung. *Gefunden seit (Halbzüge)*: Eine Suche endet, sobald eine Lösung
  über so viele weitere Halbzüge Tiefe der erste Zug der Engine geblieben ist (0 = jede Suche nutzt ihre ganze Zeit).
- **Automatische Parallelität**: mit einer Zeit die physischen Kerne − 1, geteilt durch die Threads der Engines; mit
  Knoten oder einer Tiefe jeder Hardware-Thread.

Eine Stellung ist gelöst, wenn der endgültige Zug der Engine eine Lösung ist: einer aus `bm`, keiner aus `am`, oder,
nur mit Punkten, der Zug mit den meisten. Ihre Lösungszeit ist der Zeitpunkt, an dem sich die Engine auf diesen Zug
festgelegt hat. Das Raster zeigt die Stellungen gegen die Engines, ✓ mit der Lösungszeit oder ✗ mit dem gespielten
Zug, und die STS-Punkte; die Summen stehen vorn: gelöste Stellungen, ihre durchschnittliche Zeit und die STS-Punktzahl.
Ein beendeter Lauf wird in `suites/` im Profil aufbewahrt; *Mit einem gespeicherten Lauf vergleichen…* fügt die
Spalten eines früheren Laufs derselben Suite hinzu, um Versionen einer Engine zu vergleichen. *CSV exportieren…*
schreibt das Raster.

## Neues Turnier
<!-- en: new-tournament 6d169de4 -->

**Turniere → Neues Turnier** öffnet die Einrichtung: eine Karte mit zugeklappten Zeilen, die sagen, wie die Partien
gespielt werden, die Teilnehmer mit der Engine-Bibliothek daneben, und rechts eine laufend aktualisierte
Zusammenfassung (Zahl der Partien, Partien pro Engine, geschätzte Dauer, Eröffnungen, gemeinsame Einstellungen,
Zeitvorgabe). Die Einrichtung des letzten Turniers bleibt erhalten, so dass ein erneuter Test zweier Versionen einer
Engine so geht: A und B wählen, **Start**.

Alles auf der Seite geht auch mit der Tastatur: Tab und die Pfeiltasten wechseln zwischen den Zeilen, den Listen und
den Knöpfen, Enter oder die Leertaste öffnet eine Zeile oder eine Liste, und die Tasten der
[Teilnehmer und der Bibliothek](#teilnehmer-und-bibliothek) funktionieren dort. **Start** legt das Turnier an und
spielt es; **Zur Warteschlange hinzufügen** hebt es für später auf (siehe
[Warteschlange und Neustart](#warteschlange-und-neustart)).

### Die Zusammenfassung und ihre Prüfungen
<!-- en: the-summary-and-its-checks 5609344a -->

Die Zusammenfassung gibt den Speicher an, den die Hashtabellen belegen können: zwei Engine-Prozesse pro gleichzeitig
gespielter Partie und die Engines, die auf ihre nächste Partie warten, jeweils mit ihrem `Hash`. Übersteigt dieser
Bedarf den freien Speicher, warnt sie (das System wird auslagern) und nennt die Engine mit dem größten `Hash`;
übersteigt er den eingebauten Speicher, startet das Turnier nicht. `--run` sagt dasselbe. Lässt das Speicherbudget der
**Einstellungen** weniger Partien gleichzeitig zu als die Parallelität, sagt die Zusammenfassung, wie viele (jede
Partie belegt den `Hash` ihrer beiden Engines und was deren Prozesse sonst noch belegen), und die Zeile
*Parallelität* ebenso; das Gleiche gilt für die Partien mit einer Engine, die eine GPU nutzt, gemessen an den *GPUs*.
Sie nennt außerdem die Engines, deren Programm sich seit ihrem letzten Turnier geändert hat (ein neuer Build), mit
*Was sich geändert hat*, wenn die Karte der Engine neue Standards oder Optionen auflistet (siehe
[Neu gebaute Programme](#neu-gebaute-programme)).

Was schiefgehen würde, wird mit der Engine, der Ressource und der Behebung genannt, einem Knopf unter der Zeile (die
Befehlspalette listet dieselben Behebungen auf):

- ein Programm oder ein Arbeitsverzeichnis, das nicht gefunden wird: *Engine öffnen* (ihre Karte auf der Seite
  Engines), *Suchen…*; ein Eröffnungsbuch, das nicht gefunden wird: *Suchen…*;
- eine Engine, deren eigene `Threads` (ihre Einstellung, sonst ihr Standard: Manche Engines nehmen jeden Kern) über
  der Hälfte der Hardware-Threads liegen oder über den Threads, die jeder Partie bei der Zahl gleichzeitiger Partien
  zustehen (mit einer Uhr gemessen an den physischen Kernen): *Threads auf N senken* setzt die Option der Engine in der Bibliothek, *Engine
  öffnen*. `Threads`, die die Einrichtung über die Maschine hinaus vorgibt, bekommen *Threads auf N senken* dort, wo
  sie vorgegeben werden (die Zelle des Teilnehmers, die gemeinsamen Einstellungen). Die Teilnehmerliste markiert eine
  solche Engine mit einem *!*, die Engines des Matches werden darunter gekennzeichnet;
- ein Optionswert, den eine Engine ablehnt (außerhalb ihres Bereichs, keiner ihrer Auswahlwerte, eine Option, die sie
  nicht mehr hat): fastchess sendet ihn nicht, und die Engine spielt mit ihrem Standard;
- die Syzygy-Tablebases, die das Turnier verwendet: die Ordner der Abschätzung und das, worauf der `SyzygyPath` jeder
  Engine zeigt (mehrere Ordner, unter Windows durch `;` getrennt, sonst durch `:`). Für die verwendete Steinzahl (die
  *Max. Steine* der Abschätzung, sonst die größten gefundenen Tabellen) muss jede WDL-Datei (`.rtbw`) da sein, und
  keine Datei darf abgeschnitten sein (eine Syzygy-Datei ist 16 Bytes länger als ein Vielfaches von 64; Engines lassen eine
  Datei anderer Größe weg, und manche bleiben stehen, wenn sie auf sie stoßen): *Syzygy 6 Steine: 14 Dateien fehlen in
  D:\TB (KRPPvKR.rtbw…)*, *Abschätzung bis 6 Steine, aber nur 5-Steiner-Dateien gefunden*, *SyzygyPath von Alpha zeigt
  auf einen Ordner ohne Tablebase-Dateien*, mit *Ordner öffnen* und *Auf 5 Steine senken*; der Tooltip listet die
  Dateien auf. Das sind Warnungen: Die Abschätzung und die Engines kommen ohne die fehlenden Dateien aus. Nur eine
  Abschätzung ohne Ordner, oder deren Ordner keine Tabelle enthalten, hält das Turnier bei seinem Start an (fastchess
  lehnt sie ab): *Suchen…* wählt einen anderen Ordner. Die Ordner werden im Hintergrund gelesen, und erneut alle paar
  Sekunden, solange die Zusammenfassung angezeigt wird.

Ein abgelehnter Start nennt dieselben Probleme, mit ihren Behebungen und *Protokoll öffnen*. Jedes Problem hat ein
**?**, das seinen Eintrag in der [Fehlerbehebung](#fehlerbehebung) öffnet.

### Profile und Vorlagen
<!-- en: profiles-and-templates df08e65d -->

**Profile.** Die ersten fünf Zeilen, *Format*, *Bedenkzeit*, *Eröffnungen*, *Abschätzung* und
*Engine-Einstellungen*, sind jeweils ein benanntes Profil: Seine Liste wählt eines, die Zeile daneben sagt, was es
spielt („40 Züge in 15 min, wiederholt“, „Remis 40/8/10 cp · TB 6 Steine“; ihr Tooltip gibt dasselbe so an, wie
cutechess und fastchess es schreiben, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), und ✎ oder ein Klick
auf diese Zeile öffnet die Einzelheiten direkt darunter. Eine Änderung nur für dieses Turnier setzt einen Punkt neben
das Profil, und seine Liste und seine Einzelheiten bieten *… aktualisieren* (das Profil übernimmt die Änderung, für
jedes Turnier und jede Vorlage, die es verwendet), *Zurücksetzen* und *Als Profil speichern…* (unter einem neuen
Namen). Das Programm bringt Vorgaben mit:

- Bedenkzeiten: 10+0.1 und 60+0.6 (die kurzen und langen Tests von fishtest), 2+1, 40/4, 40/15 und 40/20 (40 Züge in
  4, 15 oder 20 Minuten, wiederholt, wie Ranglisten sie spielen) und 200k Knoten pro Zug;
- Formate: SPRT Verbesserung [0, 3] und keine Verschlechterung [-5, 0], ein Match über 1000 Partien, ein Rundenturnier,
  ein Gauntlet, ein Schweizer System mit 9 Runden, ein K.-o.-System und ein Mannschaftskampf;
- Abschätzungen: keine, eine Standard-Abschätzung (Remis ab Zug 40 nach 8 Zügen innerhalb von 10 cp, Verlust nach 3
  Zügen bei 600 cp) und dieselbe mit den Tablebases bis 6 Steine;
- Engine-Einstellungen: die eigenen jeder Engine, oder Hash 16, 64 oder 256 MB mit 1 Thread und ausgeschalteten
  eigenen Büchern der Engines;
- Eröffnungen: kein Buch, oder die 960 Stellungen von Chess960.

Ein eigenes Profil, das unter dem Namen einer Vorgabe gespeichert wird, tritt an ihre Stelle; löschst du es (aus seiner
Liste, nach einer Bestätigung), kommt die Vorgabe zurück, und die Einrichtungen und Vorlagen, die es verwendet haben,
behalten seine Werte. Profile werden in `profiles.json` im Profilordner gespeichert, und die Befehlspalette (Strg+K)
listet sie auf: Wählst du eines, wird es auf die Einrichtung angewendet. Die Palette öffnet außerdem jede Zeile der
Einrichtung über ihren Titel und lädt oder speichert Vorlagen. Die Syzygy-Verzeichnisse stehen in keinem Profil: Sie
gehören zu diesem Computer, und die Abschätzung und die Engines teilen sie sich. Die Zeilen *Parallelität* und *PGN und
Sonstiges* enthalten, was nur zu diesem Turnier gehört.

**Vorlage** und **Name**, neben *Start*: Eine Vorlage ist die ganze Einrichtung, ein Profil für jeden Abschnitt plus
das, was kein Profil enthält (der Name, die Teilnehmer, die Parallelität, die PGN-Dateien, die Übertragung). Das Menü
lädt eine oder speichert die Einrichtung als Vorlage (*Einrichtung als Vorlage speichern…*, das nachfragt, bevor es
eine Vorlage gleichen Namens ersetzt), und auch die Befehlspalette lädt sie. Ein Abschnitt, der beim Speichern der
Vorlage seinem Profil folgte, folgt diesem Profil weiter, so dass ein aktualisiertes Profil jede Vorlage aktualisiert,
die es verwendet; ein für jenes Turnier geänderter Abschnitt behält seine eigenen Werte. Vorlagen früherer Versionen
werden so geladen, wie sie waren, und ihre Abschnitte erhalten die Profile, denen sie entsprechen. Ein leerer Name wird
aus den Engines gebildet.

### Format
<!-- en: format 29a36e7c -->

Der Tooltip jedes Formats sagt, was es ist.

- *Match*: zwei Engines, A (die getestete Engine; jede Statistik ist aus ihrer Sicht) und B (die Vergleichsbasis). Eine
  feste Zahl von Partien, bis du es anhältst, oder ein SPRT: elo0, elo1, alpha, beta, Modell (`normalized` verwendet
  nElo-Grenzen wie fishtest und OpenBench, `logistic` klassische Elo, `bayesian` BayesElo) und eine optionale
  Höchstzahl von Partien. Die Profile SPRT Verbesserung und keine Verschlechterung geben die üblichen Grenzen vor, und
  die Zeile unter den Grenzen sagt, wie viele Partien der Test voraussichtlich dauert. *Rechner…* zeichnet, wie der
  SPRT-Rechner von fishtest, die Wahrscheinlichkeit, dass H1 angenommen wird, und die erwartete Zahl der Partien in
  Abhängigkeit vom wahren Elo-Unterschied, aus einer erwarteten Remisquote und dem RMS-Bias der Eröffnungen; *Diese Grenzen
  verwenden* kopiert die ihm gegebenen Grenzen zurück in die Einrichtung. *Elo-Verschiebung* (standardmäßig 0) wird zu
  beiden Grenzen addiert, um gegen eine Version zu testen, die bekanntlich um so viel stärker oder schwächer ist:
  [0, 3] mit einer Verschiebung von 5 ergibt den SPRT [5, 8], den die Zusammenfassung, die Übersicht und der
  Headless-Modus als `SPRT [0, 3] + 5` schreiben; der Rechner und die Zeile unter den Grenzen berücksichtigen sie.
  *Min. Partien* (standardmäßig 0, keine) hält die Entscheidung zurück, bis so viele Partien gespielt sind, während die
  LLR weiter berechnet und angezeigt wird, um einen kurzen Test vor einem frühen Glückstreffer zu schützen: Eine LLR
  jenseits einer Grenze vor diesem Punkt wird unter dem LLR-Balken gemeldet, und das Match geht weiter; sind die
  Partien erreicht, beendet das nächste Paar, das mit der LLR jenseits einer Grenze endet, das Match. Eine Höchstzahl
  an Partien unter der Mindestzahl beendet das Match ohne Entscheidung. Das ist der schnellste Weg, zwei Versionen
  einer Engine zu vergleichen, und mehr braucht es nicht.
  *Farben* (standardmäßig *Abwechselnd*) legt fest, wer Weiß hat: A und B im Wechsel, oder *A immer mit Weiß* oder *B
  immer mit Weiß*, wie fastchess' `-noswap` (und `-reverse`), für Vorgabepartien, in denen die stärkere Engine immer die
  benachteiligte Seite haben muss, oder für eine Eröffnung, die nur von einer Seite erprobt wird. Die Partien gehen
  weiterhin in Paaren, beide mit denselben Farben, aber jede Partie hat ihre eigene Eröffnung, die nächste des Buchs
  (wie fastchess' `-games 1`): Die Paare und ihre Statistiken bleiben, das Ergebnis enthält den Vorteil von Weiß, und
  der RMS-Bias, den das Vertauschen der Farben aufhebt, wird nicht angezeigt. Die Zusammenfassung und der Kopf des
  Matches zeigen es an.
- *Rundenturnier*: Jede Engine spielt gegen jede andere, *Zyklen*-mal, jedes Mal zwei Partien mit vertauschten Farben
  (was cutechess Runden nennt, mit 2 Partien pro Begegnung). Die Runden folgen Berger-Tabellen, so dass jede Engine
  einmal pro Runde spielt.
- *Gauntlet*: Die ersten Engines, so viele wie *Getestete Engines* angibt (die gesetzten), spielen gegen jede andere,
  die anderen spielen nicht gegeneinander.
- *Schweizer System*: eine Zahl von *Runden*. Jede Runde besteht aus zwei Partien mit vertauschten Farben gegen eine
  Engine mit ähnlicher Punktzahl; niemand trifft zweimal auf denselben Gegner, wenn es sich vermeiden lässt. Bei einer
  ungeraden Zahl von Engines bekommt die am niedrigsten platzierte Engine, die noch kein Freilos hatte, eines, das
  einen Punkt wert ist.
- *K.-o.-System*: Die Engines werden in der Reihenfolge ihrer Liste gesetzt, die oben Gesetzten bekommen die Freilose,
  wenn das Feld keine Zweierpotenz ist, und die ersten beiden Gesetzten können sich erst im Finale treffen. Ein Match
  besteht aus so vielen Partiepaaren, wie *Partiepaare pro Match* angibt; ein unentschiedenes Match bekommt jeweils ein
  weiteres Paar, bis zu zehn, danach kommt die höher gesetzte Engine weiter.
- *Mannschaftskampf*: zwei Mannschaften von Engines, um eine Familie von Engines oder eine Reihe von Versionen als
  Ganzes mit einer anderen zu vergleichen. *Paarungen*: *Jeder gegen jeden*, jede Engine einer Mannschaft gegen jede
  Engine der anderen, ein Gegner pro Runde (ein Scheveninger System; die Mannschaften dürfen verschieden groß sein),
  oder *Brett gegen Brett*, die erste Engine jeder Mannschaft gegen die erste der anderen, die zweite gegen die zweite
  und so weiter (gleich große Mannschaften). Jede Paarung wird *Zyklen*-mal gespielt, jedes Mal zwei Partien mit
  derselben Eröffnung und vertauschten Farben; die Farben der ersten Partie wechseln von Brett zu Brett und von einem
  Zyklus zum nächsten. *Mannschaft A* und *Mannschaft B* benennen die Mannschaften („Mannschaft A“ und „Mannschaft B“
  ohne Namen); daneben steht, wie viele Engines jede hat. Die Mannschaften werden in der Teilnehmerliste gebildet: Die
  obere Hälfte der Liste ist Mannschaft A, die untere Mannschaft B (Mannschaft A bekommt die überzählige), bis Engines
  einer Mannschaft zugeteilt werden.

### Teilnehmer und Bibliothek
<!-- en: participants-and-the-library a7ef2e55 -->

**Teilnehmer**, mit der **Bibliothek** daneben (in einem schmalen Fenster darunter). Jeder Teilnehmer ist eine Zeile:
sein Platz (ein Stern für die gesetzten Engines eines Gauntlets), sein Name, seine Referenz-Elo, was er anders spielt
als die gemeinsamen Einstellungen („Hash 512 MB · 80 % der Zeit“) und ⋯, hervorgehoben, wenn er eigene Einstellungen
hat. ⋯, ein Rechtsklick oder die Menütaste öffnet sie: sein eigener `Hash`, eigene `Threads`, feste Knoten oder Tiefe,
Zeitvorgabe (ein Prozentsatz der Bedenkzeit, für eine Uhr), sein eigenes *Buch*, *Diese Engine noch einmal, mit einem
anderen Buch*, die Verschiebe-Befehle und *Entfernen*. Die Reihenfolge der Liste ist die Setzliste: Ziehe Teilnehmer,
um sie umzuordnen (die Gesetzten zuerst), oder drücke Strg+Hoch oder Strg+Runter, um sie ganz nach oben oder unten zu
schicken, Alt+Hoch oder Alt+Runter, um sie um eins zu verschieben. Strg- oder Umschalt-Klick wählt mehrere aus; Entf
(oder *Entfernen*) entfernt sie.

In einem Mannschaftskampf ist der Platz die Mannschaft und das Brett, A1, A2… für Mannschaft A, B1, B2… für
Mannschaft B, in der Reihenfolge der Liste (Brett gegen Brett spielt A1 gegen B1). Das Menü ⋯ teilt den Teilnehmer
einer Mannschaft zu; *Mannschaft A* und *Mannschaft B* unter der Liste teilen die ausgewählten Teilnehmer einer
Mannschaft zu, und *Hälften* bildet die Mannschaften wieder aus den Hälften der Liste (die Befehlspalette bietet
dasselbe). Eine Engine, die nach der Wahl der Mannschaften hinzukommt, tritt der kleineren bei. Die Zusammenfassung
sagt, wie viele Engines jede Mannschaft hat, und ein Start wird mit Grund abgelehnt, wenn eine Mannschaft leer ist
oder, bei Brett gegen Brett, wenn die Mannschaften verschieden groß sind.

Die Bibliothek listet die aktiven Engines mit ihrer Referenz-Elo auf, sortiert nach Name oder nach Elo (Klick auf die
Spalte), ein `!` bei denen mit einem Problem (Programm fehlt oder neu gebaut, letzte Prüfung fehlgeschlagen: der
Tooltip sagt, was) und ein ✓ bei denen, die schon mitspielen. Der Filter durchsucht die Namen, die Namen, die die
Engines melden, ihre Autoren, Programme, Notizen und Gruppen (Strg+F gibt ihm die Tastatur; Enter darin fügt die
ausgewählten Engines hinzu, oder die erste angezeigte); ein Chip pro Gruppe zeigt nur diese Gruppe, und *Alle
angezeigten hinzufügen* fügt die ganze Gruppe hinzu. Doppelklicke eine Engine, oder wähle mehrere mit Strg oder
Umschalt aus (Strg+A: alle angezeigten) und drücke Enter, um sie hinzuzufügen; oder ziehe sie an eine Stelle unter den
Teilnehmern. *Vorschlagen…* wählt die N aktiven Engines aus, die funktionieren und deren Referenz-Elo der des ersten
Teilnehmers (der getesteten Engine eines Gauntlets) am nächsten liegt, standardmäßig höchstens 150 entfernt; Enter
fügt sie hinzu. Bei einem Match füllt die Bibliothek A, dann B (B wird ersetzt, wenn beide gewählt sind), und eine
auf A oder B gezogene Engine nimmt deren Platz ein.

### Eigene Bücher der Teilnehmer
<!-- en: participants-own-books 580e52cc -->

*Buch*: ein eigenes Eröffnungsbuch des Teilnehmers, wie `book=`/`bookdepth=` bei Cute Chess: ein Polyglot- (`.bin`)
oder PGN-Buch der Seite Bücher und wie tief ihm gefolgt wird, in Halbzügen ab Partiebeginn, die Eröffnung des Turniers
eingeschlossen (*alles*: solange das Buch einen Zug hat; bei einem PGN-Buch die ersten 40 Halbzüge seiner Partien).
Ein PGN-Buch wird zuerst in ein Polyglot-Buch umgewandelt: die Züge jeder Partie bis zu dieser Tiefe, jeder Zug
gewichtet nach der Zahl der Partien, die ihn spielen; Partien mit einem illegalen Zug werden weggelassen und gezählt.
Das läuft im Hintergrund, sobald das Buch gewählt ist (Fortschritt auf dem Knopf *Buch*, in seinem Popup und in der
Zusammenfassung), und wird im Ordner `book-cache` des Profils aufbewahrt, bis sich die Datei oder die Tiefe ändert;
ein Start, der vorher kommt, wartet darauf. Ein Buch, das sich nicht vorbereiten lässt, zeigt in seinem Popup, warum,
mit *Erneut versuchen*; jeder Start versucht es ebenfalls erneut. Ein Turnier spielt aus dem Buch, das beim Start oder
Fortsetzen vorbereitet war, auch wenn sich die PGN-Datei inzwischen ändert.

Solange die Stellung in diesem Buch steht, spielt der Teilnehmer sofort den Zug des Buchs, gewählt nach den Gewichten
des Buchs, so wie der *Zufall* der Eröffnungen sie gestaltet (auch im Popup *Buch*, gleich für jedes Buch des
Turniers: siehe [Eröffnungen](#eröffnungen)); dann übernimmt seine Engine. Die Eröffnungen des Turniers, falls
vorhanden, werden zuerst gespielt, von jeder Engine; EPD-Bücher liefern nur diese und sind in der Liste ausgegraut. Um
Bücher mit einer Engine zu vergleichen, wähle *Diese Engine noch einmal, mit einem anderen Buch* in ihrem Menü ⋯:
Jede Kopie spielt als `<engine> (<book>)`, so dass ihre Ergebnisse und ihre Wertung getrennt bleiben. In einem Match
sitzen die Knöpfe *Buch* neben A und B, und eine Engine kann mit zwei Büchern gegen sich selbst spielen. Buchzüge
werden im PGN mit `{book}` markiert; steht die Maus über einem Namen in der Tabelle, erscheint sein Buch.
WinBoard-Engines können auch eines haben.

### Engine-Einstellungen
<!-- en: engine-settings 7a93d075 -->

*Hash* und *Threads*, für jede Engine erzwungen, auch in einem Match (0 = die eigenen jeder Engine; die eines
Teilnehmers aus seinem Menü ⋯ gehen vor); *Syzygy-Tablebases für die Engines* gibt die Tablebase-Verzeichnisse
(dieselben wie die der Abschätzung) an jede Engine mit einer Option `SyzygyPath`, und *Eigene Bücher der Engines aus*
setzt `OwnBook` auf false, wo es existiert, so dass jede Partie aus den Eröffnungen des Turniers gespielt wird. Beide
sind standardmäßig aus.

### Bedenkzeit
<!-- en: time-control 672cb041 -->

Grundzeit und Inkrement in Sekunden, optional Züge pro Periode, und eine Toleranz, bevor eine Partie durch Zeit
verloren ist; eine feste Zeit pro Zug (`go movetime`, mit derselben Toleranz); eine feste Zahl von Knoten pro Zug; oder
eine feste Tiefe in Halbzügen (`go depth`), bei der keine Engine durch Zeit verlieren kann.

### Parallelität
<!-- en: concurrency c02ee2cf -->

Die Zahl der gleichzeitig gespielten Partien. Im automatischen Modus wird sie beim Start ermittelt:

- feste Knoten oder Tiefe: eine Partie pro Hardware-Thread, da Hyper-Threading die gespielten Züge nicht ändert;
- mit einer Uhr: physische Kerne − 1, damit sich die Engines nicht gegenseitig bremsen.

Die Zeile der Einrichtung und der Hinweis unter dem Feld sagen, welche Regel die Zahl bestimmt hat. *CPU-Affinität* (standardmäßig aus;
nicht unter macOS, das keine hat) bindet jede Partie an einen Prozessor, den sich ihre beiden Engines teilen, da sie
nie gleichzeitig denken. fastchess unterstützt das nur für Engines mit einem Thread, daher wird es automatisch
ausgeschaltet, wenn eine Engine `Threads` > 1 hat. Eine Warnung erscheint, wenn Parallelität × Threads die
Hardware-Threads übersteigt (oder, mit einer Uhr, die physischen Kerne). *Spielen auf* wählt die Maschine, auf der das
Turnier spielt (siehe [Ein Turnier auf einer Maschine](#ein-turnier-auf-einer-maschine)).

### Eröffnungen
<!-- en: openings 44656411 -->

Ein EPD-, PGN- oder Polyglot-Buch (`.bin`), also eine Eröffnungssammlung, zufällige oder sequenzielle Reihenfolge, die
Zahl der aus einem PGN-Buch übernommenen Halbzüge, der Startindex und der Zufallsstartwert (0 = eine neue Reihenfolge
für jedes Turnier). Jede Eröffnung wird zweimal gespielt, mit vertauschten Farben (in einem Match, dessen *Farben*
festgelegt sind, jede Partie aus ihrer eigenen Eröffnung).

Einem Polyglot-Buch wird von der Ausgangsstellung bis zu einer Tiefe gefolgt (8 Halbzüge, sofern nicht anders
eingestellt), wobei die Züge wegfallen, die weniger als ein Zehntel des besten Zugs ihrer Stellung wiegen; die
erreichten Stellungen werden einmal in `<book>-<N>plies.epd` im Turnierordner geschrieben und wie ein EPD-Buch
gespielt. In zufälliger Reihenfolge werden sie in der Reihenfolge geschrieben, in der sie gezogen werden, jede einmal:
Eine Linie kommt umso früher, je wahrscheinlicher sie nach den Gewichten ihrer Züge ist, so dass ein Turnier, das nur
einen Teil des Buchs spielt, zuerst seine Hauptlinien spielt; derselbe Startwert zieht dieselbe Reihenfolge erneut. In
sequenzieller Reihenfolge behalten sie die Reihenfolge des Buchs. *Zufall* (−100 bis 100, standardmäßig 0) legt fest,
wie die Gewichte zählen, so wie Polyglot und Cute Chess einen Buchzug wählen: 0 die Gewichte, wie sie sind, −100 nur
den besten Zug jeder Stellung (eine Linie, es sei denn, mehrere teilen das beste Gewicht), 100 jeden Zug gleich (die
seltenen Züge kommen zurück); dazwischen quadriert −50 die Gewichte, und 50 nimmt die Quadratwurzel aus ihnen. Er wählt auch
die Züge der eigenen Bücher der Teilnehmer und wird für sie auch bei einem EPD- oder PGN-Buch angezeigt; die
Zusammenfassung nennt ihn, und die Schlusszeile der Tabellenkopien gibt das Buch mit seiner Tiefe an und, bei einem
Polyglot-Buch, diesen Zufall.

In einem Rundenturnier, einem Gauntlet, einem Schweizer System oder einem Mannschaftskampf gibt *Dieselbe Eröffnung
für alle Paare einer Runde* jedem Partiepaar einer Runde dieselbe Eröffnung (bei Brett gegen Brett ist ein Zyklus eine
Runde: Jedes Brett spielt dieselbe Eröffnung), für jede Runde die nächste des Buchs, so dass keine Engine leichtere
Eröffnungen bekommt als die anderen ihrer Runde (eine später hinzugefügte Engine holt in eigenen Runden auf, jede mit
ihrer Eröffnung).

*Unausgeglichene Eröffnungen markieren* (standardmäßig aus) zeigt, während die Partien eintreffen, die Eröffnungen, die
die Partie von sich aus entscheiden: Eine Partie wird bei ihrem Ende markiert, wenn beide Engines für jeden ihrer
ersten Züge nach dem Buch (standardmäßig 4; die Buchzüge aus dem eigenen Buch eines Teilnehmers zählen als Buchzüge)
dieselbe Seite mit mindestens der Bewertung (standardmäßig 200 cp) vorne sahen. Die Option liest die Bewertungen der
Engines, eine Engine, die keine angibt, markiert also nichts; die Ergebnisse werden nicht geändert. Der Tab *Partien* filtert
solche Partien (*Unausgeglichene Eröffnung*), der Tab *Eröffnungen* markiert ihre Eröffnungen mit einem `!` und zählt
sie, und die Übersicht zählt die Partien. *Das Paar mit einer anderen Eröffnung neu spielen* annulliert ein solches
Paar, sobald seine beiden Partien gespielt sind (sie bleiben in den PGN-Dateien und im Journal, wie jede annullierte
Partie), und spielt es neu aus einer Eröffnung am Ende des Buchs (in der Spielreihenfolge des Buchs), die das Turnier
zuletzt spielen würde, so dass sich die Eröffnungen der kommenden Paare nicht verschieben; höchstens dreimal für ein
Paar, danach zählt das Paar, markiert. Die Konsole und die Übersicht sagen, wie viele Eröffnungen ersetzt wurden.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer Random, in den Eröffnungen: ohne Buch die 960 Startstellungen in zufälliger Reihenfolge (einmal in
`chess960.epd` im Turnierordner geschrieben); mit einem Buch seine Stellungen. Jede Engine muss Chess960 spielen:
UCI-Engines mit der Option `UCI_Chess960`, WinBoard-Engines mit der Variante `fischerandom`. Die Zusammenfassung nennt
die, die es nicht tun, und ein solches Turnier startet nicht.

### Abschätzung
<!-- en: adjudication a18dca1e -->

Remis (ab einer Zugnummer, für N aufeinanderfolgende Züge, unter einer Bewertung), Aufgabe (N aufeinanderfolgende Züge
über einer Bewertung, optional mit Zustimmung beider Engines), eine Höchstzahl von Zügen und Syzygy-Tablebases.

### PGN und Sonstiges
<!-- en: pgn-and-misc 6762bab8 -->

- Name der Veranstaltung: Sein Tooltip nennt die beiden PGN-Dateien, in die jede Partie geschrieben wird.
- *Jede Partie zusätzlich anhängen an*: eine eigene PGN-Datei, an die jedes Turnier seine Partien anhängt, wie
  `-pgnout` bei cutechess (nie überschrieben, im Kommentarstil ChessBase / Lichess oder cutechess), in der Reihenfolge
  des Spielplans: die beiden Partien einer Eröffnung nebeneinander, die Runden der Reihe nach, egal in welcher
  Reihenfolge sie enden. Eine Partie wartet dort, bis die vor ihr geschrieben sind; ein Fortsetzen macht weiter; lässt
  sich die Datei nicht schreiben (gesperrt, verschoben), warten die Partien, und das Turnier geht weiter, mit einer
  Warnung in der Übersicht. Ein Absturz oder ein Stromausfall während des Anhängens schreibt Partien nie doppelt: Das
  Fortsetzen prüft die Datei und vervollständigt einen abgebrochenen Schreibvorgang, und es entfernt nie etwas aus ihr.
- Zusätzliche Felder in den Zugkommentaren (nodes, seldepth, nps, pv, tbhits und die Entscheidungszeit `dt=`: wann
  sich die Engine auf den gespielten Zug festgelegt hat; `wdl=W/D/L` wird für die Engines, die es melden, immer
  geschrieben), standardmäßig an, da die Diagramme des Tabs *Partien* sie lesen (pv macht das PGN merklich größer).
- *Eine abgestürzte Engine neu starten* und *Das Turnier anhalten, wenn eine Engine nicht startet* (standardmäßig aus:
  siehe [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann)).
- Das Engine-Log jeder Partie behalten (für die Partien, die schlecht enden, wird es immer behalten).
- *Matt-PVs prüfen*: Die PV einer Mattbewertung muss die richtige Länge haben und im Matt enden. Jede PV wird auf
  illegale Züge geprüft, auf Züge nach dem Ende der Partie und auf einen besten Zug, der nicht ihr erster Zug ist; die
  Züge mit einer falschen PV werden im Tab Partien markiert und ins Protokoll geschrieben.

### Tipps für die Einstellungen
<!-- en: tips-for-the-settings 62afdb4d -->

- **Zwei Versionen einer Engine vergleichen**: ein SPRT, Verbesserung [0, 3] für eine Änderung, die etwas bringen
  soll, keine Verschlechterung [-5, 0] für eine Vereinfachung oder ein Aufräumen. Zuerst eine kurze Bedenkzeit
  (10+0.1), dann eine lange (60+0.6) für das, was besteht. Verwende ein Buch mit ausgeglichenen Stellungen, nicht die
  Ausgangsstellung: Sonst wäre jede Partie eine von wenigen Eröffnungen.
- **Parallelität**: Lass sie automatisch. Mit einer Uhr bremsen sich die Engines bei mehr Partien als physischen
  Kernen weniger einem gegenseitig, und die Ergebnisse verschieben sich; mit Knoten oder einer Tiefe pro Zug darf jeder
  Hardware-Thread spielen.
- **Threads**: 1 pro Engine zum Testen; viele Partien mit je einem Thread liefern mehr Ergebnisse pro Stunde als wenige
  Partien mit vielen Threads. Gib jeder Engine dieselben `Threads` und denselben `Hash` unter
  *Engine-Einstellungen*.
- **Hash**: genug für die Bedenkzeit (16 bis 64 MB bei 10+0.1, 256 MB bei 40/15 und länger); die Zusammenfassung sagt,
  wenn der Hash der Partien nicht mehr in den Speicher passt.
- **Abschätzung**: Das Standardprofil spart in langen Turnieren Zeit, ohne die Ergebnisse stark zu verändern; lass sie
  aus, um die Endspiele einer Engine zu testen, oder verwende die Tablebases.
- **Eröffnungen**: Zufällige Reihenfolge mit festem Startwert macht einen Test exakt wiederholbar; *Dieselbe Eröffnung
  für alle Paare einer Runde* ist in einem Rundenturnier mit wenigen Zyklen fairer.
- **Mehrere Turniere**: Gib dem wichtigsten ein größeres Gewicht, statt die anderen zu pausieren (siehe
  [Mehrere Turniere gleichzeitig](#mehrere-turniere-gleichzeitig)).

## Ein Turnier durchführen
<!-- en: running-a-tournament 9bcba803 -->

Ein laufendes oder geöffnetes Turnier hat einen Arbeitsbereich mit einem Kopf (Name, Fortschritt, *Pause*, *Stopp*,
*Klonen*, *Ordner*, *Layout*) und Bereichen, standardmäßig als Tabs gezeigt:

| Tab | Was er zeigt |
|---|---|
| Übersicht | Die bisherigen Ergebnisse und die Statistik, die Diagramme, die Checkliste vor dem Einsenden |
| Tabelle | Die Tabelle als Liste oder als Kreuztabelle, die Gegner einer Engine, die Kopien für ein Forum |
| Mannschaften | Ein Mannschaftskampf: der Stand der Mannschaften und die Kreuztabelle der beiden Mannschaften |
| Turnierbaum | Ein K.-o.-Turnier: der Turnierbaum, Runde für Runde |
| Spielplan | Jedes Partiepaar, und die Änderungen am Turnier |
| Übertragung | Live-Dateien, der TLCS-Server, die Webseite und lichess |
| Live | Die laufenden Partien |
| Partien | Die beendeten Partien, ihre Filter, ihr Nachspielen und ihr Export |
| Eröffnungen | Die Ergebnisse nach Startstellung |

### Übersicht
<!-- en: overview 6985cebb -->

**Ein Match**: Elo und nElo mit ihrem 95-%-Konfidenzintervall, LOS, Punktzahl, Pentanomial-Zählungen, SPRT-Grenzen und
LLR. *Statistik* (zugeklappt) gibt die Zahlen von fishtest aus den Partien und aus den Paaren an: Punktzahl, Elo, nElo
und BayesElo mit DrawElo, Standardabweichung pro Partie, die LLR von beiden, das Varianzverhältnis von Paaren zu
Einzelpartien, den RMS-Bias der Eröffnungen, die Arten ausgeglichener Paare und die gewonnenen gegenüber den verlorenen
Paaren, die *Kopieren* mitnimmt. Dann die Diagramme: Elo und LLR im Zeitverlauf, Partielängen, wie Partien endeten, und
*Suche pro Engine*.

**Ein Turnier**: die Tabelle (mit *TF*, den Zeitverlusten jeder Engine, orange, sobald es einen gibt; ein *!* neben
einer Engine, die abgestürzt ist oder einen illegalen Zug gespielt hat), *Suche pro Engine* darunter, der Fortschritt
nach Zyklus oder Runde, Partien pro Stunde und verbleibende Zeit, die neuesten Ergebnisse.

*Suche pro Engine* gibt für jede Engine den Schnitt über die Züge, die sie gesucht hat, ohne Buchzüge: Tiefe, NPS, Zeit
pro Zug, Entscheidungszeit und Prozessornutzung; ein Klick auf eine Spalte sortiert die Engines danach. Neben ihren NPS
sagt ×4, dass die Engine mit 4 Threads sucht: Sie zählt mehr Knoten als mit einem, ohne deshalb schneller zu sein, also
vergleiche ihre NPS mit Engines, die ebenso viele haben. Die NPS brauchen Engines, die sie angeben, und *nps* in *PGN
und Sonstiges*, um beim Fortsetzen zurückgelesen zu werden.

Beide zählen die Partien aus unausgeglichenen Eröffnungen und die ersetzten Eröffnungen, wenn die Einrichtung nach
ihnen sucht, mit *Zeigen* für diese Partien. Ein beendetes Turnier fügt eine *Checkliste vor dem Einsenden* hinzu: alle
Partien gespielt, jedes Paar mit beiden Farben, keine doppelten Partien, keine Abstürze, Zeitverluste oder illegalen
Züge (mit Knöpfen, die diese Partien öffnen), der Hash jeder Engine (angezeigt, nicht bewertet), Ponder aus, das
Eröffnungsbuch, die Tablebases.

### Tabelle
<!-- en: standings e4198fcd -->

Punkte, Partien, Zeitverluste (*TF*), Siege/Remis/Niederlagen, Wertungen mit ihrem Konfidenzintervall,
Sonneborn-Berger (Buchholz beim Schweizer System), Freilose. *Tabelle* listet sie mit einer Engine pro Zeile auf;
*Kreuztabelle* (Rundenturniere, bis zu 200 Engines) zeigt jede Paarung. *Reihenfolge:* nach Punkten, Punktzahl % oder
Setzliste (die die Spalten der Kreuztabelle eines Gauntlets an ihrem Platz hält). Ein Klick auf eine Engine listet
darunter ihre Gegner auf: Partien, mit Weiß, mit Schwarz, + = −, Punkte und %, wobei die Paare markiert sind, von denen
nur eine Partie gespielt wurde; ein Gauntlet öffnet sich mit seiner getesteten Engine ausgewählt.

*Kopieren* als Text oder als BBCode für einen Forumsbeitrag (mit einer Zeile, die Format, Bedenkzeit, Partien und
Buch angibt), und in einem Gauntlet *Gauntlet-Tabelle als Text* oder *Gauntlet-Tabelle als BBCode, für ein Forum* (die
getestete Engine, dann ihre Gegner nach ihren eigenen Punkten, jeder mit Punkten, %, Elo ±, W-L-D); *HTML
exportieren…*. Alle haben *TF* wie die Tabelle des Headless-Modus, folgen der gewählten Reihenfolge und enden mit
einer Zeile, die die gespielten von den geplanten Partien angibt, die Remisquote, die Bedenkzeit, das Buch (mit den
übernommenen Halbzügen bei einem PGN-Buch; bei einem Polyglot-Buch seine Tiefe und wie seine Züge gewählt wurden, z. B.
`book:Perfect2023.bin (8
plies, moves by weight)`) und die Veranstaltung. Die Kopien unterschreiben dann mit *fastchess-gui* und seiner
Version, und die Gauntlet-Tabelle ist mit = statt mit - liniert, damit sich eine in einem Forum gepostete Tabelle von
denen anderer Programme unterscheiden lässt.

### Mannschaften
<!-- en: teams 8c845645 -->

Nur bei einem Mannschaftskampf, im Tab *Tabelle* (wo er sich öffnet) und oben in der *Übersicht*: der Stand der
Mannschaften, „Lynx 17½ – 18½ Otter“, mit den gespielten Partien, der Punktzahl % von Mannschaft A, dem Elo-Unterschied,
für den diese Punktzahl steht, mit seinem 95-%-Intervall und der LOS, über die Partiepaare gezählt, so wie die
Statistik eines Matches (pentanomial); dann die Engines jeder Mannschaft mit ihren Partien, Punkten, %,
Wertung und den Punkten, die jede gegen jede Engine der anderen Mannschaft erzielt hat (B1, B2… als Spalten, der Name
unter dem Mauszeiger): die Kreuztabelle der beiden Mannschaften. *Tabelle* und *Kreuztabelle* zeigen die Engines wie
in jedem Turnier. *Kopieren* und *HTML exportieren…* geben die Stände der Mannschaften und die Engines beider
Mannschaften gegeneinander an, mit der Zeile darunter.

### Turnierbaum und Spielplan
<!-- en: bracket-and-schedule fe207ac0 -->

*Turnierbaum*, nur bei einem K.-o.-Turnier: der Turnierbaum, Runde für Runde, mit dem Stand jedes Matches.
*Spielplan*: jedes Partiepaar nach Zyklus oder Runde, mit seinem Zustand und der Eröffnung, die es spielt (sobald es
begonnen hat, oder sobald seine Runde begonnen hat, wenn sich die Runde eine teilt), und die Änderungen am Turnier
(siehe [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern)).

### Live
<!-- en: live 5561ed9b -->

Die laufenden Partien, als Mosaik von Brettern oder als eine Partie im Fokus mit den Uhren, der laufenden Suche jeder
Engine (Tiefe, Bewertung, Knoten, nps, PV in SAN), der Zugliste und der Partiegrafik (siehe
[Die Partiegrafik](#die-partiegrafik)): die Bewertungen der beiden Engines und in ihren Tabs ihre Gewinnchancen, die
Zeit pro Zug mit der Entscheidungszeit, die Tiefe mit seldepth, die Knoten, die Geschwindigkeit, die
Tablebase-Treffer, wie weit sie auseinanderliegen, oder wie viele Halbzüge eine PV von dem wiederholt, was die PV der
anderen Engine vorhergesagt hat. Die Anteile für Sieg/Remis/Niederlage (`UCI_ShowWDL`, beim Hinzufügen für die Engines
eingeschaltet, die es haben) folgen jeder Suchzeile; der Tab *S/R/N* zeichnet sie als Bänder, die Siege von Weiß
unten.

Wenn der Platz reicht (eine Partie im Fokus, ein großes Panel), hat jede Engine eine Karte: ihr Logo so hoch wie Name
und Uhr, was sie tut (sie denkt nach, oder der Zug, den sie spielte), ihre Suche Zahl für Zahl (Tiefe und seldepth,
Bewertung, Knoten, Geschwindigkeit, die Tablebase-Treffer und die Hash-Füllung, wenn die Engine sie sendet, die bisher
auf den Zug verwendete Zeit) und ihre ganze PV; die wartende Engine zeigt, was sie beim Zug sagte, mit dem Moment, in
dem sie sich entschied. Neben dem Mosaik behält das Panel eine Zeile mit Zahlen und die ersten 12 Halbzüge der PV.
*Bewertung für die Seite am Zug* | *Bewertung für Weiß* rechts im Tab legt die Seite aller Bewertungen darin fest: die
der Seite am Zug, wie fastchess sie ins PGN schreibt, oder die von Weiß; die Bewertungsleiste und das Großbild zeigen
immer die Sicht von Weiß.

Über den Zügen: die Eröffnung, der ECO-Code und der Name der tiefsten Stellung der Partie, die die Tabelle der benannten
Eröffnungen kennt (Buch- und Engine-Züge gleichermaßen, sodass auch eine Zugumstellung nach dem Buch benannt wird), mit
dem letzten Buchzug; und sobald die Syzygy-Tablebases des Turniers (die seiner Abschätzung, sonst die den Engines
gegebenen) die gezeigte Stellung kennen, ihr Urteil: die Seite, die gewinnt, mit der DTZ (die Halbzüge bis zum nächsten
Schlag oder Bauernzug: Syzygy-Tablebases kennen keine Distanz zum Matt), oder Remis, die 50-Züge-Regel nach dem Zähler
der Partie gezählt, außer die Abschätzung ignoriert sie. Jede Stellung wird einmal abgefragt, außerhalb der Threads, die
die Partien spielen. In der Liste hat jeder Engine-Zug seine Bewertung, Tiefe und Zeit; beim Darüberfahren kommen der
Moment, in dem sich die Engine entschied, und die Antwort hinzu, die sie erwartete (der zweite Zug ihrer PV: fastchess
spielt ohne Pondern). Buchzüge sagen *Buch*, eine Zeile markiert das Ende des Buchs, und der Zug, über den nachgedacht
wird, sagt *denkt nach*.

Das Mosaik hat ein Brett je laufender Partie, nicht je Partie, die die Parallelität erlaubt: gegen Ende eines Turniers
oder mit weniger Partien als das werden die übrigen Bretter größer. Ein Brett, dessen Partie beendet ist, bleibt 10
Sekunden (die nächste Partie seines Platzes beginnt dort), das der verfolgten Partie bleibt, und wenn nichts spielt,
bleiben die letzten Partien. Bräuchten die Bretter einen Rollbalken, nimmt das Mosaik der Partie im Fokus Breite weg,
bis ihr 40 % der Seite bleiben.

Unter dem Brett: der 50-Züge-Zähler (Halbzüge seit dem letzten Schlagen oder Bauernzug, von 100), der
Materialunterschied mit den Figuren, die eine Seite mehr hat, und die verbleibenden Steine, mit *TB* markiert, wenn die
Tablebase-Abschätzung die Partie beenden kann; *Details unter den Brettern* fügt sie dem Mosaik hinzu. Darunter die
Knöpfe durch die Züge (siehe [Bretter und ihre Knöpfe](#bretter-und-ihre-knöpfe)): Die Partie im Fokus oder ein
abgetrenntes Brett lässt sich zurückblättern, während die Partie weiterläuft (auch mit ← → Pos1 Ende, einem Klick auf
einen Zug der Liste oder auf die Kurve), wobei die frühere Stellung mit ihrem Zug in der Liste und auf der Kurve
markiert gezeigt wird; *Zurück zum aktuellen Zug* (oder Ende, oder ein Klick auf den letzten Zug) lässt es der Partie
wieder folgen, wie bei jeder neuen Partie.

*Pfeile* (standardmäßig an) zeichnet den Zug, an den die Seite am Zug denkt, in der Farbe ihrer Engine und die
erwartete Antwort, heller, in der Farbe des Gegners, auf dem Brett im Fokus und den abgetrennten (nicht im Mosaik),
wenn es den aktuellen Zug zeigt. In einem Gauntlet zeigt *Getestete Engine unten* (standardmäßig an) jede neue Partie
von der Seite der getesteten Engine; Drehen (F oder sein Knopf) dreht das Brett weiterhin bis zur nächsten Partie um.
*Abtrennen* öffnet ein Brett in einem eigenen Fenster. Ein Rechtsklick auf ein Brett beendet seine Partie mit einem
Ergebnis (siehe [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern)). *Großbild* zeigt die verfolgte Partie
fensterfüllend (siehe [Großbild](#großbild)).

### Großbild
<!-- en: big-screen 14c8f0d6 -->

Für einen Fernseher, einen Verein oder einen Stream zeigt *Großbild* (im Tab *Live*, auf einem abgetrennten Brett
oder in der Befehlspalette) eine Partie fensterfüllend, hell auf Schwarz: die Namen und Uhren der Spieler in großer
Schrift (die Seite am Zug und ihre laufende Uhr in Gelb, eine Uhr unter zehn Sekunden in Rot), die Bewertungsleiste,
die Bewertung, Tiefe und Geschwindigkeit der Suche, den letzten Zug auf dem Brett umrahmt und groß geschrieben, die
Eröffnung, die Linien der Engines (*Linien*, standardmäßig an: die der Seite am Zug, während sie sucht, in Gelb, die
der anderen von ihrem letzten Zug, höchstens zwei Zeilen je), das Urteil der Tablebases, sobald sie die Stellung
kennen, und die Züge in großer Schrift, der letzte markiert; das Ergebnis über dem Brett, sobald die Partie vorbei ist. Sein Text folgt
der Größe des Fensters und hat eine eigene Größe (*A-* / *A+* oder die Tasten + und -). Es zeigt die verfolgte Partie,
wechselt alle N Sekunden zwischen den laufenden Partien (*Jede Partie der Reihe nach*) oder folgt *Die
interessanteste*: der Partie, die dem Ausgleich am nächsten ist (unter gleich nahen der längsten), und bleibt bei ihr,
solange das so bleibt. Die
Bedienelemente erscheinen drei Sekunden lang, wenn sich die Maus bewegt (der Mauszeiger verschwindet mit ihnen); ← und
→ wechseln die Partie, F11 schaltet das Fenster auf Vollbild, Esc verlässt es. Ein auf einen anderen Bildschirm (den
Fernseher) verschobenes abgetrenntes Brett zeigt es in seinem eigenen Fenster, wobei F11 dieses Fenster auf Vollbild
schaltet, während das Hauptfenster weiterläuft; im Hauptfenster angedockt, zeigt das Hauptfenster es.

### Partien
<!-- en: games ba4a9604 -->

Die beendeten Partien mit ihrer Eröffnung (ECO-Code und Name) und ein Brett zum Nachspielen: die Knöpfe darunter,
← → Pos1 Ende und F, oder ein Klick auf einen Zug oder auf die Grafik; dieselben Details darunter und dieselbe
Partiegrafik wie in *Live* (siehe [Die Partiegrafik](#die-partiegrafik)), aus den Feldern, mit denen das PGN
geschrieben wurde: Ein Diagramm, dessen Feld das Turnier nicht geschrieben hat, steht ausgegraut im Menü `⋯` der
Grafik und sagt das, mit einem Knopf zu den PGN-Optionen der Einrichtung.

**Filter**: nach Ergebnis (Siege, Remis, Niederlagen, Zeitverluste, Abstürze und illegale Züge, Abschätzungen, falsche
PVs, und die Partien einer als unausgeglichen markierten Eröffnung, wenn die Einrichtung nach ihnen sucht); nach dem,
was die Bewertungen sagen (die Engines sind sich 3 Züge in Folge um je N Bauern uneinig, die Bewertung einer Engine
springt von einem ihrer Züge zum nächsten um N Bauern, der Verlierer glaubte, N Bauern besser zu stehen; die Partie
öffnet sich an dieser Stelle); und nach Eröffnung (ein ECO-Code oder sein Anfang, ein Bereich wie `B20-B99`, oder
Wörter des Namens).

**Export** einer, aller oder der gefilterten Partien: *Alle exportieren…* schlägt `<name>.pgn` neben dem Ordner des
Turniers vor; die eigenen PGN-Dateien des Turniers werden als Ziel abgelehnt. *Für eine Rangliste exportieren…*
schreibt die Partien so, wie Ranglisten-Tester (z. B. CCRL) sie einsenden:
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, auf Wunsch auch als Zip; die Spieler so benannt, wie
die Listen sie schreiben, `<engine> 64-bit 4CPU` (für jeden Teilnehmer änderbar), die Runden neu nummeriert, die Tags
`Event` und `Site` gesetzt, die annullierten Partien und die Duplikate weggelassen, und die durch einen Absturz oder
Hänger verlorenen ebenso, sofern nicht abgehakt; der `PlyCount` einer durch einen illegalen Zug verlorenen Partie
zählt nur die geschriebenen Züge. Der Tester und der Ort werden gespeichert.

*Engine-Log* öffnet das Log einer Partie, die eines hat (siehe [Dateien](#dateien)). Ein Rechtsklick auf eine Partie
spielt sie neu, annulliert sie oder setzt ihr Ergebnis (siehe [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern)).

### Der Tab Eröffnungen
<!-- en: the-openings-tab c51dd29a -->

Ergebnisse nach Startstellung: ihre Partiepaare, die Punktzahl von Weiß, die Remis, und wie viele Paare Weiß zweimal
gewonnen hat, Schwarz zweimal (die Eröffnung bevorzugt eine Farbe) oder eine Engine zweimal (sie unterscheidet die Engines),
mit der durchschnittlichen Länge. Sortierbar nach jeder Spalte, gefiltert auf die unausgeglichenen, die remisierten
oder die entschiedenen, oder auf den Filter *Während der Partien als unausgeglichen markiert* (siehe
[Eröffnungen](#eröffnungen) in der Einrichtung: mit `!` markiert, ihre Zahl neben der Gesamtzahl, mit den ersetzten
Eröffnungen). *Als EPD exportieren…* schreibt die gezeigten Stellungen, um sie aufzubewahren oder ein Buch zu
bereinigen, wobei *Ohne die unausgeglichenen* (standardmäßig an, gezeigt, sobald eine Eröffnung markiert ist) die
markierten weglässt. Ein Klick zeigt die Stellung und ihre Partien. In einem Match, dessen *Farben* festgelegt sind, hat
jede Partie ihre eigene Eröffnung: gezählt werden die Partien, und die Spalten und Filter zu den Paaren werden nicht
angezeigt.

### Bereiche und Layouts
<!-- en: panels-and-layouts 9ed9b915 -->

Jeder Tab ist ein Bereich. Zieh einen an seinem Tab, um ihn an einer Seite eines anderen Bereichs oder als Tab daneben
anzudocken, oder lass ihn woanders los, damit er frei schwebt (auch auf einen zweiten Bildschirm unter Windows und
Linux/X11, nicht unter Wayland und nicht mit Software-Rendering). Die verfolgte Partie in *Live* zerfällt in die
Bereiche *Brett*, *Spieler*, *Suchen*, *Züge* und *Diagramm*; die Teile, deren Bereich nicht geöffnet ist, bleiben
neben dem Mosaik in *Live*, und abgetrennte Bretter docken wie die anderen Bereiche an. Schließe einen Bereich mit dem
x auf seinem Tab; hol ihn über *Layout* > *Bereiche* oder mit *… anzeigen* in der Befehlspalette zurück, dann erscheint
er angedockt.

*Layout* bietet außerdem fertige Layouts an (*Reiter*, wie standardmäßig; *Eine Partie verfolgen*: das Brett links,
die Spieler, Suchen und das Diagramm rechts, die Tabelle darunter; *Großes Turnier*: das Mosaik neben der Tabelle;
*Übertragung*: ein großes Brett mit den Spielern, Suchen und der Tabelle), *Layout speichern unter…* (deine eigenen,
nach Namen, in `layouts.json` im Profil gespeichert), *Löschen* und *Standard-Layout*. Ein aus dem Arbeitsbereich
herausgenommener Bereich bleibt auf den anderen Seiten auf dem Bildschirm; die Anordnung bleibt zwischen den Sitzungen
erhalten.

### Prozessornutzung
<!-- en: processor-use e5234cbe -->

Während Partien gespielt werden, wird die Prozessorzeit der Prozesse jeder Engine (einschließlich der Engine hinter
einem WinBoard-Adapter) bei jedem `go` und jedem Zug gemessen. Die Übersicht zeigt für jede Engine die Kerne, die sie
beschäftigt, während sie denkt, und den Anteil eines Kerns, den sie nutzt, während ihr Gegner denkt, und warnt, wenn
eine Engine deutlich mehr Kerne nutzt als ihre `Threads` oder auf der Zeit ihres Gegners denkt (Pondern, das nicht
verlangt wurde, ein Hintergrund-Thread): was eine Partie unfair gegenüber dem Gegner und den anderen laufenden Partien
macht. Sie braucht von beidem je 20 s, um das zu beurteilen, und zählt jede gespielte Partie, im Fenster oder headless
(`--run`): Was die Engines jeder Partie verbraucht haben, wird in `processor.tsv` im Turnierordner geschrieben, so dass
ein Fortsetzen es behält. Sie warnt nur; sie entscheidet nie eine Partie.

### Pausieren, anhalten und fortsetzen
<!-- en: pause-stop-and-resume 100c55cf -->

**Pause** startet kein neues Partiepaar. Laufende Partien werden zu Ende gespielt, ebenso die zweite Partie eines
Paars, dessen erste Partie begonnen hat, so dass eine Pause nie ein Ergebnis wegwirft. **Fortsetzen** macht weiter.
**Stopp** spielt entweder zuerst die laufenden Partien zu Ende oder hält sofort an (die unterbrochenen Partien werden
nicht gezählt und neu gespielt).

Ein Turnier, das von einer Engine angehalten wurde, sagt in seinem Kopf und auf der Startseite unter *Zu erledigen*,
welche es war: *Unterbrochen: Alpha konnte nicht starten: …* (nach ihrem letzten Versuch) oder *Alpha ist abgestürzt
oder antwortet nicht mehr (Partie 12)* (ohne *Eine abgestürzte Engine neu starten*), mit *Engine öffnen* und
*Protokoll öffnen*.

**Jedes Turnier wird genau dort fortgesetzt, wo es aufgehört hat**, nach einem Stopp, einem geschlossenen Fenster, einem
Absturz oder einem Stromausfall: Keine Partie geht verloren und keine wird doppelt gespielt, und ein halb gespieltes
Partiepaar wird mit derselben Eröffnung und denselben Farben vervollständigt. *Fortsetzen* auf der Startseite setzt es
fort, mit der Statistik der schon gespielten Partien: Ergebnisse, und Tiefe, Zeit pro Zug und NPS der Engines, aus den
Zugkommentaren zurückgelesen (NPS nur, wenn das PGN sie aufzeichnet, *nps* in *PGN und Sonstiges*). Endete das
Programm, während ein Turnier lief (ein Absturz, ein Stromausfall, ein Neustart, oder Beenden, während es lief), setzt
der nächste Start es von selbst fort, und die Warteschlange geht danach weiter: *Beim Start das Turnier fortsetzen, das
lief*, in den **Einstellungen**, standardmäßig an. Ein pausiertes oder angehaltenes Turnier wartet auf dich, und
`--autostart` startet stattdessen die gespeicherte Einrichtung. Ein Teilnehmer, dessen Programm seit seinem Beitritt neu
gebaut wurde oder verschwunden ist, wird im Kopf des fortgesetzten Turniers genannt, mit *Weiter* und *Pause*: Seine
nächsten Partien spielt das Programm, wie es jetzt ist, mit den Optionen, die das Turnier ihm gegeben hat.

### Mehrere Turniere gleichzeitig
<!-- en: several-tournaments-at-once df7ce566 -->

Mehrere Turniere können nebeneinander spielen: ein Gauntlet neben einem SPRT, zwei Gauntlets gleichzeitig. *Start* auf
der Seite *Neues Turnier* startet ein weiteres, während andere spielen; *Fortsetzen* setzt eines neben ihnen fort.
Jedes Turnier spielt in einem eigenen Prozess (`fastchess-gui --run <folder>`, vom Fenster gesteuert), so dass ein
Absturz nur dieses Turnier zu Fall bringt, und das Fenster, seine Engine-Prüfungen, Schnelltests und Testsuiten
warten nicht mehr darauf, dass ein Turnier endet.

Der Arbeitsbereich hat **einen Tab pro geöffnetem Turnier**, über seinem Kopf: ein Punkt, während es spielt (ein
Pausezeichen, während es pausiert), sein Status unter dem Mauszeiger, ein Kreuz, um eines zu schließen, das nicht
spielt. Die Bereiche zeigen den gewählten Tab; die Befehlspalette listet die anderen auf (*… anzeigen*, *Pause*,
*Fortsetzen*). Ein aus der Liste geöffnetes beendetes Turnier übernimmt den Tab eines Turniers, das nur angesehen
wurde. Die Startseite hat eine Karte für jedes spielende Turnier, mit seinem Fortschritt, seinem Führenden, *Öffnen*
und *Pause*, und darunter sein **Gewicht**, die Kerne, die seine Partien von den ihm zustehenden belegen, und worauf
seine nächste Partie wartet; der Balken *Kerne* oben zeigt den Anteil jedes Turniers (und den der Seite Brett) und wie
viele Kerne sich die Turniere teilen. Das *Protokoll* zeigt die Zeilen eines Turniers, des Fensters oder alle (jede
Zeile eines Turniers trägt seinen Namen); die Zeilen eines Turniers werden außerdem in `fastchess-gui.log` in seinem
Ordner aufbewahrt.

**Die Maschine teilen**: Die spielenden Turniere teilen sich ein Budget an Kernen, eingestellt in den
**Einstellungen** (*Kerne für die Turniere*: standardmäßig die physischen Kerne weniger einen). Jedes bekommt zuerst
so viel wie eine Partie, solange das Budget es zulässt, das mit dem höchsten Gewicht zuerst; der Rest wird nach
Gewicht verteilt (ein Turnier mit Gewicht 2 bekommt doppelt so viele Kerne wie eines mit Gewicht 1), keines über seine
eigene Parallelität hinaus, und was eines nicht nehmen kann, geht an die anderen. Eine Partie zählt so viel wie die
`Threads` der größeren ihrer beiden Engines: Eine denkt, während die andere wartet. Ein pausiertes Turnier behält nur,
was seine laufenden Partien und die zweiten Partien ihrer Paare brauchen. Ein kleinerer Anteil unterbricht nie eine
Partie: Das Turnier spielt ab seiner nächsten Partie weniger Partien gleichzeitig. Ein Turnier allein spielt so viele Partien, wie seine
Parallelität sagt.

**Speicher**: Eine Partie beginnt nur, wenn der `Hash` ihrer beiden Engines und das, was jeder Engine-Prozess sonst
noch belegt (64 MB und 8 MB pro Thread), in das passen, was die spielenden Partien vom Speicherbudget übrig lassen
(*Speicher für die Engines* in den **Einstellungen**, standardmäßig 75 % des eingebauten Speichers), das sich die
Turniere teilen. Die Zusammenfassung der Einrichtung sagt, wie viele Partien das Budget gleichzeitig zulässt, und die
Karte *Zustand* der Startseite, wenn Partien auf Speicher warten.

**GPUs**: Eine Engine, die auf ihrer Karte mit *Nutzt eine GPU* markiert ist (eine Netzwerk-Engine wie Lc0), zählt
gegen die *GPUs* der **Einstellungen**: Es spielen nie mehr solche Engines gleichzeitig, als es GPUs gibt, in einem
Turnier und über Turniere hinweg, während die anderen Partien daneben spielen. Die Zusammenfassung der Einrichtung
sagt, wie viele Partien mit einer GPU-Engine das zulässt, und warnt, wenn keine GPU gezählt ist oder wenn eine Partie
zwischen zwei GPU-Engines mehr GPUs braucht, als die Maschine hat (sie spielt dann allein).

**Weiterlaufen lassen**: Wird das Fenster geschlossen, während Turniere spielen, fragt es, was zu tun ist.
*Weiterlaufen lassen und beenden* (der Standard) lässt jedes in seinem Prozess weiterspielen; das Fenster zeigt sie bei
seinem nächsten Start wieder, so wie sie gerade stehen. *Stoppen und beenden* hält sie an; jedes wird später genau
dort fortgesetzt, wo es aufgehört hat. Der Schnelltest endet in beiden Fällen. Ein Turnier, dessen Prozess beim
nächsten Start nicht mehr da ist (ein Neustart, ein Absturz), wird wie jedes unterbrochene Turnier fortgesetzt. Die
Übertragung des Tabs *Übertragung* läuft im Fenster, für das gezeigte Turnier: Für eine Übertragung, die ohne das
Fenster weiterläuft, starte das Turnier in einem Terminal mit den Übertragungsoptionen (siehe
[Headless-Modus](#headless-modus)).

### Wenn eine Engine nicht starten kann
<!-- en: an-engine-that-cannot-start 765a7c61 -->

Eine unbeaufsichtigte Nacht bleibt nicht an einer Engine hängen. Eine Engine, die nicht startet (ein verschobenes oder
neu gebautes Programm, eine fehlende Netzdatei, ein verschwundenes Laufwerk), wird noch dreimal im Abstand von 10 s
versucht; dann werden ihre Partien zurückgestellt, und die anderen Partien gehen weiter. Sie wird alle 5 Minuten erneut
versucht, eine Partie nach der anderen, und ihre Partien kommen zurück, sobald sie startet. Der Kopf des Turniers und
die Startseite (unter *Zu erledigen*) nennen die Engine und den Fehler und bieten an:

- **Erneut versuchen**: sofort statt beim nächsten Versuch (etwa nachdem du das Programm repariert hast).
- **Zurückziehen…** und **Ersetzen…** (Rundenturniere, Gauntlets und Mannschaftskämpfe): der Änderungsbereich des
  Tabs Spielplan, auf diese Engine eingestellt, mit der Auswirkung, die gezeigt wird, bevor sich etwas ändert.
- **Engine-Log**: `logs/start-<engine>.log` im Turnierordner, was beim letzten Versuch an die Engines gesendet wurde
  und was sie geantwortet haben, ihre Fehlerausgabe eingeschlossen (oft der Grund: eine Bibliothek oder eine Netzdatei,
  die nicht gefunden wird).

Das Protokoll und die Desktop-Benachrichtigung sagen, wenn eine Engine zurückgestellt wird. Das Turnier hält nur an,
wenn ohne die zurückgestellten Engines nichts mehr zu spielen ist, nach einem letzten Versuch mit jeder; sein Status
und die Benachrichtigung am Ende sagen das (*Angehalten: ohne …, die nicht startet, ist keine Partie mehr zu spielen*),
und die Warteschlange geht mit dem nächsten Turnier weiter. *Fortsetzen* versucht jede Engine von Neuem. Eine Pause
hält die Versuche an. *Das Turnier anhalten, wenn eine Engine nicht startet*, unter *PGN und Sonstiges* der
Einrichtung, hält es stattdessen sofort an, sobald die Versuche aufgebraucht sind.

### Ein laufendes Turnier ändern
<!-- en: changing-a-running-tournament aebe63e2 -->

Der Bereich **Turnier ändern** des Tabs *Spielplan* arbeitet, ohne etwas anzuhalten: Laufende Partien gehen weiter, und
keine gespielte Partie geht verloren. Jede Änderung zeigt ihre Auswirkung, bevor sie angewendet wird.

- **Engine hinzufügen**: Sie spielt die bereits begonnenen Zyklen nach (*Die bereits begonnenen Zyklen nachholen*)
  oder steigt ab dem nächsten Zyklus ein.
- **Engine zurückziehen**: Ihre verbleibenden Partien fallen weg; ihre bisher gespielten Partien zählen oder nicht.
- **Engine ersetzen**: Ihre Partien zählen nicht mehr und werden von der neuen Engine neu gespielt.
- **Anzahl der Zyklen**: mehr oder weniger.
- **Partien neu spielen**: jede Partie einer Engine, oder die Zeitverluste und Abstürze.
- Rechtsklick auf eine Partie (hier oder im Tab *Partien*): sie neu spielen, sie annullieren (ohne Neuspielen) oder
  ihr Ergebnis setzen.

Eine hinzugefügte oder eingewechselte Engine spielt mit den Einstellungen des Turniers (seiner Bedenkzeit, Hash und
Threads, unter der Engine angezeigt) oder mit *Eigene Einstellungen*: Hash, Threads, Anteil an der Zeit (oder Knoten,
Tiefe), wie die eigenen Einstellungen eines Teilnehmers auf der Seite *Neues Turnier*. Ein Turnier, das vor 0.6.6
erstellt wurde, hat seine Einstellungen nicht gespeichert: Die seiner ersten Engine treten an ihre Stelle, und das
können deren eigene gewesen sein.

In einem Mannschaftskampf tritt eine hinzugefügte Engine der gewählten Mannschaft bei und spielt gegen jede Engine der
anderen Mannschaft; bei Brett gegen Brett lässt sich keine Engine hinzufügen (ihr Brett hätte keinen Gegner): Ersetze
stattdessen eine. Ein Ersatz übernimmt die Mannschaft und das Brett der Engine, die er ersetzt; weitere Zyklen paaren
die Engines, die noch spielen (bei Brett gegen Brett auf den Brettern des ersten Zyklus). Die Partien, die eine
zurückgezogene oder ersetzte Engine gespielt hat, zählen weiter für ihre Mannschaft, es sei denn, sie werden
annulliert.

**Ergebnis setzen** entscheidet eine Partie, die das Programm nicht richtig entscheiden konnte: ein Hänger, den es
nicht erkannt hat, ein gewonnenes Endspiel, in dem die Engines hin und her ziehen, eine Partie, die aus einem falschen
Grund endete. Wähle 1-0, ½-½ oder 0-1 und gib einen Grund an; die Punkte, die sich dadurch verschieben, werden gezeigt,
bevor *Ergebnis setzen* es anwendet. Die Tabelle, die Kreuztabelle, die Wertungen und die Checkliste vor dem Einsenden
zählen das neue Ergebnis (als Abschätzung: Ein auf Remis gesetzter Zeitverlust zählt nicht mehr als Zeitverlust); der
Tab *Partien* markiert es mit einem `*`, dessen Tooltip den Grund angibt und wie die Partie gespielt endete, und
*Zurück zum gespielten Ergebnis* nimmt es zurück. Eine laufende Partie wird auf dieselbe Weise beendet: Rechtsklick auf
ihr Brett im Tab *Live*, *Mit einem Ergebnis beenden* (für die verfolgte Partie auch in der Befehlspalette). Die Engine
am Zug wird gestoppt, fastchess beendet die Partie sofort, und sie zählt mit dem gewählten Ergebnis; die Engine startet
für ihre nächste Partie neu. Das setzt *Eine abgestürzte Engine neu starten* voraus (Einrichtung, *PGN und Sonstiges*,
standardmäßig an).

Annullierte Partien und von Hand gesetzte Ergebnisse bleiben in den PGN-Dateien und im Journal so, wie sie gespielt
wurden: An die PGN-Dateien wird immer nur angehängt. Alles, was die Partien liest, berücksichtigt das: Annullierte Partien werden
weggelassen, und ein von Hand gesetztes Ergebnis steht in jedem Export (*Partie exportieren…*, *Alle exportieren…*,
*Für eine Rangliste exportieren…*, der Export des direkten Vergleichs auf der Seite *Elo-Listen*, die Datei von *Jede
Partie zusätzlich anhängen an*) mit `[Termination "adjudicated by the operator"]` und dem Grund in einem Kommentar vor
dem Ergebnis. **Letzte Änderung rückgängig machen** wird angeboten, bis die nächste Partie beginnt: Unter der Änderung
wurde noch nichts gespielt, sie lässt sich also exakt zurücknehmen (Partien, die schon liefen, als sie vorgenommen
wurde, bleiben erhalten). Ein von Hand gesetztes Ergebnis lässt sich rückgängig machen, solange es die letzte Änderung
ist, ob seither Partien gespielt wurden oder nicht (in einem K.-o.-Turnier, dessen nächste Runden aus den Ergebnissen
folgen, bis die nächste Partie beginnt); der Tab *Partien* bietet *Rückgängig* auch neben der Partie an.

### Warteschlange und Neustart
<!-- en: queue-and-relaunch 382fd33e -->

Die **Warteschlange** auf der Startseite enthält Turniere, die nacheinander gespielt werden: Wenn eines von selbst
endet, startet das nächste (nicht nach einem Stopp oder einem Fehler, und nicht, solange *Alles pausieren* die
Warteschlange anhält). Mit *Starten, wenn Kerne frei sind* startet das nächste neben den spielenden, sobald das Budget
an Kernen größer ist, als sie nehmen können, jedes mit seiner Parallelität. Die Turniere lassen sich umordnen oder
entfernen, und das erste lässt sich sofort starten, wenn nichts läuft. Die Startseite sagt, wann das laufende Turnier
endet (*Endet gegen HH:MM (in …)*) und wann die ganze Warteschlange endet, jedes Turnier der Warteschlange mit seiner
Dauer und, solange die Dauern davor bekannt sind, wann es startet; die Turniere, deren Dauer sich nicht vorhersagen lässt
(ein SPRT ohne Höchstzahl, Knoten oder Tiefe pro Zug), werden gesondert gezählt.

**Neu starten mit…** bei einem beendeten Match (in seinem Kopf oder unter *Zuletzt beendet* auf der Startseite) spielt
es erneut mit einer anderen Engine A, typischerweise der nächsten Version, gegen dieselbe Engine B, mit den übrigen
Einstellungen des Matches (denen der Seite *Neues Turnier* bei einem Match, das eine ältere Version gestartet hat, die
sie nicht aufbewahrte).

**Klonen** (im Kopf eines Turniers, unter *Zuletzt beendet* und in *Alle Turniere* auf der Startseite) öffnet die Seite
*Neues Turnier*, gefüllt mit der Einrichtung dieses Turniers, gleich welches Format: die Teilnehmer mit ihren eigenen
Einstellungen, die Bedenkzeit, die Eröffnungen, die Abschätzung und der Rest, jeder Wert so, wie er gespielt wurde.
Seine Profile sind genannt, und ein seitdem geändertes Profil ist als für dieses Turnier geändert markiert. Ändere, was
anders sein soll (eine andere Bedenkzeit, die nächste Version einer Engine, ein anderes Buch), dann **Start** oder *Zur
Warteschlange hinzufügen*. Ein Turnier, das noch spielt, lässt sich ebenfalls klonen; eines, das eine ältere Version
gestartet hat, die seine Einrichtung nicht aufbewahrte, nicht.

## Elo-Listen
<!-- en: ratings 3e4be48f -->

Die Seite **Elo-Listen** berechnet Maximum-Likelihood-Wertungen (das logistische Modell, Remis als halber Punkt, wie
Ordo es standardmäßig macht) über alle Partien aller Turniere im Profil und der importierten PGN-Dateien. Engines
werden an ihrem Namen erkannt. Die gezählten Partien lassen sich oben auf der Seite eingrenzen, für jeden Tab:

- eine Bedenkzeit, eine Gruppe der Engine-Bibliothek (die Partien zwischen ihren Engines), die letzten 30 oder 365
  Tage;
- *Importierte Partien*: die Partien der unter *PGN-Dateien…* aufgeführten PGN-Dateien (der Download einer Rangliste,
  die Partien eines anderen Programms), aus ihren Kopfzeilen gelesen; ihre Engines werden an den Namen erkannt, die das
  PGN ihnen gibt, und ein Name, der von Groß-/Kleinschreibung und Leerzeichen abgesehen der einer Engine der Bibliothek
  ist („stockfish  17“ für „Stockfish 17“), ist diese Engine: Seine Partien zählen als die der Engine, und ihre
  Referenz-Elo kann sie verankern. Nichts Ungenaueres wird von selbst angewendet: „Stockfish 17 64-bit“ bleibt eine
  eigene Engine, bis du unter *PGN-Spieler…* (neben der Zahl der Engines der Bibliothek, oder unter *PGN-Dateien…*)
  angibst, welche Engine es ist. Dieses Fenster listet die Spieler auf, die dem Namen nach keine Engine der Bibliothek
  sind, mit einer Suche: für jeden *Eine eigene Engine* (der Standard) oder eine Engine der Bibliothek. Ein Name, den
  zwei Engines der Bibliothek von Groß-/Kleinschreibung abgesehen teilen, wird in der Warnfarbe gezeigt, damit du
  wählst; eine wahrscheinliche Engine (ihr Name ist der Anfang des Spielernamens) wird als *Vielleicht …?*
  vorgeschlagen, nie angewendet. Das Fenster öffnet sich von selbst, nachdem eine PGN-Datei hinzugefügt wurde, wenn es
  etwas zu entscheiden gibt. Die Wahl, eine eigene Engine eingeschlossen, wird bei der Engine gespeichert (sie folgt
  einer Umbenennung) und gilt für jede PGN-Datei und für die für die Referenz-Elo importierten Ranglisten; die Partien
  werden damit neu gelesen, wenn das Fenster geschlossen wird. Die Rangliste sagt, wie viele Engines der Bibliothek die
  PGN-Dateien enthalten, ihr Tooltip, welche;
- *Anker*: der Durchschnitt bei 0, oder eine Engine mit einer festen Wertung (standardmäßig die erste Engine mit einer
  Referenz-Elo).

### Rangliste
<!-- en: rating-list c726b55e -->

Die Liste, mit einer Mindestzahl an Partien. Neben Elo, ± und den Partien zeigt jede Engine ihre Punktzahl, ihre Siege
(+), Remis (=) und Niederlagen (−), ihre Remisquote, *Geg.*: die durchschnittliche Wertung der Gegner, auf die sie
traf, Partie für Partie, *LOS*: die Wahrscheinlichkeit, dass sie stärker ist als die nächste Engine der Liste, aus den
beiden Wertungen und der Kovarianz ihrer Fehler (näherungsweise bei mehr als 1000 Engines, wie ihr Tooltip sagt), und
*Abstand*: ihre Elo über der nächsten Engine. Ein Klick auf den Kopf einer Spalte sortiert nach ihr (# behält den Platz
nach Wertung); ein Rechtsklick blendet Spalten ein oder aus (*Abstand* ist anfangs ausgeblendet). Ein Klick auf eine
Engine öffnet ihre Karte.

*Kopieren* als Text oder BBCode, mit *Mit Kopf*, um den unten beschriebenen Kopf über die Liste zu setzen.
*Exportieren…*: CSV mit jeder Spalte; eine HTML-Seite (hell oder dunkel) und Markdown (eine Tabelle, die GitHub und
Foren anzeigen), beide beginnend mit einem Kopf, der sagt, wie die Liste entstand: die gezählten Turniere und
PGN-Dateien, die Partien mit den Siegen von Weiß und Schwarz, den Remis und der Punktzahl von Weiß, der Anker, die
Filter, die Mindestzahl an Partien, das Datum und die Version; und *Partien für Ordo…*: die gezählten Partien als PGN
aus Kopfzeilen, das Ordo und BayesElo lesen (`ordo -p ratings.pgn -o ratings.txt`).

### Engine-Karte
<!-- en: engine-card 80442e81 -->

Die Karte einer Engine: ihre Wertung, ihre Ergebnisse insgesamt und nach Bedenkzeit, ihre Zeitverluste, Abstürze und
illegalen Züge (jede Partie öffnet sich in ihrem Turnier), ihre Turniere (jedes lässt sich öffnen), ihre
durchschnittliche Tiefe, Zeit pro Zug und NPS aus den Zugkommentaren, und ihre Wertung nach jedem Turnier. Die Seite
*Engines* führt mit *Ihre Ergebnisse…* zu ihr.

### Direkter Vergleich
<!-- en: head-to-head 1021ba2f -->

Zwei Engines gegeneinander: die Punktzahl, Elo und LOS, die Pentanomial-Zählungen der Paare, von denen sie beide
Partien gespielt haben, nach Bedenkzeit und nach Turnier, ihre Partien (jede öffnet sich in ihrem Turnier) und
*Partien exportieren…*.

### Fortschritt
<!-- en: progress e2ea28fc -->

Die Wertung der angehakten Engines nach jedem Turnier, das sie gespielt haben, auf einer Zeitachse: die Versionen einer
Engine nebeneinander.

## Übertragung
<!-- en: broadcast c2f489e7 -->

Der Tab *Übertragung* eines laufenden Turniers veröffentlicht es, während es spielt; der Schnappschuss wird zweimal pro
Sekunde aktualisiert. *Gezeigte Partie* wählt die Partie, die an TLCS und die Webseite gesendet wird: automatisch, oder
die im Tab *Live* gewählte.

### Live-Dateien
<!-- en: live-files 09341896 -->

Atomar in einen Ordner deiner Wahl geschrieben, um sie irgendwo zu veröffentlichen (FTP, rsync, eine Website):
`live.pgn` (die laufenden Partien), `standings.html` (Tabelle und Kreuztabelle), `tournament.json` (alles, für
Skripte).

### TLCS-Server
<!-- en: tlcs-server 712f28cb -->

UDP-Port, standardmäßig 16001: das Protokoll von Tom's Live Chess Server, so dass TLCV und andere TLCS-Viewer das
Turnier verfolgen können (Brett, Uhren, PV, Kreuztabelle und die gespielten Partien). Es wird gegen node-tlcv geprüft.
*Ein Port pro Partie* legt jede gleichzeitige Partie auf einen eigenen Port: Partie #1 des Tabs *Live* auf den
UDP-Port, #2 auf den nächsten und so weiter. Jeder Port öffnet sich, wenn seine Partie beginnt, mit eigenen Grenzen für
Clients und Anmeldungen, und erhält die Kreuztabelle nach jeder Partie; *Gezeigte Partie* gilt dann nicht.

### Webseite
<!-- en: web-page 1d4c78b3 -->

TCP-Port: eine Seite, die fastchess-gui selbst ausliefert, für jeden Browser im Netzwerk, nur lesbar und jede Sekunde
aktualisiert. Die verfolgte Partie (die erste laufende oder die angeklickte) steht in der Mitte: das Brett mit dem
letzten Zug, einem König im Schach, den Koordinaten und den Pfeilen; die Uhr jedes Spielers in großer Schrift; die
Bewertung jeder Engine in großer Schrift, mit der Angabe, wer gerade denkt, ihre Suche als Kacheln (Tiefe/selektive
Tiefe, Knoten, Geschwindigkeit, Zeit, Tablebase-Treffer, Hash) und ihre Hauptvariante; und die Züge, die Buchzüge
kursiv. Geh mit den Pfeilen unter dem Brett oder den Tasten ← → durch die Züge (▶ hinter dem letzten Zug kehrt zur
Partie zurück, wie sie gespielt wird), dreh das Brett mit ⇅ oder F, und kopiere das PGN der Partie oder die FEN der
Stellung.

Neben der Partie zeigen Tabs die Tabelle und die Kreuztabelle, die Diagramme der verfolgten Partie, die Suche jeder
Engine im Schnitt pro Zug mit ihren Auffälligkeiten, die nächsten Partien des Spielplans (ein Schweizer System oder ein
K.-o.-Turnier paart seine späteren Runden im Lauf des Turniers), die Statistik und die Auffälligkeiten der gespielten
Partien, und die Veranstaltung. Jede laufende Partie wird darunter gezeigt, mit einer kleinen Bewertungsleiste neben
jedem Spieler. *Moves* schaltet die Züge zwischen Buchstaben (Nf3) und Figurinen (♘f3) um, und das Design folgt dem
System oder ist auf hell oder dunkel eingestellt; der Browser merkt sich diese Wahl. Die Seite lädt nichts von
anderswo und funktioniert auf Telefonen. *Download the games (PGN)* liefert die PGN-Datei des Turniers, bis zur letzten
aufgezeichneten Partie. `state.json`, das die Seite liest, enthält alles für Skripte. Die Logos der Engines (siehe
[Darstellung](#darstellung)) erscheinen neben ihren Namen, als kleine PNG-Bilder, die das Programm daraus erzeugt und
selbst ausliefert; die Seite erfährt nur die der Engines des Turniers.

### lichess
<!-- en: lichess 7597e86f -->

Eine lichess-Übertragung, mit dem Brett, der Bewertungsleiste und dem Chat von lichess, für alle im Internet. Sie
braucht ein persönliches API-Token mit dem Scope `study:write` (*Token auf lichess erstellen…*), das nur in
`settings.json` im Profil gespeichert wird, sobald du sein Feld verlässt. Ist keine Runde angegeben, wird eine nach
dem Turnier benannte Übertragung angelegt, nicht gelistet, es sei denn, *Unter den lichess-Übertragungen aufgeführt*
ist angehakt; ihr Link wird gezeigt, sobald sie auf Sendung ist. Andernfalls gehen die Partien an die Runde, deren URL
oder ID angegeben ist (eine Übertragung, die per Push gespeist wird, nicht über eine Quell-URL). Die laufenden Partien
werden alle 8 Sekunden mit ihren Uhren gesendet, und jede beendete Partie einmal; eine Runde fasst 64 Partien, danach
wird die nächste Runde derselben Übertragung angelegt. Nur die Partien, die nach dem Einschalten beendet wurden, werden
gesendet; `lichess.json` im Turnierordner merkt sich, wohin sie gingen, so dass ein fortgesetztes Turnier dieselbe
Übertragung fortführt. lichess behält von dem, was es bekommt, nur die Uhren (es verwirft das `[%eval]` und den Text
der Zugkommentare): Seine Bewertungsleiste ist seine eigene, und die Bewertungen der Engines erscheinen dort nicht.

Die Verbindung verwendet Mbed TLS, in das Programm eingebaut, und vertraut nur den Stammzertifikaten, die lichess.org
signieren (Google Trust Services), und denen von Let's Encrypt. Sie geht über den HTTP-Proxy, den `HTTPS_PROXY` (oder
`ALL_PROXY`) nennt, etwa `http://proxy.example:3128` oder `http://user:password@proxy.example:3128`, außer für die
Hosts von `NO_PROXY`; auch unter Windows kommt der Proxy aus diesen Variablen, nicht aus den Systemeinstellungen.
SOCKS-Proxys werden nicht unterstützt.

### Ein Mannschaftskampf auf Sendung
<!-- en: a-team-match-on-the-air fd32027f -->

Ein Mannschaftskampf zeigt den Stand der Mannschaften überall: im Kopf der Webseite und über ihrer Tabelle (mit der
Punktzahl % von Mannschaft A, Elo ± und LOS), in `standings.html` (die Mannschaften, dann die Engines jeder Mannschaft
gegen die der anderen), in `tournament.json` und `state.json` (`teams`, `team_elo`, `team_elo_error`, `team_los`) und
in der TLCS-Kreuztabelle, als Zeile unter der Tabelle („Lynx 17½ - 18½ Otter · 48.6 % · Elo -9.7 ± 31.2“). Seine
Partien tragen in den PGN-Dateien die Tags `WhiteTeam` und `BlackTeam`, die die Exporte beibehalten.

### Die Webseite ins Internet stellen
<!-- en: putting-the-web-page-on-the-internet b19f02f8 -->

Die Webseite lässt sich von überall verfolgen, ohne einen Port im Router zu öffnen: Hake *Im Internet veröffentlichen*
unter *Webseite* im Tab *Übertragung* an. Während ein Turnier läuft, öffnet fastchess-gui einen SSH-Tunnel (libssh2,
in das Programm eingebaut) zu einem Tunneldienst, der der Seite eine öffentliche https-Adresse gibt; nichts muss
installiert werden.

- *localhost.run* (der Standard): kostenlos, ohne Konto. Seine Adressen ändern sich ab und zu, und seine
  Geschwindigkeit ist begrenzt; die Seite und ihr Zustand werden komprimiert gesendet, das fällt also kaum ins Gewicht.
  Sein Host-Schlüssel ist fastchess-gui bekannt: Eine Verbindung zu irgendetwas anderem wird abgelehnt.
- *Ein anderer SSH-Server*: sein Host, Port und Benutzer, und der Port, den er für die Seite öffnet (so wie
  `ssh -R REMOTE_PORT:localhost:PORT user@host` es verlangen würde; 0 lässt ihn wählen). Nur Server, die den Benutzer
  ohne Schlüssel und ohne Passwort hereinlassen, sind verwendbar (Authentifizierung „none“), und sie müssen
  curve25519-sha256, einen RSA-Host-Schlüssel mit 2048 bis 4096 Bit und AES (GCM oder CTR) anbieten. Dem Schlüssel des
  Servers wird beim ersten Mal vertraut, und er wird im Profil gespeichert (`known_hosts.json`); ändert er sich, hält
  der Tunnel an und sagt das, und *Dem neuen Schlüssel vertrauen* ist für den Fall gedacht, dass der Besitzer des
  Servers bestätigt, dass er sich geändert hat.

Der Tab zeigt den Zustand (verbindet, auf Sendung mit der Zahl der Verbindungen, verbindet neu, oder ein Fehler) und,
sobald bekannt, die öffentliche Adresse mit *Kopieren*, *Öffnen* (um die Seite so zu sehen, wie die Zuschauer sie
sehen) und einem QR-Code für ein Telefon (klicke darauf, um ihn zu vergrößern). Der Tunnel verbindet sich nach einem
Fehlschlag von selbst neu, wartet dabei jedes Mal etwas länger und zeigt die neue Adresse, falls sie sich geändert hat.
Er endet mit der Webseite oder mit dem Turnier.

Andere Tunnel funktionieren auch, getrennt gestartet: Mit dem von Cloudflare zum Beispiel schaltest du die Webseite
ein (hier Port 8080) und führst `cloudflared tunnel --url http://localhost:8080` aus; es gibt eine Adresse wie
`https://some-words.trycloudflare.com` aus. ngrok, tailscale funnel oder ein Reverse-Proxy wie nginx, auch unter einem
eigenen Pfad (`location /live/ { proxy_pass http://localhost:8080/; }`), funktionieren genauso.

Die Seite ist nur lesbar: Sie beantwortet nur GET-Anfragen nach der Seite, ihrem Zustand und der PGN-Datei, nie nach
einer Datei, die eine Anfrage nennt, und nichts darin ändert das Turnier. Jede Anfrage wird für sich beantwortet,
höchstens 32 gleichzeitig, und muss innerhalb einer Sekunde ankommen und innerhalb von zehn beantwortet sein, so dass
ein langsamer oder stummer Besucher niemanden sonst aufhält; hinter dem Tunnel zählt die Verbindung jedes Zuschauers
als eine davon: 40 werden gleichzeitig weitergeleitet, ebenso viele weitere warten, bis sie dran sind, und die
nächsten werden abgelehnt; ein Zuschauer, der mehr sendet, als die Seite annimmt, wird als Einziger getrennt. Zwei Downloads
der PGN-Datei laufen gleichzeitig, und der nächste wird gebeten, einen Moment später wiederzukommen. Der Zustand wird
nie zwischengespeichert, auch nicht vom Tunnel, so dass die Zuschauer die Partien sehen, während sie laufen; die Seite
wird vom Browser aufbewahrt und bei jedem Laden erneut geprüft. Die im Tab *Übertragung* gezeigten Zuschauer werden
aus den Anfragen gezählt, die die Seiten stellen, so dass die Zahl auch hinter einem Tunnel stimmt.

Die Zuschauer sehen den Namen des Turniers, die Namen der Engines, ihre Wertungen, Suchen und Auffälligkeiten, die
Eröffnungen und die PGN-Datei der gespielten Partien. Die Ordner und die Dateien der Engines bleiben privat. Headless:

```
fastchess-gui --run FOLDER --tunnel                      # localhost.run, the address printed on the console
fastchess-gui --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Wo die Zuschauer sich unterhalten
<!-- en: where-the-viewers-talk eb3ef90c -->

Die Seite hat keinen eigenen Chat; sie verweist dorthin, wo die Unterhaltung stattfindet. Ein *Chat-Link* unter
*Webseite* (eine Discord-Einladung, ein Twitch-Kanal, ein Forumsthema; nur https, alles andere wird mit einer Meldung
unter dem Feld abgelehnt) setzt einen Knopf *Chat* in den Kopf der Seite, der ihn in einem neuen Tab öffnet. Während
die Partien an lichess gesendet werden, verlinkt die Seite auch die lichess-Runde, die ihren eigenen Chat hat.

Eine *Discord-Server-ID* fügt neben *Event* einen Tab *Discord* hinzu, mit dem Server-Widget von Discord im hellen oder
dunklen Design der Seite: der Name des Servers, die Mitglieder online, seine Sprachkanäle und ein Knopf *Join*, nicht
die Nachrichten. Der Besitzer des Servers schaltet es unter Servereinstellungen, Widget („Server-Widget aktivieren“)
ein, wo auch die zu kopierende Server-ID steht (17 bis 20 Ziffern); mit eingeschaltetem Entwicklermodus liefert ein
Rechtsklick auf das Symbol des Servers ebenfalls *Server-ID kopieren*. Das Widget wird erst von discord.com geladen,
wenn ein Zuschauer den Tab öffnet, und Discord sieht dann den Besuch dieses Zuschauers (seine Adresse, seinen Browser,
seine Discord-Anmeldung, falls er eine hat); ohne ID kontaktiert die Seite niemanden außer fastchess-gui. Die Chat- und
lichess-Links senden nichts, bis ein Zuschauer sie anklickt, und dann keinen Referrer. Eine headless ausgeführte
Einrichtungsdatei (`--run SETUP.json`) übernimmt beide aus `web_chat_link` und `web_discord_id`.

## Headless-Modus
<!-- en: headless-mode 3471fbb3 -->

```
fastchess-gui --run TOURNAMENT_FOLDER     # resume a tournament
fastchess-gui --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

Es wird kein Fenster geöffnet; auf der Konsole erscheinen die Version, dann der Fortschritt, mit den Versuchen einer
Engine, die nicht startet, und der zurückgestellten (mit ihrem Engine-Log) oder zurückgekehrten Engine, wie im Fenster,
dazu die Tabelle alle 20 Partien und am Ende (bei einem Mannschaftskampf: die Stände der Mannschaften, dann die Engines
jeder Mannschaft gegen die der anderen) und das Ende eines Turniers mit der Schlusszeile der Tabellenkopien (Partien,
Remis, Bedenkzeit, Buch und Veranstaltung). **Strg+C** hält nach den laufenden Partien an, und das Turnier
lässt sich später fortsetzen, von der Befehlszeile oder der GUI aus. Exit-Codes: 0 beendet, 1 Fehler beim Spielen, 2
nichts zu spielen, 3 vor dem Ende angehalten (durch Strg+C, oder weil ohne eine Engine, die nicht startet, nichts mehr
zu spielen ist). Das Speicherbudget und die GPUs der **Einstellungen** des Profils gelten auch hier.

`--detach` spielt es stattdessen im Hintergrund: Der Befehl kehrt sofort zurück, und das Turnier läuft ohne Terminal
weiter; das Fenster zeigt es (es verbindet sich damit), wenn es mit demselben Profil geöffnet wird, und `--attach`
spricht mit ihm:

```
fastchess-gui --run FOLDER --detach         # in the background, on a local socket
fastchess-gui --attach FOLDER               # its control channel on the standard input and output
```

Das Fenster steuert jedes Turnier auf dieselbe Weise: `--run FOLDER --control socket` (oder `--control stdio`) spielt
es ohne Ausgabe und tauscht JSON-Zeilen mit dem Fenster aus: die Ereignisse des Turniers (sein Zustand, jede
Journalzeile, die laufenden Partien Zug für Zug, jede aufgezeichnete Partie, sein Log, sein Ende) und die Befehle des
Fensters (Pause, Fortsetzen, Stopp, Änderungen, das Ende einer Partie, sein Anteil an der Maschine mit
`--allowance THREADS:MEMORY_MB:GPUS:ONE`). Die erste Zeile in jeder Richtung ist ein Hallo,
`{"hello": {"protocol": 1, ...}}`; ein Skript über `--attach` erhält das Hallo und die Ereignisse des Turniers und
darf Befehle wie `{"c": "pause"}` senden. Nur ein Fenster (oder Skript) zur Zeit spricht mit einem Turnier;
`--attach` lässt es los, wenn seine Standardeingabe endet oder seine Ausgabe nicht mehr gelesen wird, ein Skript muss
also seine Eingabe offen halten, während es die Antworten liest.

Das Turnier lässt sich gleichzeitig übertragen, wie aus dem Tab *Übertragung*:

```
fastchess-gui --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
fastchess-gui --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
fastchess-gui --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
fastchess-gui --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
fastchess-gui --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
fastchess-gui --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` nimmt das Token aus der Umgebungsvariable `FCG_LICHESS_TOKEN` oder das des Profils.

Die Optionen lassen sich kombinieren. Eine mit eingeschalteter Übertragung gespeicherte Einrichtungsdatei überträgt
auch ohne diese Optionen auf dieselbe Weise. Für Zuschauer außerhalb des lokalen Netzwerks öffne die Ports im Router: TLCV und node-tlcv
verbinden sich mit dem TLCS-Port. Für die Webseite erspart ein Tunnel das Öffnen eines Ports (siehe
[Die Webseite ins Internet stellen](#die-webseite-ins-internet-stellen)).

## Maschinen
<!-- en: machines 4d8fbfef -->

Andere Computer, auf denen Turniere laufen: ein Testrechner, ein Server, ein zweiter PC. **Einstellungen** >
*Maschinen* listet sie auf. Eine Maschine wird über SSH erreicht: Auf ihr muss nichts geöffnet werden außer ihrem
SSH-Server, und das Programm speichert nichts Geheimes.

### Eine Maschine hinzufügen
<!-- en: adding-a-machine f1f15823 -->

*Maschine hinzufügen*, dann auf ihrer Karte:

- *Name*, *Host* (ihr Name im Netzwerk oder ihre Adresse), *Port* (22) und *Benutzer*, das Konto, unter dem ihre
  Turniere laufen;
- *Anmelden mit*: dem *SSH-Agent* (ssh-agent unter Linux und macOS, Pageant oder der Dienst *OpenSSH Authentication
  Agent* unter Windows; füge deinen Schlüssel mit `ssh-add` hinzu) oder einer *Schlüsseldatei*, RSA oder Ed25519
  (`~/.ssh/id_ed25519`). Die Passphrase einer Schlüsseldatei wird einmal pro Sitzung abgefragt: Sie bleibt im Speicher
  und wird nirgends geschrieben. Die Maschine muss den Schlüssel kennen: Seine öffentliche Hälfte (`id_ed25519.pub`)
  kommt in `~/.ssh/authorized_keys` auf der Maschine (`ssh-copy-id user@host` legt sie dort ab), oder, für einen
  Administrator einer Windows-Maschine, in `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Ordner dort*: der Ordner von fastchess-gui auf der Maschine, für das Programm, seine Turniere, Bücher und
  Tablebases, relativ zum Home-Ordner des Benutzers (standardmäßig `fastchess-gui`) oder absolut (`/srv/fcg`,
  `D:\fcg`);
- *Budget*: die Kerne und der Speicher, die die Turniere auf der Maschine gleichzeitig nutzen dürfen; 0 nimmt, was der
  Test ermittelt hat, alle Threads bis auf einen und 80 % des Speichers.

*Verbindung testen* meldet sich an und nennt dann das System der Maschine, ihre Threads, ihren Speicher, den freien
Platz dort, wo ihr Ordner liegt, und ob fastchess-gui dort installiert ist, in welcher Version. Die erste Verbindung
zeigt den Host-Schlüssel der Maschine (seinen SHA256-Fingerabdruck) und vertraut ihm von da an (`known_hosts.json` im
Profil, auch die des Übertragungstunnels). Ändert sich der Schlüssel später, wird nichts gesendet, und die Karte sagt
das, mit *Dem neuen Schlüssel vertrauen* für eine Maschine, die neu installiert wurde. Jeder Fehlschlag nennt die
Maschine und was zu tun ist: der Host, der nicht erreichbar ist, ein Schlüssel, den die Maschine ablehnt (und wohin
seine öffentliche Hälfte gehört), eine Passphrase, die nicht stimmt, kein laufender Agent.

*fastchess-gui installieren* (*fastchess-gui aktualisieren*, sobald es dort ist) kopiert das Programm über SFTP in den
Ordner der Maschine und prüft dann, ob es dort startet (`fastchess-gui --version`). Es kopiert dieses Programm, wenn
die Maschine dasselbe System hat (Linux x86-64 zu Linux x86-64); für ein anderes, etwa eine Windows-Maschine von Linux
aus, lade das Release für dieses System herunter und wähle sein Programm auf der Karte (*fastchess-gui für Windows
x86_64*). Eine vom Netzwerk unterbrochene Kopie wird dort fortgesetzt, wo sie aufgehört hat.

### Engines auf einer Maschine
<!-- en: engines-on-a-machine 4b00e462 -->

*Engines auf dieser Maschine*: die Engines der Bibliothek, die die Maschine hat, jede mit ihrem Pfad dort
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Engines prüfen* prüft jeden Pfad: Das Programm ist da, lässt sich
ausführen und antwortet innerhalb von 10 Sekunden auf `uci` (`xboard` bei einer WinBoard-Engine). Die Markierung
daneben und ihr Tooltip sagen, was gefunden wurde: den Namen der Engine, oder warum sie nicht läuft (fehlt, nicht
ausführbar, für ein anderes System gebaut, stumm).

Eine Engine, die auf der Maschine fehlt, wird mit *Kopieren…* in ihrer Zeile dorthin kopiert (oder mit *\<engine\> nach
\<machine\> kopieren* in der Befehlspalette, oder mit dem Knopf eines Starts, der abgelehnt wurde, weil eine Engine dort
keinen Pfad hat). Der Bereich, der sich öffnet, sagt zuerst, was übertragen wird: das Programm, seine Größe und das
System, für das es gebaut ist, aus seinen ersten Bytes gelesen (ein Windows-Programm, ein Linux- oder macOS-Programm,
universell oder nicht, oder ein Skript). Ein Programm, das die Maschine nicht ausführen kann, wird abgelehnt, bevor
etwas gesendet wird, mit dem Hinweis, welcher Build zu wählen ist: „Alpha ist ein Windows-Programm; tc-box läuft unter
Linux: wähle den Linux-Build von Alpha“. Die Architektur zählt ebenfalls (x86-64 oder arm64); ein x86-64-Build auf
einem arm64-Mac läuft nur unter Rosetta, und ein Format, das das Programm nicht kennt, wird trotzdem kopiert: Beides
wird gesagt, nicht abgelehnt. Ein Skript geht nur auf eine Linux- oder macOS-Maschine, und nur, wenn der Interpreter
seiner ersten Zeile (`python3`, `/bin/sh`) dort vorhanden ist. *Den \<system\>-Build von \<engine\> wählen…* nimmt eine
andere Datei dieses Computers, etwa den Windows-Build der Engine, prüft sie auf dieselbe Weise und behält sie für diese
Maschine: Spätere Vergleiche und Ersetzungen verwenden sie, nicht das Programm der Bibliothek.

*Mit seinem Ordner* kopiert den ganzen Ordner der Engine neben das Programm (ihr Arbeitsverzeichnis oder den Ordner des
Programms), für eine Engine, die Dateien neben sich braucht: ihre Netze, ein Buch. Der Bereich zählt zuerst seine
Dateien und ihre Größe und nennt die Optionen der Engine, die in ihn zeigen (`EvalFile` eines Netzes): In einem Turnier
auf der Maschine zeigen sie auf die Kopie dort, und der Ordner ist das Arbeitsverzeichnis der Engine. Ein Ordner mit
mehr als 2000 Dateien wird nicht angeboten (ein Programm in `/usr/bin` soll nicht mit seinem Ordner kopiert werden),
ein Ordner `.git` wird weggelassen, und bei einer Windows-Maschine wird ein Dateiname, den Windows nicht aufnehmen kann
(`:`, `*`, `?`, ein Punkt am Ende, `CON`, `NUL`...), genannt und hält die Kopie an, bevor sie beginnt. Dateien werden
so kopiert, wie sie sind, Zeilenenden eingeschlossen.

*Kopieren* sendet die Dateien über SFTP nach `engines/<engine>/` im Ordner der Maschine, im Hintergrund mit der
verbleibenden Zeit und *Abbrechen*, nach einer Unterbrechung dort fortgesetzt, wo es aufgehört hat; der SHA-256 jeder
Datei wird nach der Kopie mit dem dieses Computers verglichen, und Dateien, die schon da und gleich sind, werden nicht
erneut gesendet. Unter Windows behält oder bekommt das Programm sein `.exe`; anderswo wird es ausführbar gemacht
(`chmod 755`). Dann wird der Pfad der Engine auf der Maschine auf die Kopie gesetzt und so geprüft, wie *Engines
prüfen* es tut. Auf einem Mac braucht ein von einem anderen Computer kopiertes Programm, das nicht startet, unter
Umständen eine Signatur oder muss die Quarantäne verlassen: Die Meldung gibt die beiden Befehle an, die dort
auszuführen sind (`codesign -s -` und `xattr -d com.apple.quarantine`); das Programm führt sie nicht selbst aus.

Die Spalte *Build* vergleicht die Engine dort mit der dieses Computers, über ihren SHA-256 (*Engines prüfen* und
*Vergleichen* tun das, wobei der Hash des Programms dieses Computers einmal berechnet wird, bis es sich ändert):
*derselbe Build wie dieser Computer* oder *ein anderer Build dort*, mit *Durch den dieses Computers ersetzen*. Das
Ersetzen kopiert das Programm dieses Computers in den Ordner der Engine im Ordner der Maschine und lässt die Engine
dorthin zeigen: Ein Programm anderswo auf der Maschine (`/opt/engines/stockfish`) wird nie überschrieben, es bleibt,
wie es war.

### Bücher und Tablebases auf einer Maschine
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Prüfen, was die Einstellung verwendet* vergleicht das Eröffnungsbuch und die eigenen Bücher der Teilnehmer in der
Einrichtung mit dem Bücherordner der Maschine, nach Größe und SHA-256 (ein Buch gleichen Namens, aber mit anderem
Inhalt ist nicht dasselbe Buch), und die Syzygy-Dateien der Einrichtung mit höchstens 6 Steinen mit dem
Tablebase-Ordner der Maschine, nach Name und Größe. *Fehlende Dateien übertragen* kopiert, was fehlt oder abweicht, im
Hintergrund, mit der verbleibenden Zeit; jedes Buch wird nach dem Kopieren über seinen Hash geprüft, jede Tabelle über
ihre Größe, und eine abgebrochene Kopie wird fortgesetzt. Tabellen mit 7 Steinen werden nicht angeboten: Sie belegen
Terabytes. Ein Turnier startet auf einer Maschine nicht ohne sein Buch; fehlende Tablebases sind eine Warnung (die
Abschätzung und die Engines kommen ohne sie aus).

### Ein Turnier auf einer Maschine
<!-- en: a-tournament-on-a-machine 8f918d9a -->

In der Einrichtung wählt *Spielen auf* (unter *Parallelität*), wo das Turnier spielt: *Dieser Computer* oder eine
Maschine. *Start* bereitet es dann dort vor: fastchess-gui muss dort in dieser Version installiert sein (sonst
*fastchess-gui aktualisieren*), jede Engine muss ihren Pfad auf der Maschine haben (sonst bietet der Start *\<engine\>
nach \<machine\> kopieren…* an), und die Bücher der Einrichtung müssen in ihrem Bücherordner liegen (sonst sagt der
Start, welches fehlt, und *Fehlende Dateien übertragen* legt es dort ab); fehlende Tablebases sind nur eine Warnung.
Das Turnier bekommt seinen Ordner in `tournaments/` im Ordner der Maschine, mit den Engines, Büchern und Tablebases an
ihren Pfaden dort, und sein Prozess startet dort außerhalb der SSH-Sitzung (`fastchess-gui --run <folder> --detach`):
Es spielt weiter, was auch immer mit dem Fenster oder dem Netzwerk geschieht. Das Fenster verfolgt es wie ein Turnier
dieses Computers: Live, Partien, die Tabelle, Pause, Stopp und die Änderungen, über den Steuerkanal seines Prozesses
(`fastchess-gui --attach <folder>` über SSH).

Seine Karte auf der Startseite sagt *Auf \<machine\>* und *Verbinde mit \<machine\>…*, solange die Verbindung unterbrochen
ist: Das Fenster verbindet sich von selbst neu, nach jedem Fehlschlag etwas später, und nimmt die Ereignisse dort auf,
wo seine Kopie aufgehört hat. Das Journal und die PGN-Dateien werden, während sie wachsen, alle paar Sekunden in den
Ordner des Turniers hier zurückkopiert, damit Elo-Listen, Exporte und die Checkliste vor dem Einsenden sie lesen; der
Ordner der Maschine bleibt die Referenz (`machine.json` hier sagt, welche Maschine und welcher Ordner). Das Schließen
des Fensters lässt es dort weiterspielen; der nächste Start zeigt es wieder. *Fortsetzen* eines unterbrochenen
Turniers einer Maschine setzt es dort fort. Das Budget der Maschine (ihre Kerne und ihr Speicher) teilen sich ihre
eigenen Turniere, getrennt von denen dieses Computers. Ein Turnier spielt auf genau einer Maschine: Seine Partien werden
nicht auf mehrere verteilt.

Eine Maschine braucht einen OpenSSH-Server (Linux, macOS, BSD; Windows 10 1809 und neuer), mit einem RSA-Host-Schlüssel
und SFTP, beides standardmäßig vorhanden. Befehle laufen über `sh`, welche Shell der Benutzer auch hat (fish oder csh
sind kein Problem), und unter Windows über cmd, die Standard-Shell seines OpenSSH-Servers.

### Von diesem Computer aus gespielte Engines
<!-- en: engines-played-from-this-computer 8c14dc16 -->

Eine Engine der Bibliothek kann auf einer Maschine laufen, während das Turnier hier spielt: eine GPU-Engine auf dem
GPU-Rechner, eine für ein anderes System gebaute Engine, eine Maschine, die einer einzigen Engine vorbehalten ist. Auf
ihrer Karte wählt *Läuft auf* zwischen *Dieser Computer* und einer Maschine; *Programm dort* und *Arbeitsverzeichnis
dort* sind dann Pfade auf der Maschine (leer: der Ordner des Programms dort), und die Knöpfe *…*, auch die der Optionen,
die eine Datei angeben, durchsuchen die Dateien der Maschine. Sobald es Maschinen gibt, bietet *Hinzufügen…* auch *Ein
Programm auf \<machine\>…* an, *Importieren…* auch *Alle Engines eines Ordners auf \<machine\>…*, und *Engine
hinzufügen…* der Maschine bietet *Ein Programm dort…* an. Die Engine wird wie eine Engine dieses Computers gelesen,
geprüft und in der Konsole, im Schnelltest und in Turnieren gespielt. Ihr Pfad ist zugleich der in der Engine-Liste der
Maschine, und der SHA-256 ihres Programms dort zeigt, wann sie neu gebaut wurde (Δ, neu gelesen).

Die Maschine muss einmal getestet worden sein (*Verbindung testen*), damit ihr Host-Schlüssel bekannt ist: Eine Engine
verbindet sich nie mit einer Maschine, deren Schlüssel nicht gezeigt und für vertrauenswürdig erklärt wurde. Die
Passphrase des Schlüssels, falls er eine hat, wird einmal pro Sitzung abgefragt, vor dem ersten Lesen oder dem Start,
und im Speicher behalten: Der Prozess des Turniers bekommt sie vom Fenster und behält sie, damit das Turnier
weiterspielt, nachdem das Fenster geschlossen wurde. `--run` in einem Terminal fragt nicht danach: Verwende dort den
SSH-Agent.

Jede Engine wird über ein Relais gestartet, `fastchess-gui --remote-engine <key>`, das der Prozess bedient, der sie
spielt: eine SSH-Verbindung zur Maschine für höchstens zehn Engines (die `MaxSessions` eines OpenSSH-Servers), jede
Engine aus ihrem Ordner dort gestartet. Eine Engine, deren Relais wegfällt, wird auch dort beendet, über ihre
Prozess-ID, wenn sie nicht von selbst endet (auf einer Linux- oder macOS-Maschine; auf einer Windows-Maschine wird ihr
Kanal geschlossen).

*Prüfen* misst die Umlaufzeit zur Maschine, den Median einiger `isready`. Sie wird auf die Uhr angerechnet: Die
Zusammenfassung der Einrichtung warnt, wenn sie mehr als 5 % des Inkrements beträgt, oder ohne Inkrement der Zeit für
einen Zug, mit *Toleranz auf … ms setzen* (das Doppelte der Umlaufzeit). Eine nie getestete Maschine verhindert den
Start, ebenso ein Turnier, das auf einer anderen Maschine läuft als der der Engine (eines, das auf der Maschine der
Engine läuft, spielt sie dort als seine eigene Engine).

Während das Turnier spielt, hat die Startseite unter den Kernen eine Zeile *Maschinen*: jede Maschine, verbunden und was
dort spielt, oder seit wann sie nicht antwortet. Eine Maschine, die nicht antwortet, ist ein Eintrag unter *Zu
erledigen*: Die Partien ihrer Engines werden zurückgestellt und erneut versucht, die anderen gehen weiter, mit *Erneut
versuchen* und *\<machine\> testen*. Eine Partie, die eine Engine verloren hat, weil ihre Verbindung abbrach (ihr Ende
als Verbindungsverlust), wird annulliert und neu gespielt und mit ihrem Grund ins Journal geschrieben
(`connection to its machine lost: <machine>`); *Zustand* zählt sie gesondert, nicht als Absturz der Engine. Bei einer
Maschine, deren Verbindung innerhalb einer Stunde mehr als dreimal abbricht, werden ihre Engines zurückgestellt. Die
Kerne, der Speicher und die GPUs dieses Computers zählen die Engines auf Maschinen nicht mit, nur ihre Relais; das
Budget der Maschine, das sich ihre eigenen Turniere teilen, zählt sie noch nicht.

### Eine Windows-Maschine
<!-- en: a-windows-machine 58ae029a -->

Windows 10 (1809 und neuer) und Windows 11 enthalten einen OpenSSH-Server, der standardmäßig nicht installiert ist.
In PowerShell, als Administrator:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

Die Installation öffnet Port 22 in der Firewall. Lege dann deinen öffentlichen Schlüssel in
`C:\Users\<user>\.ssh\authorized_keys` ab, oder, für ein Konto der Gruppe Administratoren, in
`C:\ProgramData\ssh\administrators_authorized_keys`, die nur Administratoren und SYSTEM lesen dürfen (die Gruppen
werden über ihre SIDs angegeben, die in jeder Sprache von Windows gleich sind):

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Behalte cmd als Shell des Servers: fastchess-gui spricht cmd, nicht PowerShell (wurde ein Wert `DefaultShell` unter
`HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH` gesetzt, entferne ihn). Wähle auf der Karte der Maschine die
`fastchess-gui.exe` des Windows-Release, dann *fastchess-gui installieren*.

## Dateien
<!-- en: files 837f3661 -->

Alles liegt im Profilordner: `~/.local/share/fastchess/fastchess-gui/` unter Linux,
`%APPDATA%\fastchess\fastchess-gui\` unter Windows, `~/Library/Application Support/fastchess/fastchess-gui/` unter
macOS. Dateien, die das Programm anlegt, sind für andere Benutzer nicht beschreibbar, und `settings.json` ist nur für
seinen Besitzer lesbar, sobald es ein lichess-Token enthält. Die **Einstellungen** zeigen den Profilordner, mit
*Ordner öffnen*.

### Portabler Modus
<!-- en: portable-mode 29ddbe3b -->

Liegt eine Datei namens `portable` (leer, ohne Endung) neben dem Programm (neben `fastchess-gui.exe` oder
`fastchess-gui`, unter macOS neben `fastchess-gui.app`), ist das Profil stattdessen der Ordner `data` neben dem
Programm, und der Standard-Turnierordner ist `data/runs`: Nichts wird anderswo geschrieben, außer in den Ordnern, die
für Turniere, Exporte und Ähnliches gewählt werden. Die Pfade dessen, was im Ordner des Programms liegt (Engines,
Bücher, Tablebases, Testsuiten, die PGN-Dateien der Elo-Listen und der Einrichtung, die gewählten Ordner), werden in
den Dateien des Profils relativ zu ihm gespeichert, so dass sich der ganze Ordner verschieben, auf eine andere
Maschine kopieren oder von einem USB-Stick starten lässt, egal mit welchem Laufwerksbuchstaben, und seine Engines
wiedergefunden werden. Die **Einstellungen** zeigen *Portabler Modus* neben dem Profilordner. Um ein bestehendes Profil
mitzunehmen, kopiere seine Dateien nach `data`. `--profile` wählt weiterhin einen anderen Ordner, außerhalb des
portablen Modus. Ein Turnier behält die Pfade der Engines aus der Zeit, als es angelegt wurde: Setze ein
unvollendetes fort, bevor du den Ordner verschiebst. Entferne unter macOS zuerst die Quarantäne (siehe das README):
Sonst führt macOS die Anwendung von einem temporären Ort aus, an dem die Datei `portable` nicht liegt.

### Der Profilordner
<!-- en: the-profile-folder 91838f38 -->

- `engines.json`: die Engine-Bibliothek, und `engines.json.bak` ihre vorige Version, gelesen, wenn die erste verloren
  ist;
- `setup.json`: die letzte Einrichtung; `profiles.json`: deine Profile der Abschnitte der Einrichtung;
  `templates/*.json`: die gespeicherten Vorlagen; `queue.json`: die Warteschlange;
- `book-cache/`: die in Polyglot-Bücher umgewandelten PGN-Bücher für die eigenen Bücher der Teilnehmer (nach 30 Tagen
  ohne Verwendung entfernt);
- `layout.ini`: das Fensterlayout und die Bereiche des Arbeitsbereichs; `layouts.json`: die nach Namen gespeicherten
  Layouts; `fastchess-gui.log`: das Protokoll der letzten Sitzung;
- `suite.json`: die letzte Einrichtung der Seite Suiten; `suites/<suite>-<date>.json`: die beendeten Läufe der
  Testsuiten;
- `board.json`: die Einstellungen der Analyse und der Partien der Seite Brett; `analyses/*.json`: die Zug für Zug
  analysierten Partien, je eine Datei; `games/*.pgn`: die auf der Seite Brett gespielten Partien;
- `settings.json`: der Turnierordner, wenn in den **Einstellungen** ein anderer gewählt wurde, *Software-Rendering*,
  der Ordner von *Alle Engines eines Ordners…*, der Tester und der Ort von *Für eine Rangliste exportieren…*, das
  lichess-Token, Größe und Lage des Fensters, die Breite des Anleitungsbereichs, ob die Karte Erste Schritte
  ausgeblendet ist, die Suche nach einer neuen Version (gewünscht oder nicht, wann sie zuletzt lief, das gefundene Release, der
  ausgeblendete Hinweis), die Darstellung (Brettfarben, Figuren, Zugtöne) und das Budget der Maschine (Kerne, Speicher
  und GPUs für die Turniere, *Starten, wenn Kerne frei sind*);
- `logos/`: die Logos der Engines, Kopien der auf ihren Karten gewählten Bilder;
- `machines.json`: die entfernten Maschinen der **Einstellungen** (ihre Adresse, ihr Ordner, ihre Budgets, die Pfade
  der Engines und was der letzte Test ergeben hat; keine Passphrase), und `known_hosts.json` die vertrauten
  Host-Schlüssel, der Maschinen und des Tunnels;
- `running.json`: die gerade spielenden Turniere, mit ihren Gewichten, jedes entfernt, wenn es anhält; beim Start
  gefunden, sagt es, was wieder anzuzeigen (ein Turnier, das ohne das Fenster weitergespielt hat) oder fortzusetzen
  ist;
- `runs/`: die Turniere, sofern kein anderer Ordner gewählt wurde (siehe
  [Der Ordner eines Turniers](#der-ordner-eines-turniers)).

### Der Ordner eines Turniers
<!-- en: a-tournaments-folder 76959723 -->

`runs/<date>_<A>_vs_<B>/` für ein Match, `runs/<date>_<name>/` für ein Turnier (in `runs/` oder im gewählten Ordner),
je ein Ordner, der sich verschieben, kopieren, zippen und verschicken lässt:

- `tournament.json`: das Format, die Teilnehmer (Engines mit ihren Optionen kopiert) und die Bedingungen;
- `setup.json`: die Einrichtung der Seite *Neues Turnier*, mit der es gestartet wurde, die *Klonen* und *Neu starten
  mit…* lesen (siehe [Warteschlange und Neustart](#warteschlange-und-neustart)); ein Turnier, das eine ältere Version
  gestartet hat, hat keine;
- `journal.jsonl`: alles, was seitdem geschehen ist, eine Zeile pro Ereignis (begonnene Partiepaare, beendete
  Partien, Änderungen, Paarungen des Schweizer Systems und des K.-o.-Systems); zusammen mit den PGN-Dateien ist es das,
  was ein Fortsetzen liest. Eine Partie steht auf der Festplatte, bevor ihre Zeile geschrieben wird, so dass ein
  Stromausfall höchstens die laufenden Partien kostet. Partietext, auf den keine Zeile verweist (eine Partie, die eine
  Änderung während des Speicherns verworfen hat, oder die ein Absturz hinterlassen hat), wird durch Leerzeilen ersetzt,
  so dass andere PGN-Viewer nur die Partien des Turniers zeigen;
- `tournament.lock`: gehalten vom Programm, das das Turnier spielt; ein anderes Programm, das den Ordner öffnet, zeigt
  es nur an;
- `<name>.pgn`: Zugkommentare im Stil von cutechess / fastchess, `{+0.35/18 1.234s}`, mit der Bewertung aus der Sicht
  der Seite, die gezogen hat; die Partien in der Reihenfolge, in der sie endeten (das Journal findet jede an ihrer
  Stelle in der Datei; *Alle exportieren…* im Tab *Partien* und die Datei von *Jede Partie zusätzlich anhängen an*
  folgen dem Spielplan);
- `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, mit der Bewertung aus der Sicht
  von Weiß, wie ChessBase / Fritz und Lichess sie lesen;
- `<name>` ist der Name des Turniers bei seiner Anlage (eine spätere Umbenennung behält die Dateien), wobei die
  Zeichen, die Windows nicht erlaubt, durch `_` ersetzt und auf 64 Bytes gekürzt werden; `tournament.json` hält beide
  Namen fest. Ein Turnier ohne Namen, ein älteres oder ein aus 0.2.x importiertes Match hat `games.pgn` und
  `games-chessbase.pgn`;
- `lichess.json`: die lichess-Übertragung und die Runde, an die seine Partien gingen (kein Token);
- `processor.tsv`: die Prozessorzeit, die die Engines jeder Partie verbraucht haben, für die *Prozessornutzung* eines
  fortgesetzten Turniers;
- `fastchess-gui.log`: das Protokoll der Prozesse, die es gespielt haben (jedes Turnier spielt in einem eigenen
  Prozess); `control.json`, während eines es spielt: wo sein lokaler Socket ist (`control.sock` im Ordner, oder im
  Laufzeitordner des Benutzers, wenn dieser Pfad zu lang ist; eine Named Pipe unter Windows), und welcher Prozess
  lauscht;
- `logs/game-N.log`: jede mit beiden Engines während Partie N (die im Tab *Partien* gezeigte Nummer) ausgetauschte
  Zeile, mit der Zeit, zu der sie gesendet oder gelesen wurde (`W>` an die Engine von Weiß gesendet, `W<` aus ihrer
  Ausgabe gelesen, `W!` aus ihrer Fehlerausgabe gelesen: Assertions, Absturzmeldungen), für die Partien, die durch
  Zeit, durch einen Absturz, einen Hänger oder einen illegalen Zug verloren wurden (oder für jede Partie, falls
  gewünscht). Die ersten 300 Zeilen jeder Engine und die letzten 5000 Zeilen der Partie werden behalten, in der
  Reihenfolge, in der sie kamen. Das ist es, was ein Engine-Autor braucht, um einen Fehler nachzustellen. Bei einer
  WinBoard-Engine zeigt es die UCI-Seite; `FCG_WB2UCI_LOG` liefert die WinBoard-Seite;
- `logs/start-<engine>.log`: der letzte gescheiterte Versuch, eine Engine zu starten, die nicht startet: ihr
  Programm, der Fehler und die bis dahin mit den Engines dieser Partie ausgetauschten Zeilen (siehe
  [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann)).

Ein von jemand anderem erhaltener Ordner lässt sich gefahrlos öffnen: Die Datei von *Jede Partie zusätzlich anhängen
an* muss eine `.pgn`-Datei sein (ein anderer Name wird mit einer Warnung ignoriert), und ein Ordner, dessen Journal,
PGN-Dateien oder Sperrdatei symbolische Links sind, wird nicht zum Schreiben geöffnet, so dass er das Programm nicht
dazu bringen kann, außerhalb von ihm zu schreiben. Wird er fortgesetzt, laufen die Engines, die seine
`tournament.json` nennt, wie bei jedem Turnier.

Mit Version 0.2 gespielte Matches erscheinen auf der Startseite: *Importieren* macht sie zu beendeten Turnieren.

## Befehlszeile
<!-- en: command-line bba4b98e -->

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH] [--software]
              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]
              [--quit-after SECONDS]
fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json [--tlcs UDP_PORT [--tlcs-lanes]]
              [--web TCP_PORT] [--tunnel [localhost.run|ssh://USER@HOST[:PORT][/REMOTE_PORT]]] [--live-files [DIR]] [--lichess ROUND_URL|ROUND_ID|new]
              [--control stdio|socket] [--detach] [--allowance THREADS:MEMORY_MB:GPUS:ONE]
fastchess-gui --attach TOURNAMENT_FOLDER
fastchess-gui --version
```

- `--profile DIR` verwendet einen anderen Profilordner;
- `--autostart` startet beim Start die gespeicherte Einrichtung (statt das Turnier fortzusetzen, das lief, siehe
  [Pausieren, anhalten und fortsetzen](#pausieren-anhalten-und-fortsetzen));
- `--size WxH` öffnet das Fenster in dieser Größe, zentriert, statt so, wie es verlassen wurde, und lässt die für den
  nächsten Start gespeicherte Größe und Lage unberührt;
- `--software` lässt den Prozessor das Fenster zeichnen (den Software-Renderer von SDL) statt OpenGL, wie es
  *Software-Rendering* in den **Einstellungen** tut, für diesen Start (siehe
  [Wie das Fenster gezeichnet wird](#wie-das-fenster-gezeichnet-wird));
- `--shot` speichert nach der angegebenen Zahl von Sekunden (standardmäßig 5) einen Screenshot des Fensters, mit
  WINDOW im Vordergrund, und beendet sich, sobald jeder Screenshot gemacht ist. Es lässt sich wiederholen. WINDOW ist
  eine Seite (`home`, `setup`, `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`,
  `ratings-progress`, `console`, `settings`) oder ein Tab (`live`, `games`, `openings`, `standings`, `schedule`,
  `bracket`, `broadcast`). Die Maus wird in diesem Modus ignoriert, und unter Linux wird das Fenster außerhalb des
  Bildschirms gerendert (es erscheint kein Fenster; setze `SDL_VIDEODRIVER`, um das zu ändern); das Fenster öffnet
  sich in der Standardgröße oder mit `--size`, und die gespeicherte Größe und Lage bleiben unberührt;
- `--focus WINDOW` öffnet beim Start diese Seite oder diesen Tab, oder: `engines-card` (die Karte der ersten Engine),
  `engines-refresh` (*Aktualisieren: die neuen Engines des Ordners*), `engines-ratings=FILE` (die Vorschau dieser
  Rangliste), `setup-picker` und `setup-suggest` (der Filter der Bibliothek fokussiert, oder die vorgeschlagenen
  Gegner ausgewählt), `setup-open=ROW` und `setup-profiles=ROW` (eine Zeile der Einrichtung geöffnet, oder ihre
  Profilliste: `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (das
  eigene Buch des ersten Teilnehmers), `submission-export` (*Für eine Rangliste exportieren…*), `ratings-players` (das
  Fenster *PGN-Spieler*), `clone=DIR` (die Seite *Neues Turnier*, gefüllt durch *Klonen* des Ordners dieses
  Turniers), `palette[=TEXT]` (die Befehlspalette, mit TEXT darin eingetippt), `game-menu=N` (das Menü
  von Partie N im Tab *Partien*, ein Ergebnis gewählt), `end-menu=N` (*Mit einem Ergebnis beenden* für die Partie von
  Platz N, ab 1), `big-screen[=N]` (das Großbild, mit der Partie von Platz N), `live-back=N` (die verfolgte Partie in
  *Live*, um N Halbzüge zurückgeblättert), `engines-console` (die Konsole der ersten Engine), `board-analyse` (die
  Seite Brett, analysiert), `board=FILE` (eine PGN-, FEN- oder EPD-Datei auf der Seite Brett; `board-analyse=FILE`
  analysiert, `board-review=FILE` ihre erste Partie Zug für Zug analysiert), `board-edit` (der Stellungseditor),
  `board-play` und `board-play-start` (eine Partie zwischen den ersten beiden Engines, gestartet), `machines` (die
  Karte der ersten entfernten Maschine in den **Einstellungen**; `machines-test` getestet, `machines-files` die
  Bücher und Tablebases der Einrichtung mit ihr verglichen), `guide[=ID]` (der Anleitungsbereich, beim Abschnitt ID),
  `guide-search=TEXT` (der Anleitungsbereich, auf der Suche nach TEXT);
- `--select-game N` öffnet Partie N (in der Reihenfolge ihres Endes) im Tab *Partien*, sobald es sie gibt;
- `--quit-after SECONDS` hält das Turnier an und beendet sich;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: siehe [Headless-Modus](#headless-modus);
- `--version` gibt die Version aus und das Protokoll des Kanals, über den das Fenster ein entferntes Turnier verfolgt
  (siehe [Maschinen](#maschinen)), und beendet sich.

## Fehlerbehebung
<!-- en: troubleshooting e8c4882d -->

### Das Programm startet nicht
<!-- en: the-program-does-not-start dc0a0128 -->

- **Windows sagt, das Programm sei unsicher, oder ein Virenscanner entfernt es.** Das Programm ist nicht signiert,
  daher misstrauen ihm Windows SmartScreen und manche Virenscanner. *Weitere Informationen*, *Trotzdem ausführen* lässt
  es starten; einem Virenscanner lässt sich sagen, dass er seinen Ordner zulassen soll. Die Releases werden von GitHub
  Actions aus den öffentlichen Quellen gebaut.
- **macOS weigert sich, es zu öffnen.** Es ist nicht von einem Apple-Entwicklerkonto signiert: Klicke es mit gedrückter
  Ctrl-Taste an (Rechtsklick), Öffnen, dann noch einmal Öffnen; ab macOS 15 versuche einmal, es zu öffnen, dann
  Systemeinstellungen, Datenschutz & Sicherheit, Dennoch öffnen. Oder führe `xattr -dr com.apple.quarantine` auf dem
  entpackten Ordner aus.
- **Eine Meldung sagt, dass keine Art funktioniert, das Fenster zu zeichnen, oder das Fenster bleibt schwarz.** Dem
  Grafiktreiber fehlt OpenGL 3.2, oder er versagt: Starte mit `--software` (der Prozessor zeichnet das Fenster), und
  wenn das funktioniert, hake *Software-Rendering* in den **Einstellungen** an. Unter Wayland ohne Grafiktreiber starte
  mit `SDL_VIDEODRIVER=x11`. Siehe [Wie das Fenster gezeichnet wird](#wie-das-fenster-gezeichnet-wird).
- **Chinesischer Text erscheint als Kästchen.** Chinesisch braucht eine Systemschrift: Microsoft YaHei unter Windows,
  Noto Sans CJK oder WenQuanYi unter Linux (installiere `fonts-noto-cjk` oder Ähnliches).

### Eine Engine startet nicht
<!-- en: an-engine-does-not-start 202a67d4 -->

- **Ihre Prüfung schlägt fehl, oder ihr Turnier sagt, dass sie nicht starten kann.** Lies den Fehler unter der Engine,
  dann ihr Engine-Log (*Engine-Log*, oder `logs/start-<engine>.log` im Turnierordner): Die Fehlerausgabe der Engine
  nennt oft die fehlende Datei. Die üblichen Ursachen: das Programm wurde verschoben (*Suchen…*), ein Netz oder eine
  Bibliothek wird nicht gefunden (setze das Arbeitsverzeichnis oder `EvalFile` auf ihrer Karte), ein Programm für einen
  anderen Prozessor gebaut (ein Build mit AVX-512 auf einer Maschine ohne), ein langsamer Start (erhöhe
  *Start-Timeout* auf ihrer Karte). Siehe [Wenn eine Engine nicht starten kann](#wenn-eine-engine-nicht-starten-kann).
- **Eine WinBoard-Engine startet nicht aus den Stellungen des Buchs.** Eine Engine mit Protokoll 1 ohne `setboard`
  kann nur aus der Ausgangsstellung beginnen: Gib ihr ein PGN-Buch (Züge), kein EPD-Buch. Siehe
  [WinBoard-Engines](#winboard-engines).
- **Das Turnier startet nicht: Eine Engine spielt kein Chess960.** Jede Engine muss die Option `UCI_Chess960` (UCI)
  oder die Variante `fischerandom` (WinBoard) haben; die Zusammenfassung nennt die, die sie nicht haben.

### Verlorene oder fragwürdige Partien
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Eine Engine verliert früh in ihren Partien durch Zeit.** Häufiger ist sie falsch eingerichtet, als dass sie
  schlecht spielt: zu viele Threads für die Kerne, die die Partien haben, ein Hash, der zu groß ist, um rechtzeitig reserviert
  zu werden, ein Netz, das bei jeder Partie geladen wird, ein langsamer Start. Senke ihre `Threads` und ihren `Hash`,
  entferne das Häkchen bei *Die Engine für jede Partie neu starten*, erhöhe *Start-Timeout*; die Karte *Zustand* und
  *TF* in der Übersicht zeigen die Zeitverluste.
- **Die Engines wirken langsamer, als sie sein sollten.** Zu viele Partien gleichzeitig: Lass *Parallelität* auf
  automatisch, und halte Parallelität × Threads innerhalb der physischen Kerne. *Prozessornutzung* in der Übersicht
  nennt eine Engine, die mehr Kerne nutzt als ihre `Threads` oder auf der Zeit ihres Gegners denkt.
- **Die Zusammenfassung warnt, dass der Speicher nicht reicht.** Die Hashtabellen der Partien würden nicht in den
  freien Speicher passen: Senke den `Hash` (unter *Engine-Einstellungen* oder den der genannten Engine) oder die
  Parallelität.
- **Tablebase-Dateien sollen fehlen oder abgeschnitten sein.** Ein Download wurde mittendrin abgebrochen, oder der
  Ordner enthält nur kleinere Tabellen: Lade die im Tooltip genannten Dateien erneut herunter, oder senke die
  Abschätzung auf die gefundene Steinzahl (*Auf 5 Steine senken*).
- **Ein Buch soll illegale Stellungen oder Duplikate enthalten.** fastchess lässt sie weg; *Bereinigte Kopie
  speichern…* auf der Seite Bücher schreibt ein Buch ohne sie. Siehe [Ein Buch prüfen](#ein-buch-prüfen).
- **Eine Partie wurde falsch entschieden.** Rechtsklick auf sie in *Partien* oder *Spielplan*: Spiele sie neu,
  annulliere sie oder setze ihr Ergebnis, mit dem Grund. Siehe
  [Ein laufendes Turnier ändern](#ein-laufendes-turnier-ändern).

### Probleme bei der Übertragung
<!-- en: broadcasting 4a2e6916 -->

- **TLCV oder ein anderer Viewer verbindet sich nicht.** Starte das Turnier (und seinen TLCS-Server) vor dem Viewer:
  Er meldet sich nicht von selbst neu an. Öffne den UDP-Port im Router für Zuschauer außerhalb des lokalen Netzwerks.
- **Die Webseite ist von außen nicht erreichbar.** Hake *Im Internet veröffentlichen* für einen Tunnel an (siehe
  [Die Webseite ins Internet stellen](#die-webseite-ins-internet-stellen)), oder öffne den TCP-Port im Router.
- **lichess lehnt das Token ab.** Das Token braucht den Scope `study:write`: Erstelle ein neues mit *Token auf lichess
  erstellen…*. Hinter einem Proxy setze `HTTPS_PROXY`.

### Alles andere
<!-- en: anything-else ce59c083 -->

*Problem melden…* in den **Einstellungen** oder auf der Seite **Protokoll** öffnet ein GitHub-Issue, schon ausgefüllt
mit der Version, dem System und den letzten Zeilen des Protokolls (siehe [Ein Problem melden](#ein-problem-melden)).

## Glossar
<!-- en: glossary 0386d0d9 -->

- **Abschätzung**: das Beenden einer Partie vor dem Matt, wenn ihr Ergebnis klar ist: Remis, wenn beide Bewertungen
  nahe 0 bleiben, Verlust, wenn sie weit darunter bleiben, oder was die Tablebases sagen. Siehe
  [Abschätzung](#abschätzung).
- **BayesElo, DrawElo**: ein Wertungsmodell mit einem eigenen Parameter für Remis (DrawElo), aus dem Programm von Rémi
  Coulom.
- **Berger-Tabellen**: die Reihenfolge der Runden eines Rundenturniers, in der jede Engine einmal pro Runde spielt.
- **Buchholz, Sonneborn-Berger**: Feinwertungen: die Summe der Punkte der Gegner (Schweizer System), und die Summe der
  Punkte der besiegten Gegner plus die Hälfte derer der Gegner, gegen die remisiert wurde (Rundenturnier).
- **Chess960, Chess324**: Startstellungen mit gemischter Grundreihe: 960 mit dem König zwischen den Türmen (Rochade auf
  die üblichen Felder), 324 mit König und Türmen an ihrem Platz.
- **ECO**: die Codes der Encyclopaedia of Chess Openings, A00 bis E99.
- **Elo, nElo**: der Wertungsunterschied, für den eine Punktzahl steht. nElo (normalisierte Elo) teilt durch die
  Streuung der Ergebnisse, so dass ihr Wert nicht von der Remisquote abhängt; fishtest und OpenBench geben ihre
  SPRT-Grenzen darin an.
- **EPD, FEN**: eine Zeile, die eine Stellung beschreibt; eine EPD-Zeile kann Operationen tragen, etwa den besten Zug
  `bm` einer Testsuite.
- **Eröffnungssammlung, Buch**: die Stellungen oder Züge, aus denen die Partien beginnen, jede zweimal mit vertauschten
  Farben gespielt.
- **Gauntlet**: Die getesteten Engines (die gesetzten) spielen gegen jede andere Engine; diese spielen nicht
  gegeneinander.
- **H0, H1**: die beiden Hypothesen eines SPRT: Der Elo-Unterschied ist elo0 oder weniger (H0), oder elo1 oder mehr
  (H1).
- **Hash**: der Speicher, den eine Engine für ihre Hashtabelle belegt, in MB.
- **LLR**: das Log-Likelihood-Verhältnis eines SPRT; der Test endet, wenn es seine Grenzen verlässt, die sich aus alpha
  und beta ergeben.
- **LOS**: die Überlegenheitswahrscheinlichkeit (likelihood of superiority), die Wahrscheinlichkeit, dass eine Engine
  stärker ist als eine andere, aus ihren Partien.
- **MultiPV**: eine Engine, die mehrere Linien gleichzeitig durchsucht.
- **Parallelität**: die Zahl der gleichzeitig gespielten Partien.
- **Pentanomial**: die Ergebnisse der Partiepaare, gezählt nach ihrer Summe: 0, ½, 1, 1½ oder 2 Punkte. Es ergibt
  engere Fehlerbalken als das Zählen einzelner Partien, da sich die beiden Partien eines Paars eine Eröffnung teilen.
- **PGN**: das Textformat für Schachpartien: Tags wie `[White "…"]`, dann die Züge.
- **Polyglot**: das Binärformat für Eröffnungsbücher (`.bin`), das die meisten Programme lesen, mit einem Gewicht für
  jeden Zug.
- **Ponder**: Denken auf der Zeit des Gegners; in den Turnieren von fastchess-gui aus.
- **Setzplatz**: ein Platz in der Reihenfolge der Teilnehmer: die getesteten Engines eines Gauntlets, die Favoriten
  eines K.-o.-Turniers.
- **SPRT**: der sequenzielle Wahrscheinlichkeitsquotiententest (sequential probability ratio test), der so lange
  Partien spielt, bis er mit den gewählten Fehlerraten (alpha, beta) sagen kann, ob eine Engine elo1 oder mehr gewinnt
  (H1) oder nicht mehr als elo0 (H0).
- **Syzygy**: die Endspiel-Tablebases, die das Ergebnis jeder Stellung mit wenigen Steinen liefern (WDL-Dateien,
  `.rtbw`).
- **TF**: time forfeits, die durch Zeit verlorenen Partien.
- **Threads**: die Prozessor-Threads, mit denen eine Engine sucht.
- **TLCS, TLCV**: Tom's Live Chess Server und Viewer, ein Protokoll, um Engine-Partien live zu verfolgen.
- **UCI, WinBoard (CECP)**: die beiden Protokolle, über die Programme mit Schach-Engines sprechen.
- **WDL**: die Gewinn-, Remis- und Verlustchancen, die eine Engine mit ihrer Bewertung angibt.
- **Zyklus**: in einem Rundenturnier jede Paarung einmal mit jeder Farbe gespielt (cutechess nennt es eine Runde, mit
  2 Partien pro Begegnung).
