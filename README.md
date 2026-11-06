# WERS

Seria darmowych wtyczek VST3 na Windows. W repozytorium są trzy: **WERS** do miksu wokalu, **WERS Barwa**, saturator pomyślany przede wszystkim pod wokal, i **WERS Refren**, który z jednego głosu robi dubel, chór, ekipę, harmonie i efekty z gotowych presetów.

## WERS

Wtyczka VST3 do miksu wokalu. Ocenia czytelność każdego słowa na pięciu urządzeniach, pokazuje słowa, które giną pod bitem, i naprawia je jednym kliknięciem: podbija spółgłoski w wokalu i wycina miejsce w środku bitu tylko na czas tego słowa.

Jeden plik `WERS.vst3`, trzy role wybierane w oknie wtyczki: Wokal, Bit i Master.

### Instalacja na Windows

1. Kliknij dwa razy `zainstaluj_wers.bat`. Instaluje obie wtyczki. Windows zapyta o zgodę, bo folder `C:\Program Files\Common Files\VST3` wymaga uprawnień administratora.
2. W DAW uruchom ponowne skanowanie wtyczek.

Możesz też skopiować cały folder `WERS.vst3` tam, gdzie Twój DAW szuka wtyczek. Folder musi zostać w całości, bo w środku jest też program `wers_whisper.exe`, który rozpoznaje tekst.

Tekst piosenki rozpoznaje model mowy (około 550 MB). Wtyczka pyta o jego pobranie przy pierwszym uruchomieniu i zapisuje go w `%APPDATA%\WERS\models`. Bez modelu wszystko działa dalej, tylko zamiast słów widać fragmenty czasu.

### Jak tego używać w REAPERze

1. Dodaj WERS na ślad wokalu lub folder wokalu, najlepiej na końcu łańcucha, zaraz po kompresji i saturacji. Rola Wokal.
2. Dodaj WERS na ślad bitu. Rola Bit. Ślady o nazwach w rodzaju `beat` i `main` dostają właściwą rolę same.
3. W oknie na wokalu kliknij **Przesłuchaj numer** i puść numer od początku do końca. Po zatrzymaniu analiza rusza sama.
4. Wybierz urządzenie u góry. Bursztynowe słowa słabną, koralowe giną. Pasek nad tekstem pokazuje cały numer: im wyższy słupek, tym gorzej słychać słowo. Kliknij słowo albo słupek, żeby zobaczyć przyczynę, korektę i ocenę na każdym urządzeniu. Dwuklik na słowie przewija REAPERa w to miejsce.
5. Kliknij **Napraw słowo** albo **Napraw wszystkie**. **Słuchaj bez napraw** porównuje przed i po, a **Cofnij wszystkie naprawy** wraca do początku.
6. Opcjonalnie dodaj WERS na master, rola Master: LUFS, true peak, symulacja urządzeń i normalizacji Spotify. W renderze symulacja wyłącza się sama.

Jeśli masz gotowy tekst, wklej go w **Tekst numeru**. Wtyczka poprawi nim pisownię rozpoznanych słów.

Moduły w dolnym pasku:

- **Fonem**: osobna obróbka samogłosek, głosek syczących, zwartych i oddechów. Przy zerowych ustawieniach dźwięk przechodzi bez żadnej zmiany.
- **Miejsce**: ile bit ustępuje słowom. Działa tylko w środku stereo, powyżej kicka i subu.
- **Przestrzeń**: delay i pogłos, które chowają się pod słowami i wracają w przerwach.
- **Charakter**: pad z punktami Gloss, Rage, Szept i Drill.
- **Dopasuj do bitu**: jedno kliknięcie ustawia suwaki na podstawie widma i głośności bitu oraz wokalu.

### Co warto wiedzieć

- Ocena słów to szacunek oparty na modelu czytelności mowy ANSI S3.5, a nie pomiar na ludziach. Ufaj uszom, jeśli się z nią nie zgadzają.
- Wtyczka stoi przed suwakiem śladu, więc sama go nie słyszy. W REAPERze odczytuje suwak śladu i folderów nad nim, a oceny zmieniają się od razu, gdy nim poruszysz. W innych DAW-ach ocena zakłada suwaki wokalu i bitu w tej samej pozycji, więc po dużej różnicy między nimi będzie mniej dokładna.
- Po zmianie wokalu wtyczka sama wyłącza naprawy i prosi o ponowne przesłuchanie.
- Całe rozpoznawanie działa na Twoim komputerze. Nagrania nigdzie nie wychodzą.

## WERS Barwa

Saturator, który dodaje gęstość i barwę samogłoskom, a głoski syczące zostawia czyste. Po wyłączeniu maski działa też na bicie i basie. Plik: `WERS Barwa.vst3`.

### Jak tego używać

1. Wstaw Barwę na ślad wokalu. Maska głosek startuje od 100%. Na śladach nazwanych na przykład `beat`, `bit`, `instrumental`, `instr`, `prod`, `master` albo `mix` sama ustawia się na 0%.
2. Podnoś **Drive** w pierwszym paśmie i wybierz charakter. Do 0 dB dźwięk przechodzi bez żadnej zmiany, a różnica narasta płynnie w pierwszych 3 dB suwaka.
3. **Auto poziom** trzyma głośność taką jak na wejściu, więc porównujesz brzmienie, a nie poziom. Działa tam, gdzie trafia barwa, więc to, co maska zostawia czyste, zostaje na swoim poziomie.
4. **Delta** pozwala posłuchać samego tego, co wtyczka dodaje.
5. Jeśli jedno ustawienie nie pasuje do całego dźwięku, wybierz 2 albo 3 **pasma** i przesuń podziały myszą na widmie. Każde pasmo ma własny charakter, Drive i Mix.

### Charaktery

To modele zachowania, a nie kopie konkretnych urządzeń.

- **Lampa:** miękkie nasycenie z drugą harmoniczną, która zależy od poziomu.
- **Taśma:** wysokie tony nasycają się pierwsze.
- **Transformator:** niskie tony nasycają się pierwsze.
- **Tranzystor:** twardsze kolano i nieparzyste harmoniczne.

### Reszta ekranu

- **Widmo:** cienka linia to wejście, jaśniejsza to wyjście, a mięta to miejsca, gdzie wyjście stoi ponad wejściem. Bursztynowy napis ostrzega o przesterze na wyjściu.
- **Barwa na głoskach:** ostatnie 6 sekund. Wysoko są samogłoski, nisko syczące i przerwy.
- **Maska głosek, Ton i Mix** działają tylko na tym, co wtyczka dodaje. Suchy dźwięk zostaje nietknięty.
- **Jakość:** 2x, 4x albo 8x. Więcej to czystsze wysokie tony i większe obciążenie procesora. Zmiana podczas odtwarzania może dać krótki trzask, więc nie da się jej automatyzować. Render używa tej samej jakości co odsłuch.
- **Presety:** fabryczne i własne. Własne leżą w `%APPDATA%\WERS\presets\barwa` jako zwykłe pliki XML, więc można je kopiować. Presety nie zmieniają Jakości, Bypassu, Delty ani Solo.
- **A i B:** dwa zestawy ustawień do porównania, zapisywane w projekcie.

Opóźnienie to około 5 ms i wtyczka zgłasza je do DAW-a. Bypass trzyma to samo opóźnienie.

## WERS Refren

Robi z jednego głosu wiele: dubel, refren, ekipę, chór, głos głęboki albo wysoki, harmonie, szept i efekty. Działa w czasie rzeczywistym i ma 124 gotowe presety w 12 grupach. Syczące zostają w środku, więc nawet szeroki chór nie zamazuje słów.

### Jak tego używać

1. Wstaw wtyczkę bezpośrednio na ślad wokalu.
2. Wybierz grupę po lewej i kliknij kartę presetu. Opis pojawi się po najechaniu na kartę.
3. Dużym suwakiem **Ile** ustaw, ile efektu słychać. Przy zerze zostaje sam głos.
4. Jeśli chcesz coś zmienić, zacznij od rzędu suwaków na dole. **Szerokość** i **Czas** działają na wszystkie warstwy naraz.

Preset nie dotyka **Ile**, **Output**, **Delta** ani **Bypass**, więc przełączanie presetów nie zmienia tego, co już ustawiłeś. Każdy preset jest tak dobrany, żeby przy Ile 100% grał równie głośno jak głos na wejściu.

### Grupy presetów

| Grupa | Co robi |
| --- | --- |
| Dubel | Od ciasnego do szerokiego, mało głosów, bez pogłosu |
| Refren | Szerokie głosy na hook, często z lekkim pogłosem |
| Ekipa | Różne barwy i duże przesunięcia, jak kilka osób |
| Chór | Wiele głosów, duży pogłos, ciszej suchy głos |
| Głęboki | Oktawa w dół, ciemna barwa, sub |
| Wysoki | Oktawa w górę, jasna barwa, głos jak z hyperpopu |
| Harmonie | Kwinty, kwarty i oktawy |
| Ad-liby | Efekty na pojedyncze słowa: telefon, echo, zza ściany |
| Szept | Ten sam tekst szeptem |
| Przester | Ziarno na warstwach, od ciepłej lampy po megafon |
| Efekty | Robot, shimmer, flanger, autopan, kosmos |
| Instrumenty | Te same narzędzia bez maski, do gitary, pada czy bębnów |

Grupy **Ulubione** i **Moje** są twoje. Gwiazdka na karcie dodaje preset do Ulubionych. **Zapisz** tworzy własny preset, który trafia do `%APPDATA%\WERS\presets\refren` jako zwykły plik XML. Gwiazdki leżą obok, w `ulubione.txt`.

### Reszta ekranu

- **Scena:** każdy głos to kropka. W poziomie widać, gdzie stoi, w pionie jego interwał, a jasność mówi, jak głośno gra. Na syczących kropki przygasają, bo maska chowa warstwy. Duża kropka w środku to suchy głos, a bursztynowa oznacza szczyt ponad 0 dBFS.
- **Szczegóły:** zamiast kart pokazują cztery warstwy i całą resztę. Suwak, który przy wybranym źródle nic nie robi, jest przygaszony.
- **Losuj:** rusza szerokość, czas, rozstrojenie, ton, pogłos i echo obecnego brzmienia. Interwały i barwy zostają, więc nic nie wypada ze stroju. Gwiazdka przy nazwie oznacza, że brzmienie odeszło od presetu.
- **Ruch:** głębokość ruchu. Większość presetów nie ma żadnego, więc ruszenie suwaka włącza Autopan. Rodzaj i tempo zmienisz w Szczegółach.
- **Delta:** słychać tylko to, co dodaje wtyczka.
- **Spółgłoski:** ile warstw schodzi z drogi na syczących. W grupie Instrumenty jest na zerze, bo tam nie ma czego chronić.

### Co warto wiedzieć

- Opóźnienie to 30 ms. Host wyrównuje je sam.
- Echo idzie za tempem projektu. Bez tempa przyjmuje 120 BPM.
- Warstwy Głos i Robot potrzebują wysokości głosu, więc na szepcie, oddechu i syczących milkną. Do tego są warstwy Szept i maska.

## Budowanie ze źródeł

Potrzebne: Visual Studio 2022 z C++, CMake 3.22 lub nowszy, Git.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target WERS_VST3 WersBarwa_VST3 WersRefren_VST3
```

Gotowe wtyczki powstają w `build\plugins\wers\plugin\WERS_artefacts\Release\VST3\WERS.vst3`, `build\plugins\barwa\WersBarwa_artefacts\Release\VST3\WERS Barwa.vst3` i `build\plugins\refren\WersRefren_artefacts\Release\VST3\WERS Refren.vst3`. Skrypt `scripts\build_windows.bat` robi to samo i uruchamia też testy oraz pluginval.

Układ repozytorium: `libs/dsp`, `libs/ui` i `libs/test` to wspólna baza serii, a `plugins/wers`, `plugins/barwa` i `plugins/refren` to same wtyczki. Każda kolejna wtyczka powstaje funkcją `wers_add_plugin` z `cmake/WersPlugin.cmake`.

## Licencje

Kod używa JUCE 8 (AGPLv3 albo licencja komercyjna, więc przed ewentualną sprzedażą wtyczki trzeba sprawdzić warunki JUCE), whisper.cpp (MIT), doctest (MIT). Model mowy to Whisper large-v3-turbo od OpenAI (MIT). Czcionki Bricolage Grotesque i Instrument Sans są na licencji OFL.
