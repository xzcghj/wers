// Offline driver for the whole analysis chain on real stems. It behaves like a host: audio goes block
// by block through the same classes the plugin uses.
#include "WavIO.h"
#include "analysis/SongSession.h"
#include "dsp/Fonem.h"
#include "dsp/Place.h"
#include "dsp/VocalFix.h"
#include "link/GroupControl.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace wers;

namespace {
std::vector<float> toMono(const wav::Audio& a) {
    std::vector<float> m(a.ch.at(0).size());
    for (size_t i = 0; i < m.size(); ++i) {
        float s = 0; for (const auto& c : a.ch) s += c[i];
        m[i] = s / static_cast<float>(a.ch.size());
    }
    return m;
}
void feed(TimelineStore& st, const std::vector<float>& x) {
    for (size_t p = 0; p < x.size(); p += 512) st.capture(x.data() + p, static_cast<int>(std::min<size_t>(512, x.size() - p)), static_cast<int64_t>(p));
}
const char* deviceName(int d) { static const char* n[] = { "Telefon", "Pchelki", "Auto", "Laptop", "Studio" }; return n[d]; }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: wers_cli vocal.wav beat.wav [--model path] [--lyrics file] [--depth 0..1] [--fix-all] [--render dir] [--dump file]\n";
        return 2;
    }
    std::string model, lyricsFile, renderDir, dumpFile;
    float depth = 0.0f; bool fixAll = false;
    float vocalGain = 1.0f;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--model" && i + 1 < argc) model = argv[++i];
        else if (a == "--lyrics" && i + 1 < argc) lyricsFile = argv[++i];
        else if (a == "--depth" && i + 1 < argc) depth = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--vocal-gain-db" && i + 1 < argc) vocalGain = dbToGain(static_cast<float>(std::atof(argv[++i])));
        else if (a == "--fix-all") fixAll = true;
        else if (a == "--render" && i + 1 < argc) renderDir = argv[++i];
        else if (a == "--dump" && i + 1 < argc) dumpFile = argv[++i];
    }
    wav::Audio va, ba;
    if (!wav::read(argv[1], va) || !wav::read(argv[2], ba)) { std::cerr << "cannot read input\n"; return 1; }
    const double fs = va.rate;
    auto vocal = toMono(va);
    for (auto& x : vocal) x *= vocalGain;
    auto beat = toMono(ba);
    beat.resize(vocal.size(), 0.0f);

    auto hub = LinkHub::get();
    LinkGroup& group = hub->group(0);
    const int slot = group.claimSlot();
    auto vStore = std::make_shared<TimelineStore>(), bStore = std::make_shared<TimelineStore>();
    vStore->prepare(fs); bStore->prepare(fs);
    group.addBeatStore(bStore);
    SongSession session;
    session.attachLink(&group, slot);
    session.setStores(vStore, nullptr);
    session.setPlaceDepth(depth);
    if (!lyricsFile.empty()) { std::ifstream in(lyricsFile); std::stringstream ss; ss << in.rdbuf(); session.setLyrics(ss.str()); }
    if (!model.empty()) session.setRecognizerFactory([model] {
        std::string err;
        auto r = makeWhisperRecognizer(model, &err);
        if (!r) std::cerr << "recognizer: " << err << "\n";
        return r;
    });

    const auto t0 = std::chrono::steady_clock::now();
    session.startListening();
    feed(*vStore, vocal);
    feed(*bStore, beat);
    session.stopListeningAndAnalyse();
    session.waitIdle();
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    auto res = session.result();
    if (!res) { std::cerr << "analysis failed: " << session.failureCode() << "\n"; return 1; }
    std::cout << "analysis " << seconds << " s, words " << res->words.size() << ", text " << res->hasText << ", beat " << res->hasBeat << ", LUFS " << res->lufs << "\n";

    if (fixAll) {
        const auto t1 = std::chrono::steady_clock::now();
        session.fixAllBelow(kScoreFades, Device::Phone);
        session.waitIdle();
        std::cout << "fix-all " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() << " s\n";
        res = session.result();
    }
    std::array<std::array<int, 3>, kNumDevices> counts{};
    for (const auto& w : res->words)
        for (int d = 0; d < kNumDevices; ++d) {
            const float s = w.verdict[static_cast<size_t>(d)].score;
            ++counts[static_cast<size_t>(d)][s < kScoreDies ? 0 : (s < kScoreFades ? 1 : 2)];
        }
    for (int d = 0; d < kNumDevices; ++d)
        std::cout << deviceName(d) << ": dies " << counts[static_cast<size_t>(d)][0] << ", fades " << counts[static_cast<size_t>(d)][1] << ", clear " << counts[static_cast<size_t>(d)][2] << "\n";

    if (!dumpFile.empty()) {
        std::ofstream o(dumpFile);
        o << "t0\tt1\ttext\tconf\tphone\tearbuds\tcar\tlaptop\tstudio\tphoneBase\tworstBand\tcause\tcons\tfix\n";
        for (const auto& w : res->words) {
            o << w.t0 << '\t' << w.t1 << '\t' << w.text << '\t' << w.confidence;
            for (const auto& v : w.verdict) o << '\t' << v.score;
            o << '\t' << w.verdict[0].baseScore << '\t' << static_cast<int>(w.verdict[0].worstBand) << '\t' << static_cast<int>(w.verdict[0].cause) << '\t' << w.consonants.size() << '\t';
            if (w.fix.active) { for (float x : w.fix.vocalBoostDb) o << x << ','; o << '|'; for (float x : w.fix.beatCutDb) o << x << ','; }
            o << '\n';
        }
    }

    if (!renderDir.empty()) {
        // Same classes as the plugin, in blocks: vocal fix on the vocal, Miejsce on the beat.
        const Plan* plan = group.plan(slot);
        GroupPlaceControl control(group);
        VocalFix fix; fix.prepare(fs);
        Place place; place.prepare(fs); place.setDepth(depth);
        auto v2 = vocal, b2 = beat;
        for (size_t p = 0; p < v2.size(); p += 512) {
            const int n = static_cast<int>(std::min<size_t>(512, v2.size() - p));
            float* cv[1] = { v2.data() + p }; float* cb[1] = { b2.data() + p };
            fix.process(cv, 1, n, static_cast<int64_t>(p), true, plan, true);
            place.process(cb, 1, n, nullptr, 0, static_cast<int64_t>(p), true, &control);
        }
        wav::Audio out; out.rate = va.rate; out.channels = 1;
        out.ch = { v2 }; wav::write(renderDir + "/vocal_fixed.wav", out);
        out.ch = { b2 }; wav::write(renderDir + "/beat_fixed.wav", out);

        // Measure the rendered audio with the model and compare it with what the planner promised.
        TimelineStore a(900), b(900);
        a.prepare(fs); b.prepare(fs); a.arm(); b.arm();
        feed(a, v2); feed(b, b2);
        a.disarm(); b.disarm();
        const auto f = computeFrames(a.copy(), b.copy());
        double sumErr = 0, maxErr = 0; int n = 0;
        double sumGain = 0;
        for (const auto& w : res->words) {
            if (!w.fix.active) continue;
            const float rendered = scoreWord(f, frameAtTime(w.t0), frameAtTime(w.t1), Device::Phone, {}).score;
            const float err = std::abs(rendered - w.verdict[0].score);
            sumErr += err; maxErr = std::max<double>(maxErr, err); ++n;
            sumGain += rendered - w.verdict[0].baseScore;
        }
        if (n > 0) std::cout << "rendered vs predicted on " << n << " fixed words: mean error " << sumErr / n << ", max " << maxErr << ", mean real gain " << sumGain / n << "\n";
    }
    session.attachLink(nullptr, -1);
    group.removeBeatStore(bStore);
    group.releaseSlot(slot);
    return 0;
}
