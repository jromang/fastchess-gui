# Guide de l'utilisateur de fastchess-gui

Ce guide décrit chaque page, chaque réglage et chaque fichier de fastchess-gui. Le [README](../README.md) dit ce
qu'est le programme, où le télécharger et comment le compiler. Dans le programme, F1 ouvre ce guide à côté de la page,
à la section de ce qui est sous la souris, dans la langue de l'interface (voir
[Aide dans le programme](#aide-dans-le-programme)).

## Démarrage rapide
<!-- en: quick-start 92353f2c -->

### Ton premier tournoi
<!-- en: your-first-tournament 75e9a891 -->

1. **Moteurs.** Dans la barre de gauche, *Moteurs*, puis *Ajouter…* et choisis le programme d'un moteur (un
   exécutable UCI ou WinBoard) : il est lancé une fois pour lire son nom et ses options. *Importer…* > *Une liste de
   moteurs…* reprend les moteurs d'un autre programme (cutechess, Banksia, Arena, ChessGUI), *Importer…* > *Tous les
   moteurs d'un dossier…* tous les programmes d'un dossier. *Vérifier* dit si un moteur fonctionne.
2. **Nouveau tournoi.** *Tournois* > *Nouveau tournoi*. Choisis un *Format* dans sa liste (*Toutes rondes*,
   *Gauntlet*…) et une *Cadence* (*10+0.1* pour un premier essai), et laisse les autres lignes telles quelles.
3. **Participants.** Double-clique sur des moteurs dans la *Bibliothèque*, à côté des participants, ou
   sélectionnes-en plusieurs et appuie sur Entrée.
4. **Lancer.** Le tournoi s'ouvre dans son espace de travail : *Vue d'ensemble* pour le classement et le temps
   restant, *Direct* pour les parties en cours.

Le résumé, à droite de la configuration, indique combien de parties seront jouées et combien de temps elles
prendront, et ce qui n'irait pas (un programme introuvable, trop de threads, un livre manquant), chaque fois avec un
bouton qui le corrige.

### Tester une nouvelle version d'un moteur
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Ajoute les deux versions à la bibliothèque.
2. *Nouveau tournoi*, *Format* : *SPRT gain [0, 3]* (la nouvelle version gagne-t-elle ?) ou *SPRT non-régression
   [-5, 0]* (n'est-elle pas plus faible ?).
3. Choisis la nouvelle version comme **A** et l'ancienne comme **B**, une cadence courte (*10+0.1*) et, dans
   *Ouvertures*, un livre de positions équilibrées (EPD), puis **Lancer**.

Le match s'arrête de lui-même dès que le SPRT tranche : H1 acceptée (A est plus fort, au sens des bornes) ou H0
acceptée (il ne l'est pas). La *Vue d'ensemble* montre l'Elo, le LLR et ses bornes au fil des parties. La
configuration est gardée : la prochaine fois, choisis la version suivante comme A, puis **Lancer**, ou clique sur
*Relancer avec…* sur le match terminé.

### Pour aller plus loin
<!-- en: where-to-go-next 2cd9b9f7 -->

- [Nouveau tournoi](#nouveau-tournoi) : chaque réglage, et des [conseils](#conseils-pour-les-réglages) pour les
  choisir ;
- [Un tournoi en cours](#un-tournoi-en-cours) : ce que montrent les onglets, la pause, la modification d'un tournoi
  pendant qu'il se joue ;
- [Classements](#classements) : une seule liste de classement sur toutes les parties jouées ;
- [Diffusion](#diffusion) : les parties en direct sur une page web, dans TLCV ou sur lichess ;
- [Dépannage](#dépannage) quand quelque chose ne va pas, et le [Glossaire](#glossaire) pour les mots des testeurs.

## La fenêtre
<!-- en: the-window 3a5230d4 -->

### Les pages
<!-- en: the-pages b13b4de2 -->

Une barre de navigation, à gauche, mène à huit pages :

| Page | Contenu |
|---|---|
| Tournois | La page d'accueil : ce qui tourne et ce qui demande une action, la file d'attente, les derniers tournois terminés, tous les tournois du profil, *Nouveau tournoi* |
| Moteurs | La bibliothèque de moteurs : options, vérifications, groupes, Elo de référence, import des listes d'autres programmes |
| Livres | La bibliothèque de livres d'ouvertures, et les ouvertures de chaque livre avec ce qui ne va pas chez elles |
| Échiquier | Une partie ou une position venue de n'importe où, analysée par un moteur de la bibliothèque |
| Suites | Les suites de tests (STS, WAC, ERET…) : des moteurs face aux positions d'un fichier EPD |
| Classements | Une seule liste de classement sur toutes les parties de tous les tournois |
| Journal | La sortie de fastchess lui-même et les messages du programme, et *Signaler un problème…* |
| Réglages | Langue, thème, taille de l'interface, notifications, apparence des échiquiers, dossiers du profil et des tournois, machines distantes, jeton lichess, version, manière de dessiner la fenêtre |

### Aide dans le programme
<!-- en: help-in-the-program 351be20d -->

- **Infobulles.** Laisse la souris sur un contrôle pour voir ce qu'il fait. L'infobulle d'un contrôle que ce guide
  explique plus en détail se termine par *F1 : en savoir plus dans le guide*.
- **F1** ouvre ce guide dans un panneau à droite de la page, à la section de ce qui est sous la souris (une ligne de
  la configuration, une carte de la page d'accueil, un onglet, une colonne du classement) ; sans rien sous la souris,
  à la section du dernier panneau cliqué, sinon de la page. Le **?** rond à côté du titre d'une page ouvre la section
  de la page. F1 de nouveau, ou Échap dans le panneau, le ferme.
- **Le panneau du guide** montre une section avec ses sous-sections, et la page à côté continue de fonctionner : on
  lit l'explication tout en changeant le réglage. Son en-tête a *Retour* et *Avancer* (aussi Alt+Gauche et
  Alt+Droite, ou les boutons latéraux de la souris), *Sommaire* (toutes les sections), le champ de recherche (Ctrl+F
  dans le panneau) et *Ouvrir dans le navigateur* (ce guide sur GitHub, pour la version qui tourne). La recherche
  trouve chaque mot tapé, dans n'importe quel ordre, sans tenir compte de la casse ni des accents ; les titres
  viennent en premier, et les correspondances sont surlignées. Les liens vers d'autres sections s'ouvrent dans le
  panneau, les liens vers des pages web dans le navigateur, et un bloc de code a *Copier*. Fais glisser le bord
  gauche du panneau pour l'élargir ; sa largeur est gardée.
- **Langues.** Le guide est intégré au programme, dans chaque langue de l'interface, et suit la version qui tourne ;
  rien n'est récupéré sur le réseau. Une section dont la traduction manque ou est plus ancienne que le texte anglais
  est montrée en anglais, avec une ligne qui le dit. Les archives des versions le contiennent aussi (`guide.md`, et
  `guide.<language>.md` pour chaque traduction).
- **Ctrl+K**, la palette de commandes, trouve les sections du guide par leurs titres (*Guide : …*), et *Rechercher
  « … » dans le guide* y cherche ce qui a été tapé.
- **Messages.** Un problème que le programme signale nomme le moteur, le fichier ou la ressource, avec sa correction
  sur place ; un **?** à côté ouvre son entrée dans [Dépannage](#dépannage).
- **Premiers pas.** Jusqu'au lancement d'un premier tournoi, la page d'accueil montre les étapes du
  [Démarrage rapide](#démarrage-rapide), chacune avec son bouton, cochée une fois faite ; *Masquer* les range.
- [Signale un problème](#signaler-un-problème) quand le programme ne fait pas ce qu'il devrait.

### La palette de commandes
<!-- en: the-command-palette 725e7934 -->

**Ctrl+K** ouvre la palette de commandes : les pages et les onglets, les moteurs de la bibliothèque (elle ouvre leur
fiche), les livres, les tournois de la page d'accueil, les sections du guide, et les actions possibles sur le moment
(*Pause*, *Reprendre*, *Tout mettre en pause*, *Arrêter*, *Lancer*, *Mettre en file d'attente*, *Relancer … avec…*,
les entrées d'*Importer…*, *Vérifier ceux qui ne l'ont pas été*, *Exporter pour une liste de classement…*, *Copier* le
classement, *Copier la version*, *Signaler un problème…*, les couleurs d'échiquier et les jeux de pièces, les sons des
coups activés ou non…). La casse ne compte pas et les lettres tapées n'ont pas besoin de se suivre (« lc » trouve
*Liste de classement*) ; les noms qui commencent par elles, ou par un mot qui commence par elles, viennent en premier.
Haut et Bas choisissent, Entrée exécute, Échap ferme ; Ctrl+K n'agit pas pendant la saisie dans un champ de texte.

### Langue, thème et taille
<!-- en: language-theme-and-size 954fe416 -->

L'interface est en anglais, français, allemand, espagnol, italien, portugais du Brésil, russe ou chinois simplifié.
Au démarrage, elle suit la première langue préférée du système (l'anglais quand il n'y a pas de traduction pour
elle) ; on peut l'imposer dans **Réglages**. Le chinois utilise une police du système : Microsoft YaHei sous Windows,
Noto Sans CJK ou WenQuanYi sous Linux. Le thème est clair, à moins que le thème sombre, ou celui du système, ne soit
sélectionné dans **Réglages**.

Les traductions espagnole, italienne, portugaise, russe et chinoise sont nouvelles : les corrections des joueurs qui
parlent ces langues sont les bienvenues. Celles de l'interface sont de simples fichiers `.po` dans `gui/i18n`, que
Poedit et ses semblables ouvrent ; celles de ce guide sont `docs/guide.<language>.md`.

Les pages occupent toute la fenêtre, quelle que soit sa taille : les échiquiers de l'onglet Direct grandissent avec
elle, le formulaire de nouveau tournoi se partage en deux colonnes dans une fenêtre large, et la liste des pages
Moteurs, Livres et Classements s'élargit quand on fait glisser son bord droit. Tout (texte, boutons, échiquiers) peut
aussi être agrandi ou réduit, de 80 à 250 % : **Réglages** > Taille de l'interface, ou Ctrl + molette de la souris,
Ctrl + et Ctrl -, et Ctrl 0 pour revenir à 100 %. F11 met la fenêtre en plein écran, et l'en sort.

Un badge sur l'onglet *Direct* et sur le bouton *Tournois* donne le nombre de parties en cours. Un champ numérique
accepte le calcul : `40*60`, `15+10`, avec `+ - * /` et les parenthèses, calculé sur Entrée ou quand on quitte le
champ ; Échap rend au champ sa valeur.

### Les échiquiers et leurs boutons
<!-- en: boards-and-their-buttons 1ed997f4 -->

Sous chaque échiquier qui montre une partie (le rejeu de l'onglet *Parties*, la partie mise en avant dans *Direct*,
un échiquier détaché, la page Échiquier), une rangée de petits boutons va à la position de départ, au coup précédent,
au coup suivant et au dernier, et retourne l'échiquier ; leurs infobulles donnent les touches, qui font de même : ← →
Origine Fin, et F pour retourner. Ceux qui préfèrent les touches masquent les boutons avec **Réglages** >
*Apparence* > *Boutons de navigation sous les échiquiers*, un clic droit dessus, ou la palette de commandes
(*Masquer les boutons de navigation sous les échiquiers*, et *Afficher…* pour les faire revenir). La molette de la
souris sur une liste de coups fait défiler la liste, jamais les coups. **A** sur n'importe quel échiquier ouvre sa
position sur la page [Échiquier](#échiquier) et l'analyse.

### Version et nouvelles versions
<!-- en: version-and-new-versions 70e3e9a2 -->

La version est dans le titre de la fenêtre et dans **Réglages**, dont *Copier* la met dans le presse-papiers avec le
système, pour un rapport de bug. *Me prévenir quand une nouvelle version sort* (désactivé par défaut) demande à GitHub
la dernière version publiée au lancement, au plus une fois par jour et en arrière-plan ; quand elle est plus récente,
la page d'accueil le dit, avec *Page de téléchargement* (la page de la version sur GitHub, dans le navigateur),
*Nouveautés* (ses notes) et *Masquer* (jusqu'à la version suivante). Le programme ne télécharge et n'installe rien,
et un échec (pas de réseau, GitHub injoignable) est seulement écrit dans le journal.

### Signaler un problème
<!-- en: reporting-a-problem 3e4e5989 -->

*Signaler un problème…* (dans **Réglages**, sur la page **Journal**, dans la palette de commandes, et dans le message
d'un échec au démarrage) ouvre un nouveau ticket sur GitHub dans le navigateur, à partir du formulaire de rapport de
bug, déjà rempli avec la version, le système (système d'exploitation, processeur, mémoire, carte graphique et pilote)
et les dernières lignes du journal. Relis-le avant de l'envoyer, retire ce que tu ne veux pas montrer (chemins, noms
de moteurs) et dis ce que tu as fait, ce que tu attendais et ce qui s'est passé. Le programme n'envoie rien lui-même.
Les idées vont au même endroit : *New issue*, *Idea*.

### Taille et position de la fenêtre
<!-- en: window-size-and-place 892d41c4 -->

La fenêtre s'ouvre comme elle a été laissée : sa taille, sa position sur les écrans, agrandie ou non. Quand l'écran
où elle se trouvait a disparu (un moniteur débranché), elle s'ouvre au milieu de l'écran principal ; quand sa taille
ne tient plus dans l'écran (une résolution plus basse), à la taille par défaut. Les panneaux sortis de la fenêtre,
comme un échiquier détaché, reviennent aussi là où ils étaient (`layout.ini`), et dans la fenêtre quand leur écran a
disparu. Wayland ne laisse pas un programme choisir la position de sa fenêtre : là, seuls la taille et l'état agrandi
reviennent.

### Rendu de la fenêtre
<!-- en: drawing-the-window caff7bef -->

La fenêtre est dessinée avec OpenGL 3.2. Là où il n'y en a pas (une machine virtuelle sans accélération 3D, un bureau à
distance, un vieux serveur), le programme la dessine à la place avec le rendu propre à SDL : Direct3D 11 sous Windows,
OpenGL 2.1, ou le processeur seul. Le journal dit lequel, et pourquoi OpenGL n'a pas été utilisé ; **Réglages** > Rendu
le montre aussi, avec le nom de la carte graphique quand OpenGL dessine. *Rendu logiciel* à cet endroit (ou `--software`
sur la ligne de commande, pour un lancement) fait dessiner la fenêtre par le processeur à partir du lancement suivant,
pour un pilote graphique qui échoue ou plante. Il prend plus de temps processeur, ne redessine que ce qui change (et ce
qui bouge, comme les pendules, 15 fois par seconde plutôt que 30 tant que la souris et le clavier sont au repos),
dessine des coins carrés, et les panneaux de l'espace de travail flottent à l'intérieur de la fenêtre principale plutôt
que sur un autre écran. Sous Wayland (et sur macOS), l'image dessinée par le processeur passe tout de même par le pilote
graphique ; s'il n'y en a pas, lance le programme avec `SDL_VIDEODRIVER=x11`. Quand aucune manière de dessiner ne
fonctionne, un message le dit avant que le programme se termine.

### Apparence
<!-- en: appearance decda352 -->

**Réglages** > *Apparence* choisit l'aspect de chaque échiquier du programme (la mosaïque en direct et la partie
suivie, les échiquiers détachés, le grand écran, une partie rejouée dans *Parties*, l'onglet *Ouvertures*, les
ouvertures d'un livre), avec, à côté des choix, un petit échiquier qui les montre :

![Réglages > Apparence : l'échiquier brun et les pièces Celtic](screenshots/appearance.png)

- **Échiquier** : les couleurs des cases, pour le thème clair et pour le thème sombre (l'échiquier du thème en usage
  est dessiné). Un préréglage (*Gris bleu*, les couleurs des versions précédentes ; *Brun* ; *Bleu* ; *Vert* ;
  *Gris*), ou *Personnalisé* : un clic sur *Cases claires*, *Cases sombres* ou *Dernier coup* ouvre un sélecteur de
  couleur, et changer une couleur fait de l'échiquier un échiquier personnalisé à partir des couleurs qu'il avait.
  *Dernier coup* est posé sur les deux cases du dernier coup, sa transparence laissant passer la couleur de la case.
- **Pièces** : un jeu intégré au programme (*Cburnett*, celui par défaut ; *Chessnut* ; *Celtic* ; *Fantasy* ; leurs
  auteurs et leurs licences dans l'infobulle et dans `ATTRIBUTION.md`), ou *Ton propre dossier…* : un dossier de
  12 images SVG nommées `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` et `bK.svg` … `bP.svg` (roi,
  dame, tour, fou, cavalier, pion, blancs puis noirs). Les images sont dessinées avec nanosvg, comme celles intégrées :
  formes simples, remplissages, contours et dégradés ; ni texte, ni filtres, ni images incorporées. Quand un fichier
  manque, dépasse 1 Mo ou n'est pas une image qui peut être dessinée, la page dit lequel et pourquoi, les pièces par
  défaut sont dessinées en attendant, et *Relire* relit le dossier une fois les fichiers corrigés. La page web de la
  diffusion garde les pièces par défaut.
- **Sons des coups** (désactivés par défaut) : *Un son pour chaque coup* joue un bref claquement à chaque coup de la
  partie suivie, dans *Direct* et sur le grand écran, et un plus lourd pour une prise ; pas pour chaque partie de la
  mosaïque. *Volume*, et *Essayer un coup* / *Essayer une prise* pour les entendre. Les sons sont produits par le
  programme (aucun fichier son) ; le périphérique audio n'est ouvert que lorsqu'ils sont activés, et quand il n'y en a
  pas, le journal dit pourquoi et le programme reste muet.

- **Figurines dans la notation** (activées par défaut) : les coups de la page Échiquier, dans ses coups et dans les
  lignes de son analyse, sont écrits avec les pièces du jeu choisi plus haut (Nf3 devient un cavalier et f3) ;
  désactivées, avec des lettres.

La palette de commandes propose aussi ces choix (*Couleurs de l'échiquier : Brun*, *Pièces : Celtic*, *Activer les
sons des coups*…). Ils sont gardés dans `settings.json`, dans le profil ; le dossier des pièces est gardé relatif au
dossier du programme en mode portable.

**Logos des moteurs** : la fiche d'un moteur, dans la page **Moteurs**, a *Logo* > *Choisir…*, une image PNG, JPEG
ou SVG de 2 Mo au plus (4096 pixels au plus dans chaque sens), montrée à côté du nom du moteur dans *Direct*, sur le
grand écran et sur la page web de la diffusion. L'image est copiée dans le dossier `logos` du profil : l'original
peut donc être déplacé ou supprimé, et le profil déplacé en entier ; *Retirer* l'enlève (avec sa copie, à moins
qu'un autre moteur ne l'ait). Le logo peut être changé pendant que le moteur joue.

### L'échiquier 3D
<!-- en: the-3d-board cc0dd596 -->

**Réglages** > *Apparence* > *Échiquier 3D* > *L'échiquier de la page Échiquier en 3D* dessine l'échiquier de la page
[Échiquier](#échiquier) en trois dimensions, comme la photographie d'un vrai échiquier (désactivé par défaut ; tous
les autres échiquiers restent plats). La carte graphique le calcule par tracé de chemins : l'image s'affine quelques
secondes tant que rien ne bouge, quelques millisecondes de la carte à chaque image, puis la carte se repose ; pendant
que l'échiquier tourne, une image plus grossière la remplace. Le petit échiquier d'*Apparence* est alors dessiné en 3D
lui aussi, pour que les choix à côté se voient à mesure qu'ils changent :

- l'éclairage, comme les photographes éclairent une nature morte : *Studio*, *Low key*, *High key*, *Salle
  d'exposition*, *Lumière de fenêtre*, *Heure dorée* ou *Salle de tournoi* ;
- *Exposition* : plus clair ou plus sombre que l'éclairage ne le rend, en diaphs, chacun doublant ou divisant par deux
  la lumière ;
- *Qualité* : les échantillons dont l'image est faite avant de se reposer, 512 par défaut ; moins ménage la puce
  graphique d'un processeur pendant les tournois ;
- *Vernis*, d'un bois ciré et mat à une couche où se reflètent les lumières, et *Brillance*, du satiné au miroir ;
- *Défaut* remet l'exposition, la qualité, le vernis et la brillance comme ils étaient ; l'éclairage reste.

Ce que montre l'échiquier plat y est : la barre d'évaluation, le dernier coup, la pièce prise en main et où elle peut
aller, un roi en échec, les flèches de l'analyse, qui se voient en transparence à travers les pièces, la réponse
qu'attendait le moteur qui a joué le coup en pointillés, le jugement du coup en pastille au-dessus de sa case, et le
cadre d'une ligne montrée à la place de la partie. Un clic gauche choisit une case comme sur l'échiquier plat, sur une
pièce ou sur l'échiquier derrière elle, et une pièce peut être glissée, son fantôme suivant le pointeur (voir
[Échiquier](#échiquier)). Le bouton droit maintenu pendant que la souris bouge fait tourner autour de l'échiquier (un
clic droit sans bouger lâche toujours la pièce prise en main), la molette rapproche, et un clic de la molette ramène la
vue ; retourner l'échiquier (F) fait faire un demi-tour à la caméra.

L'échiquier 3D demande OpenGL 3.3. Là où la fenêtre est dessinée sans lui (le rendu logiciel de
[Rendu de la fenêtre](#rendu-de-la-fenêtre), certaines machines virtuelles et certains bureaux à distance), les
échiquiers restent plats et *Apparence* dit pourquoi. Les pièces sont les Staunton-Pieces de clarkerubber, sous
licence MIT ; *Apparence* donne le lien vers elles.

## Raccourcis clavier
<!-- en: keyboard-shortcuts e0d74f26 -->

| Touches | Où | Ce qu'elles font |
|---|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Partout | La palette de commandes |
| <kbd>F1</kbd> | Partout | Ce guide, à la section de ce qui est sous la souris ; une seconde fois : le ferme |
| <kbd>F11</kbd> | Partout | Plein écran, et retour (la fenêtre propre d'un échiquier détaché quand elle a le clavier) |
| <kbd>Ctrl</kbd>+molette, <kbd>Ctrl</kbd>+<kbd>+</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> | Partout | Taille de l'interface ; <kbd>Ctrl</kbd>+<kbd>0</kbd> : retour à 100 % |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | Un champ numérique | Calculer la valeur ; rendre au champ sa valeur |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | Le panneau du guide | Retour, avancer |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | Le panneau du guide | Rechercher dans le guide |
| <kbd>Échap</kbd> | Le panneau du guide | Le fermer |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Page préc.</kbd> <kbd>Page suiv.</kbd> | La palette de commandes | Choisir |
| <kbd>Entrée</kbd>, <kbd>Échap</kbd> | La palette de commandes | Exécuter, fermer |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Origine</kbd> <kbd>Fin</kbd> | Un échiquier qui montre une partie | Coup précédent, coup suivant, départ, dernier coup |
| <kbd>F</kbd> | Un échiquier | Le retourner |
| <kbd>A</kbd> | Un échiquier | Ouvrir la position sur la page Échiquier et l'analyser ; sur la page Échiquier : lancer ou arrêter l'analyse |
| <kbd>Fin</kbd> | La partie mise en avant dans *Direct* | Revenir au coup en cours |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | La page Échiquier | Coller un FEN, une ligne EPD, un PGN ou une liste de coups |
| <kbd>G</kbd> | La page Échiquier | Analyser la partie, passe après passe ; une seconde fois : l'arrêter |
| <kbd>X</kbd> | La page Échiquier | Afficher ou masquer la menace |
| <kbd>N</kbd>, <kbd>Maj</kbd>+<kbd>N</kbd> | La page Échiquier | Le coup suivant, le coup précédent marqué par l'analyse |
| <kbd>Échap</kbd> | La page Échiquier | Rétablir la partie après qu'une ligne a été montrée ; reposer la pièce prise en main |
| <kbd>←</kbd> <kbd>→</kbd> | Le grand écran | La partie précédente ou suivante |
| <kbd>+</kbd> <kbd>-</kbd> | Le grand écran | Texte plus grand ou plus petit |
| <kbd>Échap</kbd> | Le grand écran | Le quitter |
| <kbd>Tab</kbd>, flèches, <kbd>Entrée</kbd>, <kbd>Espace</kbd> | Nouveau tournoi | Passer d'une ligne, d'une liste ou d'un bouton à l'autre ; ouvrir une ligne ou une liste |
| <kbd>Ctrl</kbd>+<kbd>↑</kbd>, <kbd>Ctrl</kbd>+<kbd>↓</kbd> | Les participants | Envoyer les participants sélectionnés tout en haut, tout en bas |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | Les participants | Les déplacer d'une place |
| <kbd>Suppr</kbd> | Les participants | Les retirer |
| <kbd>Menu</kbd>, <kbd>Maj</kbd>+<kbd>F10</kbd> | Les participants | Ouvrir le menu du participant (ses propres réglages) |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | La bibliothèque de la configuration | Aller à son filtre |
| <kbd>Entrée</kbd> | La bibliothèque de la configuration | Ajouter les moteurs sélectionnés, ou le premier affiché |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | La bibliothèque de la configuration | Sélectionner tous les moteurs affichés |
| <kbd>Ctrl</kbd>+clic, <kbd>Maj</kbd>+clic | Listes | En sélectionner plusieurs |
| <kbd>Suppr</kbd> | La page Moteurs | Retirer le moteur sélectionné (après confirmation) |
| <kbd>Entrée</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | La console d'un moteur | Envoyer la ligne ; faire revenir les lignes précédentes |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> | Le mode sans interface | S'arrêter après les parties en cours |

## La page d'accueil
<!-- en: the-home-page 40031a16 -->

La page **Tournois** est une salle de contrôle : d'abord ce qui tourne et ce qui a besoin de toi, puis le reste.

- **Premiers pas**, jusqu'au lancement d'un premier tournoi : les étapes du [Démarrage rapide](#démarrage-rapide)
  (ajouter des moteurs, préparer un tournoi, le lancer), chacune avec son bouton et cochée une fois faite, et *Lire le
  démarrage rapide*, qui l'ouvre dans le panneau du guide. *Masquer* range la carte.
- **Tout mettre en pause**, à côté d'*Ouvrir un dossier…* et de *Nouveau tournoi*, met en pause les tournois en cours
  (aucune nouvelle paire de parties ne commence, les parties en cours vont jusqu'au bout) et retient la file
  d'attente : aucun tournoi en attente ne commence avant **Tout reprendre**, qui les reprend et laisse la file
  d'attente continuer, aussitôt si un tournoi s'est terminé entre-temps.
- **Cœurs** : les threads matériels de la machine, et ceux que prennent les parties en cours, le reste libre. Une
  partie prend les `Threads` de celui de ses deux moteurs qui en a le plus, puisque les deux moteurs réfléchissent
  chacun à leur tour ; la barre montre le nom de chaque tournoi et son nombre, les détails s'affichant au curseur.
- **À traiter**, quand quelque chose l'exige, chaque fois avec sa correction sur place : un moteur qui ne démarre pas
  (voir [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas) : *Réessayer*, *Retirer…*, *Remplacer…*,
  *Journal moteur*) ; le tournoi qu'une erreur ou un moteur a arrêté, avec ce qui l'a arrêté, *Ouvrir le moteur*,
  *Ouvrir le journal*, *Reprendre*, *Ouvrir* et *Masquer* ; un disque trop plein pour une nuit de parties (moins de
  1 Go libre, ou moins de 2 % d'un disque de moins de 500 Go), celui du dossier des tournois ou d'un tournoi en cours,
  vérifié chaque minute, avec *Ouvrir le dossier* et *Changer le dossier des tournois…* (là où vont les nouveaux
  tournois, comme dans **Réglages**).
- **En cours** : une carte par tournoi en cours, avec son nom, son format, sa cadence et son état, une barre de
  progression avec les parties jouées sur celles prévues et les parties en cours, l'heure à laquelle il finit, et qui
  mène : le premier du classement avec son Elo face au plateau et ses points (un KO : les joueurs restants ; un
  match par équipes : le score des équipes et son Elo), le moteur en tête d'un match avec son Elo et le score, ou le
  LLR d'un SPRT avec ses bornes, puis l'Elo et le score. *Ouvrir* va à son espace de travail, *Pause* le met en pause.
  Un tournoi qui peut être repris s'affiche à la place sous *Interrompu*, avec *Reprendre*. Voir
  [Plusieurs tournois à la fois](#plusieurs-tournois-à-la-fois) pour les poids et les cœurs de chacun.
- **File d'attente** : les tournois en attente, chacun avec sa durée et l'heure à laquelle il commence, et l'heure à
  laquelle tout se termine (voir [File d'attente et relance](#file-dattente-et-relance)).
- **Derniers terminés** : les cinq derniers tournois terminés, avec leur résultat : le score d'un match, la décision
  du SPRT, le vainqueur d'un KO (celui de sa finale), le score d'un match par équipes, ou le premier du classement avec
  les points de ses parties. *Relancer avec…* pour un match, *Cloner* (voir [File d'attente et
  relance](#file-dattente-et-relance)) et *Ouvrir*.
- **Santé** : les plantages ou blocages, les pertes au temps et les coups illégaux des dernières 24 heures, la mémoire
  libre et installée, et les processus de moteurs en cours ; puis les moteurs qui ont perdu ces parties, les trois qui
  en comptent le plus, avec *Ouvrir le moteur* et *Ouvrir le dossier* (le dossier `logs` du tournoi garde le journal
  moteur de chaque partie qui a mal fini). Une perte au temps avant le coup 20 est mise à part : c'est plus souvent un
  moteur mal réglé (trop de threads, un hash trop grand pour être alloué à temps, un réseau chargé à chaque partie, un
  démarrage lent) que sa gestion du temps.
- **Tous les tournois** : tous les tournois du profil, du plus récent au plus ancien, avec *Ouvrir*, *Cloner* et
  *Dossier* ; un clic sur le titre de la carte la replie. Le résultat d'un tournoi terminé s'affiche au curseur,
  par-dessus son état.

La palette de commandes propose aussi ces actions : *Tout mettre en pause*, *Tout reprendre*, *Relancer … avec…*
pour les derniers matchs terminés, *Cloner …* pour le tournoi affiché et les derniers terminés, *Ouvrir le dossier
des tournois* et *Changer le dossier des tournois…*.

## Moteurs
<!-- en: engines 4f15155a -->

La page **Moteurs** est la bibliothèque de moteurs : la liste à gauche, la fiche du moteur choisi à droite.

### Ajouter et vérifier des moteurs
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Ajouter…** enregistre un exécutable UCI. Le moteur est lancé une fois pour lire son nom, son auteur et ses options.
Les options se modifient dans un tableau : les valeurs modifiées sont mises en évidence, et seules les valeurs qui
diffèrent du défaut du moteur sont envoyées avec `setoption` au début de chaque partie. `Threads` et `Hash` viennent
en premier.

- **Dupliquer** crée une seconde entrée pour le même binaire, avec d'autres options (un autre `Hash`, un niveau de
  jeu, un autre fichier de réseau…). Les entrées prennent le nom UCI `id name` au premier ajout et peuvent être
  renommées.
- **Vérifier** lance le moteur, mesure la vitesse à laquelle il répond à `uci` et `isready`, et lui fait jouer un
  coup ; puis il fait passer au moteur les étapes de conformité UCI de fastchess (`id name` et `id author`, positions
  depuis un FEN
  et avec des coups, `go` avec chaque pendule, lignes info avec un score entier…), jusqu'à la première qui échoue.
  Le résultat (✓ ou l'erreur) reste à côté du moteur avec sa date, et *Rapport de conformité UCI* liste les étapes,
  avec ce que le moteur a écrit après celle qui a échoué. Un moteur à qui l'on parle en xboard n'a que la première
  partie.
- **Console…** dialogue avec le moteur, hors de tout tournoi : il est lancé avec le programme, les arguments et le
  dossier de travail de la bibliothèque, et un moteur UCI reçoit `uci` puis les valeurs d'options de la bibliothèque.
  Les lignes tapées sont envoyées telles quelles (Entrée ; Haut et Bas font revenir les précédentes), et le journal
  montre chaque ligne avec son heure, celles envoyées marquées `>` et la sortie d'erreur `!`. Des boutons envoient les
  commandes habituelles (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`… ; celles de WinBoard
  pour un moteur WinBoard, à qui l'on parle dans son propre protocole sauf si *Par l'adaptateur* est coché) et la
  position de la page Échiquier. *Copier* et *Enregistrer…* gardent le journal. La console s'affiche à la place des
  réglages du moteur (*Réglages* y revient, la console restant ouverte) ; *Fermer* arrête le moteur. Pendant qu'un
  tournoi tourne, ce que le moteur calcule prend des cœurs à ses parties : la console le dit. Son moteur ne fait pas
  partie des processus du tournoi : la fin d'un tournoi ne l'arrête pas.
- **Test rapide** joue un court match contre le moteur lui-même ou un autre : 10 parties en 10+0.1 depuis 5 positions
  (dont une avec une prise en passant), par le même code qu'un tournoi, affiché dans son espace de travail pendant
  qu'il joue. Le résultat reste à côté du moteur : son score, ses pertes au temps, plantages et coups illégaux, et la
  part de sa pendule qu'il a utilisée, qui signale un moteur qui joue bien trop vite ou trop près de la chute du
  drapeau. Ses PV de mat sont vérifiées (voir *Vérifier les PV de mat* dans [PGN et divers](#pgn-et-divers)). Seul le
  dernier test rapide est gardé, dans `tests/` dans le profil (avec les journaux moteur de ses parties qui ont mal
  fini).

### Une grande bibliothèque
<!-- en: a-large-library beb231ed -->

- **Groupes** : des étiquettes libres séparées par des virgules (`pool A`, `list 40/15`…). La liste peut être filtrée
  par groupe ou par une recherche, et un groupe peut être ajouté à un tournoi en un clic. *Notes* : tout ce
  qu'il faut retenir sur le moteur. *Logo* : une image montrée à côté de son nom dans *Direct*, sur le grand écran et
  sur la page web de la diffusion (voir [Apparence](#apparence)).
- **Recherche et filtres** : chaque recherche de moteur (la bibliothèque, et les listes de moteurs des autres pages)
  trouve un nom, le nom que le moteur annonce, son auteur, le chemin de son programme ou ses notes ; le filtre d'état
  montre les moteurs qui fonctionnent, en échec ou modifiés, jamais vérifiés, qui ont perdu leur programme, qui
  parlent WinBoard, ou inactifs. Un clic avec Ctrl ou Maj sélectionne plusieurs moteurs : *Vérifier* (l'un après
  l'autre, jamais pendant qu'un tournoi joue), *Ajouter à un groupe…*, *Retirer…*. *Plus…* vérifie ceux qui ne l'ont
  jamais été (*Vérifier ceux qui ne l'ont pas été*) et retire les programmes manquants. Une option texte qui contient
  un fichier ou un dossier (`EvalFile`, `SyzygyPath`…) a un bouton *…*.
- **Inactif** (sur la fiche, ou par le clic droit pour tous les moteurs sélectionnés) : le moteur reste dans la
  bibliothèque, grisé, mais est absent des sélecteurs de moteurs (configuration, suites, test rapide, relance, ajout
  d'un moteur à un tournoi en cours) et de *Vérifier ceux qui ne l'ont pas été*. Un tournoi ou une configuration qui
  l'a déjà le garde.
- **Utilise un GPU** (sur la fiche) : un moteur à réseau qui joue sur une carte graphique, comme Lc0. Pas plus de tels
  moteurs ne jouent à la fois que les *GPU* des **Réglages**, dans un tournoi et entre tournois (voir
  [Plusieurs tournois à la fois](#plusieurs-tournois-à-la-fois)) ; à partir du prochain tournoi qu'il rejoint.
- **Elo de référence** : le classement du moteur sur une liste à laquelle tu te fies. Un tournoi ou la liste de
  classement peuvent être ancrés dessus, et les parties d'un tournoi le portent dans leurs balises `WhiteElo` et
  `BlackElo` (copié quand le moteur rejoint le tournoi). *Importer…* > *Une liste de classement, pour l'Elo de
  référence…* le règle pour beaucoup de moteurs à la fois (voir
  [Importer des moteurs et des classements](#importer-des-moteurs-et-des-classements)).

### Démarrage des moteurs
<!-- en: starting-engines 09d107ba -->

- **La lecture des options** ne bloque jamais un lancement. *Reprendre* démarre aussitôt : un tournoi fait jouer les
  moteurs copiés dans son dossier. *Lancer*, la file d'attente et le test rapide n'attendent que la lecture d'un
  moteur qu'ils font jouer, lue en premier ; toute autre lecture ou vérification est interrompue et refaite plus tard.
  Un lancement qui attend le dit sur son bouton (*En attente de …*, avec *Annuler*), et un autre lancement est refusé
  entre-temps. Un lancement attend aussi une suite de tests en cours, et une suite de tests ne démarre pas pendant que
  le test rapide tourne ou qu'un lancement attend. Les lectures, les vérifications et les suites de tests tournent à
  côté des tournois, qui jouent dans leurs propres processus. Un moteur en attente de lecture peut être lu ensuite
  depuis son menu.
- *Relancer le moteur à chaque partie* démarre un nouveau processus pour chaque partie au lieu de le réutiliser.
  Sinon, un tournoi garde un processus en attente par moteur qui a encore des parties à jouer, jusqu'à 32 (plus deux
  par partie en cours), et ferme un moteur après sa dernière partie : dans un KO dès que son match est perdu, dans un
  suisse dès qu'il n'a plus de partie dans la dernière ronde.
- *Délai de démarrage (s)* (60 par défaut) est le temps laissé à un moteur pour répondre `uciok` à son lancement et
  `isready` au début de chaque partie ; un moteur lent à démarrer (un gros réseau à charger) a besoin de plus. Un
  moteur qui ne démarre pas est réessayé trois fois, à 10 s d'intervalle, avec le même délai ; une pause retient les
  essais, sans les consommer. Ensuite, ses parties sont mises de côté et les autres continuent (voir
  [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas)). Un tournoi créé ensuite copie le délai dans ses
  participants.

### Programmes recompilés
<!-- en: rebuilt-programs 549daa6e -->

- **Programmes déplacés ou recompilés** : la bibliothèque se souvient de chaque programme (taille, date, CRC32). Un
  programme qui n'est plus là est marqué **!** avec *Localiser…*, et un tournoi ne démarre pas avec lui. Un programme
  qui a changé (une nouvelle compilation, ou un autre fichier sous le même chemin) voit ses options relues
  automatiquement.
-  **Ce qu'une nouvelle compilation a changé** : quand la nouvelle lecture diffère de la précédente, le moteur est
  marqué **Δ** et sa fiche liste les différences jusqu'à *Fermer* : son nouveau `id name` (avec *Le renommer* quand le
  moteur portait l'ancien nom) et son auteur, chaque option dont le défaut, les bornes ou les choix ont changé (avant et
  maintenant), les options nouvelles et disparues, et ta valeur pour chacune (la valeur d'une option disparue est
  abandonnée ; celle que la nouvelle compilation n'accepte plus est en orange). Pour une option dont le défaut a changé,
  ou dont la valeur ne convient plus, choisis *Garder ma valeur* (ou *Garder l'ancien défaut* là où tu n'en avais pas :
  il joue comme avant) ou *Prendre le nouveau défaut* ; *Garder mes valeurs* et *Prendre les nouveaux défauts*
  choisissent pour toutes. L'avis disparaît une fois chaque choix fait, à moins qu'il n'ait autre chose à signaler.
  Plusieurs compilations avant qu'il soit fermé font un seul avis, par rapport à la lecture d'avant la première. *En
  échec ou modifié*, dans le filtre d'état, montre aussi ces moteurs, et la palette de commandes a *Ce qui a changé*,
  *Garder mes valeurs*, *Prendre les nouveaux défauts* et *Fermer l'avis des changements* pour chacun.
- **Les programmes des tournois** : chaque tournoi note les programmes avec lesquels il a été lancé. Le résumé de la
  configuration avertit d'un moteur dont le programme a changé depuis son dernier tournoi (le plus récent du dossier
  des tournois qui a fait jouer ce programme), avec *Ce qui a changé* quand sa fiche a un avis. Reprendre un tournoi
  dont un programme a été recompilé ou a disparu depuis le dit dans son en-tête, avec *Continuer* (l'avis se ferme)
  et *Pause* ; le tournoi ne s'arrête pas de lui-même.

### Importer des moteurs et des classements
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importer…** > *Une liste de moteurs…* lit la liste de moteurs d'un autre programme : ChessGUI (`ChsGUI_Eng.TXT`,
avec les options de `ChsGUI_Cfg.ini` à côté), cutechess et Banksia (`engines.json`), Arena (`ArenaENG.cfg`, dans le
dossier d'Arena : le programme, les arguments, le protocole de chaque moteur et, pour les moteurs UCI, les valeurs
d'options réglées dans Arena) et d'autres listes de style INI. Une liste qui ne donne aucun moteur le dit, avec les
formats lus et *Tous les moteurs d'un dossier…* à essayer à la place.

*Tous les moteurs d'un dossier…* prend tous les programmes trouvés dans un dossier, et se souvient du dossier :
*Actualiser : les nouveaux moteurs du dossier* ne propose ensuite que les moteurs qui y ont été ajoutés depuis. Les
moteurs trouvés sont d'abord listés, pour décocher ceux à laisser de côté ; les moteurs déjà dans la bibliothèque
(même programme et mêmes arguments) sont sautés ; les options des autres sont lues ; les moteurs WinBoard sont
importés aussi.

*Importer…* > *Une liste de classement, pour l'Elo de référence…* lit une liste de classement, collée ou ouverte : la
version texte d'une liste de classement ou sa page web (enregistrée ou copiée), la sortie d'Ordo (texte ou CSV), ou un
CSV de lignes `name,elo`. Un nom de la liste correspond au moteur de la bibliothèque qui porte le même nom, ou au seul
dont le nom est le même aux majuscules et aux espaces près (comme pour les joueurs des fichiers PGN importés, voir
[Classements](#classements)). L'aperçu montre l'Elo de référence de chaque moteur, ancien et nouveau, coché pour être
appliqué, et les noms de la liste qui ne sont aucun moteur de la bibliothèque. Les noms d'une liste portent souvent
leur compilation (`64-bit 4CPU`) : renomme les moteurs pour qu'ils correspondent si besoin.

### Moteurs WinBoard
<!-- en: winboard-engines 079e4849 -->

**Les moteurs WinBoard** (protocole xboard, CECP) jouent comme les autres. Le *Protocole* d'un moteur est déterminé
quand il est ajouté (*Automatique*), ou choisi : UCI ou WinBoard. fastchess ne parle que l'UCI : un moteur WinBoard
est lancé à travers une traduction intégrée à fastchess-gui (`fastchess-gui --wb2uci ENGINE [ARGS]`, que fastchess
lance à la place du moteur), et ses options, `memory`, `cores` et le chemin Syzygy deviennent des options UCI. Elle
pilote le moteur comme le fait WinBoard : fonctionnalités du protocole 2, ou protocole 1 pour les moteurs qui n'en
envoient aucune ; positions par `setboard`, ou par le mode édition ; coups en coordonnées ou en SAN ; `level`,
`time`/`otim`, `st`, `sd`, `nps` ; sortie de réflexion, abandon. Les moteurs marqués ↔ dans la liste sont des moteurs
WinBoard.

- Un moteur du protocole 1 sans `setboard` ni mode édition (TSCP, par exemple) ne peut partir que de la position
  initiale : donne-lui des ouvertures sous forme de coups (un livre PGN), pas de positions (EPD).
- Chess960 pour un moteur qui a la variante `fischerandom` : `variant fischerandom`, positions en X-FEN, roques en
  `O-O`/`O-O-O`, comme WinBoard les envoie.
- *Scores from White's side*, parmi les options du moteur : pour les vieux moteurs qui donnent leurs scores du point
  de vue des Blancs plutôt que de celui du camp au trait, comme le protocole le demande ; leurs scores avec les Noirs
  sont inversés.
- Un moteur qui demande à être relancé à chaque partie (`feature reuse=0`) a *Relancer le moteur à chaque partie*
  coché quand il est ajouté.
- `FCG_WB2UCI_LOG=FILE` dans l'environnement écrit dans `FILE` chaque ligne échangée avec les moteurs WinBoard.

## Livres
<!-- en: books 4cf13005 -->

**Ajouter…** enregistre un livre d'ouvertures (EPD, PGN ou Polyglot `.bin`) sous un nom, avec une description ; pour
un livre PGN, le nombre de demi-coups gardés, pour un livre Polyglot, la profondeur jusqu'à laquelle ses lignes sont
suivies. La section *Ouvertures* de la configuration propose ensuite les livres de la bibliothèque par leur nom, avec
leur nombre d'ouvertures (tout autre fichier peut encore être choisi). *Retirer* sort un livre de la bibliothèque et
laisse le fichier tranquille.

### Vérifier un livre
<!-- en: checking-a-book 96eee229 -->

Le livre choisi est lu en arrière-plan, avec les lecteurs de fastchess lui-même (ses ouvertures sont donc exactement
celles qu'un tournoi joue), et vérifié :

- **positions illégales** : pas exactement un roi de chaque couleur, des pions sur la première ou la dernière rangée,
  le camp qui n'a pas le trait en échec ;
- **doublons** : la même position qu'une ouverture précédente, y compris par transposition ;
- **couleurs inversées** : une ouverture précédente avec les couleurs échangées (un livre fait pour être joué deux
  fois, couleurs inversées, n'a pas besoin des deux) ;
- **parties déjà finies** : aucun coup légal, ou pas assez de matériel pour mater.

fastchess écarte d'un tournoi les ouvertures dont la position ne peut pas être jouée (une ligne FEN ou EPD sans roi
ou mal formée d'une autre façon, un roi qui pourrait être pris, une partie PGN dont la balise FEN est invalide), avec
un avertissement par livre ; elles sont comptées ici comme des positions illégales. Une partie PGN dont les coups ne
se lisent plus à partir d'un point (un coup mal formé ou ambigu) garde les coups d'avant. Les lignes d'un livre PGN
qui commencent par `%` (lignes d'échappement, dont la norme PGN fait des commentaires ; les livres AVT en ont) sont
ignorées, par les tournois aussi, et la page Livres les compte (*lignes commençant par %, ignorées*).

Ses ouvertures sont listées en dessous, toutes ou seulement celles qui ont un problème, avec une recherche par
morceau de FEN ou par coups (un livre PGN montre ses coups en SAN) ; un clic montre la position sur un échiquier.
*L'utiliser pour le prochain tournoi* le met dans la configuration (avec Chess960 pour un livre Chess960).
*Enregistrer une copie nettoyée…* écrit un livre EPD sans les positions illégales, les doublons et les parties finies
(et, si c'est coché, les positions aux couleurs inversées), et l'ajoute à la bibliothèque. *Lancer comme suite de
tests…* ouvre un livre EPD dans **Suites**.

### Créer des livres
<!-- en: creating-books b9c072ab -->

**Créer…** écrit des livres pour la bibliothèque (aussi dans la palette de commandes) :

- *Générer un livre Chess324* : le Chess324 de Larry Kaufman. Les rois et les tours restent sur leurs cases
  habituelles, le roque est donc le roque habituel ; la dame, les fous et les cavaliers de chaque camp sont mélangés
  sur les autres cases de sa première rangée (les colonnes b, c, d, f et g), les fous sur des cases de couleurs
  opposées. Cela fait 18 dispositions par camp, celle des Blancs et celle des Noirs choisies indépendamment : *Les 324
  positions…*, ou *Seulement les 18 symétriques…*, où les pièces noires sont sur les mêmes colonnes que les blanches.
  Ce sont des positions ordinaires, écrites dans un fichier EPD (proposé à côté des livres de la bibliothèque, ou dans
  `books/` dans le profil) et jouées par n'importe quel moteur, sans Chess960.
- *Construire un livre Polyglot…* : un livre Polyglot (`.bin`) à partir des parties d'un fichier PGN, comme le fait
  `make-book` de Polyglot, avec la conversion qui transforme les livres PGN des participants en livres Polyglot. Son
  formulaire, à la place des détails du livre, prend le fichier PGN (le livre PGN choisi dans la liste, s'il y en a
  un), la **profondeur** (les demi-coups de chaque partie qui entrent dans le livre), le **nombre minimum de parties**
  qu'un coup doit avoir dans sa position pour être gardé (le `-min-game` de Polyglot ; les autres sont écartés comme
  trop rarement joués), les **poids**, *au score* (les points du camp qui a joué le coup : une victoire 2, une nulle 1,
  une défaite 0, comme les compte Polyglot ; les parties sans résultat sont écartées, de même qu'un coup dont toutes
  les parties ont été perdues) ou *au nombre* (le nombre de parties qui le jouent), et, si c'est coché, un
  **classement** : seuls comptent les coups d'un joueur classé au moins à ce niveau, d'après la balise `WhiteElo` ou
  `BlackElo` de la partie (un joueur qui n'en a pas est écarté). *Construire…* demande où écrire le livre (à côté du
  fichier PGN par défaut), puis lit les parties en arrière-plan, avec sa progression et *Annuler*, à côté du
  formulaire et au bas de la liste ; le livre, trié par clé comme le format le veut, est ensuite ajouté à la
  bibliothèque, ses options dans sa description. La console dit combien de parties y sont entrées, combien les options
  en ont écartées et combien n'ont pas pu être lues.

## Échiquier
<!-- en: board 0bbd00c9 -->

La page Échiquier montre une partie ou une position, venue de n'importe où : **A** sur n'importe quel échiquier du
programme (une partie de la mosaïque de l'onglet *Direct* ou celle suivie, l'échiquier de l'onglet *Parties*, de
l'onglet *Ouvertures*, d'une ouverture d'un livre) ouvre sa position ici et l'analyse ; sur la page Échiquier, A lance
et arrête l'analyse. *Revenir au tournoi* (ou *Retour*) ramène à la page d'où l'on vient. Son échiquier peut être
dessiné en 3D : voir [L'échiquier 3D](#léchiquier-3d).

L'échiquier est à gauche, avec les joueurs au-dessus et en dessous ; à droite, de haut en bas, l'analyse, les coups
(avec, en onglets, le tableau des coups, le bilan de la partie et les parties du fichier), et le graphe de la partie.
Le sous-titre de la page dit ce qu'est la partie : son événement et sa ronde, son résultat, sa cadence, son code ECO
et son ouverture, et comment elle s'est terminée.

Un coup se joue sur l'échiquier à la souris : un clic sur la pièce puis un sur sa case, ou la pièce glissée jusque-là.
Pendant qu'elle est glissée, elle suit le pointeur et la case où elle arriverait est teintée ; lâchée ailleurs, elle
revient, toujours prise, pour être jouée par un clic. Sur l'échiquier 3D, un fantôme translucide de la pièce suit le
pointeur, flottant au-dessus de l'échiquier et passant derrière les pièces devant lui, l'image restant telle quelle ; la
pièce elle-même bouge quand elle est lâchée.

### Ouvrir, coller et mettre en place une position
<!-- en: opening-pasting-and-setting-up-a-position 3778ddb5 -->

- **Ouvrir un PGN…** lit n'importe quel fichier PGN (un fichier peut aussi être déposé sur la fenêtre), en
  arrière-plan avec une barre de progression : un fichier de millions de parties prend quelques secondes, et ses
  parties sont listées dans un onglet à côté des coups, filtrées par ce qui est tapé (chaque mot trouvé dans les
  joueurs, l'événement, le lieu, la date, la ronde, l'ouverture, le code ECO ou le résultat). Un clic montre une
  partie. Les évaluations de ses coups dessinent le graphe : les commentaires de cutechess et de fastchess
  (`{+0.35/18 1.2s}`) et le `[%eval 0.35]` de lichess et de ChessBase (du point de vue des Blancs, `#-3` pour un mat)
  sont lus, les pendules de `[%clk 0:01:23]` aussi. Ses variantes sont gardées (voir [Variantes](#variantes)). Une
  partie dont la balise FEN ne peut pas être jouée est refusée, et les coups d'une partie s'arrêtent au premier qui ne
  peut pas être lu.
- **Coller** (Ctrl+V sur la page) lit le presse-papiers : un FEN, ou les quatre champs d'une ligne EPD ; un PGN d'une
  partie ou plus (listées comme celles d'un fichier) ; une liste de coups depuis la position initiale, en SAN avec ou
  sans numéros (`1. e4 e5 2. Nf3`) ou en UCI (`e2e4 e7e5`). Une position qui ne peut pas être jouée est refusée avec
  la raison (un roi manquant, un pion sur la première rangée, le camp qui n'a pas le trait en échec). Des droits de
  roque dont le roi ou la tour n'est pas sur sa case habituelle, ou écrits sous forme de colonnes (`HAha`), en font
  une position Chess960. Un fichier `.fen` ou `.epd` peut aussi être déposé.
- **Éditer la position** met en place une position sur l'échiquier : un clic pose la pièce choisie (un second clic
  l'enlève), un clic droit l'enlève ; le trait, le numéro du coup, les droits de roque (proposés seulement quand le
  roi et la tour sont là où ils doivent être), la case de prise en passant (seulement derrière un pion qui vient
  d'avancer de deux cases) et Chess960. Le FEN en dessous suit l'échiquier, et un FEN tapé là le règle. *Terminé* est
  proposé dès que la position peut être jouée, et la met sur l'échiquier.
- *Copier le PGN* (à droite des onglets des coups) copie la partie affichée, avec ses balises, ses coups (les scores
  des moteurs en commentaires quand elle en a) et ses variantes.
- **L'échiquier** : les boutons en dessous et ← → Origine Fin parcourent les coups, et F (ou le dernier bouton) le
  retourne ; un clic sur une pièce puis sur une case joue un coup (un pion qui est promu demande sa pièce). *Copier le
  FEN* copie la position.

### Les joueurs
<!-- en: the-players 1f4f551f -->

Au-dessus et en dessous de l'échiquier, quand la partie a des joueurs : le nom de chacun, l'évaluation de son dernier
coup par son moteur (le score des Blancs et la profondeur, en pastille), le coup qu'il a joué et le temps qu'il y a
mis, ou *au trait*, et à droite sa pendule après son dernier coup, encadrée tant qu'il a le trait. Les pendules sont
celles des `[%clk]` des coups quand la partie en a ; sinon elles sont reconstituées à partir de sa balise
`TimeControl` (`300+2`, `10+0.1` : secondes et incrément) et du temps de chaque coup, les coups du livre n'en prenant
aucun.

### Analyse
<!-- en: the-analysis 1e6120de -->

Un moteur de la bibliothèque analyse la position affichée et suit l'échiquier. La carte montre son moteur, puis un
bouton avec ses *Threads*, son *Hash* (0 : celui du moteur) et ses *Lignes* (MultiPV), qu'un clic ouvre : les
réglages propres à l'analyse, gardés pour la prochaine fois, qui ne changent ni le moteur de la bibliothèque ni aucun
tournoi (les autres valeurs d'options de la bibliothèque sont envoyées comme une partie les envoie). À droite, les
threads qu'utilise la page Échiquier sur ceux de la machine, dans la couleur d'avertissement pendant qu'un tournoi
tourne (son infobulle dit combien de threads prennent les parties du tournoi : l'analyse est en concurrence avec
elles).

La recherche s'affiche en tuiles (profondeur et seldepth, nœuds, vitesse, temps, accès aux tables, remplissage du
hash), les chances de gain, de nulle et de perte quand le moteur les donne (`UCI_ShowWDL`), et chaque ligne : son
score (celui des Blancs, le meilleur mis à part), sa profondeur et ses coups sur deux lignes de texte, en figurines
(voir [Apparence](#apparence)). Survoler un coup d'une ligne montre la position après lui sur l'échiquier, encadré ;
un clic joue la ligne jusqu'à ce coup. Le premier coup de chaque ligne est dessiné en flèche, la meilleure plus foncée
(**Réglages** > *Apparence* > *Les flèches de l'analyse sur la page Échiquier* les retire ; les lignes montrent
toujours les coups).

**Menace** (X) fait analyser la position, le trait passé, par un autre processus du moteur : le meilleur coup de
l'adversaire, s'il jouait de nouveau, dessiné en flèche rouge et écrit sous les lignes (aucun quand le camp au trait
est en échec).

L'analyse attend pendant qu'une autre page est affichée, et reprend quand la page Échiquier revient ; *Arrêter
l'analyse* (ou A) l'arrête, et un point sur le bouton *Échiquier* de la barre de navigation indique qu'elle tourne.
Son moteur ne fait pas partie des processus du tournoi : la fin d'un tournoi ne l'arrête pas. Une fois l'analyse
arrêtée, son moteur reste démarré pour la recherche suivante, avec sa table de hachage (et la mémoire GPU d'un moteur à
réseau) : le bouton d'alimentation à côté du moteur, *Décharger le moteur* (aussi dans la palette de commandes),
l'arrête, ainsi que celui de la menace.

### Analyser une partie entière
<!-- en: analysing-a-whole-game a7e6166c -->

**Analyser la partie** (G, dans les outils de la page ou dans l'en-tête du graphe) fait analyser chaque position de
la partie par le moteur d'analyse (ses threads et son hash, une ligne), en arrière-plan, d'abord 20 ms chacune, puis
de nouveau quatre fois plus longtemps à chaque passe (80 ms, 0,3 s, 1,3 s, 5 s…), jusqu'au temps choisi avec la
flèche à côté du bouton (0,3 s, 1,3 s, 5 s, 20 s par position, ou *jusqu'à l'arrêt*) : le graphe vient en une seconde
ou deux et s'affine à chaque passe. Les positions vont de la dernière à la première, ce qui laisse le hash du moteur
garder ce qui vient après une position (mesuré : environ 16 % plus près des longues recherches que de la première à
la dernière, pour le même temps), la position de l'échiquier en premier. L'en-tête du graphe indique la passe, le
temps par position et la progression, et un mince liseré sous sa bande des temps ce que chaque position a reçu ;
*Arrêter* (ou G) l'arrête.

Chaque coup est jugé sur les chances de gain que son auteur a perdues, d'après les scores de l'analyse avant et après
lui (la mesure de lichess : 50 % à 0.00, environ 59 % à +1.00) : 10 points perdus font une imprécision (`?!`), 20 une
erreur (`?`), 30 une gaffe (`??`), marquées après le coup, sur le graphe et sur l'échiquier ; le coup que le moteur
aurait joué n'est jamais marqué. Un coup est jugé sur la dernière passe complète ; une marque que la dernière passe a
changée reste creuse jusqu'à ce que la suivante la confirme. N et Maj+N (ou les boutons à côté de la navigation) vont
au coup marqué suivant et au précédent.

Chaque passe complète est gardée avec la partie (dans `analyses/` dans le profil) : la même partie, rouverte depuis
n'importe où, la montre aussitôt, et G reprend à partir des passes terminées. L'analyse tourne sur son propre thread
avec son propre processus de moteur, ses threads comptés dans le budget de cœurs ; l'analyse de l'échiquier (A) la met
en pause, et quand A analyse une position de la partie plus longtemps que la passe ne l'a fait, sa valeur prend la
place de celle de cette position.

**Plusieurs analyses** : *+ Moteur*, dans la légende du graphe, fait analyser la partie par un autre moteur de la
bibliothèque en plus, aussi longtemps à chaque passe. Chaque analyse a sa couleur sur le graphe et sur l'échiquier
(son meilleur coup en flèche quand l'analyse de l'échiquier est arrêtée), sa ligne dans *Ce coup* et ses précisions
dans le bilan. Elles avancent ensemble passe par passe, toutes à la fois quand les cœurs le permettent, l'une après
l'autre sinon. Une analyse juge les coups (la première ; un clic droit sur une autre dans la légende en fait le juge,
ou la retire) ; une marque est creuse quand les analyses ne jugent pas toutes le coup de la même façon. Les chances de
gain comparent mieux les moteurs que les centipions, qui diffèrent d'un moteur à l'autre : elles prennent le WDL de
chaque moteur quand il en donne un.

### Le graphe de la partie
<!-- en: the-game-graph 14e02bad -->

Sous les coups, le graphe de la partie, le même que dans *Direct* et *Parties* :

- **Évaluation** : l'analyse qui juge, en aire blanche et noire (l'avantage des Blancs au-dessus de la ligne, celui
  des Noirs en dessous), chaque moteur qui a joué en courbe de sa couleur à ses propres coups, les autres analyses en
  courbes pointillées ; les coups marqués sur la courbe ; les valeurs au-delà de ±6 dessinées le long du bord dans la
  couleur d'avertissement. Sans analyse, l'aire est la moyenne des évaluations des deux moteurs.
- Les coups du livre hachurés, les phases sous l'axe (l'ouverture ; le milieu de partie à partir de la première
  position avec au plus 10 pièces en dehors des rois et des pions, ou moins de 4 sur une rangée du fond ; la finale à
  partir de 6 au plus, comme lichess découpe une partie), la zone que connaissent les tables teintée, les numéros des
  coups en bas.
- Sous l'axe, le temps de chaque coup : celui des Blancs vers le haut, celui des Noirs vers le bas.

Ses onglets : *Chances de gain* (d'après le WDL quand les moteurs le donnent, la formule de lichess sinon), *G/N/P*
(les parts que les moteurs ont données, les victoires des Blancs en bas), *Temps* (avec le temps de décision),
*Profondeur* (avec la seldepth), *Nœuds*, *Vitesse*, *Accès aux tables*, *Accord* (l'écart entre les évaluations des
deux joueurs, chaque coup contre le précédent, et celui des analyses à chaque position) et *Accord des PV* (combien de
demi-coups de la PV d'un moteur reprennent ce que celle de l'autre avait prévu) ; ceux pour lesquels la partie n'a pas
de valeurs sont omis, et ceux qui ne tiennent pas vont dans le menu `⋯`. Le survol indique le coup et ce que chaque
moteur a dit ; un clic ou un glissement va au coup. Un clic sur une courbe dans la légende la masque ou l'affiche ; la
flèche à droite replie le graphe.

### Ce coup
<!-- en: this-move a4a908f8 -->

Sous les coups, à propos du coup affiché : ce qu'a dit le moteur qui l'a joué (son score, sa profondeur et sa
seldepth, son temps, ses nœuds, sa vitesse, ses accès aux tables, tels que le PGN les a), la ligne qu'il attendait
après lui, le verdict de chaque analyse (la part des chances de gain perdue, le jugement, le meilleur coup et son
score), et avec les tables de finales le résultat après le coup et si le coup a gardé celui d'avant. *Afficher* pose
une ligne sur l'échiquier sans toucher à la partie : la position après elle, l'échiquier encadré, et par-dessus ce
qu'est cette ligne ; Échap, un clic sur l'échiquier ou un déplacement dans la partie ramène la partie. Sur
l'échiquier, le jugement du coup est un badge sur sa case, et la réponse que son moteur attendait une flèche en
pointillés.

### Variantes
<!-- en: variations 7932794c -->

Un coup joué sur l'échiquier là où la partie a pris un autre chemin commence une variante : la partie reste telle
qu'elle était, avec ses analyses et son graphe. Les coups montrent les variantes entre parenthèses après le coup
qu'elles remplacent, imbriquées et numérotées ; un clic sur un coup y va, et *Revenir à la partie* ramène à la
position que la variante quitte. Un clic droit sur un coup d'une variante la promeut (elle devient la ligne dont elle
part, dont les coups après elle deviennent la variante), la supprime avec celles qui en partent, ou la copie.
*Copier le PGN* écrit les variantes, et celles d'un fichier PGN ouvert sont lues (commentaires, numéros, annotations
et résultats ignorés ; un coup qui ne peut pas être joué ne termine que sa variante). Le graphe, les analyses et les
jugements sont ceux de la partie ; sur une variante, le graphe marque où elle quitte la partie.

### Les coups, le tableau et le bilan
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Coups** écrit la partie en texte continu : figurines (voir [Apparence](#apparence)), les coups du livre grisés et
la fin du livre marquée, les jugements en couleur, le coup en cours encadré ; survoler un coup indique ce qu'a dit son
moteur et son jugement. **Tableau** liste chaque coup avec le score de son moteur (celui des Blancs), sa profondeur et
son temps, une barre pour le temps, les coups marqués en couleur.

**Bilan**, une fois la partie analysée : pour chaque camp sa précision (celle de lichess : celle de chaque coup
d'après les chances de gain qu'il a perdues, les coups des moments tendus pesant plus), ses imprécisions, erreurs et
gaffes, sa perte moyenne en centipions, la part de ses coups que l'analyse jouerait aussi, la profondeur et le temps
moyens de son moteur ; sa précision dans l'ouverture, le milieu de partie et la finale ; avec plusieurs analyses, les
précisions que donne chacune. Puis les moments clés, chacun à un clic : le tournant (le coup qui a perdu le plus, avec
le meilleur coup), les autres erreurs et gaffes, les moments où les deux moteurs étaient en désaccord de 1,5 pion ou
plus pendant deux coups de suite, le début de la finale et l'entrée dans les tables. *Qui l'a vu en premier* indique,
pour le tournant, à quel coup chaque moteur qui a joué et chaque analyse a donné pour la première fois un pion ou plus
au camp qui gagne. *Copier le PGN avec l'analyse* écrit la partie avec l'évaluation de chaque position (`[%eval]`),
les jugements en NAG et, après un coup marqué, la ligne que l'analyse préférait ; *Copier en BBCode* donne le bilan
pour un forum.

### Les tables de finales sur la page Échiquier
<!-- en: tablebases-on-the-board-page 721b71dd -->

Avec les dossiers Syzygy de la configuration (*Adjudication* > tables de finales), la page Échiquier interroge les
tables pour chaque position de la partie : le graphe teinte la zone que connaissent les tables, *Ce coup* indique le
résultat après le coup et si le coup a gardé celui d'avant ou l'a gâché, et le bilan marque où les tables prennent le
relais. Chaque position est interrogée comme si une prise ou un coup de pion venait d'être joué, ce qui est exact sauf
là où la règle des 50 coups est proche. Les tables sont celles du processus du programme : elles ne sont pas
interrogées pendant qu'un tournoi y joue.

### Jouer une partie depuis une position
<!-- en: playing-a-game-from-a-position 7e83c318 -->

**Jouer une partie…** joue une partie depuis la position de l'échiquier entre deux moteurs de la bibliothèque, ou entre
toi et l'un d'eux (voir *Humain* plus bas) (*Blancs* et *Noirs*, `<->` les échange avec leurs pendules), avec une
pendule pour chacun (des minutes et un incrément en secondes ; *La même pour les deux*, ou une chacun pour un handicap
de temps), et, si c'est coché, une seconde partie avec les couleurs inversées. Les moteurs sont lancés avec les valeurs
d'options de la bibliothèque, un moteur WinBoard par l'adaptateur. L'échiquier suit la partie au fil du jeu (revenir en
arrière arrête de la suivre jusqu'à ce que sa fin soit de nouveau atteinte, ou qu'on clique sur *Revenir au coup en
cours*), avec les pendules, la recherche du camp au trait et *Arrêter*, qui termine la partie sans résultat. Une partie
se termine par mat, pat, matériel insuffisant, la règle des 50 coups, une triple répétition, un coup illégal, un moteur
qui s'arrête, ou une perte au temps (la pendule accordant 0,1 s pour que le coup arrive). Chaque partie est ajoutée,
quand elle se termine, à un fichier PGN dans `games/` dans le profil (nommé d'après la date et les moteurs, avec une
balise `Termination`), que *Ouvrir les parties* liste sur l'échiquier.

Les parties sont jouées par le client de moteurs propre à fastchess-gui, à l'écart de tout tournoi : elles tournent
pendant qu'un tournoi joue, sans y toucher. Leurs threads (le plus grand nombre des deux moteurs, puisqu'ils jouent à
tour de rôle) comptent dans le budget de cœurs ; quand les parties des tournois en cours et l'analyse remplissent déjà
la machine, la carte le dit et propose *Jouer quand même* au lieu de *Jouer*. L'analyse s'arrête quand une partie
commence, car elle serait en concurrence avec ses moteurs.

**Humain**, en tête des listes *Blancs* et *Noirs*, c'est toi : tu joues ce camp toi-même ; tes coups se jouent sur le
plateau à la souris, sur le plateau à plat comme sur le plateau 3D (un pion qui atteint la dernière rangée demande sa
pièce). Le plateau se retourne pour mettre ton camp en bas à chaque partie (*Retourner* le tourne toujours), ne prend
tes pièces qu'à ton tour (la carte affiche *À toi de jouer*), et un clic pendant que tu regardes un coup précédent le
ramène à la partie. Ta pendule tourne comme celle d'un moteur, perte au temps comprise : donne-toi plus de temps qu'au
moteur en décochant *La même pour les deux*. Pendant que tu joues, ce que pense le moteur est gardé de côté (son score,
sa profondeur et sa ligne, donc aussi la barre d'évaluation et la courbe du graphe) ; la partie terminée les a, prête
pour *Analyser la partie*. *Abandonner la partie* la termine à tout moment, gagnée par le moteur ; avec une seconde
partie, celle-ci suit, ta couleur inversée. Tes parties sont enregistrées sous ton nom de session, et seul le moteur
compte dans le budget de cœurs. On ne peut pas reprendre un coup, et le moteur joue à pleine force sauf si sa fiche en
règle une plus faible (`UCI_LimitStrength` et `UCI_Elo`, ou `Skill Level`, s'il les a).

## Suites de tests
<!-- en: test-suites 42c2b2ba -->

La page **Suites** fait passer une suite de tests EPD : des positions avec leur meilleur coup (`bm`), les coups à
éviter (`am`), ou, pour la Strategic Test Suite, les points de plusieurs coups (`c0 "f5=10, Be5+=2"`).

- **Suite** : un fichier EPD, ou un livre EPD de la bibliothèque. Les lignes sans position légale ou sans aucune de
  ces opérations sont écartées (leur nombre est affiché).
- **Moteurs** : autant de moteurs de la bibliothèque que tu veux, avec leurs options ; *Hash* et *Threads* imposent
  une valeur à chacun (0 = celle de chaque moteur).
- **Temps**, **Nœuds** ou **Profondeur** par position. *Trouvé depuis (demi-coups)* : une recherche s'arrête dès
  qu'une solution est restée le premier coup du moteur pendant ce nombre de demi-coups de profondeur supplémentaires
  (0 = chaque recherche prend tout son temps).
- **Parallélisme automatique** : avec un temps, les cœurs physiques − 1 divisés par les threads des moteurs ; avec des
  nœuds ou une profondeur, tous les threads matériels.

Une position est résolue quand le coup final du moteur est une solution : l'un des `bm`, aucun des `am`, ou, avec des
points seulement, le coup qui en a le plus. Son temps de résolution est le moment où le moteur s'est fixé sur ce coup.
La grille montre les positions face aux moteurs, ✓ avec le temps de résolution ou ✗ avec le coup joué, et les points
STS ; les totaux viennent en premier : positions résolues, leur temps moyen, et le score STS. Un passage terminé est
gardé dans `suites/` dans le profil ; *Comparer avec un passage enregistré…* ajoute les colonnes d'un passage
précédent de la même suite, pour comparer des versions d'un moteur. *Exporter en CSV…* écrit la grille.

## Nouveau tournoi
<!-- en: new-tournament 6d169de4 -->

**Tournois → Nouveau tournoi** ouvre la configuration : une carte de lignes repliées qui disent comment les parties
se jouent, les participants avec la bibliothèque de moteurs à côté, et un résumé en direct à droite (nombre de
parties, parties par moteur, durée estimée, ouvertures, réglages communs, handicaps de temps). La configuration du
dernier tournoi est gardée : tester de nouveau deux versions d'un moteur revient donc à choisir A et B, puis
**Lancer**.

Tout, sur la page, peut se faire au clavier : Tab et les flèches passent d'une ligne, d'une liste ou d'un bouton à
l'autre, Entrée ou Espace ouvre une ligne ou une liste, et les touches des
[participants et de la bibliothèque](#participants-et-bibliothèque) y fonctionnent. **Lancer** crée le tournoi et le
joue ; **Mettre en file d'attente** le garde pour plus tard (voir
[File d'attente et relance](#file-dattente-et-relance)).

### Le résumé et ses vérifications
<!-- en: the-summary-and-its-checks 5609344a -->

Le résumé donne la mémoire que peuvent prendre les tables de hachage : deux processus de moteur par partie jouée en
même temps, et les moteurs qui attendent leur partie suivante, chacun avec son `Hash`. Au-delà de la mémoire libre, il
avertit (le système va utiliser le swap), en nommant le moteur qui a le plus grand `Hash` ; au-delà de la mémoire
installée, le tournoi ne démarre pas. `--run` dit la même chose. Quand le budget de mémoire des **Réglages** laisse
jouer moins de parties à la fois que le parallélisme, le résumé dit combien (chaque partie prend le `Hash` de ses deux
moteurs et ce que leurs processus prennent en plus), et la ligne *Parallélisme* aussi ; de même pour les parties avec
un moteur qui utilise un GPU, face aux *GPU*. Il nomme aussi les moteurs dont le programme a changé depuis leur dernier
tournoi (une nouvelle compilation), avec *Ce qui a changé* quand la fiche du moteur liste de nouveaux défauts ou de
nouvelles options (voir [Programmes recompilés](#programmes-recompilés)).

Ce qui n'irait pas est dit avec le moteur, la ressource et la correction, un bouton sous la ligne (la palette de
commandes liste les mêmes corrections) :

- un programme ou un dossier de travail introuvable : *Ouvrir le moteur* (sa fiche dans la page Moteurs),
  *Localiser…* ; un livre d'ouvertures introuvable : *Localiser…* ;
-  un moteur dont les `Threads` propres (son réglage, sinon son défaut : certains moteurs prennent tous les cœurs)
  dépassent la moitié des threads matériels, ou les threads dont dispose chaque partie avec les parties jouées à la fois
  (les cœurs physiques avec une pendule) : *Ramener Threads à N* règle l'option du moteur dans la bibliothèque, *Ouvrir
  le moteur*. Des `Threads` donnés par la configuration au-delà de ce qu'a la machine reçoivent *Ramener Threads* là où
  ils sont donnés (la case du participant, les réglages communs). La liste des participants marque un tel moteur d'un
  *!*, les moteurs du match sont signalés en dessous ;
- une valeur d'option qu'un moteur refuse (hors de sa plage, pas l'un de ses choix, une option qu'il n'a plus) :
  fastchess ne l'envoie pas, et le moteur joue avec son défaut ;
- les tables Syzygy qu'utilise le tournoi : les dossiers de l'adjudication et ce que désigne le `SyzygyPath` de chaque
  moteur (plusieurs dossiers séparés par `;` sous Windows, `:` ailleurs). Pour les pièces utilisées (les *Pièces max*
  de l'adjudication, sinon les plus grandes tables trouvées), chaque fichier WDL (`.rtbw`) doit être là, et aucun
  fichier tronqué (un fichier Syzygy fait 16 octets de plus qu'un multiple de 64 ; les moteurs laissent de côté un
  fichier d'une autre taille, et certains s'arrêtent quand ils le rencontrent) : *Syzygy 6 pièces : 14 fichiers
  manquants dans D:\TB (KRPPvKR.rtbw…)*, *Adjudication jusqu'à 6 pièces, mais seulement des fichiers de 5 pièces*,
  *SyzygyPath de Alpha désigne un dossier sans fichier de tables*, avec *Ouvrir le dossier* et *Descendre à 5
  pièces* ; l'infobulle liste les fichiers. Ce sont des avertissements : l'adjudication et les moteurs se passent des
  fichiers manquants. Seule une adjudication sans dossier, ou dont les dossiers ne contiennent aucune table, arrête le
  tournoi à son lancement (fastchess la refuse) : *Localiser…* choisit un autre dossier. Les dossiers sont lus en
  arrière-plan, et de nouveau toutes les quelques secondes tant que le résumé est affiché.

Un lancement refusé nomme les mêmes problèmes, avec leurs corrections et *Ouvrir le journal*. Chaque problème a un
**?** qui ouvre son entrée dans [Dépannage](#dépannage).

### Profils et modèles
<!-- en: profiles-and-templates df08e65d -->

**Profils.** Les cinq premières lignes, *Format*, *Cadence*, *Ouvertures*, *Adjudication* et *Réglages des moteurs*,
sont chacune un profil nommé : sa liste en choisit un, la ligne à côté dit ce qu'il joue (« 40 coups en 15 min,
répété », « nulle 40/8/10 cp · TB 6 pièces » ; son infobulle donne la même chose telle que cutechess et fastchess
l'écrivent, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), et ✎, ou un clic sur cette ligne, ouvre les
détails sur place, en dessous. Une modification faite pour ce tournoi seulement met un point à côté du profil, et sa
liste et ses détails proposent *Mettre à jour* (le profil prend la modification, pour chaque tournoi et chaque modèle
qui l'utilisent), *Revenir* et *Enregistrer comme profil…* (sous un nouveau nom). Le programme est livré avec des
préréglages :

- cadences : 10+0.1 et 60+0.6 (les tests courts et longs de fishtest), 2+1, 40/4, 40/15 et 40/20 (40 coups en 4, 15 ou
  20 minutes, répété, comme les jouent les listes de classement), et 200k nœuds par coup ;
- formats : SPRT gain [0, 3] et non-régression [-5, 0], un match de 1000 parties, un toutes rondes, un gauntlet, un
  suisse de 9 rondes, un KO et un match par équipes ;
- adjudications : aucune, une standard (nulle à partir du coup 40 après 8 coups à moins de 10 cp, défaite après
  3 coups à 600 cp) et la même avec les tables jusqu'à 6 pièces ;
- réglages des moteurs : ceux de chaque moteur, ou Hash 16, 64 ou 256 Mo avec 1 thread et les livres propres des
  moteurs désactivés ;
- ouvertures : pas de livre, ou les 960 positions du Chess960.

Un profil à toi enregistré sous le nom d'un préréglage prend sa place ; le supprimer (depuis sa liste, après
confirmation) fait revenir le préréglage, et les configurations et modèles qui l'utilisaient gardent ses valeurs. Les
profils sont gardés dans `profiles.json`, dans le dossier du profil, et la palette de commandes (Ctrl+K) les liste :
en choisir un l'applique à la configuration. La palette ouvre aussi chaque ligne de la configuration par son titre, et
charge ou enregistre des modèles. Les dossiers Syzygy ne sont dans aucun profil : ils appartiennent à cet ordinateur,
et l'adjudication et les moteurs les partagent. Les lignes *Parallélisme* et *PGN et divers* contiennent ce qui
n'appartient qu'à ce tournoi.

**Modèle** et **Nom**, à côté de *Lancer* : un modèle est la configuration entière, un profil pour chaque section plus
ce qu'aucun profil ne contient (le nom, les participants, le parallélisme, les fichiers PGN, la diffusion). Le menu en
charge un ou enregistre la configuration comme modèle (*Enregistrer le réglage comme modèle…*, qui demande avant de
remplacer un modèle du même nom), et la palette de commandes les charge aussi. Une section qui suivait son profil
quand le modèle a été enregistré suit ce profil, si bien que mettre à jour un profil met à jour chaque modèle qui
l'utilise ; une section modifiée pour ce tournoi garde ses propres valeurs. Les modèles enregistrés par des versions
précédentes se chargent tels qu'ils étaient, et leurs sections prennent les profils auxquels elles correspondent. Un
nom vide est formé à partir des moteurs.

### Format
<!-- en: format 29a36e7c -->

L'infobulle de chaque format dit ce qu'il est.

-  *Match* : deux moteurs, A (le moteur testé ; chaque statistique est donnée de son point de vue) et B (la référence).
  Un nombre fixe de parties, jusqu'à ce que tu l'arrêtes, ou un SPRT : elo0, elo1, alpha, beta, modèle (`normalized`
  utilise des bornes en nElo comme fishtest et OpenBench, `logistic` l'Elo classique, `bayesian` le BayesElo) et un
  nombre maximum de parties facultatif. Les profils SPRT gain et non-régression donnent les bornes habituelles, et la
  ligne sous les bornes dit combien de parties le test devrait durer. *Calculateur…* trace, comme le calculateur SPRT de
  fishtest, la probabilité que H1 soit acceptée et le nombre de parties attendu en fonction de la vraie différence
  d'Elo, à partir d'un taux de nulles attendu et du biais RMS des ouvertures ; *Utiliser ces bornes* recopie dans la
  configuration les bornes qui lui ont été données. *Biais Elo* (0 par défaut) est ajouté aux deux bornes, pour tester
  contre une version qu'on sait plus forte ou plus faible d'autant : [0, 3] avec un biais de 5 donne le SPRT [5, 8], que
  le résumé, la vue d'ensemble et le mode sans interface écrivent `SPRT [0, 3] + 5` ; le calculateur et la ligne sous
  les bornes en tiennent compte. *Parties min.* (0, aucune, par défaut) retient la décision tant que ce nombre de
  parties n'est pas joué, le LLR étant toujours calculé et affiché, pour protéger un test court d'un arrêt précoce dû à
  la chance : un LLR au-delà d'une borne avant ce moment est signalé sous la barre du LLR et le match continue ; une
  fois les parties jouées, la première paire qui se termine avec le LLR au-delà d'une borne termine le match. Un maximum
  de parties inférieur au minimum termine le match sans décision. C'est la façon la plus rapide de comparer deux
  versions d'un moteur, et il n'en faut pas plus.
  *Couleurs* (*Alternées* par défaut) dit qui a les Blancs : A et B à tour de rôle, ou *A toujours avec les Blancs* ou
  *B toujours avec les Blancs*, comme `-noswap` (et `-reverse`) de fastchess, pour les parties à handicap où le moteur
  le plus fort doit toujours avoir le camp handicapé, ou une ouverture essayée d'un seul côté. Les parties vont toujours
  par paires, toutes deux avec les mêmes couleurs, mais chaque partie a sa propre ouverture, la suivante du livre (comme
  `-games 1` de fastchess) : les paires et leurs statistiques restent, le score inclut l'avantage des Blancs, et le
  biais RMS, qu'inverser les couleurs annule, n'est pas affiché. Le résumé et l'en-tête du match l'indiquent.
- *Toutes rondes* : chaque moteur joue contre chacun des autres, *Cycles* fois, chaque fois deux parties avec les
  couleurs inversées (ce que cutechess appelle des rondes, de 2 parties par rencontre). Les rondes suivent les tables
  de Berger, si bien que chaque moteur joue une fois par ronde.
- *Gauntlet* : les premiers *Moteurs testés* (les têtes de série) jouent contre tous les autres, les autres ne jouent
  pas entre eux.
- *Suisse* : un nombre de *Rondes*. Chaque ronde est de deux parties, couleurs inversées, contre un moteur au score
  proche ; personne ne rencontre deux fois le même adversaire quand on peut l'éviter. Avec un nombre impair de
  moteurs, le moteur le moins bien classé qui n'a pas encore été exempt l'est, ce qui vaut un point.
- *KO* : les moteurs sont têtes de série dans l'ordre de leur liste, les meilleures têtes de série sont exemptes du
  premier tour quand le nombre de participants n'est pas une puissance de deux, et les deux premières têtes de série
  ne peuvent se rencontrer qu'en finale. Un match est de *Paires de parties par match* paires de parties ; un match à
  égalité reçoit une paire de plus à la fois, jusqu'à dix, après quoi la meilleure tête de série se qualifie.
- *Match par équipes* : deux équipes de moteurs, pour comparer une famille de moteurs, ou un ensemble de versions, à
  une autre dans son ensemble. *Appariements* : *Tous contre tous*, chaque moteur d'une équipe contre chaque moteur de
  l'autre équipe, un adversaire par ronde (un système Scheveningen ; les équipes peuvent être de tailles
  différentes), ou *Échiquier par échiquier*, le premier moteur de chaque équipe contre le premier de l'autre, le
  deuxième contre le deuxième et ainsi de suite (équipes de même taille). Chaque appariement est joué *Cycles* fois,
  chaque fois deux parties sur la même ouverture, couleurs inversées ; les couleurs de la première partie alternent
  d'un échiquier à l'autre et d'un cycle au suivant. *Équipe A* et *Équipe B* nomment les équipes (« Équipe A » et
  « Équipe B » sans nom) ; à côté, combien de moteurs chacune compte. Les équipes se font dans la liste des
  participants : la moitié haute de la liste est l'équipe A, la moitié basse l'équipe B (l'équipe A a le moteur en
  plus), jusqu'à ce que des moteurs soient mis dans une équipe.

### Participants et bibliothèque
<!-- en: participants-and-the-library a7ef2e55 -->

**Participants**, avec la **Bibliothèque** à côté (en dessous dans une fenêtre étroite). Chaque participant est une
ligne : sa place (une étoile pour les têtes de série d'un gauntlet), son nom, son Elo de référence, ce qu'il joue
autrement que les réglages communs (« Hash 512 Mo · 80 % du temps ») et ⋯, mis en évidence quand il a des réglages
propres. ⋯, un clic droit, ou la touche Menu les ouvre : son propre `Hash`, ses `Threads`, des nœuds ou une profondeur
fixes, un handicap de temps (un pourcentage de la cadence, pour une pendule), son propre *Livre*, *Ce moteur une fois
de plus, avec un autre livre*, les déplacements et *Retirer*. L'ordre de la liste est celui des têtes de série : fais
glisser les participants pour changer leur ordre (têtes de série en premier), ou appuie sur Ctrl+Haut ou Ctrl+Bas
pour les envoyer tout en haut ou tout en bas, Alt+Haut ou Alt+Bas pour les déplacer d'une place. Un clic avec Ctrl ou
Maj en sélectionne plusieurs ; Suppr (ou *Retirer*) les retire.

Dans un match par équipes, la place est l'équipe et l'échiquier, A1, A2… pour l'équipe A, B1, B2… pour l'équipe B,
dans l'ordre de la liste (échiquier par échiquier, A1 joue B1). Le menu ⋯ met le participant dans une équipe ;
*Équipe A* et *Équipe B* sous la liste mettent les participants sélectionnés dans l'une d'elles, et *Moitiés* refait
les équipes à partir des moitiés de la liste (la palette de commandes propose la même chose). Un moteur ajouté une
fois les équipes choisies rejoint la plus petite. Le résumé dit combien de moteurs compte chaque équipe, et un
lancement est refusé, avec la raison, quand une équipe est vide ou, échiquier par échiquier, quand les équipes sont de
tailles différentes.

La bibliothèque liste les moteurs actifs avec leur Elo de référence, triés par nom ou par Elo (clique sur la
colonne), un `!` sur ceux qui ont un problème (programme manquant ou recompilé, dernière vérification en échec :
l'infobulle dit lequel) et un ✓ sur ceux qui jouent déjà. Le filtre cherche dans les noms, les noms que les moteurs
annoncent, leurs auteurs, programmes, notes et groupes (Ctrl+F lui donne le clavier ; Entrée dans le filtre ajoute
les moteurs sélectionnés, ou le premier affiché) ; une puce par groupe ne montre que ce groupe, et *Ajouter tous ceux
affichés* ajoute le groupe entier. Double-clique sur un moteur, ou sélectionnes-en plusieurs avec Ctrl ou Maj
(Ctrl+A : tous ceux affichés) et appuie sur Entrée, pour les ajouter ; ou fais-les glisser à une place parmi les
participants. *Suggérer…* sélectionne les N moteurs actifs qui fonctionnent et dont l'Elo de référence est le plus
proche de celui du premier participant (le moteur testé d'un gauntlet), à 150 au plus par défaut ; Entrée les ajoute.
Pour un match, la bibliothèque remplit A, puis B (B est remplacé quand les deux sont choisis), et un moteur glissé sur
A ou B prend sa place.

### Livres propres des participants
<!-- en: participants-own-books 580e52cc -->

*Livre* : un livre d'ouvertures propre au participant, comme `book=`/`bookdepth=` de Cute Chess : un livre Polyglot
(`.bin`) ou PGN de la page Livres et la profondeur jusqu'à laquelle il est suivi, en demi-coups depuis le début de la
partie, ouverture du tournoi comprise (*tout* : tant que le livre a un coup ; pour un livre PGN, les 40 premiers
demi-coups de ses parties). Un livre PGN est d'abord transformé en livre Polyglot : les coups de chaque partie jusqu'à
cette profondeur, chaque coup pondéré par le nombre de parties qui le jouent ; les parties avec un coup illégal sont
écartées et comptées. Cela se fait en arrière-plan dès que le livre est choisi (progression sur le bouton *Livre*, dans
sa fenêtre et dans le résumé), et le résultat est gardé dans le dossier `book-cache` du profil jusqu'à ce que le fichier
ou la profondeur change ; un lancement qui arrive avant la fin l'attend. Un livre qui ne peut pas être préparé montre
pourquoi dans sa fenêtre, avec *Réessayer* ; chaque lancement réessaie aussi. Un tournoi joue à partir du livre préparé
quand il a été lancé ou repris, même si le fichier PGN change entre-temps.

Tant que la position est dans ce livre, le participant joue aussitôt le coup du livre, choisi selon les poids du
livre tels que les règle le *Hasard* des ouvertures (aussi dans la fenêtre *Livre*, le même pour tous les livres du
tournoi : voir [Ouvertures](#ouvertures)) ; puis son moteur prend le relais. Les ouvertures du tournoi, s'il y en a,
sont jouées d'abord, par chaque moteur ; les livres EPD ne donnent que celles-là et sont grisés dans la liste. Pour
comparer des livres avec un moteur, choisis *Ce moteur une fois de plus, avec un autre livre* dans son menu ⋯ : chaque
copie joue sous le nom `<engine> (<book>)`, si bien que ses résultats et son classement restent à part. Dans un match,
les boutons *Livre* sont à côté de A et de B, et un moteur peut jouer contre lui-même avec deux livres. Les coups de
livre sont marqués `{book}` dans le PGN ; survoler un nom dans le classement montre son livre. Les moteurs WinBoard
peuvent en avoir un aussi.

### Réglages des moteurs
<!-- en: engine-settings 7a93d075 -->

*Hash* et *Threads* imposés à chaque moteur, pour un match aussi (0 = ceux de chaque moteur ; ceux d'un participant,
depuis son menu ⋯, passent en premier) ; *Tables Syzygy pour les moteurs* donne les dossiers des tables (les mêmes que
ceux de l'adjudication) à chaque moteur qui a une option `SyzygyPath`, et *Livres propres des moteurs désactivés*
règle `OwnBook` à false là où elle existe, pour que chaque partie soit jouée à partir des ouvertures du tournoi. Les
deux sont désactivés par défaut.

### Cadence
<!-- en: time-control 672cb041 -->

Temps de base et incrément en secondes, nombre de coups par période facultatif, et une marge avant qu'une partie soit
perdue au temps ; un temps fixe par coup (`go movetime`, avec la même marge) ; un nombre fixe de nœuds par coup ; ou
une profondeur fixe en demi-coups (`go depth`), avec laquelle aucun moteur ne peut perdre au temps.

### Parallélisme
<!-- en: concurrency c02ee2cf -->

Le nombre de parties jouées en même temps. En mode automatique, il est mesuré au démarrage :

- nœuds ou profondeur fixes : une partie par thread matériel, puisque l'Hyper-Threading ne change pas les coups joués ;
- avec une pendule : les cœurs physiques − 1, pour que les moteurs ne se ralentissent pas les uns les autres.

La ligne et le texte sous le champ disent quelle règle a choisi le nombre. *Affinité CPU* (désactivée par défaut ;
pas sur macOS, qui n'en a pas) attache chaque partie à un processeur, partagé par ses deux moteurs puisqu'ils ne
réfléchissent jamais en même temps. fastchess ne la prend en charge que pour les moteurs à un seul thread, elle est
donc désactivée automatiquement quand un moteur a `Threads` > 1. Un avertissement apparaît quand parallélisme × threads
dépasse les threads matériels (ou, avec une pendule, les cœurs physiques). *Jouer sur* choisit la machine sur laquelle
le tournoi joue (voir [Un tournoi sur une machine](#un-tournoi-sur-une-machine)).

### Ouvertures
<!-- en: openings 44656411 -->

Un livre EPD, PGN ou Polyglot (`.bin`) (une suite d'ouvertures), l'ordre aléatoire ou séquentiel, le nombre de
demi-coups gardés d'un livre PGN, l'indice de départ et la graine aléatoire (0 = un nouvel ordre pour chaque tournoi).
Chaque ouverture est jouée deux fois, couleurs inversées (dans un match dont les *Couleurs* sont fixées, chaque partie
depuis sa propre ouverture).

Un livre Polyglot est suivi depuis la position initiale jusqu'à une profondeur (8 demi-coups sauf réglage contraire), en
laissant de côté les coups qui pèsent moins d'un dixième du meilleur de leur position ; les positions atteintes sont
écrites une fois dans `<book>-<N>plies.epd` dans le dossier du tournoi et jouées comme un livre EPD. En ordre aléatoire,
elles sont écrites dans l'ordre où elles sont tirées, chacune une fois : une ligne vient d'autant plus tôt qu'elle est
probable, selon les poids de ses coups, si bien qu'un tournoi qui ne joue qu'une partie du livre joue d'abord ses lignes
principales ; la même graine reproduit le même ordre. En ordre séquentiel, elles gardent l'ordre du livre. *Hasard*
(−100 à 100, 0 par défaut) dit comment comptent les poids, comme Polyglot et Cute Chess choisissent un coup de livre : 0
les poids tels quels, −100 le meilleur coup de chaque position seulement (une seule ligne, à moins que plusieurs
partagent le meilleur poids), 100 tous les coups à égalité (les coups rares reviennent) ; entre les deux, −50 élève les
poids au carré et 50 prend leurs racines carrées. Il choisit aussi les coups des livres propres des participants, et est
affiché pour eux même avec un livre EPD ou PGN ; le résumé l'indique, et la ligne sous les copies du classement donne le
livre avec sa profondeur et, pour un livre Polyglot, ce hasard.

Dans un toutes rondes, un gauntlet, un suisse ou un match par équipes, *La même ouverture pour toutes les paires d'une
ronde* donne la même ouverture à chaque paire de parties d'une ronde (échiquier par échiquier, un cycle est une ronde :
chaque échiquier joue la même ouverture), la suivante du livre pour chaque ronde, pour qu'aucun moteur n'ait des
ouvertures plus faciles que les autres de sa ronde (un moteur ajouté plus tard rattrape son retard dans des rondes à
lui, chacune avec son ouverture).

*Signaler les ouvertures déséquilibrées* (désactivé par défaut) montre, au fil des parties, les ouvertures qui décident
la partie à elles seules : une partie est signalée quand elle se termine si les deux moteurs ont vu le même camp en
avance d'au moins le score réglé (200 cp par défaut) à chacun de leurs premiers coups après le livre (4 par défaut ; les
coups du livre propre d'un participant comptent comme coups de livre). Il lit les scores des moteurs, si bien qu'un
moteur qui n'en donne pas ne signale rien ; les résultats ne sont pas changés. L'onglet *Parties* filtre ces parties
(*Ouverture déséquilibrée*), l'onglet *Ouvertures* marque leurs ouvertures d'un `!` et les compte, et la vue d'ensemble
compte les parties. *Rejouer la paire depuis une autre ouverture* annule une telle paire une fois ses deux parties
jouées (elles restent dans les fichiers PGN et le journal, comme toute partie annulée) et la rejoue depuis une ouverture
de la fin du livre (dans l'ordre de jeu du livre), que le tournoi aurait jouée en dernier, pour que les ouvertures des
paires à venir ne bougent pas ; au plus trois fois pour une paire, après quoi la paire compte, signalée. La console et
la vue d'ensemble disent combien d'ouvertures ont été remplacées.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer random, dans les ouvertures : sans livre, les 960 positions de départ en ordre aléatoire (écrites une fois dans
`chess960.epd` dans le dossier des tournois) ; avec un livre, ses positions. Chaque moteur doit jouer le Chess960 : les
moteurs UCI avec l'option `UCI_Chess960`, les moteurs WinBoard avec la variante `fischerandom`. Le résumé nomme ceux
qui ne le jouent pas, et un tel tournoi ne démarre pas.

### Adjudication
<!-- en: adjudication a18dca1e -->

Nulle (à partir d'un numéro de coup, pendant N coups consécutifs, sous un score), abandon (N coups consécutifs
au-dessus d'un score, avec l'accord des deux moteurs si l'on veut), un nombre maximum de coups, et les tables Syzygy.

### PGN et divers
<!-- en: pgn-and-misc 6762bab8 -->

- Nom de l'événement : son infobulle nomme les deux fichiers PGN dans lesquels chaque partie est écrite.
- *Ajouter aussi chaque partie à* : un fichier PGN à toi auquel chaque tournoi ajoute ses parties, comme `-pgnout` de
  cutechess (jamais écrasé, dans le style de commentaires ChessBase / Lichess ou cutechess), dans l'ordre du
  calendrier : les deux parties d'une ouverture côte à côte, les rondes dans l'ordre, quel que soit l'ordre dans lequel
  elles se terminent. Une partie y attend que celles qui la précèdent soient écrites ; une reprise continue ; quand le
  fichier ne peut pas être écrit (verrouillé, déplacé), les parties attendent et le tournoi continue, avec un
  avertissement dans la vue d'ensemble. Un plantage ou une coupure de courant pendant l'ajout de parties ne les écrit
  jamais deux fois : la reprise vérifie le fichier et complète une écriture interrompue, et n'en retire jamais rien.
- Champs en plus dans les commentaires des coups (nodes, seldepth, nps, pv, tbhits, et le temps de décision `dt=` : le
  moment où le moteur s'est fixé sur le coup qu'il a joué ; `wdl=W/D/L` est toujours écrit pour les moteurs qui le
  donnent), activés par défaut puisque les graphiques de l'onglet *Parties* les lisent (pv rend le PGN nettement plus
  gros).
- *Relancer un moteur qui plante*, et *Arrêter le tournoi quand un moteur ne démarre pas* (désactivé par défaut : voir
  [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas)).
- Garder le journal moteur de chaque partie (il est toujours gardé pour les parties qui finissent mal).
- *Vérifier les PV de mat* : la PV d'un score de mat doit avoir la bonne longueur et finir par un mat. Chaque PV est
  vérifiée pour les coups illégaux, les coups après la fin de la partie et un meilleur coup qui n'est pas son premier
  coup ; les coups avec une PV fausse sont marqués dans l'onglet Parties et écrits dans le journal.

### Conseils pour les réglages
<!-- en: tips-for-the-settings 62afdb4d -->

- **Comparer deux versions d'un moteur** : un SPRT, gain [0, 3] pour une modification censée gagner, non-régression
  [-5, 0] pour une simplification ou un nettoyage. Une cadence courte (10+0.1) d'abord, puis une longue (60+0.6) pour
  ce qui passe. Utilise un livre de positions équilibrées, pas la position de départ : sinon chaque partie serait l'une
  d'une poignée d'ouvertures.
- **Parallélisme** : laisse-le automatique. Avec une pendule, plus de parties que de cœurs physiques moins un font que
  les moteurs se ralentissent les uns les autres, et les résultats dérivent ; avec des nœuds ou une profondeur par
  coup, chaque thread matériel peut jouer.
- **Threads** : 1 par moteur pour tester ; beaucoup de parties d'un thread chacune donnent plus de résultats par heure
  que peu de parties de beaucoup de threads. Donne à chaque moteur les mêmes `Threads` et `Hash` dans *Réglages des
  moteurs*.
- **Hash** : assez pour la cadence (16 à 64 Mo en 10+0.1, 256 Mo en 40/15 et plus long) ; le résumé dit quand le hash
  des parties ne tient plus dans la mémoire.
- **Adjudication** : le profil standard fait gagner du temps dans les longs tournois sans beaucoup changer les
  résultats ; laisse-la désactivée pour tester les finales d'un moteur, ou utilise les tables.
- **Ouvertures** : l'ordre aléatoire avec une graine fixe permet de rejouer un test à l'identique ; *La même ouverture
  pour toutes les paires d'une ronde* est plus équitable dans un toutes rondes avec peu de cycles.
- **Plusieurs tournois** : donne à celui qui compte le plus un poids plus grand plutôt que de mettre les autres en
  pause (voir [Plusieurs tournois à la fois](#plusieurs-tournois-à-la-fois)).

## Un tournoi en cours
<!-- en: running-a-tournament 9bcba803 -->

Un tournoi en cours ou ouvert a un espace de travail avec un en-tête (nom, progression, *Pause*, *Arrêter*,
*Cloner*, *Dossier*, *Disposition*) et des panneaux, montrés par défaut sous forme d'onglets :

| Onglet | Ce qu'il montre |
|---|---|
| Vue d'ensemble | Les résultats jusqu'ici et les statistiques, les graphiques, la liste de contrôle avant envoi |
| Classement | Le classement sous forme de tableau ou de tableau croisé, les adversaires d'un moteur, les copies pour un forum |
| Équipes | Un match par équipes : le score des équipes et le tableau croisé des deux équipes |
| Tableau | Un KO : le tableau, tour par tour |
| Calendrier | Chaque paire de parties, et les modifications du tournoi |
| Diffusion | Les fichiers en direct, le serveur TLCS, la page web et lichess |
| Direct | Les parties en cours |
| Parties | Les parties terminées, leurs filtres, leur rejeu et leur export |
| Ouvertures | Les résultats par position de départ |

### Vue d'ensemble
<!-- en: overview 6985cebb -->

**Un match** : l'Elo et le nElo avec leur intervalle de confiance à 95 %, la LOS, le score, les comptes pentanomiaux,
les bornes du SPRT et le LLR. *Statistiques* (replié) donne les chiffres de fishtest à partir des parties et à partir
des paires : score, Elo, nElo et BayesElo avec DrawElo, écart type par partie, LLR des deux, le rapport de variance des
paires aux parties seules, le biais RMS des ouvertures, les sortes de paires équilibrées et les paires gagnées sur
perdues, que *Copier* inclut. Puis les graphiques : Elo et LLR au fil du temps, longueurs des parties, façons dont les
parties se sont terminées, et *Recherche par moteur*.

**Un tournoi** : le classement (avec *TF*, les pertes au temps de chaque moteur, en orange dès qu'il y en a une ; un *!*
à côté d'un moteur qui a planté ou joué un coup illégal), *Recherche par moteur* en dessous, la progression par cycle ou
par ronde, les parties par heure et le temps restant, les derniers résultats.

*Recherche par moteur* donne la moyenne de chaque moteur sur les coups qu'il a cherchés, hors coups du livre :
profondeur, NPS, temps par coup, temps de décision et utilisation du processeur ; un clic sur une colonne trie les
moteurs selon elle. À côté de ses NPS, ×4 dit que le moteur cherche avec 4 threads : il compte plus de nœuds que sur un
seul sans être plus rapide pour autant, donc compare ses NPS à ceux des moteurs qui en ont autant. Les NPS demandent des
moteurs qui les donnent, et *nps* dans *PGN et divers* pour être relus à une reprise.

Les deux comptent les parties issues d'ouvertures déséquilibrées, et les ouvertures remplacées, quand la
configuration les cherche, avec *Afficher* pour ces parties. Un tournoi terminé ajoute une *Liste de contrôle avant
envoi* : toutes les parties jouées, chaque paire avec les deux couleurs, pas de parties en double, pas de plantages,
de pertes au temps ni de coups illégaux (avec des boutons qui ouvrent ces parties), le hash de chaque moteur (montré,
pas jugé), ponder désactivé, le livre d'ouvertures, les tables.

### Classement
<!-- en: standings e4198fcd -->

Points, parties, pertes au temps (*TF*), victoires/nulles/défaites, classements avec leur intervalle de confiance,
Sonneborn-Berger (Buchholz pour un suisse), exempts. *Tableau* les liste un moteur par ligne ; *Tableau croisé* (toutes
rondes, jusqu'à 200 moteurs) montre chaque appariement. *Ordre :* aux points, au score % ou dans l'ordre de départ
(qui garde en place les colonnes du tableau croisé d'un gauntlet). Un clic sur un moteur liste ses adversaires en
dessous : parties, avec les Blancs, avec les Noirs, + = −, points et %, avec les paires dont une seule partie a été
jouée marquées ; un gauntlet s'ouvre avec son moteur testé sélectionné.

*Copier* en texte, ou en BBCode pour un message de forum (avec une ligne qui donne le format, la cadence, les parties
et le livre), et dans un gauntlet *Tableau du gauntlet en texte* ou *En BBCode, pour un forum* (le moteur testé, puis
ses adversaires selon leurs propres points, chacun avec ses points, %, Elo ±, victoires-défaites-nulles) ; *Exporter
en HTML…*. Tous ont *TF* comme le classement du mode sans interface, suivent l'ordre choisi, et finissent par une
ligne qui donne les parties jouées sur celles prévues, le taux de nulles, la cadence, le livre (avec les demi-coups
gardés d'un livre PGN ; pour un livre Polyglot, sa profondeur et la façon dont ses coups ont été choisis, par exemple
`book:Perfect2023.bin (8
plies, moves by weight)`) et l'événement. Les copies signent ensuite avec *fastchess-gui* et sa version, et le tableau
du gauntlet est tracé avec des = plutôt que des -, pour qu'un tableau posté sur un forum se distingue de ceux des
autres programmes.

### Équipes
<!-- en: teams 8c845645 -->

Un match par équipes seulement, dans l'onglet *Classement* (où il s'ouvre) et en haut de la *Vue d'ensemble* : le
score des équipes, « Lynx 17½ – 18½ Otter », avec les parties jouées, le score % de l'équipe A, la différence d'Elo
que ce score représente avec son intervalle à 95 % et la LOS, comptés sur les paires de parties comme le sont les
statistiques d'un match (pentanomial) ; puis les moteurs de chaque équipe avec leurs parties, points, %, classement
et les points que chacun a marqués contre chaque moteur de l'autre équipe (B1, B2… en colonnes, le nom au curseur) :
le tableau croisé des deux équipes. *Tableau* et *Tableau croisé* montrent les moteurs comme dans tout tournoi.
*Copier* et *Exporter en HTML…* donnent les scores des équipes et les moteurs des deux équipes les uns contre les
autres, avec la ligne en dessous.

### Tableau et calendrier
<!-- en: bracket-and-schedule fe207ac0 -->

*Tableau*, un KO seulement : le tableau, tour par tour, avec le score de chaque match. *Calendrier* : chaque paire de
parties par cycle ou par ronde, avec son état et l'ouverture qu'elle joue (une fois commencée, ou une fois sa ronde
commencée quand la ronde en partage une), et les modifications du tournoi (voir
[Modifier un tournoi en cours](#modifier-un-tournoi-en-cours)).

### Direct
<!-- en: live 5561ed9b -->

Les parties en cours, en mosaïque d'échiquiers ou en une partie mise en avant avec les pendules, la recherche en cours
de chaque moteur (profondeur, score, nœuds, nps, PV en SAN), la liste des coups et le graphe de la partie (voir
[Le graphe de la partie](#le-graphe-de-la-partie)) : les évaluations des deux moteurs, et dans ses onglets leurs
chances de gain, le temps par coup avec le temps de décision, la profondeur avec la seldepth, les nœuds, la vitesse,
les accès aux tables, leur écart, ou combien de demi-coups une PV reprend de ce que la PV de l'autre moteur avait
prévu. Les parts de victoire, de nulle et de défaite (`UCI_ShowWDL`, activé pour les moteurs qui l'ont quand ils sont
ajoutés) suivent chaque ligne de recherche ; l'onglet *G/N/P* les dessine en bandes, les victoires des Blancs en bas.

Quand la place le permet (une partie mise en avant, un grand panneau), chaque moteur a sa carte : son logo aussi haut
que son nom et sa pendule, ce qu'il fait (il réfléchit, ou le coup qu'il a joué), sa recherche chiffre par chiffre
(profondeur et seldepth, score, nœuds, vitesse, les accès aux tables et le remplissage du hash quand le moteur les
envoie, le temps passé sur le coup jusque-là) et toute sa PV ; le moteur qui attend montre ce qu'il disait au moment de
jouer, avec l'instant où il s'est décidé. À côté de la mosaïque, le panneau garde une ligne de chiffres et les 12
premiers demi-coups de la PV. *Score du camp au trait* | *Score des Blancs*, à droite de l'onglet, choisit le côté de
tous ses scores : celui du camp au trait, comme fastchess les écrit dans le PGN, ou celui des Blancs ; la barre
d'évaluation et le grand écran sont toujours du côté des Blancs.

Au-dessus des coups : l'ouverture, le code ECO et le nom de la position la plus avancée de la partie que la table des
ouvertures nommées connaît (coups du livre et des moteurs confondus, si bien qu'une transposition après le livre est
nommée aussi), avec le dernier coup du livre ; et dès que les tables Syzygy du tournoi (celles de son adjudication,
sinon celles données aux moteurs) connaissent la position affichée, leur verdict : le camp qui gagne avec la DTZ (les
demi-coups jusqu'à la prochaine prise ou au prochain coup de pion : les tables Syzygy n'ont pas de distance au mat), ou
la nulle, la règle des 50 coups comptée d'après le compteur de la partie sauf si l'adjudication l'ignore. Chaque
position est sondée une fois, hors des fils qui font jouer les parties. Dans la liste, chaque coup d'un moteur a son
score, sa profondeur et son temps ; le survol y ajoute l'instant où le moteur s'est décidé et la réponse qu'il attendait
(le second coup de sa PV : fastchess fait jouer sans réflexion sur le temps de l'adversaire). Les coups du livre disent
*livre*, une ligne marque la fin du livre, et le coup en réflexion dit *réfléchit*.

La mosaïque a un échiquier par partie en cours, et non par partie que le parallélisme permet : vers la fin d'un
tournoi, ou avec moins de parties que cela, les échiquiers restants grandissent. Un échiquier dont la partie s'est
terminée reste 10 secondes (la partie suivante de son emplacement y commence), celui de la partie suivie reste, et
quand rien ne joue, les dernières parties restent. Quand les échiquiers demanderaient une barre de défilement, la
mosaïque prend de la largeur à la partie mise en avant, jusqu'à lui laisser 40 % de la page.

Sous l'échiquier : le compteur des 50 coups (demi-coups depuis la dernière prise ou le dernier coup de pion, sur 100),
la différence de matériel avec les pièces qu'un camp a en plus, et les pièces restantes, marquées *TB* quand
l'adjudication par les tables peut terminer la partie ; *Détails sous les échiquiers* les ajoute à la mosaïque. En
dessous, les boutons qui parcourent les coups (voir [Les échiquiers et leurs boutons](#les-échiquiers-et-leurs-boutons))
: on peut revenir en arrière dans la partie mise en avant, ou dans un échiquier détaché, pendant qu'elle continue (aussi
avec ← → Origine Fin, un clic sur un coup de la liste ou sur la courbe), la position antérieure affichée avec son coup
marqué dans la liste et sur la courbe ; *Revenir au coup en cours* (ou Fin, ou un clic sur le dernier coup) fait de
nouveau suivre la partie, comme à chaque nouvelle partie.

*Flèches* (activé par défaut) dessine le coup auquel pense le camp au trait, dans la couleur de son moteur, et la
réponse qu'il attend, plus claire, dans la couleur de l'adversaire, sur l'échiquier mis en avant et les échiquiers
détachés (pas la mosaïque), quand il montre le coup en cours. Dans un gauntlet, *Moteur testé en bas* (activé par
défaut) montre chaque nouvelle partie du côté du moteur testé ; retourner l'échiquier (F, ou son bouton) le retourne
toujours jusqu'à la partie suivante. *Détacher* ouvre un échiquier dans sa propre fenêtre. Un clic droit sur un
échiquier termine sa partie avec un résultat (voir [Modifier un tournoi en cours](#modifier-un-tournoi-en-cours)).
*Grand écran* montre la partie suivie en remplissant la fenêtre (voir [Grand écran](#grand-écran)).

### Grand écran
<!-- en: big-screen 14c8f0d6 -->

Pour une télévision, un club ou un stream, *Grand écran* (dans l'onglet *Direct*, sur un échiquier détaché, ou dans
la palette de commandes) montre une partie qui remplit la fenêtre, en clair sur fond noir : les noms des joueurs et
leurs pendules en gros caractères (le camp au trait et sa pendule qui tourne en jaune, une pendule sous dix secondes en
rouge), la barre d'évaluation, l'évaluation, la profondeur et la vitesse de la recherche, le dernier coup encadré sur
l'échiquier et écrit en grand, l'ouverture, les lignes des moteurs (*Lignes*, activé par défaut : celle du camp au
trait pendant qu'il cherche, en jaune, celle de l'autre à son dernier coup, deux lignes chacune au plus), le verdict
des tables une fois qu'elles connaissent la position, et les coups en gros caractères, le dernier marqué ; le résultat par-dessus l'échiquier
une fois la partie finie. Son texte suit la taille de la fenêtre et a une taille propre (*A-* / *A+*, ou les touches
+ et -). Il montre la partie suivie, ou passe d'une partie en cours à l'autre toutes les N secondes (*Chaque partie à
tour de rôle*), ou suit *La plus intéressante* : la plus proche de l'équilibre, la plus longue parmi celles qui le
sont autant, gardée tant qu'elle le reste. Les commandes s'affichent trois secondes quand la souris bouge (le pointeur
se cache avec elles) ; ← et → changent de partie, F11 met la fenêtre en plein écran, Échap quitte. Un échiquier
détaché déplacé sur un autre écran (la télévision) le montre dans sa propre fenêtre, F11 mettant cette fenêtre en
plein écran pendant que la fenêtre principale continue ; ancré dans la fenêtre principale, c'est la fenêtre principale
qui le montre.

### Parties
<!-- en: games ba4a9604 -->

Les parties terminées avec leur ouverture (code ECO et nom), et un échiquier de rejeu : les boutons en dessous, ← →
Origine Fin et F, ou un clic sur un coup ou sur le graphe ; les mêmes détails en dessous et le même graphe de la
partie que dans *Direct* (voir [Le graphe de la partie](#le-graphe-de-la-partie)), à partir des champs avec lesquels
le PGN a été écrit : un graphique dont le tournoi n'a pas écrit le champ est dans le menu `⋯` du graphe, grisé, et le
dit avec un bouton vers les options PGN de la configuration.

**Filtres** : par résultat (victoires, nulles, défaites, pertes au temps, plantages et coups illégaux, adjudications,
PV fausses, et les parties d'une ouverture signalée déséquilibrée quand la configuration les cherche) ; par ce que
disent les évaluations (les moteurs sont en désaccord de N pions pendant 3 coups de chacun d'affilée, l'évaluation d'un
moteur varie de N pions d'un de ses coups au suivant, le perdant se croyait mieux de N pions ; la partie s'ouvre à ce
moment) ; et par ouverture (un code ECO ou son début, une plage comme `B20-B99`, ou des mots du nom).

**Export** d'une partie, de toutes ou des parties filtrées : *Exporter tout…* propose `<name>.pgn` à côté du dossier du
tournoi ; les propres fichiers PGN du tournoi sont refusés comme cible. *Exporter pour une liste de classement…* écrit
les parties comme les envoient les testeurs des listes de classement (par exemple le CCRL) :
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, un zip aussi si on le demande ; les joueurs nommés
comme les listes les écrivent, `<engine> 64-bit 4CPU` (modifiable pour chaque participant), les rondes renumérotées, les
balises `Event` et `Site` remplies, les parties annulées et les doublons écartés, et celles perdues par un plantage ou
un blocage aussi, sauf si c'est décoché ; le `PlyCount` d'une partie perdue par un coup illégal ne compte que les coups
écrits. Le testeur et le lieu sont mémorisés.

*Journal moteur* ouvre le journal d'une partie qui en a un (voir [Fichiers](#fichiers)). Un clic droit sur une partie
la rejoue, l'annule ou fixe son résultat (voir [Modifier un tournoi en cours](#modifier-un-tournoi-en-cours)).

### L'onglet Ouvertures
<!-- en: the-openings-tab c51dd29a -->

Les résultats par position de départ : ses paires de parties, le score des Blancs, les nulles, et combien de paires
les Blancs ont gagnées deux fois, les Noirs deux fois (l'ouverture favorise une couleur) ou un même moteur deux fois
(elle départage les moteurs), avec la longueur moyenne. Triées par n'importe quelle colonne, filtrées sur les
déséquilibrées, les nulles ou les décisives, ou sur celles *Signalées déséquilibrées pendant les parties* (voir
[Ouvertures](#ouvertures) dans la configuration : marquées `!`, leur nombre à côté du total, avec les ouvertures
remplacées). *Exporter en EPD…* écrit les positions affichées, pour les garder ou pour nettoyer un livre, *Sans les
déséquilibrées* (activé par défaut, affiché dès qu'une ouverture est signalée) écartant celles qui sont signalées. Un
clic montre la position et ses parties. Dans un match dont les *Couleurs* sont fixées, chaque partie a sa propre
ouverture : le décompte porte sur les parties, et les colonnes et filtres sur les paires ne sont pas affichés.

### Panneaux et dispositions
<!-- en: panels-and-layouts 9ed9b915 -->

Chaque onglet est un panneau. Fais-en glisser un par son onglet pour l'ancrer sur un côté d'un autre panneau ou comme
onglet à côté de lui, ou dépose-le ailleurs pour le faire flotter (aussi sur un second écran sous Windows et
Linux/X11, pas sous Wayland ni avec le rendu logiciel). La partie suivie dans *Direct* se décompose en panneaux
*Échiquier*, *Joueurs*, *Recherches*, *Coups* et *Graphique* ; les éléments qui n'ont pas de panneau ouvert restent à
côté de la mosaïque dans *Direct*, et les échiquiers détachés s'ancrent comme les autres panneaux. Ferme un panneau
avec le x de son onglet ; fais-le revenir depuis *Disposition* > *Panneaux*, ou avec *Afficher : …* dans la palette de
commandes, et il revient ancré.

*Disposition* propose aussi des dispositions toutes prêtes (*Onglets*, comme par défaut ; *Suivre une partie* :
l'échiquier à gauche, les joueurs, les recherches et le graphique à droite, le classement en dessous ; *Grand tournoi* :
la mosaïque à côté du classement ; *Diffusion* : un grand échiquier avec les joueurs, les recherches et le classement),
*Enregistrer la disposition sous…* (les tiennes, par nom, gardées dans `layouts.json` dans le profil), *Supprimer* et
*Disposition par défaut*. Un panneau sorti de l'espace de travail reste à l'écran sur les autres pages ; l'agencement
est gardé d'une session à l'autre.

### Utilisation du processeur
<!-- en: processor-use e5234cbe -->

Pendant que des parties se jouent, le temps processeur des processus de chaque moteur (le moteur derrière un
adaptateur WinBoard compris) est mesuré à chaque `go` et à chaque coup. La vue d'ensemble montre, pour chaque moteur,
les cœurs qu'il occupe pendant qu'il réfléchit et la part d'un cœur qu'il utilise pendant que son adversaire
réfléchit, et avertit quand un moteur utilise nettement plus de cœurs que ses `Threads`, ou réfléchit sur le temps de
son adversaire (un ponder qu'on ne lui a pas demandé, un thread d'arrière-plan) : ce qui rend une partie inéquitable
pour l'adversaire et pour les autres parties en cours. La mesure a besoin de 20 s de chacun pour se prononcer, et
compte chaque partie jouée, dans la fenêtre ou sans interface (`--run`) : ce qu'ont utilisé les moteurs de chaque
partie est écrit dans `processor.tsv` dans le dossier du tournoi, si bien qu'une reprise le garde. Elle ne fait
qu'avertir ; elle ne décide jamais une partie.

### Pause, arrêt et reprise
<!-- en: pause-stop-and-resume 100c55cf -->

**Pause** ne commence aucune nouvelle paire de parties. Les parties en cours vont jusqu'au bout, de même que la
seconde partie d'une paire dont la première a commencé, si bien qu'une pause ne jette jamais un résultat. **Reprendre**
continue. **Arrêter** termine d'abord les parties en cours, ou arrête tout de suite (les parties interrompues ne sont
pas comptées et seront rejouées).

Un tournoi arrêté par un moteur dit lequel dans son en-tête et sur la page d'accueil, sous *À traiter* :
*Interrompu : Alpha n'a pas pu démarrer : …* (après son dernier essai), ou *Alpha a planté ou ne répond plus
(partie 12)* (sans *Relancer un moteur qui plante*), avec *Ouvrir le moteur* et *Ouvrir le journal*.

**Chaque tournoi reprend exactement là où il s'est arrêté**, après un arrêt, une fenêtre fermée, un plantage ou une
coupure de courant : aucune partie n'est perdue et aucune n'est jouée deux fois, et une paire de parties à moitié jouée
est complétée avec la même ouverture et les mêmes couleurs. *Reprendre* sur la page d'accueil le continue, avec les
statistiques des parties déjà jouées : les résultats, et la profondeur, le temps par coup et les NPS des moteurs relus
dans les commentaires des coups (les NPS seulement quand le PGN les enregistre, *nps* dans *PGN et divers*). Quand le
programme s'est terminé pendant qu'un tournoi tournait (un plantage, une coupure de courant, un redémarrage, ou le
programme quitté pendant qu'il tournait), le lancement suivant le reprend de lui-même, et la file d'attente continue
après lui : *Reprendre au lancement le tournoi qui était en cours*, dans **Réglages**, activé par défaut. Un tournoi mis
en pause ou arrêté t'attend, et `--autostart` lance à la place la configuration enregistrée. Un participant dont le
programme a été recompilé ou a disparu depuis qu'il a rejoint le tournoi est nommé dans l'en-tête du tournoi repris,
avec *Continuer* et *Pause* : ses parties suivantes sont jouées par le programme tel qu'il est maintenant, avec les
options que le tournoi lui a données.

### Plusieurs tournois à la fois
<!-- en: several-tournaments-at-once df7ce566 -->

Plusieurs tournois peuvent jouer côte à côte : un gauntlet à côté d'un SPRT, deux gauntlets à la fois. *Lancer* sur la
page *Nouveau tournoi* en lance un autre pendant que d'autres jouent ; *Reprendre* en reprend un à côté d'eux. Chaque
tournoi joue dans son propre processus (`fastchess-gui --run <folder>` piloté par la fenêtre), si bien qu'un
plantage n'emporte que ce tournoi, et la fenêtre, ses vérifications de moteurs, ses tests rapides et ses suites de
tests n'attendent plus la fin d'un tournoi.

L'espace de travail a **un onglet par tournoi ouvert**, au-dessus de son en-tête : un point pendant qu'il joue (un
symbole de pause pendant qu'il est en pause), son statut au curseur, une croix pour fermer celui qui ne joue pas. Les
panneaux montrent l'onglet choisi ; la palette de commandes liste les autres (*Afficher : …*, *Pause*, *Reprendre*).
Un tournoi terminé ouvert depuis la liste prend l'onglet de celui qui était seulement consulté. La page d'accueil a
une carte pour chaque tournoi qui joue, avec sa progression, son meneur, *Ouvrir* et *Pause*, et en dessous son
**poids**, les cœurs que ses parties utilisent sur ceux qu'il peut utiliser, et ce qu'attend sa prochaine partie ; la
barre *Cœurs* en haut montre la part de chaque tournoi (et celle de la page Échiquier), et combien de cœurs les tournois
se partagent. Le *Journal* montre les lignes d'un tournoi, de la fenêtre, ou toutes (chaque ligne d'un tournoi porte
son nom) ; les lignes d'un tournoi sont aussi gardées dans `fastchess-gui.log` dans son dossier.

**Partager la machine** : les tournois qui jouent se partagent un budget de cœurs, réglé dans **Réglages** (*Cœurs
pour les tournois* : les cœurs physiques moins un par défaut). Chacun reçoit d'abord de quoi jouer une partie tant que
le budget le permet, le plus gros poids en premier ; le reste va selon le poids (un tournoi de poids 2 reçoit deux fois
les cœurs d'un tournoi de poids 1), aucun au-delà de son propre parallélisme, ce que l'un ne peut pas prendre allant
aux autres. Une partie compte pour les `Threads` du plus gros de ses deux moteurs : l'un réfléchit pendant que l'autre
attend. Un tournoi en pause ne garde que ce dont ont besoin ses parties en cours et les secondes parties de leurs
paires. Une part plus petite n'interrompt jamais une partie : le tournoi joue moins de parties à partir de la
suivante. Un tournoi seul joue autant de parties que son parallélisme l'indique.

**Mémoire** : une partie ne commence que quand le `Hash` de ses deux moteurs, et ce que chaque processus de moteur
prend en plus (64 Mo, et 8 Mo par thread), tiennent dans ce que les parties en cours laissent du budget de mémoire
(*Mémoire pour les moteurs* dans **Réglages**, 75 % de la mémoire installée par défaut), partagé entre les tournois. Le
résumé de la configuration dit combien de parties le budget laisse jouer à la fois, et la carte *Santé* de la page
d'accueil le dit quand des parties attendent de la mémoire.

**GPU** : un moteur marqué *Utilise un GPU* sur sa fiche (un moteur à réseau comme Lc0) compte sur les *GPU* des
**Réglages** : pas plus de tels moteurs ne jouent à la fois qu'il n'y a de GPU, dans un tournoi et entre tournois,
pendant que les autres parties jouent à côté. Le résumé de la configuration dit combien de parties avec un moteur GPU
cela permet, et avertit quand aucun GPU n'est compté, ou quand une partie entre deux moteurs GPU demande plus de GPU que
la machine n'en a (elle joue alors seule).

**Les laisser jouer** : fermer la fenêtre pendant que des tournois jouent demande quoi faire. *Les laisser jouer et
quitter* (le choix par défaut) laisse chacun jouer dans son processus ; la fenêtre les montre de nouveau, où ils en
sont, à son lancement suivant. *Arrêter et quitter* les arrête, chacun pour être repris exactement là où il s'est
arrêté. Le test rapide s'arrête dans les deux cas. Un tournoi dont le processus a disparu au lancement suivant (un
redémarrage, un plantage) est repris comme tout tournoi interrompu. La diffusion de l'onglet *Diffusion* tourne dans
la fenêtre, pour le tournoi affiché : pour une diffusion qui continue sans la fenêtre, lance le tournoi depuis un
terminal avec les options de diffusion (voir [Mode sans interface](#mode-sans-interface)).

### Un moteur qui ne démarre pas
<!-- en: an-engine-that-cannot-start 765a7c61 -->

Une nuit sans surveillance ne s'arrête pas sur un seul moteur. Un moteur qui ne démarre pas (un programme déplacé ou
recompilé, un fichier de réseau manquant, un disque disparu) est réessayé trois fois, à 10 s d'intervalle ; puis ses
parties sont mises de côté et les autres parties continuent. Il est réessayé toutes les 5 minutes, une partie à la
fois, et ses parties reviennent dès qu'il démarre. L'en-tête du tournoi et la page d'accueil (sous *À traiter*) disent
quel moteur, l'erreur, et proposent :

- **Réessayer** : tout de suite, plutôt qu'au prochain essai (après avoir corrigé le programme, par exemple).
- **Retirer…** et **Remplacer…** (toutes rondes, gauntlets et matchs par équipes) : le panneau de modification de
  l'onglet Calendrier, réglé pour ce moteur, avec l'impact montré avant que quoi que ce soit change.
- **Journal moteur** : `logs/start-<engine>.log` dans le dossier du tournoi, ce qui a été envoyé aux moteurs de son
  dernier essai et ce qu'ils ont répondu, leur sortie d'erreur comprise (souvent la raison : une bibliothèque ou un
  fichier de réseau introuvable).

Le journal et la notification du bureau disent quand un moteur est mis de côté. Le tournoi ne s'arrête que quand il ne
reste rien à jouer sans les moteurs mis de côté, après un dernier essai de chacun ; son statut et la notification de
fin le disent (*Arrêté : il ne reste aucune partie à jouer sans …*), et la file d'attente continue avec le tournoi
suivant. *Reprendre* réessaie chaque moteur à neuf. Une pause retient les essais. *Arrêter le tournoi quand un moteur ne
démarre pas*, dans *PGN et divers* de la configuration, l'arrête plutôt tout de suite, une fois les essais épuisés.

### Modifier un tournoi en cours
<!-- en: changing-a-running-tournament aebe63e2 -->

Le panneau **Modifier le tournoi** de l'onglet *Calendrier* fonctionne sans rien arrêter : les parties en cours
continuent et aucune partie jouée n'est perdue. Chaque modification montre son impact avant d'être appliquée.

- **Ajouter un moteur** : il joue les cycles déjà commencés (*Rattraper*) ou se joint à partir du cycle suivant.
- **Retirer un moteur** : ses parties restantes sont abandonnées ; ses parties jouées jusque-là comptent ou non.
- **Remplacer un moteur** : ses parties cessent de compter et sont rejouées par le nouveau moteur.
- **Nombre de cycles** : plus ou moins.
- **Rejouer des parties** : toutes les parties d'un moteur, ou les pertes au temps et les plantages.
- Clic droit sur une partie (ici, ou dans l'onglet *Parties*) : la rejouer, l'annuler (sans la rejouer), ou fixer son
  résultat.

Un moteur ajouté ou remplaçant joue avec les réglages du tournoi (sa cadence, son Hash et ses Threads, affichés sous le
moteur), ou avec des *Réglages propres* : Hash, Threads, part du temps (ou nœuds, profondeur), comme les réglages
propres d'un participant sur la page *Nouveau tournoi*. Un tournoi créé avant la 0.6.6 n'enregistrait pas ses réglages :
ceux de son premier moteur en tiennent lieu, et ce pouvaient être ses réglages propres.

Dans un match par équipes, un moteur ajouté rejoint l'équipe choisie et joue contre chaque moteur de l'autre équipe ;
échiquier par échiquier, on ne peut pas ajouter de moteur (son échiquier n'aurait pas d'adversaire) : remplaces-en un
à la place. Un remplaçant prend l'équipe et l'échiquier du moteur qu'il remplace ; davantage de cycles apparient les
moteurs qui jouent encore (échiquier par échiquier, sur les échiquiers du premier cycle). Les parties qu'un moteur
retiré ou remplacé a jouées comptent toujours pour son équipe, à moins qu'elles ne soient annulées.

**Fixer le résultat** tranche une partie que le programme n'a pas su bien trancher : un blocage qu'il n'a pas détecté,
une finale gagnée où les moteurs tournent en rond, une partie terminée pour une mauvaise raison. Choisis 1-0, ½-½ ou
0-1 et donne une raison ; les points que cela déplace sont montrés avant que *Fixer le résultat* l'applique. Le
classement, le tableau croisé, les classements Elo et la liste de contrôle avant envoi comptent le nouveau résultat
(une adjudication : une perte au temps changée en nulle ne compte plus comme telle) ; l'onglet *Parties* le marque d'un
`*`, son infobulle donnant la raison et la façon dont la partie s'est terminée telle que jouée, et *Revenir au résultat
joué* revient en arrière. Une partie en cours se termine de la même façon : clic droit sur son échiquier dans l'onglet
*Direct*, *Terminer avec un résultat* (aussi dans la palette de commandes pour la partie suivie). Le moteur au trait est
arrêté, fastchess termine aussitôt la partie et elle compte avec le résultat choisi ; le moteur redémarre pour sa
partie suivante. Il faut pour cela *Relancer un moteur qui plante* (configuration, *PGN et divers*, activé par
défaut).

Les parties annulées et les résultats fixés à la main restent dans les fichiers PGN et le journal tels qu'ils ont été
joués : les fichiers PGN ne font jamais que s'allonger. Tout ce qui lit les parties en tient compte : les parties
annulées sont écartées, et un résultat fixé à la main est dans chaque export (*Exporter la partie…*, *Exporter tout…*,
*Exporter pour une liste de classement…*, l'export face-à-face de la page *Classements*, le fichier d'*Ajouter aussi
chaque partie à*) avec `[Termination "adjudicated by the operator"]` et la raison dans un commentaire avant le résultat.
**Annuler la dernière modification** est proposé jusqu'au début de la partie suivante : rien n'a encore été joué sous la
modification, elle peut donc être défaite exactement (les parties qui étaient déjà en cours quand elle a été faite sont
gardées). Un résultat fixé à la main peut être défait tant que c'est la dernière modification, que des parties aient été
jouées depuis ou non (dans un KO, dont les tours suivants découlent des résultats, jusqu'au début de la partie suivante)
; l'onglet *Parties* propose aussi *Annuler* à côté de la partie.

### File d'attente et relance
<!-- en: queue-and-relaunch 382fd33e -->

La **File d'attente** de la page d'accueil contient des tournois à jouer l'un après l'autre : quand l'un se termine de
lui-même, le suivant commence (pas après un arrêt ou une erreur, ni pendant que *Tout mettre en pause* retient la file
d'attente). Avec *Lancer quand des cœurs sont libres*, le suivant commence à côté de ceux qui jouent dès que le budget
de cœurs dépasse ce qu'ils peuvent prendre, chacun à son parallélisme. Ils peuvent être réordonnés ou retirés, et le
premier lancé tout de suite quand rien ne tourne. La page d'accueil dit quand le tournoi en cours se termine (*Fin vers
HH:MM (dans …)*) et quand toute la file d'attente se termine, chaque tournoi en attente avec sa durée et, tant que les
durées de ceux qui le précèdent sont connues, l'heure à laquelle il commence ; les tournois dont la durée ne peut pas
être connue (un SPRT sans maximum, des nœuds ou une profondeur par coup) sont comptés à part.

**Relancer avec…** sur un match terminé (dans son en-tête, ou sous *Derniers terminés* sur la page d'accueil) le
rejoue avec un autre moteur A, typiquement la version suivante, contre le même moteur B, avec les autres réglages du
match (ceux de la page *Nouveau tournoi* pour un match lancé par une version plus ancienne, qui ne les gardait pas).

**Cloner** (dans l'en-tête d'un tournoi, sous *Derniers terminés* et dans *Tous les tournois* sur la page d'accueil)
ouvre la page *Nouveau tournoi* remplie avec la configuration de ce tournoi, quel que soit son format : les
participants avec leurs propres réglages, la cadence, les ouvertures, l'adjudication et le reste, chaque valeur telle
qu'elle a été jouée. Ses profils sont nommés, et un profil modifié depuis est marqué comme changé pour ce tournoi.
Change ce qui doit différer (une autre cadence, la version suivante d'un moteur, un autre livre), puis **Lancer** ou
*Mettre en file d'attente*. Un tournoi qui joue encore peut aussi être cloné ; un tournoi lancé par une version plus
ancienne, qui ne gardait pas sa configuration, ne le peut pas.

## Classements
<!-- en: ratings 3e4be48f -->

La page **Classements** calcule des classements au maximum de vraisemblance (le modèle logistique, les nulles comptant
pour un demi-point, comme Ordo le fait par défaut) sur toutes les parties de tous les tournois du profil, et des
fichiers PGN importés. Les moteurs sont identifiés par leur nom. Les parties comptées peuvent être restreintes en haut
de la page, pour tous les onglets :

- une cadence, un groupe de la bibliothèque de moteurs (les parties entre ses moteurs), les 30 ou 365 derniers jours ;
- *Parties importées* : les parties des fichiers PGN listés sous *Fichiers PGN…* (le téléchargement d'une liste de
  classement, les parties d'un autre programme), lues dans leurs en-têtes ; leurs moteurs sont connus par les noms que
  le PGN leur donne, et un nom qui est celui d'un moteur de la bibliothèque, aux majuscules et aux espaces près
  (« stockfish  17 » pour « Stockfish 17 »), est ce moteur : ses parties comptent comme celles du moteur et son Elo
  de référence peut les ancrer. Rien de plus lâche n'est appliqué de soi-même : « Stockfish 17 64-bit » reste un
  moteur à part jusqu'à ce que tu dises quel moteur c'est dans *Joueurs PGN…* (à côté du nombre de moteurs de la
  bibliothèque, ou dans *Fichiers PGN…*). Cette fenêtre liste par leur nom les joueurs qui ne sont aucun moteur de la
  bibliothèque, avec une recherche : pour chacun, *Un moteur à part* (le défaut) ou un moteur de la bibliothèque. Un
  nom que deux moteurs de la bibliothèque partagent, aux majuscules près, est affiché dans la couleur d'avertissement
  pour que tu choisisses ; un moteur probable (son nom commence celui du joueur) est suggéré sous la forme *Peut-être
  … ?*, jamais appliqué. La fenêtre s'ouvre d'elle-même après l'ajout d'un fichier PGN quand il y a quelque chose à
  décider. Les choix, un moteur à part compris, sont gardés avec le moteur (ils suivent un renommage) et s'appliquent
  à chaque fichier PGN et aux listes de classement importées pour l'Elo de référence ; les parties sont relues avec eux
  quand la fenêtre est fermée. La liste de classement dit combien de moteurs de la bibliothèque contiennent les
  fichiers PGN, son infobulle lesquels ;
- *Ancre* : la moyenne à 0, ou un moteur à un classement fixe (par défaut le premier moteur qui a un Elo de
  référence).

### Liste de classement
<!-- en: rating-list c726b55e -->

La liste, avec un nombre minimum de parties. Outre l'Elo, le ± et les parties, chaque moteur montre son score, ses
victoires (+), nulles (=) et défaites (−), son taux de nulles, *Adv.* : le classement moyen des adversaires qu'il a
rencontrés, partie par partie, *LOS* : la probabilité qu'il soit plus fort que le moteur suivant de la liste, d'après
les deux classements et la covariance de leurs erreurs (approchée au-delà de 1000 moteurs, comme le dit son
infobulle), et *Écart* : son Elo au-dessus du moteur suivant. Un clic sur l'en-tête d'une colonne trie selon elle
(# garde la place au classement) ; un clic droit montre ou masque des colonnes (*Écart* est masquée au départ). Un
clic sur un moteur ouvre sa fiche.

*Copier* en texte ou en BBCode, avec *Avec un en-tête* pour mettre au-dessus de la liste l'en-tête décrit ci-dessous.
*Exporter…* : CSV avec toutes les colonnes ; une page HTML (claire ou sombre) et du Markdown (un tableau que GitHub et
les forums affichent), qui s'ouvrent tous deux par un en-tête qui dit comment la liste a été faite : les tournois et
les fichiers PGN comptés, les parties avec les victoires des Blancs et des Noirs, les nulles et le score des Blancs,
l'ancre, les filtres, le nombre minimum de parties, la date et la version ; et *Parties pour Ordo…* : les parties
comptées sous forme de PGN d'en-têtes, que lisent Ordo et BayesElo (`ordo -p ratings.pgn -o ratings.txt`).

### Fiche du moteur
<!-- en: engine-card 80442e81 -->

La fiche d'un moteur : son classement, ses résultats en tout et par cadence, ses pertes au temps, plantages et coups
illégaux (chaque partie s'ouvre dans son tournoi), ses tournois (chacun s'ouvre), sa profondeur moyenne, son temps par
coup et ses NPS lus dans les commentaires des coups, et son classement après chaque tournoi. La page *Moteurs* y mène
avec *Ses résultats…*.

### Face-à-face
<!-- en: head-to-head 1021ba2f -->

Deux moteurs l'un contre l'autre : le score, l'Elo et la LOS, les comptes pentanomiaux des paires dont ils ont joué
les deux parties, par cadence et par tournoi, leurs parties (chacune s'ouvre dans son tournoi) et *Exporter les
parties…*.

### Progression
<!-- en: progress e2ea28fc -->

Le classement des moteurs cochés après chaque tournoi qu'ils ont joué, sur un axe du temps : les versions d'un moteur
côte à côte.

## Diffusion
<!-- en: broadcast c2f489e7 -->

L'onglet *Diffusion* d'un tournoi en cours le publie pendant qu'il joue ; l'instantané est actualisé deux fois par
seconde. *Partie diffusée* choisit la partie envoyée à TLCS et à la page web : automatique, ou celle choisie dans
l'onglet *Direct*.

### Fichiers en direct
<!-- en: live-files 09341896 -->

Écrits de façon atomique dans un dossier de ton choix, à publier où tu veux (FTP, rsync, un site web) : `live.pgn`
(les parties en cours), `standings.html` (classement et tableau croisé), `tournament.json` (tout, pour les scripts).

### Serveur TLCS
<!-- en: tlcs-server 712f28cb -->

Port UDP, 16001 par défaut : le protocole du Tom's Live Chess Server, pour que TLCV et d'autres visionneuses TLCS
puissent suivre le tournoi (échiquier, pendules, PV, tableau croisé et parties jouées). Il est vérifié avec
node-tlcv. *Un port par partie* met chaque partie simultanée sur son propre port : la partie n° 1 de l'onglet *Direct*
sur le port UDP, la n° 2 sur le suivant, et ainsi de suite. Chaque port s'ouvre quand sa partie commence, avec ses
propres limites de clients et de connexions, et reçoit le tableau croisé après chaque partie ; *Partie diffusée* ne
s'applique alors pas.

### Page web
<!-- en: web-page 1d4c78b3 -->

Port TCP : une page servie par fastchess-gui lui-même, pour n'importe quel navigateur du réseau, en lecture seule et
actualisée chaque seconde. La partie suivie (la première en cours, ou celle cliquée) est au milieu : l'échiquier avec le
dernier coup, un roi en échec, les coordonnées et les flèches ; la pendule de chaque joueur en gros caractères ;
l'évaluation de chaque moteur en gros caractères, avec celui qui réfléchit, sa recherche en vignettes
(profondeur/profondeur sélective, nœuds, vitesse, temps, accès aux tables, hash) et sa ligne de jeu ; et les coups, avec
les coups de livre en italique. Parcours les coups avec les flèches sous l'échiquier ou les touches ← → (▶ après le
dernier coup revient à la partie telle qu'elle se joue), retourne l'échiquier avec ⇅ ou F, et copie le PGN de la partie
ou le FEN de la position.

À côté de la partie, des onglets montrent le classement et le tableau croisé, les graphiques de la partie suivie, la
recherche de chaque moteur en moyenne par coup avec ses anomalies, les prochaines parties du calendrier (un suisse ou un
KO apparie ses rondes suivantes au fur et à mesure), les statistiques et anomalies des parties jouées, et l'événement.
Chaque partie en cours est montrée en dessous, avec une petite barre d'évaluation à côté de chaque joueur. *Moves* fait
passer les coups des lettres (Nf3) aux figurines (♘f3), et le thème suit le système ou est réglé sur clair ou sombre ;
le navigateur se souvient de ces choix. La page ne charge rien d'ailleurs et fonctionne sur les téléphones. *Download
the games (PGN)* donne le fichier PGN du tournoi, jusqu'à la dernière partie enregistrée. `state.json`, que la page lit,
contient tout cela pour les scripts. Les logos des moteurs (voir [Apparence](#apparence)) s'affichent à côté de leurs
noms, sous forme de petites images PNG que le programme en tire et sert lui-même ; la page ne connaît que ceux des
moteurs du tournoi.

### lichess
<!-- en: lichess 7597e86f -->

Une diffusion lichess, avec l'échiquier, la barre d'évaluation et la discussion de lichess, pour n'importe qui sur
internet. Il lui faut un jeton d'API personnel avec la portée `study:write` (*Créer un jeton sur lichess…*), gardé dans
`settings.json` dans le profil seulement, enregistré quand on quitte son champ. Sans ronde donnée, une diffusion
nommée d'après le tournoi est créée, non listée sauf si *Listée parmi les diffusions de lichess* est coché ; son lien
est affiché une fois qu'elle est à l'antenne. Sinon, les parties vont à la ronde dont l'URL ou l'identifiant est donné
(une diffusion alimentée par envois, pas par une URL source). Les parties en cours sont envoyées toutes les 8 secondes
avec leurs pendules, et chaque partie terminée une fois ; une ronde contient 64 parties, après quoi la ronde suivante
de la même diffusion est créée. Seules les parties terminées après son activation sont envoyées ; `lichess.json` dans
le dossier du tournoi se souvient de l'endroit où elles sont allées, si bien qu'un tournoi repris continue la même
diffusion. lichess ne garde que les pendules de ce qu'on lui envoie (il abandonne les `[%eval]` et le texte des
commentaires des coups) : sa barre d'évaluation est la sienne, et les évaluations des moteurs n'y apparaissent pas.

La connexion utilise Mbed TLS, intégré au programme, et ne fait confiance qu'aux certificats racines qui signent
lichess.org (Google Trust Services) et à ceux de Let's Encrypt. Elle passe par le proxy HTTP que nomme `HTTPS_PROXY`
(ou `ALL_PROXY`), sous la forme `http://proxy.example:3128` ou `http://user:password@proxy.example:3128`, sauf pour
les hôtes de `NO_PROXY` ; sous Windows aussi, le proxy vient de ces variables, pas des réglages du système. Les proxys
SOCKS ne sont pas pris en charge.

### Un match par équipes à l'antenne
<!-- en: a-team-match-on-the-air fd32027f -->

Un match par équipes montre le score des équipes partout : dans l'en-tête de la page web et au-dessus de son
classement (avec le score % de l'équipe A, l'Elo ± et la LOS), dans `standings.html` (les équipes, puis les moteurs de
chaque équipe face à ceux de l'autre équipe), dans `tournament.json` et `state.json` (`teams`, `team_elo`,
`team_elo_error`, `team_los`), et dans le tableau croisé TLCS, sous forme d'une ligne sous le tableau (« Lynx 17½ -
18½ Otter · 48.6 % · Elo -9.7 ± 31.2 »). Ses parties portent les balises `WhiteTeam` et `BlackTeam` dans les fichiers
PGN, que les exports gardent.

### Mettre la page web sur internet
<!-- en: putting-the-web-page-on-the-internet b19f02f8 -->

La page web peut être suivie de n'importe où sans ouvrir de port dans le routeur : coche *Publier sur internet* sous
*Page web* dans l'onglet *Diffusion*. Pendant qu'un tournoi tourne, fastchess-gui ouvre un tunnel SSH (libssh2,
intégré au programme) vers un service de tunnel, qui donne à la page une adresse https publique ; rien n'a besoin
d'être installé.

- *localhost.run* (le défaut) : gratuit, sans compte. Ses adresses changent de temps en temps, et son débit est
  limité ; la page et son état sont envoyés compressés, si bien que cela compte peu. Sa clé d'hôte est connue de
  fastchess-gui : une connexion à quoi que ce soit d'autre est refusée.
- *Un autre serveur SSH* : son hôte, son port et son utilisateur, et le port qu'il ouvre pour la page (comme le
  demanderait `ssh -R REMOTE_PORT:localhost:PORT user@host` ; 0 le laisse choisir). Seuls les serveurs qui laissent
  entrer l'utilisateur sans clé ni mot de passe peuvent être utilisés (authentification « none »), et ils doivent
  proposer curve25519-sha256, une clé d'hôte RSA de 2048 à 4096 bits, et AES (GCM ou CTR). La clé du serveur est
  approuvée la première fois et gardée dans le profil (`known_hosts.json`) ; si elle change, le tunnel s'arrête et le
  dit, et *Faire confiance à la nouvelle clé* sert quand le propriétaire du serveur confirme qu'elle a changé.

L'onglet montre l'état (connexion, à l'antenne avec le nombre de connexions, reconnexion, ou une erreur) et, une fois
connue, l'adresse publique avec *Copier*, *Ouvrir* (pour voir la page comme les spectateurs) et un QR code pour un
téléphone (clique dessus pour l'agrandir). Le tunnel se reconnecte de lui-même après un échec, en attendant un peu plus
longtemps chaque fois, et montre la nouvelle adresse si elle a changé. Il s'arrête avec la page web ou avec le tournoi.

D'autres tunnels fonctionnent aussi, lancés à part : avec celui de Cloudflare, par exemple, active la page web (port
8080 ici) et lance `cloudflared tunnel --url http://localhost:8080` ; il affiche une adresse comme
`https://some-words.trycloudflare.com`. ngrok, tailscale funnel ou un reverse proxy comme nginx, également sous un
chemin à lui (`location /live/ { proxy_pass http://localhost:8080/; }`), fonctionnent de la même façon.

La page est en lecture seule : elle ne répond qu'aux requêtes GET pour la page, son état et le fichier PGN, jamais à
un fichier qu'une requête nomme, et rien en elle ne change le tournoi. Chaque requête reçoit une réponse séparée, 32 à
la fois au plus, et doit arriver en moins d'une seconde et recevoir sa réponse en moins de dix, si bien qu'un
visiteur lent ou muet ne bloque personne d'autre ; derrière le tunnel, la connexion de chaque spectateur compte pour
l'une d'elles : 40 sont relayées à la fois, autant attendent leur tour, et les suivantes sont refusées ; un spectateur
qui envoie plus que ce que la page accepte est déconnecté, lui seul. Deux téléchargements du fichier PGN se font à la
fois, et il est demandé au suivant de revenir un instant plus tard. L'état n'est jamais mis en cache, par le tunnel
non plus, si bien que les spectateurs voient les parties au fur et à mesure ; la page est gardée par le navigateur et
revérifiée à chaque chargement. Les spectateurs affichés dans l'onglet *Diffusion* sont comptés d'après les requêtes
que font les pages, si bien que le compte est juste derrière un tunnel aussi.

Les spectateurs voient le nom du tournoi, les noms des moteurs, leurs classements, recherches et anomalies, les
ouvertures et le fichier PGN des parties jouées. Les dossiers et les fichiers des moteurs restent privés. Sans
interface :

```
fastchess-gui --run FOLDER --tunnel                      # localhost.run, the address printed on the console
fastchess-gui --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Où les spectateurs discutent
<!-- en: where-the-viewers-talk eb3ef90c -->

La page n'a pas de discussion à elle ; elle renvoie là où la discussion a lieu. Un *Lien de discussion* sous *Page web*
(une invitation Discord, une chaîne Twitch, un fil de forum ; https seulement, tout le reste est refusé avec un
message sous le champ) met dans l'en-tête de la page un bouton *Chat* qui l'ouvre dans un nouvel onglet. Pendant que
les parties sont envoyées à lichess, la page renvoie aussi à la ronde lichess, qui a sa propre discussion.

Un *ID du serveur Discord* ajoute un onglet *Discord* à côté d'*Event*, avec le widget de serveur de Discord lui-même
dans le thème clair ou sombre de la page : le nom du serveur, les membres en ligne, ses salons vocaux et un bouton
*Join*, pas les messages. Le propriétaire du serveur l'active dans Paramètres du serveur, Widget (« Activer le widget
du serveur »), qui montre aussi l'ID du serveur à copier (17 à 20 chiffres) ; avec le mode développeur activé, un clic
droit sur l'icône du serveur donne aussi *Copier l'identifiant du serveur*. Le widget n'est chargé depuis discord.com
qu'une fois qu'un spectateur ouvre l'onglet, et Discord voit alors la visite de ce spectateur (son adresse, son
navigateur, sa connexion Discord s'il en a une) ; sans ID, la page ne contacte personne d'autre que fastchess-gui. Les
liens de discussion et de lichess n'envoient rien tant qu'un spectateur ne clique pas dessus, et alors pas de
référent. Un fichier de configuration lancé sans interface (`--run SETUP.json`) prend les deux dans `web_chat_link` et
`web_discord_id`.

## Mode sans interface
<!-- en: headless-mode 3471fbb3 -->

```
fastchess-gui --run TOURNAMENT_FOLDER     # resume a tournament
fastchess-gui --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

Aucune fenêtre n'est ouverte ; la version, puis la progression, sont affichées dans la console, avec les essais d'un
moteur qui ne démarre pas, le moteur mis de côté (avec son journal moteur) ou revenu, comme dans la fenêtre, et le
classement toutes les 20 parties et à la fin (un match par équipes : les scores des équipes, puis les moteurs de chaque
équipe face à ceux de l'autre), et, à la fin d'un tournoi, la ligne des copies du classement (parties, nulles,
cadence, livre et événement). **Ctrl+C** arrête après les parties en cours et le tournoi peut être repris plus
tard, depuis la ligne de commande ou l'interface graphique. Codes de sortie : 0 terminé, 1 erreur pendant le jeu,
2 rien à lancer, 3 arrêté avant la fin (par Ctrl+C, ou parce qu'il ne reste rien à jouer sans un moteur qui ne démarre
pas). Le
budget de mémoire et les GPU des **Réglages** du profil s'y appliquent aussi.

`--detach` le joue plutôt en arrière-plan : la commande rend la main aussitôt, et le tournoi continue sans terminal ;
la fenêtre le montre (elle s'y connecte) quand elle s'ouvre avec le même profil, et `--attach` dialogue avec lui :

```
fastchess-gui --run FOLDER --detach         # in the background, on a local socket
fastchess-gui --attach FOLDER               # its control channel on the standard input and output
```

La fenêtre pilote chaque tournoi de la même façon : `--run FOLDER --control socket` (ou `--control stdio`) le joue sans
rien afficher, en échangeant des lignes JSON avec la fenêtre : les événements du tournoi (son état, chaque ligne du
journal, les parties en cours coup par coup, chaque partie enregistrée, son journal, sa fin) et les commandes de la
fenêtre (pause, reprise, arrêt, modifications, la fin d'une partie, sa part de la machine avec
`--allowance THREADS:MEMORY_MB:GPUS:ONE`). La première ligne dans chaque sens est un bonjour,
`{"hello": {"protocol": 1, ...}}` ; un script passant par `--attach` reçoit le bonjour et les événements du tournoi, et
peut envoyer des commandes comme `{"c": "pause"}`. Une seule fenêtre (ou un seul script) à la fois dialogue avec un
tournoi ; `--attach` le lâche quand son entrée standard se termine ou que sa sortie n'est plus lue, si bien qu'un
script garde son entrée ouverte pendant qu'il lit les réponses.

Le tournoi peut être diffusé en même temps, comme depuis l'onglet *Diffusion* :

```
fastchess-gui --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
fastchess-gui --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
fastchess-gui --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
fastchess-gui --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
fastchess-gui --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
fastchess-gui --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` prend le jeton dans la variable d'environnement `FCG_LICHESS_TOKEN`, ou celui du profil.

Les options peuvent être combinées. Un fichier de configuration enregistré avec la diffusion activée diffuse de la même
façon sans elles. Pour des spectateurs hors du réseau local, ouvre les ports dans le routeur : TLCV et node-tlcv se
connectent au port TLCS. Pour la page web, un tunnel évite d'ouvrir un port (voir
[Mettre la page web sur internet](#mettre-la-page-web-sur-internet)).

## Machines
<!-- en: machines 4d8fbfef -->

D'autres ordinateurs sur lesquels faire tourner des tournois : une machine de test, un serveur, un second PC.
**Réglages** > *Machines* les liste. Une machine est jointe par SSH : rien n'a besoin d'y être ouvert à part son
serveur SSH, et le programme ne garde rien de secret.

### Ajouter une machine
<!-- en: adding-a-machine f1f15823 -->

*Ajouter une machine*, puis sur sa carte :

- *Nom*, *Hôte* (son nom sur le réseau, ou son adresse), *Port* (22) et *Utilisateur*, le compte sous lequel ses
  tournois tournent ;
- *Connexion avec* : l'*Agent SSH* (ssh-agent sous Linux et macOS, Pageant ou le service *OpenSSH Authentication
  Agent* sous Windows ; ajoutes-y ta clé avec `ssh-add`), ou un *Fichier de clé*, RSA ou Ed25519
  (`~/.ssh/id_ed25519`). La phrase de passe d'un fichier de clé est demandée une fois par session : elle reste en
  mémoire et n'est jamais écrite nulle part. La machine doit connaître la clé : sa moitié publique (`id_ed25519.pub`)
  va dans `~/.ssh/authorized_keys` sur la machine (`ssh-copy-id user@host` l'y met), ou, pour un administrateur d'une
  machine Windows, dans `C:\ProgramData\ssh\administrators_authorized_keys` ;
- *Dossier là-bas* : le dossier de fastchess-gui sur la machine, pour le programme, ses tournois, ses livres et ses
  tables, relatif au dossier personnel de l'utilisateur (`fastchess-gui` par défaut) ou absolu (`/srv/fcg`, `D:\fcg`) ;
- *Budget* : les cœurs et la mémoire que les tournois de la machine peuvent utiliser à la fois ; 0 prend ce que le test
  a trouvé, tous les threads sauf un et 80 % de la mémoire.

*Tester la connexion* se connecte, puis indique le système de la machine, ses threads, sa mémoire, l'espace libre là où
se trouve son dossier, et si fastchess-gui y est installé, dans quelle version. La première connexion montre la clé
d'hôte de la machine (son empreinte SHA256) et lui fait confiance dès lors (`known_hosts.json` dans le profil, celui du
tunnel de diffusion aussi). Quand la clé change plus tard, rien n'est envoyé et la carte le dit, avec *Faire confiance
à la nouvelle clé* pour une machine qui a été réinstallée. Chaque échec nomme la machine et ce qu'il faut faire : l'hôte
qui ne peut pas être joint, une clé que la machine refuse (et où va sa moitié publique), une phrase de passe qui n'est
pas la bonne, aucun agent en cours.

*Installer fastchess-gui* (*Mettre à jour fastchess-gui* une fois qu'il y est) copie le programme dans le dossier de la
machine par SFTP, puis vérifie qu'il y démarre (`fastchess-gui --version`). Il copie ce programme-ci quand la machine a
le même système (Linux x86-64 vers Linux x86-64) ; pour un autre, une machine Windows depuis Linux par exemple,
télécharge la version pour ce système et choisis son programme sur la carte (*fastchess-gui pour Windows x86_64*). Une
copie coupée par le réseau reprend là où elle s'est arrêtée.

### Moteurs sur une machine
<!-- en: engines-on-a-machine 4b00e462 -->

*Moteurs sur cette machine* : les moteurs de la bibliothèque que la machine a, chacun avec son chemin là-bas
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Vérifier les moteurs* vérifie chaque chemin : le programme est là,
peut être lancé, et répond à `uci` (`xboard` pour un moteur WinBoard) en moins de 10 secondes. La marque à côté, et son
infobulle, disent ce qui a été trouvé : le nom du moteur, ou pourquoi il ne tourne pas (manquant, non exécutable, fait
pour un autre système, muet).

Un moteur absent de la machine y est copié avec *Copier…* sur sa ligne (ou *Copier \<moteur\> sur \<machine\>* dans la
palette de commandes, ou le bouton d'un lancement refusé parce qu'un moteur n'a pas de chemin là-bas). Le panneau qui
s'ouvre dit d'abord ce qui part : le programme, sa taille et le système pour lequel il est compilé, lu dans ses
premiers octets (un programme Windows, un programme Linux ou macOS, universel ou non, ou un script). Un programme que
la machine ne peut pas lancer est refusé avant que quoi que ce soit soit envoyé, en disant quelle compilation choisir :
« Alpha est un programme Windows ; tc-box tourne sous Linux : choisis la compilation Linux d'Alpha ». L'architecture
compte aussi (x86-64 ou arm64) ; une compilation x86-64 sur un Mac arm64 ne tourne que sous Rosetta, et un format que
le programme ne connaît pas est copié quand même : les deux sont signalés, pas refusés. Un script ne va que sur une
machine Linux ou macOS, et seulement quand l'interpréteur de sa première ligne (`python3`, `/bin/sh`) y est.
*Choisir la compilation \<système\> de \<moteur\>…* prend un autre fichier de cet ordinateur, la compilation Windows du
moteur par exemple, le vérifie de la même façon, et le garde pour cette machine : les comparaisons et remplacements
suivants l'utilisent, pas le programme de la bibliothèque.

*Avec son dossier* copie tout le dossier du moteur à côté du programme (son dossier de travail, ou le dossier du
programme), pour un moteur qui a besoin de fichiers à côté de lui : ses réseaux, un livre. Le panneau compte d'abord
ses fichiers et leur taille, et nomme les options du moteur qui pointent à l'intérieur (`EvalFile` d'un réseau) : dans
un tournoi sur la machine, elles pointent vers la copie là-bas, et le dossier est le dossier de travail du moteur. Un
dossier de plus de 2000 fichiers n'est pas proposé (un programme dans `/usr/bin` n'est pas à copier avec son dossier),
un dossier `.git` est laissé de côté, et pour une machine Windows, un nom de fichier que Windows ne peut pas accepter
(`:`, `*`, `?`, un point final, `CON`, `NUL`...) est nommé et arrête la copie avant qu'elle commence. Les fichiers sont
copiés tels quels, fins de ligne comprises.

*Copier* envoie les fichiers par SFTP vers `engines/<engine>/` dans le dossier de la machine, en arrière-plan avec le
temps restant et *Annuler*, la copie reprenant là où elle s'est arrêtée après une coupure ; le SHA-256 de chaque fichier
est comparé à celui de cet ordinateur après la copie, et les fichiers déjà là et identiques ne sont pas renvoyés. Sous
Windows, le programme garde ou reçoit son `.exe` ; ailleurs, il est rendu exécutable (`chmod 755`). Puis le chemin du
moteur sur la machine est réglé sur la copie et vérifié comme le fait *Vérifier les moteurs*. Sur un Mac, un programme
copié depuis un autre ordinateur, s'il ne démarre pas, peut avoir besoin d'une signature ou de sortir de la quarantaine
: le message donne les deux commandes à y lancer (`codesign -s -` et `xattr -d com.apple.quarantine`) ; le programme ne
les lance pas lui-même.

La colonne *Compilation* compare le moteur de là-bas avec celui de cet ordinateur, par leur SHA-256 (*Vérifier les
moteurs* et *Comparer* le font, le programme de cet ordinateur étant haché une fois jusqu'à ce qu'il change) : *même
compilation que cet ordinateur*, ou *une autre compilation là-bas*, avec *Remplacer par celui de cet ordinateur*.
Remplacer copie le programme de cet ordinateur dans le dossier du moteur, dans le dossier de la machine, et y fait
pointer le moteur : un programme situé ailleurs sur la machine (`/opt/engines/stockfish`) n'est jamais écrasé, il
reste tel qu'il était.

### Livres et tables sur une machine
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Vérifier ce que la configuration utilise* compare le livre d'ouvertures et les livres propres des participants de la
configuration avec le dossier des livres de la machine, par taille et SHA-256 (un livre du même nom mais d'un autre
contenu n'est pas le même livre), et les fichiers Syzygy de 6 pièces au plus de la configuration avec le dossier des
tables de la machine, par nom et taille. *Envoyer les fichiers manquants* copie ce qui manque ou diffère, en
arrière-plan, avec le temps restant ; chaque livre est vérifié par son hachage une fois copié, chaque table par sa
taille, et une copie interrompue reprend. Les tables de 7 pièces ne sont pas proposées : elles occupent des
téraoctets. Un tournoi ne démarre pas sur une machine sans son livre ; des tables manquantes sont un avertissement
(l'adjudication et les moteurs s'en passent).

### Un tournoi sur une machine
<!-- en: a-tournament-on-a-machine 8f918d9a -->

Dans la configuration, *Jouer sur* (dans *Parallélisme*) choisit où le tournoi joue : *Cet ordinateur*, ou une
machine. *Lancer* l'y prépare alors : fastchess-gui doit y être installé dans cette version (sinon *Mettre à jour
fastchess-gui*), chaque moteur doit avoir son chemin sur la machine (sinon le lancement propose *Copier \<moteur\> sur
\<machine\>…*), et les livres de la configuration doivent être dans son dossier des livres (sinon le lancement dit lequel
manque, et *Envoyer les fichiers manquants* l'y met) ; des tables manquantes ne sont qu'un avertissement. Le tournoi
reçoit son dossier dans `tournaments/` du dossier de la machine, avec les moteurs, les livres et les tables à leurs
chemins là-bas, et son processus y démarre hors de la session SSH (`fastchess-gui --run <folder> --detach`) : il
joue quoi qu'il arrive à la fenêtre ou au réseau. La fenêtre le suit comme un tournoi de cet ordinateur : Direct,
Parties, le classement, la pause, l'arrêt et les modifications, par le canal de contrôle de son processus
(`fastchess-gui --attach <folder>` par SSH).

Sa carte sur la page d'accueil dit *Sur \<machine\>*, et *Connexion à \<machine\>…* pendant que la connexion est coupée : la
fenêtre se reconnecte d'elle-même, en attendant un peu plus après chaque échec, et reprend les événements là où sa copie
s'était arrêtée. Le journal et les fichiers PGN sont recopiés dans le dossier du tournoi ici au fur et à mesure qu'ils
grandissent, toutes les quelques secondes, pour que Classements, les exports et la liste de contrôle avant envoi les
lisent ; le dossier de la machine reste la référence (`machine.json` ici dit quelle machine et quel dossier). Fermer la
fenêtre le laisse jouer là-bas ; le lancement suivant le montre de nouveau. *Reprendre* sur un tournoi interrompu d'une
machine le reprend là-bas. Le budget de la machine (ses cœurs et sa mémoire) est partagé par ses propres tournois,
séparément de celui de cet ordinateur. Un tournoi joue sur une seule machine : ses parties ne sont pas réparties sur
plusieurs.

Une machine a besoin d'un serveur OpenSSH (Linux, macOS, BSD ; Windows 10 1809 et ultérieur), avec une clé d'hôte RSA
et SFTP, tous deux présents par défaut. Les commandes passent par `sh` quel que soit le shell de l'utilisateur (fish ou
csh conviennent), et par cmd sous Windows, le shell par défaut de son serveur OpenSSH.

### Moteurs joués depuis cet ordinateur
<!-- en: engines-played-from-this-computer 8c14dc16 -->

Un moteur de la bibliothèque peut tourner sur une machine pendant que le tournoi joue ici : un moteur GPU sur la machine
à GPU, un moteur compilé pour un autre système, une machine réservée à un moteur. Sur sa carte, *Tourne sur* choisit
*Cet ordinateur* ou une machine ; *Programme là-bas* et *Dossier de travail là-bas* sont alors des chemins sur la
machine (vide : le dossier du programme là-bas), et les boutons *…*, ceux des options qui nomment un fichier compris,
parcourent les fichiers de la machine. Dès qu'il y a des machines, *Ajouter…* propose *Un programme sur \<machine\>…*,
*Importer…* propose *Tous les moteurs d'un dossier sur \<machine\>…*, et *Ajouter un moteur…* de la machine propose *Un
programme là-bas…*. Le moteur est lu, vérifié, et joué dans la console, le test rapide et les tournois comme l'est un
moteur de cet ordinateur. Son chemin est aussi celui de la liste des moteurs de la machine, et le SHA-256 de son
programme là-bas dit quand il a été recompilé (Δ, relu).

La machine doit avoir été testée une fois (*Tester la connexion*), pour que sa clé d'hôte soit connue : un moteur ne se
connecte jamais à une machine dont la clé n'a pas été montrée et acceptée. La phrase de passe de la clé, si elle en a
une, est demandée une fois par session, avant la première lecture ou le lancement, et gardée en mémoire : le processus
du tournoi la reçoit de la fenêtre et la garde, pour que le tournoi continue de jouer une fois la fenêtre fermée.
`--run` dans un terminal ne la demande pas : là, utilise l'agent SSH.

Chaque moteur est lancé par un relais, `fastchess-gui --remote-engine <key>`, que sert le processus qui le fait jouer :
une connexion SSH à la machine pour dix moteurs au plus (le `MaxSessions` d'un serveur OpenSSH), chaque moteur étant
lancé depuis son dossier là-bas. Un moteur dont le relais disparaît est arrêté là-bas aussi, par son identifiant de
processus, quand il ne s'arrête pas de lui-même (sur une machine Linux ou macOS ; sur une machine Windows, son canal est
fermé).

*Vérifier* mesure l'aller-retour jusqu'à la machine, la médiane de quelques `isready`. La pendule le compte : le résumé
de la configuration avertit quand il prend plus de 5 % de l'incrément, ou du temps dont dispose un coup sans incrément,
avec *Mettre une marge de … ms* (deux fois l'aller-retour). Une machine jamais testée arrête le lancement, de même qu'un
tournoi qui joue sur une autre machine que celle du moteur (un tournoi qui joue sur la machine du moteur l'y fait jouer
comme son propre moteur).

Pendant que le tournoi joue, la page d'accueil a une ligne *Machines* sous les cœurs : chaque machine, connectée et ce
qui y joue, ou depuis quand elle ne répond pas. Une machine qui ne répond pas fait une seule entrée sous *À traiter* :
les parties de ses moteurs sont mises de côté et réessayées, les autres continuent, avec *Réessayer* et *Tester
\<machine\>*. Une partie qu'un moteur a perdue parce que sa connexion s'est rompue (sa fin étant une déconnexion) est
annulée et rejouée, écrite dans le journal avec sa raison (`connection to its machine lost: <machine>`) ; *Santé* la
compte à part, pas comme un plantage du moteur. Une machine coupée plus de trois fois en une heure voit ses moteurs mis
de côté. Les cœurs, la mémoire et les GPU de cet ordinateur ne comptent pas les moteurs sur des machines, seulement
leurs relais ; le budget de la machine, que partagent ses propres tournois, ne les compte pas encore.

### Une machine Windows
<!-- en: a-windows-machine 58ae029a -->

Windows 10 (1809 et ultérieur) et Windows 11 contiennent un serveur OpenSSH, non installé par défaut. Dans PowerShell,
en administrateur :

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

L'installation ouvre le port 22 dans le pare-feu. Mets ensuite ta clé publique dans
`C:\Users\<user>\.ssh\authorized_keys`, ou, pour un compte du groupe Administrateurs, dans
`C:\ProgramData\ssh\administrators_authorized_keys`, que seuls les Administrateurs et SYSTEM peuvent lire (les groupes
sont désignés par leurs SID, les mêmes dans toutes les langues de Windows) :

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Garde cmd comme shell du serveur : fastchess-gui parle cmd, pas PowerShell (si une valeur `DefaultShell` a été créée
sous `HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, supprime-la). Sur la carte de la machine, choisis le `fastchess-gui.exe` de
la version Windows, puis *Installer fastchess-gui*.

## Fichiers
<!-- en: files 837f3661 -->

Tout se trouve dans le dossier du profil : `~/.local/share/fastchess/fastchess-gui/` sous Linux,
`%APPDATA%\fastchess\fastchess-gui\` sous Windows, `~/Library/Application Support/fastchess/fastchess-gui/` sous macOS.
Les fichiers que le programme crée ne sont pas modifiables par les autres utilisateurs, et `settings.json` n'est
lisible que par son propriétaire dès qu'il contient un jeton lichess. **Réglages** montre le dossier du profil, avec
*Ouvrir le dossier*.

### Mode portable
<!-- en: portable-mode 29ddbe3b -->

Avec un fichier nommé `portable` (vide, sans extension) à côté du programme (à côté de `fastchess-gui.exe` ou de
`fastchess-gui`, à côté de `fastchess-gui.app` sous macOS), le profil est à la place le dossier `data` à côté du
programme, et le dossier des tournois par défaut est `data/runs` : rien n'est écrit ailleurs, sauf dans les dossiers
choisis pour les tournois, les exports et autres. Les chemins de ce qui se trouve dans le dossier du programme
(moteurs, livres, tables, suites de tests, fichiers PGN des classements et de la configuration, dossiers choisis) sont
gardés relatifs à lui dans les fichiers du profil, si bien que le dossier entier peut être déplacé, copié sur une
autre machine ou lancé depuis une clé USB, quelle que soit sa lettre de lecteur, et ses moteurs sont retrouvés.
**Réglages** affiche *Mode portable* à côté du dossier du profil. Pour reprendre un profil existant, copie ses
fichiers dans `data`. `--profile` choisit toujours un autre dossier, hors du mode portable. Un tournoi garde les
chemins des moteurs du moment de sa création : reprends un tournoi inachevé avant de déplacer le dossier. Sous macOS,
retire d'abord la quarantaine (voir le README) : sinon macOS lance l'application depuis un emplacement temporaire, où
le fichier `portable` n'est pas.

### Le dossier du profil
<!-- en: the-profile-folder 91838f38 -->

- `engines.json` : la bibliothèque de moteurs, et `engines.json.bak` sa version précédente, lue si la première est
  perdue ;
- `setup.json` : la dernière configuration ; `profiles.json` : tes profils des sections de la configuration ;
  `templates/*.json` : les modèles enregistrés ; `queue.json` : la file d'attente ;
- `book-cache/` : les livres PGN transformés en livres Polyglot pour les livres propres des participants (supprimés
  après 30 jours sans usage) ;
- `layout.ini` : la disposition de la fenêtre et les panneaux de l'espace de travail ; `layouts.json` : les
  dispositions enregistrées par nom ; `fastchess-gui.log` : le journal de la dernière session ;
- `suite.json` : la dernière configuration de la page Suites ; `suites/<suite>-<date>.json` : les passages de suites
  de tests terminés ;
- `board.json` : les réglages de l'analyse et des parties de la page Échiquier ; `analyses/*.json` : les parties
  analysées coup par coup, un fichier chacune ; `games/*.pgn` : les parties jouées depuis la page Échiquier ;
- `settings.json` : le dossier des tournois, quand un autre a été choisi dans **Réglages**, *Rendu logiciel*, le
  dossier de *Tous les moteurs d'un dossier…*, le testeur et le lieu d'*Exporter pour une liste de classement…*, le
  jeton lichess, la taille et la position de la fenêtre, la largeur du panneau du guide, la carte des premiers pas
  masquée, la recherche de nouvelle version (voulue ou non, sa dernière date, la version trouvée, l'avis masqué),
  l'apparence (couleurs de l'échiquier, pièces, sons des coups), et le budget de la machine (cœurs, mémoire et GPU pour
  les tournois, *Lancer quand des cœurs sont libres*) ;
- `logos/` : les logos des moteurs, copies des images choisies sur leurs fiches ;
- `machines.json` : les machines distantes des **Réglages** (leur adresse, leur dossier, leurs budgets, les chemins
  des moteurs et ce que le dernier test a trouvé ; pas de phrase de passe), et `known_hosts.json` les clés d'hôte
  approuvées, des machines et du tunnel ;
- `running.json` : les tournois qui jouent en ce moment, avec leurs poids, chacun retiré quand il s'arrête ; trouvé au
  lancement, il dit quoi montrer de nouveau (un tournoi qui a continué de jouer sans la fenêtre) ou reprendre ;
- `runs/` : les tournois, à moins qu'un autre dossier ait été choisi (voir
  [Le dossier d'un tournoi](#le-dossier-dun-tournoi)).

### Le dossier d'un tournoi
<!-- en: a-tournaments-folder 76959723 -->

`runs/<date>_<A>_vs_<B>/` pour un match, `runs/<date>_<name>/` pour un tournoi (dans `runs/` ou le dossier choisi), un
dossier chacun, qui peut être déplacé, copié, zippé et envoyé :

- `tournament.json` : le format, les participants (moteurs copiés avec leurs options) et les conditions ;
- `setup.json` : la configuration de la page *Nouveau tournoi* dont il est parti, que lisent *Cloner* et *Relancer
  avec…* (voir [File d'attente et relance](#file-dattente-et-relance)) ; un tournoi lancé par une version plus
  ancienne n'en a pas ;
- `journal.jsonl` : tout ce qui s'est passé depuis, une ligne par événement (paires de parties commencées, parties
  terminées, modifications, appariements des suisses et des KO) ; avec les fichiers PGN, c'est ce que lit une reprise.
  Une partie est sur le disque avant sa ligne, si bien qu'une coupure de courant fait perdre au plus les parties en
  cours. Le texte d'une partie vers lequel aucune ligne ne pointe (une partie abandonnée par une modification pendant
  qu'elle était enregistrée, ou laissée par un plantage) est remplacé par des lignes vides, si bien que les autres
  visionneuses PGN ne montrent que les parties du tournoi ;
- `tournament.lock` : tenu par le programme qui joue le tournoi ; un autre programme qui ouvre le dossier ne fait que
  le montrer ;
- `<name>.pgn` : commentaires des coups dans le style cutechess / fastchess, `{+0.35/18 1.234s}`, avec le score du
  point de vue du camp qui a joué ; les parties dans l'ordre où elles se sont terminées (le journal retrouve chacune par
  sa place dans le fichier ; *Exporter tout…* dans l'onglet *Parties*, et le fichier d'*Ajouter aussi chaque partie à*,
  suivent le calendrier) ;
- `<name>-chessbase.pgn` : `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, avec l'évaluation du point de
  vue des Blancs, telle que la lisent ChessBase / Fritz et Lichess ;
- `<name>` est le nom du tournoi au moment de sa création (le renommer plus tard garde les fichiers), avec les
  caractères que Windows n'autorise pas remplacés par `_` et coupé à 64 octets ; `tournament.json` enregistre les deux
  noms. Un tournoi sans nom, un tournoi plus ancien, ou un match importé de la 0.2.x a `games.pgn` et
  `games-chessbase.pgn` ;
- `lichess.json` : la diffusion et la ronde lichess où sont allées ses parties (pas de jeton) ;
- `processor.tsv` : le temps processeur qu'ont utilisé les moteurs de chaque partie, pour l'*Utilisation du
  processeur* d'un tournoi repris ;
- `fastchess-gui.log` : le journal des processus qui l'ont joué (chaque tournoi joue dans son propre processus) ;
  `control.json`, pendant qu'un processus le joue : où est son socket local (`control.sock` dans le dossier, ou dans
  le dossier d'exécution de l'utilisateur quand ce chemin est trop long ; un tube nommé sous Windows), et quel
  processus écoute ;
- `logs/game-N.log` : chaque ligne échangée avec les deux moteurs pendant la partie N (le numéro affiché dans l'onglet
  *Parties*), avec l'heure à laquelle elle a été envoyée ou lue (`W>` envoyée au moteur des Blancs, `W<` lue sur sa
  sortie, `W!` lue sur sa sortie d'erreur : assertions, messages de plantage), pour les parties perdues au temps, par un
  plantage, un blocage ou un coup illégal (ou toutes les parties, si on le demande). Les 300 premières lignes de chaque
  moteur et les 5000 dernières lignes de la partie sont gardées, dans l'ordre où elles sont venues. C'est ce dont
  l'auteur d'un moteur a besoin pour reproduire un bug. Pour un moteur WinBoard, il montre le côté UCI ;
  `FCG_WB2UCI_LOG` donne le côté WinBoard ;
- `logs/start-<engine>.log` : le dernier essai raté de lancer un moteur qui ne démarre pas : son programme, l'erreur,
  et les lignes échangées avec les moteurs de cette partie jusque-là (voir
  [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas)).

Un dossier reçu de quelqu'un d'autre peut être ouvert sans risque : le fichier d'*Ajouter aussi chaque partie à* doit
être un fichier `.pgn` (un autre nom est ignoré, avec un avertissement), et un dossier dont le journal, les fichiers
PGN ou le fichier de verrou sont des liens symboliques n'est pas ouvert en écriture, si bien qu'il ne peut pas faire
écrire le programme en dehors de lui. Le reprendre lance les moteurs que nomme son `tournament.json`, comme le fait
tout tournoi.

Les matchs joués avec la version 0.2 apparaissent sur la page d'accueil : *Les importer* en fait des tournois terminés.

## Ligne de commande
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

- `--profile DIR` utilise un autre dossier de profil ;
- `--autostart` lance la configuration enregistrée au démarrage (plutôt que de reprendre le tournoi qui était en
  cours, voir [Pause, arrêt et reprise](#pause-arrêt-et-reprise)) ;
- `--size WxH` ouvre la fenêtre à cette taille, centrée, plutôt que comme elle a été laissée, et ne touche pas à la
  taille et à la position gardées pour le lancement suivant ;
- `--software` fait dessiner la fenêtre par le processeur (le rendu logiciel de SDL) plutôt que par OpenGL, comme le
  fait *Rendu logiciel* dans **Réglages**, pour ce lancement (voir [Rendu de la fenêtre](#rendu-de-la-fenêtre)) ;
- `--shot` enregistre une capture d'écran de la fenêtre après le nombre de secondes donné (5 par défaut), avec WINDOW
  mis au premier plan, et quitte une fois toutes les captures faites. Il peut être répété. WINDOW est une page
  (`home`, `setup`, `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`,
  `console`, `settings`) ou un onglet (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`).
  La souris est ignorée dans ce mode et, sous Linux, la fenêtre est rendue hors écran (aucune fenêtre n'apparaît ;
  règle `SDL_VIDEODRIVER` pour passer outre) ; la fenêtre s'ouvre à la taille par défaut, ou à `--size`, et la taille
  et la position gardées ne sont pas touchées ;
- `--focus WINDOW` ouvre cette page ou cet onglet au lancement, ou : `engines-card` (la fiche du premier moteur),
  `engines-refresh` (*Actualiser : les nouveaux moteurs du dossier*), `engines-ratings=FILE` (l'aperçu de cette liste
  de classement), `setup-picker` et `setup-suggest` (le filtre de la bibliothèque avec le focus, ou les adversaires
  suggérés sélectionnés), `setup-open=ROW` et `setup-profiles=ROW` (une ligne de la configuration ouverte, ou sa liste
  de profils : `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (le livre
  propre du premier participant), `submission-export` (*Exporter pour une liste de classement…*), `ratings-players`
  (la fenêtre *Joueurs PGN…*), `clone=DIR` (la page *Nouveau tournoi* remplie par *Cloner* du dossier de ce
  tournoi), `palette[=TEXT]` (la palette de commandes, avec TEXT tapé dedans), `game-menu=N` (le
  menu de la partie N dans l'onglet *Parties*, un résultat choisi), `end-menu=N` (*Terminer avec un résultat* de la
  partie de l'emplacement N, à partir de 1), `big-screen[=N]` (le grand écran, de la partie de l'emplacement N),
  `live-back=N` (la partie suivie dans *Direct*, reculée de N demi-coups), `engines-console` (la console du premier
  moteur), `board-analyse` (la page Échiquier, analysée), `board=FILE` (un fichier PGN, FEN ou EPD sur la page
  Échiquier ; `board-analyse=FILE` analysé, `board-review=FILE` sa première partie analysée coup par coup),
  `board-edit` (l'éditeur de position), `board-play` et `board-play-start` (une partie entre les deux premiers
  moteurs, lancée), `machines` (la carte de la première machine distante dans **Réglages** ; `machines-test` testée,
  `machines-files` les livres et les tables de la configuration comparés avec elle), `guide[=ID]` (le panneau du guide,
  à la section ID), `guide-search=TEXT` (le panneau du guide, en train de chercher TEXT) ;
- `--select-game N` ouvre la partie N (dans l'ordre de fin) dans l'onglet *Parties* dès qu'elle existe ;
- `--quit-after SECONDS` arrête le tournoi et quitte ;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance` : voir [Mode sans interface](#mode-sans-interface) ;
- `--version` affiche la version et le protocole du canal par lequel la fenêtre suit un tournoi distant (voir
  [Machines](#machines)), et quitte.

## Dépannage
<!-- en: troubleshooting e8c4882d -->

### Le programme ne démarre pas
<!-- en: the-program-does-not-start dc0a0128 -->

- **Windows dit que le programme n'est pas sûr, ou un antivirus le supprime.** Le programme n'est pas signé, si bien
  que Windows SmartScreen et certains antivirus s'en méfient. *Informations complémentaires*, *Exécuter quand même*
  le laisse démarrer ; on peut dire à un antivirus d'autoriser son dossier. Les versions sont compilées par GitHub
  Actions à partir des sources publiques.
- **macOS refuse de l'ouvrir.** Il n'est pas signé par un compte de développeur Apple : Contrôle-clic (clic droit)
  dessus, Ouvrir, puis Ouvrir de nouveau ; sur macOS 15 et ultérieur, essaie de l'ouvrir une fois, puis Réglages
  Système, Confidentialité et sécurité, Ouvrir quand même. Ou lance `xattr -dr com.apple.quarantine` sur le dossier
  décompressé.
- **Un message dit qu'aucune manière de dessiner la fenêtre ne fonctionne, ou la fenêtre reste noire.** Le pilote
  graphique n'a pas OpenGL 3.2 ou échoue : lance avec `--software` (le processeur dessine la fenêtre), et si cela
  fonctionne, coche *Rendu logiciel* dans **Réglages**. Sous Wayland sans pilote graphique, lance avec
  `SDL_VIDEODRIVER=x11`. Voir [Rendu de la fenêtre](#rendu-de-la-fenêtre).
- **Le texte chinois s'affiche en carrés.** Le chinois a besoin d'une police du système : Microsoft YaHei sous
  Windows, Noto Sans CJK ou WenQuanYi sous Linux (installe `fonts-noto-cjk` ou un équivalent).

### Un moteur ne démarre pas
<!-- en: an-engine-does-not-start 202a67d4 -->

- **Sa vérification échoue, ou son tournoi dit qu'il ne démarre pas.** Lis l'erreur sous le moteur, puis son journal
  moteur (*Journal moteur*, ou `logs/start-<engine>.log` dans le dossier du tournoi) : la sortie d'erreur du moteur
  nomme souvent le fichier manquant. Les causes habituelles : le programme a été déplacé (*Localiser…*), un réseau ou
  une bibliothèque introuvable (règle le dossier de travail, ou `EvalFile`, sur sa fiche), un programme compilé pour un
  autre processeur (une compilation qui utilise l'AVX-512 sur une machine qui ne l'a pas), un démarrage lent (augmente
  le *Délai de démarrage* sur sa fiche). Voir [Un moteur qui ne démarre pas](#un-moteur-qui-ne-démarre-pas).
- **Un moteur WinBoard ne part pas des positions du livre.** Un moteur du protocole 1 sans `setboard` ne peut partir
  que de la position initiale : donne-lui un livre PGN (des coups), pas un livre EPD. Voir
  [Moteurs WinBoard](#moteurs-winboard).
- **Le tournoi refuse de démarrer : un moteur ne joue pas le Chess960.** Chaque moteur doit avoir l'option
  `UCI_Chess960` (UCI) ou la variante `fischerandom` (WinBoard) ; le résumé nomme ceux qui ne l'ont pas.

### Des parties sont perdues ou semblent fausses
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Un moteur perd au temps tôt dans ses parties.** Il est plus souvent mal réglé qu'il ne joue mal : trop de threads
  pour les cœurs dont disposent les parties, un hash trop grand pour être alloué à temps, un réseau chargé à chaque
  partie, un démarrage lent. Baisse ses `Threads` et son `Hash`, décoche *Relancer le moteur à chaque partie*, augmente
  le *Délai de démarrage* ; la carte *Santé* et la colonne *TF* de la vue d'ensemble montrent les pertes au temps.
- **Les moteurs semblent plus lents qu'ils ne devraient.** Trop de parties à la fois : laisse le *Parallélisme* en
  automatique, et garde parallélisme × threads dans la limite des cœurs physiques. *Utilisation du processeur*, dans la
  vue d'ensemble, nomme un moteur qui utilise plus de cœurs que ses `Threads`, ou réfléchit sur le temps de son
  adversaire.
- **Le résumé avertit que la mémoire ne suffira pas.** Les tables de hachage des parties ne tiendraient pas dans la
  mémoire libre : baisse le `Hash` (dans *Réglages des moteurs*, ou celui du moteur qu'il nomme) ou le parallélisme.
- **Des fichiers de tables seraient manquants ou tronqués.** Un téléchargement s'est arrêté à mi-chemin, ou le dossier
  ne contient que des tables plus petites : télécharge de nouveau les fichiers nommés dans l'infobulle, ou ramène
  l'adjudication aux pièces trouvées (*Descendre à 5 pièces*).
- **Un livre contiendrait des positions illégales ou des doublons.** fastchess les écarte ; *Enregistrer une copie
  nettoyée…* sur la page Livres écrit un livre sans eux. Voir [Vérifier un livre](#vérifier-un-livre).
- **Une partie a été mal tranchée.** Clic droit dessus dans *Parties* ou *Calendrier* : la rejouer, l'annuler, ou fixer
  son résultat, avec la raison. Voir [Modifier un tournoi en cours](#modifier-un-tournoi-en-cours).

### Problèmes de diffusion
<!-- en: broadcasting 4a2e6916 -->

- **TLCV ou une autre visionneuse ne se connecte pas.** Lance le tournoi (et son serveur TLCS) avant la visionneuse :
  elle ne se reconnecte pas d'elle-même. Ouvre le port UDP dans le routeur pour les spectateurs hors du réseau local.
- **La page web n'est pas joignable de l'extérieur.** Coche *Publier sur internet* pour un tunnel (voir
  [Mettre la page web sur internet](#mettre-la-page-web-sur-internet)), ou ouvre le port TCP dans le routeur.
- **lichess refuse le jeton.** Le jeton a besoin de la portée `study:write` : crées-en un nouveau avec *Créer un jeton
  sur lichess…*. Derrière un proxy, règle `HTTPS_PROXY`.

### Autre chose
<!-- en: anything-else ce59c083 -->

*Signaler un problème…* dans **Réglages** ou sur la page **Journal** ouvre un ticket GitHub déjà rempli avec la
version, le système et les dernières lignes du journal (voir [Signaler un problème](#signaler-un-problème)).

## Glossaire
<!-- en: glossary 0386d0d9 -->

- **Adjudication** : terminer une partie avant le mat quand son résultat est clair : une nulle quand les deux scores
  restent proches de 0, une défaite quand ils restent très en dessous, ou ce que disent les tables. Voir
  [Adjudication](#adjudication).
- **BayesElo, DrawElo** : un modèle de classement avec un paramètre à part pour les nulles (DrawElo), tiré du
  programme de Rémi Coulom.
- **Buchholz, Sonneborn-Berger** : départages : la somme des scores des adversaires (un suisse), et la somme des scores
  des adversaires battus plus la moitié de ceux avec qui la partie a été nulle (un toutes rondes).
- **Chess960, Chess324** : des positions de départ avec la première rangée mélangée : 960 avec le roi entre les tours
  (le roque allant sur ses cases habituelles), 324 avec le roi et les tours à leur place.
- **Cycle** : dans un toutes rondes, chaque appariement joué une fois avec chaque couleur (cutechess l'appelle une
  ronde, de 2 parties par rencontre).
- **ECO** : les codes de l'Encyclopaedia of Chess Openings, de A00 à E99.
- **Elo, nElo** : la différence de classement que représente un score. Le nElo (Elo normalisé) divise par la
  dispersion des résultats, si bien que sa valeur ne dépend pas du taux de nulles ; fishtest et OpenBench expriment
  en nElo les bornes de leurs SPRT.
- **EPD, FEN** : une ligne qui décrit une position ; une ligne EPD peut porter des opérations, comme le meilleur coup
  `bm` d'une suite de tests.
- **Gauntlet** : les moteurs testés (les têtes de série) jouent contre tous les autres moteurs, qui ne jouent pas entre
  eux.
- **H0, H1** : les deux hypothèses d'un SPRT : la différence d'Elo est de elo0 ou moins (H0), ou de elo1 ou plus (H1).
- **Hash** : la mémoire qu'un moteur prend pour sa table de transposition, en Mo.
- **LLR** : le log-likelihood ratio (logarithme du rapport de vraisemblance) d'un SPRT ; le test se termine quand il
  sort de ses bornes, calculées à partir d'alpha et beta.
- **LOS** : la likelihood of superiority, la probabilité qu'un moteur soit plus fort qu'un autre, d'après leurs
  parties.
- **MultiPV** : un moteur qui analyse plusieurs lignes à la fois.
- **Parallélisme** : le nombre de parties jouées en même temps.
- **Pentanomial** : les résultats des paires de parties comptés selon leur total : 0, ½, 1, 1½ ou 2 points. Il donne
  des barres d'erreur plus serrées que le compte des parties seules, puisque les deux parties d'une paire partagent une
  ouverture.
- **PGN** : le format texte des parties d'échecs : des balises comme `[White "…"]`, puis les coups.
- **Polyglot** : le format binaire de livres d'ouvertures (`.bin`) lu par la plupart des programmes, avec un poids pour
  chaque coup.
- **Ponder** : réfléchir sur le temps de l'adversaire ; désactivé dans les tournois de fastchess-gui.
- **SPRT** : le sequential probability ratio test (test séquentiel du rapport de vraisemblance), qui joue des parties
  jusqu'à pouvoir dire, avec les taux d'erreur choisis (alpha, beta), si un moteur gagne elo1 ou plus (H1) ou pas plus
  de elo0 (H0).
- **Suite d'ouvertures, livre** : les positions ou les coups dont partent les parties, chacun joué deux fois, couleurs
  inversées.
- **Syzygy** : les tables de finales qui donnent le résultat de chaque position avec peu de pièces (fichiers WDL,
  `.rtbw`).
- **Tables de Berger** : l'ordre des rondes d'un toutes rondes dans lequel chaque moteur joue une fois par ronde.
- **Tête de série** : une place dans l'ordre des participants : les moteurs testés d'un gauntlet, les favoris d'un KO.
- **TF** : time forfeits, les parties perdues au temps.
- **Threads** : les threads du processeur avec lesquels un moteur calcule.
- **TLCS, TLCV** : Tom's Live Chess Server et Viewer, un protocole pour suivre des parties de moteurs en direct.
- **UCI, WinBoard (CECP)** : les deux protocoles par lesquels les programmes parlent aux moteurs d'échecs.
- **WDL** : les chances de victoire, de nulle et de défaite qu'un moteur donne avec son score.
