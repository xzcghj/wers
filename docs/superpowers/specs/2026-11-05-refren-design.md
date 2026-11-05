# WERS Refren: specyfikacja wersji 1

Data: 2026-11-05. Status: czeka na przegląd użytkownika. Implementacja rusza dopiero po akceptacji tego pliku i planu oraz po domknięciu Barwy.

## Cel

Druga nowa wtyczka marki WERS: darmowy VST3 na Windows, który z jednej ścieżki wokalu robi dubel, ekipę, chór, głęboki głos, szept albo mocny efekt. Działa od razu z gotowego presetu i ma jeden główny suwak **Ile**. Styl pracy jest podobny do Omnivoxa od Cymatics, ale Refren nie jest jego kopią.

Refren różni się od takich wtyczek na dwa sposoby:
- **Słowa zostają czytelne.** Warstwy cichną na głoskach syczących i zwartych, które zostają ostre w suchym głosie. Klasyfikator głosek z WERS-a robi to automatycznie.
- **Presety naprawdę się różnią.** Jest ich co najmniej 120, a test pilnuje, żeby żadne dwa nie brzmiały prawie tak samo.

Sukces: wtyczka przechodzi wszystkie pomiary z tej specyfikacji, a użytkownik uznaje po odsłuchu grup, że presety są dobre i różne.

## Decyzje

| Decyzja | Wybór | Kto |
| --- | --- | --- |
| Która wtyczka teraz | Refren, w stylu Omnivoxa | Użytkownik |
| Presety | Ma ich być dużo i mają być różne | Użytkownik |
| Budowa | Warstwy (wariant B) | Użytkownik |
| Projekt dźwięku, ekranu i testów | Zaakceptowany w rozmowie | Użytkownik |
| Opłaty | Jak cała seria: darmowo dla innych, zero kosztów, Windows, VST3, bez podpisu | Użytkownik, wcześniej |
| Zmiana wysokości | TD-PSOLA z wykrywaniem wysokości, tylko dla pojedynczego głosu | Claude |
| Harmonie | Stałe interwały. W tonacji dopiero po v1 | Claude |
| Nagrania użytkownika | Nowe numery nie trafiają do chmury. Próbki i pomiary powstają na jego komputerze | Claude, jak przy Barwie |
| Nazwy presetów | Bez nazwisk artystów i nazw marek | Claude |
| Nazwa Refren | Nie znalazłem wtyczki o tej nazwie w szybkim wyszukiwaniu. To nie jest sprawdzenie znaków towarowych | Claude |

## Poza zakresem v1

Harmonie w tonacji, MIDI, tryb małego opóźnienia do nagrywania, zmiana wysokości dźwięków wielogłosowych, efekty w dowolnej kolejności, wejście boczne, macOS, AU, AAX i publiczne wydanie. Przed wydaniem trzeba przeczytać aktualną licencję JUCE i sprawdzić nazwę.

## Zależności

Refren powstaje po Barwie, na tej samej bazie. Do `libs/dsp` przechodzi z Barwy to, czego potrzebują obie wtyczki:
- `CharacterShaper`, czyli cztery charaktery saturacji,
- `Ramp` i `Gate` do płynnych zmian,
- `Tilt` i `DcBlock`.

Ten ruch nie może zmienić dźwięku Barwy. Pilnują tego jej testy i pomiary. `PresetStore` dostaje grupę i krótki opis presetu, a stare wywołania działają bez zmian.

Pogłos Refrenu powstaje jako nowa klasa wzorowana na `Space` z WERS-a. `Space` zostaje bez zmian, żeby WERS brzmiał jak dotąd.

## Dźwięk

### Droga sygnału

```
wejście ─┬─ opóźnienie L ───────────── suchy ─────────────────────────────┐
         ├─ klasyfikator głosek ─ maska                                   │
         ├─ wykrywanie wysokości                                          │
         └─ warstwy 1–4 ─ suma ─ ziarno ─ ton ─ ruch ─┬─────── mokry ─────┤
                                                      └─ echo, pogłos ────┴─ Ile ─ wyrównanie ─ Output
```

```
a = Ile (0..1)
suchy_g = 1 + a × (Suchy − 1)
wyjście = (suchy_g × suchy + a × mokry) × (1 + a × (Wyrównanie − 1)) × Output
```

Przy Ile 0% zostaje samo opóźnione wejście, co do próbki. Delta wysyła na wyjście sam mokry sygnał. Stereo na wejściu zostaje stereo w suchej ścieżce, a warstwy powstają z sumy mono, liczonej po wyczyszczeniu NaN i Inf. Przy wyjściu mono mokry sygnał to średnia kanałów.

### Opóźnienie

`L = round(0,030 × fs)`, stałe dla każdego presetu i częstotliwości próbkowania. Dzięki temu zmiana presetu nie zmienia opóźnienia, a wykrywanie zdąża przed dźwiękiem. Bypass trzyma to samo opóźnienie i przechodzi w 10 ms.

### Warstwy

Preset ma do 4 warstw. Każda ma te ustawienia:

| Ustawienie | Zakres | Działanie |
| --- | --- | --- |
| Włączona | wł/wył | |
| Źródło | Głos, Szept, Sub, Robot | Patrz niżej |
| Głosy | 1–16 | Razem najwyżej 32. Nadmiar odpada z ostatnich warstw |
| Interwał | −24 do +24 półtonów | Głos i Sub |
| Barwa głosu | −12 do +12 półtonów | Przesuwa formanty bez zmiany wysokości. Głos i Robot |
| Poziom | −48 do +6 dB | Podzielony przez pierwiastek z liczby głosów, więc więcej głosów nie znaczy głośniej |
| Szerokość | 0–100% | Rozkład głosów w panoramie, równa moc |
| Czas | 0–40 ms | Przesunięcia głosów względem suchego, od −8 ms do +40 ms. Warstwy z interwałem i warstwy Robot zaczynają od 0, bo zmiana wysokości zużywa zapas opóźnienia |
| Rozstrojenie | 0–50 centów | Wolne, losowe pływanie wysokości każdego głosu |
| Filtr | górny 20 Hz–2 kHz, dolny 1–20 kHz | Na całą warstwę |
| Nuta | C2–C5 | Tylko Robot |

Mnożniki `width` i `time` działają do granic zakresu warstwy. Głos pojedynczy to odczyt z linii opóźniającej z interpolacją sześcienną. Odczyt prowadzi gładki szum o częstotliwości 0,3–1,2 Hz, inny dla każdego głosu. Głos ma też drobną własną różnicę poziomu (±1,5 dB) i barwy (±2 dB powyżej 2,5 kHz), żeby brzmiał jak inna osoba. Wszystkie losowe wartości wynikają ze stałego ziarna i numeru głosu, więc po `reset` wynik jest powtarzalny.

### Źródła

| Źródło | Co robi | Jak |
| --- | --- | --- |
| Głos, interwał 0 | Kopie ścieżki | Bez zmiany wysokości, bez dodatkowych artefaktów |
| Głos, interwał różny od 0 | Kopie o interwał wyżej lub niżej | TD-PSOLA: znaczniki okresów, okna Hanna o długości dwóch okresów, formanty przez zmianę tempa odczytu okna. Na dźwiękach bezdźwięcznych warstwa cichnie w 5 ms |
| Szept | Ten sam tekst szeptem | 16 pasm od 200 Hz do 12 kHz: obwiednia głosu prowadzi szum. Atak 5 ms, zwolnienie 30 ms. Maska na to źródło nie działa |
| Sub | Czysty ton pod głosem, domyślnie oktawę niżej | Oscylator sinus plus 30% drugiej harmonicznej, idący za wysokością głosu, z jego obwiednią. Bez głosu dźwięcznego cisza |
| Robot | Stała wysokość zamiast melodii | TD-PSOLA z odstępem okien wynikającym z parametru Nuta |

Wykrywanie wysokości: YIN na sygnale zdecymowanym do około 12 kHz, krok 2,5 ms, zakres 75–1000 Hz, próg 0,15. Poniżej pewności 0,5 ramka liczy się jako bezdźwięczna.

### Maska: Spółgłoski w środku

Klasyfikator z `libs/dsp` pracuje na niepóźnionej sumie mono:

```
w = clamp(samogłoska + 0,2 × zwarta, 0, 1)
w_min = minimum w z ostatnich Cd = 20 ms
c = (1 − s) + s × w_min        s to parametr Spółgłoski, 0..1
```

Sygnał dla głosów bez zmiany wysokości to `y(n) = x(n − Cd) × c(n)`, ze zboczem najwyżej 3 ms. W warstwach z interwałem i warstwach Robot TD-PSOLA pracuje na niemaskowanym wejściu, a maska, wyrównana w czasie, trafia na jego wynik. Minimum z okna sprawia, że maska zamyka się przed głoską syczącą, a nie po niej. Otwiera się później, na początku samogłoski, co nie przeszkadza. W obu przypadkach maska jest nałożona przed przesunięciami w czasie, więc każdy głos gaśnie dokładnie na swojej kopii głoski.

### Szyna

Wszystko poniżej działa tylko na mokrym sygnale:

- **Ziarno:** `CharacterShaper` przy oversamplingu 2x, wybór charakteru, Drive 0–36 dB. Przy 0 dB nic nie zmienia. Jego opóźnienie jest stałe i wliczone w `L`.
- **Ton:** nachylenie wokół 1 kHz, ±6 dB.
- **Ruch:** Brak, Autopan, Tremolo albo Flanger, z częstotliwością 0,05–10 Hz i głębokością. Sprzężenie flangera ma limit 0,7.
- **Echo:** stereo, w tempie hosta, a bez tempa przy 120 BPM. Czasy 1/16, 1/8, 1/8 z kropką, 1/4, 1/4 z kropką, 1/2 i slap 80 ms. Sprzężenie do 90%, ping-pong, filtr dolny.
- **Pogłos:** 8 linii z macierzą Householdera, rozmiar, czas 0,2–8 s, tłumienie góry, predelay 0–80 ms, lekka modulacja linii. Shimmer to oktawa w górę z ziarnistego przesuwacza w pętli sprzężenia, z limitem zapewniającym stabilność.
- **Pod słowa:** parametr Chowanie przycisza echo i pogłos, kiedy głos gra, a pozwala im wrócić w przerwach, jak w WERS-ie.
- **Wysyłka suchego:** część suchego sygnału może iść do echa i pogłosu, dla presetów z głosem w przestrzeni.

### Głośność

Bez Auto poziomu, bo ogony pogłosu by pompowały. Każdy preset fabryczny ma stałe **Wyrównanie**, ustawione narzędziem na etapie tworzenia presetów, tak żeby przy Ile 100% głośność z wagą K była równa głośności wejścia. Wartość jest zapisana w presecie.

### Płynność

- Parametry są czytane co 32 próbki (skalowane z częstotliwością) i wygładzane przez 20 ms.
- Zmiany, których nie da się wygładzić (źródło, liczba głosów, interwał, barwa, nuta), przechodzą przez bramkę: mokry sygnał gaśnie w 20 ms, zmiana, 10 ms ciszy, powrót w 20 ms. Suchy głos gra dalej bez zmian.
- Nie ma alokacji, blokad, plików ani wątków w torze audio. Pamięć jest przydzielana w `prepare`.

## Parametry

Identyfikatory są zapisywane w projektach i nigdy się nie zmieniają. `<n>` to numer warstwy od 1 do 4.

| Identyfikator | Nazwa | Zakres | Domyślnie | Automatyzacja |
| --- | --- | --- | --- | --- |
| `amount` | Ile | 0–100% | 100% | Tak |
| `width` | Szerokość | 0–200%, mnożnik warstw | 100% | Tak |
| `time` | Czas | 0–200%, mnożnik warstw | 100% | Tak |
| `tilt` | Ton | −100 do +100% | 0% | Tak |
| `grain` | Ziarno | 0–36 dB | 0 dB | Tak |
| `grainChar` | Charakter ziarna | Lampa, Taśma, Transformator, Tranzystor | Lampa | Nie |
| `motion` | Rodzaj ruchu | Brak, Autopan, Tremolo, Flanger | Brak | Nie |
| `motionRate` | Tempo ruchu | 0,05–10 Hz | 1 Hz | Tak |
| `motionDepth` | Ruch | 0–100% | 0% | Tak |
| `echoMix` | Echo | 0–100% | 0% | Tak |
| `echoTime` | Czas echa | lista jak wyżej | 1/8 | Nie |
| `echoFeedback` | Sprzężenie echa | 0–90% | 25% | Tak |
| `echoPing` | Ping-pong | wł/wył | wył | Nie |
| `echoTone` | Góra echa | 500 Hz–12 kHz | 6 kHz | Tak |
| `verbMix` | Pogłos | 0–100% | 0% | Tak |
| `verbSize` | Rozmiar | 0–100% | 50% | Nie |
| `verbDecay` | Czas pogłosu | 0,2–8 s | 1,5 s | Tak |
| `verbTone` | Góra pogłosu | 1–16 kHz | 6 kHz | Tak |
| `verbPre` | Predelay | 0–80 ms | 10 ms | Tak |
| `verbShimmer` | Shimmer | 0–100% | 0% | Tak |
| `duck` | Chowanie | 0–100% | 50% | Tak |
| `sendDry` | Wysyłka suchego | 0–100% | 0% | Tak |
| `consonants` | Spółgłoski w środku | 0–100% | 100% | Tak |
| `dry` | Suchy | −48 do 0 dB | 0 dB | Tak |
| `trim` | Wyrównanie | −24 do +12 dB | 0 dB | Nie |
| `output` | Output | −24 do +24 dB | 0 dB | Tak |
| `delta` | Delta | wł/wył | wył | Nie |
| `bypass` | Bypass | wł/wył | wył | Tak |
| `l<n>On` | Warstwa włączona | wł/wył | 1: wł, reszta: wył | Nie |
| `l<n>Source` | Źródło | Głos, Szept, Sub, Robot | Głos | Nie |
| `l<n>Voices` | Głosy | 1–16 | 2 | Nie |
| `l<n>Semis` | Interwał | −24 do +24 | 0 | Nie |
| `l<n>Formant` | Barwa głosu | −12 do +12 | 0 | Nie |
| `l<n>Level` | Poziom | −48 do +6 dB | −6 dB | Tak |
| `l<n>Width` | Szerokość warstwy | 0–100% | 50% | Tak |
| `l<n>Time` | Czas warstwy | 0–40 ms | 12 ms | Tak |
| `l<n>Detune` | Rozstrojenie | 0–50 centów | 8 centów | Tak |
| `l<n>Low` | Filtr górny | 20 Hz–2 kHz | 20 Hz | Tak |
| `l<n>High` | Filtr dolny | 1–20 kHz | 20 kHz | Tak |
| `l<n>Note` | Nuta | C2–C5 | C3 | Nie |

## Presety

### Grupy

Co najmniej 120 fabrycznych. Nazwy poniżej są przykładami, a pełną listę ustala plan i odsłuch.

| Grupa | Co najmniej | Przepis | Przykłady |
| --- | --- | --- | --- |
| Dubel | 12 | 1–4 głosy, mały czas, małe rozstrojenie, bez pogłosu | Ciasny, Luźny, Lewo i prawo |
| Refren | 12 | 6–14 głosów, szeroko, trochę oktawy, krótki pogłos | Szeroki hook, Melodyjny, Stadion |
| Ekipa | 10 | Duży czas, różne barwy głosu, sucho | Trzech ziomków, Tłum, Trybuny |
| Chór | 10 | 16–32 głosy, długi pogłos, mało suchego | Gospel, Katedra, Ściana |
| Głęboki | 10 | Oktawa w dół, ciemna barwa, Sub | Sub pod głosem, Demon, Olbrzym |
| Wysoki | 10 | Oktawa w górę, jasna barwa | Anioły, Hyperpop, Chipmunk |
| Harmonie | 10 | Stałe interwały: oktawy, kwinty, kwarty | Kwinta, Oktawy, Kwarta w tle |
| Ad-liby | 12 | Mało suchego, filtry, echa w tempie | Zza ściany, Ping-pong, Telefon |
| Szept | 8 | Warstwa Szept, bez maski | Szept obok, Oddech, W głowie |
| Przester | 10 | Mocne ziarno na warstwach | Rage, Megafon, Spalony |
| Efekty | 12 | Robot, flanger, shimmer, radio | Robot, Radio, Kosmos, Shimmer |
| Instrumenty | 8 | Same głosy bez zmiany wysokości, maska wyłączona | Gitary, Pad, Szeroki synth |

### Zasady

- Preset zmienia wszystko oprócz `amount`, `output`, `delta` i `bypass`. Czego preset nie wymienia, przyjmuje wartość domyślną.
- Nazwa ma najwyżej 18 znaków, żeby mieściła się na karcie. Każdy preset ma też opis w jednym zdaniu, widoczny jako podpowiedź.
- Nowa instancja startuje z presetem Luźny z grupy Dubel. Stan z projektu ma pierwszeństwo.
- Presety własne to pliki XML w `%APPDATA%\WERS\presets\refren`, w tym samym formacie co w Barwie, widoczne w grupie Moje. Gwiazdki są zapisane w tym samym folderze, w pliku `ulubione.txt`.
- **Losuj** rusza względem obecnego presetu: `width`, `time`, Rozstrojenie, `tilt`, `verbMix` i `echoMix`. Nie rusza interwałów, barwy głosu, źródeł ani liczby głosów, żeby nic nie zafałszowało. Nazwa dostaje wtedy gwiazdkę na końcu.

### Odcisk brzmienia

Test przepuszcza każdy preset przez tę samą syntetyczną frazę głosu i mierzy cechy:

| Cecha | Różnica uznana za słyszalną |
| --- | --- |
| Szerokość: moc boków do środka | 2 dB |
| Udział dołu, poniżej 200 Hz | 1,5 dB |
| Udział góry, powyżej 5 kHz | 1,5 dB |
| Ogon: czas spadku o 30 dB po końcu frazy | 100 ms |
| Ruch: zmienność poziomu boków w czasie | 1,5 dB |
| Energia w okolicy połowy wysokości głosu | 3 dB |
| Energia w okolicy podwójnej wysokości głosu | 3 dB |
| Gęstość: liczba szczytów obwiedni na sekundę | 20% |

Dwa presety są zbyt podobne, gdy żadna cecha nie różni się ponad próg. Taka para nie przechodzi testu. Progów nie obniżam, żeby przepchnąć preset: zmieniam albo usuwam preset.

## Ekran

Rozmiar bazowy 1040 × 680, ze skalowaniem i ciemnym wyglądem serii.

```
┌ WERS Refren ─────────────── [A][B] [Losuj] [Szczegóły] [Delta] [Bypass] ┐
│ ★ Ulubione   ┌────────┐┌────────┐┌────────┐     Scena   ·  · ·  ·       │
│   Moje       │ Ciasny ││ Luźny  ││ L i P  │          ·  · ·●· ·  ·      │
│   Dubel      └────────┘└────────┘└────────┘             ·   ·           │
│   Refren     ┌────────┐┌────────┐┌────────┐     Ile ━━━━━━━●━━━         │
│   Ekipa ...  │        ││        ││        │                             │
├ Szerokość  Czas  Ton  Ziarno  Ruch  Pogłos  Echo  Spółgłoski  Output ───┤
└─────────────────────────────────────────────────────────────────────────┘
```

- **Grupy i karty:** lewa kolumna to grupy, środek to karty presetów danej grupy z gwiazdką i przewijaniem. Strzałki chodzą po grupie. Zapisywanie, A i B są z `PresetBar`.
- **Scena:** każda kropka to jeden głos. Oś pozioma to panorama, oś pionowa to interwał, a jasność to bieżący poziom warstwy razem z maską. Na „s” kropki gasną, a na samogłoskach wracają. Suchy głos to większa kropka na środku. Wartości trafiają na ekran przez zmienne atomowe, bez blokad.
- **Dolne suwaki:** Szerokość, Czas, Ton, Ziarno, Ruch, Pogłos, Echo, Spółgłoski i Output.
- **Szczegóły:** zamiast kart pokazują 4 kolumny warstw oraz resztę parametrów szyny, Suchy i Wyrównanie.
- **Kolory:** jak w serii. Mięta to warstwy i to, co wtyczka dodaje. Bursztyn tylko przy szczycie wyjścia ponad 0 dBFS. Bez nowych kolorów i czcionek.
- **Tekst:** po polsku, w `plugins/refren/ui/Strings.h`.

## Stan i identyfikatory

- `PRODUCT_NAME` „WERS Refren”, `PLUGIN_CODE` `Wrfn`, `BUNDLE_ID` `app.wers.refren`, cel `WersRefren`, kategoria `Fx Modulation`.
- Układy: mono do mono, mono do stereo, stereo do stereo. Inne są odrzucane.
- Stan: magia `WRFN`, numer wersji, XML parametrów, skala ekranu, sloty A i B, nazwa bieżącego presetu. Brakujący parametr dostaje wartość domyślną, a uszkodzony stan jest odrzucany bez skutków.

## Narzędzia na komputerze użytkownika

Nagrania zostają na jego komputerze, a do sesji wracają tylko liczby.

- **`refren_render`:** dla ścieżek z pliku listy zapisuje 20 sekund każdej próby (oktawa w dół, oktawa w górę, kwinta, barwa ciemna, barwa jasna, Sub, Robot, Szept) do `logs\refren_probe`. Słucha tego użytkownik.
- **`refren_measure`:** dla tych samych ścieżek zapisuje same liczby: udział ramek dźwięcznych, stabilność wysokości, spadek syczących na bokach i poziom względem wejścia.
- **`refren_measure --cpu`:** procent jednego rdzenia dla najcięższego presetu. Limit 5%.

## Testy

| Test | Warunek | Cel |
| --- | --- | --- |
| Neutralność | Ile 0%, każdy preset | Różnica z wejściem opóźnionym o `L` poniżej −140 dB |
| Opóźnienie | Każdy preset i częstotliwość | `getLatencySamples()` równe `round(0,030 × fs)`, zmierzone na impulsie |
| Rozmiar bloku | 32, 256, 4096, losowy | Wynik identyczny co do próbki |
| Częstotliwości | 44,1, 48, 88,2, 96, 176,4, 192 kHz | Neutralność, trafność oktawy, wynik skończony |
| Oktawa w dół i w górę | Syntetyczny głos 90–400 Hz, stały i z poślizgiem | 90% ramek w granicach ±5 centów |
| Robot | Wejście o zmiennej wysokości | Wyjście na nucie ±5 centów |
| Sub | Syntetyczny głos i ciche syczące | Ton f0/2 ±5 centów, na syczących co najmniej 40 dB ciszej |
| Barwa głosu | Syntetyczna samogłoska, ±6 półtonów | Formanty przesunięte w granicach ±1 półtonu, wysokość bez zmian |
| Wyrównanie w czasie | Warstwa Głos i warstwa z interwałem, Czas 0 | Obwiednia w granicach ±2 ms względem suchego |
| Maska, syczące | Na przemian samogłoska i szum 4–9 kHz, 8 głosów, Czas 30 ms | Na bokach co najmniej 12 dB mniej niż bez maski, licząc od 5 ms po początku głoski |
| Maska, samogłoski | Ten sam sygnał | Warstwy najwyżej 1 dB ciszej, licząc od 30 ms po początku samogłoski |
| Maska wyłączona | Spółgłoski 0% | Wynik zgodny z torem bez klasyfikatora |
| Głośność presetu | Każdy fabryczny, Ile 100%, fraza ze szczytem −12 i −3 dBFS | Waga K ±1 dB względem wejścia, szczyt najwyżej 3 dB wyżej |
| Mono | Każdy fabryczny, suma kanałów | Głośność najwyżej 3 dB niższa niż stereo. Tyle tracą niezależne warstwy z natury, a więcej oznaczałoby znoszenie się głosów |
| Różnorodność | Wszystkie pary presetów | Patrz Odcisk brzmienia |
| Liczba presetów | Grupy | Co najmniej 120, a w każdej grupie co najmniej tyle, ile w tabeli |
| Bez trzasków | Zmiana presetu, źródła, głosów i interwału na sinusie i frazie | Skok próbki najwyżej 2 razy większy niż bez zmiany, suchy głos nietknięty |
| Stabilność | Sprzężenie 90%, shimmer 100%, flanger na limicie, czas 8 s, impuls | Ogon maleje, brak NaN |
| Gorące wejście | Fraza do +5,9 dBFS | Wynik skończony, skok próbki najwyżej 2 razy większy niż na wejściu |
| Fuzz | NaN, Inf, DC, skrajne parametry, zmiana wszystkiego na raz | Wynik skończony, normalne działanie po 1 s |
| Host | Blok większy niż zgłoszony, pojedyncze próbki, mono, zmiana częstotliwości, brak tempa, zmiana tempa | Wynik skończony, opóźnienie poprawne |
| Alokacje | Wątek audio, ze zmianą parametrów | 0 |
| Stan | Zapis i odczyt, stary stan, uszkodzony stan | Wartości wracają, nic się nie psuje |
| Pliki presetów | Polskie litery, brak folderu, uszkodzony plik, ulubione bez pliku | Bez awarii, z komunikatem na pasku |
| Ekran | Otwieranie i zamykanie w każdym stanie, grupy, gwiazdka, Losuj, Szczegóły | Bez awarii. Losuj nie zmienia interwałów. Gwiazdka zostaje po ponownym otwarciu |
| Obciążenie | Najcięższy preset: 4 warstwy, 32 głosy, ziarno, echo, pogłos z shimmerem, 48 kHz, stereo, blok 256 | Najwyżej 5% rdzenia na Ryzen 9 7900X |
| Sanitizery | ASan, UBSan, TSan na obu poziomach testów | Bez uwag |
| pluginval | Poziom 10, Linux i Windows | Zaliczony |

## Kolejność i ryzyka

1. **Próbka zmiany wysokości, zanim powstanie reszta.** Powstają tylko wykrywanie wysokości, TD-PSOLA, Sub, Szept i `refren_render`. Użytkownik słucha WAV-ów ze swoich suchych śladów.
2. **Jeśli próbka brzmi źle na szybkim rapie,** zatrzymuję się i razem wybieramy: inna metoda (ziarnista albo fazowa, z większym opóźnieniem), węższy zakres interwałów albo oktawa w dół tylko przez Sub.
3. Dopiero potem warstwy, maska, szyna i ekran, a na końcu presety, grupa po grupie.

| Ryzyko | Co zrobię |
| --- | --- |
| Zmiana wysokości brzmi sztucznie na szybkim rapie | Próbka na początku, maska wycina najtrudniejsze głoski, cisza na bezdźwięcznych |
| Presety powstają bez słuchu po mojej stronie | Przepisy grup, pomiary, odcisk brzmienia i odsłuch użytkownika grupami |
| Pogłos brzmi metalicznie | Modulacja linii, pomiar gęstości ech, odsłuch grupy Chór |
| Mono: warstwy znoszą się po zsumowaniu | Różne czasy głosów i test mono dla każdego presetu |
| Ponad 70 parametrów | Rzadko używane są niedostępne dla automatyzacji i schowane pod Szczegóły |

## Odsłuchy

1. Próbka zmiany wysokości na suchych śladach.
2. Grupy Dubel, Refren, Ekipa i Chór.
3. Grupy Głęboki, Wysoki, Harmonie i Szept.
4. Grupy Ad-liby, Przester, Efekty i Instrumenty.

W każdej rundzie użytkownik pisze, które presety wyrzucić albo zmienić. Brzmienie ocenia użytkownik, a ja tylko mierzę.
