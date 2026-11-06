#include "doctest.h"
#include "AllocationCount.h"
#include "Measure.h"
#include "RenderProbes.h"
#include "SubVoice.h"
#include "TestSignals.h"
#include "VoiceSignals.h"
#include "WavIO.h"
#include "WhisperVoice.h"
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace refren;
using namespace refren::test;
using namespace wers::test;

namespace {

int streamDelay(double fs) { return static_cast<int>(std::lround(0.030 * fs)) - 49 - 2; }

std::vector<float> runSub(const std::vector<float>& x, double fs, float semitones) {
    VoiceAnalysis voice;
    voice.prepare(fs);
    SubVoice sub;
    sub.prepare(fs, streamDelay(fs));
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        voice.push(x[i]);
        y[i] = sub.tick(voice, semitones);
    }
    return y;
}

std::vector<float> runWhisper(const std::vector<float>& x, double fs) {
    WhisperVoice whisper;
    whisper.prepare(fs);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = whisper.tick(x[i]);
    return y;
}

template <typename Wanted>
double shareOnPitch(const std::vector<float>& y, double fs, double cents, Wanted&& wantedHz) {
    const int delay = streamDelay(fs), step = static_cast<int>(fs * 0.025);
    int total = 0, close = 0;
    for (int start = static_cast<int>(fs * 0.2);; start += step) {
        const double first = wantedHz(static_cast<double>(start - delay) / fs);
        const int length = static_cast<int>(3.0 * fs / first);
        if (start + length + static_cast<int>(1.2 * fs / first) + 2 >= static_cast<int>(y.size())) break;
        const double wanted = wantedHz((start + 0.5 * length - delay) / fs);
        const double hz = framePitchHz(y, start, length, fs, wanted / 1.15, wanted * 1.15);
        ++total;
        if (hz > 0.0 && std::abs(centsBetween(hz, wanted)) <= cents) ++close;
    }
    REQUIRE(total > 5);
    return static_cast<double>(close) / total;
}

// Levels in dB of the bands the whisper uses, measured with filters of its own.
std::vector<double> bandLevels(const std::vector<float>& x, double fs, int from) {
    std::vector<double> levels;
    for (int b = 0; b < 16; ++b) {
        wers::Svf filter;
        filter.set(200.0 * std::pow(60.0, b / 15.0), 3.0, fs);
        double energy = 0.0;
        for (size_t i = 0; i < x.size(); ++i) {
            const double v = filter.band(static_cast<double>(x[i]));
            if (static_cast<int>(i) >= from) energy += v * v;
        }
        levels.push_back(10.0 * std::log10(energy + 1e-30));
    }
    return levels;
}

} // namespace

TEST_CASE("sub follows the voice") {
    const double fs = 48000.0;
    SUBCASE("a steady voice") {
        const auto y = runSub(vowel(150.0, -18.0, static_cast<int>(fs), fs), fs, -12.0f);
        CHECK(shareOnPitch(y, fs, 5.0, [](double) { return 75.0; }) >= 0.9);
    }
    SUBCASE("a glide") {
        const auto y = runSub(glideVoice(110.0, 220.0, -18.0, static_cast<int>(fs * 2.0), fs), fs, -12.0f);
        const double share = shareOnPitch(y, fs, 5.0, [](double t) { return 0.5 * glideHz(110.0, 220.0, t, 2.0); });
        MESSAGE("glide: " << share * 100.0 << "% on pitch");
        CHECK(share >= 0.9);
    }
}

TEST_CASE("sub goes quiet on sibilants") {
    const double fs = 48000.0;
    const auto s = makeSyllables(fs, 6);
    const auto y = runSub(s.signal, fs, -12.0f);
    const int late = streamDelay(fs), skip = static_cast<int>(fs * 0.03);
    const double vowels = zoneEnergy(y, s.vowels, skip, late), sibilants = zoneEnergy(y, s.sibilants, skip, late);
    const double drop = 10.0 * std::log10((vowels + 1e-30) / (sibilants + 1e-30));
    MESSAGE("sibilants down " << drop << " dB");
    CHECK(drop >= 40.0);
}

TEST_CASE("sub has its second harmonic") {
    const double fs = 48000.0;
    auto y = runSub(vowel(200.0, -18.0, static_cast<int>(fs * 2.0), fs), fs, -12.0f);
    y.erase(y.begin(), y.begin() + static_cast<int>(fs * 0.5));
    const double difference = toneLevelDb(y, 200.0, fs) - toneLevelDb(y, 100.0, fs);
    MESSAGE("second harmonic " << difference << " dB");
    CHECK(std::abs(difference + 10.5) <= 1.0);
    CHECK(std::abs(rmsDb(y) + 18.0) <= 1.5);
}

TEST_CASE("whisper keeps the shape of the voice") {
    const double fs = 48000.0;
    const auto x = vowel(150.0, -18.0, static_cast<int>(fs * 2.0), fs);
    const auto y = runWhisper(x, fs);
    const int from = static_cast<int>(fs * 0.3);
    const auto a = bandLevels(x, fs, from), b = bandLevels(y, fs, from);
    double meanA = 0.0, meanB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        meanA += a[i] / static_cast<double>(a.size());
        meanB += b[i] / static_cast<double>(b.size());
    }
    double together = 0.0, ownA = 0.0, ownB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        together += (a[i] - meanA) * (b[i] - meanB);
        ownA += (a[i] - meanA) * (a[i] - meanA);
        ownB += (b[i] - meanB) * (b[i] - meanB);
    }
    const double correlation = together / std::sqrt(ownA * ownB + 1e-30);
    MESSAGE("correlation of the band levels " << correlation << ", level " << rmsDb(y, from) << " against " << rmsDb(x, from) << " dB");
    CHECK(correlation >= 0.9);
    CHECK(std::abs(rmsDb(y, from) - rmsDb(x, from)) <= 3.0);
}

TEST_CASE("whisper has no pitch") {
    const double fs = 48000.0;
    auto y = runWhisper(vowel(150.0, -18.0, static_cast<int>(fs * 2.0), fs), fs);
    y.erase(y.begin(), y.begin() + static_cast<int>(fs * 0.3));
    const double level = rmsDb(y);
    MESSAGE("level " << level << " dB, at 150 Hz " << toneLevelDb(y, 150.0, fs) << " dB, at 300 Hz " << toneLevelDb(y, 300.0, fs) << " dB");
    CHECK(toneLevelDb(y, 150.0, fs) <= level - 20.0);
    CHECK(toneLevelDb(y, 300.0, fs) <= level - 20.0);
}

TEST_CASE("whisper follows in time") {
    const double fs = 48000.0;
    WhisperVoice latency;
    latency.prepare(fs);
    // Vowels and silence in turn. The level of noise moves by itself, so the start is the average over many of them.
    const int part = static_cast<int>(fs * 0.3), count = 12;
    std::vector<float> x;
    const auto v = vowel(150.0, -18.0, part, fs);
    for (int i = 0; i < count; ++i) {
        append(x, v);
        x.resize(x.size() + static_cast<size_t>(part), 0.0f);
    }
    const auto y = runWhisper(x, fs);
    const int window = static_cast<int>(fs * 0.004);
    const auto levelAt = [&](const std::vector<float>& z, int start) { return rmsDb(z, start, start + window); };
    double started = 0.0, ended = 0.0;
    for (int i = 1; i < count; ++i) {
        const int from = i * 2 * part, to = from + part;
        const double voice = rmsDb(x, from + part / 2, to), stream = rmsDb(y, from + part / 2, to);
        int a = from - window, b = from - window;
        while (levelAt(x, a) < voice - 3.0) ++a;
        while (levelAt(y, b) < stream - 3.0) ++b;
        started += (b - a) / fs * 1000.0 / (count - 1);
        int c = to;
        while (levelAt(y, c) > stream - 20.0) ++c;
        ended += (c + window - to) / fs * 1000.0 / (count - 1);
    }
    const double latencyMs = latency.latencySamples() / fs * 1000.0;
    MESSAGE("starts " << started << " ms after the voice with " << latencyMs << " ms expected, down by 20 dB in " << ended << " ms");
    CHECK(std::abs(started - latencyMs) <= 3.0);
    CHECK(ended <= 100.0);
}

TEST_CASE("whisper is silent on silence") {
    const double fs = 48000.0;
    const int part = static_cast<int>(fs * 0.5);
    auto x = vowel(150.0, -18.0, part, fs);
    x.resize(static_cast<size_t>(part) * 2, 0.0f);
    const auto y = runWhisper(x, fs);
    const double sounding = rmsDb(y, part / 2, part);
    const double after = rmsDb(y, part + static_cast<int>(fs * 0.3));
    MESSAGE("sounding " << sounding << " dB, after 300 ms of silence " << after << " dB");
    CHECK(sounding - after >= 80.0);
}

TEST_CASE("sources never allocate") {
    const double fs = 48000.0;
    const auto x = loudPhrase(fs, 1.0, -3.0);
    VoiceAnalysis voice;
    voice.prepare(fs);
    SubVoice sub;
    sub.prepare(fs, streamDelay(fs));
    WhisperVoice whisper;
    whisper.prepare(fs);
    AllocationCounter counter;
    for (float v : x) {
        voice.push(v);
        sub.tick(voice, -12.0f);
        whisper.tick(v);
    }
    CHECK(counter.stop() == 0);
}

TEST_CASE("render writes every probe") {
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() / "refren_render_test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    const double rate = 48000.0;
    wers::wav::Audio audio;
    audio.rate = static_cast<int>(rate);
    audio.channels = 2;
    const auto voiceSignal = loudPhrase(rate, 3.0, -6.0);
    audio.ch = { voiceSignal, voiceSignal };
    const std::string name = "Żółć numer";
    const auto song = folder / fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()) + u8".wav");
    {
        // Written through the path object, so the name with Polish letters also works on Windows.
        const auto temporary = folder / "temporary.wav";
        REQUIRE(wers::wav::write(temporary.string(), audio));
        fs::rename(temporary, song);
    }
    const auto list = folder / "list.txt";
    {
        std::ofstream file(list, std::ios::binary);
        const auto bytes = song.u8string();
        file << "\xEF\xBB\xBF" << std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) << "\r\n";
        file << (folder / "nie ma takiego pliku.wav").string() << "\n";
    }
    const auto out = folder / "probe";
    REQUIRE(renderProbes(list.u8string(), out.u8string()) == 1);
    int files = 0;
    for (const auto& entry : fs::directory_iterator(out)) {
        ++files;
        const auto fileName = entry.path().filename().string();
        CAPTURE(fileName);
        CHECK(fileName.rfind("1_", 0) == 0);
        CHECK(fileName.find("numer") == std::string::npos);
        CHECK(fileName.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.") == std::string::npos);
        wers::wav::Audio written;
        std::vector<uint8_t> bytes;
        {
            std::ifstream file(entry.path(), std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        REQUIRE(wers::wav::readBytes(bytes, written));
        CHECK(written.rate == static_cast<int>(rate));
        REQUIRE(!written.ch.empty());
        CHECK(written.ch[0].size() == voiceSignal.size());
        CHECK(rmsDb(written.ch[0]) > -60.0);
    }
    CHECK(files == 16);
    fs::remove_all(folder);
}
