// Renders the editor to PNG files in all roles and states, for reviewing the interface.
#include <juce_audio_formats/juce_audio_formats.h>
#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"
#include <algorithm>
#include <iterator>
using namespace wers;

namespace {
void shot(juce::AudioProcessorEditor& ed, const juce::File& file) {
    const auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    png.writeImageToStream(img, out);
}

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// Stands in for the speech model so the word view can be checked with Polish text on any machine.
class SampleWords final : public Recognizer {
public:
    std::vector<RecognizedWord> transcribe(const std::vector<float>&, const std::vector<Region>& regions, const RecognizerOptions&,
                                           const std::atomic<bool>&, const std::function<void(float)>&) override {
        static const char* words[] = { "księżyc", "szeptem", "czarny", "ulica", "światło", "krzyczę", "miasto", "ciężko", "żyję", "dalej", "przez", "noc", "wszystko", "źle", "chcę", "więcej", "gdzie", "jesteś", "dziś", "łapię" };
        std::vector<RecognizedWord> out;
        size_t next = 0;
        for (const auto& r : regions) {
            const double start = static_cast<double>(r.start) / 16000.0, end = start + static_cast<double>(r.length) / 16000.0;
            for (double t = start; t < end - 0.1; t += 0.34) {
                RecognizedWord w;
                w.text = juce::String::fromUTF8(words[next++ % std::size(words)]).toStdString();
                w.t0 = t; w.t1 = std::min(end, t + 0.32);
                out.push_back(std::move(w));
            }
        }
        return out;
    }
};

std::vector<float> take(const char* path, double seconds) {
    juce::File f(path);
    juce::MemoryBlock m;
    f.loadFileAsData(m);
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> r(fmt.createReaderFor(new juce::MemoryInputStream(m, true), true));
    if (!r) return {};
    const auto n = juce::jmin(static_cast<juce::int64>(seconds * r->sampleRate), r->lengthInSamples);
    juce::AudioBuffer<float> b(1, static_cast<int>(n));
    r->read(&b, 0, static_cast<int>(n), 0, true, false);
    return std::vector<float>(b.getReadPointer(0), b.getReadPointer(0) + n);
}
} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File dir(argc > 1 ? argv[1] : "/tmp/shots");
    dir.createDirectory();
    {
        WersProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto* e = dynamic_cast<WersEditor*>(ed.get());
        e->updateNow(); pump(100);
        shot(*ed, dir.getChildFile("1_idle.png"));

        p.startAnalysis();
        e->updateNow(); pump(100);
        shot(*ed, dir.getChildFile("2_listening.png"));
        p.session().cancelListening();

        std::vector<float> vocal, beat;
        if (argc > 3) {
            auto vs = std::make_shared<TimelineStore>(); auto bs = std::make_shared<TimelineStore>();
            vs->prepare(48000); bs->prepare(48000);
            vocal = take(argv[2], 110.0); beat = take(argv[3], 110.0);
            const size_t len = juce::jmin(vocal.size(), beat.size());
            SongSession& s = p.session();
            s.setStores(vs, nullptr);
            p.group()->addBeatStore(bs);
            p.group()->beatInstanceDelta(1);
            p.group()->publishPlaceDepth(0.5f);
            const auto analyse = [&](const juce::File& out) {
                s.startListening();
                for (size_t pos = 0; pos < len; pos += 512) {
                    const int n = static_cast<int>(juce::jmin<size_t>(512, len - pos));
                    vs->capture(vocal.data() + pos, n, static_cast<int64_t>(pos));
                    bs->capture(beat.data() + pos, n, static_cast<int64_t>(pos));
                }
                s.stopListeningAndAnalyse();
                s.waitIdle();
                pump(200); e->updateNow(); pump(300); e->updateNow();
                shot(*ed, out);
            };
            s.setRecognizerFactory(nullptr);
            analyse(dir.getChildFile("3_fragments.png"));
            s.setRecognizerFactory([]() -> std::unique_ptr<Recognizer> { return std::make_unique<SampleWords>(); });
            analyse(dir.getChildFile("3b_words.png"));
            s.fixAllBelow(kScoreDies, Device::Phone);
            s.waitIdle();
            pump(200); e->updateNow(); pump(300); e->updateNow();
            shot(*ed, dir.getChildFile("4_fixed.png"));
            s.fixAllBelow(kScoreFades, Device::Phone);
            s.waitIdle();
            pump(200); e->updateNow(); pump(300); e->updateNow();
            shot(*ed, dir.getChildFile("4b_all_fixed.png"));
            if (auto* fixes = p.state().getParameter(ids::fixes)) {
                fixes->setValueNotifyingHost(0.0f);
                pump(100); e->updateNow(); pump(100);
                shot(*ed, dir.getChildFile("4c_compare.png"));
                fixes->setValueNotifyingHost(1.0f);
                pump(100); e->updateNow();
            }
            p.group()->removeBeatStore(bs);
            p.group()->beatInstanceDelta(-1);
        }

        // Beat role: real audio through the live cut, reading a vocal from the sidechain.
        p.chooseRole(Role::Beat);
        pump(100); e->updateNow(); pump(100);
        shot(*ed, dir.getChildFile("5_beat_empty.png"));
        if (!vocal.empty()) {
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
            if (p.setBusesLayout(layout)) {
                p.prepareToPlay(48000.0, 1024);
                juce::AudioBuffer<float> block(4, 1024);
                juce::MidiBuffer midi;
                const size_t start = 48000 * 16;
                for (size_t pos = start; pos + 1024 <= start + 48000 * 3; pos += 1024) {
                    for (int i = 0; i < 1024; ++i) {
                        const float b = beat[pos + static_cast<size_t>(i)], v = vocal[pos + static_cast<size_t>(i)];
                        block.setSample(0, i, b); block.setSample(1, i, b); block.setSample(2, i, v); block.setSample(3, i, v);
                    }
                    p.processBlock(block, midi);
                }
                layout.inputBuses.getReference(1) = juce::AudioChannelSet::disabled();
                p.setBusesLayout(layout);
                p.prepareToPlay(48000.0, 1024);
            }
            e->updateNow(); pump(60); e->updateNow();
        }
        shot(*ed, dir.getChildFile("5_beat.png"));
        p.chooseRole(Role::Master);
        pump(100);
        if (!vocal.empty()) {
            // A whole song through the meter: vocal and beat added up, as on the master bus.
            const size_t len = juce::jmin(vocal.size(), beat.size());
            p.prepareToPlay(48000.0, 1024);
            juce::AudioBuffer<float> block(2, 1024);
            juce::MidiBuffer midi;
            for (size_t pos = 0; pos + 1024 <= len; pos += 1024) {
                for (int i = 0; i < 1024; ++i) {
                    const float v = vocal[pos + static_cast<size_t>(i)] + beat[pos + static_cast<size_t>(i)];
                    block.setSample(0, i, v); block.setSample(1, i, v);
                }
                p.processBlock(block, midi);
                if (pos % (1024 * 12) == 0) { e->updateNow(); }
            }
        }
        e->updateNow(); pump(100); e->updateNow();
        shot(*ed, dir.getChildFile("6_master.png"));
        ed.reset();
    }
    return 0;
}
