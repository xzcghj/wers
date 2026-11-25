#include "PowiewProcessor.h"
#include "ui/PowiewEditor.h"

namespace powiew {

namespace {
constexpr juce::uint32 kStateMagic = 0x504f5731; // "POW1"
constexpr int kHeaderBytes = 12;
} // namespace

PowiewProcessor::PowiewProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "POWIEW", createParameterLayout()),
      presets_(apvts_, juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("WERS").getChildFile("presets").getChildFile("powiew"),
               factoryPresets(), presetExcludedIds()),
      reader_(apvts_) {
    engine_.prepare(fs_);
    setLatencySamples(engine_.latency());
}

void PowiewProcessor::prepareToPlay(double sampleRate, int) {
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    engine_.prepare(fs_);
    // The detector looks ahead and the harmonics are oversampled: both cost a fixed delay the host makes up for.
    setLatencySamples(engine_.latency());
}

void PowiewProcessor::reset() { engine_.reset(); }

bool PowiewProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

juce::AudioProcessorParameter* PowiewProcessor::getBypassParameter() const { return apvts_.getParameter(ids::bypass); }

void PowiewProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, reader_.bypassed()); }

void PowiewProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, true); }

void PowiewProcessor::processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(buffer.getNumChannels(), getMainBusNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (channels < 1 || n == 0) return;
    for (int c = getMainBusNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    auto params = reader_.read();
    // The engine fades to the delayed dry signal itself, so bypass keeps the latency the host expects.
    params.bypass = bypassed;
    engine_.setParams(params);
    float* left = buffer.getWritePointer(0);
    // Mono runs as a stereo pair of the same signal and keeps the left.
    float* right = channels > 1 ? buffer.getWritePointer(1) : left;
    if (channels > 1) {
        engine_.process(left, right, n);
    } else {
        // In place on one channel: the engine reads each sample before it writes it, left then right,
        // so feeding the same buffer twice gives the left result.
        engine_.process(left, left, n);
    }
}

void PowiewProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(static_cast<int>(kStateMagic));
    out.writeInt(kStateVersion);
    auto tree = apvts_.copyState();
    tree.setProperty("uiScale", uiScale_.load(), nullptr);
    tree.appendChild(presets_.toTree(), nullptr);
    juce::MemoryOutputStream xml;
    if (auto e = tree.createXml()) e->writeTo(xml, juce::XmlElement::TextFormat().withoutHeader().singleLine());
    out.writeInt(static_cast<int>(xml.getDataSize()));
    out.write(xml.getData(), xml.getDataSize());
}

void PowiewProcessor::setStateInformation(const void* data, int size) {
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
    apvts_.replaceState(tree);
}

juce::AudioProcessorEditor* PowiewProcessor::createEditor() { return new PowiewEditor(*this); }

} // namespace powiew

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new powiew::PowiewProcessor(); }
