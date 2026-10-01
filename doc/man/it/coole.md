---
date: settembre 2026
---

<!-- help:topics "Indice degli argomenti:" -->
# NOME <!-- help:skip -->

coole - un editor di testo.

# USO <!-- help:skip -->

**coole**
[opzioni] [+riga] [file1[:riga]] [file2[:riga]] ...

# DESCRIZIONE <a id="description"></a>

**coole**
è un editor di testo per la console. Continua
**cooledit**,
l'editor di Paul Sheer, la cui versione per terminale è stata sviluppata per
molti anni come editor interno di GNU Midnight Commander; coole lo porta avanti
come programma a sé.

Modifica più file insieme, ciascuno nella sua finestra, e anche file binari.
Ogni file indicato sulla riga di comando viene aperto nella sua finestra;
senza file, l'editor apre una finestra vuota.

# OPZIONI <a id="options"></a>

*+riga*
: Andare alla riga con quel numero (senza spazio tra il segno
*+*
e il numero). Sono ammessi più numeri, ma viene usato solo l'ultimo, e solo per
il primo file.

*file:riga[:]*
: Aprire il file a quella riga, come indicano un punto i compilatori e grep.

*-V, --version*
: Mostrare la versione del programma.

*-f, --datadir*
: Mostrare i percorsi di ricerca compilati per i file di dati di coole.

*-F, --datadir-info*
: Mostrare informazioni estese sulle directory usate da coole.

*--configure-options*
: Mostrare le opzioni con cui il programma è stato configurato.

*--no-lua*
: Avviare senza l'ambiente Lua: non viene caricato nessuno script Lua.

*-h, -?, --help*
: Mostrare le opzioni e cosa fanno.

*-x, --xterm*
: Forzare la modalità xterm.

*-X, --no-x11*
: Non usare il sistema X Window per conoscere lo stato dei tasti modificatori.

*-g, --oldmouse*
: Usare il vecchio tracciamento del mouse del terminale.

*-d, --nomouse*
: Disattivare il mouse.

*-t, --termcap*
: Usare termcap invece di terminfo (solo con la libreria S-Lang).

*-s, --slow*
: Terminale lento: lo schermo viene ridisegnato meno spesso.

*-a, --stickchars*
: Disegnare le cornici con caratteri semplici.

*-K file, --keymap=file*
: Caricare le associazioni dei tasti da quel file.

*--nokeymap*
: Non caricare nessun file dei tasti: usare le associazioni interne.

*-b, --nocolor*
: Forzare la visualizzazione in bianco e nero.

*-c, --color*
: Forzare la modalità a colori.

*-S tema, --skin=tema*
: Usare il tema (skin) con quel nome.

# Editor di file interno <a id="internal-file-editor"></a>

L'editor di file interno è un editor a pieno schermo con tutte le funzioni
consuete. Un file più grande di
*editor_filesize_threshold*
(64 MB come impostazione predefinita) viene aperto dopo una domanda. I file
binari si possono modificare.

Le funzioni che allo stato attuale supporta sono: copia, spostamento,
cancellazione, taglia e incolla di blocchi; annullamento tasto per tasto;
menu a discesa; inserimento file; definizione di macro; ricerca e
sostituzione con espressioni regolari; selezione del testo con
maiuscole-freccia (se il terminale le distingue); scambio tra inserimento e
sovrascrittura; a capo automatico; rientro automatico; dimensione della
tabulazione regolabile; evidenziazione della sintassi per vari tipi di file;
e la possibilità di passare blocchi di testo a comandi di shell come indent o
ispell.

Sezioni:
: [Opzioni dell'editor](#editor-options)

L'editor è molto semplice da usare e non richiede apprendimento. Per vedere
cosa fanno i tasti, basta consultare il menu a discesa appropriato. Gli altri
tasti sono: maiuscole più i tasti freccia selezionano il testo.
**Ctrl-Ins**
copia nel file di scambio
**~/.local/share/coole/clipboard**,
**Shift-Ins**
incolla da esso,
**Shift-Del**
vi taglia dentro e
**Ctrl-Del**
cancella il testo selezionato.
Funziona anche la selezione con il mouse che, come al solito, si può
scavalcare premendo il tasto Maiuscole mentre si trascina, lasciando al
terminale la propria gestione del mouse.

Per definire una macro, premere
**Ctrl-R**,
poi premere i tasti che si vuole vengano eseguiti. Premere nuovamente
**Ctrl-R**
quando si ha finito e assegnare la macro a un tasto premendolo. La macro
viene eseguita con quel tasto e anche premendo
**Ctrl-A**
seguito dal tasto assegnato. Le macro stanno nella sezione
**[editor]**
del file
**~/.local/share/coole/macros**,
e si cancella una macro cancellando la sua riga in quel file.

Il set di caratteri del testo mostrato si cambia con Alt-e (M-e). La
riconversione avviene dal set scelto a quello di sistema. Per annullarla,
scegliere "\<No translation>" nella finestra di scelta del set di caratteri.

Il pulsante
**Filtro**
della finestra di ricerca
(**F7**)
nasconde tutte le righe che non contengono il testo cercato, con le stesse
impostazioni di tipo, maiuscole e parola intera della ricerca. I numeri di
riga restano quelli originali e la colonna di stato segna ogni tratto
nascosto. L'insieme delle righe nascoste viene fissato nel momento in cui si
preme il pulsante: la modifica non riapplica la condizione, quindi una riga
visibile che venga divisa o unita resta visibile, e le righe scritte dopo
restano visibili anche se non corrispondono. Il tasto
**M-s**
toglie il filtro; premuto di nuovo rimette l'ultima ricerca come filtro.
Anche "Espandi tutto" del menu Comandi lo toglie.

# Opzioni dell'editor <a id="editor-options"></a>

Le impostazioni dell'
[editor interno](#internal-file-editor).
Le apre la voce
**Generale...**
del menu
**Opzioni**
dell'editor.

*Modalità di a capo.*
Disattivata, formattazione continua del paragrafo, oppure l'a capo da macchina
da scrivere, che spezza la riga alla lunghezza fissata mentre si scrive.

*Mezze tabulazioni finte.*
Tra il testo e il margine sinistro il movimento e il rientro vanno di mezza
tabulazione e si riempiono di spazi; altrove la tabulazione è quella normale.

*Backspace attraverso le tabulazioni.*
Un solo backspace cancella tutto il rientro fino al margine sinistro quando
tra il cursore e il margine non c'è testo.

*Riempi le tabulazioni con spazi.*
Invece del carattere di tabulazione vengono inseriti spazi fino alla
tabulazione successiva.

*Ampiezza della tabulazione.*
L'ampiezza che vale il carattere di tabulazione. 8 in modo predefinito.

*Invio fa il rientro automatico.*
La riga nuova comincia con il rientro della riga sopra.

*Conferma prima di salvare.*
Chiede prima di scrivere il file.

*Ricorda la posizione nel file.*
Il file si apre dove è stato lasciato l'ultima volta.

*Mostra gli spazi finali.*
Gli spazi a fine riga vengono segnati.

*Mostra le tabulazioni.*
I caratteri di tabulazione vengono segnati.

*Mostra i caratteri di controllo.*
I caratteri di controllo del testo vengono stampati invece che nascosti.

*Evidenziazione della sintassi.*
Il testo viene colorato con le regole di sintassi del suo tipo di file.

*Cursore dopo il blocco inserito.*
Dopo aver inserito un blocco il cursore resta alla fine e non all'inizio.

*Selezione persistente.*
La selezione resta quando il cursore si muove, invece di perdersi.

*Cursore oltre la fine della riga.*
Il cursore può stare dopo l'ultimo carattere della riga.

*Annulla in gruppo.*
Un solo annullamento riporta indietro una serie di modifiche dello stesso
tipo, non un singolo tasto.

*Lunghezza della riga per l'a capo.*
La colonna in cui le modalità di a capo spezzano la riga. 72 in modo
predefinito.

# Salva come <a id="save-file-as"></a>

Il nome con cui scrivere il file e i fine riga con cui scriverlo: come li ha
il file, Unix (LF), Windows e DOS (CR LF) oppure Macintosh (CR).

# Modalità di salvataggio <a id="edit-save-mode"></a>

Come viene scritto il file:

**Salvataggio rapido**
: Scrive subito sopra il file. Veloce, e un guasto a metà lascia il file
scritto a metà.

**Salvataggio sicuro**
: Scrive prima un file temporaneo e lo rinomina sopra l'originale quando è
intero, così un guasto non tocca l'originale.

**Crea copie di sicurezza con l'estensione**
: Salvataggio sicuro, e l'originale resta con il suo nome più l'estensione
della riga di ingresso, "~" in modo predefinito.

**Controlla il fine riga POSIX**
: Chiede del fine riga che manca alla fine del file prima di scriverlo.

# Esploratore di macro <a id="macro-explorer"></a>

Le macro registrate, con il tasto a cui ciascuna risponde e ciò che fa. I
pulsanti sono

**Esegui**
: Riproduce la macro su cui è il cursore.

**Elimina**
: La toglie, dopo una domanda.

**Modifica file**
: Apre il file in cui stanno le macro.

# File aperti <a id="open-files"></a>

I file che l'editor ha aperti, uno per riga. Invio va al file su cui è il
cursore, Esc lascia quello mostrato.

# Informazioni sui componenti <a id="plugin-info"></a>

I componenti aggiuntivi che l'editor ha caricato: il nome, se è attivo, cosa
offre e cosa fa. È un elenco da guardare; si disattiva un componente e se ne
aprono le impostazioni nella finestra
[Gestione dei componenti](#manage-plugins)
del menu Opzioni.

# Associazioni dei tasti <a id="key-bindings"></a>

Vedere e cambiare le combinazioni di tasti delle azioni del programma (menu
Opzioni, "Associazioni dei tasti...").

**Invio**
: Sostituisce la combinazione: premere il tasto da assegnare.

**F5**
: Aggiunge un'altra combinazione per questa azione.

**F8, Canc**
: Toglie la combinazione.

**Salva**
: Scrive le modifiche in
*~/.config/coole/keymap.ini*.

**Modifica file dei tasti**
: Apre
*keymap.ini*
nell'editor.

**Modifica file del terminale**
: Apre le definizioni dei tasti del terminale.

Le azioni segnate con \* si scostano da quelle di serie.

# Analizzatore di tasti <a id="key-sniffer"></a>

Premere Cattura e poi un tasto qualsiasi. Mostra:

```
Combinazione   Nome simbolico (per esempio Ctrl-F5)
Azione         Azione associata nella mappa attuale
Grezzo         Sequenza di escape e byte in esadecimale
Codice         Codice numerico interno
```

Utile per capire i problemi con i tasti del terminale.

# AMBIENTE <a id="environment"></a>

**COOLE_DATADIR**
: La directory dei file di dati, al posto di {{pkgdatadir}}.

**COOLE_PROFILE_ROOT**
: La radice dei file dell'utente, al posto della directory personale.

**COOLE_SKIN**
: Il tema da usare.

**COOLE_KEYMAP**
: Il file dei tasti da usare.

**COOLE_TMPDIR**, **TMPDIR**
: La directory dei file temporanei.

**COOLE_NO_LUA**
: Con il valore 1, avviare senza l'ambiente Lua.

**COOLE_LOG_ENABLE**, **COOLE_LOG_FILE**, **COOLE_SPELL_LOG**
: I registri di debug.

# FILE <a id="files"></a>

*{{pkgdatadir}}/help/coole.md*
: Il file della guida del programma.

*{{sysconfdir}}/coole/coole.ini*, *{{pkgdatadir}}/coole.ini*
: Le impostazioni di sistema, usate solo se l'utente non ha
~/.config/coole/ini.

*{{sysconfdir}}/coole/keymap.ini*, *{{sysconfdir}}/coole/coole.menu*
: I tasti e il menu utente di sistema.

*{{pkgdatadir}}/syntax/\**
: I file di sintassi di sistema.

*{{plugins_dir}}/*
: I componenti aggiuntivi dell'editor.

*~/.config/coole/ini*, *~/.config/coole/keymap.ini*, *~/.config/coole/menu*
: Le impostazioni, i tasti e il menu utente propri.

*~/.local/share/coole/*
: La cronologia, le posizioni dei file, gli appunti (clipboard), le macro
(macros, macros.d/), la sintassi (syntax/Syntax), i temi (skins/) e gli script
Lua (lua/scripts/) dell'utente.

*~/.cache/coole/*
: Il file del blocco (block) e i file temporanei dell'editor.

# VEDERE ANCHE <a id="see-also"></a>

cooledit(1), aspell(1), ctags(1).

Il manuale in inglese, coole(1), descrive tutte le funzioni dell'editor.
