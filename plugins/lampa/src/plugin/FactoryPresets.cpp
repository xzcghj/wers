#include "FactoryPresets.h"
#include "Params.h"

namespace lampa {

// Values are in the units the controls show. Mode 0 is Opto, 1 is Lampa; limit 0 or 1.
// Anything a preset leaves out takes its default: Ściskanie 30, +3 dB, Kolor 25%, Mix 100%, Opto,
// no limit, link 100%, detector filter off, output 0 dB.
std::vector<wers::ui::FactoryPreset> factoryPresets() {
    using namespace ids;
    constexpr const char* start = "Start";
    constexpr const char* wokal = "Wokal";
    constexpr const char* bas = "Bas";
    constexpr const char* gitara = "Gitara";
    constexpr const char* beben = "Bęben";
    constexpr const char* szyna = "Szyna";
    constexpr const char* master = "Master";
    constexpr const char* kolorG = "Kolor";
    return {
        { "Domyślny", {}, start, "Spokojne wyrównanie w trybie Opto. Kręć Ściskaniem, aż igła zacznie tańczyć." },

        { "Wokal na przód", { { squeeze, 46.0f }, { gain, 8.0f }, { color, 30.0f }, { sidechain, 80.0f } },
          wokal, "Klasyczne wyrównanie głosu: równy, blisko ucha, z ciepłym środkiem." },
        { "Wokal jedwab", { { squeeze, 30.0f }, { gain, 3.0f }, { color, 15.0f }, { sidechain, 100.0f } },
          wokal, "Ledwo słychać, że pracuje. Głos po prostu siedzi w miksie." },
        { "Rap gęsto", { { squeeze, 41.0f }, { gain, 6.0f }, { color, 40.0f }, { limit, 1.0f }, { sidechain, 120.0f } },
          wokal, "Mocno ściśnięty, gęsty głos, który nie ucieka z bitu." },
        { "Wokal radiowy", { { squeeze, 43.0f }, { gain, 6.0f }, { color, 60.0f }, { mode, 1.0f }, { limit, 1.0f }, { sidechain, 150.0f } },
          wokal, "Zbity i gęsty jak w radiu, z wyraźnym lampowym ziarnem." },
        { "Chórki w tle", { { squeeze, 40.0f }, { gain, 5.0f }, { color, 35.0f }, { mode, 1.0f }, { sidechain, 150.0f } },
          wokal, "Chórki sklejone w jedną miękką ścianę, która nie wychodzi przed solistę." },

        { "Bas równy", { { squeeze, 36.0f }, { gain, 5.0f }, { color, 30.0f } },
          bas, "Każda nuta tak samo głośna. Klasyka na bas z linii." },
        { "Bas gruby", { { squeeze, 30.0f }, { gain, 3.0f }, { color, 65.0f }, { mode, 1.0f } },
          bas, "Lampa dokłada harmonicznych, więc bas słychać nawet na małych głośnikach." },
        { "808 w ryzach", { { squeeze, 36.0f }, { gain, 5.0f }, { color, 45.0f }, { limit, 1.0f } },
          bas, "Długie 808 trzymane krótko: wybrzmienie równe, atak nie skacze." },

        { "Gitara akustyczna", { { squeeze, 36.0f }, { gain, 2.5f }, { color, 20.0f }, { sidechain, 120.0f } },
          gitara, "Wyrównuje bicie i palcowanie, dół pudła nie pompuje." },
        { "Przester w ryzach", { { squeeze, 30.0f }, { gain, 2.0f }, { color, 25.0f }, { mode, 1.0f } },
          gitara, "Przesterowana gitara trochę zbita i wygładzona, bez utraty ataku." },
        { "Czysta z sustainem", { { squeeze, 50.0f }, { gain, 8.0f }, { color, 30.0f }, { limit, 1.0f } },
          gitara, "Czysty dźwięk gra dłużej: ogon akordu wyciągnięty do góry." },

        { "Stopa i werbel", { { squeeze, 40.0f }, { gain, 4.0f }, { color, 40.0f }, { mode, 1.0f }, { mix, 75.0f }, { sidechain, 60.0f } },
          beben, "Szybka lampa łapie uderzenie i dodaje mu ciała." },
        { "Overheady razem", { { squeeze, 34.0f }, { gain, 3.0f }, { color, 30.0f }, { mode, 1.0f }, { sidechain, 150.0f } },
          beben, "Blachy i bębny w jednym spójnym obrazie, stopa nie szarpie." },
        { "Room zgnieciony", { { squeeze, 45.0f }, { gain, 8.5f }, { color, 60.0f }, { limit, 1.0f }, { mix, 40.0f } },
          beben, "Równoległe zgniecenie: pokój oddycha między uderzeniami." },
        { "Bębny równolegle", { { squeeze, 42.0f }, { gain, 7.5f }, { color, 50.0f }, { mode, 1.0f }, { limit, 1.0f }, { mix, 45.0f } },
          beben, "Zgnieciona kopia pod oryginałem: bębny grubsze, transjenty zostają." },

        { "Klej na szynę", { { squeeze, 30.0f }, { gain, 2.0f }, { color, 25.0f }, { mode, 1.0f }, { sidechain, 90.0f } },
          szyna, "Pół do dwóch dB redukcji i wszystko gra razem." },
        { "Szyna bębnów", { { squeeze, 36.0f }, { gain, 3.0f }, { color, 35.0f }, { mode, 1.0f }, { sidechain, 60.0f } },
          szyna, "Cały zestaw sklejony i trochę mocniejszy." },
        { "Szyna wokali", { { squeeze, 32.0f }, { gain, 2.5f }, { color, 20.0f }, { sidechain, 120.0f } },
          szyna, "Główny wokal i chórki wyrównane jedną optyczną komórką." },

        { "Master klej", { { squeeze, 24.0f }, { gain, 1.0f }, { color, 15.0f }, { mode, 1.0f }, { sidechain, 100.0f } },
          master, "Delikatnie: decybel redukcji, ciepło i spójność na sumie." },
        { "Master ciepło", { { squeeze, 14.0f }, { gain, 0.5f }, { color, 40.0f }, { mode, 1.0f }, { sidechain, 100.0f } },
          master, "Głównie kolor lampy i transformatorów, kompresji prawie nie ma." },
        { "Master głośno", { { squeeze, 32.0f }, { gain, 3.0f }, { color, 30.0f }, { mode, 1.0f }, { limit, 1.0f }, { sidechain, 120.0f } },
          master, "Więcej gęstości i głośności, lampa łagodnie łapie szczyty." },

        { "Tylko lampa", { { squeeze, 0.0f }, { gain, 0.0f }, { color, 50.0f } },
          kolorG, "Bez kompresji, sam kolor: parzyste harmoniczne i miękka góra." },
        { "Ciepła taśma", { { squeeze, 10.0f }, { gain, 0.0f }, { color, 70.0f }, { mode, 1.0f } },
          kolorG, "Nieparzyste harmoniczne i lekko zaokrąglone szczyty, jak z rozgrzanego toru." },
        { "Przegrzany", { { squeeze, 36.0f }, { gain, 3.0f }, { color, 100.0f }, { mode, 1.0f } },
          kolorG, "Lampa na granicy: gęsto, brudno i głośno. Na efekt, nie na sumę." },
    };
}

juce::StringArray presetExcludedIds() { return { ids::bypass }; }

} // namespace lampa
