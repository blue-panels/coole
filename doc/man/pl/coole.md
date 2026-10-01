---
date: wrzesień 2026
---

<!-- help:topics "Spis treści:" -->
# NAZWA <!-- help:skip -->

coole - edytor tekstu.

# UŻYTKOWANIE <!-- help:skip -->

**coole**
[opcje] [+wiersz] [plik1[:wiersz]] [plik2[:wiersz]] ...

# OPIS <a id="description"></a>

**coole**
to konsolowy edytor tekstu. Jest kontynuacją
**cooledit**,
edytora Paula Sheera, którego terminalowa wersja przez wiele lat była rozwijana
jako wbudowany edytor GNU Midnight Commandera; coole rozwija go dalej jako
osobny program.

Edytuje kilka plików naraz, każdy w osobnym oknie, także pliki binarne. Każdy
plik podany w wierszu poleceń otwiera się we własnym oknie; bez plików edytor
otwiera puste okno.

# OPCJE <a id="options"></a>

*+wiersz*
: Przejść do wiersza o tym numerze (bez spacji między znakiem
*+*
a liczbą). Można podać kilka numerów, ale użyty zostanie tylko ostatni i tylko
dla pierwszego pliku.

*plik:wiersz[:]*
: Otworzyć plik w tym wierszu, tak jak miejsce w pliku podają kompilatory i
grep.

*-V, --version*
: Pokazać wersję programu.

*-f, --datadir*
: Pokazać wkompilowane ścieżki wyszukiwania plików danych coole.

*-F, --datadir-info*
: Pokazać rozszerzone informacje o katalogach używanych przez coole.

*--configure-options*
: Pokazać opcje, z którymi program został skonfigurowany.

*--no-lua*
: Uruchomić bez środowiska Lua: żaden skrypt Lua nie jest wczytywany.

*-h, -?, --help*
: Pokazać opcje i ich działanie.

*-x, --xterm*
: Wymusić tryb xterm.

*-X, --no-x11*
: Nie korzystać z systemu X Window przy odczycie stanu klawiszy modyfikujących.

*-g, --oldmouse*
: Używać starego sposobu śledzenia myszy przez terminal.

*-d, --nomouse*
: Wyłączyć obsługę myszy.

*-t, --termcap*
: Używać termcap zamiast terminfo (tylko z biblioteką S-Lang).

*-s, --slow*
: Wolny terminal: ekran jest odświeżany rzadziej.

*-a, --stickchars*
: Rysować ramki zwykłymi znakami.

*-K plik, --keymap=plik*
: Wczytać przypisania klawiszy z tego pliku.

*--nokeymap*
: Nie wczytywać żadnego pliku klawiszy: używać wbudowanych przypisań.

*-b, --nocolor*
: Wymusić tryb czarno-biały.

*-c, --color*
: Wymusić tryb kolorowy.

*-S skórka, --skin=skórka*
: Użyć skórki o tej nazwie.

# Wbudowany edytor plików <a id="internal-file-editor"></a>

Wbudowany edytor plików to pełnoekranowy edytor ze wszystkimi zwykłymi
możliwościami. Plik większy niż
*editor_filesize_threshold*
(domyślnie 64 MB) otwiera się po pytaniu. Pliki binarne można edytować.

Obsługiwane możliwości to: kopiowanie, przenoszenie, kasowanie, wycinanie i
wklejanie bloków; cofanie klawisz po klawiszu; menu rozwijalne; wstawianie
plików; makra; szukanie i zastępowanie wyrażeniami regularnymi; zaznaczanie
tekstu strzałkami z Shiftem (jeśli terminal je rozróżnia); przełączanie
wstawiania i zastępowania; zawijanie wierszy; automatyczne wcięcia;
ustawialna szerokość tabulacji; podświetlanie składni dla różnych typów
plików; oraz przepuszczanie bloku tekstu przez polecenie powłoki, takie jak
indent czy ispell.

Rozdziały:
: [Opcje edytora](#editor-options)

Edytor jest bardzo prosty w użyciu i nie wymaga przygotowania. Aby zobaczyć,
co robią klawisze, wystarczy obejrzeć odpowiednie menu rozwijalne. Poza tym
strzałki z Shiftem zaznaczają tekst.
**Ctrl-Ins**
kopiuje do pliku wymiany
**~/.local/share/coole/clipboard**,
**Shift-Ins**
wkleja z niego,
**Shift-Del**
wycina do niego, a
**Ctrl-Del**
kasuje zaznaczony tekst. Zaznaczanie myszą też działa, a można je przesłonić
zwykłym zaznaczaniem terminala, trzymając Shift podczas przeciągania.

Aby zdefiniować makro, naciśnij
**Ctrl-R**,
a potem te klawisze, które mają zostać wykonane. Naciśnij ponownie
**Ctrl-R**,
kiedy skończysz, i przypisz makro do klawisza, naciskając go. Makro wykonuje
się tym klawiszem, a także skrótem
**Ctrl-A**
i przypisanym klawiszem. Makra są przechowywane w sekcji
**[editor]**
pliku
**~/.local/share/coole/macros**,
a kasuje się je, usuwając ich wiersz z tego pliku.

Zestaw znaków wyświetlanego tekstu zmienia Alt-e (M-e). Przekodowanie idzie z
wybranej strony kodowej na systemową. Aby je wyłączyć, wybierz
"\<No translation>" w oknie wyboru zestawu znaków.

Przycisk
**Filtr**
w oknie szukania
(**F7**)
ukrywa wszystkie wiersze, w których nie ma szukanego tekstu, z tymi samymi
ustawieniami rodzaju szukania, wielkości liter i całych słów co szukanie.
Numery wierszy pozostają oryginalne, a kolumna stanu zaznacza każdy ukryty
fragment. Zbiór ukrytych wierszy ustala się w chwili naciśnięcia przycisku:
edycja nie sprawdza warunku ponownie, więc widoczny wiersz, który zostanie
podzielony albo złączony, pozostaje widoczny, a wiersze napisane później też
pozostają widoczne, nawet jeśli nie pasują do warunku.
**M-s**
zdejmuje filtr; naciśnięty ponownie zakłada ostatnie szukanie jako filtr.
Pozycja "Rozwiń wszystko" w menu Polecenia również go zdejmuje.

# Opcje edytora <a id="editor-options"></a>

Ustawienia
[wbudowanego edytora](#internal-file-editor).
Otwiera je pozycja
**Ogólne...**
menu
**Opcje**
edytora.

*Tryb zawijania.*
Wyłączony, bieżące formatowanie akapitu albo zawijanie maszynowe, które łamie
wiersz na zadanej długości w trakcie pisania.

*Udawanie połówek tabulacji.*
Między tekstem a lewym marginesem ruch i wcięcie idą o pół tabulacji i
wypełniane są spacjami; w pozostałych miejscach tabulacja jest zwykła.

*Backspace przez tabulacje.*
Jedno naciśnięcie Backspace usuwa całe wcięcie do lewego marginesu, gdy między
kursorem a marginesem nie ma tekstu.

*Wypełnianie tabulacji spacjami.*
Zamiast znaku tabulacji wstawiane są spacje do następnej pozycji tabulacji.

*Szerokość tabulacji.*
Szerokość, którą zajmuje znak tabulacji. Domyślnie 8.

*Enter robi wcięcie.*
Nowy wiersz zaczyna się z wcięciem wiersza powyżej.

*Potwierdzanie zapisu.*
Przed zapisaniem pliku program pyta.

*Zapamiętywanie pozycji w pliku.*
Plik otwiera się tam, gdzie go ostatnio zostawiono.

*Pokazywanie spacji na końcu wiersza.*
Spacje na końcu wiersza są oznaczane.

*Pokazywanie tabulacji.*
Znaki tabulacji są oznaczane.

*Pokazywanie znaków sterujących.*
Znaki sterujące tekstu są wypisywane, a nie ukrywane.

*Podświetlanie składni.*
Tekst jest kolorowany regułami składni dla jego typu pliku.

*Kursor za wstawionym blokiem.*
Po wstawieniu bloku kursor zostaje na jego końcu, a nie na początku.

*Trwałe zaznaczenie.*
Zaznaczenie pozostaje przy ruchu kursora, zamiast znikać.

*Kursor za końcem wiersza.*
Kursor może stać za ostatnim znakiem wiersza.

*Cofanie grupowe.*
Jedno cofnięcie przywraca serię zmian tego samego rodzaju, a nie jedno
naciśnięcie klawisza.

*Długość wiersza przy zawijaniu.*
Kolumna, na której tryby zawijania łamią wiersz. Domyślnie 72.

# Zapisz jako <a id="save-file-as"></a>

Nazwa, pod którą zapisać plik, i końce wierszy, z którymi go zapisać: takie,
jakie są w pliku, Unix (LF), Windows i DOS (CR LF) albo Macintosh (CR).

# Tryb zapisu <a id="edit-save-mode"></a>

Jak zapisywany jest plik:

**Szybki zapis**
: Pisze od razu na pliku. Szybko, a awaria w połowie zostawia plik zapisany do
połowy.

**Bezpieczny zapis**
: Najpierw pisze plik tymczasowy i przemianowuje go na pierwotny, gdy jest
cały, więc awaria nie narusza oryginału.

**Kopie zapasowe z rozszerzeniem**
: Bezpieczny zapis, a oryginał zostaje pod swoją nazwą z dodanym rozszerzeniem
z linii wejściowej, domyślnie "~".

**Sprawdzanie końca wiersza POSIX**
: Pyta o brakujący koniec wiersza na końcu pliku przed zapisem.

# Przeglądarka makr <a id="macro-explorer"></a>

Nagrane makra, z klawiszem, na który każde odpowiada, i tym, co robi. Przyciski
to

**Uruchom**
: Odtwarza makro, na którym stoi kursor.

**Usuń**
: Usuwa je po pytaniu.

**Edytuj plik**
: Otwiera plik, w którym mieszkają makra.

# Otwarte pliki <a id="open-files"></a>

Pliki otwarte w edytorze, po jednym w wierszu. Enter przechodzi do pliku, na
którym stoi kursor, Esc zostawia pokazywany.

# Informacje o wtyczkach <a id="plugin-info"></a>

Wtyczki, które edytor wczytał: nazwa, czy jest włączona, co daje i co robi. To
lista do oglądania; wtyczkę wyłącza się i jej ustawienia otwiera w oknie
[Zarządzanie wtyczkami](#manage-plugins)
menu Opcje.

# Przypisania klawiszy <a id="key-bindings"></a>

Przeglądanie i zmiana skrótów klawiszowych dla działań programu (menu Opcje,
"Przypisania klawiszy...").

**Enter**
: Zmienia skrót: naciśnij klawisz, który chcesz przypisać.

**F5**
: Dodaje kolejny skrót dla tego działania.

**F8, Del**
: Usuwa skrót.

**Zapisz**
: Zapisuje zmiany w pliku
*~/.config/coole/keymap.ini*.

**Edytuj plik klawiszy**
: Otwiera
*keymap.ini*
w edytorze.

**Edytuj plik terminala**
: Otwiera definicje klawiszy terminala.

Działania oznaczone \* różnią się od domyślnych.

# Podsłuch klawiszy <a id="key-sniffer"></a>

Naciśnij Przechwyć, a potem dowolny klawisz. Pokazuje:

```
Skrót       Nazwa symboliczna (na przykład Ctrl-F5)
Działanie   Działanie przypisane w bieżącej mapie
Surowo      Sekwencja sterująca i bajty szesnastkowo
Kod         Wewnętrzny kod liczbowy
```

Przydatne przy szukaniu przyczyn kłopotów z klawiszami terminala.

# ŚRODOWISKO <a id="environment"></a>

**COOLE_DATADIR**
: Katalog plików danych, zamiast {{pkgdatadir}}.

**COOLE_PROFILE_ROOT**
: Korzeń plików użytkownika, zamiast katalogu domowego.

**COOLE_SKIN**
: Skórka do użycia.

**COOLE_KEYMAP**
: Plik klawiszy do użycia.

**COOLE_TMPDIR**, **TMPDIR**
: Katalog plików tymczasowych.

**COOLE_NO_LUA**
: Wartość 1 uruchamia program bez środowiska Lua.

**COOLE_LOG_ENABLE**, **COOLE_LOG_FILE**, **COOLE_SPELL_LOG**
: Dzienniki diagnostyczne.

# PLIKI <a id="files"></a>

*{{pkgdatadir}}/help/coole.md*
: Plik pomocy programu.

*{{sysconfdir}}/coole/coole.ini*, *{{pkgdatadir}}/coole.ini*
: Ustawienia systemowe, używane tylko wtedy, gdy użytkownik nie ma pliku
~/.config/coole/ini.

*{{sysconfdir}}/coole/keymap.ini*, *{{sysconfdir}}/coole/coole.menu*
: Systemowe klawisze i menu użytkownika.

*{{pkgdatadir}}/syntax/\**
: Systemowe pliki składni.

*{{plugins_dir}}/*
: Wtyczki edytora.

*~/.config/coole/ini*, *~/.config/coole/keymap.ini*, *~/.config/coole/menu*
: Własne ustawienia, klawisze i menu użytkownika.

*~/.local/share/coole/*
: Historia, pozycje w plikach, schowek (clipboard), makra (macros,
macros.d/), składnia (syntax/Syntax), skórki (skins/) i skrypty Lua
(lua/scripts/) użytkownika.

*~/.cache/coole/*
: Plik bloku (block) i pliki tymczasowe edytora.

# ZOBACZ TAKŻE <a id="see-also"></a>

cooledit(1), aspell(1), ctags(1).

Angielski podręcznik, coole(1), opisuje wszystkie funkcje edytora.
