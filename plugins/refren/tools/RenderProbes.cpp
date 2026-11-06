#include "RenderProbes.h"
#include "ListedAudio.h"
#include "Psola.h"
#include "SubVoice.h"
#include "WhisperVoice.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>

namespace refren {

namespace {

constexpr double kSampleSeconds = 20.0, kWithVoiceGain = 0.5012; // -6 dB
constexpr double kLatencySeconds = 0.030;
constexpr int kOversamplingLatency = 49, kShortestRead = 2;

struct Probe {
    const char* name;
    std::function<float(const VoiceAnalysis&, float)> next;
    int delay;
};

// The loudest stretch of the song, so the sample has as much voice in it as possible.
std::pair<size_t, size_t> loudestStretch(const std::vector<float>& mono, double fs) {
    const auto wanted = static_cast<size_t>(kSampleSeconds * fs);
    if (mono.size() <= wanted) return { 0, mono.size() };
    const auto second = static_cast<size_t>(fs);
    std::vector<double> energy(mono.size() / second + 1, 0.0);
    for (size_t i = 0; i < mono.size(); ++i) energy[i / second] += static_cast<double>(mono[i]) * mono[i];
    const size_t seconds = wanted / second;
    double sum = 0.0, best = -1.0;
    size_t bestStart = 0;
    for (size_t s = 0; s < energy.size(); ++s) {
        sum += energy[s];
        if (s >= seconds) sum -= energy[s - seconds];
        if (s + 1 >= seconds && sum > best) {
            best = sum;
            bestStart = s + 1 - seconds;
        }
    }
    return { bestStart * second, std::min(mono.size(), bestStart * second + wanted) };
}

bool writeWav(const std::filesystem::path& path, const std::vector<float>& samples, int rate) {
    const auto bytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    const auto put32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    file.write("RIFF", 4);
    put32(36 + bytes);
    file.write("WAVEfmt ", 8);
    put32(16);
    put16(3); // floating point
    put16(1);
    put32(static_cast<uint32_t>(rate));
    put32(static_cast<uint32_t>(rate) * 4u);
    put16(4);
    put16(32);
    file.write("data", 4);
    put32(bytes);
    file.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(bytes));
    return static_cast<bool>(file);
}

void renderOne(int index, const std::vector<float>& mono, int rate, const std::filesystem::path& folder) {
    const double fs = rate;
    const auto [from, to] = loudestStretch(mono, fs);
    const int streamDelay = static_cast<int>(std::lround(kLatencySeconds * fs)) - kOversamplingLatency - kShortestRead;

    const auto shifted = [&](float semitones, float formant, bool fixedNote) {
        auto psola = std::make_shared<Psola>();
        psola->prepare(fs, streamDelay);
        PsolaSettings settings;
        settings.semitones = semitones;
        settings.formantSemitones = formant;
        settings.fixedNote = fixedNote;
        psola->set(settings);
        return [psola](const VoiceAnalysis& voice, float) { return psola->tick(voice); };
    };
    auto sub = std::make_shared<SubVoice>();
    sub->prepare(fs, streamDelay);
    auto whisper = std::make_shared<WhisperVoice>();
    whisper->prepare(fs);
    const int whisperDelay = whisper->latencySamples();
    const std::vector<Probe> probes{
        { "oktawa_dol", shifted(-12.0f, 0.0f, false), streamDelay },
        { "oktawa_gora", shifted(12.0f, 0.0f, false), streamDelay },
        { "kwinta", shifted(7.0f, 0.0f, false), streamDelay },
        { "barwa_ciemna", shifted(0.0f, -4.0f, false), streamDelay },
        { "barwa_jasna", shifted(0.0f, 4.0f, false), streamDelay },
        { "sub", [sub](const VoiceAnalysis& voice, float) { return sub->tick(voice, -12.0f); }, streamDelay },
        { "robot", shifted(0.0f, 0.0f, true), streamDelay },
        { "szept", [whisper](const VoiceAnalysis&, float x) { return whisper->tick(x); }, whisperDelay },
    };

    VoiceAnalysis voice;
    voice.prepare(fs);
    const size_t length = to - from;
    for (const auto& probe : probes) {
        voice.reset();
        std::vector<float> alone(length), together(length);
        for (size_t i = 0; i < length; ++i) {
            const float x = mono[from + i];
            voice.push(x);
            const float y = probe.next(voice, x);
            alone[i] = y;
            const float heard = i >= static_cast<size_t>(probe.delay) ? mono[from + i - static_cast<size_t>(probe.delay)] : 0.0f;
            together[i] = heard + static_cast<float>(kWithVoiceGain) * y;
        }
        const std::string start = std::to_string(index) + "_" + probe.name;
        writeWav(folder / (start + "_sam.wav"), alone, rate);
        writeWav(folder / (start + "_z_glosem.wav"), together, rate);
    }
}

} // namespace

int renderProbes(const std::u8string& listPath, const std::u8string& outFolder) {
    namespace fs = std::filesystem;
    const fs::path folder(outFolder);
    std::error_code error;
    fs::create_directories(folder, error);
    return wers::test::forEachListedFile(listPath, [&](int index, const std::vector<float>* mono, int rate) {
        if (mono != nullptr) renderOne(index, *mono, rate, folder);
    });
}

} // namespace refren
