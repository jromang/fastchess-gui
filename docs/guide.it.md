# Guida utente di fastchess-gui

Questa guida descrive ogni pagina, impostazione e file di fastchess-gui. Il [README](../README.md) dice cos'è il
programma, dove scaricarlo e come compilarlo. Nel programma, F1 apre questa guida accanto alla pagina, alla sezione di
ciò che si trova sotto il mouse, nella lingua dell'interfaccia (vedi [Aiuto nel programma](#aiuto-nel-programma)).

## Avvio rapido
<!-- en: quick-start 92353f2c -->

### Il tuo primo torneo
<!-- en: your-first-tournament 75e9a891 -->

1. **Motori.** Nella barra a sinistra, *Motori*, poi *Aggiungi…* e scegli il programma di un motore (un eseguibile
   UCI o WinBoard): viene avviato una volta per leggerne il nome e le opzioni. *Importa…* > *Una lista di motori…*
   prende i motori di un altro programma (cutechess, Banksia, Arena, ChessGUI), *Importa…* > *Tutti i motori di una
   cartella…* tutti i programmi di una cartella. *Verifica* dice se un motore funziona.
2. **Nuovo torneo.** *Tornei* > *Nuovo torneo*. Scegli un *Formato* nel suo elenco (*Girone all'italiana*,
   *Gauntlet*…) e una *Cadenza* (*10+0.1* per una prima prova), e lascia le altre righe come sono.
3. **Partecipanti.** Fai doppio clic sui motori nella *Libreria* accanto ai partecipanti, oppure selezionane diversi e
   premi Invio.
4. **Avvia.** Il torneo si apre nel suo spazio di lavoro: *Panoramica* per la classifica e il tempo rimanente,
   *Diretta* per le partite in corso.

Il riepilogo a destra della configurazione dice quante partite verranno giocate e quanto dureranno, e cosa andrebbe
storto (un programma non trovato, troppi thread, un libro mancante), ogni cosa con un pulsante che la sistema.

### Testare una nuova versione di un motore
<!-- en: testing-a-new-version-of-an-engine 300dc828 -->

1. Aggiungi entrambe le versioni alla libreria.
2. *Nuovo torneo*, *Formato*: *SPRT miglioramento [0, 3]* (la nuova versione guadagna?) o *SPRT non regressione
   [-5, 0]* (non è più debole?).
3. Scegli la nuova versione come **A** e quella vecchia come **B**, una cadenza breve (*10+0.1*) e, in *Aperture*, un
   libro di posizioni equilibrate (EPD), poi **Avvia**.

Il match si ferma da solo appena l'SPRT decide: H1 accettata (A è più forte, secondo i limiti) o H0 accettata (non lo
è). La *Panoramica* mostra l'Elo, l'LLR e i suoi limiti man mano che arrivano le partite. La configurazione viene
conservata: la volta successiva scegli la versione seguente come A e **Avvia**, oppure clicca *Rilancia con…* sul
match terminato.

### Dove proseguire
<!-- en: where-to-go-next 2cd9b9f7 -->

- [Nuovo torneo](#nuovo-torneo): ogni impostazione, e [consigli](#consigli-per-le-impostazioni) per sceglierle;
- [Svolgimento di un torneo](#svolgimento-di-un-torneo): cosa mostrano le schede, la pausa, come modificare un torneo
  mentre gioca;
- [Classifiche](#classifiche): una sola lista Elo su tutte le partite giocate;
- [Trasmissione](#trasmissione): le partite in diretta su una pagina web, in TLCV o su lichess;
- [Risoluzione dei problemi](#risoluzione-dei-problemi) quando qualcosa va storto, e il [Glossario](#glossario) per le
  parole dei tester.

## La finestra
<!-- en: the-window 3a5230d4 -->

### Le pagine
<!-- en: the-pages b13b4de2 -->

Una barra di navigazione a sinistra porta a otto pagine:

| Pagina | Contenuto |
|---|---|
| Tornei | La pagina iniziale: cosa è in corso e cosa richiede un intervento, la coda, gli ultimi tornei terminati, tutti i tornei del profilo, *Nuovo torneo* |
| Motori | La libreria dei motori: opzioni, verifiche, gruppi, Elo di riferimento, importazione delle liste di altri programmi |
| Libri | La libreria dei libri di aperture, e le aperture di ogni libro con i loro difetti |
| Scacchiera | Una partita o una posizione di qualsiasi provenienza, analizzata da un motore della libreria |
| Suite | Suite di test (STS, WAC, ERET…): motori contro le posizioni di un file EPD |
| Classifiche | Una sola lista Elo su tutte le partite di tutti i tornei |
| Log | L'output di fastchess stesso e i messaggi del programma, e *Segnala un problema…* |
| Impostazioni | Lingua, tema, dimensione dell'interfaccia, notifiche, aspetto delle scacchiere, cartelle del profilo e dei tornei, macchine remote, token lichess, versione, modo in cui viene disegnata la finestra |

### Aiuto nel programma
<!-- en: help-in-the-program 351be20d -->

- **Tooltip.** Lascia il mouse su un controllo per vedere cosa fa. Il tooltip di un controllo che questa guida spiega
  più a fondo termina con *F1: maggiori dettagli nella guida*.
- **F1** apre questa guida in un pannello a destra della pagina, alla sezione di ciò che si trova sotto il mouse (una
  riga della configurazione, una scheda della pagina iniziale, la linguetta di una scheda, una colonna della
  classifica); se sotto il mouse non c'è nulla, alla sezione del pannello cliccato per ultimo, altrimenti a quella
  della pagina. Il **?** rotondo accanto al titolo di una pagina apre la sezione della pagina. Di nuovo F1, o Esc nel
  pannello, lo chiude.
- **Il pannello della guida** mostra una sezione con le sue sottosezioni, e la pagina accanto continua a funzionare:
  si può leggere la spiegazione mentre si cambia l'impostazione. La sua intestazione ha *Indietro* e *Avanti* (anche
  Alt+Sinistra e Alt+Destra, o i tasti laterali del mouse), *Indice* (tutte le sezioni), il campo di ricerca (Ctrl+F
  nel pannello) e *Apri nel browser* (questa guida su GitHub, per la versione in uso). La ricerca trova tutte le
  parole digitate, in qualsiasi ordine, maiuscole e accenti a parte; i titoli vengono prima, e le corrispondenze sono
  evidenziate. I link ad altre sezioni si aprono nel pannello, quelli a pagine web nel browser, e un blocco di codice
  ha *Copia*. Trascina il bordo sinistro del pannello per allargarlo; la sua larghezza viene conservata.
- **Lingue.** La guida è integrata nel programma, in tutte le lingue dell'interfaccia, e segue la versione in uso;
  niente viene scaricato dalla rete. Una sezione la cui traduzione manca o è più vecchia del testo inglese viene
  mostrata in inglese, con una riga che lo dice. Anche gli archivi delle versioni la contengono (`guide.md`, e
  `guide.<language>.md` per ogni traduzione).
- **Ctrl+K**, la tavolozza dei comandi, trova le sezioni della guida per titolo (*Guida: …*), e *Cerca «…» nella
  guida* vi cerca ciò che è stato digitato.
- **Messaggi.** Un problema segnalato dal programma nomina il motore, il file o la risorsa, con la soluzione a portata
  di mano; un **?** accanto apre la sua voce in [Risoluzione dei problemi](#risoluzione-dei-problemi).
- **Per iniziare.** Finché non viene avviato un primo torneo, la pagina iniziale mostra i passi dell'[Avvio
  rapido](#avvio-rapido), ciascuno con il suo pulsante, spuntato una volta fatto; *Nascondi* li toglie.
- [Segnala un problema](#segnalare-un-problema) quando il programma non fa ciò che dovrebbe.

### La tavolozza dei comandi
<!-- en: the-command-palette 725e7934 -->

**Ctrl+K** apre la tavolozza dei comandi: le pagine e le schede, i motori della libreria (apre la loro scheda), i
libri, i tornei della pagina iniziale, le sezioni della guida, e le azioni possibili in quel momento (*Pausa*,
*Riprendi*, *Metti tutto in pausa*, *Ferma*, *Avvia*, *Metti in coda*, *Rilancia … con…*, le voci di *Importa…*,
*Verifica quelli non verificati*, *Esporta per una lista di rating…*, *Copia* la classifica, *Copia la versione*,
*Segnala un problema…*, i colori della scacchiera e i set di pezzi, i suoni delle mosse attivati o disattivati…).
Maiuscole e minuscole non contano e le lettere digitate non devono essere adiacenti («le» trova *Lista Elo*); i nomi
che iniziano con esse, o con una parola che inizia con esse, vengono prima. Su e Giù scelgono, Invio esegue, Esc
chiude; Ctrl+K viene ignorato mentre si scrive in un campo di testo.

### Lingua, tema e dimensione
<!-- en: language-theme-and-size 954fe416 -->

L'interfaccia è in inglese, francese, tedesco, spagnolo, italiano, portoghese brasiliano, russo o cinese semplificato.
All'avvio segue la prima lingua preferita del sistema (l'inglese quando non esiste una traduzione per essa); la si può
imporre in **Impostazioni**. Il cinese usa un font del sistema: Microsoft YaHei su Windows, Noto Sans CJK o
WenQuanYi su Linux. Il tema è chiaro, a meno che in **Impostazioni** non si scelga quello scuro, o quello del
sistema.

Le traduzioni spagnola, italiana, portoghese, russa e cinese sono nuove: le correzioni dei giocatori di queste lingue
sono benvenute. Quelle dell'interfaccia sono semplici file `.po` in `gui/i18n`, che Poedit e programmi simili aprono;
quelle di questa guida sono `docs/guide.<language>.md`.

Le pagine usano tutta la finestra, qualunque sia la sua dimensione: le scacchiere della scheda Diretta crescono con
essa, il modulo del nuovo torneo si divide in due colonne su una finestra larga, e l'elenco delle pagine Motori, Libri
e Classifiche si può allargare trascinandone il bordo destro. Tutto (testo, pulsanti, scacchiere) si può anche
ingrandire o rimpicciolire, dall'80 al 250 %: **Impostazioni** > Dimensione dell'interfaccia, oppure Ctrl + rotellina
del mouse, Ctrl + e Ctrl -, e Ctrl 0 per tornare al 100 %. F11 porta la finestra a schermo intero, e la riporta
indietro.

Un badge sulla scheda *Diretta* e sul pulsante *Tornei* indica il numero di partite in corso. Un campo numerico
accetta operazioni aritmetiche: `40*60`, `15+10`, con `+ - * /` e parentesi, calcolate con Invio o quando si lascia il
campo; Esc riporta il campo al suo valore precedente.

### Le scacchiere e i loro pulsanti
<!-- en: boards-and-their-buttons 1ed997f4 -->

Sotto ogni scacchiera che mostra una partita (la riproduzione della scheda *Partite*, la partita in primo piano in
*Diretta*, una scacchiera staccata, la pagina Scacchiera), una fila di piccoli pulsanti porta alla posizione
iniziale, alla mossa precedente, alla mossa successiva e all'ultima, e ruota la scacchiera; i loro tooltip indicano i
tasti, che fanno lo stesso: ← → Inizio Fine, e F per ruotare. Chi preferisce i tasti nasconde i pulsanti con
**Impostazioni** > *Aspetto* > *Pulsanti di navigazione sotto le scacchiere*, con un clic destro su di essi, o con la
tavolozza dei comandi (*Nascondi i pulsanti di navigazione sotto le scacchiere*, e *Mostra…* per farli tornare). La
rotellina del mouse su un elenco di mosse scorre l'elenco, mai le mosse. **A** su qualsiasi scacchiera apre la sua
posizione nella pagina [Scacchiera](#scacchiera) e la analizza.

### Versione e nuove versioni
<!-- en: version-and-new-versions 70e3e9a2 -->

La versione è nel titolo della finestra e in **Impostazioni**, il cui *Copia* la mette negli appunti insieme al
sistema, per una segnalazione di bug. *Avvisami quando esce una nuova versione* (disattivato per impostazione
predefinita) chiede a GitHub l'ultima versione pubblicata all'avvio, al massimo una volta al giorno e in background;
quando è più recente, la pagina iniziale lo dice, con *Pagina di download* (la pagina della versione su GitHub, nel
browser), *Novità* (le sue note) e *Nascondi* (fino alla versione successiva). Il programma non scarica né installa
nulla, e un errore (nessuna rete, GitHub irraggiungibile) viene solo scritto nel log.

### Segnalare un problema
<!-- en: reporting-a-problem 3e4e5989 -->

*Segnala un problema…* (in **Impostazioni**, nella pagina **Log**, nella tavolozza dei comandi e nel messaggio di un
errore all'avvio) apre una nuova issue su GitHub nel browser, dal modulo di segnalazione dei bug, già compilata con la
versione, il sistema (sistema operativo, processore, memoria, scheda grafica e driver) e le ultime righe del log.
Rileggila prima di inviarla, togli ciò che non vuoi mostrare (percorsi, nomi di motori) e di' cosa hai fatto, cosa ti
aspettavi e cosa è successo. Il programma non invia nulla da sé. Le idee vanno nello stesso posto: *New issue*,
*Idea*.

### Dimensione e posizione della finestra
<!-- en: window-size-and-place 892d41c4 -->

La finestra si apre come è stata lasciata: dimensione, posizione sugli schermi, massimizzata o no. Quando lo schermo
su cui si trovava non c'è più (un monitor scollegato), si apre al centro di quello principale; quando la sua
dimensione non entra più nello schermo (una risoluzione più bassa), alla dimensione predefinita. Anche i pannelli
trascinati fuori di essa, come una scacchiera staccata, tornano dov'erano (`layout.ini`), e dentro la finestra quando
il loro schermo non c'è più. Wayland non permette a un programma di scegliere la posizione della sua finestra: lì
tornano solo la dimensione e lo stato massimizzato.

### Disegno della finestra
<!-- en: drawing-the-window caff7bef -->

La finestra è disegnata con OpenGL 3.2. Dove non è disponibile (una macchina virtuale senza accelerazione 3D, un
desktop remoto, un vecchio server), il programma la disegna invece con il renderer proprio di SDL: Direct3D 11 su
Windows, OpenGL 2.1, o il solo processore. Il log dice quale, e perché OpenGL non è stato usato; anche
**Impostazioni** > Rendering lo mostra, con il nome della scheda grafica quando disegna OpenGL. *Rendering software*
lì (o `--software` sulla riga di comando, per un solo avvio) fa disegnare la finestra al processore dall'avvio
successivo, per un driver grafico che non funziona o va in crash. Usa più tempo del processore, ridisegna solo ciò che
cambia (e ciò che si muove, come gli orologi, 15 volte al secondo invece di 30 mentre mouse e tastiera sono fermi),
disegna angoli squadrati, e i pannelli dello spazio di lavoro fluttuano dentro la finestra principale invece che su
un altro schermo. Sotto Wayland (e su macOS) l'immagine del processore viene comunque mostrata tramite il driver
grafico; se non ce n'è uno, avvia il programma con `SDL_VIDEODRIVER=x11`. Quando nessun modo di disegnare funziona,
un messaggio lo dice prima che il programma termini.

### Aspetto
<!-- en: appearance decda352 -->

**Impostazioni** > *Aspetto* sceglie l'aspetto di tutte le scacchiere del programma (il mosaico in diretta e la
partita seguita, le scacchiere staccate, lo schermo grande, una partita rivista in *Partite*, la scheda *Aperture*,
le aperture di un libro), con una piccola scacchiera accanto alle scelte che le mostra:

![Impostazioni > Aspetto: la scacchiera marrone e i pezzi Celtic](screenshots/appearance.png)

- **Scacchiera**: i colori delle case, per il tema chiaro e per quello scuro (viene disegnata la scacchiera del tema
  in uso). Uno schema predefinito (*Grigio azzurro*, i colori delle versioni precedenti; *Marrone*; *Blu*; *Verde*;
  *Grigio*), oppure *Personalizzato*: un clic su *Case chiare*, *Case scure* o *Ultima mossa* apre un selettore di
  colore, e cambiare un colore rende la scacchiera personalizzata a partire dai colori che aveva. *Ultima mossa* viene
  sovrapposta alle due case dell'ultima mossa, con una trasparenza che lascia vedere il colore della casa.
- **Pezzi**: un set integrato nel programma (*Cburnett*, quello predefinito; *Chessnut*; *Celtic*; *Fantasy*; autori e
  licenze nel tooltip e in `ATTRIBUTION.md`), oppure *La tua cartella…*: una cartella di 12 immagini SVG chiamate
  `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` e `bK.svg` … `bP.svg` (re, donna, torre, alfiere,
  cavallo e pedone bianchi e neri). Le immagini sono disegnate con nanosvg, come quelle integrate: forme semplici,
  riempimenti, tratti e sfumature; niente testo, filtri o immagini incorporate. Quando un file manca, supera 1 MB o non
  è un'immagine che si possa disegnare, la pagina dice quale e perché, nel frattempo vengono disegnati i pezzi
  predefiniti, e *Rileggi* rilegge la cartella una volta corretti i file. La pagina web della trasmissione mantiene i
  pezzi predefiniti.
- **Suoni delle mosse** (disattivati per impostazione predefinita): *Un suono per ogni mossa* emette un breve colpo
  per ogni mossa della partita seguita, in *Diretta* e sullo schermo grande, e uno più pesante per una cattura; non
  per ogni partita del mosaico. *Volume*, e *Prova una mossa* / *Prova una cattura* per sentirli. I suoni sono generati
  dal programma (nessun file audio); il dispositivo audio viene aperto solo mentre sono attivi, e quando non ce n'è uno
  il log dice perché e il programma resta in silenzio.

- **Figurine nella notazione** (attive per impostazione predefinita): le mosse della pagina Scacchiera, nelle sue mosse
  e nelle linee della sua analisi, sono scritte con i pezzi del set scelto sopra (Nf3 diventa un cavallo e f3);
  disattivate, con le lettere.

Anche la tavolozza dei comandi ha queste scelte (*Colori della scacchiera: Marrone*, *Pezzi: Celtic*, *Attiva i suoni
delle mosse*…). Sono conservate in `settings.json` nel profilo; nella modalità portatile la cartella dei pezzi viene
conservata relativa alla cartella del programma.

**Loghi dei motori**: la scheda di un motore nella pagina **Motori** ha *Logo* > *Scegli…*, un'immagine PNG, JPEG o
SVG di 2 MB al massimo (4096 pixel al massimo per lato), mostrata accanto al nome del motore in *Diretta*, sullo
schermo grande e sulla pagina web della trasmissione. L'immagine viene copiata nella cartella `logos` del profilo,
quindi l'originale può essere spostato o cancellato e il profilo spostato per intero; *Rimuovi* la toglie (con la sua
copia, a meno che un altro motore non la usi). Il logo si può cambiare mentre il motore gioca.

### La scacchiera 3D
<!-- en: the-3d-board cc0dd596 -->

**Impostazioni** > *Aspetto* > *Scacchiera 3D* > *La scacchiera della pagina Scacchiera in 3D* disegna la scacchiera
della pagina [Scacchiera](#scacchiera) in tre dimensioni, come la fotografia di una scacchiera vera (disattivata per
impostazione predefinita; tutte le altre scacchiere restano piatte). La scheda grafica la calcola con il path tracing:
l'immagine si affina per qualche secondo finché nulla si muove, pochi millisecondi della scheda per fotogramma, poi la
scheda si ferma; mentre la scacchiera ruota, un'immagine più grossolana ne prende il posto. La piccola scacchiera di
*Aspetto* è allora disegnata anch'essa in 3D, perché le scelte accanto si vedano mentre cambiano:

- l'illuminazione, come i fotografi illuminano una natura morta: *Studio*, *Low key*, *High key*, *Showroom*, *Luce
  da finestra*, *Ora d'oro* o *Sala del torneo*;
- *Esposizione*: più chiaro o più scuro di come lo rende l'illuminazione, in stop, ognuno raddoppia o dimezza la luce;
- *Qualità*: i campioni di cui è fatta l'immagine prima di fermarsi, 512 per impostazione predefinita; di meno
  risparmiano la grafica di un processore durante i tornei;
- *Vernice*, da un legno cerato e opaco a uno strato in cui si vedono le luci, e *Lucentezza*, dal satinato allo
  specchio;
- *Predefinito* riporta esposizione, qualità, vernice e lucentezza come erano; l'illuminazione resta.

Ciò che mostra la scacchiera piatta c'è: la barra di valutazione, l'ultima mossa, il pezzo preso e dove può andare, un
re sotto scacco, le frecce dell'analisi, che si vedono in trasparenza attraverso i pezzi, la risposta che il motore che
ha giocato la mossa si aspettava, tratteggiata, il giudizio sulla mossa come bollino sopra la sua casa, e la cornice di
una linea mostrata al posto della partita. Un clic sinistro sceglie una casa come sulla scacchiera piatta, su un pezzo o
sulla scacchiera dietro di esso, e un pezzo si può trascinare, con il suo fantasma che segue il puntatore (vedi
[Scacchiera](#scacchiera)). Se tieni premuto il tasto destro mentre muovi il mouse, la telecamera gira intorno alla
scacchiera (un clic destro senza muoversi lascia sempre il pezzo preso), la rotellina avvicina, e un clic della
rotellina riporta la vista; ruotare la scacchiera (F) fa fare alla telecamera mezzo giro.

La scacchiera 3D richiede OpenGL 3.3. Dove la finestra è disegnata senza (il renderer software di
[Disegno della finestra](#disegno-della-finestra), alcune macchine virtuali e desktop remoti), le scacchiere restano
piatte e *Aspetto* dice perché. I pezzi sono gli Staunton-Pieces di clarkerubber, con licenza MIT; *Aspetto* ne dà il
collegamento.

## Scorciatoie da tastiera
<!-- en: keyboard-shortcuts e0d74f26 -->

| Tasti | Dove | Cosa fanno |
|---|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | Ovunque | La tavolozza dei comandi |
| <kbd>F1</kbd> | Ovunque | Questa guida, alla sezione di ciò che è sotto il mouse; di nuovo: la chiude |
| <kbd>F11</kbd> | Ovunque | Schermo intero, e ritorno (la finestra propria di una scacchiera staccata quando ha la tastiera) |
| <kbd>Ctrl</kbd>+rotellina, <kbd>Ctrl</kbd>+<kbd>+</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> | Ovunque | Dimensione dell'interfaccia; <kbd>Ctrl</kbd>+<kbd>0</kbd>: ritorno al 100 % |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | Un campo numerico | Calcola il valore; riporta il campo al suo valore precedente |
| <kbd>Alt</kbd>+<kbd>←</kbd>, <kbd>Alt</kbd>+<kbd>→</kbd> | Il pannello della guida | Indietro, avanti |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | Il pannello della guida | Cerca nella guida |
| <kbd>Esc</kbd> | Il pannello della guida | Lo chiude |
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>Pag↑</kbd> <kbd>Pag↓</kbd> | La tavolozza dei comandi | Scelgono |
| <kbd>Invio</kbd>, <kbd>Esc</kbd> | La tavolozza dei comandi | Esegue, chiude |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>Inizio</kbd> <kbd>Fine</kbd> | Una scacchiera che mostra una partita | Mossa precedente, mossa successiva, inizio, ultima mossa |
| <kbd>F</kbd> | Una scacchiera | La ruota |
| <kbd>A</kbd> | Una scacchiera | Apre la posizione nella pagina Scacchiera e la analizza; nella pagina Scacchiera: avvia o ferma l'analisi |
| <kbd>Fine</kbd> | La partita in primo piano in *Diretta* | Torna alla mossa attuale |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | La pagina Scacchiera | Incolla un FEN, una riga EPD, un PGN o un elenco di mosse |
| <kbd>G</kbd> | La pagina Scacchiera | Analizza la partita, passaggio dopo passaggio; di nuovo: la ferma |
| <kbd>X</kbd> | La pagina Scacchiera | Mostra o nasconde la minaccia |
| <kbd>N</kbd>, <kbd>Maiusc</kbd>+<kbd>N</kbd> | La pagina Scacchiera | La mossa successiva, la precedente segnata dall'analisi |
| <kbd>Esc</kbd> | La pagina Scacchiera | Riporta la partita dopo che è stata mostrata una linea; posa il pezzo preso |
| <kbd>←</kbd> <kbd>→</kbd> | Lo schermo grande | La partita precedente o successiva |
| <kbd>+</kbd> <kbd>-</kbd> | Lo schermo grande | Testo più grande o più piccolo |
| <kbd>Esc</kbd> | Lo schermo grande | Esce |
| <kbd>Tab</kbd>, frecce, <kbd>Invio</kbd>, <kbd>Spazio</kbd> | Nuovo torneo | Spostano tra righe, elenchi e pulsanti; aprono una riga o un elenco |
| <kbd>Ctrl</kbd>+<kbd>↑</kbd>, <kbd>Ctrl</kbd>+<kbd>↓</kbd> | I partecipanti | Mandano i partecipanti selezionati in cima, in fondo |
| <kbd>Alt</kbd>+<kbd>↑</kbd>, <kbd>Alt</kbd>+<kbd>↓</kbd> | I partecipanti | Li spostano di un posto |
| <kbd>Canc</kbd> | I partecipanti | Li toglie |
| <kbd>Menu</kbd>, <kbd>Maiusc</kbd>+<kbd>F10</kbd> | I partecipanti | Apre il menu del partecipante (le sue impostazioni proprie) |
| <kbd>Ctrl</kbd>+<kbd>F</kbd> | La libreria della configurazione | Va al suo filtro |
| <kbd>Invio</kbd> | La libreria della configurazione | Aggiunge i motori selezionati, o il primo mostrato |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | La libreria della configurazione | Seleziona tutti i motori mostrati |
| <kbd>Ctrl</kbd>+clic, <kbd>Maiusc</kbd>+clic | Elenchi | Selezionano più elementi |
| <kbd>Canc</kbd> | La pagina Motori | Rimuove il motore selezionato (dopo una conferma) |
| <kbd>Invio</kbd>, <kbd>↑</kbd> <kbd>↓</kbd> | La console di un motore | Invia la riga; richiamano le righe precedenti |
| <kbd>Ctrl</kbd>+<kbd>C</kbd> | La modalità headless | Ferma dopo le partite in corso |

## La pagina iniziale
<!-- en: the-home-page 40031a16 -->

La pagina **Tornei** è una sala di controllo: prima ciò che è in corso e ciò che ha bisogno di te, poi il resto.

- **Per iniziare**, finché non viene avviato un primo torneo: i passi dell'[Avvio rapido](#avvio-rapido) (aggiungere
  motori, configurare un torneo, avviarlo), ciascuno con il suo pulsante e spuntato una volta fatto, e *Leggi l'avvio
  rapido*, che lo apre nel pannello della guida. *Nascondi* toglie la scheda.
- **Metti tutto in pausa**, accanto ad *Apri una cartella…* e *Nuovo torneo*, mette in pausa i tornei in corso (non
  inizia nessuna nuova coppia di partite, le partite in corso arrivano alla fine) e trattiene la coda: nessun torneo in
  coda parte prima di **Riprendi tutto**, che li riprende e lascia proseguire la coda, subito se nel frattempo un
  torneo è finito.
- **Core**: i thread hardware della macchina, e quelli occupati dalle partite in corso, il resto libero. Una partita
  occupa i `Threads` del suo motore che ne ha di più, perché i due motori pensano a turno; la barra mostra il nome di
  ogni torneo e il suo conteggio, e al passaggio del mouse i dettagli.
- **Da sistemare**, quando c'è qualcosa, ognuno con la sua soluzione a portata di mano: un motore che non parte (vedi
  [Un motore che non parte](#un-motore-che-non-parte): *Riprova*, *Ritira…*, *Sostituisci…*, *Log del motore*); il
  torneo che un errore o un motore ha fermato, con ciò che l'ha fermato, *Apri il motore*, *Apri il log*, *Riprendi*,
  *Apri* e *Nascondi*; un disco troppo pieno per una notte di partite (meno di 1 GB libero, o meno del 2 % di un disco
  sotto i 500 GB), quello della cartella dei tornei o di un torneo in corso, controllato ogni minuto, con *Apri la
  cartella* e *Cambia la cartella dei tornei…* (dove vanno i nuovi tornei, come in **Impostazioni**).
- **In corso**: una scheda per ogni torneo in corso: nome, formato, cadenza e stato, una barra di avanzamento con le
  partite giocate su quelle previste e le partite in corso, quando finisce, e chi è in testa: il primo della classifica
  con il suo rating contro il resto del campo e i suoi punti (un'eliminazione diretta: i giocatori rimasti; un incontro
  a squadre: il punteggio delle squadre e il suo Elo), il motore in vantaggio in un match con il suo Elo e il
  punteggio, o l'LLR di un SPRT con i suoi limiti, poi l'Elo e il punteggio. *Apri* va al suo spazio di lavoro,
  *Pausa* lo mette in pausa. Un torneo che si può riprendere compare invece sotto *Interrotto*, con *Riprendi*. Vedi
  [Più tornei insieme](#più-tornei-insieme) per i pesi e i core di ciascuno.
- **Coda**: i tornei in attesa, ciascuno con la sua durata e quando parte, e quando finisce tutto (vedi [Coda e
  rilancio](#coda-e-rilancio)).
- **Ultimi terminati**: gli ultimi cinque tornei terminati, con il loro risultato: il punteggio di un match, la
  decisione dell'SPRT, il vincitore di un'eliminazione diretta (della sua finale), il punteggio di un incontro a
  squadre, o il primo della classifica con i punti delle sue partite. *Rilancia con…* per un match, *Clona* (vedi
  [Coda e rilancio](#coda-e-rilancio)) e *Apri*.
- **Affidabilità**: i crash o blocchi, le sconfitte per tempo e le mosse illegali delle ultime 24 ore, la memoria
  libera e installata, e i processi dei motori in esecuzione; poi i motori che hanno perso quelle partite, i tre con il
  numero più alto, con *Apri il motore* e *Apri la cartella* (la cartella `logs` del torneo conserva il log del motore
  di ogni partita finita male). Una sconfitta per tempo prima della mossa 20 viene distinta: più spesso è un motore
  configurato male (troppi thread, un hash troppo grande per essere allocato in tempo, una rete caricata a ogni
  partita, un avvio lento) che un problema di gestione del tempo.
- **Tutti i tornei**: tutti i tornei del profilo, dal più recente, con *Apri*, *Clona* e *Cartella*; il titolo li
  ripiega. Il risultato di un torneo terminato compare al passaggio del mouse, sopra il suo stato.

Anche la tavolozza dei comandi ha queste azioni: *Metti tutto in pausa*, *Riprendi tutto*, *Rilancia … con…* per gli
ultimi match terminati, *Clona …* per il torneo mostrato e gli ultimi terminati, *Apri la cartella dei tornei* e
*Cambia la cartella dei tornei…*.

## Motori
<!-- en: engines 4f15155a -->

La pagina **Motori** è la libreria dei motori: l'elenco a sinistra, la scheda del motore scelto a destra.

### Aggiungere e verificare i motori
<!-- en: adding-and-checking-engines ebf41ee1 -->

**Aggiungi…** registra un eseguibile UCI. Il motore viene avviato una volta per leggerne il nome, l'autore e le
opzioni. Le opzioni si modificano in una tabella: i valori modificati sono evidenziati, e solo i valori diversi dal
predefinito del motore vengono inviati con `setoption` all'inizio di ogni partita. `Threads` e `Hash` vengono per
primi.

- **Duplica** crea una seconda voce per lo stesso binario, con altre opzioni (un `Hash` diverso, un livello di forza,
  un altro file di rete…). Le voci prendono il nome dall'`id name` UCI quando vengono aggiunte e possono essere
  rinominate.
- **Verifica** avvia il motore, misura quanto rapidamente risponde a `uci` e `isready`, e gli fa giocare una mossa;
  poi esegue i passi di conformità UCI di fastchess (`id name` e `id author`, posizioni da un FEN e con mosse, `go`
  con ogni orologio, righe info con un punteggio intero…), fino al primo che fallisce. Il risultato (✓ o l'errore)
  resta accanto al motore con la sua data, e *Rapporto di conformità UCI* elenca i passi, con ciò che il motore ha
  scritto dopo quello fallito. Un motore a cui si parla in xboard riceve solo la prima parte.
- **Console…** parla con il motore, fuori da qualsiasi torneo: viene avviato con il programma, gli argomenti e la
  cartella di lavoro della libreria, e un motore UCI riceve `uci` e poi i valori delle opzioni della libreria. Le righe
  digitate vengono inviate così come sono (Invio; Su e Giù richiamano quelle precedenti), e il log mostra ogni riga
  con la sua ora, quelle inviate marcate `>` e l'output degli errori `!`. Dei pulsanti inviano i comandi abituali
  (`isready`, `position startpos`, `go depth 10`, `go infinite`, `stop`…; quelli di WinBoard per un motore WinBoard,
  a cui si parla nel suo protocollo a meno che non sia spuntato *Tramite l'adattatore*) e la posizione della pagina
  Scacchiera. *Copia* e *Salva…* conservano il log. La console compare al posto delle impostazioni del motore
  (*Impostazioni* vi torna, lasciando la console aperta); *Chiudi* termina il motore. Mentre un torneo è in corso, ciò
  che il motore cerca toglie core alle sue partite: la console lo dice. Il suo motore non è uno dei processi del
  torneo: la fine di un torneo non lo termina.
- **Test rapido** gioca un breve match contro il motore stesso o un altro: 10 partite a 10+0.1 da 5 posizioni (una con
  una presa en passant), con lo stesso codice di un torneo, mostrato nel suo spazio di lavoro mentre gioca. Il
  risultato resta accanto al motore: il suo punteggio, le sue sconfitte per tempo, i crash e le mosse illegali, e la
  quota del suo orologio che ha usato, che rivela un motore che gioca troppo in fretta o troppo vicino alla caduta
  della bandierina. Le sue PV di matto vengono verificate (vedi *Verifica le PV di matto* in [PGN e
  varie](#pgn-e-varie)). Viene conservato solo l'ultimo test rapido, in `tests/` nel profilo (con i log del motore
  delle sue partite finite male).

### Una libreria grande
<!-- en: a-large-library beb231ed -->

- **Gruppi**: etichette libere separate da virgole (`pool A`, `list 40/15`…). L'elenco si può filtrare per gruppo o
  cercare, e un gruppo si può aggiungere a un torneo con un clic. *Note*: qualsiasi cosa da ricordare sul motore.
  *Logo*: un'immagine mostrata accanto al suo nome in *Diretta*, sullo schermo grande e sulla pagina web della
  trasmissione (vedi [Aspetto](#aspetto)).
- **Ricerca e filtri**: ogni ricerca di motori (la libreria, e gli elenchi di motori delle altre pagine) trova un
  nome, il nome che il motore dichiara, il suo autore, il percorso del suo programma o le sue note; il filtro di stato
  mostra i motori che funzionano, che hanno fallito o sono cambiati, mai verificati, che hanno perso il loro
  programma, che parlano WinBoard, o inattivi. Ctrl o Maiusc clic seleziona più motori: *Verifica* (uno dopo l'altro,
  mai mentre un torneo gioca), *Aggiungi a un gruppo…*, *Rimuovi…*. *Altro…* verifica quelli mai verificati
  (*Verifica quelli non verificati*) e rimuove i programmi mancanti. Un'opzione stringa che contiene un file o una
  cartella (`EvalFile`, `SyzygyPath`…) ha un pulsante *…*.
- **Inattivo** (sulla scheda, o dal clic destro per tutti i motori selezionati): il motore resta nella libreria, in
  grigio, ma è escluso dai selettori di motori (configurazione, suite, test rapido, rilancio, aggiunta di un motore a
  un torneo in corso) e da *Verifica quelli non verificati*. Un torneo o una configurazione che lo ha già lo tiene.
- **Usa una GPU** (sulla scheda): un motore a rete che gioca su una scheda grafica, come Lc0. Non giocano insieme più
  motori di questo tipo delle *GPU* di **Impostazioni**, in un torneo e tra tornei (vedi [Più tornei
  insieme](#più-tornei-insieme)); dal prossimo torneo a cui partecipa.
- **Elo di riferimento**: il rating del motore in una lista di cui ti fidi. Un torneo o la lista Elo possono essere
  ancorati su di esso, e le partite di un torneo lo riportano nei loro tag `WhiteElo` e `BlackElo` (copiato quando il
  motore entra nel torneo). *Importa…* > *Una lista Elo, per l'Elo di riferimento…* lo imposta per molti motori in una
  volta (vedi [Importare motori e rating](#importare-motori-e-rating)).

### Avvio dei motori
<!-- en: starting-engines 09d107ba -->

- **La lettura delle opzioni** non blocca mai un avvio. *Riprendi* parte subito: un torneo fa giocare i motori copiati
  nella sua cartella. *Avvia*, la coda e il test rapido attendono solo la lettura di un motore che fanno giocare, letto
  per primo; qualsiasi altra lettura o verifica viene interrotta e rifatta più tardi. Un avvio che attende lo dice sul
  suo pulsante (*In attesa di …*, con *Annulla*), e nel frattempo un altro avvio viene rifiutato. Un avvio attende
  anche una suite di test in corso, e una suite di test non parte mentre è in corso il test rapido o un avvio attende.
  Letture, verifiche e suite di test girano accanto ai tornei, che giocano in processi propri. Un motore in coda può
  essere letto subito dopo dal suo menu.
- *Riavvia il motore a ogni partita* avvia un nuovo processo per ogni partita invece di riutilizzarlo. Altrimenti un
  torneo tiene un processo in attesa per ogni motore che ha ancora partite da giocare, fino a 32 (più due per partita
  in corso), e chiude un motore dopo la sua ultima partita: in un'eliminazione diretta appena perde il suo match, in un
  sistema svizzero quando non ha più partite nell'ultimo turno.
- *Timeout di avvio (s)* (60 per impostazione predefinita) è il tempo concesso a un motore per rispondere `uciok`
  quando parte e `isready` all'inizio di ogni partita; un motore lento ad avviarsi (una grande rete da caricare) ne ha
  bisogno di più. Un motore che non riesce ad avviarsi viene riprovato tre volte, a 10 s di distanza, con lo stesso
  timeout; una pausa trattiene i tentativi, senza consumarli. Poi le sue partite vengono messe da parte e le altre
  proseguono (vedi [Un motore che non parte](#un-motore-che-non-parte)). Un torneo creato dopo copia il timeout nei
  suoi partecipanti.

### Programmi ricompilati
<!-- en: rebuilt-programs 549daa6e -->

- **Programmi spostati o ricompilati**: la libreria ricorda ogni programma (dimensione, data, CRC32). Un programma che
  non c'è più viene marcato **!** con *Individua…*, e un torneo non parte con esso. Un programma che è cambiato (una
  nuova build, o un altro file con lo stesso percorso) ha le sue opzioni rilette automaticamente.
- **Cosa ha cambiato una nuova build**: quando la nuova lettura differisce dalla precedente, il motore viene marcato
  **Δ** e la sua scheda elenca le differenze fino a *Chiudi*: il suo nuovo `id name` (con *Rinominalo* quando il
  motore aveva preso il nome da quello vecchio) e autore, ogni opzione il cui predefinito, i cui limiti o le cui
  scelte sono cambiati (prima e ora), le opzioni nuove e sparite, e il tuo valore di ciascuna (il valore di
  un'opzione sparita viene scartato; uno che la nuova build non accetta più è in arancione). Per un'opzione il cui
  predefinito è cambiato, o il cui valore non va più bene, scegli *Tieni il mio valore* (o *Tieni il vecchio
  predefinito* dove non ne avevi uno: gioca come prima) o *Usa il nuovo predefinito*; *Tieni i miei valori* e *Usa i
  nuovi predefiniti* scelgono per tutte. L'avviso se ne va una volta fatta ogni scelta, a meno che non abbia altro da
  dire. Più build prima che sia chiuso danno un solo avviso, rispetto alla lettura precedente alla prima. Anche
  *Fallito o modificato* nel filtro di stato mostra questi motori, e la tavolozza dei comandi ha *Cosa è cambiato*,
  *Tieni i miei valori*, *Usa i nuovi predefiniti* e *Chiudi l'avviso delle modifiche* per ciascuno.
- **I programmi dei tornei**: ogni torneo registra i programmi con cui è stato avviato. Il riepilogo della
  configurazione avvisa di un motore il cui programma è cambiato dal suo ultimo torneo (il più recente della cartella
  dei tornei che ha fatto giocare questo programma), con *Cosa è cambiato* quando la sua scheda ha un avviso.
  Riprendere un torneo con un programma ricompilato o sparito nel frattempo lo segnala nella sua intestazione, con
  *Continua* (l'avviso si chiude) e *Pausa*; il torneo non si ferma da solo.

### Importare motori e rating
<!-- en: importing-engines-and-ratings 63154f3f -->

**Importa…** > *Una lista di motori…* legge la lista di motori di un altro programma: ChessGUI (`ChsGUI_Eng.TXT`, con
le opzioni di `ChsGUI_Cfg.ini` accanto), cutechess e Banksia (`engines.json`), Arena (`ArenaENG.cfg`, nella cartella
di Arena: il programma di ogni motore, gli argomenti, il protocollo e, per i motori UCI, i valori delle opzioni
impostati in Arena) e altre liste in stile INI. Una lista che non dà nessun motore lo dice, con i formati letti e
*Tutti i motori di una cartella…* da provare al suo posto.

*Tutti i motori di una cartella…* prende tutti i programmi trovati in una cartella, e ricorda la cartella: *Aggiorna:
i nuovi motori della cartella* propone poi solo i motori aggiunti da allora. I motori trovati vengono prima elencati,
per togliere la spunta a quelli da escludere; i motori già nella libreria (stesso programma e stessi argomenti)
vengono saltati; le opzioni degli altri vengono lette; vengono importati anche i motori WinBoard.

*Importa…* > *Una lista Elo, per l'Elo di riferimento…* legge una lista Elo, incollata o aperta: la versione di
testo di una lista Elo o la sua pagina web (salvata o copiata), l'output di Ordo (testo o CSV), o un CSV di righe
`name,elo`. Un nome della lista è il motore della libreria con lo stesso nome, o l'unico il cui nome è lo stesso a
parte maiuscole e spazi (come per i giocatori dei file PGN importati, vedi [Classifiche](#classifiche)). L'anteprima
mostra l'Elo di riferimento di ogni motore, vecchio e nuovo, spuntato per essere impostato, e i nomi della lista che
non sono un motore della libreria. I nomi di una lista spesso riportano la loro build (`64-bit 4CPU`): se serve,
rinomina i motori perché corrispondano.

### Motori WinBoard
<!-- en: winboard-engines 079e4849 -->

I **motori WinBoard** (protocollo xboard, CECP) giocano come gli altri. Il *Protocollo* di un motore viene
determinato quando viene aggiunto (*Automatico*), o scelto: UCI o WinBoard. fastchess parla solo UCI: un motore
WinBoard viene avviato tramite una traduzione integrata in fastchess-gui (`fastchess-gui --wb2uci ENGINE [ARGS]`, che
fastchess avvia al posto del motore), e le sue opzioni, `memory`, `cores` e il percorso Syzygy diventano opzioni UCI.
Guida il motore come fa WinBoard: feature del protocollo 2, o protocollo 1 per i motori che non ne inviano; posizioni
con `setboard`, o modalità edit; mosse in coordinate o SAN; `level`, `time`/`otim`, `st`, `sd`, `nps`; output del
pensiero, abbandono. I motori marcati ↔ nell'elenco sono motori WinBoard.

- Un motore del protocollo 1 senza `setboard` né modalità edit (TSCP, per esempio) può partire solo dalla posizione
  iniziale: dagli le aperture come mosse (un libro PGN), non come posizioni (EPD).
- Chess960 per un motore con la variante `fischerandom`: `variant fischerandom`, posizioni in X-FEN, arrocco come
  `O-O`/`O-O-O`, come le invia WinBoard.
- *Scores from White's side*, tra le opzioni del motore: per i vecchi motori che danno i loro punteggi dal punto di
  vista del Bianco anziché da quello del lato che muove, come chiede il protocollo; i loro punteggi con il Nero
  vengono invertiti.
- Un motore che chiede di essere riavviato per ogni partita (`feature reuse=0`) riceve *Riavvia il motore a ogni
  partita* spuntato quando viene aggiunto.
- `FCG_WB2UCI_LOG=FILE` nell'ambiente scrive in `FILE` ogni riga scambiata con i motori WinBoard.

## Libri
<!-- en: books 4cf13005 -->

**Aggiungi…** registra un libro di aperture (EPD, PGN o Polyglot `.bin`) con un nome e una descrizione; per un libro
PGN il numero di semimosse tenute, per un libro Polyglot la profondità fino a cui vengono seguite le sue linee. La
sezione *Aperture* della configurazione propone poi i libri della libreria per nome, con il loro numero di aperture
(si può comunque scegliere qualsiasi altro file). *Rimuovi* toglie un libro dalla libreria e lascia stare il file.

### Verificare un libro
<!-- en: checking-a-book 96eee229 -->

Il libro scelto viene letto in background, con i lettori propri di fastchess (così le sue aperture sono esattamente
quelle che gioca un torneo), e verificato:

- **posizioni illegali**: non un re per colore, pedoni sulla prima o sull'ultima traversa, il lato che non muove sotto
  scacco;
- **duplicati**: la stessa posizione di un'apertura precedente, anche per trasposizione;
- **colori invertiti**: un'apertura precedente con i colori scambiati (un libro pensato per essere giocato due volte,
  a colori invertiti, non ha bisogno di entrambe);
- **partite già finite**: nessuna mossa legale, o materiale insufficiente per dare matto.

fastchess esclude da un torneo le aperture la cui posizione non si può giocare (una riga FEN o EPD senza re o comunque
malformata, un re che potrebbe essere catturato, una partita PGN il cui tag FEN non è valido), con un avviso per
libro; qui vengono contate come posizioni illegali. Una partita PGN le cui mosse smettono di essere lette (una mossa
malformata o ambigua) tiene le mosse precedenti. Le righe di un libro PGN che iniziano con `%` (righe di escape, che
lo standard PGN rende commenti; i libri AVT ne hanno alcune) vengono ignorate, anche dai tornei, e la pagina Libri le
conta (*righe che iniziano con %, ignorate*).

Le sue aperture sono elencate sotto, tutte o solo quelle con un problema, ricercabili per parte di un FEN o per mosse
(un libro PGN mostra le sue mosse in SAN); un clic mostra la posizione su una scacchiera. *Usalo per il prossimo
torneo* lo mette nella configurazione (con Chess960 per un libro Chess960). *Salva una copia ripulita…* scrive un
libro EPD senza le posizioni illegali, i duplicati e le partite finite (e, se spuntato, le posizioni a colori
invertiti), e lo aggiunge alla libreria. *Esegui come suite di test…* apre un libro EPD in **Suite**.

### Creare libri
<!-- en: creating-books b9c072ab -->

**Crea…** scrive libri per la libreria (anche nella tavolozza dei comandi):

- *Genera un libro Chess324*: il Chess324 di Larry Kaufman. I re e le torri restano sulle loro case abituali, quindi
  l'arrocco è quello abituale; la donna, gli alfieri e i cavalli di ogni parte sono mescolati sulle altre case della
  sua prima traversa (le colonne b, c, d, f e g), gli alfieri su case di colore opposto. Questo fa 18 disposizioni per
  parte, quella del Bianco e quella del Nero scelte indipendentemente: *Tutte le 324 posizioni*, o *Solo le 18
  simmetriche*, in cui i pezzi del Nero stanno sulle stesse colonne di quelli del Bianco. Sono posizioni normali,
  scritte in un file EPD (proposto accanto ai libri della libreria, o in `books/` nel profilo) e giocate da qualsiasi
  motore, senza Chess960.
- *Costruisci un libro Polyglot…*: un libro Polyglot (`.bin`) dalle partite di un file PGN, come fa `make-book` di
  Polyglot, con la conversione che trasforma i libri PGN dei partecipanti in libri Polyglot. Il suo modulo, al posto
  dei dettagli del libro, prende il file PGN (il libro PGN scelto nell'elenco, se c'è), la **profondità** (le
  semimosse di ogni partita che entrano nel libro), le **partite minime** di cui una mossa ha bisogno nella sua
  posizione per essere tenuta (`-min-game` di Polyglot; le altre vengono escluse perché giocate troppo raramente), i
  **pesi**, *per punti* (i punti della parte che ha giocato la mossa: una vittoria 2, una patta 1, una sconfitta 0,
  come conta Polyglot; le partite senza risultato vengono escluse, e così una mossa le cui partite sono state tutte
  perse) o *per numero* (il numero di partite che la giocano), e, se spuntato, un **Elo**: contano solo le mosse di un
  giocatore con almeno quel rating, secondo il tag `WhiteElo` o `BlackElo` della partita (un giocatore senza rating
  viene escluso). *Costruisci…* chiede dove scrivere il libro (accanto al file PGN per impostazione predefinita), poi
  legge le partite in background, con il suo avanzamento e *Annulla*, accanto al modulo e in fondo all'elenco; il
  libro, ordinato per chiave come vuole il formato, viene poi aggiunto alla libreria, con le sue opzioni nella
  descrizione. La console dice quante partite vi sono entrate, quante ne hanno escluse le opzioni e quante non si sono
  potute leggere.

## Scacchiera
<!-- en: board 0bbd00c9 -->

La pagina Scacchiera mostra una partita o una posizione, di qualsiasi provenienza: **A** su qualsiasi scacchiera del
programma (una partita del mosaico della scheda *Diretta* o quella seguita, la scacchiera della scheda *Partite*, della
scheda *Aperture*, di un'apertura di un libro) apre qui la sua posizione e la analizza; nella pagina Scacchiera, A avvia
e ferma l'analisi. *Torna al torneo* (o *Indietro*) riporta alla pagina da cui si è arrivati. La sua scacchiera può
essere disegnata in 3D: vedi [La scacchiera 3D](#la-scacchiera-3d).

La scacchiera è a sinistra, con i giocatori sopra e sotto di essa; a destra, dall'alto, l'analisi, le mosse (con la
tabella delle mosse, il resoconto della partita e le partite del file in schede) e il grafico della partita. Il
sottotitolo della pagina dice di che partita si tratta: il suo evento e il suo turno, il suo risultato, la sua cadenza,
il suo codice ECO e la sua apertura, e come è finita.

Una mossa si gioca sulla scacchiera con il mouse: un clic sul pezzo e poi uno sulla sua casa, oppure il pezzo trascinato
fin lì. Mentre viene trascinato, segue il puntatore e la casa in cui atterrerebbe è colorata; lasciato altrove, torna
indietro, ancora preso, per essere mosso con un clic. Sulla scacchiera 3D un fantasma traslucido del pezzo segue il
puntatore, sospeso sopra la scacchiera e passando dietro i pezzi davanti a lui, mentre l'immagine resta com'è; il pezzo
stesso si muove quando viene lasciato.

### Aprire, incollare e impostare una posizione
<!-- en: opening-pasting-and-setting-up-a-position 3778ddb5 -->

- **Apri un PGN…** legge qualsiasi file PGN (un file si può anche trascinare sulla finestra), in background con una
  barra di avanzamento: un file di milioni di partite richiede pochi secondi, e le sue partite sono elencate in una
  scheda accanto alle mosse, filtrate da ciò che viene digitato (ogni parola trovata nei giocatori, nell'evento, nel
  luogo, nella data, nel turno, nell'apertura, nel codice ECO o nel risultato). Un clic mostra una partita. Le
  valutazioni delle sue mosse disegnano il grafico: vengono letti i commenti di cutechess e fastchess
  (`{+0.35/18 1.2s}`) e il `[%eval 0.35]` di lichess e ChessBase (dal punto di vista del Bianco, `#-3` per un matto),
  e anche gli orologi di `[%clk 0:01:23]`. Le sue varianti vengono conservate (vedi [Varianti](#varianti)). Una
  partita il cui tag FEN non si può giocare viene rifiutata, e le mosse di una partita si fermano alla prima che non
  si può leggere.
- **Incolla** (Ctrl+V sulla pagina) legge gli appunti: un FEN, o i quattro campi di una riga EPD; un PGN di una o più
  partite (elencate come quelle di un file); un elenco di mosse dalla posizione iniziale, in SAN con o senza numeri
  (`1. e4 e5 2. Nf3`) o in UCI (`e2e4 e7e5`). Una posizione che non si può giocare viene rifiutata con il motivo (un re
  mancante, un pedone sulla prima traversa, il lato che non muove sotto scacco). Diritti di arrocco il cui re o la cui
  torre non sono sulla loro casa abituale, o scritti come colonne (`HAha`), ne fanno una posizione Chess960. Si può
  trascinare anche un file `.fen` o `.epd`.
- **Modifica la posizione** imposta una posizione sulla scacchiera: un clic mette il pezzo scelto (un secondo clic lo
  toglie), un clic destro lo toglie; il lato che muove, il numero della mossa, i diritti di arrocco (proposti solo
  quando il re e la torre sono dove devono essere), la casa en passant (solo dietro un pedone che ha appena avanzato di
  due case) e Chess960. Il FEN sotto segue la scacchiera, e un FEN digitato lì la imposta. *Fatto* viene proposto
  appena la posizione si può giocare, e la mette sulla scacchiera.
- *Copia PGN* (a destra delle schede delle mosse) copia la partita mostrata, con i suoi tag, le sue mosse (i punteggi
  dei motori come commenti quando li ha) e le sue varianti.
- **La scacchiera**: i pulsanti sotto di essa e ← → Inizio Fine scorrono le mosse, e F (o l'ultimo pulsante) la ruota;
  un clic su un pezzo e su una casa gioca una mossa (un pedone che promuove chiede il suo pezzo). *Copia FEN* copia la
  posizione.

### I giocatori
<!-- en: the-players 1f4f551f -->

Sopra e sotto la scacchiera, quando la partita ha dei giocatori: il nome di ciascuno, la valutazione della sua ultima
mossa data dal suo motore (il punteggio del Bianco e la profondità, in un'etichetta), la mossa che ha giocato e il
tempo che ha impiegato, oppure *al tratto*, e a destra il suo orologio dopo la sua ultima mossa, incorniciato mentre ha
il tratto. Gli orologi sono quelli dei `[%clk]` delle mosse quando la partita li ha; altrimenti vengono ricostruiti dal
suo tag `TimeControl` (`300+2`, `10+0.1`: secondi e incremento) e dal tempo di ogni mossa, senza che le mosse di libro
ne consumino.

### Analisi
<!-- en: the-analysis 1e6120de -->

Un motore della libreria analizza la posizione mostrata e segue la scacchiera. La scheda mostra il suo motore, poi un
pulsante con i suoi *Threads*, *Hash* (0: quello del motore) e *Linee* (MultiPV), che un clic apre: le impostazioni
proprie dell'analisi, conservate per la volta successiva, che non cambiano né il motore della libreria né alcun torneo
(gli altri valori delle opzioni della libreria vengono inviati come li invia una partita). A destra, i thread che la
pagina Scacchiera usa su quelli della macchina, nel colore di avviso mentre un torneo è in corso (il suo tooltip dice
quanti thread prendono le partite del torneo: l'analisi compete con esse).

La ricerca si presenta in riquadri (profondità e seldepth, nodi, velocità, tempo, accessi alle tablebase, quanto è
pieno l'hash), le probabilità di vittoria, patta e sconfitta quando il motore le dà (`UCI_ShowWDL`), e ogni linea: il
suo punteggio (del Bianco, il migliore messo in risalto), la sua profondità e le sue mosse su due righe, con le figurine
(vedi [Aspetto](#aspetto)). Passare il mouse su una mossa di una linea mostra sulla scacchiera, incorniciata, la
posizione dopo di essa; un clic gioca la linea fino a quella mossa. La prima mossa di ogni linea è disegnata come una
freccia, la migliore più scura (**Impostazioni** > *Aspetto* > *Le frecce dell'analisi nella pagina Scacchiera* le
toglie; le linee mostrano comunque le mosse).

**Minaccia** (X) fa analizzare a un altro processo del motore la posizione con il tratto passato: la mossa migliore
dell'avversario, se muovesse di nuovo, disegnata come una freccia rossa e scritta sotto le linee (nessuna mentre il
lato che muove è sotto scacco).

L'analisi attende mentre è mostrata un'altra pagina, e prosegue quando si torna alla pagina Scacchiera; *Ferma
l'analisi* (o A) la ferma, e un punto sul pulsante *Scacchiera* della barra indica quando è in corso. Il suo motore non
è uno dei processi del torneo: la fine di un torneo non lo termina. Fermata l'analisi, il suo motore resta avviato per
la ricerca successiva, con la sua tabella hash (e la memoria GPU di un motore a rete neurale): il pulsante di accensione
accanto al motore, *Scarica il motore* (anche nella tavolozza dei comandi), lo termina, insieme a quello della minaccia.

### Analizzare una partita intera
<!-- en: analysing-a-whole-game a7e6166c -->

**Analizza la partita** (G, negli strumenti della pagina o nell'intestazione del grafico) fa analizzare al motore
dell'analisi (i suoi thread e il suo hash, una linea) ogni posizione della partita in background, prima per 20 ms
ciascuna, poi di nuovo quattro volte più a lungo a ogni passaggio (80 ms, 0.3 s, 1.3 s, 5 s…), fino al tempo scelto con
la freccia accanto al pulsante (0.3 s, 1.3 s, 5 s, 20 s per posizione, o *fino all'arresto*): il grafico compare in un
secondo o due e si affina a ogni passaggio. Le posizioni vanno dall'ultima alla prima, così l'hash del motore conserva
ciò che viene dopo una posizione (misurato: circa il 16 % più vicino alle ricerche lunghe che dalla prima all'ultima, a
parità di tempo), con la posizione sulla scacchiera per prima. L'intestazione del grafico indica il passaggio, il tempo
per posizione e l'avanzamento, e una sottile fascia sotto la sua striscia dei tempi ciò che ha ricevuto ogni posizione;
*Ferma* (o G) la ferma.

Ogni mossa è giudicata dalle probabilità di vittoria che chi l'ha giocata ha perso, secondo i punteggi dell'analisi
prima e dopo di essa (la misura di lichess: 50 % a 0.00, circa 59 % a +1.00): 10 punti persi sono un'imprecisione
(`?!`), 20 un errore (`?`), 30 uno svarione (`??`), segnati dopo la mossa, sul grafico e sulla scacchiera; la mossa che
il motore avrebbe giocato non viene mai segnata. Una mossa è giudicata sull'ultimo passaggio completo; un segno che
l'ultimo passaggio ha cambiato resta vuoto finché il successivo non lo conferma. N e Maiusc+N (o i pulsanti accanto
alla navigazione) vanno alla mossa segnata successiva e a quella precedente.

Ogni passaggio completo viene conservato con la partita (in `analyses/` nel profilo): la stessa partita, riaperta da
qualsiasi punto, lo mostra subito, e G riprende dai passaggi fatti. L'analisi gira su un thread proprio con un proprio
processo del motore, i suoi thread contati nel budget dei core; l'analisi della scacchiera (A) la mette in pausa, e
quando A analizza una posizione della partita più a lungo del passaggio, il suo valore prende il posto di quello della
posizione.

**Più analisi**: *+ Motore* nella legenda del grafico fa analizzare la partita anche a un altro motore della libreria,
altrettanto a lungo a ogni passaggio. Ogni analisi ha il suo colore sul grafico e sulla scacchiera (la sua mossa
migliore come freccia mentre l'analisi della scacchiera è ferma), la sua riga in *Questa mossa* e le sue precisioni nel
resoconto. Procedono insieme passaggio per passaggio, tutte allo stesso tempo quando i core lo permettono, una dopo
l'altra altrimenti. Un'analisi giudica le mosse (la prima; un clic destro su un'altra nella legenda ne fa il giudice, o
la rimuove); un segno è vuoto quando le analisi non giudicano tutte la mossa allo stesso modo. Le probabilità di
vittoria confrontano i motori meglio dei centipedoni, che differiscono da un motore all'altro: prendono il WDL di ogni
motore quando ne dà uno.

### Il grafico della partita
<!-- en: the-game-graph 14e02bad -->

Sotto le mosse, il grafico della partita, lo stesso che in *Diretta* e in *Partite*:

- **Valutazione**: l'analisi che giudica come un'area bianca e nera (il vantaggio del Bianco sopra la linea, quello del
  Nero sotto), ogni motore che ha giocato come una curva del suo colore alle sue mosse, le altre analisi come curve
  tratteggiate; le mosse segnate sulla curva; i valori oltre ±6 disegnati lungo il bordo nel colore di avviso. Senza
  un'analisi, l'area è la media delle valutazioni dei due motori.
- Le mosse di libro a strisce, le fasi sotto l'asse (l'apertura; il mediogioco dalla prima posizione con al massimo 10
  pezzi oltre ai re e ai pedoni, o meno di 4 su una traversa di fondo; il finale da al massimo 6, come lichess divide
  una partita), la zona che le tablebase conoscono colorata, i numeri delle mosse lungo il fondo.
- Sotto l'asse, il tempo di ogni mossa: quello del Bianco verso l'alto, quello del Nero verso il basso.

Le sue schede: *Probabilità di vittoria* (dal WDL quando i motori lo danno, con la formula di lichess altrimenti),
*V/P/S* (le quote riportate dai motori, le vittorie del Bianco in basso), *Tempo* (con il tempo di decisione),
*Profondità* (con la seldepth), *Nodi*, *Velocità*, *Accessi alle tablebase*, *Accordo* (quanto distano le valutazioni
dei due giocatori, ogni mossa rispetto a quella prima, e quelle delle analisi in ogni posizione) e *Accordo delle PV*
(quante semimosse della PV di un motore ripetono ciò che quella dell'altro aveva previsto); quelle per cui la partita
non ha valori sono escluse, e quelle che non ci stanno vanno nel menu `⋯`. Passando il mouse si vedono la mossa e ciò
che ha detto ogni motore; un clic o un trascinamento va alla mossa. Un clic su una curva nella legenda la nasconde o la
mostra; la freccia a destra chiude il grafico.

### Questa mossa
<!-- en: this-move a4a908f8 -->

Sotto le mosse, sulla mossa mostrata: ciò che ha detto il motore che l'ha giocata (il suo punteggio, profondità e
seldepth, tempo, nodi, velocità, accessi alle tablebase, come li ha il PGN), la linea che si aspettava dopo di essa, il
verdetto di ogni analisi (la quota delle probabilità di vittoria persa, il giudizio, la mossa migliore e il suo
punteggio), e con le tablebase il risultato dopo la mossa e se la mossa ha mantenuto quello di prima. *Mostra* posa una
linea sulla scacchiera senza toccare la partita: la posizione dopo di essa, la scacchiera incorniciata, con scritto
sopra di che cosa si tratta; Esc, un clic sulla scacchiera o una mossa nella partita riporta la partita. Sulla
scacchiera, il giudizio della mossa è un distintivo sulla sua casa, e la risposta che il suo motore si aspettava una
freccia tratteggiata.

### Varianti
<!-- en: variations 7932794c -->

Una mossa giocata sulla scacchiera dove la partita è andata diversamente apre una variante: la partita resta com'era,
con le sue analisi e il suo grafico. Le mosse mostrano le varianti tra parentesi dopo la mossa che sostituiscono,
annidate e numerate; un clic su una mossa porta lì, e *Torna alla partita* riporta alla posizione da cui parte la
variante. Un clic destro su una mossa di una variante la promuove (diventa la linea da cui parte, le cui mosse dopo di
essa diventano la variante), la elimina con quelle che partono da essa, o la copia. *Copia PGN* scrive le varianti, e
quelle di un file PGN aperto vengono lette (commenti, numeri, annotazioni e risultati saltati; una mossa che non si può
giocare termina solo la sua variante). Il grafico, le analisi e i giudizi sono quelli della partita; su una variante,
il grafico segna dove essa lascia la partita.

### Le mosse, la tabella e il resoconto
<!-- en: the-moves-the-table-and-the-review 7880f8d8 -->

**Mosse** scrive la partita come testo continuo: figurine (vedi [Aspetto](#aspetto)), le mosse di libro in grigio e la
fine del libro segnata, i giudizi a colori, la mossa attuale incorniciata; passando il mouse su una mossa si vedono ciò
che ha detto il suo motore e il suo giudizio. **Tabella** elenca ogni mossa con il punteggio del suo motore (del
Bianco), la profondità e il tempo, una barra per il tempo, le mosse segnate colorate.

**Resoconto**, una volta analizzata la partita: per ciascun lato la sua precisione (quella di lichess: quella di ogni
mossa dalle probabilità di vittoria che ha perso, le mosse dei momenti critici pesano di più), le sue imprecisioni,
errori e svarioni, la sua perdita media in centipedoni, la quota delle sue mosse che anche l'analisi giocherebbe, la
profondità e il tempo medi del suo motore; la sua precisione nell'apertura, nel mediogioco e nel finale; con più
analisi, le precisioni che dà ciascuna. Poi i momenti chiave, ognuno a un clic di distanza: la svolta (la mossa che ha
perso di più, con la mossa migliore), gli altri errori e svarioni, dove i due motori sono stati in disaccordo di 1.5
pedoni o più per due mosse di fila, l'inizio del finale e quello delle tablebase. *Chi l'ha visto per primo* indica,
per la svolta, a quale mossa ogni motore che ha giocato e ogni analisi ha dato per la prima volta un pedone o più al
lato che guadagna. *Copia il PGN con l'analisi* scrive la partita con la valutazione di ogni posizione (`[%eval]`), i
giudizi come NAG e, dopo una mossa segnata, la linea che l'analisi preferiva; *Copia come BBCode* dà il resoconto per
un forum.

### Le tablebase nella pagina Scacchiera
<!-- en: tablebases-on-the-board-page 721b71dd -->

Con le cartelle Syzygy della configurazione (*Aggiudicazione* > tablebase), la pagina Scacchiera interroga ogni
posizione della partita: il grafico colora la zona che le tabelle conoscono, *Questa mossa* indica il risultato dopo la
mossa e se la mossa ha mantenuto quello di prima o lo ha rovinato, e il resoconto segna dove subentrano le tabelle. Ogni
posizione è interrogata come se fosse appena stata giocata una cattura o una mossa di pedone, il che è esatto tranne
dove la regola delle 50 mosse è vicina. Le tabelle sono quelle del processo del programma: non vengono interrogate
mentre un torneo vi gioca.

### Giocare una partita da una posizione
<!-- en: playing-a-game-from-a-position 7e83c318 -->

**Gioca una partita…** gioca una partita dalla posizione sulla scacchiera tra due motori della libreria, o tra te e uno
di essi (vedi *Umano* più sotto) (*Bianco* e *Nero*, `<->` li scambia con i loro orologi), con un orologio per ciascuno
(minuti e un incremento in secondi; *Lo stesso per entrambi*, o uno ciascuno per un handicap di tempo), e, se spuntata,
una seconda partita a colori invertiti. I motori vengono avviati con i valori delle opzioni della libreria, un motore
WinBoard tramite l'adattatore. La scacchiera segue la partita mentre viene giocata (tornare indietro smette di seguirla
finché non se ne raggiunge di nuovo la fine, o si clicca *Torna alla mossa attuale*), con gli orologi, la ricerca del
lato che muove e *Ferma*, che termina la partita senza risultato. Una partita finisce per matto, stallo, materiale
insufficiente, regola delle 50 mosse, triplice ripetizione, mossa illegale, un motore che si ferma, o sconfitta per
tempo (all'orologio sono concessi 0.1 s perché la mossa arrivi). Ogni partita viene aggiunta, quando finisce, a un file
PGN in `games/` nel profilo (con il nome della data e dei motori, con un tag `Termination`), che *Apri le partite*
elenca sulla scacchiera.

Le partite sono giocate dal client per motori proprio di fastchess-gui, al di fuori di qualsiasi torneo: si svolgono
mentre ne gioca uno, senza toccarlo. I loro thread (il maggiore tra quelli dei due motori, perché giocano a turno)
contano nel budget dei core; quando le partite dei tornei in corso e l'analisi riempiono già la macchina, la scheda lo
dice e propone *Gioca comunque* invece di *Gioca*. L'analisi si ferma quando inizia una partita, perché competerebbe
con i suoi motori.

**Umano**, primo negli elenchi *Bianco* e *Nero*, sei tu: giochi quel lato in prima persona; le tue mosse si fanno sulla
scacchiera con il mouse, su quella piatta come su quella 3D (un pedone che raggiunge l'ultima traversa chiede il suo
pezzo). La scacchiera si gira per mettere il tuo lato in basso a ogni partita (*Ruota* continua a girarla), accetta i
tuoi pezzi solo quando tocca a te (la scheda mostra *Tocca a te*), e un clic mentre guardi una mossa precedente la
riporta alla partita. Il tuo orologio scorre come quello di un motore, sconfitta per tempo compresa: concediti più tempo
del motore togliendo la spunta a *Lo stesso per entrambi*. Mentre giochi, ciò che pensa il motore viene tenuto da parte
(la sua valutazione, la profondità e la variante, quindi anche la barra di valutazione e la curva del grafico); la
partita finita li ha, pronta per *Analizza la partita*. *Abbandona la partita* la chiude in qualsiasi momento, vinta dal
motore; con una seconda partita, questa segue, con il tuo colore invertito. Le tue partite vengono salvate con il tuo
nome utente, e solo il motore conta nel budget dei core. Non si può ritirare una mossa, e il motore gioca a piena forza
a meno che la sua scheda non ne imposti una minore (`UCI_LimitStrength` e `UCI_Elo`, o `Skill Level`, se li ha).

## Suite di test
<!-- en: test-suites 42c2b2ba -->

La pagina **Suite** esegue una suite di test EPD: posizioni con la loro mossa migliore (`bm`), le mosse da evitare
(`am`), o, per la Strategic Test Suite, i punti di più mosse (`c0 "f5=10, Be5+=2"`).

- **Suite**: un file EPD, o un libro EPD della libreria. Le righe senza una posizione legale o senza una di queste
  operazioni vengono escluse (ne viene mostrato il numero).
- **Motori**: un numero qualsiasi di motori della libreria, con le loro opzioni; *Hash* e *Threads* impongono un
  valore a ciascuno (0 = quello di ogni motore).
- **Tempo**, **Nodi** o **Profondità** per posizione. *Trovata da (semimosse)*: una ricerca si ferma quando una
  soluzione è rimasta la prima mossa del motore per altrettante semimosse di profondità in più (0 = ogni ricerca usa
  tutto il suo tempo).
- **Parallelismo automatico**: con un tempo, core fisici − 1 divisi per i thread dei motori; con nodi o una
  profondità, ogni thread hardware.

Una posizione è risolta quando la mossa finale del motore è una soluzione: una delle `bm`, nessuna delle `am`, o, con
soli punti, la mossa con il punteggio più alto. Il suo tempo di soluzione è il momento in cui il motore si è fissato su
quella mossa. La griglia mostra le posizioni contro i motori, ✓ con il tempo di soluzione o ✗ con la mossa giocata, e
i punti STS; i totali vengono prima: posizioni risolte, il loro tempo medio e il punteggio STS. Un'esecuzione
terminata viene conservata in `suites/` nel profilo; *Confronta con un'esecuzione salvata…* aggiunge le colonne di
un'esecuzione precedente della stessa suite, per confrontare versioni di un motore. *Esporta CSV…* scrive la griglia.

## Nuovo torneo
<!-- en: new-tournament 6d169de4 -->

**Tornei → Nuovo torneo** apre la configurazione: una scheda di righe ripiegate che dicono come si giocano le partite,
i partecipanti con la libreria dei motori accanto, e un riepilogo aggiornato in tempo reale a destra (numero di
partite, partite per motore, durata stimata, aperture, impostazioni comuni, handicap di tempo). La configurazione
dell'ultimo torneo viene conservata, quindi testare di nuovo due versioni di un motore significa: scegli A e B,
**Avvia**.

Tutto nella pagina si può fare da tastiera: Tab e le frecce spostano tra le righe, gli elenchi e i pulsanti, Invio o
Spazio aprono una riga o un elenco, e lì funzionano i tasti dei [partecipanti e della
libreria](#partecipanti-e-libreria). **Avvia** crea il torneo e lo gioca; **Metti in coda** lo tiene per dopo (vedi
[Coda e rilancio](#coda-e-rilancio)).

### Il riepilogo e i suoi controlli
<!-- en: the-summary-and-its-checks 5609344a -->

Il riepilogo indica la memoria che possono occupare le tabelle hash: due processi di motore per ogni partita giocata
in contemporanea, e i motori in attesa della loro prossima partita, ciascuno con il suo `Hash`. Oltre la memoria
libera avvisa (il sistema userà lo swap), nominando il motore con l'`Hash` più grande; oltre la memoria installata il
torneo non parte. `--run` dice lo stesso. Quando il budget di memoria di **Impostazioni** permette di giocare insieme
meno partite di quante ne indica il parallelismo, il riepilogo dice quante (ogni partita occupa l'`Hash` dei suoi due
motori e ciò che i loro processi occupano in più), e così la riga *Parallelismo*; lo stesso per le partite con un
motore che usa una GPU, rispetto alle *GPU*. Nomina anche i motori il cui programma è cambiato dal loro ultimo torneo
(una nuova build), con *Cosa è cambiato* quando la scheda del motore elenca nuovi predefiniti o nuove opzioni (vedi
[Programmi ricompilati](#programmi-ricompilati)).

Ciò che andrebbe storto viene detto con il motore, la risorsa e la soluzione, un pulsante sotto la riga (la tavolozza
dei comandi elenca le stesse soluzioni):

- un programma o una cartella di lavoro non trovati: *Apri il motore* (la sua scheda nella pagina Motori),
  *Individua…*; un libro di aperture non trovato: *Individua…*;
- un motore il cui `Threads` proprio (la sua impostazione, altrimenti il suo predefinito: alcuni motori prendono tutti
  i core) supera la metà dei thread hardware, o i thread di cui dispone ogni partita con le partite giocate
  insieme (i core fisici con un orologio): *Abbassa Threads a N* imposta l'opzione del motore nella libreria, *Apri il
  motore*. I `Threads` dati dalla configurazione oltre le capacità della macchina ricevono *Abbassa Threads* dove sono
  dati (la cella del partecipante, le impostazioni comuni). L'elenco dei partecipanti marca un motore del genere con
  un *!*, i motori del match sono segnalati sotto di essi;
- un valore di opzione che un motore rifiuta (fuori dal suo intervallo, non tra le sue scelte, un'opzione che non ha
  più): fastchess non lo invia, e il motore gioca con il suo predefinito;
- le tablebase Syzygy che usa il torneo: le cartelle dell'aggiudicazione e ciò a cui punta il `SyzygyPath` di ogni
  motore (più cartelle separate da `;` su Windows, `:` altrove). Per i pezzi usati (i *Pezzi max* dell'aggiudicazione,
  altrimenti le tabelle più grandi trovate), deve esserci ogni file WDL (`.rtbw`), e nessun file troncato (un file
  Syzygy è lungo 16 byte in più di un multiplo di 64; i motori escludono un file di altra dimensione, e alcuni si
  fermano quando lo incontrano): *Syzygy 6 pezzi: 14 file mancanti in D:\TB (KRPPvKR.rtbw…)*, *Aggiudicazione fino a
  6 pezzi, ma solo file da 5 pezzi trovati*, *SyzygyPath di Alpha punta a una cartella senza file di tablebase*, con
  *Apri la cartella* e *Abbassa a 5 pezzi*; il tooltip elenca i file. Sono avvisi: l'aggiudicazione e i motori fanno a
  meno dei file mancanti. Solo un'aggiudicazione senza cartella, o le cui cartelle non contengono tabelle, ferma il
  torneo al suo avvio (fastchess lo rifiuta): *Individua…* sceglie un'altra cartella. Le cartelle vengono lette in
  background, e di nuovo ogni pochi secondi mentre il riepilogo è mostrato.

Un avvio rifiutato nomina gli stessi problemi, con le loro soluzioni e *Apri il log*. Ogni problema ha un **?** che
apre la sua voce in [Risoluzione dei problemi](#risoluzione-dei-problemi).

### Profili e modelli
<!-- en: profiles-and-templates df08e65d -->

**Profili.** Le prime cinque righe, *Formato*, *Cadenza*, *Aperture*, *Aggiudicazione* e *Impostazioni dei motori*,
sono ciascuna un profilo con un nome: il suo elenco ne sceglie uno, la riga accanto dice cosa gioca ("40 mosse in
15 min, ripetuta", "patta 40/8/10 cp · TB 6 pezzi"; il suo tooltip dà lo stesso come lo scrivono cutechess e
fastchess, `tc=40/900`, `-draw movenumber=40 movecount=8 score=10`), e ✎, o un clic su quella riga, apre i dettagli
sul posto, sotto di essa. Una modifica fatta solo per questo torneo mette un punto accanto al profilo, e il suo
elenco e i suoi dettagli propongono *Aggiorna* (il profilo prende la modifica, per ogni torneo e modello che lo usa),
*Ripristina* e *Salva come profilo…* (con un nuovo nome). Il programma include dei profili predefiniti:

- cadenze: 10+0.1 e 60+0.6 (i test brevi e lunghi di fishtest), 2+1, 40/4, 40/15 e 40/20 (40 mosse in 4, 15 o 20
  minuti, ripetute, come le giocano le liste Elo), e 200k nodi per mossa;
- formati: SPRT miglioramento [0, 3] e non regressione [-5, 0], un match di 1000 partite, un girone all'italiana, un
  gauntlet, un sistema svizzero di 9 turni, un'eliminazione diretta e un incontro a squadre;
- aggiudicazioni: nessuna, una standard (patta dalla mossa 40 dopo 8 mosse entro 10 cp, sconfitta dopo 3 mosse a
  600 cp) e la stessa con le tablebase fino a 6 pezzi;
- impostazioni dei motori: quelle di ogni motore, o Hash 16, 64 o 256 MB con 1 thread e i libri propri dei motori
  disattivati;
- aperture: nessun libro, o le 960 posizioni di Chess960.

Un tuo profilo salvato con il nome di un predefinito ne prende il posto; eliminarlo (dal suo elenco, dopo una
conferma) riporta il predefinito, e le configurazioni e i modelli che lo usavano ne conservano i valori. I profili
sono conservati in `profiles.json` nella cartella del profilo, e la tavolozza dei comandi (Ctrl+K) li elenca:
sceglierne uno lo applica alla configurazione. La tavolozza apre anche ogni riga della configurazione per titolo, e
carica o salva i modelli. Le cartelle Syzygy non sono in nessun profilo: appartengono a questo computer, e
l'aggiudicazione e i motori le condividono. Le righe *Parallelismo* e *PGN e varie* contengono ciò che è proprio solo
di questo torneo.

**Modello** e **Nome**, accanto ad *Avvia*: un modello è l'intera configurazione, un profilo per ogni sezione più ciò
che nessun profilo contiene (il nome, i partecipanti, il parallelismo, i file PGN, la trasmissione). Il menu ne carica
uno o salva la configurazione come modello (*Salva la configurazione come modello…*, che chiede conferma prima di
sostituire un modello con lo stesso nome), e anche la tavolozza dei comandi li carica. Una sezione che seguiva il suo
profilo quando il modello è stato salvato segue quel profilo, quindi aggiornare un profilo aggiorna ogni modello che
lo usa; una sezione modificata per quel torneo conserva i propri valori. I modelli salvati da versioni precedenti si
caricano com'erano, e le loro sezioni ricevono i profili a cui corrispondono. Un nome vuoto viene composto dai
motori.

### Formato
<!-- en: format 29a36e7c -->

Il tooltip di ogni formato dice di cosa si tratta.

- *Match*: due motori, A (il motore in prova; ogni statistica è dal suo punto di vista) e B (il riferimento). Un
  numero fisso di partite, fino a quando lo fermi, o un SPRT: elo0, elo1, alpha, beta, modello (`normalized` usa limiti
  in nElo come fishtest e OpenBench, `logistic` l'Elo classico, `bayesian` il BayesElo) e un numero massimo di partite
  facoltativo. I profili SPRT miglioramento e non regressione danno i limiti abituali, e la riga sotto i limiti dice
  quante partite durerà probabilmente il test. *Calcolatore…* disegna, come fa il calcolatore SPRT di fishtest, la
  probabilità che H1 sia accettata e il numero atteso di partite in funzione della vera differenza Elo, a partire da
  una percentuale di patte attesa e dal bias RMS delle aperture; *Usa questi limiti* ricopia nella configurazione i
  limiti che gli sono stati dati. *Scarto Elo* (0 per impostazione predefinita) viene aggiunto a entrambi i limiti, per
  testare contro una versione nota per essere più forte o più debole di tanto: [0, 3] con uno scarto di 5 dà l'SPRT
  [5, 8], che il riepilogo, la panoramica e la modalità headless scrivono `SPRT [0, 3] + 5`; il calcolatore e la riga
  sotto i limiti ne tengono conto. *Partite min.* (0, nessun minimo, per impostazione predefinita) trattiene la
  decisione finché non sono state giocate altrettante partite, mentre l'LLR viene comunque calcolato e mostrato, per
  proteggere un test breve da un arresto precoce dovuto alla fortuna: un LLR oltre un limite prima di allora viene
  segnalato sotto la barra dell'LLR e il match continua; una volta raggiunte le partite, la prossima coppia che
  termina con l'LLR oltre un limite termina il match. Un massimo di partite inferiore al minimo termina il match senza
  decisione. È il modo più rapido per confrontare due versioni di un motore, e non serve altro.
  *Colori* (*Alternati* per impostazione predefinita) dice chi ha il Bianco: A e B a turno, oppure *A sempre col Bianco*
  o *B sempre col Bianco*, come `-noswap` (e `-reverse`) di fastchess, per le partite con handicap in cui il motore più
  forte deve avere sempre il lato svantaggiato, o un'apertura provata da un solo lato. Le partite vanno sempre a coppie,
  entrambe con gli stessi colori, ma ogni partita ha la propria apertura, la successiva del libro (come `-games 1` di
  fastchess): le coppie e le loro statistiche restano, il punteggio include il vantaggio del Bianco, e il bias RMS, che
  invertire i colori annulla, non viene mostrato. Il riepilogo e l'intestazione del match lo indicano.
- *Girone all'italiana*: ogni motore gioca contro ogni altro, *Cicli* volte, ogni volta due partite a colori invertiti
  (ciò che cutechess chiama round, da 2 partite per incontro). I turni seguono le tabelle Berger, quindi ogni motore
  gioca una volta per turno.
- *Gauntlet*: i primi *Motori testati* (le teste di serie) giocano contro tutti gli altri, gli altri non giocano tra
  loro.
- *Sistema svizzero*: un numero di *Turni*. Ogni turno è di due partite, a colori invertiti, contro un motore con un
  punteggio simile; nessuno incontra due volte lo stesso avversario quando si può evitare. Con un numero dispari di
  motori, il motore peggio classificato che non ha ancora avuto un turno di riposo ne riceve uno, che vale un punto.
- *Eliminazione diretta*: i motori sono teste di serie nell'ordine del loro elenco, le prime teste di serie ricevono i
  turni di riposo quando il numero di partecipanti non è una potenza di due, e le prime due teste di serie possono
  incontrarsi solo in finale. Un match è di *Coppie di partite per match* coppie di partite; un match in parità riceve
  una coppia in più alla volta, fino a dieci, dopo di che passa la testa di serie meglio classificata.
- *Incontro a squadre*: due squadre di motori, per confrontare una famiglia di motori, o un insieme di versioni, con
  un'altra nel suo complesso. *Abbinamenti*: *Tutti contro tutti*, ogni motore di una squadra contro ogni motore
  dell'altra, un avversario per turno (un sistema Scheveningen; le squadre possono essere di dimensioni diverse), o
  *Scacchiera per scacchiera*, il primo motore di ogni squadra contro il primo dell'altra, il secondo contro il secondo
  e così via (squadre della stessa dimensione). Ogni abbinamento viene giocato *Cicli* volte, ogni volta due partite
  sulla stessa apertura, a colori invertiti; i colori della prima partita si alternano da una scacchiera all'altra e da
  un ciclo al successivo. *Squadra A* e *Squadra B* danno il nome alle squadre ("Squadra A" e "Squadra B" senza un
  nome); accanto, quanti motori ha ciascuna. Le squadre si formano nell'elenco dei partecipanti: la metà superiore
  dell'elenco è la squadra A, la metà inferiore la squadra B (la squadra A riceve quello dispari), finché non si
  mettono dei motori in una squadra.

### Partecipanti e libreria
<!-- en: participants-and-the-library a7ef2e55 -->

**Partecipanti**, con la **Libreria** accanto (sotto, in una finestra stretta). Ogni partecipante è una riga: il suo
posto (una stella per le teste di serie di un gauntlet), il suo nome, il suo Elo di riferimento, ciò che gioca in modo
diverso dalle impostazioni comuni ("Hash 512 MB · 80 % del tempo") e ⋯, evidenziato quando ha impostazioni proprie.
⋯, un clic destro o il tasto Menu le aprono: il suo `Hash`, i suoi `Threads`, nodi o profondità fissi, handicap di
tempo (una percentuale della cadenza, per un orologio), il suo *Libro*, *Questo motore ancora una volta, con un altro
libro*, gli spostamenti e *Rimuovi*. L'ordine dell'elenco definisce le teste di serie: trascina i partecipanti per
riordinarli (prima le teste di serie), o premi Ctrl+Su o Ctrl+Giù per mandarli in cima o in fondo, Alt+Su o Alt+Giù
per spostarli di uno. Ctrl o Maiusc clic ne seleziona diversi; Canc (o *Rimuovi*) li toglie.

In un incontro a squadre il posto è la squadra e la scacchiera, A1, A2… per la squadra A, B1, B2… per la squadra B,
nell'ordine dell'elenco (scacchiera per scacchiera, A1 gioca contro B1). Il menu ⋯ mette il partecipante in una
squadra; *Squadra A* e *Squadra B* sotto l'elenco mettono in una di esse i partecipanti selezionati, e *Metà* rifà le
squadre dalle metà dell'elenco (la tavolozza dei comandi propone lo stesso). Un motore aggiunto quando le squadre sono
già scelte entra nella più piccola. Il riepilogo dice quanti motori ha ogni squadra, e un avvio viene rifiutato, con il
motivo, quando una squadra è vuota o, scacchiera per scacchiera, quando le squadre hanno dimensioni diverse.

La libreria elenca i motori attivi con il loro Elo di riferimento, ordinati per nome o per Elo (clicca sulla colonna),
un `!` su quelli con un problema (programma mancante o ricompilato, ultima verifica fallita: il tooltip dice quale) e
un ✓ su quelli che giocano già. Il filtro cerca nei nomi, nei nomi dichiarati dai motori, nei loro autori, programmi,
note e gruppi (Ctrl+F gli dà la tastiera; Invio al suo interno aggiunge i motori selezionati, o il primo mostrato); un
chip per gruppo mostra solo quel gruppo, e *Aggiungi tutti quelli mostrati* aggiunge l'intero gruppo. Fai doppio clic
su un motore, o selezionane diversi con Ctrl o Maiusc (Ctrl+A: tutti quelli mostrati) e premi Invio, per aggiungerli;
oppure trascinali in un punto tra i partecipanti. *Suggerisci…* seleziona gli N motori attivi che funzionano e il cui
Elo di riferimento è il più vicino a quello del primo partecipante (il motore in prova di un gauntlet), al massimo a
150 di distanza per impostazione predefinita; Invio li aggiunge. Per un match, la libreria riempie A, poi B (B viene
sostituito quando entrambi sono scelti), e un motore trascinato su A o B ne prende il posto.

### Libri propri dei partecipanti
<!-- en: participants-own-books 580e52cc -->

*Libro*: un libro di aperture proprio del partecipante, come `book=`/`bookdepth=` di Cute Chess: un libro Polyglot
(`.bin`) o PGN della pagina Libri e fino a che profondità viene seguito, in semimosse dall'inizio della partita,
compresa l'apertura del torneo (*tutto*: finché il libro ha una mossa; per un libro PGN, le prime 40 semimosse delle
sue partite). Un libro PGN viene prima trasformato in un libro Polyglot: le mosse di ogni partita fino a quella
profondità, ogni mossa pesata dal numero di partite che la giocano; le partite con una mossa illegale vengono escluse
e contate. Questo avviene in background appena il libro viene scelto (avanzamento sul pulsante *Libro*, nel suo popup
e nel riepilogo) e viene conservato nella cartella `book-cache` del profilo finché il file o la profondità non
cambiano; un avvio che arriva prima della fine lo attende. Un libro che non si può preparare mostra il motivo nel suo
popup, con *Riprova*; anche ogni avvio riprova. Un torneo gioca dal libro preparato quando è stato avviato o ripreso,
anche se nel frattempo il file PGN cambia.

Finché la posizione è in quel libro, il partecipante gioca subito la mossa del libro, scelta secondo i pesi del libro
così come li rende la *Casualità* delle aperture (anche nel popup *Libro*, la stessa per tutti i libri del torneo:
vedi [Aperture](#aperture)); poi subentra il suo motore. Le aperture del torneo, se ci sono, vengono giocate per prime,
da ogni motore; i libri EPD danno solo quelle e sono in grigio nell'elenco. Per confrontare libri con un solo motore,
scegli *Questo motore ancora una volta, con un altro libro* nel suo menu ⋯: ogni copia gioca come `<engine> (<book>)`,
così i suoi risultati e il suo rating restano separati. In un match i pulsanti *Libro* stanno accanto ad A e B, e un
motore può giocare contro se stesso con due libri. Le mosse di libro sono marcate `{book}` nel PGN; passare il mouse
su un nome nella classifica mostra il suo libro. Anche i motori WinBoard possono averne uno.

### Impostazioni dei motori
<!-- en: engine-settings 7a93d075 -->

*Hash* e *Threads* imposti a ogni motore, anche per un match (0 = quello di ogni motore; quello proprio di un
partecipante, dal suo menu ⋯, ha la precedenza); *Tablebase Syzygy per i motori* dà le cartelle delle tablebase (le
stesse dell'aggiudicazione) a ogni motore con un'opzione `SyzygyPath`, e *Libri propri dei motori disattivati*
imposta `OwnBook` a false dove esiste, così ogni partita viene giocata dalle aperture del torneo. Entrambe sono
disattivate per impostazione predefinita.

### Cadenza
<!-- en: time-control 672cb041 -->

Tempo base e incremento in secondi, mosse per periodo facoltative, e un margine prima che una partita sia persa per
tempo; un tempo fisso per mossa (`go movetime`, con lo stesso margine); un numero fisso di nodi per mossa; o una
profondità fissa in semimosse (`go depth`), con cui nessun motore può perdere per tempo.

### Parallelismo
<!-- en: concurrency c02ee2cf -->

Il numero di partite giocate contemporaneamente. In modalità automatica viene misurato all'avvio:

- nodi o profondità fissi: una partita per thread hardware, perché l'Hyper-Threading non cambia le mosse giocate;
- con un orologio: core fisici − 1, perché i motori non si rallentino a vicenda.

La riga e la linea sotto il campo dicono quale regola ha scelto il numero. *Affinità CPU* (disattivata per
impostazione predefinita; non su macOS, che non ne dispone) vincola ogni partita a un processore, condiviso dai suoi due
motori perché non pensano mai nello stesso momento. fastchess la supporta solo per motori a un thread, quindi viene
disattivata automaticamente quando un motore ha `Threads` > 1. Compare un avviso quando parallelismo × thread supera i
thread hardware (o, con un orologio, i core fisici). *Gioca su* sceglie la macchina su cui gioca il torneo (vedi [Un
torneo su una macchina](#un-torneo-su-una-macchina)).

### Aperture
<!-- en: openings 44656411 -->

Un libro EPD, PGN o Polyglot (`.bin`) (una suite di aperture), ordine casuale o sequenziale, il numero di semimosse
tenute da un libro PGN, l'indice di partenza e il seme casuale (0 = un nuovo ordine per ogni torneo). Ogni apertura
viene giocata due volte, a colori invertiti (in un match i cui *Colori* sono fissati, ogni partita dalla propria
apertura).

Un libro Polyglot viene seguito dalla posizione iniziale fino a una profondità (8 semimosse se non impostata),
escludendo le mosse che pesano meno di un decimo della migliore della loro posizione; le posizioni raggiunte vengono
scritte una volta in `<book>-<N>plies.epd` nella cartella del torneo e giocate come un libro EPD. In ordine casuale
vengono scritte nell'ordine in cui vengono estratte, ciascuna una volta: una linea arriva tanto prima quanto è più
probabile, secondo i pesi delle sue mosse, così un torneo che gioca solo una parte del libro gioca per prime le sue
linee principali; il seme estrae di nuovo lo stesso ordine. In ordine sequenziale mantengono l'ordine del libro.
*Casualità* (da −100 a 100, 0 per impostazione predefinita) dice come contano i pesi, come Polyglot e Cute Chess
scelgono una mossa di libro: 0 i pesi così come sono, −100 solo la mossa migliore di ogni posizione (una sola linea, a
meno che più mosse non condividano il peso migliore), 100 tutte le mosse allo stesso modo (le mosse rare tornano); a
metà strada, −50 eleva i pesi al quadrato e 50 ne prende la radice quadrata. Sceglie anche le mosse dei libri propri
dei partecipanti, e viene mostrata per loro anche con un libro EPD o PGN; il riepilogo la indica, e la riga sotto le
copie della classifica dà il libro con la sua profondità e, per un libro Polyglot, questa casualità.

In un girone all'italiana, un gauntlet, un sistema svizzero o un incontro a squadre, *La stessa apertura per tutte le
coppie di un turno* dà a ogni coppia di partite di un turno la stessa apertura (scacchiera per scacchiera, un ciclo è
un turno: ogni scacchiera gioca la stessa apertura), la successiva del libro a ogni turno, così nessun motore ha
aperture più facili degli altri del suo turno (un motore aggiunto dopo recupera in turni propri, ciascuno con la sua
apertura).

*Segnala le aperture sbilanciate* (disattivato per impostazione predefinita) mostra, man mano che arrivano le partite,
le aperture che decidono la partita da sole: una partita viene segnalata quando finisce se entrambi i motori hanno
visto lo stesso lato in vantaggio di almeno il punteggio indicato (200 cp per impostazione predefinita) a ciascuna
delle loro prime mosse dopo il libro (4 per impostazione predefinita; le mosse del libro proprio di un partecipante
contano come mosse di libro). Legge i punteggi dei motori, quindi un motore che non ne dà non segnala nulla; i
risultati non cambiano. La scheda *Partite* filtra queste partite (*Apertura sbilanciata*), la scheda *Aperture* segna
le loro aperture con un `!` e le conta, e la panoramica conta le partite. *Rigiocare la coppia da un'altra apertura*
annulla una coppia del genere una volta giocate le sue due partite (restano nei file PGN e nel diario, come ogni
partita annullata) e la rigioca da un'apertura alla fine del libro (nell'ordine di gioco del libro), che il torneo
giocherebbe per ultima, così le aperture delle coppie successive non si spostano; al massimo tre volte per una coppia,
dopo di che la coppia conta, segnalata. La console e la panoramica dicono quante aperture sono state sostituite.

### Chess960
<!-- en: chess960 515f3f2d -->

Fischer random, nelle aperture: senza libro, le 960 posizioni di partenza in ordine casuale (scritte una volta in
`chess960.epd` nella cartella dei tornei); con un libro, le sue posizioni. Ogni motore deve giocare a Chess960: i
motori UCI con l'opzione `UCI_Chess960`, i motori WinBoard con la variante `fischerandom`. Il riepilogo nomina quelli
che non lo fanno, e un torneo del genere non parte.

### Aggiudicazione
<!-- en: adjudication a18dca1e -->

Patta (da un numero di mossa, per N mosse consecutive, sotto una valutazione), abbandono (N mosse consecutive sopra
una valutazione, facoltativamente con entrambi i motori d'accordo), un numero massimo di mosse, e tablebase Syzygy.

### PGN e varie
<!-- en: pgn-and-misc 6762bab8 -->

- Nome dell'evento: il suo tooltip nomina i due file PGN in cui viene scritta ogni partita.
- *Aggiungi anche ogni partita a*: un tuo file PGN a cui ogni torneo aggiunge le sue partite, come `-pgnout` di
  cutechess (mai sovrascritto, nello stile di commento ChessBase / Lichess o cutechess), nell'ordine del calendario: le
  due partite di un'apertura una accanto all'altra, i turni in ordine, in qualunque ordine finiscano. Una partita
  attende lì finché quelle precedenti non sono scritte; una ripresa continua; quando il file non si può scrivere
  (bloccato, spostato), le partite attendono e il torneo prosegue, con un avviso nella panoramica. Un crash o
  un'interruzione di corrente mentre le partite vengono aggiunte non le scrive mai due volte: la ripresa controlla il
  file e completa una scrittura interrotta, e non vi toglie mai nulla.
- Campi aggiuntivi nei commenti delle mosse (nodes, seldepth, nps, pv, tbhits, e il tempo di decisione `dt=`: quando il
  motore si è fissato sulla mossa che ha giocato; `wdl=W/D/L` viene sempre scritto per i motori che lo forniscono),
  attivi per impostazione predefinita perché i grafici della scheda *Partite* li leggono (pv rende il PGN
  sensibilmente più grande).
- *Riavvia un motore che va in crash*, e *Ferma il torneo quando un motore non parte* (disattivato per impostazione
  predefinita: vedi [Un motore che non parte](#un-motore-che-non-parte)).
- Conservare il log del motore di ogni partita (viene sempre conservato per le partite finite male).
- *Verifica le PV di matto*: la PV di una valutazione di matto deve avere la lunghezza giusta e finire con un matto.
  Ogni PV viene verificata per mosse illegali, mosse dopo la fine della partita e una mossa migliore che non è la sua
  prima mossa; le mosse con una PV errata sono segnate nella scheda Partite e scritte nel log.

### Consigli per le impostazioni
<!-- en: tips-for-the-settings 62afdb4d -->

- **Confrontare due versioni di un motore**: un SPRT, miglioramento [0, 3] per una modifica pensata per guadagnare,
  non regressione [-5, 0] per una semplificazione o una pulizia del codice. Prima una cadenza breve (10+0.1), poi una
  lunga (60+0.6) per ciò che passa. Usa un libro di posizioni equilibrate, non la posizione iniziale: altrimenti ogni
  partita sarebbe una di poche aperture.
- **Parallelismo**: lascialo automatico. Con un orologio, più partite dei core fisici meno uno fanno rallentare i
  motori a vicenda, e i risultati si falsano; con nodi o una profondità per mossa, può giocare ogni thread hardware.
- **Threads**: 1 per motore per i test; molte partite da un thread ciascuna danno più risultati all'ora di poche
  partite da molti thread. Dai a ogni motore gli stessi `Threads` e `Hash` in *Impostazioni dei motori*.
- **Hash**: abbastanza per la cadenza (da 16 a 64 MB a 10+0.1, 256 MB a 40/15 e oltre); il riepilogo dice quando
  l'hash delle partite non entra più nella memoria.
- **Aggiudicazione**: il profilo standard fa risparmiare tempo nei tornei lunghi senza cambiare molto i risultati;
  lasciala disattivata per testare i finali di un motore, o usa le tablebase.
- **Aperture**: l'ordine casuale con un seme fisso permette di rigiocare un test esattamente; *La stessa apertura per
  tutte le coppie di un turno* è più equa in un girone all'italiana con pochi cicli.
- **Più tornei**: dai a quello che conta di più un peso maggiore invece di mettere in pausa gli altri (vedi [Più tornei
  insieme](#più-tornei-insieme)).

## Svolgimento di un torneo
<!-- en: running-a-tournament 9bcba803 -->

Un torneo in corso o aperto ha uno spazio di lavoro con un'intestazione (nome, avanzamento, *Pausa*, *Ferma*,
*Clona*, *Cartella*, *Layout*) e dei pannelli, mostrati per impostazione predefinita come schede:

| Scheda | Cosa mostra |
|---|---|
| Panoramica | I risultati fin qui e le statistiche, i grafici, la checklist dell'invio |
| Classifica | La classifica come tabella o tabella incrociata, gli avversari di un motore, le copie per un forum |
| Squadre | Un incontro a squadre: il punteggio delle squadre e la tabella incrociata delle due squadre |
| Tabellone | Un'eliminazione diretta: il tabellone, turno per turno |
| Calendario | Ogni coppia di partite, e le modifiche al torneo |
| Trasmissione | File in diretta, il server TLCS, la pagina web e lichess |
| Diretta | Le partite in corso |
| Partite | Le partite terminate, i loro filtri, la loro riproduzione e la loro esportazione |
| Aperture | I risultati per posizione di partenza |

### Panoramica
<!-- en: overview 6985cebb -->

**Un match**: Elo e nElo con il loro intervallo di confidenza al 95 %, LOS, punteggio, conteggi pentanomiali, limiti
dell'SPRT e LLR. *Statistiche* (ripiegato) dà i numeri di fishtest calcolati sulle partite e sulle coppie: punteggio,
Elo, nElo e BayesElo con DrawElo, deviazione standard per partita, LLR di entrambi, il rapporto di varianza tra coppie e
partite singole, il bias RMS delle aperture, i tipi di coppie in equilibrio e le coppie vinte su quelle perse, che
*Copia* include. Poi i grafici: Elo e LLR nel tempo, lunghezza delle partite, come sono finite le partite e *Ricerca per
motore*.

**Un torneo**: la classifica (con *TF*, le sconfitte per tempo di ogni motore, in arancione appena ce n'è una; un *!*
accanto a un motore che è andato in crash o ha giocato una mossa illegale), *Ricerca per motore* sotto di essa,
l'avanzamento per ciclo o per turno, le partite all'ora e il tempo rimanente, gli ultimi risultati.

*Ricerca per motore* dà la media di ogni motore sulle mosse che ha cercato, escluse le mosse di libro: profondità, NPS,
tempo per mossa, tempo di decisione e uso del processore; un clic su una colonna ordina i motori secondo quella. Accanto
ai suoi NPS, ×4 dice che il motore cerca con 4 thread: conta più nodi che con uno solo senza essere per questo più
veloce, quindi confronta i suoi NPS con quelli dei motori che ne hanno altrettanti. Gli NPS richiedono motori che li
forniscano, e *nps* in *PGN e varie* per essere riletti alla ripresa.

Entrambi contano le partite da aperture sbilanciate, e le aperture sostituite, quando la configurazione le cerca, con
*Mostra* per quelle partite. Un torneo terminato aggiunge una *Checklist dell'invio*: tutte le partite giocate, ogni
coppia con entrambi i colori, nessuna partita duplicata, nessun crash, nessuna sconfitta per tempo né mossa illegale
(con pulsanti che aprono quelle partite), l'hash di ogni motore (mostrato, non giudicato), ponder disattivato, il
libro di aperture, le tablebase.

### Classifica
<!-- en: standings e4198fcd -->

Punti, partite, sconfitte per tempo (*TF*), vittorie/patte/sconfitte, rating con il loro intervallo di confidenza,
Sonneborn-Berger (Buchholz per un sistema svizzero), turni di riposo. *Tabella* li elenca un motore per riga;
*Tabella incrociata* (gironi all'italiana, fino a 200 motori) mostra ogni abbinamento. *Ordine:* per punti, punteggio %
o ordine iniziale (che tiene al loro posto le colonne della tabella incrociata di un gauntlet). Un clic su un motore
elenca sotto i suoi avversari: partite, con il Bianco, con il Nero, + = −, punti e %, con segnate le coppie di cui è
stata giocata una sola partita; un gauntlet si apre con il suo motore testato selezionato.

*Copia* come testo, o come BBCode per un post in un forum (con una riga che indica formato, cadenza, partite e libro),
e in un gauntlet *Tabella del gauntlet come testo* o *in BBCode, per un forum* (il motore testato, poi i suoi
avversari secondo i loro punti, ciascuno con punti, %, Elo ±, W-L-D); *Esporta HTML…*. Tutti hanno *TF* come la
classifica della modalità headless, seguono l'ordine scelto, e finiscono con una riga che indica le partite giocate su
quelle previste, la percentuale di patte, la cadenza, il libro (con le semimosse tenute di un libro PGN; per un libro
Polyglot la sua profondità e come sono state scelte le sue mosse, per esempio `book:Perfect2023.bin (8
plies, moves by weight)`) e l'evento. Le copie poi firmano con *fastchess-gui* e la sua versione, e la tabella del
gauntlet è rigata con = invece di -, così una tabella pubblicata in un forum si distingue da quelle di altri
programmi.

### Squadre
<!-- en: teams 8c845645 -->

Solo un incontro a squadre, nella scheda *Classifica* (dove si apre) e in cima alla *Panoramica*: il punteggio delle
squadre, "Lynx 17½ – 18½ Otter", con le partite giocate, il punteggio % della squadra A, la differenza Elo che quel
punteggio rappresenta con il suo intervallo al 95 % e il LOS, contati sulle coppie di partite come le statistiche di
un match (pentanomiale); poi i motori di ogni squadra con le loro partite, punti, %, rating e i punti che ciascuno ha
fatto contro ogni motore dell'altra squadra (B1, B2… come colonne, il nome al passaggio del mouse): la tabella
incrociata delle due squadre. *Tabella* e *Tabella incrociata* mostrano i motori come in qualsiasi torneo. *Copia* ed
*Esporta HTML…* danno i punteggi delle squadre e i motori delle due squadre gli uni contro gli altri, con la riga
sotto di essi.

### Tabellone e calendario
<!-- en: bracket-and-schedule fe207ac0 -->

*Tabellone*, solo per un'eliminazione diretta: il tabellone, turno per turno, con il punteggio di ogni match.
*Calendario*: ogni coppia di partite per ciclo o per turno, con il suo stato e l'apertura che gioca (una volta
iniziata, o una volta iniziato il suo turno quando il turno ne condivide una), e le modifiche al torneo (vedi
[Modificare un torneo in corso](#modificare-un-torneo-in-corso)).

### Diretta
<!-- en: live 5561ed9b -->

Le partite in corso, come un mosaico di scacchiere o una sola partita in primo piano con gli orologi, la ricerca in
corso di ogni motore (profondità, valutazione, nodi, nps, PV in SAN), l'elenco delle mosse e il grafico della partita
(vedi [Il grafico della partita](#il-grafico-della-partita)): le valutazioni dei due motori, e nelle sue schede le loro
probabilità di vittoria, il tempo per mossa con il tempo di decisione, la profondità con la seldepth, i nodi, la
velocità, gli accessi alle tablebase, quanto distano, o quante semimosse una PV ripete di ciò che la PV dell'altro
motore aveva previsto. Le quote di vittoria/patta/sconfitta (`UCI_ShowWDL`, attivato per i motori che lo hanno quando
vengono aggiunti) seguono ogni riga di ricerca; la scheda *V/P/S* le disegna come fasce, le vittorie del Bianco in
basso.

Quando c'è spazio (una partita in primo piano, un pannello grande), ogni motore ha il suo riquadro: il logo alto quanto
il nome e l'orologio, cosa fa (pensa, o la mossa che ha giocato), la sua ricerca cifra per cifra (profondità e seldepth,
valutazione, nodi, velocità, gli accessi alle tablebase e il riempimento dell'hash quando il motore li invia, il tempo
speso sulla mossa finora) e tutta la sua PV; il motore che attende mostra ciò che diceva quando ha giocato, con il
momento in cui si è deciso. Accanto al mosaico, il pannello tiene una riga di cifre e le prime 12 semimosse della PV.
*Valutazione del lato al tratto* | *Valutazione del Bianco*, a destra della scheda, sceglie il lato di tutte le sue
valutazioni: quello del lato al tratto, come fastchess le scrive nel PGN, o quello del Bianco; la barra di valutazione e
lo schermo grande mostrano sempre quello del Bianco.

Sopra le mosse: l'apertura, il codice ECO e il nome della posizione più avanzata della partita che la tabella delle
aperture con nome conosce (mosse del libro e dei motori allo stesso modo, così che anche una trasposizione dopo il libro
abbia un nome), con l'ultima mossa del libro; e appena le tablebase Syzygy del torneo (quelle della sua aggiudicazione,
altrimenti quelle date ai motori) conoscono la posizione mostrata, il loro verdetto: il lato che vince con la DTZ (le
semimosse fino alla prossima cattura o mossa di pedone: le tablebase Syzygy non hanno la distanza dal matto), o la
patta, con la regola delle 50 mosse contata dal contatore della partita salvo che l'aggiudicazione la ignori. Ogni
posizione viene interrogata una volta, fuori dai thread che giocano le partite. Nell'elenco, ogni mossa di un motore ha
la sua valutazione, profondità e tempo; passandoci sopra si aggiungono il momento in cui il motore si è deciso e la
risposta che si aspettava (la seconda mossa della sua PV: fastchess gioca senza ponder). Le mosse del libro dicono
*libro*, una riga segna la fine del libro, e la mossa su cui si pensa dice *pensa*.

Il mosaico ha una scacchiera per ogni partita in corso, non per ogni partita che il parallelismo consente: verso la
fine di un torneo, o con meno partite di così, le scacchiere rimaste si ingrandiscono. Una scacchiera la cui partita è
finita resta 10 secondi (la partita successiva del suo posto inizia lì), quella della partita seguita resta, e quando
non si gioca nulla restano le ultime partite. Quando le scacchiere richiederebbero una barra di scorrimento, il mosaico
toglie larghezza alla partita in primo piano, fino a lasciarle il 40 % della pagina.

Sotto la scacchiera: il contatore delle 50 mosse (semimosse dall'ultima cattura o mossa di pedone, su 100), la
differenza di materiale con i pezzi che una parte ha in più, e i pezzi rimasti, marcati *TB* quando l'aggiudicazione
con le tablebase può terminare la partita; *Dettagli sotto le scacchiere* li aggiunge al mosaico. Sotto di essi, i
pulsanti per scorrere le mosse (vedi [Le scacchiere e i loro pulsanti](#le-scacchiere-e-i-loro-pulsanti)): la
partita in primo piano, o una scacchiera staccata, si può riportare indietro mentre prosegue (anche con ← → Inizio
Fine, un clic su una mossa dell'elenco o sulla curva), con la posizione precedente mostrata e la sua mossa segnata
nell'elenco e sulla curva; *Torna alla mossa attuale* (o Fine, o un clic sull'ultima mossa) la riporta a seguire la
partita, come avviene a ogni nuova partita.

*Frecce* (attivo per impostazione predefinita) disegna la mossa a cui pensa il lato che muove nel colore del suo
motore e la risposta che si aspetta, più chiara, nel colore dell'avversario, sulla scacchiera in primo piano e su
quelle staccate (non sul mosaico), quando mostra la mossa attuale. In un gauntlet, *Motore testato in basso* (attivo
per impostazione predefinita) mostra ogni nuova partita dal lato del motore testato; ruotare la scacchiera (F, o il suo
pulsante) la capovolge comunque fino alla partita successiva. *Stacca* apre una scacchiera in una finestra propria. Un
clic destro su una scacchiera termina la sua partita con un risultato (vedi [Modificare un torneo in
corso](#modificare-un-torneo-in-corso)). *Schermo grande* mostra la partita seguita a tutta finestra (vedi [Schermo
grande](#schermo-grande)).

### Schermo grande
<!-- en: big-screen 14c8f0d6 -->

Per una televisione, un circolo o uno stream, *Schermo grande* (nella scheda *Diretta*, su una scacchiera staccata, o
nella tavolozza dei comandi) mostra una partita a tutta finestra, chiaro su nero: i nomi dei giocatori e gli orologi a
caratteri grandi (il lato che muove e il suo orologio che scorre in giallo, un orologio sotto i dieci secondi in rosso),
la barra di valutazione, la valutazione, la profondità e la velocità della ricerca, l'ultima mossa incorniciata sulla
scacchiera e scritta in grande, l'apertura, le linee dei motori (*Linee*, attivo per impostazione predefinita: quella
del lato al tratto mentre cerca, in giallo, quella dell'altro dalla sua ultima mossa, al massimo due righe ciascuna),
il verdetto delle tablebase appena conoscono la posizione, e le mosse a caratteri grandi, con l'ultima segnata; il risultato sopra la scacchiera
quando la partita è finita. Il suo testo segue la dimensione della finestra e ha una dimensione propria (*A-* / *A+*,
o i tasti + e -). Mostra la partita seguita, o passa da una partita in corso all'altra ogni N secondi (*Ogni partita a
turno*), o segue *La più interessante*: la più vicina alla parità, la più lunga tra quelle altrettanto vicine, tenuta
finché resta tale. I controlli compaiono per tre secondi quando il mouse si muove (il puntatore si nasconde con essi);
← e → cambiano partita, F11 porta la finestra a schermo intero, Esc esce. Una scacchiera staccata spostata su un altro
schermo (la televisione) lo mostra nella sua finestra, con F11 che porta quella finestra a schermo intero mentre la
finestra principale continua; agganciata nella finestra principale, lo mostra la finestra principale.

### Partite
<!-- en: games ba4a9604 -->

Le partite terminate con la loro apertura (codice ECO e nome), e una scacchiera di riproduzione: i pulsanti sotto di
essa, ← → Inizio Fine e F, o un clic su una mossa o sul grafico; gli stessi dettagli sotto di essa e lo stesso grafico
della partita che in *Diretta* (vedi [Il grafico della partita](#il-grafico-della-partita)), dai campi con cui è stato
scritto il PGN: un grafico il cui campo il torneo non ha scritto si trova nel menu `⋯` del grafico della partita, in
grigio, e lo dice con un pulsante verso le opzioni PGN della configurazione.

**Filtri**: per risultato (vittorie, patte, sconfitte, sconfitte per tempo, crash e mosse illegali, aggiudicazioni, PV
errate, e le partite di un'apertura segnalata come sbilanciata quando la configurazione le cerca); per ciò che dicono
le valutazioni (i motori sono in disaccordo di N pedoni per 3 mosse ciascuno di fila, la valutazione di un motore
oscilla di N pedoni da una sua mossa alla successiva, chi ha perso pensava di essere N pedoni meglio; la partita si apre
in quel momento); e per apertura (un codice ECO o il suo inizio, un intervallo come `B20-B99`, o parole del nome).

**Esportazione** di una, di tutte o delle partite filtrate: *Esporta tutto…* propone `<name>.pgn` accanto alla
cartella del torneo; i file PGN propri del torneo vengono rifiutati come destinazione. *Esporta per una lista di
rating…* scrive le partite come le inviano i tester delle liste di rating (per esempio CCRL):
`[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, anche uno zip se richiesto; i giocatori nominati
come li scrivono le liste, `<engine> 64-bit 4CPU` (modificabile per ogni partecipante), i turni rinumerati, i tag
`Event` e `Site` impostati, le partite annullate e i duplicati esclusi, e anche quelle perse per crash o blocco a meno
che non si tolga la spunta; il `PlyCount` di una partita persa per mossa illegale conta solo le mosse scritte. Il
tester e il sito vengono ricordati.

*Log del motore* apre il log di una partita che ne ha uno (vedi [File](#file)). Un clic destro su una partita la
rigioca, la annulla o ne fissa il risultato (vedi [Modificare un torneo in corso](#modificare-un-torneo-in-corso)).

### La scheda Aperture
<!-- en: the-openings-tab c51dd29a -->

Risultati per posizione di partenza: le sue coppie di partite, il punteggio del Bianco, le patte, e quante coppie il
Bianco ha vinto due volte, il Nero ha vinto due volte (l'apertura favorisce un colore) o un motore ha vinto due volte
(distingue i motori), con la lunghezza media. Ordinabili per qualsiasi colonna, filtrate sulle sbilanciate, sulle
patte o sulle decisive, o su quelle *Segnalate come sbilanciate durante le partite* (vedi [Aperture](#aperture) nella
configurazione: marcate `!`, il loro numero accanto al conteggio, con le aperture sostituite). *Esporta come EPD…*
scrive le posizioni mostrate, per conservarle o per ripulire un libro, con *Senza le sbilanciate* (attivo per
impostazione predefinita, mostrato appena un'apertura viene segnalata) che esclude quelle segnalate. Un clic mostra la
posizione e le sue partite. In un match i cui *Colori* sono fissati, ogni partita ha la propria apertura: si contano le
partite, e le colonne e i filtri sulle coppie non vengono mostrati.

### Pannelli e layout
<!-- en: panels-and-layouts 9ed9b915 -->

Ogni scheda è un pannello. Trascinane uno per la sua linguetta per agganciarlo a un lato di un altro pannello o come
scheda accanto ad esso, o rilascialo altrove per farlo fluttuare (anche su un secondo schermo su Windows e Linux/X11,
non su Wayland né con il rendering software). La partita seguita in *Diretta* si scompone nei pannelli *Scacchiera*,
*Giocatori*, *Ricerche*, *Mosse* e *Grafico*; le parti che non hanno un pannello aperto restano accanto al mosaico in
*Diretta*, e le scacchiere staccate si agganciano come gli altri pannelli. Chiudi un pannello con la x sulla sua
linguetta; fallo tornare da *Layout* > *Pannelli*, o con *Mostra: …* nella tavolozza dei comandi, e torna agganciato.

*Layout* propone anche dei layout già pronti (*Schede*, come per impostazione predefinita; *Segui una partita*: la
scacchiera a sinistra, i giocatori, le ricerche e il grafico a destra, la classifica sotto di essi; *Torneo grande*: il
mosaico accanto alla classifica; *Trasmissione*: una scacchiera grande con i giocatori, le ricerche e la classifica),
*Salva il layout come…* (i tuoi, per nome, conservati in `layouts.json` nel profilo), *Elimina* e *Layout
predefinito*. Un pannello portato fuori dallo spazio di lavoro resta sullo schermo nelle altre pagine; la disposizione
viene conservata tra una sessione e l'altra.

### Uso del processore
<!-- en: processor-use e5234cbe -->

Mentre si giocano le partite, il tempo di processore dei processi di ogni motore (compreso il motore dietro un
adattatore WinBoard) viene misurato a ogni `go` e a ogni mossa. La Panoramica mostra, per ogni motore, i core che tiene
occupati mentre pensa e la quota di un core che usa mentre pensa il suo avversario, e avvisa quando un motore usa
chiaramente più core dei suoi `Threads`, o pensa sul tempo del suo avversario (un ponder non richiesto, un thread in
background): ciò che rende una partita sleale verso l'avversario e verso le altre partite in corso. Gli servono 20 s di
ciascuno per dirlo, e conta ogni partita giocata, nella finestra o in modalità headless (`--run`): ciò che hanno usato
i motori di ogni partita viene scritto in `processor.tsv` nella cartella del torneo, così una ripresa lo conserva. Si
limita ad avvisare; non decide mai una partita.

### Pausa, arresto e ripresa
<!-- en: pause-stop-and-resume 100c55cf -->

**Pausa** non avvia nuove coppie di partite. Le partite in corso arrivano alla fine, e così la seconda partita di una
coppia la cui prima partita è iniziata, quindi una pausa non butta mai via un risultato. **Riprendi** continua.
**Ferma** o termina prima le partite in corso, o si ferma subito (le partite interrotte non vengono contate e saranno
rigiocate).

Un torneo fermato da un motore dice quale nella sua intestazione e nella pagina iniziale, sotto *Da sistemare*:
*Interrotto: Alpha non è riuscito ad avviarsi: …* (dopo il suo ultimo tentativo), o *Alpha è andato in crash o non
risponde più (partita 12)* (senza *Riavvia un motore che va in crash*), con *Apri il motore* e *Apri il log*.

**Ogni torneo riprende esattamente da dove si è fermato**, dopo un arresto, una finestra chiusa, un crash o
un'interruzione di corrente: nessuna partita va persa e nessuna viene giocata due volte, e una coppia di partite giocata
a metà viene completata con la stessa apertura e gli stessi colori. *Riprendi* nella pagina iniziale lo prosegue, con
le statistiche delle partite già giocate: risultati, e profondità, tempo per mossa e NPS dei motori riletti dai
commenti delle mosse (NPS solo quando il PGN lo registra, *nps* in *PGN e varie*). Quando il programma è terminato
mentre un torneo era in corso (un crash, un'interruzione di corrente, un riavvio, o l'uscita mentre era in corso),
l'avvio successivo lo riprende da solo, e la coda prosegue dopo di esso: *Riprendi all'avvio il torneo che era in
corso*, in **Impostazioni**, attivo per impostazione predefinita. Un torneo in pausa o fermato ti aspetta, e
`--autostart` avvia invece la configurazione salvata. Un partecipante il cui programma è stato ricompilato o è sparito
da quando è entrato viene nominato nell'intestazione del torneo ripreso, con *Continua* e *Pausa*: le sue prossime
partite vengono giocate dal programma com'è ora, con le opzioni che il torneo gli ha dato.

### Più tornei insieme
<!-- en: several-tournaments-at-once df7ce566 -->

Più tornei possono giocare fianco a fianco: un gauntlet accanto a un SPRT, due gauntlet alla volta. *Avvia* nella
pagina *Nuovo torneo* ne avvia un altro mentre altri giocano; *Riprendi* ne riprende uno accanto a essi. Ogni torneo
gioca in un processo proprio (`fastchess-gui --run <folder>` guidato dalla finestra), quindi un crash fa cadere solo
quel torneo, e la finestra, le sue verifiche dei motori, i test rapidi e le suite di test non aspettano più la fine di
un torneo.

Lo spazio di lavoro ha **una scheda per ogni torneo aperto**, sopra la sua intestazione: un punto mentre gioca (un
segno di pausa mentre è in pausa), il suo stato al passaggio del mouse, una croce per chiudere quello che non gioca. I
pannelli mostrano la scheda scelta; la tavolozza dei comandi elenca le altre (*Mostra: …*, *Pausa*, *Riprendi*). Un
torneo terminato aperto dall'elenco prende la scheda di uno solo consultato. La pagina iniziale ha una scheda per ogni
torneo che gioca, con il suo avanzamento, chi è in testa, *Apri* e *Pausa*, e sotto il suo **peso**, i core che le sue
partite usano su quelli che può usare, e ciò che attende la sua prossima partita; la barra *Core* in alto mostra la
parte di ogni torneo (e quella della pagina Scacchiera), e quanti core i tornei condividono. Il *Log* mostra le righe
di un torneo, della finestra, o tutte (ogni riga di un torneo riporta il suo nome); le righe di un torneo sono anche
conservate in `fastchess-gui.log` nella sua cartella.

**Condividere la macchina**: i tornei che giocano condividono un budget di core, impostato in **Impostazioni** (*Core
per i tornei*: i core fisici meno uno per impostazione predefinita). Ciascuno riceve prima l'equivalente di una
partita finché il budget lo permette, a partire dal peso maggiore; il resto va secondo il peso (un torneo di peso 2
riceve il doppio dei core di uno di peso 1), nessuno oltre il proprio parallelismo, e ciò che uno non può prendere va
agli altri. Una partita conta come i `Threads` del maggiore dei suoi due motori: uno pensa mentre l'altro aspetta. Un
torneo in pausa tiene solo ciò di cui hanno bisogno le sue partite in corso e le seconde partite delle loro coppie.
Una quota più piccola non interrompe mai una partita: il torneo gioca meno partite dalla successiva. Un torneo da solo
gioca tante partite quante ne dice il suo parallelismo.

**Memoria**: una partita parte solo quando l'`Hash` dei suoi due motori, e ciò che ogni processo di motore occupa in
più (64 MB, e 8 MB per thread), entrano in ciò che le partite in corso lasciano del budget di memoria (*Memoria per i
motori* in **Impostazioni**, il 75 % della memoria installata per impostazione predefinita), condiviso tra i tornei.
Il riepilogo della configurazione dice quante partite il budget permette di giocare insieme, e la scheda
*Affidabilità* della pagina iniziale quando delle partite attendono memoria.

**GPU**: un motore segnato *Usa una GPU* sulla sua scheda (un motore a rete come Lc0) conta rispetto alle *GPU* di
**Impostazioni**: non giocano insieme più motori di questo tipo di quante GPU ci sono, in un torneo e tra tornei,
mentre le altre partite giocano accanto a essi. Il riepilogo della configurazione dice quante partite con un motore GPU
ciò permette, e avvisa quando non è contata nessuna GPU, o quando una partita tra due motori GPU richiede più GPU di
quante ne abbia la macchina (allora gioca da sola).

**Lasciarli in corso**: chiudere la finestra mentre dei tornei giocano chiede cosa fare. *Lasciarli in corso e uscire*
(la scelta predefinita) lascia ciascuno a giocare nel suo processo; la finestra li mostra di nuovo, al punto in cui
sono, al suo avvio successivo. *Ferma ed esci* li ferma, ciascuno da riprendere esattamente da dove si è fermato. Il
test rapido si ferma in entrambi i casi. Un torneo il cui processo non c'è più all'avvio successivo (un riavvio, un
crash) viene ripreso come qualsiasi torneo interrotto. La trasmissione della scheda *Trasmissione* gira nella
finestra, per il torneo mostrato: per una trasmissione che continui senza la finestra, avvia il torneo da un terminale
con le opzioni di trasmissione (vedi [Modalità headless](#modalità-headless)).

### Un motore che non parte
<!-- en: an-engine-that-cannot-start 765a7c61 -->

Una notte lasciata da sola non si ferma per un motore. Un motore che non parte (un programma spostato o ricompilato,
un file di rete mancante, un disco sparito) viene riprovato tre volte, a 10 s di distanza; poi le sue partite vengono
messe da parte e le altre partite proseguono. Viene riprovato ogni 5 minuti, una partita alla volta, e le sue partite
tornano appena parte. L'intestazione del torneo e la pagina iniziale (sotto *Da sistemare*) dicono quale motore,
l'errore, e propongono:

- **Riprova**: subito, invece che al prossimo tentativo (dopo aver sistemato il programma, per esempio).
- **Ritira…** e **Sostituisci…** (gironi all'italiana, gauntlet e incontri a squadre): il pannello delle modifiche
  della scheda Calendario, impostato per quel motore, con l'impatto mostrato prima che qualcosa cambi.
- **Log del motore**: `logs/start-<engine>.log` nella cartella del torneo, ciò che è stato inviato ai motori del suo
  ultimo tentativo e ciò che hanno risposto, compreso il loro output degli errori (spesso il motivo: una libreria o un
  file di rete non trovati).

Il log e la notifica del desktop dicono quando un motore viene messo da parte. Il torneo si ferma solo quando non resta
nulla da giocare senza i motori messi da parte, dopo un ultimo tentativo per ciascuno; il suo stato e la notifica
alla fine lo dicono (*Fermato: non resta nessuna partita da giocare senza …*), e la coda prosegue con il torneo
successivo. *Riprendi* riprova ogni motore da capo. Una pausa trattiene i tentativi. *Ferma il torneo quando un motore
non parte*, in *PGN e varie* della configurazione, lo ferma invece subito, una volta esauriti i tentativi.

### Modificare un torneo in corso
<!-- en: changing-a-running-tournament aebe63e2 -->

Il pannello **Modifica il torneo** della scheda *Calendario* funziona senza fermare nulla: le partite in corso
proseguono e nessuna partita giocata va persa. Ogni modifica mostra il suo impatto prima di essere applicata.

- **Aggiungi un motore**: gioca i cicli già iniziati (*Recupera*) o entra dal ciclo successivo.
- **Ritira un motore**: le sue partite rimanenti vengono eliminate; le sue partite giocate fin qui contano o no.
- **Sostituisci un motore**: le sue partite smettono di contare e vengono rigiocate dal nuovo motore.
- **Numero di cicli**: di più o di meno.
- **Rigioca partite**: tutte le partite di un motore, o le sconfitte per tempo e i crash.
- Clic destro su una partita (qui, o nella scheda *Partite*): rigiocarla, annullarla (senza rigiocarla), o fissarne il
  risultato.

Un motore aggiunto o entrato come sostituto gioca con le impostazioni del torneo (la sua cadenza, Hash e Threads,
mostrati sotto il motore), o con *Impostazioni proprie*: Hash, Threads, quota del tempo (o nodi, profondità), come le
impostazioni proprie di un partecipante nella pagina *Nuovo torneo*. Un torneo creato prima della 0.6.6 non registrava
le sue impostazioni: le sostituiscono quelle del suo primo motore, che potevano essere le impostazioni proprie di quel
motore.

In un incontro a squadre un motore aggiunto entra nella squadra scelta e gioca contro ogni motore dell'altra squadra;
scacchiera per scacchiera non si può aggiungere un motore (la sua scacchiera non avrebbe avversario): sostituiscine
uno invece. Un sostituto prende la squadra e la scacchiera del motore che sostituisce; più cicli abbinano i motori che
giocano ancora (scacchiera per scacchiera, sulle scacchiere del primo ciclo). Le partite giocate da un motore ritirato
o sostituito contano ancora per la sua squadra, a meno che non vengano annullate.

**Fissa il risultato** decide una partita che il programma non ha saputo decidere bene: un blocco che non ha
rilevato, un finale vinto in cui i motori girano a vuoto, una partita finita per un motivo sbagliato. Scegli 1-0, ½-½
o 0-1 e indica un motivo; i punti che sposta vengono mostrati prima che *Fissa il risultato* lo applichi. La
classifica, la tabella incrociata, i rating e la checklist dell'invio contano il nuovo risultato (un'aggiudicazione:
una sconfitta per tempo cambiata in patta non conta più come tale); la scheda *Partite* la segna con un `*`, con il
tooltip che indica il motivo e come è finita la partita giocata, e *Torna al risultato giocato* torna indietro. Una
partita in corso si termina allo stesso modo: clic destro sulla sua scacchiera nella scheda *Diretta*, *Termina con un
risultato* (anche nella tavolozza dei comandi per la partita seguita). Il motore al tratto viene fermato, fastchess
termina subito la partita e questa conta con il risultato scelto; il motore riparte per la sua partita successiva.
Richiede *Riavvia un motore che va in crash* (configurazione, *PGN e varie*, attivo per impostazione predefinita).

Le partite annullate e i risultati fissati a mano restano nei file PGN e nel diario come sono stati giocati: ai file
PGN si aggiunge soltanto. Ciò che legge le partite li applica: le partite annullate vengono escluse, e un risultato
fissato a mano compare in ogni esportazione (*Esporta la partita…*, *Esporta tutto…*, *Esporta per una lista di
rating…*, l'esportazione degli scontri diretti della pagina *Classifiche*, il file di *Aggiungi anche ogni partita a*)
con `[Termination "adjudicated by the operator"]` e il motivo in un commento prima del risultato. **Annulla l'ultima
modifica** viene proposto finché non inizia la partita successiva: sotto la modifica non è ancora stato giocato nulla,
quindi può essere annullata esattamente (le partite già in corso quando è stata fatta vengono conservate). Un risultato
fissato a mano si può annullare finché è l'ultima modifica, che siano state giocate partite da allora o no (in
un'eliminazione diretta, i cui turni successivi dipendono dai risultati, finché non inizia la partita successiva); la
scheda *Partite* propone anche *Annulla* accanto alla partita.

### Coda e rilancio
<!-- en: queue-and-relaunch 382fd33e -->

La **Coda** nella pagina iniziale contiene tornei da giocare uno dopo l'altro: quando uno finisce da solo, parte il
successivo (non dopo un arresto o un errore, né mentre *Metti tutto in pausa* trattiene la coda). Con *Avvia quando ci
sono core liberi*, il successivo parte accanto a quelli che giocano appena il budget di core supera ciò che essi
possono prendere, ciascuno al suo parallelismo. Si possono riordinare o rimuovere, e avviare subito il primo quando non
è in corso nulla. La pagina iniziale dice quando finisce il torneo in corso (*Finisce verso HH:MM (tra …)*) e quando
finisce l'intera coda, ogni torneo in coda con la sua durata e, finché le durate di quelli che lo precedono sono note,
quando parte; i tornei la cui durata non si può conoscere (un SPRT senza massimo, nodi o profondità per mossa) vengono
contati a parte.

**Rilancia con…** su un match terminato (nella sua intestazione, o sotto *Ultimi terminati* nella pagina iniziale) lo
rigioca con un altro motore A, tipicamente la versione successiva, contro lo stesso motore B, con le altre
impostazioni del match (quelle della pagina *Nuovo torneo* per un match avviato da una versione precedente, che non le
conservava).

**Clona** (nell'intestazione di un torneo, sotto *Ultimi terminati* e in *Tutti i tornei* nella pagina iniziale) apre
la pagina *Nuovo torneo* compilata con la configurazione di quel torneo, qualunque sia il formato: i partecipanti con le
loro impostazioni, la cadenza, le aperture, l'aggiudicazione e il resto, ogni valore com'è stato giocato. I suoi
profili sono indicati per nome, e un profilo modificato da allora è segnato come cambiato per questo torneo. Cambia ciò
che deve essere diverso (un'altra cadenza, la versione successiva di un motore, un altro libro), poi **Avvia** o *Metti
in coda*. Anche un torneo ancora in corso si può clonare; uno avviato da una versione precedente, che non ne conservava
la configurazione, no.

## Classifiche
<!-- en: ratings 3e4be48f -->

La pagina **Classifiche** calcola rating di massima verosimiglianza (il modello logistico, le patte come mezzo punto,
come fa Ordo per impostazione predefinita) su tutte le partite di tutti i tornei del profilo, e dei file PGN importati.
I motori sono identificati dal loro nome. Le partite contate si possono restringere in cima alla pagina, per ogni
scheda:

- una cadenza, un gruppo della libreria dei motori (le partite tra i suoi motori), gli ultimi 30 o 365 giorni;
- *Partite importate*: le partite dei file PGN elencati in *File PGN…* (il download di una lista Elo, le partite di un
  altro programma), lette dalle loro intestazioni; i loro motori sono riconosciuti dai nomi che dà loro il PGN, e un
  nome che è quello di un motore della libreria, maiuscole e spazi a parte ("stockfish  17" per "Stockfish 17"), è quel
  motore: le sue partite contano come quelle del motore e il suo Elo di riferimento può ancorarle. Nulla di più
  approssimativo viene applicato da sé: "Stockfish 17 64-bit" resta un motore separato finché non dici quale motore è
  in *Giocatori PGN…* (accanto al conteggio dei motori della libreria, o in *File PGN…*). Quella finestra elenca i
  giocatori che non sono un motore della libreria per nome, con una ricerca: per ciascuno, *Un motore a sé* (il
  predefinito) o un motore della libreria. Un nome che hanno due motori della libreria, maiuscole a parte, viene
  mostrato nel colore di avviso perché tu scelga; un motore probabile (il cui nome è l'inizio di quello del giocatore)
  viene suggerito come *Forse …?*, mai applicato. La finestra si apre da sola dopo l'aggiunta di un file PGN quando c'è
  qualcosa da decidere. Le scelte, compreso un motore a sé, vengono conservate con il motore (seguono una
  ridenominazione) e valgono per ogni file PGN e per le liste Elo importate per l'Elo di riferimento; le partite vengono
  rilette con esse quando la finestra si chiude. La lista Elo dice quanti motori della libreria contengono i file PGN,
  il suo tooltip quali;
- *Ancora*: la media a 0, o un motore a un rating fisso (per impostazione predefinita il primo motore con un Elo di
  riferimento).

### Lista Elo
<!-- en: rating-list c726b55e -->

La lista, con un numero minimo di partite. Oltre a Elo, ± e partite, ogni motore mostra il suo punteggio, le sue
vittorie (+), patte (=) e sconfitte (−), la sua percentuale di patte, *Avv.*: il rating medio degli avversari che ha
incontrato, partita per partita, *LOS*: la probabilità che sia più forte del motore successivo della lista, calcolata
dai due rating e dalla covarianza dei loro errori (approssimata oltre i 1000 motori, come dice il suo tooltip), e
*Distacco*: i suoi punti Elo in più rispetto al motore successivo. Un clic sull'intestazione di una colonna ordina per
essa (# mantiene il posto per rating); un clic destro mostra o nasconde le colonne (*Distacco* è nascosta all'inizio).
Un clic su un motore apre la sua scheda.

*Copia* come testo o BBCode, con *Con intestazione* per mettere sopra la lista l'intestazione descritta sotto.
*Esporta…*: CSV con tutte le colonne; una pagina HTML (chiara o scura) e Markdown (una tabella che GitHub e i forum
mostrano come tale), entrambe aperte da un'intestazione che dice come è stata fatta la lista: i tornei e i file PGN
contati, le partite con le vittorie del Bianco e del Nero, le patte e il punteggio del Bianco, l'ancora, i filtri, il
numero minimo di partite, la data e la versione; e *Partite per Ordo…*: le partite contate come un PGN di sole
intestazioni, che Ordo e BayesElo leggono (`ordo -p ratings.pgn -o ratings.txt`).

### Scheda del motore
<!-- en: engine-card 80442e81 -->

La scheda di un motore: il suo rating, i suoi risultati complessivi e per cadenza, le sue sconfitte per tempo, i crash
e le mosse illegali (ogni partita si apre nel suo torneo), i suoi tornei (ciascuno si apre), la sua profondità media,
il tempo per mossa e gli NPS letti dai commenti delle mosse, e il suo rating dopo ogni torneo. La pagina *Motori* vi
porta con *I suoi risultati…*.

### Scontri diretti
<!-- en: head-to-head 1021ba2f -->

Due motori l'uno contro l'altro: il punteggio, Elo e LOS, i conteggi pentanomiali delle coppie di cui hanno giocato
entrambe le partite, per cadenza e per torneo, le loro partite (ciascuna si apre nel suo torneo) ed *Esporta le
partite…*.

### Avanzamento
<!-- en: progress e2ea28fc -->

Il rating dei motori spuntati dopo ogni torneo che hanno giocato, su un asse temporale: le versioni di un motore una
accanto all'altra.

## Trasmissione
<!-- en: broadcast c2f489e7 -->

La scheda *Trasmissione* di un torneo in corso lo pubblica mentre gioca; l'istantanea viene aggiornata due volte al
secondo. *Partita mostrata* sceglie la partita inviata a TLCS e alla pagina web: automatica, o quella scelta nella
scheda *Diretta*.

### File in diretta
<!-- en: live-files 09341896 -->

Scritti in modo atomico in una cartella a tua scelta, da pubblicare dove vuoi (FTP, rsync, un sito web): `live.pgn`
(le partite in corso), `standings.html` (classifica e tabella incrociata), `tournament.json` (tutto, per gli script).

### Server TLCS
<!-- en: tlcs-server 712f28cb -->

Porta UDP, 16001 per impostazione predefinita: il protocollo del Tom's Live Chess Server, così TLCV e gli altri
visualizzatori TLCS possono seguire il torneo (scacchiera, orologi, PV, tabella incrociata e partite giocate). È
verificato con node-tlcv. *Una porta per partita* mette ogni partita in contemporanea su una porta propria: la partita
n. 1 della scheda *Diretta* sulla porta UDP, la n. 2 sulla successiva, e così via. Ogni porta si apre quando inizia la
sua partita, con i propri limiti di client e di accessi, e riceve la tabella incrociata dopo ogni partita; *Partita
mostrata* allora non si applica.

### Pagina web
<!-- en: web-page 1d4c78b3 -->

Porta TCP: una pagina servita da fastchess-gui stesso, per qualsiasi browser sulla rete, in sola lettura e aggiornata
ogni secondo. La partita seguita (la prima in corso, o quella cliccata) è al centro: la scacchiera con l'ultima mossa,
un re sotto scacco, le coordinate e le frecce; l'orologio di ogni giocatore a caratteri grandi; la valutazione di ogni
motore a caratteri grandi con chi sta pensando, la sua ricerca a riquadri (profondità/profondità selettiva, nodi,
velocità, tempo, accessi alle tablebase, hash) e la sua linea di gioco; e le mosse, con le mosse di libro in corsivo.
Scorri le mosse con le frecce sotto la scacchiera o i tasti ← → (▶ oltre l'ultima mossa torna alla partita mentre
viene giocata), ruota la scacchiera con ⇅ o F, e copia il PGN della partita o il FEN della posizione.

Accanto alla partita, delle schede mostrano la classifica e la tabella incrociata, i grafici della partita seguita, la
ricerca di ogni motore in media per mossa con le sue anomalie, le prossime partite del calendario (un sistema svizzero
o un'eliminazione diretta abbina i turni successivi man mano), le statistiche e le anomalie delle partite giocate, e
l'evento. Ogni partita in corso è mostrata sotto, con una piccola barra di valutazione accanto a ogni giocatore.
*Moves* alterna le mosse tra lettere (Nf3) e figurine (♘f3), e il tema segue il sistema o è impostato su chiaro o
scuro; il browser ricorda queste scelte. La pagina non carica nulla da altrove e funziona sui telefoni. *Download the
games (PGN)* dà il file PGN del torneo, fino all'ultima partita registrata. `state.json`, che la pagina legge, contiene
tutto per gli script. I loghi dei motori (vedi [Aspetto](#aspetto)) compaiono accanto ai loro nomi, come piccole
immagini PNG che il programma ne ricava e serve da sé; la pagina conosce solo quelli dei motori del torneo.

### lichess
<!-- en: lichess 7597e86f -->

Una trasmissione lichess, con la scacchiera, la barra di valutazione e la chat di lichess, per chiunque su internet.
Richiede un token API personale con il permesso `study:write` (*Crea un token su lichess…*), conservato solo in
`settings.json` nel profilo, salvato quando si lascia il suo campo. Senza un turno indicato, viene creata una
trasmissione con il nome del torneo, non elencata a meno che non sia spuntato *Elencata tra le trasmissioni di
lichess*; il suo link viene mostrato quando è in onda. Altrimenti le partite vanno al turno di cui si indica l'URL o
l'id (una trasmissione alimentata da invii, non da un URL sorgente). Le partite in corso vengono inviate ogni 8
secondi con i loro orologi, e ogni partita terminata una volta; un turno contiene 64 partite, dopo di che viene creato
il turno successivo della stessa trasmissione. Vengono inviate solo le partite terminate dopo che è stata attivata;
`lichess.json` nella cartella del torneo ricorda dove sono andate, così un torneo ripreso continua la stessa
trasmissione. lichess tiene solo gli orologi di ciò che riceve (scarta il `[%eval]` e il testo dei commenti delle
mosse): la sua barra di valutazione è la sua, e le valutazioni dei motori lì non compaiono.

La connessione usa Mbed TLS, integrato nel programma, e si fida solo dei certificati radice che firmano lichess.org
(Google Trust Services) e di quelli di Let's Encrypt. Passa per il proxy HTTP indicato da `HTTPS_PROXY` (o
`ALL_PROXY`), come `http://proxy.example:3128` o `http://user:password@proxy.example:3128`, tranne che per gli host di
`NO_PROXY`; anche su Windows il proxy viene da queste variabili, non dalle impostazioni di sistema. I proxy SOCKS non
sono supportati.

### Un incontro a squadre in onda
<!-- en: a-team-match-on-the-air fd32027f -->

Un incontro a squadre mostra il punteggio delle squadre ovunque: nell'intestazione della pagina web e sopra la sua
classifica (con il punteggio % della squadra A, Elo ± e LOS), in `standings.html` (le squadre, poi i motori di ogni
squadra contro quelli dell'altra), in `tournament.json` e `state.json` (`teams`, `team_elo`, `team_elo_error`,
`team_los`), e nella tabella incrociata TLCS, come una riga sotto la tabella ("Lynx 17½ - 18½ Otter · 48.6 % · Elo
-9.7 ± 31.2"). Le sue partite portano i tag `WhiteTeam` e `BlackTeam` nei file PGN, che le esportazioni conservano.

### Pubblicare la pagina web su internet
<!-- en: putting-the-web-page-on-the-internet b19f02f8 -->

La pagina web si può seguire da ovunque senza aprire una porta nel router: spunta *Pubblica su internet* sotto
*Pagina web* nella scheda *Trasmissione*. Mentre un torneo è in corso, fastchess-gui apre un tunnel SSH (libssh2,
integrato nel programma) verso un servizio di tunnel, che dà alla pagina un indirizzo https pubblico; non serve
installare nulla.

- *localhost.run* (il predefinito): gratuito, senza account. I suoi indirizzi cambiano ogni tanto, e la sua velocità
  è limitata; la pagina e il suo stato vengono inviati compressi, quindi questo conta poco. La sua chiave host è nota a
  fastchess-gui: una connessione a qualsiasi altra cosa viene rifiutata.
- *Un altro server SSH*: il suo host, porta e utente, e la porta che apre per la pagina (come chiederebbe
  `ssh -R REMOTE_PORT:localhost:PORT user@host`; 0 la lascia scegliere a lui). Si possono usare solo server che fanno
  entrare l'utente senza chiave né password (autenticazione "none"), e devono offrire curve25519-sha256, una chiave
  host RSA da 2048 a 4096 bit, e AES (GCM o CTR). La chiave del server viene accettata come fidata la prima volta e
  conservata nel profilo (`known_hosts.json`); se cambia, il tunnel si ferma e lo dice, e *Fidati della nuova chiave*
  serve per quando il proprietario del server conferma che è cambiata.

La scheda mostra lo stato (connessione in corso, in onda con il numero di connessioni, riconnessione, o un errore) e,
una volta noto, l'indirizzo pubblico con *Copia*, *Apri* (per vedere la pagina come la vedono gli spettatori) e un
codice QR per un telefono (cliccalo per ingrandirlo). Il tunnel si riconnette da solo dopo un errore, aspettando un
po' di più ogni volta, e mostra il nuovo indirizzo se è cambiato. Si ferma con la pagina web o con il torneo.

Funzionano anche altri tunnel, avviati a parte: con quello di Cloudflare, per esempio, attiva la pagina web (qui porta
8080) ed esegui `cloudflared tunnel --url http://localhost:8080`; stampa un indirizzo come
`https://some-words.trycloudflare.com`. ngrok, tailscale funnel o un reverse proxy come nginx, anche sotto un percorso
proprio (`location /live/ { proxy_pass http://localhost:8080/; }`), funzionano allo stesso modo.

La pagina è in sola lettura: risponde solo a richieste GET per la pagina, il suo stato e il file PGN, mai a un file
nominato da una richiesta, e nulla in essa modifica il torneo. Ogni richiesta riceve una risposta a sé, 32 alla volta
al massimo, e deve arrivare entro un secondo e ricevere risposta entro dieci, così un visitatore lento o silenzioso non
blocca nessun altro; dietro il tunnel la connessione di ogni spettatore conta come una di queste: 40 vengono inoltrate
insieme, altrettante attendono il loro turno, e le successive vengono rifiutate; uno spettatore che invia più di quanto
la pagina accetti viene lasciato andare da solo. Due download del file PGN avvengono insieme, e al successivo viene
chiesto di riprovare un attimo dopo. Lo stato non viene mai messo in cache, nemmeno dal tunnel, così gli spettatori
vedono le partite mentre procedono; la pagina viene conservata dal browser e ricontrollata a ogni caricamento. Gli
spettatori mostrati nella scheda *Trasmissione* sono contati dalle richieste fatte dalle pagine, quindi il conteggio è
giusto anche dietro un tunnel.

Gli spettatori vedono il nome del torneo, i nomi dei motori, i loro rating, ricerche e anomalie, le aperture e il file
PGN delle partite giocate. Le cartelle e i file dei motori restano privati. In modalità headless:

```
fastchess-gui --run FOLDER --tunnel                      # localhost.run, the address printed on the console
fastchess-gui --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Dove parlano gli spettatori
<!-- en: where-the-viewers-talk eb3ef90c -->

La pagina non ha una chat propria; rimanda a dove si svolge la conversazione. Un *Link della chat* sotto *Pagina web*
(un invito Discord, un canale Twitch, una discussione di un forum; solo https, qualsiasi altra cosa viene rifiutata
con un messaggio sotto il campo) mette nell'intestazione della pagina un pulsante *Chat* che lo apre in una nuova
scheda. Mentre le partite vengono inviate a lichess, la pagina rimanda anche al turno su lichess, che ha la sua chat.

Un *ID del server Discord* aggiunge una scheda *Discord* accanto a *Event*, con il widget del server di Discord stesso
nel tema chiaro o scuro della pagina: il nome del server, i membri online, i suoi canali vocali e un pulsante *Join*,
non i messaggi. Il proprietario del server lo attiva in Server Settings, Widget ("Enable Server Widget"), che mostra
anche l'ID del server da copiare (da 17 a 20 cifre); con la Developer Mode attiva, un clic destro sull'icona del server
dà anche *Copy Server ID*. Il widget viene caricato da discord.com solo quando uno spettatore apre la scheda, e
Discord vede allora la visita di quello spettatore (il suo indirizzo, il suo browser, il suo accesso a Discord se ne ha
uno); senza un ID, la pagina non contatta nessuno tranne fastchess-gui. I link della chat e di lichess non inviano
nulla finché uno spettatore non li clicca, e allora nessun referrer. Un file di configurazione eseguito in modalità
headless (`--run SETUP.json`) prende entrambi da `web_chat_link` e `web_discord_id`.

## Modalità headless
<!-- en: headless-mode 3471fbb3 -->

```
fastchess-gui --run TOURNAMENT_FOLDER     # resume a tournament
fastchess-gui --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

Non viene aperta nessuna finestra; la versione, poi l'avanzamento, vengono stampati sulla console, con i tentativi di
un motore che non parte, il motore messo da parte (con il suo log del motore) o tornato, come nella finestra, e la
classifica ogni 20 partite e alla fine (un incontro a squadre: i punteggi delle squadre, poi i motori di ogni squadra
contro quelli dell'altra), con la fine di un torneo accompagnata dalla riga delle copie della classifica (partite,
patte, cadenza, libro ed evento). **Ctrl+C** ferma dopo le partite in corso e il torneo può essere ripreso più tardi,
dalla riga di comando o dall'interfaccia grafica. Codici di uscita: 0 terminato, 1 errore durante il gioco, 2 niente da
eseguire, 3 fermato prima della fine (con Ctrl+C, o perché non resta nulla da giocare senza un motore che non parte).
Anche il budget di memoria e le GPU delle **Impostazioni** del profilo vi si applicano.

`--detach` lo gioca invece in background: il comando ritorna subito, e il torneo prosegue senza un terminale; la
finestra lo mostra (vi si collega) quando si apre con lo stesso profilo, e `--attach` parla con esso:

```
fastchess-gui --run FOLDER --detach         # in the background, on a local socket
fastchess-gui --attach FOLDER               # its control channel on the standard input and output
```

La finestra guida ogni torneo allo stesso modo: `--run FOLDER --control socket` (o `--control stdio`) lo gioca senza
stampare nulla, scambiando righe JSON con la finestra: gli eventi del torneo (il suo stato, ogni riga del diario, le
partite in corso mossa per mossa, ogni partita registrata, il suo log, la sua fine) e i comandi della finestra (pausa,
ripresa, arresto, modifiche, la fine di una partita, la sua quota della macchina con
`--allowance THREADS:MEMORY_MB:GPUS:ONE`). La prima riga in ogni direzione è un saluto,
`{"hello": {"protocol": 1, ...}}`; uno script tramite `--attach` riceve il saluto e gli eventi del torneo, e può
inviare comandi come `{"c": "pause"}`. Una sola finestra (o script) alla volta parla con un torneo; `--attach` lo
lascia quando il suo standard input finisce o il suo output non viene più letto, quindi uno script deve tenere aperto
il suo input mentre legge le risposte.

Il torneo può essere trasmesso nello stesso momento, come dalla scheda *Trasmissione*:

```
fastchess-gui --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
fastchess-gui --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
fastchess-gui --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
fastchess-gui --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
fastchess-gui --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
fastchess-gui --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` prende il token dalla variabile d'ambiente `FCG_LICHESS_TOKEN`, o quello del profilo.

Le opzioni si possono combinare. Un file di configurazione salvato con la trasmissione attiva trasmette allo stesso
modo senza di esse. Per gli spettatori fuori dalla rete locale, apri le porte nel router: TLCV e node-tlcv si collegano
alla porta TLCS. Per la pagina web, un tunnel evita di aprire una porta (vedi [Pubblicare la pagina web su
internet](#pubblicare-la-pagina-web-su-internet)).

## Macchine
<!-- en: machines 4d8fbfef -->

Altri computer su cui far girare tornei: una macchina di test, un server, un secondo PC. **Impostazioni** > *Macchine*
li elenca. Una macchina si raggiunge con SSH: su di essa non bisogna aprire nulla tranne il suo server SSH, e il
programma non conserva nulla di segreto.

### Aggiungere una macchina
<!-- en: adding-a-machine f1f15823 -->

*Aggiungi una macchina*, poi sulla sua scheda:

- *Nome*, *Host* (il suo nome sulla rete, o il suo indirizzo), *Porta* (22) e *Utente*, l'account con cui girano i suoi
  tornei;
- *Accedi con*: l'*Agente SSH* (ssh-agent su Linux e macOS, Pageant o il servizio *OpenSSH Authentication Agent* su
  Windows; aggiungigli la tua chiave con `ssh-add`), o un *File di chiave*, RSA o Ed25519 (`~/.ssh/id_ed25519`). La
  passphrase di un file di chiave viene chiesta una volta per sessione: resta in memoria e non viene mai scritta da
  nessuna parte. La macchina deve conoscere la chiave: la sua metà pubblica (`id_ed25519.pub`) va in
  `~/.ssh/authorized_keys` sulla macchina (`ssh-copy-id user@host` ce la mette), o, per un amministratore di una
  macchina Windows, in `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Cartella lì*: la cartella di fastchess-gui sulla macchina, per il programma, i suoi tornei, libri e tablebase,
  relativa alla cartella home dell'utente (`fastchess-gui` per impostazione predefinita) o assoluta (`/srv/fcg`,
  `D:\fcg`);
- *Budget*: i core e la memoria che i tornei sulla macchina possono usare insieme; 0 prende ciò che ha trovato il
  test, tutti i thread tranne uno e l'80 % della memoria.

*Prova la connessione* accede, poi indica il sistema della macchina, i thread, la memoria, lo spazio libero dove si
trova la sua cartella, e se fastchess-gui vi è installato, in quale versione. La prima connessione mostra la chiave
host della macchina (la sua impronta SHA256) e da allora la considera fidata (`known_hosts.json` nel profilo, lo stesso
del tunnel della trasmissione). Quando in seguito la chiave cambia, non viene inviato nulla e la scheda lo dice, con
*Fidati della nuova chiave* per una macchina che è stata reinstallata. Ogni errore nomina la macchina e cosa fare:
l'host che non si riesce a raggiungere, una chiave che la macchina rifiuta (e dove va la sua metà pubblica), una
passphrase che non è quella giusta, nessun agente in esecuzione.

*Installa fastchess-gui* (*Aggiorna fastchess-gui* una volta che c'è) copia il programma nella cartella della macchina
via SFTP, poi verifica che lì si avvii (`fastchess-gui --version`). Copia questo programma quando la macchina usa lo
stesso sistema (da Linux x86-64 a Linux x86-64); per un altro, per esempio una macchina Windows da Linux, scarica la
versione per quel sistema e scegli il suo programma sulla scheda (*fastchess-gui per Windows x86_64*). Una copia
interrotta dalla rete riprende da dove si era fermata.

### Motori su una macchina
<!-- en: engines-on-a-machine 4b00e462 -->

*Motori su questa macchina*: i motori della libreria che la macchina ha, ciascuno con il suo percorso lì
(`/opt/engines/stockfish`, `C:\Engines\sf.exe`). *Verifica i motori* verifica ogni percorso: il programma c'è, si può
eseguire, e risponde a `uci` (`xboard` per un motore WinBoard) entro 10 secondi. Il segno accanto, e il suo tooltip,
dicono cosa è stato trovato: il nome del motore, o perché non gira (mancante, non eseguibile, fatto per un altro
sistema, muto).

Un motore mancante sulla macchina vi viene copiato con *Copia…* sulla sua riga (o *Copia \<motore\> su \<macchina\>*
nella tavolozza dei comandi, o il pulsante di un avvio rifiutato perché un motore non ha un percorso lì). Il pannello
che si apre dice prima cosa viene inviato: il programma, la sua dimensione e il sistema per cui è compilato, letto dai suoi
primi byte (un programma Windows, uno Linux o macOS, universale o no, o uno script). Un programma che la macchina non
può eseguire viene rifiutato prima di inviare qualsiasi cosa, indicando quale build scegliere: "Alpha è un programma
Windows; tc-box usa Linux: scegli la build Linux di Alpha". Conta anche l'architettura (x86-64 o arm64); una build
x86-64 su un Mac arm64 gira solo sotto Rosetta, e un formato che il programma non conosce viene copiato lo stesso:
entrambi i casi vengono segnalati, non rifiutati. Uno script va solo su una macchina Linux o macOS, e solo quando
l'interprete della sua prima riga (`python3`, `/bin/sh`) c'è. *Scegli la build \<sistema\> di \<motore\>…* prende
un altro file di questo computer, per esempio la build Windows del motore, lo verifica allo stesso modo, e lo tiene per
quella macchina: i confronti e le sostituzioni successivi usano quello, non il programma della libreria.

*Con la sua cartella* copia l'intera cartella del motore accanto al programma (la sua cartella di lavoro, o la cartella
del programma), per un motore che ha bisogno di file accanto a sé: le sue reti, un libro. Il pannello conta prima i
suoi file e la loro dimensione, e nomina le opzioni del motore che puntano al suo interno (`EvalFile` di una rete): in
un torneo sulla macchina puntano alla copia lì, e la cartella è la cartella di lavoro del motore. Una cartella di più di
2000 file non viene proposta (un programma in `/usr/bin` non va copiato con la sua cartella), una cartella `.git` viene
esclusa, e per una macchina Windows un nome di file che Windows non può contenere (`:`, `*`, `?`, un punto finale,
`CON`, `NUL`...) viene nominato e ferma la copia prima che inizi. I file vengono copiati così come sono, fine riga
compresi.

*Copia* invia i file via SFTP in `engines/<engine>/` nella cartella della macchina, in background con il tempo
rimanente e *Annulla*, ripresa da dove si era fermata dopo un'interruzione; lo SHA-256 di ogni file viene confrontato
con quello di questo computer dopo la copia, e i file già presenti e identici non vengono inviati di nuovo. Su Windows
il programma mantiene o riceve la sua `.exe`; altrove viene reso eseguibile (`chmod 755`). Poi il percorso del motore
sulla macchina viene impostato sulla copia e verificato come fa *Verifica i motori*. Su un Mac, un programma copiato da
un altro computer che non si avvia può aver bisogno di una firma o di uscire dalla quarantena: il messaggio indica i
due comandi da eseguire lì (`codesign -s -` e `xattr -d com.apple.quarantine`); il programma non li esegue da sé.

La colonna *Build* confronta il motore lì con quello di questo computer, tramite il loro SHA-256 (lo fanno *Verifica i
motori* e *Confronta*, con il programma di questo computer sottoposto a hash una volta finché non cambia): *stessa
build di questo computer*, o *un'altra build lì*, con *Sostituisci con quello di questo computer*. La sostituzione
copia il programma di questo computer nella cartella del motore dentro la cartella della macchina e vi fa puntare il
motore: un programma altrove sulla macchina (`/opt/engines/stockfish`) non viene mai sovrascritto, resta com'era.

### Libri e tablebase su una macchina
<!-- en: books-and-tablebases-on-a-machine 8542f2e7 -->

*Verifica ciò che usa la configurazione* confronta il libro di aperture e i libri propri dei partecipanti della
configurazione con la cartella dei libri della macchina, per dimensione e SHA-256 (un libro con lo stesso nome ma un
altro contenuto non è lo stesso libro), e i file Syzygy della configurazione di 6 pezzi al massimo con la cartella
delle tablebase della macchina, per nome e dimensione. *Invia i file mancanti* copia ciò che manca o è diverso, in
background, con il tempo rimanente; ogni libro viene verificato tramite il suo hash una volta copiato, ogni tabella
tramite la sua dimensione, e una copia interrotta riprende. Le tabelle a 7 pezzi non vengono proposte: occupano
terabyte. Un torneo non parte su una macchina senza il suo libro; le tablebase mancanti sono un avviso
(l'aggiudicazione e i motori ne fanno a meno).

### Un torneo su una macchina
<!-- en: a-tournament-on-a-machine 8f918d9a -->

Nella configurazione, *Gioca su* (in *Parallelismo*) sceglie dove gioca il torneo: *Questo computer*, o una macchina.
*Avvia* allora lo prepara lì: fastchess-gui deve esservi installato in questa versione (altrimenti *Aggiorna
fastchess-gui*), ogni motore deve avere il suo percorso sulla macchina (altrimenti l'avvio propone *Copia \<motore\> su
\<macchina\>…*), e i libri della configurazione devono essere nella sua cartella dei libri (altrimenti l'avvio dice
quale manca, e *Invia i file mancanti* ce lo mette); le tablebase mancanti sono solo un avviso. Il torneo riceve la sua
cartella in `tournaments/` della cartella della macchina, con i motori, i libri e le tablebase ai loro percorsi lì, e
il suo processo parte lì fuori dalla sessione SSH (`fastchess-gui --run <folder> --detach`): gioca qualunque cosa
accada alla finestra o alla rete. La finestra lo segue come un torneo di questo computer: Diretta, Partite, la
classifica, pausa, arresto e modifiche, tramite il canale di controllo del suo processo
(`fastchess-gui --attach <folder>` via SSH).

La sua scheda nella pagina iniziale dice *Su \<macchina\>*, e *Connessione a \<macchina\>…* mentre la connessione
è interrotta: la finestra si riconnette da sola, un po' più tardi dopo ogni errore, e riprende gli eventi dove si era
fermata la sua copia. Il diario e i file PGN vengono ricopiati nella cartella del torneo qui man mano che crescono, ogni
pochi secondi, così le Classifiche, le esportazioni e la checklist dell'invio li leggono; la cartella della macchina
resta il riferimento (`machine.json` qui dice quale macchina e quale cartella). Chiudere la finestra lo lascia giocare
lì; l'avvio successivo lo mostra di nuovo. *Riprendi* su un torneo interrotto di una macchina lo riprende lì. Il budget
della macchina (i suoi core e la sua memoria) è condiviso dai suoi tornei, separatamente da quelli di questo computer.
Un torneo gioca su una sola macchina: le sue partite non vengono distribuite su più macchine.

Una macchina ha bisogno di un server OpenSSH (Linux, macOS, BSD; Windows 10 1809 e successivi), con una chiave host
RSA e SFTP, entrambi presenti per impostazione predefinita. I comandi passano per `sh` qualunque sia la shell
dell'utente (fish o csh vanno bene), e per cmd su Windows, la shell predefinita del suo server OpenSSH.

### Motori giocati da questo computer
<!-- en: engines-played-from-this-computer 8c14dc16 -->

Un motore della libreria può girare su una macchina mentre il torneo gioca qui: un motore GPU sulla macchina con le GPU,
un motore compilato per un altro sistema, una macchina riservata a un solo motore. Sulla sua scheda, *Gira su* sceglie
*Questo computer* o una macchina; *Programma lì* e *Cartella di lavoro lì* sono allora percorsi sulla macchina (vuoto:
la cartella del programma lì), e i pulsanti *…*, compresi quelli delle opzioni che nominano un file, sfogliano i file
della macchina. Una volta che ci sono delle macchine, *Aggiungi…* propone *Un programma su \<macchina\>…*, *Importa…*
propone *Tutti i motori di una cartella su \<macchina\>…*, e *Aggiungi un motore…* della macchina propone *Un programma
lì…*. Il motore viene letto, verificato e giocato nella console, nel test rapido e nei tornei come lo è un motore di
questo computer. Il suo percorso è anche quello dell'elenco dei motori della macchina, e lo SHA-256 del suo programma lì
dice quando è stato ricompilato (Δ, riletto).

La macchina deve essere stata provata una volta (*Prova la connessione*), così che la sua chiave host sia nota: un
motore non si connette mai a una macchina la cui chiave non è stata mostrata e considerata fidata. La passphrase della
chiave, se ne ha una, viene chiesta una volta per sessione, prima della prima lettura o dell'avvio, e tenuta in memoria:
il processo del torneo la riceve dalla finestra e la conserva, così che il torneo continui a giocare dopo la chiusura
della finestra. `--run` in un terminale non la chiede: lì usa l'agente SSH.

Ogni motore viene avviato tramite un relay, `fastchess-gui --remote-engine <key>`, servito dal processo che lo fa
giocare: una connessione SSH alla macchina per dieci motori al massimo (il `MaxSessions` di un server OpenSSH), ogni
motore avviato dalla sua cartella lì. Un motore il cui relay se ne va viene terminato anche lì, tramite il suo
identificativo di processo, quando non termina da solo (su una macchina Linux o macOS; su una Windows, viene chiuso il
suo canale).

*Verifica* misura il tempo di andata e ritorno verso la macchina, la mediana di alcuni `isready`. L'orologio lo conta:
il riepilogo della configurazione avvisa quando supera il 5 % dell'incremento, o del tempo che una mossa ha senza
incremento, con *Imposta un margine di … ms* (il doppio dell'andata e ritorno). Una macchina mai provata ferma l'avvio,
e così pure un torneo che gira su una macchina diversa da quella del motore (uno che gira sulla macchina del motore lo
gioca lì come un suo motore).

Mentre il torneo gioca, la pagina iniziale ha una riga *Macchine* sotto i core: ogni macchina, connessa e ciò che vi
gioca, o da quando non risponde. Una macchina che non risponde è una sola voce sotto *Da sistemare*: le partite dei suoi
motori vengono messe da parte e riprovate, le altre proseguono, con *Riprova* e *Prova \<macchina\>*. Una partita che un
motore ha perso perché la sua connessione si è interrotta (la sua fine come disconnessione) viene annullata e rigiocata,
scritta nel diario con il suo motivo (`connection to its machine lost: <machine>`); *Affidabilità* la conta a parte, non
come crash del motore. I motori di una macchina disconnessa più di tre volte in un'ora vengono messi da parte. I core,
la memoria e le GPU di questo computer non contano i motori sulle macchine, solo i loro relay; il budget della macchina,
condiviso dai suoi tornei, non li conta ancora.

### Una macchina Windows
<!-- en: a-windows-machine 58ae029a -->

Windows 10 (1809 e successivi) e Windows 11 includono un server OpenSSH, non installato per impostazione predefinita.
In PowerShell, come amministratore:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

L'installazione apre la porta 22 nel firewall. Poi metti la tua chiave pubblica in
`C:\Users\<user>\.ssh\authorized_keys`, o, per un account del gruppo Administrators, in
`C:\ProgramData\ssh\administrators_authorized_keys`, che solo Administrators e SYSTEM possono leggere (i gruppi sono
indicati dai loro SID, uguali in tutte le lingue di Windows):

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "*S-1-5-32-544:F" /grant "*S-1-5-18:F"
```

Mantieni cmd come shell del server: fastchess-gui parla cmd, non PowerShell (se è stato impostato un valore
`DefaultShell` sotto `HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, rimuovilo). Sulla scheda della macchina, scegli il
`fastchess-gui.exe` della versione per Windows, poi *Installa fastchess-gui*.

## File
<!-- en: files 837f3661 -->

Tutto risiede nella cartella del profilo: `~/.local/share/fastchess/fastchess-gui/` su Linux,
`%APPDATA%\fastchess\fastchess-gui\` su Windows, `~/Library/Application Support/fastchess/fastchess-gui/` su macOS. I
file creati dal programma non sono scrivibili da altri utenti, e `settings.json` è leggibile solo dal suo proprietario
appena contiene un token lichess. **Impostazioni** mostra la cartella del profilo, con *Apri la cartella*.

### Modalità portatile
<!-- en: portable-mode 29ddbe3b -->

Con un file chiamato `portable` (vuoto, senza estensione) accanto al programma (accanto a `fastchess-gui.exe` o
`fastchess-gui`, accanto a `fastchess-gui.app` su macOS), il profilo è invece la cartella `data` accanto al programma,
e la cartella dei tornei predefinita è `data/runs`: non viene scritto nulla altrove, se non nelle cartelle scelte per
tornei, esportazioni e simili. I percorsi di ciò che si trova nella cartella del programma (motori, libri, tablebase,
suite di test, i file PGN delle classifiche e della configurazione, le cartelle scelte) vengono conservati relativi a
essa nei file del profilo, così l'intera cartella si può spostare, copiare su un'altra macchina o eseguire da una
chiavetta USB, qualunque sia la lettera della sua unità, e i suoi motori vengono ritrovati. **Impostazioni** indica
*Modalità portatile* accanto alla cartella del profilo. Per trasferire un profilo esistente, copia i suoi file in
`data`. `--profile` sceglie comunque un'altra cartella, fuori dalla modalità portatile. Un torneo conserva i percorsi
dei motori del momento in cui è stato creato: riprendi un torneo non finito prima di spostare la cartella. Su macOS,
togli prima la quarantena (vedi il README): altrimenti macOS esegue l'applicazione da una posizione temporanea, dove il
file `portable` non c'è.

### La cartella del profilo
<!-- en: the-profile-folder 91838f38 -->

- `engines.json`: la libreria dei motori, e `engines.json.bak` la sua versione precedente, letta se il primo va perso;
- `setup.json`: l'ultima configurazione; `profiles.json`: i tuoi profili delle sezioni della configurazione;
  `templates/*.json`: i modelli salvati; `queue.json`: la coda;
- `book-cache/`: i libri PGN trasformati in libri Polyglot per i libri propri dei partecipanti (rimossi dopo 30 giorni
  di inutilizzo);
- `layout.ini`: la disposizione della finestra e i pannelli dello spazio di lavoro; `layouts.json`: i layout salvati
  per nome; `fastchess-gui.log`: il log dell'ultima sessione;
- `suite.json`: l'ultima configurazione della pagina Suite; `suites/<suite>-<date>.json`: le esecuzioni terminate
  delle suite di test;
- `board.json`: le impostazioni dell'analisi e delle partite della pagina Scacchiera; `analyses/*.json`: le partite
  analizzate mossa per mossa, un file ciascuna; `games/*.pgn`: le partite giocate dalla pagina Scacchiera;
- `settings.json`: la cartella dei tornei, quando ne è stata scelta un'altra in **Impostazioni**, *Rendering
  software*, la cartella di *Tutti i motori di una cartella…*, il tester e il sito di *Esporta per una lista di
  rating…*, il token lichess, la dimensione e la posizione della finestra, la larghezza del pannello della guida, la
  scheda Per iniziare nascosta, il controllo di una nuova versione (desiderato o no, quando è stato fatto l'ultima
  volta, la versione trovata, l'avviso nascosto), l'aspetto (colori della scacchiera, pezzi, suoni delle mosse), e il
  budget della macchina (core, memoria e GPU per i tornei, *Avvia quando ci sono core liberi*);
- `logos/`: i loghi dei motori, copie delle immagini scelte sulle loro schede;
- `machines.json`: le macchine remote di **Impostazioni** (il loro indirizzo, cartella, budget, percorsi dei motori e
  ciò che ha trovato l'ultimo test; nessuna passphrase), e `known_hosts.json` le chiavi host considerate fidate, delle
  macchine e del tunnel;
- `running.json`: i tornei che giocano in questo momento, con i loro pesi, ciascuno rimosso quando si ferma; trovato
  all'avvio, dice cosa mostrare di nuovo (un torneo che ha continuato a giocare senza la finestra) o riprendere;
- `runs/`: i tornei, a meno che non sia stata scelta un'altra cartella (vedi [La cartella di un
  torneo](#la-cartella-di-un-torneo)).

### La cartella di un torneo
<!-- en: a-tournaments-folder 76959723 -->

`runs/<date>_<A>_vs_<B>/` per un match, `runs/<date>_<name>/` per un torneo (in `runs/` o nella cartella scelta), una
cartella ciascuno, che si può spostare, copiare, comprimere in zip e inviare:

- `tournament.json`: il formato, i partecipanti (motori copiati con le loro opzioni) e le condizioni;
- `setup.json`: la configurazione della pagina *Nuovo torneo* da cui è partito, che leggono *Clona* e *Rilancia con…*
  (vedi [Coda e rilancio](#coda-e-rilancio)); un torneo avviato da una versione precedente non ce l'ha;
- `journal.jsonl`: tutto ciò che è successo da allora, una riga per evento (coppie di partite iniziate, partite
  terminate, modifiche, abbinamenti del sistema svizzero e dell'eliminazione diretta); con i file PGN, è ciò che legge
  una ripresa. Una partita è su disco prima della sua riga, quindi un'interruzione di corrente perde al massimo le
  partite in corso. Il testo di una partita a cui nessuna riga rimanda (una partita eliminata da una modifica mentre
  veniva salvata, o lasciata da un crash) viene sostituito da righe vuote, così gli altri visualizzatori PGN mostrano
  solo le partite del torneo;
- `tournament.lock`: tenuto dal programma che gioca il torneo; un altro programma che apre la cartella si limita a
  mostrarlo;
- `<name>.pgn`: commenti delle mosse nello stile cutechess / fastchess, `{+0.35/18 1.234s}`, con il punteggio dal punto
  di vista del lato che ha mosso; partite nell'ordine in cui sono finite (il diario trova ciascuna dal suo posto nel
  file; *Esporta tutto…* nella scheda *Partite*, e il file di *Aggiungi anche ogni partita a*, seguono il calendario);
- `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, con la valutazione dal punto di
  vista del Bianco, come la leggono ChessBase / Fritz e Lichess;
- `<name>` è il nome del torneo al momento della creazione (rinominarlo in seguito conserva i file), con i caratteri
  che Windows non permette sostituiti da `_` e troncato a 64 byte; `tournament.json` registra entrambi i nomi. Un
  torneo senza nome, uno più vecchio, o un match importato dalla 0.2.x ha `games.pgn` e `games-chessbase.pgn`;
- `lichess.json`: la trasmissione e il turno lichess a cui sono andate le sue partite (nessun token);
- `processor.tsv`: il tempo di processore usato dai motori di ogni partita, per l'*Uso del processore* di un torneo
  ripreso;
- `fastchess-gui.log`: il log dei processi che lo hanno giocato (ogni torneo gioca in un processo proprio);
  `control.json`, mentre uno lo gioca: dove si trova il suo socket locale (`control.sock` nella cartella, o nella
  cartella runtime dell'utente quando quel percorso è troppo lungo; una named pipe su Windows), e quale processo è in
  ascolto;
- `logs/game-N.log`: ogni riga scambiata con entrambi i motori durante la partita N (il numero mostrato nella scheda
  *Partite*), con l'ora in cui è stata inviata o letta (`W>` inviata al motore del Bianco, `W<` letta dal suo output,
  `W!` letta dal suo output degli errori: asserzioni, messaggi di crash), per le partite perse per tempo, per crash,
  per blocco o per mossa illegale (o per ogni partita, se richiesto). Vengono conservate le prime 300 righe di ogni
  motore e le ultime 5000 righe della partita, nell'ordine in cui sono arrivate. È ciò di cui ha bisogno l'autore di un
  motore per riprodurre un bug. Per un motore WinBoard mostra il lato UCI; `FCG_WB2UCI_LOG` dà il lato WinBoard;
- `logs/start-<engine>.log`: l'ultimo tentativo fallito di avviare un motore che non parte: il suo programma, l'errore,
  e le righe scambiate con i motori di quella partita fino a quel momento (vedi [Un motore che non
  parte](#un-motore-che-non-parte)).

Una cartella ricevuta da qualcun altro si può aprire senza rischi: il file di *Aggiungi anche ogni partita a* deve
essere un file `.pgn` (un altro nome viene ignorato, con un avviso), e una cartella il cui diario, i cui file PGN o il
cui file di lock sono link simbolici non viene aperta in scrittura, quindi non può far scrivere il programma al di
fuori di essa. Riprenderla esegue i motori nominati dal suo `tournament.json`, come fa qualsiasi torneo.

I match giocati con la versione 0.2 compaiono nella pagina iniziale: *Importali* li trasforma in tornei terminati.

## Riga di comando
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

- `--profile DIR` usa un'altra cartella del profilo;
- `--autostart` avvia la configurazione salvata all'avvio (invece di riprendere il torneo che era in corso, vedi
  [Pausa, arresto e ripresa](#pausa-arresto-e-ripresa));
- `--size WxH` apre la finestra a quella dimensione, centrata, invece che come è stata lasciata, e non tocca la
  dimensione e la posizione conservate per l'avvio successivo;
- `--software` fa disegnare la finestra al processore (il renderer software di SDL) invece che a OpenGL, come fa
  *Rendering software* in **Impostazioni**, per questo avvio (vedi [Disegno della finestra](#disegno-della-finestra));
- `--shot` salva uno screenshot della finestra dopo il numero di secondi indicato (5 per impostazione predefinita), con
  WINDOW portata in primo piano, ed esce una volta presi tutti gli screenshot. Si può ripetere. WINDOW è una pagina
  (`home`, `setup`, `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`,
  `console`, `settings`) o una scheda (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`). Il
  mouse viene ignorato in questa modalità e, su Linux, la finestra viene disegnata fuori dallo schermo (non compare
  nessuna finestra; imposta `SDL_VIDEODRIVER` per cambiarlo); la finestra si apre alla dimensione predefinita, o
  `--size`, e la dimensione e la posizione conservate non vengono toccate;
- `--focus WINDOW` apre all'avvio quella pagina o scheda, oppure: `engines-card` (la scheda del primo motore),
  `engines-refresh` (*Aggiorna: i nuovi motori della cartella*), `engines-ratings=FILE` (l'anteprima di quella lista
  Elo), `setup-picker` e `setup-suggest` (il filtro della libreria con il focus, o gli avversari suggeriti
  selezionati), `setup-open=ROW` e `setup-profiles=ROW` (una riga della configurazione aperta, o il suo elenco di
  profili: `format`, `time`, `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (il libro
  proprio del primo partecipante), `submission-export` (*Esporta per una lista di rating…*), `ratings-players` (la
  finestra *Giocatori PGN*), `clone=DIR` (la pagina *Nuovo torneo* compilata da *Clona* dalla cartella di quel
  torneo), `palette[=TEXT]` (la tavolozza dei comandi, con TEXT digitato), `game-menu=N` (il menu
  della partita N nella scheda *Partite*, con un risultato scelto), `end-menu=N` (*Termina con un risultato* della
  partita dello slot N, da 1), `big-screen[=N]` (lo schermo grande, della partita dello slot N), `live-back=N` (la
  partita seguita in *Diretta*, riportata indietro di N semimosse), `engines-console` (la console del primo motore),
  `board-analyse` (la pagina Scacchiera, con l'analisi), `board=FILE` (un file PGN, FEN o EPD nella pagina Scacchiera;
  `board-analyse=FILE` con l'analisi, `board-review=FILE` con la sua prima partita analizzata mossa per mossa),
  `board-edit` (l'editor di posizione), `board-play` e `board-play-start` (una partita tra i primi due motori,
  avviata), `machines` (la scheda della prima macchina remota in **Impostazioni**; `machines-test` con la prova,
  `machines-files` con i libri e le tablebase della configurazione confrontati con essa), `guide[=ID]` (il pannello
  della guida, alla sezione ID), `guide-search=TEXT` (il pannello della guida, con la ricerca di TEXT);
- `--select-game N` apre la partita N (in ordine di conclusione) nella scheda *Partite* appena esiste;
- `--quit-after SECONDS` ferma il torneo ed esce;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: vedi [Modalità headless](#modalità-headless);
- `--version` stampa la versione e il protocollo del canale tramite cui la finestra segue un torneo remoto (vedi
  [Macchine](#macchine)), ed esce.

## Risoluzione dei problemi
<!-- en: troubleshooting e8c4882d -->

### Il programma non parte
<!-- en: the-program-does-not-start dc0a0128 -->

- **Windows dice che il programma non è sicuro, o un antivirus lo rimuove.** Il programma non è firmato, quindi Windows
  SmartScreen e alcuni antivirus ne diffidano. *Ulteriori informazioni*, *Esegui comunque* lo lascia partire; a un
  antivirus si può dire di consentire la sua cartella. Le versioni sono compilate da GitHub Actions a partire dai
  sorgenti pubblici.
- **macOS si rifiuta di aprirlo.** Non è firmato da un account sviluppatore Apple: fai Ctrl-clic (clic destro) su di
  esso, Apri, poi di nuovo Apri; su macOS 15 e successivi, prova ad aprirlo una volta, poi Impostazioni di Sistema,
  Privacy e sicurezza, Apri comunque. Oppure esegui `xattr -dr com.apple.quarantine` sulla cartella estratta.
- **Un messaggio dice che nessun modo di disegnare la finestra funziona, o la finestra resta nera.** Al driver grafico
  manca OpenGL 3.2 o non funziona: avvia con `--software` (il processore disegna la finestra), e se funziona, spunta
  *Rendering software* in **Impostazioni**. Sotto Wayland senza un driver grafico, avvia con `SDL_VIDEODRIVER=x11`.
  Vedi [Disegno della finestra](#disegno-della-finestra).
- **Il testo cinese appare come quadratini.** Il cinese ha bisogno di un font del sistema: Microsoft YaHei su Windows,
  Noto Sans CJK o WenQuanYi su Linux (installa `fonts-noto-cjk` o simili).

### Un motore non parte
<!-- en: an-engine-does-not-start 202a67d4 -->

- **La sua verifica fallisce, o il suo torneo dice che non riesce ad avviarsi.** Leggi l'errore sotto il motore, poi il
  suo log del motore (*Log del motore*, o `logs/start-<engine>.log` nella cartella del torneo): l'output degli errori
  del motore spesso nomina il file mancante. Le cause abituali: il programma spostato (*Individua…*), una rete o una
  libreria non trovate (imposta la cartella di lavoro, o `EvalFile`, sulla sua scheda), un programma compilato per un
  altro processore (una build che usa AVX-512 su una macchina che non lo ha), un avvio lento (aumenta *Timeout di
  avvio* sulla sua scheda). Vedi [Un motore che non parte](#un-motore-che-non-parte).
- **Un motore WinBoard non parte dalle posizioni del libro.** Un motore del protocollo 1 senza `setboard` può partire
  solo dalla posizione iniziale: dagli un libro PGN (mosse), non un libro EPD. Vedi [Motori
  WinBoard](#motori-winboard).
- **Il torneo si rifiuta di partire: un motore non gioca a Chess960.** Ogni motore deve avere l'opzione
  `UCI_Chess960` (UCI) o la variante `fischerandom` (WinBoard); il riepilogo nomina quelli che non le hanno.

### Partite perse o che sembrano sbagliate
<!-- en: games-are-lost-or-look-wrong f8a7cc97 -->

- **Un motore perde per tempo all'inizio delle sue partite.** Più spesso è configurato male che non gioca male: troppi
  thread per i core di cui dispongono le partite, un hash troppo grande per essere allocato in tempo, una rete
  caricata a ogni partita, un avvio lento. Abbassa i suoi `Threads` e il suo `Hash`, togli la spunta a *Riavvia il
  motore a ogni partita*, aumenta *Timeout di avvio*; la scheda *Affidabilità* e la colonna *TF* della panoramica
  mostrano le sconfitte per tempo.
- **I motori sembrano più lenti di quanto dovrebbero.** Troppe partite insieme: lascia *Parallelismo* automatico, e
  mantieni parallelismo × thread entro i core fisici. *Uso del processore* nella panoramica nomina un motore che usa
  più core dei suoi `Threads`, o pensa sul tempo del suo avversario.
- **Il riepilogo avvisa che la memoria non basta.** Le tabelle hash delle partite non entrerebbero nella memoria
  libera: abbassa l'`Hash` (in *Impostazioni dei motori*, o quello del motore che nomina) o il parallelismo.
- **Dei file di tablebase risultano mancanti o troncati.** Un download si è fermato a metà, o la cartella contiene solo
  tabelle più piccole: scarica di nuovo i file nominati nel tooltip, o abbassa l'aggiudicazione ai pezzi trovati
  (*Abbassa a 5 pezzi*).
- **Un libro risulta avere posizioni illegali o duplicati.** fastchess le esclude; *Salva una copia ripulita…* nella
  pagina Libri scrive un libro senza di esse. Vedi [Verificare un libro](#verificare-un-libro).
- **Una partita è stata decisa in modo sbagliato.** Clic destro su di essa in *Partite* o *Calendario*: rigiocala,
  annullala, o fissane il risultato, con il motivo. Vedi [Modificare un torneo in
  corso](#modificare-un-torneo-in-corso).

### Trasmettere
<!-- en: broadcasting 4a2e6916 -->

- **TLCV o un altro visualizzatore non si collega.** Avvia il torneo (e il suo server TLCS) prima del visualizzatore:
  non si riconnette da solo. Apri la porta UDP nel router per gli spettatori fuori dalla rete locale.
- **La pagina web non è raggiungibile dall'esterno.** Spunta *Pubblica su internet* per un tunnel (vedi [Pubblicare la
  pagina web su internet](#pubblicare-la-pagina-web-su-internet)), o apri la porta TCP nel router.
- **lichess rifiuta il token.** Il token ha bisogno del permesso `study:write`: creane uno nuovo con *Crea un token su
  lichess…*. Dietro un proxy, imposta `HTTPS_PROXY`.

### Tutto il resto
<!-- en: anything-else ce59c083 -->

*Segnala un problema…* in **Impostazioni** o nella pagina **Log** apre una issue su GitHub già compilata con la
versione, il sistema e le ultime righe del log (vedi [Segnalare un problema](#segnalare-un-problema)).

## Glossario
<!-- en: glossary 0386d0d9 -->

- **Aggiudicazione**: terminare una partita prima del matto quando il suo risultato è chiaro: patta quando entrambe le
  valutazioni restano vicine a 0, sconfitta quando restano molto sotto, o ciò che dicono le tablebase. Vedi
  [Aggiudicazione](#aggiudicazione).
- **BayesElo, DrawElo**: un modello di rating con un parametro separato per le patte (DrawElo), dal programma di Rémi
  Coulom.
- **Buchholz, Sonneborn-Berger**: criteri di spareggio: la somma dei punteggi degli avversari (un sistema svizzero), e
  la somma dei punteggi degli avversari battuti più la metà di quelli con cui si è pattato (un girone all'italiana).
- **Chess960, Chess324**: posizioni di partenza con la prima traversa mescolata: 960 con il re tra le torri (l'arrocco
  porta sulle case abituali), 324 con il re e le torri al loro posto.
- **Ciclo**: in un girone all'italiana, ogni abbinamento giocato una volta con ciascun colore (cutechess lo chiama
  round, da 2 partite per incontro).
- **ECO**: i codici dell'Encyclopaedia of Chess Openings, da A00 a E99.
- **Elo, nElo**: la differenza di rating che un punteggio rappresenta. Il nElo (Elo normalizzato) divide per la
  dispersione dei risultati, così il suo valore non dipende dalla percentuale di patte; fishtest e OpenBench vi
  esprimono i limiti dei loro SPRT.
- **EPD, FEN**: una riga che descrive una posizione; una riga EPD può contenere operazioni, come la mossa migliore `bm`
  di una suite di test.
- **Gauntlet**: i motori in prova (le teste di serie) giocano contro tutti gli altri motori, che non giocano tra loro.
- **H0, H1**: le due ipotesi di un SPRT: la differenza Elo è elo0 o meno (H0), o elo1 o più (H1).
- **Hash**: la memoria che un motore occupa per la sua tabella delle trasposizioni, in MB.
- **LLR**: il log-likelihood ratio (logaritmo del rapporto di verosimiglianza) di un SPRT; il test finisce quando esce
  dai suoi limiti, ricavati da alpha e beta.
- **LOS**: la likelihood of superiority, la probabilità che un motore sia più forte di un altro, calcolata dalle loro
  partite.
- **MultiPV**: un motore che analizza più linee insieme.
- **Parallelismo**: il numero di partite giocate contemporaneamente.
- **Pentanomiale**: i risultati delle coppie di partite contati in base al loro totale: 0, ½, 1, 1½ o 2 punti. Dà
  barre d'errore più strette che contare le partite singole, perché le due partite di una coppia condividono
  un'apertura.
- **PGN**: il formato testuale delle partite a scacchi: tag come `[White "…"]`, poi le mosse.
- **Polyglot**: il formato binario dei libri di aperture (`.bin`) letto dalla maggior parte dei programmi, con un peso
  per ogni mossa.
- **Ponder**: pensare sul tempo dell'avversario; disattivato nei tornei di fastchess-gui.
- **SPRT**: il sequential probability ratio test (test sequenziale del rapporto di verosimiglianza), che gioca partite
  finché può dire, con i tassi di errore scelti (alpha, beta), se un motore guadagna elo1 o più (H1) o non più di elo0
  (H0).
- **Suite di aperture, libro**: le posizioni o le mosse da cui partono le partite, ciascuna giocata due volte a colori
  invertiti.
- **Syzygy**: le tablebase dei finali che danno il risultato di ogni posizione con pochi pezzi (file WDL, `.rtbw`).
- **Tabelle Berger**: l'ordine dei turni di un girone all'italiana in cui ogni motore gioca una volta per turno.
- **Testa di serie**: un posto nell'ordine dei partecipanti: i motori in prova di un gauntlet, i favoriti di
  un'eliminazione diretta.
- **TF**: time forfeit, le partite perse per tempo.
- **Threads**: i thread del processore con cui un motore analizza.
- **TLCS, TLCV**: Tom's Live Chess Server e Viewer, un protocollo per seguire in diretta le partite tra motori.
- **UCI, WinBoard (CECP)**: i due protocolli con cui i programmi parlano con i motori di scacchi.
- **WDL**: le probabilità di vittoria, patta e sconfitta che un motore dà insieme alla sua valutazione.
