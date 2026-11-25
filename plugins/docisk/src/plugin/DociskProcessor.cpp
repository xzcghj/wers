#include "DociskProcessor.h"
#include "ui/DociskEditor.h"

namespace doc {

namespace {
constexpr juce::uint32 kStateMagic = 0x444f4331; // "DOC1"
constexpr int kHeaderBytes = 12;
constexpr double kBypassSeconds = 0.02;
constexpr int kChunk = 1024;
} // namespace

DociskProcessor::DociskProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "DOCISK", createParameterLayout()),
      presets_(apvts_, juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("WERS").getChildFile("presets").getChildFile("docisk"),
               factoryPresets(), presetExcludedIds()),
      reader_(apvts_) {
    dry_.setSize(2, kChunk);
    spare_.setSize(1, kChunk);
    keys_.setSize(2, kChunk);
    comp_.prepare(fs_);
}

void DociskProcessor::prepareToPlay(double sampleRate, int) {
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    comp_.prepare(fs_);
    comp_.setParams(reader_.read());
    comp_.reset();
    setLatencySamples(comp_.latencySamples());
    bypassStep_ = static_cast<float>(1.0 / (kBypassSeconds * fs_));
    bypassMix_ = reader_.bypassed() ? 1.0f : 0.0f;
}

void DociskProcessor::reset() { comp_.reset(); }

bool DociskProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (!(in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo()))) return false;
    // The sidechain may be off, mono or stereo.
    if (layouts.inputBuses.size() > 1) {
        const auto key = layouts.getChannelSet(true, 1);
        if (!key.isDisabled() && key != juce::AudioChannelSet::mono() && key != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

juce::AudioProcessorParameter* DociskProcessor::getBypassParameter() const { return apvts_.getParameter(ids::bypass); }

void DociskProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, reader_.bypassed()); }

void DociskProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processAudio(buffer, true); }

void DociskProcessor::processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    juce::ScopedNoDenormals noDenormals;
    const int ins = getMainBusNumInputChannels(), outs = getMainBusNumOutputChannels();
    const int total = buffer.getNumSamples();
    if (outs < 1 || buffer.getNumChannels() < outs || total == 0) return;

    // The sidechain's channels follow the main input's in the buffer, when the host gives them.
    const float* key[2] = { nullptr, nullptr };
    if (getBusCount(true) > 1) {
        const auto* bus = getBus(true, 1);
        const int count = bus != nullptr && bus->isEnabled() ? bus->getNumberOfChannels() : 0;
        const int first = count > 0 ? bus->getChannelIndexInProcessBlockBuffer(0) : -1;
        if (count > 0 && first >= 0 && first + count <= buffer.getNumChannels()) {
            key[0] = buffer.getReadPointer(first);
            key[1] = buffer.getReadPointer(first + (count > 1 ? 1 : 0));
        }
    }
    keyConnected_.store(key[0] != nullptr, std::memory_order_relaxed);

    const auto params = reader_.read();
    comp_.setParams(params);
    if (getLatencySamples() != comp_.latencySamples()) setLatencySamples(comp_.latencySamples());

    for (int from = 0; from < total; from += kChunk) {
        const int n = juce::jmin(kChunk, total - from);
        float* left = buffer.getWritePointer(0, from);
        // The key is copied first: with a mono input the second output channel may share the sidechain's place.
        const float* keyL = nullptr;
        const float* keyR = nullptr;
        if (key[0] != nullptr) {
            keys_.copyFrom(0, 0, key[0] + from, n);
            keys_.copyFrom(1, 0, key[1] + from, n);
            keyL = keys_.getReadPointer(0);
            keyR = keys_.getReadPointer(1);
        }
        // A mono input feeds both sides; a mono output works on a spare second channel.
        float* right = outs > 1 ? buffer.getWritePointer(1, from) : spare_.getWritePointer(0);
        if (ins == 1 || outs == 1) juce::FloatVectorOperations::copy(right, left, n);
        float* dl = dry_.getWritePointer(0);
        float* dr = dry_.getWritePointer(1);
        comp_.process(left, right, keyL, keyR, n, dl, dr);
        if (bypassed || bypassMix_ > 0.0f) {
            const float target = bypassed ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i) {
                bypassMix_ = target > bypassMix_ ? juce::jmin(target, bypassMix_ + bypassStep_) : juce::jmax(target, bypassMix_ - bypassStep_);
                left[i] += bypassMix_ * (dl[i] - left[i]);
                right[i] += bypassMix_ * (dr[i] - right[i]);
            }
        }
    }
}

void DociskProcessor::getStateInformation(juce::MemoryBlock& dest) {
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

void DociskProcessor::setStateInformation(const void* data, int size) {
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

juce::AudioProcessorEditor* DociskProcessor::createEditor() { return new DociskEditor(*this); }

} // namespace doc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new doc::DociskProcessor(); }
