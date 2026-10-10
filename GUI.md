# fastchess-gui

Interface graphique légère pour [fastchess](https://github.com/Disservin/fastchess) : choisir deux moteurs,
lancer un match (nombre fixe de parties, infini ou SPRT), au temps ou à nœuds fixes, regarder les parties en
direct, les relire et les exporter en PGN annoté, suivre l'Elo et le LLR.

L'interface est en français ou en anglais selon la langue du système. On peut la forcer dans
Configuration → *PGN et divers* → *Langue*.

Ce dépôt est un fork de fastchess (branche `gui`, remote `upstream`). La ligne de commande upstream est
intacte : `make` construit toujours `fastchess`, et les 165 tests upstream passent.

## Construire

Dépendances : un compilateur C++20 et CMake ≥ 3.24. SDL3, Dear ImGui (docking), ImPlot, nanosvg et
stb_image_write sont téléchargés par CMake à des versions figées et liés statiquement.

```bash
# Linux
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/fastchess-gui

# Windows, cross-compilé depuis Linux (paquet mingw-w64-gcc)
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build-win
# -> build-win/fastchess-gui.exe, autonome (seulement des DLL système)
```

Cibles : `fastchess-gui`, `fastchess` (la CLI, depuis le même cœur), `fastchess-gui-tests`.

```bash
./build/fastchess-gui-tests          # tests de la GUI (dont un vrai tournoi avec le moteur aléatoire)
make -C app tests && ./app/fastchess-tests   # tests upstream, à lancer depuis la racine
```

## Utilisation

1. **Moteurs** : *Ajouter…* un exécutable UCI. Ses options sont lues automatiquement ; seules les valeurs
   différentes du défaut sont envoyées (`setoption`). *Dupliquer* permet d'avoir le même binaire avec
   d'autres options (par exemple `Hash` ou `Skill Level`).
2. **Configuration** : A (le moteur testé, les statistiques sont de son point de vue) et B (la référence),
   le type de match, la cadence, le parallélisme, le livre d'ouvertures, l'adjudication.
3. **Lancer**. *Pause* ne démarre plus de nouvelle paire : les parties en cours, et la revanche de leur
   paire, vont à leur terme. *Arrêter* : soit après les parties en cours (aucun résultat perdu), soit
   tout de suite (les parties en cours sont abandonnées et ne comptent pas).

### Parallélisme

Le nombre de cœurs est mesuré au démarrage. En mode automatique :

- **nœuds fixes** : une partie par thread matériel (l'Hyper-Threading ne change pas un compte de nœuds) ;
- **au temps** : cœurs physiques − 1, pour que les moteurs ne se ralentissent pas entre eux.

L'affinité CPU épingle chaque partie sur un processeur. fastchess ne la gère que pour des moteurs à un
thread : elle est désactivée automatiquement si un moteur a `Threads` > 1.

### Fenêtres

| Fenêtre | Contenu |
|---|---|
| Direct | un onglet par partie simultanée : échiquier, pendules, recherche en cours (profondeur, score, nœuds, nps, PV en SAN), coups annotés, courbe d'éval |
| Parties | parties terminées (filtres gains/nulles/pertes/temps/crash/adjudication), relecture (← → Début Fin, clic sur la courbe), export |
| Résultats | Elo et nElo avec IC 95 %, LOS, score, V/N/D, pentanomial, LLR et bornes du SPRT, pertes au temps et crashs ; *Copier le résumé* |
| Graphiques | Elo ± IC et LLR (ou nElo) selon le nombre de parties, paires, longueur des parties, fins de partie, profondeur et NPS moyens par moteur |
| Console | la sortie de fastchess et les messages de la GUI |

Les chiffres sont calculés par les classes Elo et SPRT de fastchess : ce sont ceux de la ligne de commande.

### Fichiers

Tout est dans le dossier de profil (`~/.local/share/fastchess/fastchess-gui/` sous Linux,
`%APPDATA%\fastchess\fastchess-gui\` sous Windows) :

- `engines.json`, `setup.json`, `layout.ini` (disposition des fenêtres), `fastchess-gui.log` ;
- `runs/<date>_<A>_vs_<B>/` pour chaque match :
  - `games.pgn` : commentaires `{+0.35/18 1.234s}` (style cutechess / fastchess, score du camp qui joue) ;
  - `games-chessbase.pgn` : `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}` (éval du point de
    vue des Blancs, lue par ChessBase / Fritz et Lichess) ;
  - `config.json` : l'état fastchess (sauvegardé toutes les 20 parties).

### Options de ligne de commande

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH]
              [--shot FICHIER.png[@FENÊTRE[@SECONDES]]]... [--select-game N] [--quit-after SECONDES]
```

`--profile` utilise un autre dossier de profil ; `--autostart` lance le match enregistré dès l'ouverture ;
`--shot` enregistre une capture de la fenêtre (répétable), puis quitte — utile pour vérifier le rendu sans
écran interactif. FENÊTRE est l'identifiant stable d'une fenêtre : `setup`, `live`, `games`, `results`,
`charts`, `engines` ou `console`.

## Ce qui a été ajouté à fastchess

Des points d'accroche sans effet tant qu'aucun observateur n'est installé (la CLI n'en installe jamais) :

- `app/src/core/observer.hpp` : interface `GameObserver` (début, réflexion, lignes moteur, coup, fin de
  partie, statistiques) et `PauseGate` (pause par paires, compteur de parties en cours) ;
- numéro de worker par thread dans le pool (`core/threading/slot.hpp`), et correction du double
  `resize` de `ThreadPool` qui créait 2N entrées pour N threads ;
- `IProcess::setLineObserver` pour lire la sortie des moteurs en temps réel ;
- `PgnBuilder` : style de commentaire `ChessBase` en plus du style habituel ;
- `Logger::setPrintSink` pour la console de la GUI ;
- Windows : `CREATE_NO_WINDOW` quand le parent n'a pas de console (sinon chaque moteur ouvrirait une fenêtre).

Licences des ressources embarquées : [gui/assets/ATTRIBUTION.md](gui/assets/ATTRIBUTION.md).
