#include "FactoryPresets.h"
#include "Params.h"

namespace doc {

// Values are in the units the controls show: dB, ms, % and the ratio as a number (100 is infinity, below 1 expands).
// Character 0 is Czysty, 1 Punch, 2 Klej; switches are 0 or 1. Anything left out takes its default:
// -18 dB, 4:1, knee 6 dB, 10 ms, 120 ms, no range limit, peak detector, fully linked, 100% wet.
std::vector<wers::ui::FactoryPreset> factoryPresets() {
    using namespace ids;
    constexpr const char* start = "Start";
    constexpr const char* wokal = "Wokal";
    constexpr const char* bebny = "Bębny";
    constexpr const char* bas = "Bas";
    constexpr const char* miks = "Miks";
    constexpr const char* master = "Master";
    constexpr const char* rownolegla = "Równoległa";
    constexpr const char* sidechain = "Sidechain";
    return {
        { "Neutralnie", {}, start, "Umiarkowane 4:1 z miękkim kolanem. Punkt wyjścia do własnych ustawień." },

        { "Wokal: równo", { { threshold, -26.0f }, { ratio, 3.0f }, { knee, 8.0f }, { attack, 5.0f }, { release, 90.0f }, { autoRelease, 1.0f }, { detector, 30.0f }, { keyHighPass, 90.0f }, { autoMakeup, 1.0f } },
          wokal, "Wyrównuje głos bez śladu pracy: miękko, z powrotem dopasowanym do frazy." },
        { "Wokal: do przodu", { { threshold, -30.0f }, { ratio, 4.5f }, { knee, 6.0f }, { attack, 3.0f }, { release, 60.0f }, { character, 1.0f }, { keyHighPass, 110.0f }, { lookahead, 1.5f }, { makeup, 7.0f } },
          wokal, "Głos staje przed miksem: spółgłoski przechodzą, reszta trzyma się blisko." },
        { "Wokal: gęsto", { { threshold, -34.0f }, { ratio, 8.0f }, { knee, 4.0f }, { attack, 0.6f }, { release, 45.0f }, { lookahead, 3.0f }, { keyHighPass, 120.0f }, { makeup, 13.0f }, { clip, 1.0f } },
          wokal, "Mocno ściśnięty, nowoczesny wokal popowy, który nie ginie pod niczym." },
        { "Lektor i podcast", { { threshold, -28.0f }, { ratio, 3.0f }, { knee, 10.0f }, { attack, 8.0f }, { release, 160.0f }, { autoRelease, 1.0f }, { detector, 60.0f }, { keyHighPass, 100.0f }, { autoMakeup, 1.0f }, { output, -1.5f } },
          wokal, "Mowa równa od szeptu do śmiechu, bez pompowania między słowami." },

        { "Stopa: atak", { { threshold, -22.0f }, { ratio, 4.0f }, { knee, 3.0f }, { attack, 25.0f }, { release, 70.0f }, { character, 1.0f }, { makeup, 3.0f } },
          bebny, "Długi atak przepuszcza uderzenie, a ogon cichnie: stopa klika i kopie." },
        { "Werbel: trzask", { { threshold, -24.0f }, { ratio, 5.0f }, { knee, 4.0f }, { attack, 8.0f }, { release, 90.0f }, { character, 1.0f }, { makeup, 4.0f } },
          bebny, "Werbel z wyraźnym trzaskiem na początku i krótszym, zwartym ciałem." },
        { "Bębny: zwarte", { { threshold, -20.0f }, { ratio, 3.0f }, { knee, 6.0f }, { attack, 15.0f }, { release, 150.0f }, { autoRelease, 1.0f }, { character, 2.0f }, { autoMakeup, 1.0f } },
          bebny, "Cały zestaw gra jak jeden instrument. Klej na ślad bębnów." },
        { "Room: zgniecione", { { threshold, -36.0f }, { ratio, 20.0f }, { knee, 0.0f }, { attack, 0.5f }, { release, 250.0f }, { makeup, 24.0f }, { output, -1.0f }, { clip, 1.0f } },
          bebny, "Mikrofony ambientowe ściśnięte do ściany: pogłos pokoju wychodzi na wierzch." },
        { "Bębny: więcej życia", { { threshold, -26.0f }, { ratio, 0.7f }, { knee, 6.0f }, { attack, 2.0f }, { release, 70.0f }, { range, 6.0f }, { character, 1.0f }, { output, -3.0f }, { clip, 1.0f } },
          bebny, "Ekspansja w górę: głośne uderzenia rosną nawet o 6 dB. Odwrotność kompresji." },

        { "Bas: równy", { { threshold, -24.0f }, { ratio, 4.0f }, { knee, 8.0f }, { attack, 15.0f }, { release, 200.0f }, { detector, 50.0f }, { autoMakeup, 1.0f } },
          bas, "Każda nuta basu tak samo głośna, bez gubienia ataku palców." },
        { "Bas: ciasny", { { threshold, -26.0f }, { ratio, 6.0f }, { knee, 4.0f }, { attack, 3.0f }, { release, 80.0f }, { lookahead, 2.0f }, { character, 1.0f }, { makeup, 8.0f } },
          bas, "Krótko trzymany bas, który siedzi pod stopą jak przyklejony." },
        { "Bas syntezatorowy", { { threshold, -20.0f }, { ratio, 3.0f }, { knee, 10.0f }, { attack, 30.0f }, { release, 300.0f }, { character, 2.0f }, { detector, 40.0f }, { autoMakeup, 1.0f } },
          bas, "Gęsty, równy dół z syntezatora, bez pulsowania na długich nutach." },

        { "Klej na sumę", { { threshold, -16.0f }, { ratio, 2.0f }, { knee, 6.0f }, { attack, 30.0f }, { release, 150.0f }, { autoRelease, 1.0f }, { character, 2.0f }, { keyHighPass, 90.0f }, { autoMakeup, 1.0f } },
          miks, "Szyna miksu: 1–3 dB redukcji i wszystko zaczyna grać razem." },
        { "Gitary na szynie", { { threshold, -20.0f }, { ratio, 3.0f }, { knee, 10.0f }, { attack, 10.0f }, { release, 120.0f }, { character, 2.0f }, { autoMakeup, 1.0f } },
          miks, "Ściana gitar równa i gęsta, bez skakania akordów." },
        { "Pianino łagodnie", { { threshold, -22.0f }, { ratio, 2.5f }, { knee, 12.0f }, { attack, 20.0f }, { release, 250.0f }, { detector, 50.0f }, { autoMakeup, 1.0f } },
          miks, "Wyrównuje dynamikę pianina tak, że tylko słychać więcej szczegółów." },
        { "Akustyk", { { threshold, -24.0f }, { ratio, 3.0f }, { knee, 8.0f }, { attack, 12.0f }, { release, 140.0f }, { autoRelease, 1.0f }, { keyHighPass, 80.0f }, { autoMakeup, 1.0f }, { output, -1.5f } },
          miks, "Gitara akustyczna równo w miksie, a bicie kostki dalej żyje." },

        { "Master: przezroczyście", { { threshold, -14.0f }, { ratio, 1.5f }, { knee, 12.0f }, { attack, 30.0f }, { release, 200.0f }, { autoRelease, 1.0f }, { character, 2.0f }, { keyHighPass, 60.0f }, { range, 4.0f }, { autoMakeup, 1.0f } },
          master, "Najwyżej 4 dB łagodnej kompresji na sumie. Słychać spójność, nie kompresor." },
        { "Master: głośniej", { { threshold, -14.0f }, { ratio, 2.5f }, { knee, 8.0f }, { attack, 10.0f }, { release, 100.0f }, { autoRelease, 1.0f }, { lookahead, 3.0f }, { keyHighPass, 60.0f }, { makeup, 4.0f }, { clip, 1.0f } },
          master, "Więcej gęstości i głośności, a miękki clip łapie to, co wystaje." },
        { "Ogranicznik", { { threshold, -6.0f }, { ratio, 100.0f }, { knee, 2.0f }, { attack, 0.05f }, { release, 60.0f }, { autoRelease, 1.0f }, { lookahead, 5.0f }, { makeup, 4.0f }, { clip, 1.0f } },
          master, "Twardy sufit z wyprzedzeniem: szczyty nie przejdą, reszta zostaje." },

        { "Nowojorska na bębny", { { threshold, -40.0f }, { ratio, 10.0f }, { knee, 0.0f }, { attack, 1.0f }, { release, 100.0f }, { character, 1.0f }, { makeup, 20.0f }, { mix, 40.0f } },
          rownolegla, "Zgnieciona kopia pod oryginałem: bębny grubsze, a uderzenia nietknięte." },
        { "Wokal równoległy", { { threshold, -38.0f }, { ratio, 6.0f }, { knee, 4.0f }, { attack, 3.0f }, { release, 80.0f }, { makeup, 16.0f }, { mix, 35.0f }, { keyHighPass, 100.0f } },
          rownolegla, "Ciche frazy wychodzą do przodu, a głośne zostają naturalne." },
        { "Gęsty środek", { { threshold, -32.0f }, { ratio, 4.0f }, { knee, 8.0f }, { attack, 20.0f }, { release, 200.0f }, { character, 2.0f }, { autoMakeup, 1.0f }, { mix, 50.0f } },
          rownolegla, "Pół na pół: więcej mięsa i ciała w każdym śladzie, bez utraty dynamiki." },

        { "Pompowanie od stopy", { { external, 1.0f }, { threshold, -24.0f }, { ratio, 8.0f }, { knee, 4.0f }, { attack, 1.0f }, { release, 180.0f }, { range, 12.0f } },
          sidechain, "Podepnij stopę na wejście sidechain: pad albo bas oddycha w rytm." },
        { "Bas ustępuje stopie", { { external, 1.0f }, { threshold, -28.0f }, { ratio, 6.0f }, { knee, 4.0f }, { attack, 0.5f }, { release, 90.0f }, { lookahead, 2.0f }, { range, 8.0f }, { keyTilt, -3.0f } },
          sidechain, "Stopa na sidechain: bas ścisza się tylko na moment uderzenia." },
        { "Podkład ustępuje głosowi", { { external, 1.0f }, { threshold, -36.0f }, { ratio, 3.0f }, { knee, 10.0f }, { attack, 15.0f }, { release, 300.0f }, { range, 6.0f }, { detector, 60.0f }, { keyHighPass, 150.0f } },
          sidechain, "Wokal albo lektor na sidechain: muzyka robi mu miejsce, kiedy mówi." },
        { "Syczące spółgłoski", { { threshold, -32.0f }, { ratio, 5.0f }, { knee, 4.0f }, { attack, 0.5f }, { release, 60.0f }, { keyHighPass, 500.0f }, { keyTilt, 6.0f }, { range, 8.0f } },
          sidechain, "Detektor słyszy tylko górę: przycisza całość na ostrym „s” i „sz”." },
    };
}

juce::StringArray presetExcludedIds() { return { ids::bypass, ids::listen }; }

} // namespace doc
