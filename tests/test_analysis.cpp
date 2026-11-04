#include "doctest.h"
#include "TestSignals.h"
#include "analysis/FixPlanner.h"
#include "analysis/Frames.h"
#include "analysis/Sii.h"
using namespace wers;
using namespace wers::test;

namespace {
std::array<float, kSiiBands> arr(std::initializer_list<float> v) {
    std::array<float, kSiiBands> a{}; size_t i = 0; for (float x : v) a[i++] = x; return a;
}
// Speech-like stream: vowel, sibilant, vowel, stop-like gap, repeated.
std::vector<float> fakeVoice(double db, double seconds) {
    const double fs = kAnalysisRate;
    std::vector<float> x;
    int k = 0;
    while (static_cast<double>(x.size()) < seconds * fs) {
        append(x, vowel(130 + 15 * (k % 3), db, static_cast<int>(0.18 * fs), fs));
        append(x, bandNoise(3000, 7400, db - 8, static_cast<int>(0.07 * fs), fs, static_cast<uint32_t>(k + 1)));
        append(x, vowel(150, db, static_cast<int>(0.14 * fs), fs));
        append(x, std::vector<float>(static_cast<size_t>(0.06 * fs), 0.0f));
        ++k;
    }
    return x;
}
}

TEST_CASE("SII follows the published algorithm on the reference example from ANSI S3.5 code") {
    // The example vectors shipped with the reference MATLAB implementation (Loizou 2012).
    const auto sp = arr({ 40, 45, 50, 24, 56, 60, 55, 55, 52, 48, 50, 51, 55, 67, 76, 67, 56, 31 });
    const auto ns = arr({ 30, 50, 60, 20, 60, 50, 70, 45, 80, 40, 60, 20, 60, 22, 55, 50, 67, 40 });
    const double s = siiFromLevels(sp.data(), ns.data());
    CHECK(s > 0.0);
    CHECK(s < 1.0);
    // Hand-checkable bound: speech 30 dB above noise in all bands at normal level is close to 1 or
    // level-limited, and speech far below noise is 0.
    std::array<float, kSiiBands> E{}, loud{}, none{};
    for (size_t i = 0; i < kSiiBands; ++i) { E[i] = 45.0f; loud[i] = 90.0f; none[i] = -40.0f; }
    CHECK(siiFromLevels(E.data(), none.data()) > 0.85);
    CHECK(siiFromLevels(E.data(), loud.data()) == doctest::Approx(0.0));
}

TEST_CASE("score grows monotonically with SII and has the expected shape") {
    CHECK(scoreFromSii(0.0) == doctest::Approx(0.0));
    CHECK(scoreFromSii(1.0) > 98.0f);
    float last = -1.0f;
    for (double s = 0.0; s <= 1.0; s += 0.05) { const float v = scoreFromSii(s); CHECK(v >= last); last = v; }
    CHECK(scoreFromSii(0.5) > 75.0f);
    CHECK(scoreFromSii(0.2) < 40.0f);
}

TEST_CASE("frames report a level that matches the signal") {
    const double fs = kAnalysisRate;
    auto v = bandNoise(900, 1100, -20, static_cast<int>(fs * 2), fs);
    auto b = std::vector<float>(v.size(), 0.0f);
    const auto f = computeFrames(v, b);
    REQUIRE(f.count > 100);
    // The 1 kHz one-third-octave band should hold the power: PSD = power / bandwidth around the band.
    double e = 0;
    for (int i = 20; i < f.count - 20; ++i) e += std::pow(10.0, f.v18(i)[8] * 0.1);
    const double psd = 10.0 * std::log10(e / (f.count - 40));
    CHECK(psd == doctest::Approx(-20.0 - 10.0 * std::log10(232.0)).epsilon(0.1)); // 232 Hz band width
}

TEST_CASE("a louder beat lowers the word score and a louder voice raises it") {
    const double fs = kAnalysisRate;
    const int n = static_cast<int>(fs * 4);
    auto voice = fakeVoice(-20, 4.0); voice.resize(static_cast<size_t>(n));
    const auto scoreAt = [&](double voiceGainDb, double beatDb, Device d) {
        auto v = voice;
        for (auto& s : v) s *= dbToGain(static_cast<float>(voiceGainDb));
        auto b = bandNoise(300, 7000, beatDb, n, fs);
        const auto f = computeFrames(v, b);
        return scoreWord(f, 100, 300, d, {}).score;
    };
    const float soft = scoreAt(0, -30, Device::Earbuds), mid = scoreAt(0, -18, Device::Earbuds), loud = scoreAt(0, -6, Device::Earbuds);
    CHECK(soft > mid);
    CHECK(mid > loud);
    CHECK(soft > 80.0f);
    CHECK(loud < 45.0f);
    CHECK(scoreAt(6, -12, Device::Earbuds) > scoreAt(0, -12, Device::Earbuds));
}

TEST_CASE("same input gives exactly the same scores") {
    const double fs = kAnalysisRate;
    auto v = fakeVoice(-20, 3.0);
    auto b = bandNoise(200, 6000, -22, static_cast<int>(v.size()), fs);
    const auto f1 = computeFrames(v, b), f2 = computeFrames(v, b);
    CHECK(f1.vocal18 == f2.vocal18);
    CHECK(f1.cls == f2.cls);
    const auto s1 = scoreWord(f1, 50, 200, Device::Phone, {}), s2 = scoreWord(f2, 50, 200, Device::Phone, {});
    CHECK(s1.score == s2.score);
}

TEST_CASE("sound classes in frames line up with the signal in time") {
    const double fs = kAnalysisRate;
    std::vector<float> v = vowel(150, -18, static_cast<int>(fs), fs);
    const int sibFrom = static_cast<int>(v.size());
    append(v, bandNoise(3500, 7400, -24, static_cast<int>(fs * 0.3), fs));
    append(v, vowel(150, -18, static_cast<int>(fs), fs));
    const auto f = computeFrames(v, {});
    int sibHits = 0, total = 0;
    const int a = frameAtTime(sibFrom / fs + 0.05), b = frameAtTime(sibFrom / fs + 0.25);
    for (int i = a; i < b; ++i) { ++total; if (f.cls[static_cast<size_t>(i)] == SoundClass::Sibilant) ++sibHits; }
    CHECK(sibHits > total * 3 / 4);
    CHECK(f.cls[static_cast<size_t>(frameAtTime(0.6))] == SoundClass::Vowel);
    const auto spans = consonantSpans(f, sibFrom / fs - 0.1, sibFrom / fs + 0.4);
    REQUIRE(!spans.empty());
    CHECK(spans.front().first == doctest::Approx(sibFrom / fs).epsilon(0.05));
}

TEST_CASE("planner reaches the target or stops at its limits, and never makes things worse") {
    const double fs = kAnalysisRate;
    auto v = fakeVoice(-22, 6.0);
    auto b = bandNoise(300, 7000, -15, static_cast<int>(v.size()), fs);
    const auto f = computeFrames(v, b);
    Word w; w.t0 = 2.0; w.t1 = 2.5;
    const float before = scoreWord(f, frameAtTime(w.t0), frameAtTime(w.t1), Device::Phone, {}).score;
    w.fix = planFix(f, w, Device::Phone, 0.0f);
    REQUIRE(w.fix.active);
    scoreAllDevices(f, w, 0.0f);
    CHECK(w.verdict[0].score > before + 3.0f);
    CHECK(w.verdict[0].baseScore == doctest::Approx(before).epsilon(0.01));
    for (float x : w.fix.vocalBoostDb) CHECK(x <= 4.0f);
    for (float x : w.fix.beatCutDb) CHECK(x <= 6.0f);
    CHECK(w.fix.beatCutDb[0] == 0.0f); // low bands stay alone
    CHECK(w.fix.beatCutDb[1] == 0.0f);
}

TEST_CASE("a word that is already clear gets no fix") {
    auto v = fakeVoice(-14, 3.0);
    auto b = std::vector<float>(v.size(), 0.0f);
    const auto f = computeFrames(v, b);
    Word w; w.t0 = 1.0; w.t1 = 1.4;
    CHECK(!planFix(f, w, Device::Studio, 0.0f).active);
}

TEST_CASE("segmenter finds voice stretches and leaves silence out") {
    const double fs = kAnalysisRate;
    std::vector<float> v(static_cast<size_t>(fs), 0.0f);
    append(v, fakeVoice(-20, 2.0));
    const size_t end = v.size();
    append(v, std::vector<float>(static_cast<size_t>(fs), 0.0f));
    const auto f = computeFrames(v, {});
    const auto spans = segmentVoice(f);
    REQUIRE(!spans.empty());
    CHECK(spans.front().t0 == doctest::Approx(1.0).epsilon(0.05));
    CHECK(spans.back().t1 == doctest::Approx(end / fs).epsilon(0.05));
    for (const auto& s : spans) CHECK(s.t1 - s.t0 < 1.0);
}

#include "analysis/HelperProtocol.h"
#include "analysis/WordTiming.h"
#include <sstream>
#include "analysis/SongSession.h"
#include <thread>

namespace {
struct FakeRecognizer final : Recognizer {
    std::vector<RecognizedWord> words;
    std::vector<RecognizedWord> wordsInMix; // the second call is the check on the mix
    int transcribes = 0;
    std::atomic<int>* calls = nullptr;
    int slowMs = 0;
    std::vector<RecognizedWord> transcribe(const std::vector<float>&, const std::vector<Region>&, const RecognizerOptions&, const std::atomic<bool>& cancel, const std::function<void(float)>& progress) override {
        if (calls) ++*calls;
        for (int i = 0; i < slowMs && !cancel.load(); i += 5) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (progress) progress(1.0f);
        return ++transcribes == 2 ? wordsInMix : words;
    }
};

struct Rig {
    std::shared_ptr<LinkHub> hub = LinkHub::get();
    LinkGroup& group;
    int slot;
    std::shared_ptr<TimelineStore> vs = std::make_shared<TimelineStore>(60), bs = std::make_shared<TimelineStore>(60);
    explicit Rig(int g = 3) : group(hub->group(g)), slot(group.claimSlot()) {
        vs->prepare(48000); bs->prepare(48000);
        group.addBeatStore(bs);
    }
    ~Rig() { group.removeBeatStore(bs); group.releaseSlot(slot); }
    void play(SongSession& s, double seconds, double voiceDb = -20, double beatDb = -18) {
        const int n = static_cast<int>(seconds * 48000);
        std::vector<float> v;
        int k = 0;
        while (static_cast<int>(v.size()) < n) {
            append(v, vowel(140 + 10 * (k % 3), voiceDb, 9600, 48000));
            append(v, bandNoise(3000, 8000, voiceDb - 8, 3200, 48000, static_cast<uint32_t>(k + 3)));
            append(v, vowel(160, voiceDb, 6400, 48000));
            append(v, std::vector<float>(2400, 0.0f));
            ++k;
        }
        v.resize(static_cast<size_t>(n));
        auto b = bandNoise(200, 7000, beatDb, n, 48000);
        s.startListening();
        for (int p = 0; p < n; p += 512) {
            vs->capture(v.data() + p, std::min(512, n - p), p);
            bs->capture(b.data() + p, std::min(512, n - p), p);
        }
        s.stopListeningAndAnalyse();
    }
};
}

TEST_CASE("session: analysis without recognizer gives scored fragments and a plan") {
    Rig r;
    SongSession s;
    s.attachLink(&r.group, r.slot);
    s.setStores(r.vs, nullptr);
    r.play(s, 6.0);
    s.waitIdle();
    CHECK(s.phase() == SessionPhase::Ready);
    auto res = s.result();
    REQUIRE(res);
    CHECK(!res->hasText);
    CHECK(res->hasBeat);
    CHECK(res->words.size() > 5);
    for (const auto& w : res->words) { CHECK(w.text.empty()); CHECK(w.verdict[0].score >= 0.0f); CHECK(w.verdict[0].score <= 100.0f); }
    const Plan* plan = r.group.plan(r.slot);
    REQUIRE(plan != nullptr);
    CHECK(plan->mapFrames > 500);
    CHECK(plan->events.empty());
    s.attachLink(nullptr, -1);
}

TEST_CASE("session: recognizer words are used, refined and scored, and the model listens twice: vocal, then mix") {
    Rig r;
    SongSession s;
    s.attachLink(&r.group, r.slot);
    s.setStores(r.vs, nullptr);
    std::atomic<int> calls{ 0 };
    s.setRecognizerFactory([&] {
        auto f = std::make_unique<FakeRecognizer>();
        f->calls = &calls;
        f->words = { { "Szeptem,", 1.0, 1.4, 0.9f }, { "cisza", 1.5, 1.9, 0.8f } };
        return f;
    });
    r.play(s, 4.0);
    s.waitIdle();
    auto res = s.result();
    REQUIRE(res);
    CHECK(calls == 2);
    CHECK(res->hasText);
    REQUIRE(res->words.size() == 2);
    CHECK(res->words[0].text == "szeptem"); // no capitals or commas guessed by the model
    CHECK(res->words[0].t1 <= res->words[1].t0);
    s.attachLink(nullptr, -1);
}

TEST_CASE("session: the model also listens to the mix and marks what it no longer hears") {
    Rig r;
    SongSession s;
    s.attachLink(&r.group, r.slot);
    s.setStores(r.vs, nullptr);
    s.setRecognizerFactory([&] {
        auto f = std::make_unique<FakeRecognizer>();
        f->words = { { "szeptem", 1.0, 1.4, 0.9f }, { "cisza", 1.5, 1.9, 0.8f }, { "miasto", 2.0, 2.4, 0.8f } };
        f->wordsInMix = { { "szeptem", 1.05, 1.4, 0.9f }, { "miastom", 2.05, 2.4, 0.5f } };
        return f;
    });
    r.play(s, 4.0);
    s.waitIdle();
    auto res = s.result();
    REQUIRE(res);
    REQUIRE(res->words.size() == 3);
    CHECK(res->words[0].heardInMix == 1);
    CHECK(res->words[1].heardInMix == 0);
    CHECK(res->words[2].heardInMix == 1); // one wrong letter is still the same word

    const auto saved = s.save();
    SongSession restored;
    REQUIRE(restored.load(saved.data(), saved.size()));
    CHECK(restored.result()->words[1].heardInMix == 0);
    s.attachLink(nullptr, -1);
}

TEST_CASE("heard words: order and time decide, so repeated words are not confused") {
    const std::vector<RecognizedWord> dry{ { "nie", 1.0, 1.2, 1 }, { "wiem", 1.3, 1.5, 1 }, { "nie", 5.0, 5.2, 1 }, { "wiem", 5.3, 5.5, 1 } };
    const auto heard = markHeardWords(dry, { { "nie", 5.0, 5.2, 1 }, { "wiem", 5.3, 5.5, 1 } });
    REQUIRE(heard.size() == 4);
    CHECK(heard[0] == 0);
    CHECK(heard[1] == 0);
    CHECK(heard[2] == 1);
    CHECK(heard[3] == 1);
    CHECK(markHeardWords({}, {}).empty());
    CHECK(markHeardWords(dry, {})[0] == 0);
}

TEST_CASE("session: fix and undo update result and plan") {
    Rig r;
    SongSession s;
    s.attachLink(&r.group, r.slot);
    s.setStores(r.vs, nullptr);
    r.play(s, 5.0, -24, -12);
    s.waitIdle();
    auto res = s.result();
    REQUIRE(res);
    REQUIRE(res->words.size() > 3);
    const int idx = 2;
    const float before = res->words[idx].verdict[0].score;
    s.fixWord(idx, Device::Phone);
    s.waitIdle();
    res = s.result();
    CHECK(res->words[idx].fix.active);
    CHECK(res->words[idx].verdict[0].score > before);
    const Plan* plan = r.group.plan(r.slot);
    REQUIRE(plan != nullptr);
    CHECK(plan->events.size() == 1);
    s.undoFix(idx);
    s.waitIdle();
    res = s.result();
    CHECK(!res->words[idx].fix.active);
    CHECK(res->words[idx].verdict[0].score == doctest::Approx(before).epsilon(0.001));
    CHECK(r.group.plan(r.slot)->events.empty());
    s.fixAllBelow(kScoreTarget, Device::Phone);
    s.waitIdle();
    int fixed = 0; for (const auto& w : s.result()->words) fixed += w.fix.active ? 1 : 0;
    CHECK(fixed > 0);
    s.clearAllFixes();
    s.waitIdle();
    fixed = 0; for (const auto& w : s.result()->words) fixed += w.fix.active ? 1 : 0;
    CHECK(fixed == 0);
    s.attachLink(nullptr, -1);
}

TEST_CASE("session: a preview does not outlive the result it was made from") {
    Rig r;
    SongSession s;
    s.attachLink(&r.group, r.slot);
    s.setStores(r.vs, nullptr);
    r.play(s, 5.0, -24, -12);
    s.waitIdle();
    REQUIRE(s.result());
    REQUIRE(s.result()->words.size() > 3);
    s.requestPreview(2, Device::Phone);
    s.waitIdle();
    CHECK(s.preview().index == 2);
    CHECK(s.preview().fix.active);
    // Any change to the result can change the answer, so the old one must not be shown.
    s.fixWord(1, Device::Phone);
    s.waitIdle();
    CHECK(s.preview().index == -1);
    s.requestPreview(2, Device::Phone);
    s.waitIdle();
    CHECK(s.preview().index == 2);
    r.play(s, 5.0, -24, -12);
    s.waitIdle();
    CHECK(s.preview().index == -1);
    s.attachLink(nullptr, -1);
}

TEST_CASE("session: silence fails cleanly, then a real take works") {
    Rig r;
    SongSession s;
    s.setStores(r.vs, nullptr);
    s.startListening();
    std::vector<float> z(48000, 0.0f);
    r.vs->capture(z.data(), 48000, 0);
    s.stopListeningAndAnalyse();
    s.waitIdle();
    CHECK(s.phase() == SessionPhase::Failed);
    CHECK(s.failureCode() == "no-vocal");
    r.play(s, 3.0);
    s.waitIdle();
    CHECK(s.phase() == SessionPhase::Ready);
}

TEST_CASE("session: no beat means the score only sees the device") {
    auto vs = std::make_shared<TimelineStore>(60);
    vs->prepare(48000);
    SongSession s;
    s.setStores(vs, nullptr);
    s.startListening();
    auto v = vowel(150, -20, 96000, 48000);
    vs->capture(v.data(), 96000, 0);
    s.stopListeningAndAnalyse();
    s.waitIdle();
    auto res = s.result();
    REQUIRE(res);
    CHECK(!res->hasBeat);
}

TEST_CASE("session: destroying it during a long recognition returns quickly") {
    auto vs = std::make_shared<TimelineStore>(60);
    vs->prepare(48000);
    const auto started = std::chrono::steady_clock::now();
    {
        SongSession s;
        s.setStores(vs, nullptr);
        s.setRecognizerFactory([] {
            auto f = std::make_unique<FakeRecognizer>();
            f->slowMs = 60000;
            return f;
        });
        s.startListening();
        auto v = vowel(150, -20, 96000, 48000);
        vs->capture(v.data(), 96000, 0);
        s.stopListeningAndAnalyse();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < 5.0);
}

TEST_CASE("session: state round trip keeps words, fixes and the ability to fix more") {
    Rig r;
    std::vector<uint8_t> blob;
    int words = 0;
    float score2 = 0;
    {
        SongSession s;
        s.attachLink(&r.group, r.slot);
        s.setStores(r.vs, nullptr);
        s.setLyrics("wstaje gdy miasto spi");
        r.play(s, 5.0, -24, -12);
        s.waitIdle();
        s.fixWord(1, Device::Phone);
        s.waitIdle();
        blob = s.save();
        words = static_cast<int>(s.result()->words.size());
        score2 = s.result()->words[2].verdict[0].score;
        s.attachLink(nullptr, -1);
    }
    SongSession t;
    t.attachLink(&r.group, r.slot);
    REQUIRE(t.load(blob.data(), blob.size()));
    auto res = t.result();
    REQUIRE(res);
    CHECK(static_cast<int>(res->words.size()) == words);
    CHECK(res->words[1].fix.active);
    CHECK(t.lyrics() == "wstaje gdy miasto spi");
    CHECK(t.phase() == SessionPhase::Ready);
    CHECK(r.group.plan(r.slot)->events.size() == 1);
    t.fixWord(2, Device::Phone);
    t.waitIdle();
    CHECK(t.result()->words[2].verdict[0].score >= score2 - 0.5f);
    t.attachLink(nullptr, -1);
}

TEST_CASE("session: damaged state is refused and leaves the session usable") {
    Rig r;
    std::vector<uint8_t> blob;
    {
        SongSession s;
        s.setStores(r.vs, nullptr);
        r.play(s, 3.0);
        s.waitIdle();
        blob = s.save();
    }
    SongSession t;
    CHECK(!t.load(nullptr, 0));
    std::vector<uint8_t> shortBlob(blob.begin(), blob.begin() + 10);
    CHECK(!t.load(shortBlob.data(), shortBlob.size()));
    auto flipped = blob; flipped[blob.size() / 2] ^= 0x55;
    CHECK(!t.load(flipped.data(), flipped.size()));
    auto truncated = blob; truncated.resize(blob.size() / 2);
    CHECK(!t.load(truncated.data(), truncated.size()));
    Lcg rng;
    std::vector<uint8_t> junk(4096);
    for (auto& b : junk) b = static_cast<uint8_t>(rng.next() * 127.0f + 128.0f);
    CHECK(!t.load(junk.data(), junk.size()));
    CHECK(!t.result());
    CHECK(t.load(blob.data(), blob.size()));
    CHECK(t.result());
}

TEST_CASE("syllables come from the spelling") {
    CHECK(syllableCount("miasto") == 2);
    CHECK(syllableCount("nie") == 1);
    CHECK(syllableCount("się") == 1);
    CHECK(syllableCount("ciszy") == 2);
    CHECK(syllableCount("w") == 0);
    CHECK(syllableCount("Popłyniemy,") == 4);
    CHECK(syllableCount("żółć") == 1);
    CHECK(syllableCount("księżyc") == 2);
    CHECK(syllableCount("") == 0);
}

namespace {
// A voice with syllables at known times: a vowel with a soft start and end, short gaps between them.
std::vector<float> syllableVoice(const std::vector<double>& starts, double length, double total) {
    const double fs = kAnalysisRate;
    std::vector<float> x(static_cast<size_t>(total * fs), 0.0f);
    const auto v = vowel(150, -14, static_cast<int>(length * fs), fs);
    for (double t : starts) {
        const auto start = static_cast<size_t>(t * fs);
        for (size_t i = 0; i < v.size() && start + i < x.size(); ++i) {
            const double p = static_cast<double>(i) / static_cast<double>(v.size());
            x[start + i] += static_cast<float>(v[i] * std::sin(kPi * p));
        }
    }
    return x;
}
}

TEST_CASE("peaks are found on every syllable of a fast verse") {
    std::vector<double> starts;
    for (int i = 0; i < 24; ++i) starts.push_back(0.5 + i * 0.14); // seven syllables a second
    const auto f = computeFrames(syllableVoice(starts, 0.12, 5.0), {});
    const auto peaks = findSyllablePeaks(f, 0, f.count);
    REQUIRE(peaks.size() == starts.size());
    for (size_t i = 0; i < peaks.size(); ++i) CHECK(std::abs(frameCentre(peaks[i].frame) - (starts[i] + 0.06)) < 0.03);
}

TEST_CASE("rough word times land on the real syllables") {
    // Words of 2, 1, 3, 1 and 2 syllables, then a pause and two more words.
    const std::vector<int> counts{ 2, 1, 3, 1, 2, 2, 1 };
    const std::vector<std::string> names{ "mama", "nie", "papuga", "tak", "woda", "kawa", "dom" };
    std::vector<double> starts;
    std::vector<TimeSpan> truth;
    double t = 0.5;
    for (size_t w = 0; w < counts.size(); ++w) {
        if (w == 5) t += 1.0;
        const double begin = t;
        for (int s = 0; s < counts[w]; ++s) { starts.push_back(t); t += 0.2; }
        truth.push_back({ begin, t - 0.02 });
    }
    const auto f = computeFrames(syllableVoice(starts, 0.18, t + 1.0), {});
    // The model's times are late or early by up to 170 ms.
    const std::vector<double> error{ 0.15, -0.12, 0.17, -0.10, 0.12, -0.16, 0.10 };
    std::vector<TimeSpan> spans = truth;
    for (size_t w = 0; w < spans.size(); ++w) { spans[w].t0 += error[w]; spans[w].t1 += error[w]; }
    snapWordsToSyllables(f, names, spans);
    REQUIRE(spans.size() == truth.size());
    for (size_t w = 0; w < spans.size(); ++w) {
        CAPTURE(w);
        CHECK(std::abs(spans[w].t0 - truth[w].t0) < 0.06);
        CHECK(std::abs(spans[w].t1 - truth[w].t1) < 0.06);
        if (w > 0) CHECK(spans[w].t0 >= spans[w - 1].t1);
        CHECK(spans[w].t1 > spans[w].t0);
    }
}

TEST_CASE("snapping never breaks the order, whatever it is given") {
    // Peaks that do not match the words at all, a word with no vowel, identical times, empty text.
    std::vector<double> starts{ 1.0, 1.3, 1.6, 4.0, 4.1, 4.2 };
    const auto f = computeFrames(syllableVoice(starts, 0.2, 6.0), {});
    const std::vector<std::string> names{ "w", "z", "", "dziewczyna", "a", "szczebrzeszyn", "i", "o" };
    std::vector<TimeSpan> spans{ { 0.5, 0.5 }, { 0.5, 0.6 }, { 0.6, 0.6 }, { 1.0, 3.0 }, { 3.0, 3.0 }, { 3.1, 5.5 }, { 5.5, 5.6 }, { 5.6, 9.0 } };
    snapWordsToSyllables(f, names, spans);
    for (size_t w = 0; w < spans.size(); ++w) {
        CHECK(spans[w].t1 > spans[w].t0);
        if (w > 0) CHECK(spans[w].t0 >= spans[w - 1].t1);
    }
    std::vector<TimeSpan> none;
    snapWordsToSyllables(f, {}, none);
    CHECK(none.empty());
    const BandFrames emptyFrames;
    std::vector<TimeSpan> one{ { 1.0, 2.0 } };
    snapWordsToSyllables(emptyFrames, { "dom" }, one);
    CHECK(one[0].t0 == 1.0);
}

TEST_CASE("the recognition job and its answer survive the trip between processes") {
    std::vector<float> pcm(1000);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<float>(i) * 0.001f;
    RecognizerOptions opt;
    opt.language = "pl"; opt.prompt = "księżyc nad miastem"; opt.threads = 3;
    const std::vector<Recognizer::Region> regions{ { 10, 400 }, { 500, 300 } };
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(helper::writeJob(stream, pcm, regions, opt));
    helper::Job job;
    REQUIRE(helper::readJob(stream, job));
    CHECK(job.pcm == pcm);
    REQUIRE(job.regions.size() == 2);
    CHECK(job.regions[1].start == 500);
    CHECK(job.regions[1].length == 300);
    CHECK(job.options.prompt == opt.prompt);
    CHECK(job.options.threads == 3);

    std::stringstream bad(std::ios::in | std::ios::out | std::ios::binary);
    bad << "WJOBxxxxxxxx";
    helper::Job broken;
    CHECK(!helper::readJob(bad, broken));

    RecognizedWord w;
    w.text = "żółć i spacja"; w.t0 = 12.5; w.t1 = 13.25; w.confidence = 0.5f;
    std::string line = helper::wordLine(w);
    REQUIRE(line.back() == '\n');
    line.pop_back();
    RecognizedWord back;
    float progress = 0.0f;
    std::string error;
    REQUIRE(helper::parseLine(line, progress, back, error) == helper::LineKind::Word);
    CHECK(back.text == w.text);
    CHECK(back.t0 == doctest::Approx(12.5));
    CHECK(back.t1 == doctest::Approx(13.25));
    CHECK(back.confidence == doctest::Approx(0.5f));
    line = helper::progressLine(0.25f);
    line.pop_back();
    CHECK(helper::parseLine(line, progress, back, error) == helper::LineKind::Progress);
    CHECK(progress == doctest::Approx(0.25f));
    CHECK(helper::parseLine("E ok", progress, back, error) == helper::LineKind::End);
    CHECK(helper::parseLine("X model", progress, back, error) == helper::LineKind::Error);
    CHECK(error == "model");
    CHECK(helper::parseLine("garbage", progress, back, error) == helper::LineKind::Unknown);
    CHECK(helper::parseLine("W 1 2", progress, back, error) == helper::LineKind::Unknown);
}

TEST_CASE("lyrics replace recognised spelling but keep recognised timing") {
    std::vector<RecognizedWord> rec = { { "wstaje", 1.0, 1.3, 0.5f }, { "gdy", 1.3, 1.5, 0.5f }, { "miasto", 1.5, 1.9, 0.5f }, { "jeszcze", 1.9, 2.2, 0.5f }, { "spi", 2.2, 2.5, 0.5f } };
    const auto out = alignToLyrics(rec, "Wstaję, gdy miasto\njeszcze śpi");
    REQUIRE(out.size() == 5);
    CHECK(out[0].text == "Wstaję,");
    CHECK(out[4].text == "śpi");
    CHECK(out[4].t0 == doctest::Approx(2.2));
    // A recognised word with no lyric counterpart keeps its own text.
    std::vector<RecognizedWord> extra = { { "yyy", 0.5, 0.7, 0.2f }, { "miasto", 1.5, 1.9, 0.5f } };
    const auto o2 = alignToLyrics(extra, "miasto");
    CHECK(o2[0].text == "yyy");
    CHECK(o2[1].text == "miasto");
    CHECK(normaliseWord("ŚWIATŁA,") == "światła");
    CHECK(alignToLyrics(rec, "").size() == 5);
}
