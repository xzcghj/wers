# WERS Barwa i wspólna baza serii: specyfikacja wersji 1

Data: 2026-11-05. Status: czeka na przegląd użytkownika. Implementacja rusza dopiero po akceptacji tego pliku i planu.

## Cel

Pierwsza nowa wtyczka marki WERS: darmowy saturator VST3 na Windows, zaprojektowany przede wszystkim do wokalu. Dodaje gęstość i barwę samogłoskom, a głoski syczące zostawia czyste. Po wyłączeniu maski głosek działa też na bicie i basie.

Sukces: wtyczka przechodzi wszystkie pomiary z tej specyfikacji, a użytkownik uznaje brzmienie charakterów za dobre po własnych odsłuchach. Przy okazji powstaje wspólna baza, na której kolejne wtyczki (najpierw kompresor Akcent) nie zaczynają od zera.

## Decyzje

| Decyzja | Wybór | Kto |
| --- | --- | --- |
| Opłaty | Darmowo dla innych, zero kosztów: Windows, VST3, bez podpisu cyfrowego | Użytkownik |
| Kierunek Barwy | Przede wszystkim wokal, maskę można wyłączyć | Użytkownik |
| Plan v1 | Zaakceptowany w rozmowie | Użytkownik |
| Kod | Zamknięty, JUCE Starter. Otworzyć można później, odwrotnie już nie | Claude |
| Budowa serii | Osobny VST3 dla każdej wtyczki i wspólna baza w jednym repozytorium | Claude |
| Nagrania użytkownika | Nowe numery nie trafiają do chmury. Pomiary działają na jego komputerze, do sesji wracają same liczby | Claude, po pytaniu użytkownika o chmurę |
| Język | Polski w v1. Teksty w jednym pliku, żeby angielski był prostym dodatkiem | Claude |

## Poza zakresem v1

Modulacja, więcej niż 3 pasma, krzywa rysowana ręcznie, faza liniowa, tryb M/S, kilka instancji połączonych ze sobą, macOS, AU, AAX i publiczne wydanie. Wydanie to osobny krok, przed którym trzeba przeczytać aktualną licencję JUCE i sprawdzić nazwę. Barwa nie goni Saturn 2 szerokością (6 pasm, 28 stylów, modulacja).

## Wspólna baza

### Układ repozytorium

```
libs/dsp          wers_dsp: Common, Filters, PhonemeClassifier, Oversampler, Crossover, LevelMatch
libs/ui           Theme, Controls, PresetBar, wspólne czcionki
libs/test         sygnały testowe i pomiary: FFT, harmoniczne, aliasy, null
plugins/wers      dzisiejszy WERS, przeniesiony bez zmian w działaniu
plugins/barwa     dsp, plugin, ui, testy
cmake             funkcja wers_add_plugin z jednakowymi ustawieniami każdej wtyczki
scripts           build_windows.bat buduje, testuje i instaluje wszystkie wtyczki
```

### Nowe części

- **Oversampler:** kaskada filtrów półpasmowych FIR dla 2x, 4x i 8x, z oknem Kaisera i tłumieniem około 100 dB. WERS zostaje przy swoim `Halfband2x`, bo jego 47 współczynników nie da takiego tłumienia. Podaje dokładne opóźnienie w całych próbkach. Nie alokuje pamięci w wątku audio. Przy wysokich częstotliwościach próbkowania ogranicza współczynnik tak, żeby nie przekraczać 384 kHz.
- **Crossover:** do 3 pasm, Linkwitz-Riley 24 dB/okt., z korekcją fazy między pasmami, żeby suma miała płaską charakterystykę. Działa na częstotliwości bazowej.
- **LevelMatch:** porównuje moc z wagą K przed i po obróbce w oknie około 400 ms i podaje wyrównanie w granicach ±12 dB. Zmienia je powoli i zamraża w ciszy, poniżej −60 dBFS.
- **PresetBar:** presety fabryczne z pliku wtyczki, presety użytkownika w `%APPDATA%\WERS\presets\<wtyczka>`, sloty A i B z kopiowaniem. Cofanie zmian zostaje po stronie DAW.
- **Zestaw pomiarów:** ten sam dla każdej wtyczki: null, aliasy, harmoniczne, niezależność od bloku, częstotliwości próbkowania, fuzz, brak alokacji.

### Zasady całej serii

- Identyfikatory parametrów i kody wtyczek nigdy się nie zmieniają.
- Stan projektu ma numer wersji. Nowy parametr ma wartość neutralną, więc stary projekt brzmi tak samo.
- Zmiana brzmienia oznacza nowy charakter. Stary zostaje na liście.
- Latencja jest zgłaszana poprawnie, a bypass trzyma tę samą latencję.
- Brak alokacji, blokad i I/O w wątku audio.

### Przeniesienie WERS-a

Kod wtyczki, nazwa pliku, `PLUGIN_CODE`, `PLUGIN_MANUFACTURER_CODE`, identyfikatory parametrów i format stanu zostają bez zmian. Przed przeniesieniem zapisuję stan z obecnej wersji jako plik wzorcowy. Warunki zakończenia:
- wszystkie obecne testy przechodzą,
- plik wzorcowy się wczytuje,
- wtyczka przechodzi pluginval na poziomie 10.

Zmiana nazwy na „WERS Słowo” to osobna decyzja, po sprawdzeniu, że stare projekty się otwierają. W v1 nazwa zostaje.

## Barwa: dźwięk

### Droga sygnału

```
wejście ──┬─ klasyfikator głosek (mono, 4 ms wyprzedzenia) ──────────┐
          └─ opóźnienie 4 ms ─ podział 1–3 pasma                     │ maska
                 każde pasmo:                                        ▼
                   góra 2x/4x/8x ─ emfaza ─ kształt ─ deemfaza ─ dół = wynik
                   różnica = wynik − pasmo ─ DC blok ─ Ton ─ Mix pasma × maska
                 suma = pasma + różnice
          ─ Mix ogólny ─ Auto poziom ─ Output ─ wyjście (albo Delta: sama różnica)
```

Maska, Mix i Ton działają na różnicy, a nie na całym sygnale. Dzięki temu przy zerowej różnicy dźwięk przechodzi nietknięty, a suchy sygnał nie zmienia barwy. Pasmo jest opóźnione o czas oversamplingu, żeby różnica była dokładna. Ton to filtr nachylenia: ujemny przyciemnia dodane harmoniczne, dodatni je rozjaśnia.

### Charaktery

Każdy kształt ma wzmocnienie 1 dla małych sygnałów, a emfaza i deemfaza są swoimi dokładnymi odwrotnościami. To modele zachowania, a nie kopie konkretnych urządzeń. Marketing nie używa nazw sprzętu.

| Charakter | Model | Pomiar charakteru (mono, 48 kHz, Mix 100%, bez maski, bez Auto poziomu) |
| --- | --- | --- |
| Lampa | Tangens hiperboliczny z małym przesunięciem pracy, które zależy od poziomu | Sinus 1 kHz, −18 dBFS, Drive 12 dB: 2. harmoniczna co najmniej 3 dB nad 3. |
| Taśma | Miękki kształt między emfazą a deemfazą wysokich tonów | Ten sam poziom i Drive, przy 96 kHz, bo przy 48 kHz 3. harmoniczna 8 kHz wypada poza pasmem: zniekształcenia przy 8 kHz co najmniej 6 dB wyższe niż przy 1 kHz |
| Transformator | Miękki kształt między emfazą a deemfazą niskich tonów | Ten sam poziom i Drive: zniekształcenia przy 100 Hz co najmniej 6 dB wyższe niż przy 1 kHz |
| Tranzystor | Symetryczny kształt z twardszym kolanem | Sinus 1 kHz, −18 dBFS, Drive 12 dB: 3. harmoniczna co najmniej 12 dB nad 2. |

### Maska głosek

`PhonemeClassifier` z WERS-a działa na sumie mono wejścia i co około 2 ms podaje przynależność do klas. Mnożnik różnicy to `m = (1 − Maska) + Maska × w`, gdzie `w = samogłoska + 0,35 × zwarta`. Syczące, oddechy i cisza mają wagę 0. `m` jest wygładzane próbka po próbce. Klasyfikator widzi dźwięk 4 ms wcześniej, więc maska zmienia się przed głoską, a nie po niej.

Wartość startowa wynika z nazwy śladu: 100%, a 0% przy nazwach `beat`, `bit`, `instrumental`, `instr`, `prod`, `master` i `mix`. Ta sama reguła dokładnych słów co w WERS-ie. Pierwszy ruch suwaka kończy zgadywanie.

### Parametry

| Parametr | Zakres | Domyślnie | Automatyzacja |
| --- | --- | --- | --- |
| Liczba pasm | 1, 2, 3 | 1 | Nie |
| Podział 1 | 60–1000 Hz | 200 Hz | Tak |
| Podział 2 | 1–10 kHz | 3 kHz | Tak |
| Charakter pasma (3 razy) | Lampa, Taśma, Transformator, Tranzystor | Lampa | Nie |
| Drive pasma (3 razy) | 0–36 dB | 0 dB | Tak |
| Mix pasma (3 razy) | 0–100% | 100% | Tak |
| Solo pasma (3 razy) | wł./wył. | wył. | Nie |
| Maska głosek | 0–100% | z nazwy śladu | Tak |
| Ton | −100–100% | 0% | Tak |
| Mix | 0–100% | 100% | Tak |
| Auto poziom | wł./wył. | wł. | Nie |
| Output | −24–24 dB | 0 dB | Tak |
| Delta | wł./wył. | wył. | Nie |
| Jakość | 2x, 4x, 8x | 4x | Nie |
| Bypass | wł./wył. | wył. | Tak |

Drive 0 dB oznacza brak zmian: różnica narasta płynnie w pierwszych 3 dB suwaka. Wszystkie suwaki są wygładzane, żeby automatyzacja nie trzeszczała. Stereo to dwa kanały z jedną maską. Mono też działa.

### Opóźnienie

Stałe dla danej jakości: 4 ms wyprzedzenia plus oversampling, razem około 5 ms. Maska wyłączona nie zmienia opóźnienia, więc można ją automatyzować. Zmiana jakości zmienia opóźnienie i może dać krótki trzask podczas odtwarzania, więc nie jest automatyzowana. Render używa tej samej jakości co odsłuch, więc plik brzmi tak jak miks.

## Barwa: ekran

Ten sam ciemny styl co WERS: te same kolory, czcionki i kontrolki. Okno skalowalne, rozmiar bazowy 1040 × 680.

```
┌ WERS Barwa ─────────────── < preset > ─ A/B ─ Delta ─ Bypass ┐
│ widmo: wejście, wyjście, dodane harmoniczne,                 │
│        uchwyty podziału pasm                                 │
│ pasek głosek: ostatnie 6 s, ile barwy dostaje każda głoska   │
├ Pasmo 1 ───── Pasmo 2 ───── Pasmo 3 ───── Całość ────────────┤
│ charakter     charakter     charakter     Maska, Ton, Mix    │
│ Drive, Mix    Drive, Mix    Drive, Mix    Output, Auto, Jakość│
└──────────────────────────────────────────────────────────────┘
```

Mięta oznacza to, co wtyczka dodaje, bursztyn ostrzega o przesterze na wyjściu. Pasek głosek jest wyróżnikiem tego ekranu: od razu widać, że syczące nie dostają barwy. Dane do widma i paska płyną z wątku audio przez kolejki bez blokad.

## Presety fabryczne

Presety nie zmieniają Jakości, Bypassu, Delty ani Solo. Około 12: Ciepły refren, Gęsty rap, Szept z bliska, Przód miksu, Rage, Dubel w tle, Klej na bit, 808 na telefon, Werbel z pazurem, Hi-haty bez ostrości, Delikatny klej na master, Czysty start. Nazwy mogą się zmienić. Wartości ustawiam wstępnie z pomiarów, a dostrajamy je po odsłuchach użytkownika.

## Testy

### Automatyczne

| Test | Warunek zaliczenia |
| --- | --- |
| Neutralność, 1 pasmo, Drive 0 dB | Wyjście równe wejściu z opóźnieniem, różnica poniżej −140 dB |
| Neutralność, 3 pasma, Drive 0 dB | Charakterystyka płaska ±0,05 dB od 20 Hz do 20 kHz |
| Małe sygnały, każdy charakter, Drive 12 dB, sinus −60 dBFS przy 100 Hz, 1 kHz i 8 kHz | Wzmocnienie 0 ±0,1 dB przy wyłączonym Auto poziomie |
| Aliasy, każdy charakter, sinus 5 kHz −6 dBFS, Drive 12 dB, 48 kHz | Najsilniejszy składnik poza harmonicznymi co najmniej 60 dB pod sygnałem przy 2x, 80 dB przy 4x, 90 dB przy 8x |
| Charaktery | Warunki z tabeli charakterów |
| Auto poziom, realistyczny głos, Drive 0–24 dB | Głośność ±1 dB względem wejścia, szczyty najwyżej 1 dB wyżej |
| Maska, syntetyczny głos, Maska 100% kontra 0% | Różnica w syczących niższa co najmniej o 12 dB, w samogłoskach najwyżej o 1 dB |
| Rozmiar bloku, od 32 do 4096 i losowo | Wynik identyczny co do próbki |
| Częstotliwość próbkowania | Testy neutralności i charakterów przechodzą przy 44,1, 48, 88,2, 96, 176,4 i 192 kHz |
| Fuzz | Ekstremalne ustawienia i NaN, Inf, DC na wejściu: wyjście skończone, powrót do normy w 1 s |
| Automatyzacja | Skok Drive, Mix i Maski na sinusie bez skoku próbki ponad próg trzasku |
| Wątek audio | Zero alokacji, czysto pod ASan, UBSan i TSan |
| Stan i presety | Zapis i odczyt identyczne, stary stan bez nowych pól się wczytuje, uszkodzony jest odrzucany |
| pluginval, Windows | Poziom 10 |
| Procesor, Windows, 48 kHz, stereo, 3 pasma, 4x | Najwyżej 5% jednego rdzenia na komputerze użytkownika |
| Regresja WERS | Wszystkie obecne testy i plik wzorcowy stanu |

Jeśli charakter nie spełnia celu aliasów, zmieniam jego kształt lub dodaję antyaliasing pochodnej (ADAA). Celu nie obniżam bez uzgodnienia z użytkownikiem.

### Na prawdziwych numerach

Program `barwa_measure` działa na komputerze użytkownika, na kilku numerach z jego folderu. Zapisuje wyłącznie liczby, bez dźwięku i tekstu. Dla każdego numeru liczy:
- spadek dodanej energii w głoskach syczących i zmianę w samogłoskach,
- różnicę głośności,
- szczyty.

Odcinki syczące wyznacza niezależnie od klasyfikatora, z udziału energii powyżej 4 kHz. Cele są takie same jak w teście syntetycznym. Jeśli klasyfikator zawiedzie na innym głosie, poprawiam go i opisuję to w wynikach.

### Odsłuchy użytkownika

Nie słyszę dźwięku, więc charakter oceniasz Ty. Nie testuję na Twoim ekranie: Ty sprawdzasz i opisujesz wrażenia, a ja odczytuję logi z pliku.

1. **Charaktery:** czy każdy ma sens na wokalu i bicie, gdzie brzmi źle.
2. **Maska:** czy syczące są czyste, czy maski nie słychać jako pompowania, czy spółgłoski nie tracą wyrazu.
3. **Presety:** czy nadają się jako punkt startu.

## Dostarczenie v1

Paczka: `WERS Barwa.vst3` zainstalowana w folderze VST3 użytkownika przez `build_windows.bat`, krótka instrukcja i tabela wyników testów w dokumentacji. WERS jest budowany tym samym skryptem.

## Ryzyka

| Ryzyko | Co z nim robię |
| --- | --- |
| Klasyfikator był strojony na jednym głosie | Pomiar na kilku numerach przed oddaniem. Maskę zawsze można wyłączyć |
| Przeniesienie WERS-a coś psuje | Plik wzorcowy stanu i wszystkie testy są warunkiem zakończenia. Osobne commity, które można cofnąć |
| Nie słyszę efektu | Pomiary pilnują techniki, a brzmienie zatwierdza użytkownik w 3 rundach |
| Modele zachowania mogą ustępować płatnym emulacjom | Mówimy otwarcie, że to nie emulacje. Wyróżnikiem jest maska głosek |
| Licencja JUCE może się zmienić | Przed publicznym wydaniem czytam aktualny tekst licencji |
