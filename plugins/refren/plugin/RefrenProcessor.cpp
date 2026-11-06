#include "RefrenProcessor.h"
#include "ui/RefrenEditor.h"

namespace refren {

namespace {

constexpr juce::uint32 kStateMagic = 0x4E465257u; // "WRFN" in the file
constexpr double kBypassSeconds = 0.01, kTailSeconds = 10.0;
constexpr int kHeaderBytes = 12;
constexpr const char* kFirstPreset = "Luźny";

template <typename Enum>
Enum choiceOf(const std::atomic<float>* value, int last) noexcept {
    return static_cast<Enum>(juce::jlimit(0, last, juce::roundToInt(value->load())));
}

float shareOf(const std::atomic<float>* value) noexcept { return value->load() * 0.01f; }

} // namespace

juce::File RefrenProcessor::defaultPresetFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("WERS").getChildFile("presets").getChildFile("refren");
}

RefrenProcessor::RefrenProcessor(juce::File presetFolder)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      presetFolder_(std::move(presetFolder)),
      apvts_(*this, nullptr, "REFREN", createParameterLayout()),
      presets_(apvts_, presetFolder_, factoryPresets(), presetExcludedIds()) {
    const auto value = [this](const juce::String& id) { return apvts_.getRawParameterValue(id); };
    pAmount_ = value(ids::amount);
    pWidth_ = value(ids::width);
    pTime_ = value(ids::time);
    pTilt_ = value(ids::tilt);
    pGrain_ = value(ids::grain);
    pGrainChar_ = value(ids::grainChar);
    pMotion_ = value(ids::motion);
    pMotionRate_ = value(ids::motionRate);
    pMotionDepth_ = value(ids::motionDepth);
    pEchoMix_ = value(ids::echoMix);
    pEchoTime_ = value(ids::echoTime);
    pEchoFeedback_ = value(ids::echoFeedback);
    pEchoPing_ = value(ids::echoPing);
    pEchoTone_ = value(ids::echoTone);
    pVerbMix_ = value(ids::verbMix);
    pVerbSize_ = value(ids::verbSize);
    pVerbDecay_ = value(ids::verbDecay);
    pVerbTone_ = value(ids::verbTone);
    pVerbPre_ = value(ids::verbPre);
    pVerbShimmer_ = value(ids::verbShimmer);
    pDuck_ = value(ids::duck);
    pSendDry_ = value(ids::sendDry);
    pConsonants_ = value(ids::consonants);
    pDry_ = value(ids::dry);
    pTrim_ = value(ids::trim);
    pOutput_ = value(ids::output);
    pDelta_ = value(ids::delta);
    pBypass_ = value(ids::bypass);
    for (int k = 0; k < kLayers; ++k) {
        auto& l = pLayer_[static_cast<size_t>(k)];
        l.on = value(layerId(k, ids::on));
        l.source = value(layerId(k, ids::source));
        l.voices = value(layerId(k, ids::voices));
        l.semis = value(layerId(k, ids::semis));
        l.formant = value(layerId(k, ids::formant));
        l.level = value(layerId(k, ids::level));
        l.width = value(layerId(k, ids::layerWidth));
        l.time = value(layerId(k, ids::layerTime));
        l.detune = value(layerId(k, ids::detune));
        l.low = value(layerId(k, ids::low));
        l.high = value(layerId(k, ids::high));
        l.note = value(layerId(k, ids::note));
    }
    presets_.load(juce::String::fromUTF8(kFirstPreset)); // a project replaces it right after
    dry_.setSize(2, kChunk);
    prepareAll();
    setLatencySamples(engine_.latencySamples());
}

void RefrenProcessor::prepareAll() {
    engine_.prepare(fs_);
    engine_.setParams(readParams());
    for (auto& d : dryDelay_) d.prepare(engine_.latencySamples());
    bypassStep_ = static_cast<float>(1.0 / (kBypassSeconds * fs_));
    bypassMix_ = pBypass_->load() > 0.5f ? 1.0f : 0.0f;
    hostBpm_ = 0.0;
}

void RefrenProcessor::prepareToPlay(double sampleRate, int) {
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    prepareAll();
    setLatencySamples(engine_.latencySamples());
}

void RefrenProcessor::reset() {
    // A VST3 host calls this on a thread of its own.
    const juce::ScopedLock lock(getCallbackLock());
    engine_.reset();
    for (auto& d : dryDelay_) d.reset();
}

bool RefrenProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out == juce::AudioChannelSet::stereo()) return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    return out == juce::AudioChannelSet::mono() && in == juce::AudioChannelSet::mono();
}

juce::AudioProcessorParameter* RefrenProcessor::getBypassParameter() const { return apvts_.getParameter(ids::bypass); }

double RefrenProcessor::getTailLengthSeconds() const { return static_cast<double>(getLatencySamples()) / fs_ + kTailSeconds; }

RefrenParams RefrenProcessor::readParams() const noexcept {
    RefrenParams p;
    p.amount = shareOf(pAmount_);
    p.widthScale = shareOf(pWidth_);
    p.timeScale = shareOf(pTime_);
    p.tilt = shareOf(pTilt_);
    p.grainDb = pGrain_->load();
    p.grainChar = choiceOf<Character>(pGrainChar_, 3);
    p.motion = choiceOf<Motion>(pMotion_, 3);
    p.motionRate = pMotionRate_->load();
    p.motionDepth = shareOf(pMotionDepth_);
    p.echoMix = shareOf(pEchoMix_);
    p.echoTime = choiceOf<EchoTime>(pEchoTime_, 6);
    p.echoFeedback = shareOf(pEchoFeedback_);
    p.echoPing = pEchoPing_->load() > 0.5f;
    p.echoToneHz = pEchoTone_->load();
    p.verbMix = shareOf(pVerbMix_);
    p.verbSize = shareOf(pVerbSize_);
    p.verbDecay = pVerbDecay_->load();
    p.verbToneHz = pVerbTone_->load();
    p.verbPreMs = pVerbPre_->load();
    p.verbShimmer = shareOf(pVerbShimmer_);
    p.duck = shareOf(pDuck_);
    p.sendDry = shareOf(pSendDry_);
    p.consonants = shareOf(pConsonants_);
    p.dryDb = pDry_->load();
    p.trimDb = pTrim_->load();
    p.outputDb = pOutput_->load();
    p.delta = pDelta_->load() > 0.5f;
    for (size_t k = 0; k < p.layer.size(); ++k) {
        const auto& from = pLayer_[k];
        auto& l = p.layer[k];
        l.on = from.on->load() > 0.5f;
        l.source = choiceOf<Source>(from.source, 3);
        l.voices = juce::roundToInt(from.voices->load());
        l.semis = juce::roundToInt(from.semis->load());
        l.formant = from.formant->load();
        l.levelDb = from.level->load();
        l.width = shareOf(from.width);
        l.timeMs = from.time->load();
        l.detuneCents = from.detune->load();
        l.lowHz = from.low->load();
        l.highHz = from.high->load();
        l.note = juce::roundToInt(from.note->load());
    }
    return p;
}

void RefrenProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    midi.clear();
    processAudio(buffer, pBypass_->load() > 0.5f);
}

void RefrenProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    midi.clear();
    processAudio(buffer, true);
}

void RefrenProcessor::processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int inputs = getTotalNumInputChannels(), outputs = getTotalNumOutputChannels();
    for (int c = inputs; c < outputs && c < buffer.getNumChannels(); ++c) {
        // A mono voice on a stereo track goes to both sides of the dry path.
        if (inputs == 1 && c == 1) buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
        else buffer.clear(c, 0, numSamples);
    }
    const int count = juce::jmin(2, getMainBusNumOutputChannels(), buffer.getNumChannels());
    if (count <= 0 || numSamples <= 0) return;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto bpm = position->getBpm()) hostBpm_ = *bpm;
    engine_.setTempo(hostBpm_);
    engine_.setParams(readParams());
    for (int done = 0; done < numSamples;) {
        const int n = juce::jmin(kChunk, numSamples - done);
        std::array<float*, 2> channels{};
        for (int c = 0; c < count; ++c) channels[static_cast<size_t>(c)] = buffer.getWritePointer(c, done);
        processChunk(channels.data(), count, n, bypassed);
        done += n;
    }
}

void RefrenProcessor::processChunk(float* const* channels, int count, int n, bool bypassed) noexcept {
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
}

void RefrenProcessor::getStateInformation(juce::MemoryBlock& dest) {
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

void RefrenProcessor::setStateInformation(const void* data, int size) {
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
    // What the file does not mention takes its default value.
    for (auto* param : getParameters()) {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(param);
        if (withId == nullptr || tree.getChildWithProperty("id", withId->paramID).isValid()) continue;
        juce::ValueTree child("PARAM");
        child.setProperty("id", withId->paramID, nullptr);
        child.setProperty("value", withId->convertFrom0to1(withId->getDefaultValue()), nullptr);
        tree.appendChild(child, nullptr);
    }
    uiScale_.store(juce::jlimit(0.5f, 2.0f, static_cast<float>(tree.getProperty("uiScale", 1.0f))));
    apvts_.replaceState(tree);
}

void RefrenProcessor::randomise(juce::Random& random) {
    const auto move = [&](const juce::String& id, float reach) {
        auto* parameter = apvts_.getParameter(id);
        const auto range = apvts_.getParameterRange(id);
        const float now = range.convertFrom0to1(parameter->getValue());
        const float next = juce::jlimit(range.start, range.end, now + reach * (random.nextFloat() * 2.0f - 1.0f));
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(range.convertTo0to1(next));
        parameter->endChangeGesture();
    };
    move(ids::width, 40.0f);
    move(ids::time, 40.0f);
    move(ids::tilt, 25.0f);
    move(ids::verbMix, 12.0f);
    move(ids::echoMix, 12.0f);
    for (int k = 0; k < kLayers; ++k) move(layerId(k, ids::detune), 6.0f);
    const auto name = presets_.currentName();
    if (name.isNotEmpty() && !name.endsWithChar('*')) presets_.setShownName(name + "*");
}

juce::AudioProcessorEditor* RefrenProcessor::createEditor() { return new RefrenEditor(*this); }

} // namespace refren

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new refren::RefrenProcessor(); }
