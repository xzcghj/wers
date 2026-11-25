#include "LampaProcessor.h"
#include "ui/LampaEditor.h"

namespace lampa {

namespace {
constexpr juce::uint32 kStateMagic = 0x4c414d31; // "LAM1"
constexpr int kHeaderBytes = 12;
} // namespace

LampaProcessor::LampaProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "LAMPA", createParameterLayout()),
      presets_(apvts_, juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("WERS").getChildFile("presets").getChildFile("lampa"),
               factoryPresets(), presetExcludedIds()),
      reader_(apvts_) {
    engine_.setParams(reader_.read());
    setLatencySamples(engine_.latency());
}

void LampaProcessor::prepareToPlay(double sampleRate, int) {
    engine_.prepare(sampleRate > 0.0 ? sampleRate : 48000.0);
    engine_.setParams(reader_.read());
    setLatencySamples(engine_.latency());
}

void LampaProcessor::reset() { engine_.reset(); }

bool LampaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Mono or stereo, the same in and out.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

juce::AudioProcessorParameter* LampaProcessor::getBypassParameter() const { return apvts_.getParameter(ids::bypass); }

void LampaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, reader_.bypassed()); }

void LampaProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, true); }

void LampaProcessor::processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(buffer.getNumChannels(), getMainBusNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (channels < 1 || n == 0) return;
    for (int c = channels; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    auto p = reader_.read();
    p.bypass = bypassed;
    engine_.setParams(p);
    float* left = buffer.getWritePointer(0);
    if (channels >= 2) {
        engine_.process(left, buffer.getWritePointer(1), n);
        return;
    }
    // Mono: the engine works in stereo, so the right side is a scratch copy of the left in small pieces.
    float scratch[256];
    for (int from = 0; from < n; from += 256) {
        const int len = juce::jmin(256, n - from);
        std::copy(left + from, left + from + len, scratch);
        engine_.process(left + from, scratch, len);
    }
}

void LampaProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(static_cast<int>(kStateMagic));
    out.writeInt(kStateVersion);
    auto tree = apvts_.copyState();
    tree.setProperty("uiScale", uiScale_.load(), nullptr);
    tree.setProperty("meterMode", meterMode_.load(), nullptr);
    tree.appendChild(presets_.toTree(), nullptr);
    juce::MemoryOutputStream xml;
    if (auto e = tree.createXml()) e->writeTo(xml, juce::XmlElement::TextFormat().withoutHeader().singleLine());
    out.writeInt(static_cast<int>(xml.getDataSize()));
    out.write(xml.getData(), xml.getDataSize());
}

void LampaProcessor::setStateInformation(const void* data, int size) {
    if (data == nullptr || size < kHeaderBytes) return;
    juce::MemoryInputStream in(data, static_cast<size_t>(size), false);
    if (static_cast<juce::uint32>(in.readInt()) != kStateMagic) return;
    if (in.readInt() < 1) return;
    const int xmlSize = in.readInt();
    if (xmlSize <= 0 || xmlSize > in.getNumBytesRemaining()) return;
    juce::MemoryBlock xmlData;
    in.readIntoMemoryBlock(xmlData, xmlSize);
    const auto e = juce::parseXML(xmlData.toString());
    if (e == nullptr || !e->hasTagName(apvts_.state.getType().toString())) return;
    auto tree = juce::ValueTree::fromXml(*e);
    if (!tree.isValid()) return;
    const auto presetTree = tree.getChildWithName(wers::ui::PresetStore::treeType());
    if (presetTree.isValid()) {
        presets_.fromTree(presetTree);
        tree.removeChild(presetTree, nullptr);
    }
    // What the file does not mention takes its default, so older projects keep loading.
    for (auto* param : getParameters()) {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(param);
        if (withId == nullptr || tree.getChildWithProperty("id", withId->paramID).isValid()) continue;
        juce::ValueTree child("PARAM");
        child.setProperty("id", withId->paramID, nullptr);
        child.setProperty("value", withId->convertFrom0to1(withId->getDefaultValue()), nullptr);
        tree.appendChild(child, nullptr);
    }
    uiScale_.store(juce::jlimit(0.6f, 2.0f, static_cast<float>(tree.getProperty("uiScale", 1.0f))));
    meterMode_.store(juce::jlimit(0, 1, static_cast<int>(tree.getProperty("meterMode", 0))));
    apvts_.replaceState(tree);
}

juce::AudioProcessorEditor* LampaProcessor::createEditor() { return new LampaEditor(*this); }

} // namespace lampa

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new lampa::LampaProcessor(); }
