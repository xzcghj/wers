#include "FactoryPresets.h"
#include "Params.h"

namespace powiew {

// Values are in the units the controls show. Character: 0 Jedwab, 1 Blask, 2 Analog.
// Anything a preset leaves out takes its default: both knobs at 0%, 6 and 12 kHz, smoothness 50%, Jedwab, mix 100%.
std::vector<wers::ui::FactoryPreset> factoryPresets() {
    using namespace ids;
    constexpr const char* start = "Start";
    constexpr const char* wokal = "Wokal";
    constexpr const char* akustyk = "Akustyk";
    constexpr const char* bebny = "Bębny";
    constexpr const char* miks = "Miks";
    constexpr const char* master = "Master";
    constexpr const char* podcast = "Podcast";
    return {
        { "Neutralnie", {}, start, "Oba pokrętła na zero: dźwięk przechodzi dokładnie taki, jaki wszedł." },
        { "Pierwszy powiew", { { presence, 30.0f }, { air, 40.0f } }, start, "Dobry punkt wyjścia do wszystkiego: trochę obecności, trochę powietrza, łagodnie." },

        { "Wokal: jedwabny połysk", { { presence, 35.0f }, { air, 60.0f }, { smooth, 55.0f }, { airHz, 13000.0f } },
          wokal, "Głos robi się otwarty i drogi, a sybilanty zostają miękkie. Na prawie każdy wokal." },
        { "Wokal pop z przodu", { { presence, 60.0f }, { air, 65.0f }, { character, 1.0f }, { smooth, 65.0f }, { presenceHz, 5000.0f } },
          wokal, "Blask i dykcja: wokal wychodzi przed miks jak w radiu, bez syczenia." },
        { "Szept i oddech", { { presence, 15.0f }, { air, 55.0f }, { smooth, 75.0f }, { airHz, 15000.0f } },
          wokal, "Intymny, bliski głos z oddechem na górze. Mocne hamowanie pilnuje sybilantów." },
        { "Chórki w powietrzu", { { presence, 20.0f }, { air, 75.0f }, { smooth, 50.0f }, { airHz, 14000.0f }, { mix, 80.0f } },
          wokal, "Chórki unoszą się nad głównym wokalem, jaśniejsze, ale schowane za nim." },
        { "Rap: dykcja", { { presence, 65.0f }, { air, 30.0f }, { character, 1.0f }, { presenceHz, 4200.0f }, { smooth, 65.0f } },
          wokal, "Spółgłoski czytelne nawet przy głośnym bicie. Obecność wyżej, powietrze oszczędnie." },

        { "Gitara akustyczna: struny", { { presence, 40.0f }, { air, 50.0f }, { character, 2.0f }, { presenceHz, 5500.0f }, { airHz, 11000.0f }, { smooth, 35.0f } },
          akustyk, "Szelest strun i błysk kostki, z analogowymi harmonicznymi. Gitara gra bliżej." },
        { "Fortepian: otwarcie", { { presence, 25.0f }, { air, 45.0f }, { airHz, 13000.0f }, { smooth, 30.0f } },
          akustyk, "Fortepian dostaje przestrzeń nad sobą, a atak młoteczków zostaje naturalny." },
        { "Smyczki: jedwab", { { presence, 15.0f }, { air, 60.0f }, { airHz, 15000.0f }, { smooth, 40.0f } },
          akustyk, "Smyczki i pady lśnią na samej górze, bez szorstkości w środku." },

        { "Blachy: połysk", { { presence, 15.0f }, { air, 55.0f }, { character, 1.0f }, { airHz, 13000.0f }, { smooth, 20.0f } },
          bebny, "Talerze i hi-hat błyszczą. Gładkość nisko, bo tu jasność jest celem." },
        { "Werbel: atak", { { presence, 60.0f }, { air, 25.0f }, { character, 1.0f }, { presenceHz, 5000.0f }, { smooth, 25.0f } },
          bebny, "Werbel strzela ostrzej: dodatek skacze na uderzeniach i cofa się w wybrzmieniu." },
        { "Overheady: przestrzeń", { { presence, 20.0f }, { air, 65.0f }, { airHz, 16000.0f }, { smooth, 30.0f } },
          bebny, "Pomieszczenie i blachy nad zestawem otwierają się szeroko i wysoko." },

        { "Miks: więcej powietrza", { { presence, 20.0f }, { air, 45.0f }, { airHz, 14000.0f }, { smooth, 50.0f } },
          miks, "Cały miks oddycha na górze. Gdy coś już jest jasne, Powiew sam odpuszcza." },
        { "Ciemny miks: rozjaśnij", { { presence, 50.0f }, { air, 65.0f }, { character, 2.0f }, { presenceHz, 5500.0f }, { smooth, 45.0f } },
          miks, "Zamulone nagranie odzyskuje górę: harmoniczne tworzą to, czego brakowało." },
        { "Syntezatory: blask", { { presence, 35.0f }, { air, 50.0f }, { character, 1.0f }, { smooth, 35.0f } },
          miks, "Leady i pady nabierają szklistego połysku i przebijają się przez miks." },

        { "Master: subtelny powiew", { { presence, 12.0f }, { air, 30.0f }, { airHz, 16000.0f }, { monoSafe, 1.0f } },
          master, "Ledwo słyszalne otwarcie sumy. Dodatek tylko w środku, więc boki się nie zmieniają." },
        { "Master: drogi połysk", { { presence, 22.0f }, { air, 45.0f }, { airHz, 14000.0f }, { smooth, 55.0f }, { monoSafe, 1.0f } },
          master, "Nowoczesna, jasna suma bez ostrości. Pilnuje sybilantów i talerzy." },
        { "Master: analogowe ciepło góry", { { presence, 20.0f }, { air, 30.0f }, { character, 2.0f }, { mix, 70.0f }, { monoSafe, 1.0f } },
          master, "Harmoniczne jak ze sprzętu analogowego, wmieszane w 70%. Góra gęstsza, nie głośniejsza." },

        { "Podcast: czytelność", { { presence, 45.0f }, { air, 20.0f }, { presenceHz, 4500.0f }, { smooth, 70.0f }, { monoSafe, 1.0f } },
          podcast, "Mowa wyraźna na słuchawkach i w samochodzie. Sybilanty trzymane krótko." },
        { "Podcast: radiowy głos", { { presence, 50.0f }, { air, 35.0f }, { character, 2.0f }, { presenceHz, 4000.0f }, { smooth, 75.0f } },
          podcast, "Ciepły, bliski głos z połyskiem jak z dobrego mikrofonu studyjnego." },
        { "Mikrofon z laptopa: ratunek", { { presence, 60.0f }, { air, 55.0f }, { character, 2.0f }, { presenceHz, 5000.0f }, { smooth, 70.0f } },
          podcast, "Głuche nagranie z laptopa albo telefonu odzyskuje górę, której mikrofon nie nagrał." },
    };
}

juce::StringArray presetExcludedIds() { return { ids::bypass, ids::delta }; }

} // namespace powiew
