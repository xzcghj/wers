// Renders every factory preset onto fitting demo material (a synthetic voice with sibilants for the vocal and
// podcast presets, the demo loop for the rest), as 24-bit WAV, and prints numbers to listen by: level, peak,
// and how much the presence and air bands changed.
// Usage: pow_render <output folder> [input.wav]
#include "TestAudio.h"
#include "TestVoice.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/FactoryPresets.h"
#include "plugin/PowiewProcessor.h"

using namespace powiew;

namespace {

double bandRms(const juce::AudioBuffer<float>& b, double lo, double hi, double fs, int skip) {
    // Band level through a pair of biquads on the left channel.
    auto h = powiew::test::detail::Biquad::highpass(lo, 0.7071, fs);
    auto h2 = powiew::test::detail::Biquad::highpass(lo, 0.7071, fs);
    auto l = powiew::test::detail::Biquad::lowpass(hi, 0.7071, fs);
    double sum = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double y = l(h2(h(b.getSample(0, i))));
        if (i >= skip) sum += y * y;
    }
    return std::sqrt(sum / std::max(1, b.getNumSamples() - skip));
}

// The high band (4,5-16 kHz) gain of the output over the input, only inside vowels or only inside sibilants.
double segmentGainDb(const juce::AudioBuffer<float>& dry, const juce::AudioBuffer<float>& out, double fs, bool sibilants) {
    auto band = [&](const juce::AudioBuffer<float>& b, std::vector<double>& y) {
        auto h = powiew::test::detail::Biquad::highpass(4500.0, 0.7071, fs), h2 = powiew::test::detail::Biquad::highpass(4500.0, 0.7071, fs);
        auto l = powiew::test::detail::Biquad::lowpass(16000.0, 0.7071, fs);
        y.resize(static_cast<size_t>(b.getNumSamples()));
        for (int i = 0; i < b.getNumSamples(); ++i) y[static_cast<size_t>(i)] = l(h2(h(b.getSample(0, i))));
    };
    std::vector<double> a, c;
    band(dry, a);
    band(out, c);
    double ea = 0.0, ec = 0.0;
    const auto v = powiew::test::voiceLoop(fs, static_cast<double>(dry.getNumSamples()) / fs);
    for (const auto& s : v.segments) {
        if (s.sibilant != sibilants) continue;
        const auto from = static_cast<size_t>((s.start + 0.02) * fs), to = std::min(a.size(), static_cast<size_t>((s.end - 0.02) * fs));
        for (size_t i = from; i < to; ++i) {
            ea += a[i] * a[i];
            ec += c[i] * c[i];
        }
    }
    return 10.0 * std::log10(ec / ea);
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) {
        std::printf("pow_render <output folder> [input.wav]\n");
        return 1;
    }
    juce::File dir(juce::String::fromUTF8(argv[1]));
    dir.createDirectory();
    double fs = 48000.0;

    auto toBuffer = [](const std::vector<float>& l, const std::vector<float>& r) {
        juce::AudioBuffer<float> b(2, static_cast<int>(l.size()));
        b.copyFrom(0, 0, l.data(), b.getNumSamples());
        b.copyFrom(1, 0, r.data(), b.getNumSamples());
        return b;
    };
    juce::AudioBuffer<float> loop, voice;
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
        loop.setSize(2, n);
        reader->read(&loop, 0, n, 0, true, true);
        voice = loop;
    } else {
        const auto l = wir::test::demoLoop(fs, 12.0);
        const auto v = powiew::test::voiceLoop(fs, 12.8);
        loop = toBuffer(l.l, l.r);
        voice = toBuffer(v.l, v.r);
    }

    auto write = [&](const juce::File& file, const juce::AudioBuffer<float>& b) {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(fs).withNumChannels(2).withBitsPerSample(24));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    };

    write(dir.getChildFile(juce::String::fromUTF8("00 bez efektu (pętla).wav")), loop);
    write(dir.getChildFile(juce::String::fromUTF8("00 bez efektu (głos).wav")), voice);

    std::printf("%-34s %7s %7s %9s %9s %10s %10s\n", "preset", "dB", "peak", "5-9 kHz", ">10 kHz", "samogloski", "sybilanty");
    int index = 1;
    for (const auto& preset : factoryPresets()) {
        const auto group = juce::String::fromUTF8(preset.group);
        const bool onVoice = argc > 2 || group == "Wokal" || group == "Podcast" || group == "Start";
        const auto& dry = onVoice ? voice : loop;
        PowiewProcessor p;
        p.setPlayConfigDetails(2, 2, fs, 512);
        p.prepareToPlay(fs, 512);
        const auto name = juce::String::fromUTF8(preset.name);
        p.presets().load(name);
        p.reset();
        const int n = dry.getNumSamples();
        const int lat = p.getLatencySamples();
        // The input runs on by the latency, and the output is moved back, so the file lines up with the dry one.
        juce::AudioBuffer<float> b(2, n + lat);
        b.clear();
        b.copyFrom(0, 0, dry, 0, 0, n);
        b.copyFrom(1, 0, dry, 1, 0, n);
        juce::MidiBuffer midi;
        for (int at = 0; at < n + lat; at += 512) {
            const int len = juce::jmin(512, n + lat - at);
            juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, at, len);
            p.processBlock(view, midi);
        }
        juce::AudioBuffer<float> out(2, n);
        out.copyFrom(0, 0, b, 0, lat, n);
        out.copyFrom(1, 0, b, 1, lat, n);
        write(dir.getChildFile(juce::String(index).paddedLeft('0', 2) + " " + name.replaceCharacter(':', '-').replaceCharacter('/', '-') + ".wav"), out);
        const int skip = static_cast<int>(fs * 0.2);
        const double rmsIn = bandRms(dry, 20.0, 20000.0, fs, skip), rmsOut = bandRms(out, 20.0, 20000.0, fs, skip);
        const double pIn = bandRms(dry, 5000.0, 9000.0, fs, skip), pOut = bandRms(out, 5000.0, 9000.0, fs, skip);
        const double aIn = bandRms(dry, 10000.0, 20000.0, fs, skip), aOut = bandRms(out, 10000.0, 20000.0, fs, skip);
        juce::String split = juce::String::fromUTF8("(pętla)");
        if (onVoice && argc <= 2)
            split = juce::String::formatted("%+10.2f %+10.2f", segmentGainDb(dry, out, fs, false), segmentGainDb(dry, out, fs, true));
        std::printf("%-34s %+7.2f %7.3f %+9.2f %+9.2f %s\n", name.toRawUTF8(), 20 * std::log10(rmsOut / rmsIn), out.getMagnitude(0, n),
                    20 * std::log10(pOut / pIn), 20 * std::log10(aOut / aIn), split.toRawUTF8());
        ++index;
    }
    return 0;
}
