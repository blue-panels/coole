---
date: 2026. szeptember
---

<!-- help:topics "Tartalomjegyzék" -->
# NÉV <!-- help:skip -->

coole - szövegszerkesztő.

# ALKALMAZÁSA <!-- help:skip -->

**coole**
[kapcsolók] [+sor] [fájl1[:sor]] [fájl2[:sor]] ...

# LEÍRÁS <a id="description"></a>

A
**coole**
konzolos szövegszerkesztő, a
**cooledit**
folytatása. A cooledit Paul Sheer szerkesztője, amelynek terminálos változatát
sok éven át a GNU Midnight Commander belső szerkesztőjeként fejlesztették; a
coole önálló programként fejleszti tovább.

Egyszerre több fájlt szerkeszt, mindegyiket a saját ablakában, bináris
fájlokat is. A parancssorban megadott minden fájl a saját ablakában nyílik
meg; fájl nélkül a szerkesztő üres ablakot nyit.

# KAPCSOLÓK <a id="options"></a>

*+sor*
: Ugrás a megadott számú sorra (a
*+*
jel és a szám közé ne tegyél szóközt). Több sorszám is megadható, de csak az
utolsó számít, és csak az első fájlra.

*fájl:sor[:]*
: A fájl megnyitása azon a soron, ahogy a fordítóprogramok és a grep megadják
a helyet.

*-V, --version*
: A program verziójának kiírása.

*-f, --datadir*
: A coole adatfájljainak beépített keresési útvonalainak kiírása.

*-F, --datadir-info*
: Részletes adatok a coole által használt könyvtárakról.

*--configure-options*
: A program konfigurálásakor használt kapcsolók kiírása.

*--no-lua*
: Indítás Lua nélkül: egyetlen Lua-szkript sem töltődik be.

*-h, -?, --help*
: A kapcsolók és jelentésük kiírása.

*-x, --xterm*
: Az xterm mód kikényszerítése.

*-X, --no-x11*
: A módosítóbillentyűk állapotát ne az X Window rendszertől kérdezze.

*-g, --oldmouse*
: A terminál régi egérkövetésének használata.

*-d, --nomouse*
: Az egér kikapcsolása.

*-t, --termcap*
: A terminfo helyett a termcap használata (csak az S-Lang könyvtárral).

*-s, --slow*
: Lassú terminál: a képernyő ritkábban frissül.

*-a, --stickchars*
: A keretek rajzolása egyszerű karakterekkel.

*-K fájl, --keymap=fájl*
: A billentyűtársítások betöltése ebből a fájlból.

*--nokeymap*
: Semmilyen billentyűfájl ne töltődjön be: a beépített társítások
érvényesek.

*-b, --nocolor*
: Fekete-fehér megjelenítés kikényszerítése.

*-c, --color*
: Színes mód kikényszerítése.

*-S téma, --skin=téma*
: A megadott nevű téma (skin) használata.

# Belső fájlszerkesztő <a id="internal-file-editor"></a>

A belső fájlszerkesztő egy teljes képernyős, minden szokásos eszközzel
ellátott szerkesztő. Az
*editor_filesize_threshold*
értéknél (alapértelmezésben 64 MB) nagyobb fájl kérdés után nyílik meg.
Bináris fájlokat is lehet vele szerkeszteni.

A támogatott eszközök: blokk másolása, mozgatása, törlése, kivágása,
beillesztése; billentyűnkénti visszavonás; legördülő menük; fájl
beillesztése; makrók; keresés és csere reguláris kifejezéssel; szöveg
kijelölése Shift-kurzorral (ha a terminál ismeri); beszúrás-felülírás
váltása; sortörés; automatikus behúzás; állítható tabulátorméret;
szintaxiskiemelés többféle fájltípushoz; és szövegblokk átadása
shell-parancsnak, például az indent vagy az ispell programnak.

Szakaszok:
: [A szerkesztő beállításai](#editor-options)

A szerkesztő használata nagyon egyszerű és nem igényel magyarázatot. Hogy
melyik billentyű mit csinál, az a legördülő menükben látszik. Ezenkívül a
Shift és a kurzorbillentyűk szöveget jelölnek ki. A
**Ctrl-Ins**
a csereállományba másol
(**~/.local/share/coole/clipboard**),
a
**Shift-Ins**
onnan illeszt be, a
**Shift-Del**
oda vág ki, a
**Ctrl-Del**
pedig törli a kijelölt szöveget. Az egérrel való kijelölés is működik, és a
szokásos módon megkerülhető: a Shift nyomva tartásával a terminál saját
egérkezelése marad érvényben.

Makró megadásához nyomd le a
**Ctrl-R**-t,
majd üsd le azokat a billentyűket, amelyeknek le kell futniuk. Ha kész vagy,
nyomd le újra a
**Ctrl-R**-t,
és rendeld a makrót egy billentyűhöz annak lenyomásával. A makró ezzel a
billentyűvel, valamint a
**Ctrl-A**
és a hozzárendelt billentyű leütésével fut le. A makrók a
**~/.local/share/coole/macros**
fájl
**[editor]**
szakaszában vannak, és a hozzájuk tartozó sor törlésével szűnnek meg.

A megjelenített szöveg kódlapját az Alt-e (M-e) váltja. Az átkódolás a
választott kódlapról a rendszerére történik. Megszüntetéséhez a
kódlapválasztó ablakban a "\<No translation>" tételt kell választani.

A keresőablak
(**F7**)
**Szűrő**
gombja elrejti azokat a sorokat, amelyekben nincs benne a keresett szöveg,
ugyanazokkal a keresési, kis- és nagybetű, valamint teljes szó
beállításokkal, mint a keresés. A sorszámok az eredetiek maradnak, az
állapotoszlop pedig megjelöli minden elrejtett szakasz helyét. Az elrejtett
sorok köre a gomb megnyomásakor rögzül: a szerkesztés nem alkalmazza újra a
feltételt, így a kettévágott vagy összevont látható sor látható marad, és az
utána beírt sorok is láthatóak maradnak, akkor is, ha nem felelnek meg a
feltételnek. Az
**M-s**
leveszi a szűrőt; újra megnyomva az utolsó keresést teszi vissza szűrőként. A
Parancsok menü "Mindent kinyit" tétele szintén leveszi.

# A szerkesztő beállításai <a id="editor-options"></a>

A
[belső fájlszerkesztő](#internal-file-editor)
beállításai. A szerkesztő
**Beállítások**
menüjének
**Általános...**
pontja nyitja meg őket.

*Tördelési mód.*
Kikapcsolva, a bekezdés folyamatos formázása, vagy az írógépszerű tördelés,
amely gépelés közben töri a sort a megadott hossznál.

*Fél tabulátorok utánzása.*
A szöveg és a bal margó között a mozgás és a behúzás fél tabulátorral megy, és
szóközökkel telik ki; máshol a tabulátor a szokásos.

*Visszatörlés tabulátorokon át.*
Egyetlen visszatörlés a bal margóig törli a behúzást, ha a kurzor és a margó
között nincs szöveg.

*Tabulátorok kitöltése szóközökkel.*
Tabulátor karakter helyett szóközök kerülnek a következő tabulátorpozícióig.

*Tabulátor szélessége.*
Ennyi karakternek felel meg a tabulátor. Alapértelmezés szerint 8.

*Enter behúzást tart.*
Az új sor a fölötte lévő sor behúzásával kezdődik.

*Megerősítés mentés előtt.*
A fájl kiírása előtt kérdez.

*Pozíció megjegyzése.*
A fájl ott nyílik meg, ahol legutóbb elhagyták.

*Sorvégi szóközök mutatása.*
A sor végén lévő szóközök jelölve lesznek.

*Tabulátorok mutatása.*
A tabulátor karakterek jelölve lesznek.

*Vezérlőkarakterek mutatása.*
A szöveg vezérlőkarakterei kiíródnak, nem rejtve maradnak.

*Szintaxiskiemelés.*
A szöveg a fájltípusának szintaxisszabályaival színeződik.

*Kurzor a beszúrt blokk után.*
Blokk beszúrása után a kurzor a végén marad, nem az elején.

*Állandó kijelölés.*
A kijelölés a kurzor mozgatásakor megmarad, nem szűnik meg.

*Kurzor a sor vége mögött.*
A kurzor a sor utolsó karaktere mögött is állhat.

*Csoportos visszavonás.*
Egy visszavonás azonos fajtájú változtatások sorát vonja vissza, nem egyetlen
billentyűt.

*Tördelési sorhossz.*
Az az oszlop, amelynél a tördelési módok törik a sort. Alapértelmezés szerint
72.

# Mentés másként <a id="save-file-as"></a>

A név, amellyel a fájlt kiírjuk, és a sorvégek, amelyekkel kiírjuk: ahogy a
fájlban vannak, Unix (LF), Windows és DOS (CR LF) vagy Macintosh (CR).

# Mentési mód <a id="edit-save-mode"></a>

Hogyan íródik ki a fájl:

**Gyors mentés**
: Azonnal a fájlra ír. Gyors, és a közben bekövetkező hiba félig kiírt fájlt
hagy hátra.

**Biztonságos mentés**
: Előbb ideiglenes fájlba ír, és azt nevezi át az eredetire, amikor egészben
kész, így a hiba nem bántja az eredetit.

**Biztonsági másolat ezzel a kiterjesztéssel**
: Biztonságos mentés, és az eredeti megmarad a saját nevén a beviteli sorban
megadott kiterjesztéssel, alapértelmezés szerint "~".

**POSIX sorvég ellenőrzése**
: Kiírás előtt kérdez a fájl végéről hiányzó sorvégről.

# Makró böngésző <a id="macro-explorer"></a>

A felvett makrók, azzal a billentyűvel, amelyre mindegyik válaszol, és azzal,
amit csinál. A gombok:

**Futtatás**
: Lejátssza azt a makrót, amelyen a kurzor áll.

**Törlés**
: Kérdés után eltávolítja.

**Fájl szerkesztése**
: Megnyitja azt a fájlt, amelyben a makrók vannak.

# Megnyitott fájlok <a id="open-files"></a>

A szerkesztőben megnyitott fájlok, soronként egy. Az Enter arra a fájlra lép,
amelyen a kurzor áll, az Esc a megjelenítettet hagyja.

# Bővítmények adatai <a id="plugin-info"></a>

A szerkesztő által betöltött bővítmények: a név, be van-e kapcsolva, mit nyújt
és mit csinál. Ez néznivaló lista; kikapcsolni és beállítani a bővítményt a
Beállítások menü
[Bővítmények kezelése](#manage-plugins)
ablakában lehet.

# Billentyűtársítások <a id="key-bindings"></a>

A program műveleteihez tartozó billentyűk megtekintése és módosítása (a
Beállítások menü "Billentyűtársítások..." pontja).

**Enter**
: A társítás cseréje: nyomja le a billentyűt, amelyet hozzá akar rendelni.

**F5**
: Újabb billentyű hozzáadása ehhez a művelethez.

**F8, Del**
: A társítás eltávolítása.

**Mentés**
: A változtatások kiírása a
*~/.config/coole/keymap.ini*
fájlba.

**Billentyűfájl szerkesztése**
: A
*keymap.ini*
megnyitása a szerkesztőben.

**Terminálfájl szerkesztése**
: A terminál billentyűdefinícióinak megnyitása.

A \* jellel jelölt műveletek eltérnek az alapértelmezettől.

# Billentyűfigyelő <a id="key-sniffer"></a>

Nyomja meg az Elfogás gombot, majd egy tetszőleges billentyűt. Megmutatja:

```
Társítás   Jelképes név (például Ctrl-F5)
Művelet    A jelenlegi kiosztásban hozzá tartozó művelet
Nyers      Vezérlősorozat és a bájtok hexadecimálisan
Kód        Belső számkód
```

Hasznos a terminál billentyűivel kapcsolatos gondok felderítéséhez.

# KÖRNYEZET <a id="environment"></a>

**COOLE_DATADIR**
: Az adatfájlok könyvtára a {{pkgdatadir}} helyett.

**COOLE_PROFILE_ROOT**
: A felhasználói fájlok gyökere a saját könyvtár helyett.

**COOLE_SKIN**
: A használandó téma.

**COOLE_KEYMAP**
: A használandó billentyűfájl.

**COOLE_TMPDIR**, **TMPDIR**
: Az ideiglenes fájlok könyvtára.

**COOLE_NO_LUA**
: 1 értékkel a program Lua nélkül indul.

**COOLE_LOG_ENABLE**, **COOLE_LOG_FILE**, **COOLE_SPELL_LOG**
: A hibakeresési naplók.

# FÁJLOK <a id="files"></a>

*{{pkgdatadir}}/help/coole.md*
: A program súgófájlja.

*{{sysconfdir}}/coole/coole.ini*, *{{pkgdatadir}}/coole.ini*
: A rendszerszintű beállítások; csak akkor használatosak, ha a felhasználónak
nincs ~/.config/coole/ini fájlja.

*{{sysconfdir}}/coole/keymap.ini*, *{{sysconfdir}}/coole/coole.menu*
: A rendszerszintű billentyűk és felhasználói menü.

*{{pkgdatadir}}/syntax/\**
: A rendszerszintű szintaxisfájlok.

*{{plugins_dir}}/*
: A szerkesztő bővítményei.

*~/.config/coole/ini*, *~/.config/coole/keymap.ini*, *~/.config/coole/menu*
: A felhasználó saját beállításai, billentyűi és menüje.

*~/.local/share/coole/*
: A felhasználó előzményei, fájlpozíciói, vágólapja (clipboard), makrói
(macros, macros.d/), szintaxisa (syntax/Syntax), témái (skins/) és
Lua-szkriptjei (lua/scripts/).

*~/.cache/coole/*
: A blokkfájl (block) és a szerkesztő ideiglenes fájljai.

# Lásd még... <a id="see-also"></a>

cooledit(1), aspell(1), ctags(1).

Az angol nyelvű kézikönyv, a coole(1), a szerkesztő minden funkcióját leírja.
