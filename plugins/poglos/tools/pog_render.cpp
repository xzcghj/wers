// Renders every factory preset of Pogłos on demo material to 24-bit WAV files, and prints their levels.
// Usage: pog_render <output folder> [input.wav]
#include "TestAudio.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/FactoryPresets.h"
#include "plugin/PoglosProcessor.h"

using namespace pog;

namespace {

// The demo loop for four bars, then silence, so each render ends with the whole tail on its own.
// A dry vocal-like line on top: a few sung notes with breath, which shows a reverb best.
wir::test::Stereo demo(double fs) {
    auto music = wir::test::demoLoop(fs, 8.0);
    const size_t n = static_cast<size_t>(fs * 14.0);
    wir::test::Stereo out{ std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
    const double notes[] = { 440.0, 523.25, 587.33, 659.25, 587.33, 523.25, 440.0, 392.0 };
    for (size_t i = 0; i < music.l.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        const int k = static_cast<int>(t / 1.0) % 8;
        const double inNote = std::fmod(t, 1.0);
        const double env = inNote < 0.6 ? std::sin(juce::MathConstants<double>::pi * std::min(1.0, inNote / 0.6)) : 0.0;
        double v = 0.0;
        for (int h = 1; h <= 8; ++h) v += std::sin(2.0 * juce::MathConstants<double>::pi * notes[k] * h * t * (1.0 + 0.003 * std::sin(t * 30.0))) / (h * 1.3);
        const float voice = static_cast<float>(0.12 * env * v);
        out.l[i] = 0.7f * music.l[i] + voice;
        out.r[i] = 0.7f * music.r[i] + voice;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) {
        std::printf("pog_render <output folder> [input.wav]\n");
        return 1;
    }
    juce::File dir(juce::String::fromUTF8(argv[1]));
    dir.createDirectory();

    double fs = 48000.0;
    wir::test::Stereo in;
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
        in.l.assign(b.getReadPointer(0), b.getReadPointer(0) + n);
        in.r.assign(b.getReadPointer(reader->numChannels > 1 ? 1 : 0), b.getReadPointer(reader->numChannels > 1 ? 1 : 0) + n);
    } else {
        in = demo(fs);
    }

    auto write = [&](const juce::File& file, const juce::AudioBuffer<float>& b) {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(fs).withNumChannels(2).withBitsPerSample(24));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    };

    const int n = static_cast<int>(in.l.size());
    juce::AudioBuffer<float> dry(2, n);
    dry.copyFrom(0, 0, in.l.data(), n);
    dry.copyFrom(1, 0, in.r.data(), n);
    write(dir.getChildFile("00 bez efektu.wav"), dry);
    const double dryRms = std::sqrt(0.5 * (std::pow(dry.getRMSLevel(0, 0, n), 2.0) + std::pow(dry.getRMSLevel(1, 0, n), 2.0)));

    int index = 1;
    for (const auto& preset : factoryPresets()) {
        PoglosProcessor p;
        p.setPlayConfigDetails(2, 2, fs, 512);
        const auto name = juce::String::fromUTF8(preset.name);
        p.presets().load(name);
        p.prepareToPlay(fs, 512);
        juce::AudioBuffer<float> b(dry);
        juce::MidiBuffer midi;
        for (int at = 0; at < n; at += 512) {
            const int len = juce::jmin(512, n - at);
            juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, at, len);
            p.processBlock(view, midi);
        }
        const double r = std::sqrt(0.5 * (std::pow(b.getRMSLevel(0, 0, n), 2.0) + std::pow(b.getRMSLevel(1, 0, n), 2.0)));
        const float peak = juce::jmax(b.getMagnitude(0, 0, n), b.getMagnitude(1, 0, n));
        // The level of the tail alone, in the last two seconds, after four seconds of silence.
        const float tail = juce::jmax(b.getRMSLevel(0, n - static_cast<int>(fs * 2.0), static_cast<int>(fs * 2.0)), 1e-9f);
        write(dir.getChildFile(juce::String(index).paddedLeft('0', 2) + " " + name.replaceCharacter('/', '-').replaceCharacter(':', '-') + ".wav"), b);
        std::printf("%2d %-32s level %+5.1f dB  peak %5.2f  tail end %6.1f dBFS\n", index, name.toRawUTF8(), 20.0 * std::log10(r / dryRms), peak,
                    20.0 * std::log10(tail));
        ++index;
    }
    return 0;
}
