#include "PluginProcessor.h"
#include "ui/PluginEditor.h"

namespace wers {

namespace {
constexpr juce::uint32 kStateMagic = 0x31535257u; // "WRS1"
}

WersProcessor::WersProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Wejście", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Wyjście", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts_(*this, nullptr, "WERS", createParameterLayout()) {
    pDensity_ = apvts_.getRawParameterValue(ids::density);
    pClarity_ = apvts_.getRawParameterValue(ids::clarity);
    pAttack_ = apvts_.getRawParameterValue(ids::attack);
    pBreath_ = apvts_.getRawParameterValue(ids::breath);
    pPlace_ = apvts_.getRawParameterValue(ids::place);
    pSpace_ = apvts_.getRawParameterValue(ids::space);
    pCharX_ = apvts_.getRawParameterValue(ids::charX);
    pCharY_ = apvts_.getRawParameterValue(ids::charY);
    pFixes_ = apvts_.getRawParameterValue(ids::fixes);
    pMonDevice_ = apvts_.getRawParameterValue(ids::monDevice);
    pMonNorm_ = apvts_.getRawParameterValue(ids::monNorm);
    pBypass_ = apvts_.getRawParameterValue(ids::bypass);

    hub_ = LinkHub::get();
    vocalStore_ = std::make_shared<TimelineStore>();
    sideStore_ = std::make_shared<TimelineStore>();
    beatStore_ = std::make_shared<TimelineStore>();
    session_.setStores(vocalStore_, sideStore_);
    session_.setRecognizerFactory([this]() -> std::unique_ptr<Recognizer> {
        if (model_->state() != ModelManager::State::Ready || !canRecognise()) return nullptr;
        return std::make_unique<ProcessRecognizer>(ProcessRecognizer::bundledHelper(), ModelManager::modelFile().getFullPathName());
    });
    // Sensible rates until the host prepares us, so nothing is ever uninitialised.
    prepareToPlay(48000.0, kChunk);

    apvts_.addParameterListener(ids::role, this);
    apvts_.addParameterListener(ids::group, this);
    applyRoleAndGroup();
    startTimerHz(10);
}

WersProcessor::~WersProcessor() {
    stopTimer();
    apvts_.removeParameterListener(ids::role, this);
    apvts_.removeParameterListener(ids::group, this);
    session_.attachLink(nullptr, -1);
    rtGroup_.store(nullptr);
    rtControl_.store(nullptr);
    if (group_ != nullptr) {
        if (slot_ >= 0) group_->releaseSlot(slot_);
        if (beatStoreRegistered_) { group_->removeBeatStore(beatStore_); group_->beatInstanceDelta(-1); }
    }
}

bool WersProcessor::canRecognise() const {
    return recognitionSupported() && ProcessRecognizer::bundledHelper().existsAsFile();
}

void WersProcessor::parameterChanged(const juce::String&, float) {
    roleDirty_.store(true); // applied on the message thread by the timer
}

void WersProcessor::applyRoleAndGroup() {
    const int wantRole = juce::jlimit(0, 2, static_cast<int>(apvts_.getRawParameterValue(ids::role)->load()));
    const int wantGroup = juce::jlimit(0, kLinkGroups - 1, static_cast<int>(apvts_.getRawParameterValue(ids::group)->load()));
    const bool roleChanged = wantRole != activeRole_.load();
    const void* project = reaper_.project();
    const bool groupChanged = wantGroup != appliedGroup_ || project != appliedProject_;
    if (!roleChanged && !groupChanged) return;

    // Audio thread stops using the old connections first.
    rtSlot_.store(-1);
    rtControl_.store(nullptr);
    if (group_ != nullptr) {
        if (slot_ >= 0) {
            session_.attachLink(nullptr, -1);
            group_->releaseSlot(slot_);
            slot_ = -1;
        }
        if (beatStoreRegistered_) {
            group_->removeBeatStore(beatStore_);
            group_->beatInstanceDelta(-1);
            beatStoreRegistered_ = false;
        }
    }
    group_ = &hub_->group(wantGroup, project);
    appliedGroup_ = wantGroup;
    appliedProject_ = project;
    control_.setGroup(group_);
    rtGroup_.store(group_);

    if (wantRole == static_cast<int>(Role::Vocal)) {
        slot_ = group_->claimSlot();
        session_.attachLink(group_, slot_);
        rtSlot_.store(slot_);
    } else if (wantRole == static_cast<int>(Role::Beat)) {
        group_->addBeatStore(beatStore_);
        group_->beatInstanceDelta(1);
        beatStoreRegistered_ = true;
        rtControl_.store(&control_);
        group_->publishPlaceDepth(pPlace_->load() * 0.01f);
    }
    activeRole_.store(wantRole);
    roleForAudio_.store(wantRole);
    needsReset_.store(true);
    setLatencySamples(wantRole == static_cast<int>(Role::Vocal) ? fonem_.latencySamples() : 0);
}

void WersProcessor::prepareToPlay(double sampleRate, int) {
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    fonem_.prepare(fs_);
    vocalFix_.prepare(fs_);
    space_.prepare(fs_);
    place_.prepare(fs_);
    monitor_.prepare(fs_);
    liveEnv_.prepare(fs_);
    staleDetector_.prepare(fs_);
    ringHop_ = juce::jmax(16, juce::roundToInt(fs_ * BandRing::kHop));
    ringCount_ = 0;
    vocalStore_->prepare(fs_);
    sideStore_->prepare(fs_);
    beatStore_->prepare(fs_);
    dryScratch_.setSize(2, kChunk, false, true, true);
    for (auto& d : bypassDelay_) d.prepare(fonem_.latencySamples());
    setLatencySamples(role() == Role::Vocal ? fonem_.latencySamples() : 0);
    bypassMix_ = pBypass_->load() > 0.5f ? 1.0f : 0.0f;
    bypassStep_ = static_cast<float>(1.0 / (0.01 * fs_));
    needsReset_.store(false);
}

void WersProcessor::releaseResources() {}

bool WersProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())) return false;
    if (layouts.inputBuses.size() > 1) {
        const auto sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

juce::AudioProcessorParameter* WersProcessor::getBypassParameter() const { return apvts_.getParameter(ids::bypass); }

void WersProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    midi.clear();
    juce::ScopedNoDenormals noDenormals;
    ScopedFlushDenormals flush;

    const int totalIn = getTotalNumInputChannels(), totalOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const int mainIn = getMainBusNumInputChannels(), mainOut = getMainBusNumOutputChannels();
    for (int c = totalIn; c < totalOut; ++c) buffer.clear(c, 0, numSamples);
    const int nc = juce::jmin(2, mainOut, buffer.getNumChannels(), mainIn > 0 ? mainIn : mainOut);
    if (nc <= 0 || numSamples <= 0) return;

    if (needsReset_.exchange(false)) {
        fonem_.reset(); vocalFix_.reset(); space_.reset(); place_.reset(); monitor_.reset();
        liveEnv_.reset(); staleDetector_.reset();
        for (auto& d : bypassDelay_) d.reset();
    }

    int64_t timeline = -1;
    bool playing = false;
    double bpm = 0.0;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto t = pos->getTimeInSamples()) timeline = *t;
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
        }
    }
    playing_.store(playing);
    if (timeline >= 0) seconds_.store(static_cast<double>(timeline) / fs_);
    lastBlockMs_.store(juce::Time::getMillisecondCounter());

    int nsc = 0;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()) nsc = juce::jmin(2, getBus(true, 1)->getNumberOfChannels());
    const int scFirst = mainIn;
    if (scFirst + nsc > buffer.getNumChannels()) nsc = 0;

    const bool bypassed = pBypass_->load() > 0.5f;
    int done = 0;
    while (done < numSamples) {
        const int n = juce::jmin(kChunk, numSamples - done);
        std::array<float*, 2> ch{};
        std::array<const float*, 2> sc{};
        for (int c = 0; c < nc; ++c) ch[static_cast<size_t>(c)] = buffer.getWritePointer(c, done);
        for (int c = 0; c < nsc; ++c) sc[static_cast<size_t>(c)] = buffer.getReadPointer(scFirst + c, done);
        processChunk(ch.data(), nc, sc.data(), nsc, n, timeline >= 0 ? timeline + done : -1, playing, bpm, bypassed);
        done += n;
    }
    // Clear any further output channels so sidechain audio cannot leak out.
    for (int c = nc; c < mainOut && c < buffer.getNumChannels(); ++c) buffer.copyFrom(c, 0, buffer, 0, 0, numSamples);

    // Values for the interface.
    const Role r = static_cast<Role>(roleForAudio_.load());
    if (r == Role::Beat) {
        std::array<float, kCutBands> cuts{};
        place_.currentCutsDb(cuts.data());
        for (size_t i = 0; i < kCutBands; ++i) shownCuts_[i].store(cuts[i], std::memory_order_relaxed);
    } else if (r == Role::Vocal) {
        const auto& m = fonem_.memberships();
        shownVowel_.store(m.vowel, std::memory_order_relaxed); shownSib_.store(m.sib, std::memory_order_relaxed);
        shownStop_.store(m.stop, std::memory_order_relaxed); shownBreath_.store(m.breath, std::memory_order_relaxed);
        shownGr_.store(fonem_.gainReductionDb(), std::memory_order_relaxed);
        shownDeEss_.store(fonem_.deEssDb(), std::memory_order_relaxed);
    } else {
        const auto& lm = monitor_.meter();
        shownM_.store(lm.momentaryLufs(), std::memory_order_relaxed); shownS_.store(lm.shortTermLufs(), std::memory_order_relaxed);
        shownI_.store(lm.integratedLufs(), std::memory_order_relaxed); shownTp_.store(lm.truePeakDb(), std::memory_order_relaxed);
        shownSec_.store(lm.seconds(), std::memory_order_relaxed); shownGain_.store(monitor_.currentGainDb(), std::memory_order_relaxed);
    }
}

void WersProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    midi.clear();
    const int nc = juce::jmin(2, getMainBusNumOutputChannels(), buffer.getNumChannels());
    if (static_cast<Role>(roleForAudio_.load()) == Role::Vocal) {
        for (int c = 0; c < nc; ++c) {
            auto* d = buffer.getWritePointer(c);
            for (int i = 0; i < buffer.getNumSamples(); ++i) d[i] = bypassDelay_[static_cast<size_t>(c)].process(d[i]);
        }
    }
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
}

void WersProcessor::processChunk(float* const* ch, int nc, const float* const* sc, int nsc, int n, int64_t timeline, bool playing, double bpm, bool bypassed) {
    const Role r = static_cast<Role>(roleForAudio_.load());

    // Delayed dry signal: the bypass path and the crossfade partner.
    std::array<float*, 2> dry{ dryScratch_.getWritePointer(0), dryScratch_.getWritePointer(1) };
    if (r == Role::Vocal) {
        for (int c = 0; c < nc; ++c)
            for (int i = 0; i < n; ++i) dry[static_cast<size_t>(c)][i] = bypassDelay_[static_cast<size_t>(c)].process(ch[c][i]);
    } else {
        for (int c = 0; c < nc; ++c) juce::FloatVectorOperations::copy(dry[static_cast<size_t>(c)], ch[c], n);
    }

    const float target = bypassed ? 1.0f : 0.0f;
    if (bypassMix_ == 1.0f && target == 1.0f) {
        for (int c = 0; c < nc; ++c) juce::FloatVectorOperations::copy(ch[c], dry[static_cast<size_t>(c)], n);
        return;
    }
    processWet(ch, nc, sc, nsc, n, timeline, playing, bpm, r);
    if (bypassMix_ != target || bypassMix_ != 0.0f) {
        for (int i = 0; i < n; ++i) {
            bypassMix_ = target > bypassMix_ ? juce::jmin(target, bypassMix_ + bypassStep_) : juce::jmax(target, bypassMix_ - bypassStep_);
            for (int c = 0; c < nc; ++c) ch[c][i] += (dry[static_cast<size_t>(c)][i] - ch[c][i]) * bypassMix_;
        }
    }
}

void WersProcessor::processWet(float* const* ch, int nc, const float* const* sc, int nsc, int n, int64_t timeline, bool playing, double bpm, Role r) {
    LinkGroup* group = rtGroup_.load();
    const int64_t tl = playing ? timeline : -1;
    if (r == Role::Vocal) {
        // Sidechain capture is the fallback beat source when instances cannot see each other.
        if (nsc > 0) {
            if (nsc > 1) sideStore_->captureStereo(sc[0], sc[1], n, tl);
            else sideStore_->capture(sc[0], n, tl);
            if (!sideSeen_.load(std::memory_order_relaxed) && juce::FloatVectorOperations::findMaximum(sc[0], n) > 1e-5f) sideSeen_.store(true, std::memory_order_relaxed);
        }
        fonem_.setParams({ pDensity_->load() * 0.01f, pClarity_->load() * 0.01f, pAttack_->load() * 0.01f, pBreath_->load() * 0.01f, pCharX_->load() });
        fonem_.process(ch, nc, n);
        const int latency = fonem_.latencySamples();
        const int64_t outTime = tl >= 0 ? tl - latency : -1;
        if (nc > 1) vocalStore_->captureStereo(ch[0], ch[1], n, outTime);
        else vocalStore_->capture(ch[0], n, outTime);

        const int slot = rtSlot_.load();
        const Plan* plan = group != nullptr && slot >= 0 ? group->plan(slot) : nullptr;
        // Live band levels for the beat instance, addressed by project time.
        for (int i = 0; i < n; ++i) {
            const double mono = nc > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
            liveEnv_.tick(mono);
            if (++ringCount_ >= ringHop_) {
                ringCount_ = 0;
                if (outTime >= 0) {
                    std::array<float, kCutBands> lv{};
                    liveEnv_.levelsDb(lv.data());
                    const double t = static_cast<double>(outTime + i) / fs_;
                    if (group != nullptr && slot >= 0 && t >= 0.0) group->ring(slot).write(t, lv.data());
                    staleDetector_.feed(t, frameVoiceLevelDb(lv.data()), plan);
                }
            }
        }
        const bool stale = staleDetector_.stale();
        staleFlag_.store(stale, std::memory_order_relaxed);
        const bool enabled = pFixes_->load() > 0.5f && !stale;
        vocalFix_.process(ch, nc, n, outTime, playing, plan, enabled);
        space_.setParams({ pSpace_->load() * 0.01f, pCharY_->load() });
        space_.process(ch, nc, n, bpm);
    } else if (r == Role::Beat) {
        if (nc > 1) beatStore_->captureStereo(ch[0], ch[1], n, tl);
        else beatStore_->capture(ch[0], n, tl);
        place_.setDepth(pPlace_->load() * 0.01f);
        place_.process(ch, nc, n, nsc > 0 ? sc : nullptr, nsc, timeline, playing, rtControl_.load());
    } else {
        if (resetMeterRequest_.exchange(false)) monitor_.reset();
        monitor_.setDevice(static_cast<Device>(juce::jlimit(0, kNumDevices - 1, static_cast<int>(pMonDevice_->load()))));
        monitor_.setNormalisation(static_cast<NormMode>(juce::jlimit(0, 3, static_cast<int>(pMonNorm_->load()))));
        monitor_.process(ch, nc, n, isNonRealtime());
    }
}

void WersProcessor::timerCallback() {
    // The host says which project this instance is in only after it has been created.
    if (roleDirty_.exchange(false) || reaper_.project() != appliedProject_) applyRoleAndGroup();
    if (group_ == nullptr) return;

    const Role r = role();
    const float faderDb = reaper_.trackGainDb();
    if (r == Role::Beat) {
        const float wanted = group_->takeRequestedPlaceDepth();
        if (wanted >= 0.0f) setParam(ids::place, wanted * 100.0f);
        group_->publishPlaceDepth(pPlace_->load() * 0.01f);
        group_->setBeatFaderDb(faderDb);
    } else if (r == Role::Vocal) {
        if (slot_ >= 0) {
            group_->setStale(slot_, staleFlag_.load());
            group_->setVocalFaderDb(slot_, faderDb);
        }
        group_->setFixesEnabled(pFixes_->load() > 0.5f);
        session_.setPlaceDepth(group_->beatCount() > 0 ? group_->placeDepth() : 0.0f);
        session_.setFaders(faderDb, group_->beatCount() > 0 ? group_->beatFaderDb() : 0.0f);

        // When the song stops after a real listen, the analysis starts by itself.
        if (session_.phase() == SessionPhase::Listening) {
            const bool hostSilent = juce::Time::getMillisecondCounter() - lastBlockMs_.load() > 1000u;
            if (playing_.load() && !hostSilent) {
                stoppedTicks_ = 0;
            } else if (session_.heardSeconds() >= 3.0 && ++stoppedTicks_ >= 5) {
                stoppedTicks_ = 0;
                session_.stopListeningAndAnalyse();
            }
        }
    }
}

bool WersProcessor::startAnalysis() {
    stoppedTicks_ = 0;
    session_.startListening();
    return true;
}

void WersProcessor::chooseRole(Role r) {
    roleChosenByUser_ = true;
    setParam(ids::role, static_cast<float>(r));
    roleDirty_.store(false);
    applyRoleAndGroup();
}

void WersProcessor::cycleGroup() {
    setParam(ids::group, static_cast<float>((appliedGroup_ + 1) % kLinkGroups));
    roleDirty_.store(false);
    applyRoleAndGroup();
}

void WersProcessor::setParam(const char* id, float plainValue) {
    if (auto* p = apvts_.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
        p->endChangeGesture();
    }
}

void WersProcessor::applySuggestion(const SuggestedSettings& s) {
    if (!s.valid) return;
    setParam(ids::density, s.density * 100.0f);
    setParam(ids::clarity, s.clarity * 100.0f);
    setParam(ids::attack, s.attack * 100.0f);
    setParam(ids::breath, s.breath * 100.0f);
    setParam(ids::space, s.space * 100.0f);
    setParam(ids::charX, s.characterX);
    setParam(ids::charY, s.characterY);
    if (role() == Role::Beat) setParam(ids::place, s.place * 100.0f);
    else if (group_ != nullptr) group_->requestPlaceDepth(s.place);
}

void WersProcessor::applyCharacterPreset(float x, float y, float density, float clarity, float space) {
    setParam(ids::charX, x);
    setParam(ids::charY, y);
    setParam(ids::density, density);
    setParam(ids::clarity, clarity);
    setParam(ids::space, space);
}

void WersProcessor::classLevels(float& vowel, float& sib, float& stop, float& breath) const noexcept {
    vowel = shownVowel_.load(std::memory_order_relaxed); sib = shownSib_.load(std::memory_order_relaxed);
    stop = shownStop_.load(std::memory_order_relaxed); breath = shownBreath_.load(std::memory_order_relaxed);
}

WersProcessor::MeterValues WersProcessor::meters() const noexcept {
    return { shownM_.load(), shownS_.load(), shownI_.load(), shownTp_.load(), shownSec_.load(), shownGain_.load() };
}

void WersProcessor::updateTrackProperties(const TrackProperties& properties) {
    if (roleChosenByUser_ || !properties.name.has_value()) return;
    // Only exact words count, so a track called "orbit" does not turn into the beat.
    juce::StringArray words;
    words.addTokens(properties.name->toLowerCase(), " _-./()[]", "");
    int guess = -1;
    for (const auto& w : words) {
        if (w == "beat" || w == "bit" || w == "instrumental" || w == "instr" || w == "prod") guess = static_cast<int>(Role::Beat);
        else if (w == "master" || w == "mix") guess = static_cast<int>(Role::Master);
        else if (w == "vox" || w == "vocal" || w == "vocals" || w == "wokal" || w == "main" || w == "lead") guess = static_cast<int>(Role::Vocal);
    }
    if (guess < 0) return;
    roleChosenByUser_ = true; // a later rename must not change the role under the user's feet
    setParam(ids::role, static_cast<float>(guess));
}

void WersProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(static_cast<int>(kStateMagic));
    auto tree = apvts_.copyState();
    tree.setProperty("uiScale", uiScale_.load(), nullptr);
    juce::MemoryOutputStream xml;
    if (auto e = tree.createXml()) e->writeTo(xml, juce::XmlElement::TextFormat().withoutHeader().singleLine());
    out.writeInt(static_cast<int>(xml.getDataSize()));
    out.write(xml.getData(), xml.getDataSize());

    juce::MemoryBlock compressed;
    if (role() == Role::Vocal) {
        const auto blob = session_.save();
        juce::MemoryOutputStream raw(compressed, false);
        juce::GZIPCompressorOutputStream gz(raw, 6);
        gz.write(blob.data(), blob.size());
        gz.flush();
    }
    out.writeInt(static_cast<int>(compressed.getSize()));
    out.write(compressed.getData(), compressed.getSize());
}

void WersProcessor::setStateInformation(const void* data, int size) {
    if (data == nullptr || size < 12) return;
    juce::MemoryInputStream in(data, static_cast<size_t>(size), false);
    if (static_cast<juce::uint32>(in.readInt()) != kStateMagic) return;
    const int xmlSize = in.readInt();
    if (xmlSize <= 0 || xmlSize > in.getNumBytesRemaining()) return;
    juce::MemoryBlock xmlData;
    in.readIntoMemoryBlock(xmlData, xmlSize);
    if (auto e = juce::parseXML(xmlData.toString())) {
        if (e->hasTagName(apvts_.state.getType().toString())) {
            auto tree = juce::ValueTree::fromXml(*e);
            uiScale_.store(juce::jlimit(0.5f, 2.0f, static_cast<float>(tree.getProperty("uiScale", 1.0f))));
            roleChosenByUser_ = true; // a saved instance keeps the role it had
            apvts_.replaceState(tree);
        }
    }
    const int blobSize = in.getNumBytesRemaining() >= 4 ? in.readInt() : 0;
    if (blobSize > 0 && blobSize <= in.getNumBytesRemaining()) {
        juce::MemoryBlock compressed;
        in.readIntoMemoryBlock(compressed, blobSize);
        juce::MemoryInputStream raw(compressed, false);
        juce::GZIPDecompressorInputStream gz(raw);
        juce::MemoryOutputStream unpacked;
        // A hard limit protects against a damaged stream claiming to be enormous.
        constexpr juce::int64 kMaxBytes = 256 * 1024 * 1024;
        char buf[16384];
        juce::int64 total = 0;
        for (;;) {
            const int got = gz.read(buf, sizeof(buf));
            if (got <= 0) break;
            total += got;
            if (total > kMaxBytes) { total = -1; break; }
            unpacked.write(buf, static_cast<size_t>(got));
        }
        if (total > 0) session_.load(static_cast<const uint8_t*>(unpacked.getData()), unpacked.getDataSize());
    }
    roleDirty_.store(true);
    if (juce::MessageManager::existsAndIsCurrentThread()) { roleDirty_.store(false); applyRoleAndGroup(); }
}

juce::AudioProcessorEditor* WersProcessor::createEditor() { return new WersEditor(*this); }

} // namespace wers

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new wers::WersProcessor(); }
