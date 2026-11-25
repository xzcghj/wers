#include "PoglosProcessor.h"
#include "ui/PoglosEditor.h"

namespace pog {

namespace {
constexpr juce::uint32 kStateMagic = 0x504f4731; // "POG1"
constexpr int kHeaderBytes = 12;
constexpr double kBypassSeconds = 0.02;
constexpr int kChunk = 1024;
} // namespace

PoglosProcessor::PoglosProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "POGLOS", createParameterLayout()),
      presets_(apvts_, juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("WERS").getChildFile("presets").getChildFile("poglos"),
               factoryPresets(), presetExcludedIds()),
      reader_(apvts_) {
    dry_.setSize(2, kChunk);
}

void PoglosProcessor::prepareToPlay(double sampleRate, int) {
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    reverb_.setParams(reader_.read(bpm_.load()));
    reverb_.prepare(fs_);
    bypassStep_ = static_cast<float>(1.0 / (kBypassSeconds * fs_));
    bypassMix_ = reader_.bypassed() ? 1.0f : 0.0f;
}

void PoglosProcessor::reset() {
    reverb_.setParams(reader_.read(bpm_.load()));
    reverb_.reset();
}

bool PoglosProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Stereo out, from a stereo or a mono source: a reverb makes space out of a mono voice too.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

juce::AudioProcessorParameter* PoglosProcessor::getBypassParameter() const { return apvts_.getParameter(ids::bypass); }

void PoglosProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, reader_.bypassed()); }

void PoglosProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, true); }

void PoglosProcessor::processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    juce::ScopedNoDenormals noDenormals;
    const int ins = getMainBusNumInputChannels(), outs = getMainBusNumOutputChannels();
    const int total = buffer.getNumSamples();
    if (outs < 2 || buffer.getNumChannels() < 2 || total == 0) return;
    if (ins == 1) buffer.copyFrom(1, 0, buffer, 0, 0, total);

    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0) bpm_.store(*bpm);
    reverb_.setParams(reader_.read(bpm_.load()));

    for (int from = 0; from < total; from += kChunk) {
        const int n = juce::jmin(kChunk, total - from);
        float* left = buffer.getWritePointer(0, from);
        float* right = buffer.getWritePointer(1, from);
        const bool fading = bypassed || bypassMix_ > 0.0f;
        if (fading) {
            dry_.copyFrom(0, 0, left, n);
            dry_.copyFrom(1, 0, right, n);
        }
        reverb_.process(left, right, n);
        if (fading) {
            const float target = bypassed ? 1.0f : 0.0f;
            const float* dl = dry_.getReadPointer(0);
            const float* dr = dry_.getReadPointer(1);
            for (int i = 0; i < n; ++i) {
                bypassMix_ = target > bypassMix_ ? juce::jmin(target, bypassMix_ + bypassStep_) : juce::jmax(target, bypassMix_ - bypassStep_);
                left[i] += bypassMix_ * (dl[i] - left[i]);
                right[i] += bypassMix_ * (dr[i] - right[i]);
            }
        }
    }
}

void PoglosProcessor::getStateInformation(juce::MemoryBlock& dest) {
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

void PoglosProcessor::setStateInformation(const void* data, int size) {
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

juce::AudioProcessorEditor* PoglosProcessor::createEditor() { return new PoglosEditor(*this); }

} // namespace pog

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pog::PoglosProcessor(); }
