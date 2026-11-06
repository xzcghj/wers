#include "FactoryPresets.h"
#include "Params.h"
#include "RefrenParams.h"
#include <cstring>

namespace refren {

namespace {

// Written out, because a preset keeps pointers to these texts for as long as the program runs.
struct LayerIds {
    const char* on, *source, *voices, *semis, *formant, *level, *width, *time, *detune, *low, *high, *note;
};
constexpr std::array<LayerIds, 4> kLayerIds{ {
    { "l1On", "l1Source", "l1Voices", "l1Semis", "l1Formant", "l1Level", "l1Width", "l1Time", "l1Detune", "l1Low", "l1High", "l1Note" },
    { "l2On", "l2Source", "l2Voices", "l2Semis", "l2Formant", "l2Level", "l2Width", "l2Time", "l2Detune", "l2Low", "l2High", "l2Note" },
    { "l3On", "l3Source", "l3Voices", "l3Semis", "l3Formant", "l3Level", "l3Width", "l3Time", "l3Detune", "l3Low", "l3High", "l3Note" },
    { "l4On", "l4Source", "l4Voices", "l4Semis", "l4Formant", "l4Level", "l4Width", "l4Time", "l4Detune", "l4Low", "l4High", "l4Note" },
} };

// What a preset does not say takes the default of the parameter.
struct Voices {
    Source source = Source::Voice;
    int count = 2, semis = 0;
    float formant = 0.0f, time = 12.0f, width = 50.0f, detune = 8.0f, low = 20.0f, high = 20000.0f, level = -6.0f;
    int note = 48;
};

struct Bus {
    float verb = 0.0f, decay = 1.5f, size = 50.0f, verbTone = 6000.0f, pre = 10.0f, shimmer = 0.0f;
    float echo = 0.0f;
    EchoTime echoTime = EchoTime::Eighth;
    float feedback = 25.0f;
    bool ping = false;
    float echoTone = 6000.0f;
    float grain = 0.0f;
    Character character = Character::Lamp;
    Motion motion = Motion::None;
    float rate = 1.0f, depth = 0.0f;
    float tilt = 0.0f, duck = 50.0f, send = 0.0f, consonants = 100.0f, dry = 0.0f, width = 100.0f, time = 100.0f;
};

using Values = std::vector<std::pair<const char*, float>>;

void add(Values& values, const char* id, float value, float initial) {
    if (std::abs(value - initial) > 1.0e-6f) values.emplace_back(id, value);
}

Values valuesOf(const std::vector<Voices>& layers, const Bus& b) {
    Values v;
    for (size_t k = 0; k < kLayerIds.size(); ++k) {
        const auto& id = kLayerIds[k];
        const bool on = k < layers.size();
        add(v, id.on, on ? 1.0f : 0.0f, k == 0 ? 1.0f : 0.0f);
        if (!on) continue;
        const auto& l = layers[k];
        add(v, id.source, static_cast<float>(l.source), 0.0f);
        add(v, id.voices, static_cast<float>(l.count), 2.0f);
        add(v, id.semis, static_cast<float>(l.semis), 0.0f);
        add(v, id.formant, l.formant, 0.0f);
        add(v, id.level, l.level, -6.0f);
        add(v, id.width, l.width, 50.0f);
        add(v, id.time, l.time, 12.0f);
        add(v, id.detune, l.detune, 8.0f);
        add(v, id.low, l.low, 20.0f);
        add(v, id.high, l.high, 20000.0f);
        add(v, id.note, static_cast<float>(l.note), 48.0f);
    }
    add(v, ids::verbMix, b.verb, 0.0f);
    add(v, ids::verbDecay, b.decay, 1.5f);
    add(v, ids::verbSize, b.size, 50.0f);
    add(v, ids::verbTone, b.verbTone, 6000.0f);
    add(v, ids::verbPre, b.pre, 10.0f);
    add(v, ids::verbShimmer, b.shimmer, 0.0f);
    add(v, ids::echoMix, b.echo, 0.0f);
    add(v, ids::echoTime, static_cast<float>(b.echoTime), static_cast<float>(EchoTime::Eighth));
    add(v, ids::echoFeedback, b.feedback, 25.0f);
    add(v, ids::echoPing, b.ping ? 1.0f : 0.0f, 0.0f);
    add(v, ids::echoTone, b.echoTone, 6000.0f);
    add(v, ids::grain, b.grain, 0.0f);
    add(v, ids::grainChar, static_cast<float>(b.character), 0.0f);
    add(v, ids::motion, static_cast<float>(b.motion), 0.0f);
    add(v, ids::motionRate, b.rate, 1.0f);
    add(v, ids::motionDepth, b.depth, 0.0f);
    add(v, ids::tilt, b.tilt, 0.0f);
    add(v, ids::duck, b.duck, 50.0f);
    add(v, ids::sendDry, b.send, 0.0f);
    add(v, ids::consonants, b.consonants, 100.0f);
    add(v, ids::dry, b.dry, 0.0f);
    add(v, ids::width, b.width, 100.0f);
    add(v, ids::time, b.time, 100.0f);
    return v;
}

struct Trim {
    const char* name;
    float db;
};

// What each preset needs to be as loud as the voice that goes in. Written by refren_trim.
constexpr Trim kTrims[] = {
    { "", 0.0f },
#include "PresetTrims.inc"
};

float trimFor(const char* name) {
    for (const auto& t : kTrims)
        if (std::strcmp(t.name, name) == 0) return t.db;
    return 0.0f;
}

class Builder {
public:
    Builder& group(const char* name) {
        group_ = name;
        return *this;
    }
    Builder& add(const char* name, const char* description, std::vector<Voices> layers, Bus bus = {}) {
        auto values = valuesOf(layers, bus);
        refren::add(values, ids::trim, trimFor(name), 0.0f);
        all_.push_back({ name, std::move(values), group_, description });
        return *this;
    }
    std::vector<wers::ui::FactoryPreset> take() { return std::move(all_); }
private:
    const char* group_ = nullptr;
    std::vector<wers::ui::FactoryPreset> all_;
};

} // namespace

std::vector<wers::ui::FactoryPreset> factoryPresets() {
    Builder b;
    b.group("Dubel")
        .add("Ciasny", "Jeden bliski dubel, prawie na samym głosie.", { { .count = 1, .time = 6.0f, .width = 0.0f, .detune = 3.0f, .level = -8.0f } })
        .add("Luźny", "Dwa dublujące głosy, lekko spóźnione i rozstrojone.", { { .time = 14.0f, .detune = 8.0f, .level = -8.0f } })
        .add("Lewo i prawo", "Po jednym dublu na każdą stronę.", { { .time = 10.0f, .width = 100.0f, .detune = 5.0f, .level = -8.0f } })
        .add("Szeroki dubel", "Cztery głosy rozłożone szeroko po bokach.", { { .count = 4, .time = 16.0f, .width = 100.0f, .detune = 6.0f, .level = -8.0f } })
        .add("Za głosem", "Dubel wyraźnie spóźniony, jak drugie nagranie.", { { .time = 28.0f, .width = 40.0f, .detune = 6.0f, .level = -10.0f } })
        .add("Przed i za", "Jeden głos wchodzi przed tobą, drugi po tobie.", { { .time = 40.0f, .width = 70.0f, .detune = 5.0f, .level = -10.0f } })
        .add("Ciemny dubel", "Dubel bez góry, ukryty pod głosem.", { { .time = 12.0f, .width = 60.0f, .detune = 6.0f, .high = 3000.0f, .level = -5.0f } }, { .dry = -3.0f })
        .add("Jasny dubel", "Dubel bez dołu, sama góra dokoła głosu.", { { .time = 10.0f, .width = 70.0f, .detune = 5.0f, .low = 1000.0f, .level = -6.0f } })
        .add("Cienki dubel", "Wąskie pasmo po bokach, jak ślad w tle.", { { .time = 10.0f, .width = 100.0f, .low = 800.0f, .high = 6000.0f, .level = -14.0f } })
        .add("Gruby dubel", "Ciemniejsza barwa, lekkie ziarno i cichy sub pod spodem.", { { .formant = -1.0f, .time = 12.0f, .width = 70.0f, .detune = 10.0f, .level = -8.0f }, { .source = Source::Sub, .count = 1, .semis = -12, .time = 6.0f, .width = 0.0f, .level = -20.0f } }, { .grain = 6.0f })
        .add("Płynący", "Mocno rozstrojone głosy, które wolno się przelewają.", { { .time = 16.0f, .width = 40.0f, .detune = 30.0f, .level = -9.0f } })
        .add("Ręczny", "Luźno zagrany dubel, jak nagrany drugi raz i rozłożony na boki.", { { .time = 30.0f, .width = 100.0f, .detune = 14.0f, .level = -9.0f } });
    b.group("Refren")
        .add("Szeroki hook", "Osiem szerokich głosów z cichą oktawą nad nimi.", { { .count = 8, .time = 22.0f, .width = 100.0f, .detune = 10.0f, .level = -6.0f }, { .semis = 12, .time = 10.0f, .width = 90.0f, .level = -16.0f } }, { .verb = 15.0f, .decay = 0.8f })
        .add("Melodyjny", "Sześć głosów w szerokim, miękkim pogłosie.", { { .count = 6, .time = 18.0f, .width = 90.0f, .detune = 8.0f, .level = -10.0f } }, { .verb = 20.0f, .decay = 1.2f })
        .add("Stadion", "Cała trybuna śpiewa razem z tobą.", { { .count = 12, .time = 35.0f, .width = 100.0f, .detune = 14.0f, .level = -6.0f } }, { .verb = 50.0f, .decay = 3.0f, .size = 70.0f })
        .add("Mocny refren", "Gęsty refren z nasyceniem lampy.", { { .count = 8, .time = 20.0f, .width = 85.0f, .detune = 8.0f, .level = -6.0f } }, { .grain = 8.0f })
        .add("Ciepły refren", "Miękki, ciemniejszy refren bez ostrej góry.", { { .count = 6, .time = 18.0f, .width = 75.0f, .detune = 8.0f, .high = 4000.0f, .level = -5.0f } }, { .grain = 4.0f, .character = Character::Tape, .tilt = -20.0f, .dry = -3.0f })
        .add("Jasny refren", "Lekki refren, sama góra po bokach.", { { .count = 8, .time = 20.0f, .width = 70.0f, .detune = 8.0f, .low = 300.0f, .level = -12.0f } }, { .tilt = 30.0f })
        .add("Oktawa w tle", "Szeroki refren z cichą oktawą pod spodem.", { { .count = 6, .time = 20.0f, .width = 90.0f, .level = -7.0f }, { .count = 4, .semis = -12, .time = 8.0f, .width = 60.0f, .level = -14.0f }, { .source = Source::Sub, .count = 1, .semis = -12, .time = 6.0f, .width = 0.0f, .level = -22.0f } })
        .add("Oktawa nad głosem", "Szeroki refren z cichą oktawą nad głosem.", { { .count = 6, .time = 20.0f, .width = 100.0f, .level = -8.0f }, { .count = 4, .semis = 12, .time = 8.0f, .width = 70.0f, .level = -18.0f } }, { .verb = 12.0f, .decay = 0.7f })
        .add("Kwinta w refrenie", "Szeroki refren z kwintą w tle.", { { .count = 6, .time = 20.0f, .width = 60.0f, .level = -10.0f }, { .semis = 7, .time = 8.0f, .width = 80.0f, .level = -18.0f } }, { .verb = 25.0f, .decay = 0.9f })
        .add("Wąski refren", "Gęsty refren skupiony blisko środka.", { { .count = 8, .time = 22.0f, .width = 30.0f, .detune = 10.0f, .level = -9.0f } }, { .verb = 10.0f, .decay = 0.6f })
        .add("Refren z echem", "Szeroki refren z echem na ósemkach.", { { .count = 6, .time = 18.0f, .width = 90.0f, .level = -10.0f } }, { .echo = 25.0f, .feedback = 30.0f })
        .add("Drugi plan", "Głos cofa się o krok, a refren otacza go z boków.", { { .count = 10, .time = 30.0f, .width = 100.0f, .detune = 12.0f, .low = 400.0f, .level = -9.0f } }, { .verb = 25.0f, .decay = 1.2f, .dry = -6.0f });
    b.group("Ekipa")
        .add("Trzech ziomków", "Trzy różne głosy rzucają wersy razem z tobą.", { { .formant = -2.0f, .time = 29.0f, .width = 55.0f, .detune = 12.0f, .level = -7.0f }, { .count = 1, .formant = 2.0f, .time = 21.0f, .detune = 10.0f, .level = -8.0f } }, { .dry = -5.0f })
        .add("Tłum", "Szesnaście luźnych głosów o różnej barwie.", { { .count = 8, .formant = -2.0f, .time = 40.0f, .width = 100.0f, .detune = 25.0f, .level = -8.0f }, { .count = 8, .formant = 2.0f, .time = 40.0f, .width = 100.0f, .detune = 25.0f, .level = -8.0f } }, { .dry = -4.0f })
        .add("Trybuny", "Wielki tłum w dużej przestrzeni.", { { .count = 12, .formant = -2.0f, .time = 40.0f, .width = 100.0f, .detune = 20.0f, .level = -8.0f }, { .count = 12, .formant = 2.0f, .time = 40.0f, .width = 100.0f, .detune = 20.0f, .level = -8.0f } }, { .verb = 40.0f, .decay = 2.2f, .size = 70.0f, .dry = -4.0f })
        .add("Piątka", "Pięć osób przy jednym mikrofonie.", { { .count = 3, .formant = -1.0f, .time = 28.0f, .width = 90.0f, .detune = 14.0f, .level = -9.0f }, { .formant = 2.0f, .time = 22.0f, .width = 60.0f, .level = -10.0f } })
        .add("Szatnia", "Niskie głosy w małym, twardym pomieszczeniu.", { { .count = 6, .formant = -3.0f, .time = 30.0f, .width = 50.0f, .detune = 14.0f, .level = -9.0f } }, { .verb = 30.0f, .decay = 0.4f, .size = 20.0f })
        .add("Ulica", "Ekipa na zewnątrz, z odbiciami od ścian po obu stronach.", { { .count = 5, .formant = -1.0f, .time = 36.0f, .width = 100.0f, .high = 3500.0f, .level = -6.0f }, { .count = 3, .formant = 1.0f, .time = 24.0f, .width = 100.0f, .high = 3500.0f, .level = -8.0f } }, { .echo = 30.0f, .echoTime = EchoTime::Slap, .feedback = 35.0f })
        .add("Ziomek obok", "Jeden kolega z niższym głosem tuż obok.", { { .count = 1, .formant = -2.0f, .time = 28.0f, .level = -7.0f } })
        .add("Wyższe głosy", "Cztery głosy o wyższej, jaśniejszej barwie.", { { .count = 4, .formant = 4.0f, .time = 27.0f, .width = 90.0f, .detune = 12.0f, .level = -5.0f } }, { .dry = -3.0f })
        .add("Niskie głosy", "Cztery głosy o niższej, ciemniejszej barwie.", { { .count = 4, .formant = -4.0f, .time = 24.0f, .width = 90.0f, .detune = 12.0f, .level = -5.0f } }, { .dry = -3.0f })
        .add("Cała ekipa", "Dwadzieścia głosów o trzech różnych barwach.", { { .count = 8, .formant = -2.0f, .time = 38.0f, .width = 100.0f, .level = -8.0f }, { .count = 8, .formant = 1.0f, .time = 34.0f, .width = 100.0f, .level = -8.0f }, { .count = 4, .formant = 3.0f, .time = 30.0f, .width = 80.0f, .level = -10.0f } }, { .dry = -7.0f });
    b.group("Chór")
        .add("Gospel", "Pełny chór z oktawami w obie strony.", { { .count = 16, .time = 30.0f, .width = 100.0f, .detune = 15.0f, .level = -10.0f }, { .count = 8, .semis = -12, .time = 20.0f, .width = 90.0f, .level = -16.0f }, { .count = 8, .semis = 12, .time = 20.0f, .width = 90.0f, .level = -18.0f } }, { .verb = 50.0f, .decay = 3.0f, .dry = -5.0f })
        .add("Katedra", "Chór w ogromnej, długo brzmiącej przestrzeni.", { { .count = 16, .time = 35.0f, .width = 100.0f, .detune = 12.0f, .level = -9.0f } }, { .verb = 60.0f, .decay = 6.0f, .size = 90.0f, .dry = -9.0f })
        .add("Ściana", "Trzydzieści dwa głosy zlane w jedną ścianę.", { { .count = 16, .time = 40.0f, .width = 100.0f, .detune = 18.0f, .level = -8.0f }, { .count = 16, .formant = -1.0f, .time = 40.0f, .width = 100.0f, .detune = 18.0f, .level = -8.0f } }, { .verb = 35.0f, .decay = 2.0f, .dry = -9.0f })
        .add("Sala", "Chór w średniej sali koncertowej.", { { .count = 16, .time = 28.0f, .width = 100.0f, .detune = 10.0f, .level = -9.0f } }, { .verb = 35.0f, .decay = 1.5f, .dry = -6.0f })
        .add("Mnisi", "Niski, ciemny chór oktawę pod głosem.", { { .count = 16, .semis = -12, .formant = -2.0f, .time = 30.0f, .width = 100.0f, .detune = 8.0f, .high = 5000.0f, .level = -8.0f } }, { .verb = 50.0f, .decay = 5.0f, .dry = -12.0f })
        .add("Mały chór", "Jasne głosy w niedużym pomieszczeniu.", { { .count = 16, .formant = 3.0f, .time = 25.0f, .width = 90.0f, .detune = 12.0f, .level = -10.0f } }, { .verb = 40.0f, .decay = 2.0f, .size = 40.0f, .dry = -6.0f })
        .add("Długa sala", "Chór, którego pogłos wybrzmiewa bardzo długo.", { { .count = 16, .time = 30.0f, .width = 100.0f, .detune = 10.0f, .level = -9.0f } }, { .verb = 55.0f, .decay = 8.0f, .dry = -9.0f })
        .add("Chór w oddali", "Odległy chór, prawie bez głosu z przodu.", { { .count = 16, .time = 35.0f, .width = 100.0f, .low = 300.0f, .high = 6000.0f, .level = -10.0f } }, { .verb = 70.0f, .decay = 4.0f, .dry = -24.0f })
        .add("Kwinty w chórze", "Chór z kwintą w górę i kwartą w dół.", { { .count = 12, .time = 30.0f, .width = 100.0f, .level = -10.0f }, { .count = 12, .semis = 7, .time = 20.0f, .width = 100.0f, .level = -14.0f }, { .count = 8, .semis = -5, .time = 20.0f, .width = 100.0f, .level = -14.0f } }, { .verb = 45.0f, .decay = 4.0f, .dry = -6.0f })
        .add("Pełny chór", "Cztery warstwy: głos, oktawy i kwinta.", { { .count = 8, .time = 30.0f, .width = 100.0f, .level = -10.0f }, { .count = 8, .semis = 12, .time = 20.0f, .width = 100.0f, .level = -16.0f }, { .count = 8, .semis = -12, .time = 20.0f, .width = 100.0f, .level = -14.0f }, { .count = 8, .semis = 7, .time = 20.0f, .width = 100.0f, .level = -16.0f } }, { .verb = 50.0f, .decay = 5.0f, .dry = -6.0f });
    b.group("Głęboki")
        .add("Sub pod głosem", "Czysty ton oktawę pod głosem, który daje dół bez barwy.", { { .source = Source::Sub, .count = 1, .semis = -12, .level = -8.0f } })
        .add("Demon", "Oktawa w dół z ciemną barwą i ziarnem, suchy głos schowany.", { { .semis = -12, .formant = -3.0f, .time = 4.0f, .width = 30.0f, .level = -4.0f } }, { .grain = 10.0f, .character = Character::Transformer, .dry = -15.0f })
        .add("Olbrzym", "Wielki, ciemny głos w dużej przestrzeni.", { { .count = 3, .semis = -12, .formant = -5.0f, .time = 8.0f, .level = -4.0f } }, { .verb = 20.0f, .decay = 1.2f, .dry = -18.0f })
        .add("Oktawa w dół", "Jeden głos oktawę niżej, równo z tobą.", { { .count = 1, .semis = -12, .time = 0.0f, .width = 0.0f, .level = -6.0f } })
        .add("Dwie oktawy w dół", "Dwie oktawy pod głosem, z subem na dnie.", { { .count = 1, .semis = -24, .time = 0.0f, .width = 0.0f, .level = -8.0f }, { .source = Source::Sub, .count = 1, .semis = -12, .level = -12.0f } }, { .dry = -9.0f })
        .add("Bas pod spodem", "Sam dół dwóch niskich głosów i sub, reszta zostaje czysta.", { { .semis = -12, .time = 6.0f, .width = 40.0f, .low = 40.0f, .high = 1000.0f, .level = -6.0f }, { .source = Source::Sub, .count = 1, .semis = -12, .level = -14.0f } })
        .add("Niski i szeroki", "Cztery głosy oktawę niżej, szeroko po bokach.", { { .count = 4, .semis = -12, .time = 12.0f, .width = 100.0f, .level = -7.0f } })
        .add("Ciemny głos", "Ten sam głos z ciemniejszą barwą, bez zmiany wysokości.", { { .count = 1, .formant = -4.0f, .time = 0.0f, .width = 0.0f, .level = -3.0f } }, { .dry = -18.0f })
        .add("Potwór", "Kwinta w dół, bardzo ciemna barwa i mocny przester.", { { .semis = -7, .formant = -6.0f, .time = 6.0f, .width = 40.0f, .level = -3.0f } }, { .grain = 16.0f, .character = Character::Transistor, .dry = -24.0f })
        .add("Sub i echo", "Sub pod głosem i echo na ćwierćnuty.", { { .source = Source::Sub, .count = 1, .semis = -12, .level = -8.0f } }, { .echo = 30.0f, .echoTime = EchoTime::Quarter, .feedback = 30.0f });
    b.group("Wysoki")
        .add("Anioły", "Jasne głosy oktawę wyżej w długim pogłosie.", { { .count = 6, .semis = 12, .formant = 2.0f, .time = 16.0f, .width = 100.0f, .level = -10.0f } }, { .verb = 40.0f, .decay = 3.0f, .dry = -6.0f })
        .add("Hyperpop", "Wysoki, nasycony głos prawie bez oryginału.", { { .semis = 12, .formant = 3.0f, .time = 2.0f, .width = 40.0f, .level = -4.0f } }, { .grain = 10.0f, .character = Character::Transistor, .tilt = 20.0f, .dry = -30.0f })
        .add("Chipmunk", "Sam głos oktawę wyżej i z wysoką barwą.", { { .count = 1, .semis = 12, .formant = 6.0f, .time = 0.0f, .width = 0.0f, .level = -3.0f } }, { .dry = -48.0f })
        .add("Oktawa w górę", "Jeden głos oktawę wyżej, równo z tobą.", { { .count = 1, .semis = 12, .time = 0.0f, .width = 0.0f, .level = -8.0f } })
        .add("Dwie oktawy w górę", "Cichy głos dwie oktawy nad tobą.", { { .count = 1, .semis = 24, .time = 0.0f, .width = 0.0f, .level = -12.0f } })
        .add("Szklany", "Sama góra oktawy wyżej, rozpływająca się w pogłosie.", { { .count = 4, .semis = 12, .time = 10.0f, .width = 100.0f, .low = 2000.0f, .level = -12.0f } }, { .verb = 35.0f, .decay = 2.5f, .shimmer = 30.0f })
        .add("Jasna oktawa", "Cztery jasne głosy oktawę wyżej, bez dołu.", { { .count = 4, .semis = 12, .formant = 4.0f, .time = 10.0f, .width = 90.0f, .low = 800.0f, .level = -12.0f } })
        .add("Cienki głos", "Ten sam głos z jaśniejszą barwą, bez zmiany wysokości.", { { .count = 1, .formant = 5.0f, .time = 0.0f, .width = 0.0f, .level = -3.0f } }, { .dry = -18.0f })
        .add("Piskliwy", "Dwa bardzo wysokie głosy nad tobą.", { { .semis = 19, .formant = 4.0f, .time = 4.0f, .width = 60.0f, .level = -14.0f } })
        .add("Lekka oktawa", "Ledwie słyszalna oktawa, która rozjaśnia głos.", { { .count = 3, .semis = 12, .time = 6.0f, .width = 70.0f, .level = -20.0f } });
    b.group("Harmonie")
        .add("Kwinta", "Dwa głosy o kwintę wyżej.", { { .semis = 7, .time = 4.0f, .level = -10.0f } })
        .add("Oktawy", "Oktawa w górę i oktawa w dół.", { { .semis = 12, .time = 4.0f, .width = 60.0f, .level = -14.0f }, { .semis = -12, .time = 4.0f, .width = 40.0f, .level = -12.0f } })
        .add("Kwarta w tle", "Cicha kwarta rozłożona po bokach.", { { .semis = 5, .time = 10.0f, .width = 80.0f, .level = -14.0f } })
        .add("Kwinta w dół", "Dwa głosy o kwintę niżej.", { { .semis = -7, .time = 4.0f, .level = -10.0f } })
        .add("Kwarta w dół", "Dwa głosy o kwartę niżej.", { { .semis = -5, .time = 4.0f, .level = -10.0f } })
        .add("Kwinta i oktawa", "Kwinta i oktawa nad głosem.", { { .semis = 7, .time = 6.0f, .width = 70.0f, .level = -12.0f }, { .semis = 12, .time = 6.0f, .width = 90.0f, .level = -14.0f } })
        .add("Pusty akord", "Kwinta w górę i oktawa w dół.", { { .semis = 7, .time = 4.0f, .width = 60.0f, .level = -10.0f }, { .semis = -12, .time = 4.0f, .width = 30.0f, .level = -10.0f } })
        .add("Szeroka harmonia", "Oktawy i kwinta po cztery głosy, szeroko.", { { .count = 4, .semis = 12, .time = 18.0f, .width = 100.0f, .level = -16.0f }, { .count = 4, .semis = -12, .time = 18.0f, .width = 100.0f, .level = -14.0f }, { .count = 4, .semis = 7, .time = 18.0f, .width = 100.0f, .level = -16.0f } })
        .add("Kwarta i kwinta", "Kwarta i kwinta nad głosem, szeroko.", { { .semis = 5, .time = 6.0f, .width = 90.0f, .level = -13.0f }, { .semis = 7, .time = 6.0f, .width = 90.0f, .level = -13.0f } })
        .add("Organy", "Cztery równe, nieruchome składowe jak w organach.", { { .count = 1, .semis = -12, .time = 0.0f, .detune = 0.0f, .level = -10.0f }, { .count = 1, .semis = 7, .time = 0.0f, .detune = 0.0f, .level = -14.0f }, { .count = 1, .semis = 12, .time = 0.0f, .detune = 0.0f, .level = -14.0f }, { .count = 1, .semis = 19, .time = 0.0f, .detune = 0.0f, .level = -18.0f } });
    b.group("Szept")
        .add("Szept obok", "Ten sam tekst szeptem, tuż przy głosie.", { { .source = Source::Whisper, .count = 1, .time = 8.0f, .level = -8.0f } })
        .add("Oddech", "Sama góra szeptu, jak oddech wokół głosu.", { { .source = Source::Whisper, .time = 16.0f, .width = 60.0f, .low = 2000.0f, .level = -12.0f } })
        .add("W głowie", "Szepty dookoła, a głos prawie znika.", { { .source = Source::Whisper, .count = 4, .time = 20.0f, .width = 100.0f, .level = -9.0f } }, { .verb = 15.0f, .decay = 0.4f, .size = 20.0f, .dry = -18.0f })
        .add("Szept i głos", "Dubel głosu i szept szeroko po bokach.", { { .time = 10.0f, .width = 60.0f, .level = -12.0f }, { .source = Source::Whisper, .time = 14.0f, .width = 100.0f, .level = -10.0f } })
        .add("Duch", "Szept w długim pogłosie, bez głosu z przodu.", { { .source = Source::Whisper, .count = 3, .time = 30.0f, .width = 100.0f, .level = -6.0f } }, { .verb = 50.0f, .decay = 3.0f, .dry = -30.0f })
        .add("Szept w tle", "Cichy, szeroki szept za głosem.", { { .source = Source::Whisper, .count = 4, .time = 30.0f, .width = 100.0f, .level = -15.0f } })
        .add("Szept z echem", "Szept odbijający się od lewej do prawej.", { { .source = Source::Whisper, .time = 10.0f, .width = 60.0f, .level = -10.0f } }, { .echo = 35.0f, .feedback = 40.0f, .ping = true })
        .add("Sam szept", "Tylko szept, bez głosu.", { { .source = Source::Whisper, .count = 1, .time = 0.0f, .width = 0.0f, .level = -3.0f } }, { .dry = -48.0f });
    return b.take();
}

juce::StringArray presetExcludedIds() { return { ids::amount, ids::output, ids::delta, ids::bypass }; }

} // namespace refren
