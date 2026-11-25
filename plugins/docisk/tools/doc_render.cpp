// Renders a WAV file (or the built-in demo loop) through every factory preset, 24-bit.
// Sidechain presets get a kick on every beat (or, for the voice ducking, a sung line) on the sidechain input.
// Usage: doc_render <output folder> [input.wav]
#include "TestKeys.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/FactoryPresets.h"
#include "plugin/DociskProcessor.h"

using namespace doc;

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) {
        std::printf("doc_render <output folder> [input.wav]\n");
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
        // The demo loop with a louder and a quieter half, so the compression has dynamics to work on.
        in = wir::test::demoLoop(fs, 16.0);
        for (size_t i = 0; i < in.l.size(); ++i) {
            const float g = (i / static_cast<size_t>(fs * 4.0)) % 2 == 0 ? 1.0f : 0.35f;
            in.l[i] *= g;
            in.r[i] *= g;
        }
    }
    const int n = static_cast<int>(in.l.size());
    const auto kicks = doc::test::kickTrack(fs, static_cast<double>(n) / fs + 1.0);
    const auto voice = doc::test::voiceTrack(fs, static_cast<double>(n) / fs + 1.0);

    auto write = [&](const juce::File& file, const juce::AudioBuffer<float>& b) {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(fs).withNumChannels(2).withBitsPerSample(24));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    };

    juce::AudioBuffer<float> dry(2, n);
    dry.copyFrom(0, 0, in.l.data(), n);
    dry.copyFrom(1, 0, in.r.data(), n);
    write(dir.getChildFile("00 bez efektu.wav"), dry);

    int index = 1;
    for (const auto& preset : factoryPresets()) {
        DociskProcessor p;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(juce::AudioChannelSet::stereo());
        layout.inputBuses.add(juce::AudioChannelSet::stereo());
        layout.outputBuses.add(juce::AudioChannelSet::stereo());
        p.setBusesLayout(layout);
        p.prepareToPlay(fs, 512);
        const auto name = juce::String::fromUTF8(preset.name);
        p.presets().load(name);
        p.prepareToPlay(fs, 512); // start from the preset, not from a glide towards it
        const auto& key = name.contains(juce::String::fromUTF8("głosowi")) ? voice : kicks;
        // The output is read back after the latency, so every render lines up with the dry file.
        const int latency = p.getLatencySamples();
        juce::AudioBuffer<float> out(2, n);
        juce::MidiBuffer midi;
        for (int at = 0; at < n + latency; at += 512) {
            const int len = juce::jmin(512, n + latency - at);
            juce::AudioBuffer<float> b(4, len);
            b.clear();
            const int have = juce::jlimit(0, len, n - at);
            if (have > 0) {
                b.copyFrom(0, 0, in.l.data() + at, have);
                b.copyFrom(1, 0, in.r.data() + at, have);
                b.copyFrom(2, 0, key.l.data() + at, have);
                b.copyFrom(3, 0, key.r.data() + at, have);
            }
            p.processBlock(b, midi);
            for (int i = 0; i < len; ++i) {
                const int to = at + i - latency;
                if (to >= 0 && to < n) {
                    out.setSample(0, to, b.getSample(0, i));
                    out.setSample(1, to, b.getSample(1, i));
                }
            }
        }
        write(dir.getChildFile(juce::String(index).paddedLeft('0', 2) + " " + name.replace(": ", " - ").replaceCharacter('/', '-') + ".wav"), out);
        std::printf("%s\n", name.toRawUTF8());
        ++index;
    }
    return 0;
}
