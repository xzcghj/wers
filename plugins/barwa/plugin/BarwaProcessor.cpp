#include "BarwaProcessor.h"
#include "ui/BarwaEditor.h"

namespace barwa {

namespace {

constexpr juce::uint32 kStateMagic = 0x57524257u; // "WBRW" in the file
constexpr double kBypassSeconds = 0.01;
constexpr int kHeaderBytes = 12;

// Quality in the parameter is 0, 1 or 2 for 2x, 4x or 8x.
int qualityToLog2(float value) noexcept { return juce::jlimit(1, 3, juce::roundToInt(value) + 1); }

} // namespace

BarwaProcessor::BarwaProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "BARWA", createParameterLayout()) {
    pBands_ = apvts_.getRawParameterValue(ids::bands);
    pXover1_ = apvts_.getRawParameterValue(ids::xover1);
    pXover2_ = apvts_.getRawParameterValue(ids::xover2);
    pMask_ = apvts_.getRawParameterValue(ids::mask);
    pTone_ = apvts_.getRawParameterValue(ids::tone);
    pMix_ = apvts_.getRawParameterValue(ids::mix);
    pAutoLevel_ = apvts_.getRawParameterValue(ids::autoLevel);
    pOutput_ = apvts_.getRawParameterValue(ids::output);
    pDelta_ = apvts_.getRawParameterValue(ids::delta);
    pQuality_ = apvts_.getRawParameterValue(ids::quality);
    pBypass_ = apvts_.getRawParameterValue(ids::bypass);
    pChar_ = { apvts_.getRawParameterValue(ids::char1), apvts_.getRawParameterValue(ids::char2), apvts_.getRawParameterValue(ids::char3) };
    pDrive_ = { apvts_.getRawParameterValue(ids::drive1), apvts_.getRawParameterValue(ids::drive2), apvts_.getRawParameterValue(ids::drive3) };
    pBandMix_ = { apvts_.getRawParameterValue(ids::bandMix1), apvts_.getRawParameterValue(ids::bandMix2), apvts_.getRawParameterValue(ids::bandMix3) };
    pSolo_ = { apvts_.getRawParameterValue(ids::solo1), apvts_.getRawParameterValue(ids::solo2), apvts_.getRawParameterValue(ids::solo3) };
    apvts_.addParameterListener(ids::mask, this);
    apvts_.addParameterListener(ids::quality, this);
    scopeIn_.assign(kScopeSize, 0.0f);
    scopeOut_.assign(kScopeSize, 0.0f);
    dry_.setSize(2, kChunk);
    prepareForQuality();
    setLatencySamples(engine_.latencySamples());
}

BarwaProcessor::~BarwaProcessor() {
    cancelPendingUpdate();
    apvts_.removeParameterListener(ids::mask, this);
    apvts_.removeParameterListener(ids::quality, this);
}

int BarwaProcessor::wantedQuality() const noexcept { return qualityToLog2(pQuality_->load()); }

void BarwaProcessor::prepareForQuality() {
    appliedQuality_ = wantedQuality();
    engine_.prepare(fs_, appliedQuality_);
    engine_.setParams(readParams());
    for (auto& d : dryDelay_) d.prepare(engine_.latencySamples());
    bypassStep_ = static_cast<float>(1.0 / (kBypassSeconds * fs_));
    bypassMix_ = pBypass_->load() > 0.5f ? 1.0f : 0.0f;
}

void BarwaProcessor::prepareToPlay(double sampleRate, int) {
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    prepareForQuality();
    setLatencySamples(engine_.latencySamples());
    scopeFifo_.reset();
}

void BarwaProcessor::reset() {
    engine_.reset();
    for (auto& d : dryDelay_) d.reset();
}

void BarwaProcessor::applyQuality() {
    if (wantedQuality() == appliedQuality_) return;
    suspendProcessing(true);
    {
        const juce::ScopedLock lock(getCallbackLock());
        prepareForQuality();
    }
    setLatencySamples(engine_.latencySamples());
    suspendProcessing(false);
}

void BarwaProcessor::handleAsyncUpdate() { applyQuality(); }

void BarwaProcessor::parameterChanged(const juce::String& id, float) {
    if (id == ids::mask) maskDecided_.store(true);
    else if (id == ids::quality) triggerAsyncUpdate();
}

bool BarwaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorParameter* BarwaProcessor::getBypassParameter() const { return apvts_.getParameter(ids::bypass); }

BarwaParams BarwaProcessor::readParams() const noexcept {
    BarwaParams p;
    p.bands = juce::roundToInt(pBands_->load()) + 1;
    p.f1 = pXover1_->load();
    p.f2 = pXover2_->load();
    for (size_t i = 0; i < 3; ++i) {
        p.band[i].character = static_cast<Character>(juce::jlimit(0, 3, juce::roundToInt(pChar_[i]->load())));
        p.band[i].driveDb = pDrive_[i]->load();
        p.band[i].mix = pBandMix_[i]->load() * 0.01f;
        p.band[i].solo = pSolo_[i]->load() > 0.5f;
    }
    p.mask = pMask_->load() * 0.01f;
    p.tone = pTone_->load() * 0.01f;
    p.mix = pMix_->load() * 0.01f;
    p.outputDb = pOutput_->load();
    p.autoLevel = pAutoLevel_->load() > 0.5f;
    p.delta = pDelta_->load() > 0.5f;
    return p;
}

void BarwaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    midi.clear();
    processAudio(buffer, pBypass_->load() > 0.5f);
}

void BarwaProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    midi.clear();
    processAudio(buffer, true);
}

void BarwaProcessor::processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear(c, 0, numSamples);
    const int count = juce::jmin(2, getMainBusNumOutputChannels(), buffer.getNumChannels());
    if (count <= 0 || numSamples <= 0) return;
    engine_.setParams(readParams());
    for (int done = 0; done < numSamples;) {
        const int n = juce::jmin(kChunk, numSamples - done);
        std::array<float*, 2> channels{};
        for (int c = 0; c < count; ++c) channels[static_cast<size_t>(c)] = buffer.getWritePointer(c, done);
        processChunk(channels.data(), count, n, bypassed);
        done += n;
    }
}

void BarwaProcessor::processChunk(float* const* channels, int count, int n, bool bypassed) noexcept {
    for (int c = 0; c < count; ++c) {
        auto* dry = dry_.getWritePointer(c);
        const auto* in = channels[c];
        auto& delay = dryDelay_[static_cast<size_t>(c)];
        for (int i = 0; i < n; ++i) dry[i] = delay.process(std::isfinite(in[i]) ? in[i] : 0.0f);
    }
    engine_.process(channels, count, n);

    if (bypassed || bypassMix_ > 0.0f) {
        const float target = bypassed ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i) {
            bypassMix_ = target > bypassMix_ ? juce::jmin(target, bypassMix_ + bypassStep_) : juce::jmax(target, bypassMix_ - bypassStep_);
            // In this form the far ends of the fade are exact.
            for (int c = 0; c < count; ++c) channels[c][i] = channels[c][i] * (1.0f - bypassMix_) + dry_.getSample(c, i) * bypassMix_;
        }
    }

    float peak = outputPeak_.load(std::memory_order_relaxed);
    for (int c = 0; c < count; ++c)
        for (int i = 0; i < n; ++i) peak = juce::jmax(peak, std::abs(channels[c][i]));
    outputPeak_.store(peak, std::memory_order_relaxed);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    scopeFifo_.prepareToWrite(n, start1, size1, start2, size2);
    if (size1 + size2 == n) {
        const auto* dryLeft = dry_.getReadPointer(0);
        const auto* dryRight = dry_.getReadPointer(count - 1);
        const auto* outLeft = channels[0];
        const auto* outRight = channels[count - 1];
        const auto store = [&](int start, int size, int offset) {
            for (int i = 0; i < size; ++i) {
                scopeIn_[static_cast<size_t>(start + i)] = 0.5f * (dryLeft[offset + i] + dryRight[offset + i]);
                scopeOut_[static_cast<size_t>(start + i)] = 0.5f * (outLeft[offset + i] + outRight[offset + i]);
            }
        };
        store(start1, size1, 0);
        store(start2, size2, size1);
        scopeFifo_.finishedWrite(n);
    }
}

bool BarwaProcessor::popScope(float* in, float* out, int n) noexcept {
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    if (scopeFifo_.getNumReady() < n) return false;
    scopeFifo_.prepareToRead(n, start1, size1, start2, size2);
    std::copy_n(scopeIn_.data() + start1, size1, in);
    std::copy_n(scopeOut_.data() + start1, size1, out);
    std::copy_n(scopeIn_.data() + start2, size2, in + size1);
    std::copy_n(scopeOut_.data() + start2, size2, out + size1);
    scopeFifo_.finishedRead(size1 + size2);
    return true;
}

void BarwaProcessor::updateTrackProperties(const TrackProperties& properties) {
    if (maskDecided_.load() || !properties.name.has_value()) return;
    // Only exact words count, so a track called "orbit" still gets the mask.
    juce::StringArray words;
    words.addTokens(properties.name->toLowerCase(), " _-./()[]", "");
    const juce::StringArray notAVoice{ "beat", "bit", "instrumental", "instr", "prod", "master", "mix" };
    for (const auto& word : words) {
        if (notAVoice.contains(word)) {
            auto* param = apvts_.getParameter(ids::mask);
            param->setValueNotifyingHost(param->convertTo0to1(0.0f));
            maskDecided_.store(true); // a later rename must not change it
            return;
        }
    }
}

void BarwaProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(static_cast<int>(kStateMagic));
    out.writeInt(kStateVersion);
    auto tree = apvts_.copyState();
    tree.setProperty("uiScale", uiScale_.load(), nullptr);
    juce::MemoryOutputStream xml;
    if (auto e = tree.createXml()) e->writeTo(xml, juce::XmlElement::TextFormat().withoutHeader().singleLine());
    out.writeInt(static_cast<int>(xml.getDataSize()));
    out.write(xml.getData(), xml.getDataSize());
}

void BarwaProcessor::setStateInformation(const void* data, int size) {
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
    // What the file does not mention takes its neutral value, so an old project sounds as it did.
    for (auto* param : getParameters()) {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(param);
        if (withId == nullptr || tree.getChildWithProperty("id", withId->paramID).isValid()) continue;
        juce::ValueTree child("PARAM");
        child.setProperty("id", withId->paramID, nullptr);
        child.setProperty("value", withId->convertFrom0to1(withId->getDefaultValue()), nullptr);
        tree.appendChild(child, nullptr);
    }
    maskDecided_.store(true); // a saved instance keeps its mask
    uiScale_.store(juce::jlimit(0.5f, 2.0f, static_cast<float>(tree.getProperty("uiScale", 1.0f))));
    apvts_.replaceState(tree);
    if (juce::MessageManager::existsAndIsCurrentThread()) {
        cancelPendingUpdate();
        applyQuality();
    }
}

juce::AudioProcessorEditor* BarwaProcessor::createEditor() { return new BarwaEditor(*this); }

} // namespace barwa

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new barwa::BarwaProcessor(); }
