#include "FactoryPresets.h"
#include "Params.h"

namespace pog {

// Values are in the units the controls show. algo: 0 Sala, 1 Pokój, 2 Płyta, 3 Komora, 4 Ambient, 5 Katedra,
// 6 Błysk, 7 Nieliniowy. era: 0 Dziś, 1 Lata 80, 2 Lata 70. preNote indexes 1/64 .. 1/2 (3 is 1/16, 6 is 1/8, 9 is 1/4).
// shimPitch: 0 +12, 1 +7, 2 +19, 3 +24, 4 -12. Anything left out takes its default: a 2,5 s hall at 30%.
std::vector<wers::ui::FactoryPreset> factoryPresets() {
    using namespace ids;
    constexpr const char* start = "Start";
    constexpr const char* wokal = "Wokal";
    constexpr const char* bebny = "Bębny";
    constexpr const char* gitara = "Gitara";
    constexpr const char* synth = "Syntezator";
    constexpr const char* ambient = "Ambient";
    constexpr const char* przestrzenie = "Przestrzenie";
    constexpr const char* plyty = "Płyty";
    constexpr const char* efekty = "Efekty";
    constexpr const char* miks = "Miks";
    return {
        { "Czysta sala", {}, start, "Punkt wyjścia: sala 2,5 s, 30% pogłosu, wszystko po środku." },

        // --- Wokal ---------------------------------------------------------------------------------
        { "Wokal: jedwabna płyta", { { algo, 2.0f }, { decay, 1.8f }, { predelay, 35.0f }, { mix, 22.0f }, { lowCut, 180.0f }, { damp, 7000.0f }, { highCut, 12000.0f }, { bassMult, 0.8f }, { duck, 25.0f } },
          wokal, "Gładka, jasna płyta z predelayem, który odkleja ją od głosu. Ducking robi miejsce na słowa." },
        { "Wokal: sala w tle", { { algo, 0.0f }, { decay, 2.6f }, { predelay, 60.0f }, { mix, 20.0f }, { size, 65.0f }, { lowCut, 200.0f }, { highCut, 9000.0f }, { duck, 40.0f }, { balance, 70.0f } },
          wokal, "Duża sala, która wchodzi dopiero między frazami. Głos zostaje z przodu." },
        { "Wokal: bliski pokój", { { algo, 1.0f }, { decay, 0.7f }, { size, 45.0f }, { mix, 18.0f }, { balance, 40.0f }, { lowCut, 150.0f }, { modDepth, 20.0f } },
          wokal, "Mały, ciepły pokój: głos brzmi jak nagrany w przestrzeni, a nie w szafie." },
        { "Wokal: ballada", { { algo, 0.0f }, { decay, 3.8f }, { predelay, 80.0f }, { mix, 28.0f }, { size, 75.0f }, { attack, 20.0f }, { lowCut, 160.0f }, { damp, 5000.0f }, { modDepth, 55.0f }, { duck, 30.0f } },
          wokal, "Długi, miękki ogon pod wolne tempo. Gęsto, ale ciemno, więc nie syczy." },
        { "Wokal: eko lat 80", { { algo, 2.0f }, { era, 1.0f }, { decay, 2.2f }, { predelay, 25.0f }, { mix, 25.0f }, { lowCut, 200.0f }, { damp, 9000.0f }, { modDepth, 50.0f } },
          wokal, "Jasna, połyskująca płyta z wczesnej cyfrowej epoki, z lekkim ziarnem w cichym końcu." },
        { "Wokal: chór w kościele", { { algo, 5.0f }, { decay, 5.5f }, { predelay, 40.0f }, { mix, 30.0f }, { lowCut, 140.0f }, { damp, 4000.0f }, { width, 120.0f } },
          wokal, "Kamienna katedra, która powoli rośnie. Do chórków, harmonii i wielogłosów." },
        { "Wokal: rap na sucho", { { algo, 3.0f }, { decay, 0.5f }, { size, 35.0f }, { mix, 14.0f }, { balance, 30.0f }, { lowCut, 250.0f }, { highCut, 9000.0f }, { duck, 50.0f } },
          wokal, "Ledwie słyszalna komora. Wokal nie stoi w próżni, a nadal jest suchy i blisko." },

        // --- Bębny ---------------------------------------------------------------------------------
        { "Werbel: bramka lat 80", { { algo, 7.0f }, { era, 1.0f }, { decay, 0.45f }, { attack, 50.0f }, { mix, 35.0f }, { lowCut, 180.0f }, { size, 55.0f } },
          bebny, "Wielki, płaski pogłos ucięty jak nożem. Klasyka werbla i tomów z lat osiemdziesiątych." },
        { "Werbel: jasna płyta", { { algo, 2.0f }, { decay, 1.4f }, { predelay, 8.0f }, { mix, 25.0f }, { lowCut, 220.0f }, { damp, 9000.0f }, { bassMult, 0.6f } },
          bebny, "Krótka, błyszcząca płyta, która dodaje werblowi długości i blasku." },
        { "Perkusja: pokój", { { algo, 1.0f }, { decay, 0.9f }, { size, 55.0f }, { mix, 22.0f }, { balance, 35.0f }, { lowCut, 90.0f }, { modDepth, 15.0f } },
          bebny, "Pokój nagraniowy z wyraźnymi odbiciami. Cały zestaw zaczyna grać razem." },
        { "Stopa: ciasna komora", { { algo, 3.0f }, { decay, 0.4f }, { size, 25.0f }, { mix, 15.0f }, { balance, 40.0f }, { lowCut, 60.0f }, { highCut, 6000.0f } },
          bebny, "Krótka, twarda komora dodaje stopie ciała bez dudnienia." },
        { "Bęben odwrócony", { { algo, 7.0f }, { decay, 0.6f }, { attack, 100.0f }, { mix, 45.0f }, { lowCut, 150.0f } },
          bebny, "Pogłos rośnie zamiast gasnąć i urywa się tuż przed następnym uderzeniem." },
        { "Blachy w hali", { { algo, 0.0f }, { decay, 2.0f }, { mix, 18.0f }, { lowCut, 600.0f }, { damp, 10000.0f }, { size, 70.0f }, { predelay, 15.0f } },
          bebny, "Tylko góra trafia do sali, więc blachy i overheady dostają powietrze, a dół zostaje czysty." },
        { "Loop z lat 70", { { algo, 3.0f }, { era, 2.0f }, { decay, 1.1f }, { size, 45.0f }, { mix, 25.0f }, { modDepth, 50.0f } },
          bebny, "Ciemna, lekko chwiejna komora. Pętla perkusyjna brzmi jak zdjęta z winylu." },

        // --- Gitara --------------------------------------------------------------------------------
        { "Gitara: sprężynowa jasność", { { algo, 2.0f }, { era, 2.0f }, { decay, 2.0f }, { mix, 28.0f }, { lowCut, 200.0f }, { damp, 8000.0f }, { diffEarly, 55.0f } },
          gitara, "Ciemniejsza płyta z charakterem lat 70. Gra pod surf, rockabilly i czysty przester." },
        { "Gitara akustyczna", { { algo, 1.0f }, { decay, 1.2f }, { size, 60.0f }, { mix, 20.0f }, { balance, 45.0f }, { lowCut, 120.0f }, { damp, 9000.0f } },
          gitara, "Drewniany, jasny pokój. Akustyk dostaje przestrzeń, a nie traci ataku." },
        { "Gitara: szeroka sala", { { algo, 0.0f }, { decay, 3.2f }, { predelay, 45.0f }, { mix, 30.0f }, { size, 80.0f }, { width, 130.0f }, { modDepth, 60.0f } },
          gitara, "Pod czyste arpeggia i długie akordy: szeroko, gęsto, z łagodnym chorusem w ogonie." },
        { "Solo w stadionie", { { algo, 5.0f }, { decay, 4.0f }, { predelay, 90.0f }, { mix, 22.0f }, { lowCut, 250.0f }, { duck, 45.0f } },
          gitara, "Ogromna przestrzeń, która chowa się pod nutami i wypełnia pauzy." },
        { "Gitara: kraut z lat 70", { { algo, 0.0f }, { era, 2.0f }, { decay, 2.8f }, { mix, 30.0f }, { modDepth, 70.0f }, { modRate, 0.4f } },
          gitara, "Ciemna sala z szumiącą, nierówną modulacją. Brudno i psychodelicznie." },

        // --- Syntezator ----------------------------------------------------------------------------
        { "Pad: nieskończona sala", { { algo, 0.0f }, { decay, 8.0f }, { size, 90.0f }, { mix, 40.0f }, { attack, 30.0f }, { modDepth, 70.0f }, { width, 140.0f }, { damp, 5000.0f } },
          synth, "Bardzo długa, gęsta sala z szerokim chorusem. Pad zamienia się w chmurę." },
        { "Lead z lat 80", { { algo, 0.0f }, { era, 1.0f }, { decay, 2.4f }, { predelay, 70.0f }, { mix, 26.0f }, { modDepth, 65.0f }, { modRate, 0.9f } },
          synth, "Jasny, roztańczony hall z epoki pierwszych cyfrowych pogłosów." },
        { "Arpeggio w szkle", { { algo, 2.0f }, { decay, 2.6f }, { preSync, 1.0f }, { preNote, 6.0f }, { mix, 30.0f }, { lowCut, 300.0f }, { damp, 12000.0f } },
          synth, "Predelay ósemkowy, w rytm utworu, i bardzo jasna płyta." },
        { "Bas z przestrzenią", { { algo, 1.0f }, { decay, 0.8f }, { mix, 20.0f }, { lowCut, 300.0f }, { size, 40.0f } },
          synth, "Obcięty dół: bas zostaje twardy na środku, a górne harmoniczne dostają pokój." },
        { "Pluck na hali", { { algo, 0.0f }, { decay, 1.9f }, { predelay, 30.0f }, { mix, 30.0f }, { diffEarly, 90.0f }, { lowCut, 200.0f } },
          synth, "Krótkie dźwięki zostawiają za sobą równy, gładki ogon." },
        { "Błyszczący pad", { { algo, 6.0f }, { decay, 6.0f }, { shimmer, 45.0f }, { mix, 35.0f }, { lowCut, 200.0f }, { width, 130.0f } },
          synth, "Oktawa w górę narasta w ogonie. Pad świeci od środka." },

        // --- Ambient -------------------------------------------------------------------------------
        { "Mgła", { { algo, 4.0f }, { decay, 12.0f }, { size, 95.0f }, { attack, 70.0f }, { mix, 50.0f }, { modDepth, 60.0f }, { damp, 3500.0f }, { highCut, 8000.0f } },
          ambient, "Dźwięk rozpływa się w miękką, ciemną mgłę bez żadnego początku." },
        { "Zorza", { { algo, 6.0f }, { decay, 14.0f }, { shimmer, 60.0f }, { size, 90.0f }, { attack, 40.0f }, { mix, 50.0f }, { width, 150.0f } },
          ambient, "Długi błysk z oktawą w górę. Każda nuta zostawia świetlistą smugę." },
        { "Głęboka woda", { { algo, 6.0f }, { decay, 10.0f }, { shimPitch, 4.0f }, { shimmer, 45.0f }, { mix, 45.0f }, { highCut, 6000.0f }, { damp, 3000.0f } },
          ambient, "Błysk w drugą stronę: oktawa w dół tonie w ciemnym, ciężkim ogonie." },
        { "Kwinty w chmurach", { { algo, 6.0f }, { decay, 9.0f }, { shimPitch, 1.0f }, { shimmer, 50.0f }, { mix, 45.0f }, { attack, 30.0f } },
          ambient, "Kwinta nad kwintą: harmoniczna, organowa chmura." },
        { "Bezkres", { { algo, 5.0f }, { decay, 25.0f }, { size, 100.0f }, { attack, 60.0f }, { mix, 55.0f }, { modDepth, 50.0f }, { damp, 4000.0f }, { bassMult, 1.5f } },
          ambient, "Najdłuższa katedra, jaka się da. Do dronów, gongów i długich plam." },
        { "Taśma z lat 70", { { algo, 4.0f }, { era, 2.0f }, { decay, 7.0f }, { mix, 45.0f }, { modDepth, 80.0f }, { modRate, 0.3f }, { size, 80.0f } },
          ambient, "Szum, chwianie i ciemność starego sprzętu, w wielkiej, miękkiej chmurze." },

        // --- Przestrzenie --------------------------------------------------------------------------
        { "Filharmonia", { { algo, 0.0f }, { decay, 2.2f }, { predelay, 22.0f }, { size, 70.0f }, { mix, 25.0f }, { bassMult, 1.3f }, { damp, 6000.0f }, { balance, 55.0f } },
          przestrzenie, "Sala koncertowa z dobrą akustyką: ciepły dół, przejrzyste odbicia boczne." },
        { "Kamienna katedra", { { algo, 5.0f }, { decay, 7.0f }, { size, 85.0f }, { mix, 30.0f }, { bassMult, 1.4f }, { damp, 3500.0f } },
          przestrzenie, "Wysokie sklepienie, długi ciemny ogon, odbicia z daleka." },
        { "Mały pokój", { { algo, 1.0f }, { decay, 0.45f }, { size, 30.0f }, { mix, 20.0f }, { balance, 35.0f } },
          przestrzenie, "Pokój jak w mieszkaniu: blisko, krótko, naturalnie." },
        { "Studio z drewna", { { algo, 1.0f }, { decay, 0.8f }, { size, 55.0f }, { mix, 22.0f }, { damp, 8000.0f }, { bassMult, 0.9f } },
          przestrzenie, "Duże studio nagraniowe z drewnianymi ścianami. Żywe, ale nie dudni." },
        { "Betonowa komora", { { algo, 3.0f }, { decay, 1.6f }, { size, 55.0f }, { mix, 25.0f }, { damp, 9000.0f } },
          przestrzenie, "Komora pogłosowa z betonu: twarde ściany i wyraźny środek pasma." },
        { "Klatka schodowa", { { algo, 3.0f }, { decay, 2.4f }, { size, 70.0f }, { mix, 25.0f }, { balance, 40.0f }, { damp, 7000.0f }, { bassMult, 0.7f } },
          przestrzenie, "Długa, wąska i twarda. Odbicia gonią się w górę i w dół." },
        { "Hangar", { { algo, 0.0f }, { decay, 5.0f }, { size, 100.0f }, { predelay, 50.0f }, { mix, 30.0f }, { damp, 4000.0f }, { bassMult, 1.6f } },
          przestrzenie, "Pusta, metalowa hala: dużo dołu i dalekie odbicia." },
        { "Kafelki w łazience", { { algo, 3.0f }, { decay, 1.0f }, { size, 20.0f }, { mix, 28.0f }, { balance, 35.0f }, { damp, 12000.0f } },
          przestrzenie, "Mała, jasna i twarda przestrzeń. Dla żartu albo dla charakteru." },
        { "Niewidzialny pokój", { { algo, 4.0f }, { decay, 0.5f }, { size, 40.0f }, { mix, 15.0f }, { lowCut, 120.0f } },
          przestrzenie, "Nie słychać pogłosu, słychać, że go brakuje, kiedy go wyłączysz." },

        // --- Płyty ---------------------------------------------------------------------------------
        { "Płyta złota", { { algo, 2.0f }, { decay, 2.4f }, { mix, 25.0f }, { damp, 7500.0f }, { bassMult, 0.8f } },
          plyty, "Klasyczna płyta: gęsta od pierwszej chwili, jasna i gładka." },
        { "Płyta ciemna", { { algo, 2.0f }, { decay, 3.0f }, { mix, 25.0f }, { damp, 3000.0f }, { highCut, 9000.0f } },
          plyty, "Płyta mocno wytłumiona: ciepła, aksamitna, bez syczenia." },
        { "Płyta krótka", { { algo, 2.0f }, { decay, 0.9f }, { mix, 22.0f }, { lowCut, 200.0f } },
          plyty, "Krótka płyta, która dodaje blasku bez zamazywania rytmu." },
        { "Płyta z lat 80", { { algo, 2.0f }, { era, 1.0f }, { decay, 2.0f }, { mix, 25.0f }, { modDepth, 55.0f } },
          plyty, "Cyfrowa płyta pierwszej generacji: jasna, z chorusem i lekkim ziarnem." },
        { "Płyta z lat 70", { { algo, 2.0f }, { era, 2.0f }, { decay, 2.6f }, { mix, 25.0f }, { modDepth, 45.0f } },
          plyty, "Ciemniejsza, z szumiącą modulacją, jak stara blacha w studiu na piętrze." },
        { "Płyta szeroka", { { algo, 2.0f }, { decay, 3.5f }, { predelay, 30.0f }, { mix, 28.0f }, { width, 150.0f }, { modDepth, 50.0f } },
          plyty, "Płyta rozciągnięta na całą scenę stereo." },

        // --- Efekty --------------------------------------------------------------------------------
        { "Odwrócony oddech", { { algo, 7.0f }, { decay, 1.2f }, { attack, 100.0f }, { mix, 50.0f }, { size, 70.0f } },
          efekty, "Długi, narastający pogłos, który urywa się nagle, jak taśma puszczona od tyłu." },
        { "Bramka z gniazdka", { { algo, 7.0f }, { decay, 0.3f }, { attack, 50.0f }, { mix, 40.0f }, { era, 1.0f } },
          efekty, "Bardzo krótki, ucięty pogłos. Werbel, klask i wszystko, co ma uderzać." },
        { "Zamrożony akord", { { algo, 0.0f }, { decay, 20.0f }, { size, 90.0f }, { mix, 50.0f }, { modDepth, 50.0f }, { attack, 40.0f } },
          efekty, "Prawie nieskończony ogon. Wciśnij Zamroź, a akord zostanie na zawsze." },
        { "Anielski chór", { { algo, 6.0f }, { decay, 8.0f }, { shimPitch, 2.0f }, { shimmer, 55.0f }, { mix, 45.0f }, { attack, 50.0f } },
          efekty, "Błysk o kwintę ponad oktawą: dźwięk unosi się w górę jak chór." },
        { "Kosmiczne echo", { { algo, 5.0f }, { decay, 10.0f }, { preSync, 1.0f }, { preNote, 9.0f }, { mix, 40.0f }, { modDepth, 90.0f }, { modRate, 1.8f } },
          efekty, "Ćwierćnutowy predelay i mocno pływająca katedra. Odlot." },
        { "Dwie oktawy wyżej", { { algo, 6.0f }, { decay, 7.0f }, { shimPitch, 3.0f }, { shimmer, 40.0f }, { mix, 40.0f } },
          efekty, "Ogon wspina się po dwóch oktawach w górę i rozsypuje w iskry." },
        { "Lo-fi taśma", { { algo, 1.0f }, { era, 2.0f }, { decay, 1.4f }, { mix, 35.0f }, { highCut, 4000.0f }, { lowCut, 250.0f }, { modDepth, 100.0f }, { modRate, 2.5f } },
          efekty, "Wąsko, ciemno i rozstrojone. Jak pogłos nagrany na kasetę." },

        // --- Miks ----------------------------------------------------------------------------------
        { "Szyna: sala", { { algo, 0.0f }, { decay, 2.0f }, { mix, 100.0f }, { lowCut, 200.0f }, { highCut, 10000.0f }, { predelay, 20.0f } },
          miks, "Do wysyłki (aux): sam pogłos, z obciętym dołem i górą, żeby nie zamulał miksu." },
        { "Szyna: płyta", { { algo, 2.0f }, { decay, 1.6f }, { mix, 100.0f }, { lowCut, 250.0f }, { highCut, 11000.0f }, { predelay, 15.0f } },
          miks, "Do wysyłki: sama płyta na wokal i werbel, gotowa do miksu." },
        { "Szyna: pokój", { { algo, 1.0f }, { decay, 0.7f }, { mix, 100.0f }, { lowCut, 150.0f }, { balance, 40.0f } },
          miks, "Do wysyłki: wspólny pokój, który skleja instrumenty w jedną przestrzeń." },
        { "Klej do miksu", { { algo, 4.0f }, { decay, 0.9f }, { mix, 12.0f }, { lowCut, 200.0f }, { highCut, 9000.0f }, { size, 50.0f } },
          miks, "Odrobina niewidocznego ambientu na sumie albo grupie. Miks przestaje brzmieć sucho." },
        { "Głębia za sceną", { { algo, 0.0f }, { decay, 3.0f }, { predelay, 0.0f }, { mix, 35.0f }, { balance, 75.0f }, { highCut, 6000.0f }, { damp, 3500.0f } },
          miks, "Ciemny pogłos bez predelayu odsuwa instrument w głąb sceny." },
        { "Do przodu", { { algo, 1.0f }, { decay, 0.6f }, { predelay, 40.0f }, { mix, 15.0f }, { lowCut, 300.0f }, { duck, 60.0f } },
          miks, "Krótki pokój z predelayem i duckingiem: przestrzeń jest, a dźwięk zostaje z przodu." },
    };
}

juce::StringArray presetExcludedIds() { return { ids::bypass, ids::freeze }; }

} // namespace pog
