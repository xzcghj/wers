# WERS: specyfikacja wersji 1 (VST3)

Data: 2026-11-04. Autor decyzji: Claude. Użytkownik napisał „nie pytaj, rób jak uważasz", więc wszystkie
bramki akceptacji uznaję za spełnione, a każdą decyzję zapisuję tutaj.

## Co użytkownik chce osiągnąć

- Wtyczka **VST3 na Windows x64** do miksowania wokalu w DAW. Główny DAW użytkownika to REAPER, ma też FL Studio.
- Sukces: wtyczka ładuje się w DAW bez problemów, realnie poprawia czytelność wokalu, działa na jego sesjach:
  grupa `main` z łańcuchem Auto-Tune, Pro-Q, 1176, CL 1B itd., bit na osobnym śladzie.
- Założenia moje: wersja 1 jest po polsku; własny użytek użytkownika, więc licencja JUCE AGPL/Starter wystarcza
  (przy sprzedaży trzeba to sprawdzić ponownie).

## Co pokazało badanie rozwiązań

| Pytanie | Wybór | Dlaczego |
| --- | --- | --- |
| Framework | JUCE 8 (C++20) | Najlepsza zgodność z DAW, sidechain, pluginval. Na komputerze użytkownika jest MSVC 14.44, CMake, Git. |
| Budowa Windows | MSVC na komputerze użytkownika | W chmurze nie ma kompilatora na Windows (apt/pip/npm zablokowane). Git działa, więc kod zależności pobieram z GitHuba. |
| Ocena słów | Własna implementacja ANSI S3.5 (SII, tabele 1/3 oktawy) liczona dla każdego słowa, plus opcjonalny test Whisperem na miksie | W wyzwaniu CLIP1 baseline STOI miał korelację 0,21, baseline Whisper 0,58, najlepsze systemy 0,67. STOI odpada. SII jest deterministyczne i tłumaczy przyczynę, więc nadaje się do planowania naprawy. Whisper na miksie to najlepszy tani sygnał „czy słowo przeżyło". |
| Tekst i czas słów | whisper.cpp v1.9.5 lokalnie, wklejony tekst jako podpowiedź i korekta pisowni | MIT, bez zależności, działa offline, zna polski. Czas słów jest niedokładny, więc dociągam granice do obwiedni energii. |
| Dostęp do śladu | Przechwycenie przy odtwarzaniu | ARA2 to duża zależność, a użytkownik i tak puszcza numer. |
| Połączenie instancji | Wspólny obiekt w procesie, dane adresowane czasem osi, nie zegarem | REAPER liczy ślady z wyprzedzeniem (anticipative FX), więc współdzielenie „na żywo" po zegarze byłoby rozjechane. |

## Role jednej wtyczki

Jeden plik `WERS.vst3`, trzy role wybierane w oknie. Rola startowa z nazwy śladu, jeśli host ją podaje.

- **Wokal**: Fonem, Przestrzeń, przechwycenie wokalu, analiza, Napraw Słowo (część wokalna).
- **Bit**: Miejsce, przechwycenie bitu, Napraw Słowo (część w bicie). Zero opóźnienia.
- **Master**: Odsłuch Prawdy, LUFS, true peak, symulacja normalizacji.

## Moduły

**Fonem** (Wokal). Klasyfikator głosek działający na żywo daje miękkie przynależności: samogłoska, sycząca, zwarta,
oddech, cisza. Sterują one: gęstością samogłosek (kompresja + saturacja tylko na samogłoskach), de-esserem
śledzącym środek głoski syczącej (osobno sz, ś, s), atakiem zwartych, ściszaniem oddechów, usuwaniem dmuchnięć.
Saturacja jest dodawana jako różnica, więc przy zerowych ustawieniach wyjście równa się wejściu z opóźnieniem.

**Miejsce** (Bit). 10 filtrów dzwonowych na samym środku M/S, 250 Hz do 10 kHz. Sub, kick poniżej 200 Hz i boki stereo
zostają. Sterowanie: mapa pasm wokalu z analizy, na żywo pierścień adresowany czasem osi albo sidechain. Dokładność
zapewnia mapa z analizy, bo daje też wyprzedzenie bez dodatkowego opóźnienia.

**Przestrzeń** (Wokal). Delay zsynchronizowany z tempem i pogłos FDN. Mokry sygnał jest przyciszany pod słowami i wraca
w przerwach.

**Mapa Słów i Napraw Słowo** (Wokal + Bit). Użytkownik klika „Przesłuchaj numer", puszcza utwór, wtyczka zbiera wokal
i bit w 16 kHz na osi czasu. Potem w tle: tekst, granice, klasy głosek, ocena na 5 urządzeń. Kliknięcie słowa pokazuje
przyczynę i korektę. Planer sprawdza krok po kroku podbicia wokalu (tylko na spółgłoskach) i cięcia bitu, licząc
przewidywaną ocenę tym samym modelem. Korekty działają według osi czasu projektu.

**Odsłuch Prawdy** (Master). Symulacja urządzeń tymi samymi krzywymi co ocena, normalizacja według zasad Spotify
(-14, -11, -19 LUFS), BS.1770, true peak. W renderze offline symulacja wyłącza się sama, żeby nie trafić do eksportu.

**Charakter**. Pad XY: intymny–agresywny, suchy–szeroki. Skrajne punkty to Szept, Gloss, Drill, Rage.

**Dopasuj do bitu**. Jednorazowo ustawia parametry na podstawie widma i głośności bitu oraz wokalu.

## Ocena słowa

18 pasm 1/3 oktawy, ramka 32 ms, krok 10 ms, 16 kHz. Poziomy kalibrowane przez normalizację do -14 LUFS i założony
poziom odsłuchu urządzenia. Algorytm: ANSI S3.5 (samomaskowanie, rozlewanie maskowania, próg wewnętrzny, zniekształcenie
poziomu). Maskerem jest ramka bitu (ESII) plus hałas otoczenia urządzenia. Widmo mowy to średnia dla samogłosek i osobno
dla spółgłosek w słowie, a wynik łączy je z wagą 0,4 / 0,6. Mapowanie SII na 0–100: `(1-10^(-S/0.4))^3.334`
(funkcja przejścia dla słów). Progi: poniżej 50 „ginie", poniżej 65 „słabnie". To szacunek, nie pomiar na ludziach.

## Bezpieczeństwo pracy w DAW

- Brak alokacji, blokad i I/O w wątku audio (sprawdzane testem z licznikiem alokacji).
- Stałe opóźnienie w danej roli, poprawnie zgłaszane; bypass trzyma to samo opóźnienie.
- Wynik niezależny od rozmiaru bloku (test).
- Zmiana wokalu po analizie: odcisk obwiedni wykrywa niezgodność i korekty same się wyłączają.
- Analiza na osobnym wątku o niskim priorytecie, z możliwością przerwania. Whisper nie uruchamia się bez AVX2.
- Brak modelu lub brak bitu nie blokuje wtyczki: są komunikaty i tryb zastępczy (fragmenty zamiast słów).

## Interfejs

Ciemny interfejs (zmiana na prośbę użytkownika, 2026-11-05), tekst numeru jako główny ekran. Kolor zawsze znaczy to samo:
bursztyn słabnie, koral ginie, mięta jest czytelna lub naprawiona i oznacza główne działanie. Nad tekstem pasek całego numeru.
Czcionki Bricolage Grotesque i Instrument Sans (OFL) wbudowane. Okno skalowalne ze stałymi proporcjami. Rola Bit i Master
mają własne, prostsze widoki.

## Testy

- Testy jednostkowe rdzenia (bez JUCE) z ASan/UBSan i TSan.
- Test zerowy, niezależność od bloku, częstotliwości 44,1–192 kHz, NaN/Inf, fuzz parametrów.
- End-to-end na prawdziwych śladach użytkownika: przewidywana ocena po naprawie kontra ocena po wyrenderowaniu dźwięku.
- pluginval (poziom 10) na Linuksie i Windows, ładowanie w REAPERze na komputerze użytkownika.

## Decyzje podjęte w trakcie budowy

Testy na prawdziwych śladach zmieniły kilka założeń z tej specyfikacji. Obowiązuje to, co niżej.

- **Rozpoznawanie w osobnym procesie.** `wers_whisper.exe` leży w paczce `WERS.vst3` i pracuje na niższym priorytecie.
  Wtyczka nie zawiera ggml, więc awaria modelu nie zabiera ze sobą DAW-a. Efektem awarii jest tylko brak tekstu.
- **Model large-v3-turbo-q5_0** (574 MB) zamiast small. Na polskim rapie słyszy wyraźnie więcej słów.
- **Czas słów.** Czasy DTW z whisper.cpp wymagają wyłączenia flash attention. Potem słowa dociągam do sylab:
  liczba sylab wynika z polskiej pisowni, a programowanie dynamiczne dopasowuje słowa do szczytów w paśmie samogłosek.
  Słaby garb 20 dB pod sąsiednim głosem nie liczy się jako sylaba.
- **Drugie przesłuchanie.** Ten sam model słucha też wokalu z bitem i zaznacza słowa, których już nie słyszy. To tylko
  dodatkowa uwaga w panelu słowa. Krok jest pomijany na komputerach z mniej niż 8 wątkami.
- **Suwaki.** Wtyczka stoi przed suwakiem śladu, a realne projekty mają suwaki od -5 do -10 dB. W REAPERze odczytuję
  `D_VOL` śladu i folderów nad nim przez `IReaperHostApplication`, a oceny przeliczam od razu po ruchu suwaka.
- **Grupy połączeń osobno dla każdego projektu**, żeby zakładki REAPERa się nie mieszały.
- **Przełączanie urządzenia w roli Master** bez trzasku: wyciszenie i podgłośnienie po 6 ms.

## Poza zakresem wersji 1

ARA2, AU/AAX, macOS, symulacja kodeka, eksport automatyki do DAW, klonowanie głosu, chmura.
Walidacja oceny na słuchaczach wymaga ludzi, więc nie da się jej wykonać w tej sesji.
