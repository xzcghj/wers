// Renders every factory preset on material that fits its group, and prints what each render measures.
// Usage: lampa_render <output folder> [input.wav]
#include "LampaAudio.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/FactoryPresets.h"
#include "plugin/LampaProcessor.h"

using namespace lampa;

namespace {

struct Stats {
    double rmsDb = -200.0, peakDb = -200.0;
};

Stats measure(const juce::AudioBuffer<float>& b) {
    double sum = 0.0;
    float peak = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const float v = b.getSample(c, i);
            sum += static_cast<double>(v) * v;
            peak = std::max(peak, std::abs(v));
        }
    return { 10.0 * std::log10(sum / (2.0 * b.getNumSamples()) + 1e-30), 20.0 * std::log10(peak + 1e-30) };
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) {
        std::printf("lampa_render <output folder> [input.wav]\n");
        return 1;
    }
    juce::File dir(juce::String::fromUTF8(argv[1]));
    dir.createDirectory();
    double fs = 48000.0;
    test::Stereo file;
    if (argc > 2) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(juce::String::fromUTF8(argv[2]))));
        if (reader == nullptr) {
            std::printf("cannot read %s\n", argv[2]);
            return 1;
        }
        fs = reader->sampleRate;
        const int n = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(fs * 60.0)));
        juce::AudioBuffer<float> b(2, n);
        reader->read(&b, 0, n, 0, true, true);
        file.l.assign(b.getReadPointer(0), b.getReadPointer(0) + n);
        file.r.assign(b.getReadPointer(reader->numChannels > 1 ? 1 : 0), b.getReadPointer(reader->numChannels > 1 ? 1 : 0) + n);
    }

    auto write = [&](const juce::File& f, const juce::AudioBuffer<float>& b) {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(f);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(fs).withNumChannels(2).withBitsPerSample(24));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    };
    auto toBuffer = [](const test::Stereo& s) {
        juce::AudioBuffer<float> b(2, static_cast<int>(s.l.size()));
        b.copyFrom(0, 0, s.l.data(), b.getNumSamples());
        b.copyFrom(1, 0, s.r.data(), b.getNumSamples());
        return b;
    };

    // The dry material of each kind, once.
    const char* kinds[] = { "Wokal", "Bas", "Bęben", "Szyna" };
    const char* kindNames[] = { "wokal", "bas", "bebny", "petla" };
    for (int k = 0; k < 4; ++k) {
        const auto dry = toBuffer(file.l.empty() ? test::materialFor(kinds[k], fs, 12.0) : file);
        write(dir.getChildFile(juce::String("00 bez efektu - ") + kindNames[k] + ".wav"), dry);
        if (!file.l.empty()) break;
    }

    int index = 1;
    for (const auto& preset : factoryPresets()) {
        const auto source = file.l.empty() ? test::materialFor(preset.group, fs, 12.0) : file;
        juce::AudioBuffer<float> dry = toBuffer(source);
        LampaProcessor p;
        p.setPlayConfigDetails(2, 2, fs, 512);
        p.prepareToPlay(fs, 512);
        const auto name = juce::String::fromUTF8(preset.name);
        p.presets().load(name);
        p.reset();
        // Run the latency off the end so the render lines up with the source.
        const int n = dry.getNumSamples(), d = p.getLatencySamples();
        juce::AudioBuffer<float> b(2, n + d);
        b.clear();
        b.copyFrom(0, 0, dry, 0, 0, n);
        b.copyFrom(1, 0, dry, 1, 0, n);
        juce::MidiBuffer midi;
        float maxGr = 0.0f;
        for (int at = 0; at < n + d; at += 512) {
            const int len = juce::jmin(512, n + d - at);
            juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, at, len);
            p.processBlock(view, midi);
            maxGr = std::max(maxGr, p.engine().meterReductionDb());
        }
        juce::AudioBuffer<float> out(2, n);
        out.copyFrom(0, 0, b, 0, d, n);
        out.copyFrom(1, 0, b, 1, d, n);
        write(dir.getChildFile(juce::String(index).paddedLeft('0', 2) + " " + name.replaceCharacter('/', '-') + ".wav"), out);
        const auto in = measure(dry), o = measure(out);
        std::printf("%-22s %-7s in %6.1f dB rms / %5.1f peak, out %6.1f / %5.1f, change %+5.1f dB, crest %4.1f -> %4.1f, max GR %4.1f dB\n",
                    name.toRawUTF8(), juce::String::fromUTF8(preset.group).toRawUTF8(), in.rmsDb, in.peakDb, o.rmsDb, o.peakDb, o.rmsDb - in.rmsDb,
                    in.peakDb - in.rmsDb, o.peakDb - o.rmsDb, maxGr);
        ++index;
    }
    return 0;
}
