# WERS

Wtyczka VST3 do miksu wokalu. Ocenia czytelność każdego słowa na pięciu urządzeniach, pokazuje słowa, które giną pod bitem, i naprawia je jednym kliknięciem: podbija spółgłoski w wokalu i wycina miejsce w środku bitu tylko na czas tego słowa.

Jeden plik `WERS.vst3`, trzy role wybierane w oknie wtyczki: Wokal, Bit i Master.

## Instalacja na Windows

1. Kliknij dwa razy `zainstaluj_wers.bat`. Windows zapyta o zgodę, bo folder `C:\Program Files\Common Files\VST3` wymaga uprawnień administratora.
2. W DAW uruchom ponowne skanowanie wtyczek.

Możesz też skopiować cały folder `WERS.vst3` tam, gdzie Twój DAW szuka wtyczek. Folder musi zostać w całości, bo w środku jest też program `wers_whisper.exe`, który rozpoznaje tekst.

Tekst piosenki rozpoznaje model mowy (około 550 MB). Wtyczka pyta o jego pobranie przy pierwszym uruchomieniu i zapisuje go w `%APPDATA%\WERS\models`. Bez modelu wszystko działa dalej, tylko zamiast słów widać fragmenty czasu.

## Jak tego używać w REAPERze

1. Dodaj WERS na ślad wokalu lub folder wokalu, najlepiej na końcu łańcucha, zaraz po kompresji i saturacji. Rola Wokal.
2. Dodaj WERS na ślad bitu. Rola Bit. Ślady o nazwach w rodzaju `beat` i `main` dostają właściwą rolę same.
3. W oknie na wokalu kliknij **Przesłuchaj numer** i puść numer od początku do końca. Po zatrzymaniu analiza rusza sama.
4. Wybierz urządzenie u góry. Żółte słowa słabną, różowe giną. Kliknij słowo, żeby zobaczyć przyczynę i korektę.
5. Kliknij **Napraw słowo** albo **Napraw wszystkie**. **Słuchaj bez napraw** porównuje przed i po.
6. Opcjonalnie dodaj WERS na master, rola Master: LUFS, true peak, symulacja urządzeń i normalizacji Spotify. W renderze symulacja wyłącza się sama.

Jeśli masz gotowy tekst, wklej go w **Tekst numeru**. Wtyczka poprawi nim pisownię rozpoznanych słów.

Moduły w dolnym pasku:

- **Fonem**: osobna obróbka samogłosek, głosek syczących, zwartych i oddechów. Przy zerowych ustawieniach dźwięk przechodzi bez żadnej zmiany.
- **Miejsce**: ile bit ustępuje słowom. Działa tylko w środku stereo, powyżej kicka i subu.
- **Przestrzeń**: delay i pogłos, które chowają się pod słowami i wracają w przerwach.
- **Charakter**: pad z punktami Gloss, Rage, Szept i Drill.
- **Dopasuj do bitu**: jedno kliknięcie ustawia suwaki na podstawie widma i głośności bitu oraz wokalu.

## Co warto wiedzieć

- Ocena słów to szacunek oparty na modelu czytelności mowy ANSI S3.5, a nie pomiar na ludziach. Ufaj uszom, jeśli się z nią nie zgadzają.
- Wtyczka stoi przed suwakiem śladu, więc sama go nie słyszy. W REAPERze odczytuje suwak śladu i folderów nad nim, a oceny zmieniają się od razu, gdy nim poruszysz. W innych DAW-ach ocena zakłada suwaki wokalu i bitu w tej samej pozycji, więc po dużej różnicy między nimi będzie mniej dokładna.
- Po zmianie wokalu wtyczka sama wyłącza naprawy i prosi o ponowne przesłuchanie.
- Całe rozpoznawanie działa na Twoim komputerze. Nagrania nigdzie nie wychodzą.

## Budowanie ze źródeł

Potrzebne: Visual Studio 2022 z C++, CMake 3.22 lub nowszy, Git.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target WERS_VST3
```

Gotowa wtyczka powstaje w `build\src\plugin\WERS_artefacts\Release\VST3\WERS.vst3`. Skrypt `scripts\build_windows.bat` robi to samo i uruchamia też testy oraz pluginval.

## Licencje

Kod używa JUCE 8 (AGPLv3 albo licencja komercyjna, więc przed ewentualną sprzedażą wtyczki trzeba sprawdzić warunki JUCE), whisper.cpp (MIT), doctest (MIT). Model mowy to Whisper large-v3-turbo od OpenAI (MIT). Czcionki Bricolage Grotesque i Instrument Sans są na licencji OFL.
