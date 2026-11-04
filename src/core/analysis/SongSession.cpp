#include "SongSession.h"
#include "WordTiming.h"
#include <cstring>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

namespace wers {

namespace {

void lowerThreadPriority() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

// Voice regions for recognition: runs of voice with some air around them, at most 28 s each.
std::vector<Recognizer::Region> voiceRegions(const BandFrames& f) {
    std::vector<Recognizer::Region> out;
    const auto spans = segmentVoice(f);
    constexpr double kMergeGap = 0.8, kPad = 0.25, kMaxLen = 28.0;
    double a = -1.0, b = -1.0;
    const auto flush = [&] {
        if (a < 0.0) return;
        const double s = std::max(0.0, a - kPad), e = b + kPad;
        out.push_back({ static_cast<size_t>(s * kAnalysisRate), static_cast<size_t>((e - s) * kAnalysisRate) });
    };
    for (const auto& sp : spans) {
        if (a < 0.0) { a = sp.t0; b = sp.t1; continue; }
        if (sp.t0 - b < kMergeGap && sp.t1 - a < kMaxLen) { b = sp.t1; continue; }
        flush();
        a = sp.t0; b = sp.t1;
    }
    flush();
    return out;
}

void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i))); }
void putF(std::vector<uint8_t>& o, float v) { uint32_t u; std::memcpy(&u, &v, 4); put32(o, u); }
void putD(std::vector<uint8_t>& o, double v) { uint64_t u; std::memcpy(&u, &v, 8); put32(o, static_cast<uint32_t>(u)); put32(o, static_cast<uint32_t>(u >> 32)); }

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    bool ok() const { return ok_; }
    uint32_t u32() { if (p_ + 4 > n_) { ok_ = false; return 0; } uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d_[p_ + static_cast<size_t>(i)]) << (8 * i); p_ += 4; return v; }
    float f32() { const uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return std::isfinite(v) ? v : (ok_ = false, 0.0f); }
    double f64() { const uint64_t lo = u32(); const uint64_t hi = u32(); const uint64_t u = lo | (hi << 32); double v; std::memcpy(&v, &u, 8); return std::isfinite(v) ? v : (ok_ = false, 0.0); }
    const uint8_t* bytes(size_t count) { if (count > n_ - std::min(p_, n_)) { ok_ = false; return nullptr; } const uint8_t* r = d_ + p_; p_ += count; return r; }
    size_t remaining() const { return n_ - std::min(p_, n_); }
private:
    const uint8_t* d_; size_t n_, p_ = 0; bool ok_ = true;
};

constexpr uint32_t kMagic = 0x53524557u; // "WERS"
constexpr uint32_t kVersion = 1;

uint32_t checksum(const uint8_t* d, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; }
    return h;
}

inline uint8_t quantPsd(float db) { return static_cast<uint8_t>(std::clamp((db + 150.0f) * 2.0f, 0.0f, 255.0f)); }
inline float expandPsd(uint8_t q) { return q * 0.5f - 150.0f; }

} // namespace

SongSession::SongSession() {
    worker_ = std::thread([this] { workerLoop(); });
}

SongSession::~SongSession() {
    stop_.store(true);
    cancelJob_.store(true);
    queueCv_.notify_all();
    worker_.join();
    if (vocalStore_) vocalStore_->disarm();
    if (sideStore_) sideStore_->disarm();
}

void SongSession::enqueue(Job job) {
    {
        std::lock_guard<std::mutex> g(queueMutex_);
        queue_.push_back(std::move(job));
        busy_.store(true);
    }
    queueCv_.notify_all();
}

void SongSession::workerLoop() {
    lowerThreadPriority();
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> g(queueMutex_);
            queueCv_.wait(g, [this] { return stop_.load() || !queue_.empty(); });
            if (stop_.load()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            workerBusy_ = true;
        }
        job();
        {
            std::lock_guard<std::mutex> g(queueMutex_);
            workerBusy_ = false;
            if (queue_.empty()) busy_.store(false);
        }
        idleCv_.notify_all();
    }
}

void SongSession::waitIdle() const {
    std::unique_lock<std::mutex> g(queueMutex_);
    idleCv_.wait(g, [this] { return (queue_.empty() && !workerBusy_) || stop_.load(); });
}

void SongSession::attachLink(LinkGroup* group, int slot) noexcept {
    std::shared_ptr<const SongResult> res;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        group_ = group;
        slot_ = slot;
        res = result_;
    }
    if (res) publishPlan(res->words);
}

void SongSession::setStores(std::shared_ptr<TimelineStore> vocal, std::shared_ptr<TimelineStore> sidechain) {
    std::lock_guard<std::mutex> g(stateMutex_);
    vocalStore_ = std::move(vocal);
    sideStore_ = std::move(sidechain);
}

void SongSession::setRecognizerFactory(std::function<std::unique_ptr<Recognizer>()> factory) {
    std::lock_guard<std::mutex> g(stateMutex_);
    recognizerFactory_ = std::move(factory);
}

void SongSession::setLyrics(const std::string& lyrics) { std::lock_guard<std::mutex> g(stateMutex_); lyrics_ = lyrics; }
std::string SongSession::lyrics() const { std::lock_guard<std::mutex> g(stateMutex_); return lyrics_; }
void SongSession::setLanguage(const std::string& language) { std::lock_guard<std::mutex> g(stateMutex_); language_ = language; }
std::string SongSession::failureCode() const { std::lock_guard<std::mutex> g(stateMutex_); return failure_; }
std::shared_ptr<const SongResult> SongSession::result() const { std::lock_guard<std::mutex> g(stateMutex_); return result_; }

void SongSession::setPlaceDepth(float depth) {
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        if (std::abs(depth - mix_.placeDepth) < 1e-4f) return;
        mix_.placeDepth = depth;
    }
    if (!rescorePending_.exchange(true))
        enqueue([this] { rescorePending_.store(false); rescoreAll(); });
}

void SongSession::setFaders(float vocalDb, float beatDb) {
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        MixState next = mix_;
        next.vocalFaderDb = vocalDb;
        next.beatFaderDb = beatDb;
        if (next.sameAs(mix_)) return;
        mix_ = next;
    }
    if (!rescorePending_.exchange(true))
        enqueue([this] { rescorePending_.store(false); rescoreAll(); });
}

bool SongSession::canAnalyseAgain() const {
    std::lock_guard<std::mutex> g(queueMutex_);
    return !workerBusy_ && queue_.empty() && !lastVocal_.empty();
}

SongSession::Preview SongSession::preview() const {
    std::lock_guard<std::mutex> g(stateMutex_);
    return preview_;
}

void SongSession::requestPreview(int index, Device target) {
    // Only the newest request matters when someone clicks through words quickly.
    const int ticket = previewRequest_.fetch_add(1) + 1;
    enqueue([this, index, target, ticket] {
        if (ticket != previewRequest_.load()) return;
        std::shared_ptr<const BandFrames> frames;
        std::shared_ptr<const SongResult> res;
        MixState mix;
        {
            std::lock_guard<std::mutex> g(stateMutex_);
            frames = frames_; res = result_; mix = mix_;
        }
        if (!frames || !res || index < 0 || index >= static_cast<int>(res->words.size())) return;
        Word w = res->words[static_cast<size_t>(index)];
        PlannerLimits lim;
        lim.maxCutDb = 3.0f + 5.0f * mix.placeDepth;
        Preview p;
        p.index = index; p.target = target;
        p.fix = planFix(*frames, w, target, mix, lim);
        w.fix = p.fix;
        scoreAllDevices(*frames, w, mix);
        p.predictedScore = w.verdict[static_cast<size_t>(target)].score;
        {
            std::lock_guard<std::mutex> g(stateMutex_);
            preview_ = p;
        }
        generation_.fetch_add(1);
    });
}

void SongSession::startListening() {
    cancelJob_.store(true); // a running analysis of older audio is pointless now
    enqueue([this] { cancelJob_.store(false); });
    std::shared_ptr<TimelineStore> v, s;
    LinkGroup* group;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        v = vocalStore_; s = sideStore_; group = group_;
    }
    if (v) v->arm();
    if (s) s->arm();
    if (group != nullptr) for (auto& b : group->beatStores()) b->arm();
    phase_.store(SessionPhase::Listening);
    progress_.store(0.0f);
    generation_.fetch_add(1);
}

double SongSession::heardSeconds() const {
    std::lock_guard<std::mutex> g(stateMutex_);
    return vocalStore_ ? vocalStore_->heardSeconds() : 0.0;
}

void SongSession::cancelListening() {
    std::shared_ptr<TimelineStore> v, s;
    LinkGroup* group;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        v = vocalStore_; s = sideStore_; group = group_;
    }
    if (v) v->disarm();
    if (s) s->disarm();
    if (group != nullptr) for (auto& b : group->beatStores()) b->disarm();
    if (phase_.load() == SessionPhase::Listening) phase_.store(result() ? SessionPhase::Ready : SessionPhase::Idle);
    generation_.fetch_add(1);
}

void SongSession::stopListeningAndAnalyse() {
    if (phase_.load() != SessionPhase::Listening) return;
    std::shared_ptr<TimelineStore> v, s;
    LinkGroup* group;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        v = vocalStore_; s = sideStore_; group = group_;
    }
    if (v) v->disarm();
    if (s) s->disarm();
    std::vector<std::shared_ptr<TimelineStore>> beats;
    if (group != nullptr) beats = group->beatStores();
    for (auto& b : beats) b->disarm();

    phase_.store(SessionPhase::Measuring);
    progress_.store(0.0f);
    enqueue([this, v, s, beats] {
        std::vector<float> vocal = v ? v->copy() : std::vector<float>{};
        // The best beat source is the instance on the beat, then the sidechain of this instance.
        std::vector<float> beat;
        double bestHeard = 0.0;
        for (const auto& b : beats) {
            if (b->heardSeconds() > bestHeard) { beat = b->copy(); bestHeard = b->heardSeconds(); }
        }
        if (beat.empty() && s && s->heardSeconds() > 0.5) {
            auto side = s->copy();
            double e = 0.0;
            for (float x : side) e += static_cast<double>(x) * x;
            if (e / std::max<size_t>(1, side.size()) > 1e-9) { beat = std::move(side); }
        }
        const bool hadBeat = !beat.empty();
        if (hadBeat) beat.resize(vocal.size(), 0.0f);
        runAnalysis(std::move(vocal), std::move(beat), hadBeat);
    });
}

void SongSession::analyseAgain() {
    enqueue([this] {
        if (lastVocal_.empty()) return;
        runAnalysis(lastVocal_, lastBeat_, lastHadBeat_);
    });
    phase_.store(SessionPhase::Measuring);
}

void SongSession::runAnalysis(std::vector<float> vocal, std::vector<float> beat, bool hadBeat) {
    cancelJob_.store(false);
    const auto failed = [this](const char* code) {
        {
            std::lock_guard<std::mutex> g(stateMutex_);
            failure_ = code;
        }
        phase_.store(SessionPhase::Failed);
        generation_.fetch_add(1);
    };
    // A vocal shorter than a second or without any signal cannot be analysed.
    double energy = 0.0;
    for (float x : vocal) energy += static_cast<double>(x) * x;
    if (vocal.size() < static_cast<size_t>(kAnalysisRate) || energy / static_cast<double>(std::max<size_t>(1, vocal.size())) < 1e-10) {
        failed("no-vocal");
        return;
    }
    lastVocal_ = vocal; lastBeat_ = beat; lastHadBeat_ = hadBeat;

    phase_.store(SessionPhase::Measuring);
    auto frames = std::make_shared<BandFrames>(computeFrames(vocal, beat, [this](float p) { progress_.store(p); return !cancelJob_.load() && !stop_.load(); }));
    if (cancelJob_.load() || stop_.load()) return;
    if (frames->count == 0) { failed("no-vocal"); return; }

    std::function<std::unique_ptr<Recognizer>()> factory;
    std::string lyrics, language;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        factory = recognizerFactory_;
        lyrics = lyrics_;
        language = language_;
    }
    std::vector<Word> words;
    bool hasText = false, textFailed = false;
    if (factory) {
        phase_.store(SessionPhase::Recognizing);
        progress_.store(0.0f);
        generation_.fetch_add(1);
        auto rec = factory();
        if (rec) {
            RecognizerOptions opt;
            opt.language = language;
            opt.prompt = lyrics.substr(0, 800);
            const auto regions = voiceRegions(*frames);
            auto recognised = rec->transcribe(vocal, regions, opt, cancelJob_, [this](float p) { progress_.store(p); });
            textFailed = rec->failed();
            if (cancelJob_.load() || stop_.load()) return;
            // The same model on vocal plus beat: an independent check of what survives in the mix.
            std::vector<int8_t> heard;
            // It takes as long as the recognition itself, so a small computer skips it.
            const bool enoughCores = std::thread::hardware_concurrency() >= 8 || forceMixCheck_;
            if (hadBeat && !recognised.empty() && !textFailed && enoughCores) {
                phase_.store(SessionPhase::CheckingMix);
                progress_.store(0.0f);
                generation_.fetch_add(1);
                std::vector<float> mix(vocal.size());
                for (size_t i = 0; i < mix.size(); ++i) mix[i] = vocal[i] + (i < beat.size() ? beat[i] : 0.0f);
                RecognizerOptions mixOptions = opt;
                mixOptions.prompt.clear(); // the lyrics would tell the model what it should have heard
                const auto inMix = rec->transcribe(mix, regions, mixOptions, cancelJob_, [this](float p) { progress_.store(p); });
                if (cancelJob_.load() || stop_.load()) return;
                if (!rec->failed()) heard = markHeardWords(recognised, inMix);
            }
            rec.reset(); // the model takes a lot of memory, so it goes away right here
            if (!lyrics.empty()) recognised = alignToLyrics(recognised, lyrics);
            std::vector<TimeSpan> spans;
            std::vector<std::string> texts;
            spans.reserve(recognised.size());
            texts.reserve(recognised.size());
            for (const auto& r : recognised) { spans.push_back({ r.t0, r.t1 }); texts.push_back(r.text); }
            snapWordsToSyllables(*frames, texts, spans);
            words.reserve(recognised.size());
            for (size_t i = 0; i < recognised.size(); ++i) {
                Word w;
                // Pasted lyrics keep their spelling; recognised text loses the capitals and commas
                // the model guesses, which mean nothing in a verse.
                w.text = lyrics.empty() ? normaliseWord(recognised[i].text) : recognised[i].text;
                w.t0 = spans[i].t0; w.t1 = spans[i].t1;
                w.confidence = recognised[i].confidence;
                if (i < heard.size()) w.heardInMix = heard[i];
                words.push_back(std::move(w));
            }
            hasText = !words.empty();
        }
    }
    if (words.empty()) {
        for (const auto& s : segmentVoice(*frames)) {
            Word w; w.t0 = s.t0; w.t1 = s.t1;
            words.push_back(std::move(w));
        }
    }

    phase_.store(SessionPhase::Scoring);
    progress_.store(0.0f);
    MixState mix;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        mix = mix_;
    }
    for (size_t i = 0; i < words.size(); ++i) {
        if (stop_.load() || cancelJob_.load()) return;
        words[i].consonants = consonantSpans(*frames, words[i].t0, words[i].t1);
        scoreAllDevices(*frames, words[i], mix);
        progress_.store(static_cast<float>(i + 1) / static_cast<float>(words.size()));
    }
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        frames_ = frames;
        failure_.clear();
    }
    publish(std::move(words), hasText, hadBeat, static_cast<double>(vocal.size()) / kAnalysisRate, frames->lufs, textFailed);
    phase_.store(SessionPhase::Ready);
    generation_.fetch_add(1);
}

void SongSession::publish(std::vector<Word> words, bool hasText, bool hasBeat, double end, double lufs, bool textFailed) {
    auto res = std::make_shared<SongResult>();
    res->words = std::move(words);
    res->hasText = hasText;
    res->hasBeat = hasBeat;
    res->textFailed = textFailed;
    res->endSeconds = end;
    res->lufs = lufs;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        result_ = res;
        preview_ = Preview{}; // made from the old result, so it may no longer be true
    }
    publishPlan(res->words);
    generation_.fetch_add(1);
}

void SongSession::publishPlan(const std::vector<Word>& words) {
    std::shared_ptr<const BandFrames> frames;
    LinkGroup* group; int slot;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        frames = frames_; group = group_; slot = slot_;
    }
    if (group == nullptr || slot < 0) return;
    auto plan = std::make_shared<Plan>();
    for (const auto& w : words) {
        if (!w.fix.active) continue;
        FixEvent e;
        e.t0 = w.t0; e.t1 = w.t1;
        e.beatCutDb = w.fix.beatCutDb;
        e.vocalBoostDb = w.fix.vocalBoostDb;
        e.consonants = w.consonants;
        plan->events.push_back(std::move(e));
    }
    std::sort(plan->events.begin(), plan->events.end(), [](const FixEvent& a, const FixEvent& b) { return a.t0 < b.t0; });
    if (frames) {
        plan->mapFrames = frames->count;
        plan->vocalMap.resize(static_cast<size_t>(frames->count) * kCutBands);
        plan->levelTrack.resize(static_cast<size_t>(frames->count));
        for (int i = 0; i < frames->count; ++i) {
            const float* v = frames->v10(i);
            for (int j = 0; j < kCutBands; ++j) plan->vocalMap[static_cast<size_t>(i) * kCutBands + static_cast<size_t>(j)] = Plan::quantise(v[j]);
            plan->levelTrack[static_cast<size_t>(i)] = frameVoiceLevelDb(v);
        }
    }
    group->publishPlan(slot, std::move(plan));
}

void SongSession::rescoreAll() {
    std::shared_ptr<const BandFrames> frames;
    std::shared_ptr<const SongResult> old;
    MixState mix;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        frames = frames_; old = result_; mix = mix_;
    }
    if (!frames || !old) return;
    auto words = old->words;
    for (auto& w : words) {
        if (stop_.load()) return;
        scoreAllDevices(*frames, w, mix);
    }
    publish(std::move(words), old->hasText, old->hasBeat, old->endSeconds, old->lufs, old->textFailed);
}

void SongSession::fixWord(int index, Device target) {
    enqueue([this, index, target] {
        std::shared_ptr<const BandFrames> frames;
        std::shared_ptr<const SongResult> old;
        MixState mix;
        {
            std::lock_guard<std::mutex> g(stateMutex_);
            frames = frames_; old = result_; mix = mix_;
        }
        if (!frames || !old || index < 0 || index >= static_cast<int>(old->words.size())) return;
        auto words = old->words;
        Word& w = words[static_cast<size_t>(index)];
        PlannerLimits lim;
        lim.maxCutDb = 3.0f + 5.0f * mix.placeDepth;
        w.fix = planFix(*frames, w, target, mix, lim);
        scoreAllDevices(*frames, w, mix);
        publish(std::move(words), old->hasText, old->hasBeat, old->endSeconds, old->lufs, old->textFailed);
    });
}

void SongSession::undoFix(int index) {
    enqueue([this, index] {
        std::shared_ptr<const BandFrames> frames;
        std::shared_ptr<const SongResult> old;
        MixState mix;
        {
            std::lock_guard<std::mutex> g(stateMutex_);
            frames = frames_; old = result_; mix = mix_;
        }
        if (!old || index < 0 || index >= static_cast<int>(old->words.size())) return;
        auto words = old->words;
        Word& w = words[static_cast<size_t>(index)];
        w.fix = WordFix{};
        if (frames) scoreAllDevices(*frames, w, mix);
        else for (auto& v : w.verdict) v.score = v.baseScore;
        publish(std::move(words), old->hasText, old->hasBeat, old->endSeconds, old->lufs, old->textFailed);
    });
}

void SongSession::fixAllBelow(float threshold, Device target) {
    enqueue([this, threshold, target] {
        std::shared_ptr<const BandFrames> frames;
        std::shared_ptr<const SongResult> old;
        MixState mix;
        {
            std::lock_guard<std::mutex> g(stateMutex_);
            frames = frames_; old = result_; mix = mix_;
        }
        if (!frames || !old) return;
        auto words = old->words;
        PlannerLimits lim;
        lim.maxCutDb = 3.0f + 5.0f * mix.placeDepth;
        size_t done = 0;
        for (auto& w : words) {
            if (stop_.load()) return;
            ++done;
            if (w.fix.active || w.verdict[static_cast<size_t>(target)].score >= threshold) continue;
            w.fix = planFix(*frames, w, target, mix, lim);
            scoreAllDevices(*frames, w, mix);
            progress_.store(static_cast<float>(done) / static_cast<float>(words.size()));
        }
        publish(std::move(words), old->hasText, old->hasBeat, old->endSeconds, old->lufs, old->textFailed);
    });
}

void SongSession::clearAllFixes() {
    enqueue([this] {
        std::shared_ptr<const BandFrames> frames;
        std::shared_ptr<const SongResult> old;
        MixState mix;
        {
            std::lock_guard<std::mutex> g(stateMutex_);
            frames = frames_; old = result_; mix = mix_;
        }
        if (!old) return;
        auto words = old->words;
        for (auto& w : words) {
            w.fix = WordFix{};
            if (frames) scoreAllDevices(*frames, w, mix);
            else for (auto& v : w.verdict) v.score = v.baseScore;
        }
        publish(std::move(words), old->hasText, old->hasBeat, old->endSeconds, old->lufs, old->textFailed);
    });
}

SuggestedSettings SongSession::suggestSettings() const {
    SuggestedSettings s{};
    std::shared_ptr<const BandFrames> f;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        f = frames_;
    }
    if (!f || f->count < 100) return s;
    // Fixed starting points, then measurements move them.
    std::vector<float> vowels, sibExcess, masks;
    const float ref = voiceReferenceDb(*f);
    for (int i = 0; i < f->count; i += 2) {
        const float lvl = frameVoiceLevelDb(f->v10(i));
        const auto cls = f->cls[static_cast<size_t>(i)];
        if (cls == SoundClass::Vowel) vowels.push_back(lvl);
        if (cls == SoundClass::Sibilant) sibExcess.push_back(std::max(f->v10(i)[7], f->v10(i)[8]) - ref);
        if (cls != SoundClass::Silence && lvl > ref - 25.0f) {
            float m = 0.0f;
            for (int j = 3; j <= 7; ++j) m += f->b10(i)[j] - f->v10(i)[j];
            masks.push_back(m / 5.0f);
        }
    }
    const auto pct = [](std::vector<float>& v, float p, float fallback) {
        if (v.empty()) return fallback;
        const auto k = static_cast<long>(static_cast<float>(v.size() - 1) * p);
        std::nth_element(v.begin(), v.begin() + k, v.end());
        return v[static_cast<size_t>(k)];
    };
    const float spread = pct(vowels, 0.95f, ref) - pct(vowels, 0.5f, ref);  // wide vowels want density
    const float sib = pct(sibExcess, 0.9f, -20.0f);                         // loud sibilants want clarity
    const float mask = pct(masks, 0.5f, -12.0f);                            // a beat over the voice wants room
    s.density = std::clamp(0.25f + spread / 14.0f, 0.2f, 0.85f);
    s.clarity = std::clamp(0.45f + (sib + 12.0f) / 24.0f, 0.2f, 0.95f);
    s.attack = 0.15f;
    s.breath = 0.35f;
    s.place = std::clamp(0.3f + (mask + 3.0f) / 16.0f, 0.15f, 0.9f);
    s.space = std::clamp(0.4f - (mask + 3.0f) / 40.0f, 0.15f, 0.5f);
    s.characterX = std::clamp(0.5f + (f->lufs > -11.0 ? 0.2f : -0.1f), 0.0f, 1.0f);
    s.characterY = s.space;
    s.valid = true;
    return s;
}

std::vector<uint8_t> SongSession::save() const {
    std::shared_ptr<const SongResult> res;
    std::shared_ptr<const BandFrames> frames;
    std::string lyrics;
    {
        std::lock_guard<std::mutex> g(stateMutex_);
        res = result_; frames = frames_; lyrics = lyrics_;
    }
    std::vector<uint8_t> o;
    put32(o, kMagic); put32(o, kVersion);
    put32(o, 0); // checksum placeholder
    put32(o, static_cast<uint32_t>(lyrics.size()));
    o.insert(o.end(), lyrics.begin(), lyrics.end());
    put32(o, res ? 1u : 0u);
    if (res) {
        put32(o, (res->hasText ? 1u : 0u) | (res->hasBeat ? 2u : 0u));
        putD(o, res->endSeconds); putD(o, res->lufs);
        put32(o, static_cast<uint32_t>(res->words.size()));
        for (const Word& w : res->words) {
            put32(o, static_cast<uint32_t>(w.text.size()));
            o.insert(o.end(), w.text.begin(), w.text.end());
            putD(o, w.t0); putD(o, w.t1); putF(o, w.confidence);
            o.push_back(static_cast<uint8_t>(w.heardInMix + 1));
            for (const auto& v : w.verdict) { putF(o, v.score); putF(o, v.baseScore); o.push_back(static_cast<uint8_t>(v.worstBand + 1)); o.push_back(static_cast<uint8_t>(v.cause)); }
            o.push_back(w.fix.active ? 1 : 0); o.push_back(static_cast<uint8_t>(w.fix.target));
            for (float x : w.fix.vocalBoostDb) putF(o, x);
            for (float x : w.fix.beatCutDb) putF(o, x);
            put32(o, static_cast<uint32_t>(w.consonants.size()));
            for (const auto& c : w.consonants) { putF(o, c.first); putF(o, c.second); }
        }
    }
    put32(o, frames ? static_cast<uint32_t>(frames->count) : 0u);
    if (frames) {
        putD(o, frames->lufs); putD(o, frames->lufsVocal); putD(o, frames->lufsBeat);
        const auto n = static_cast<size_t>(frames->count);
        for (size_t i = 0; i < n * kSiiBands; ++i) o.push_back(quantPsd(frames->vocal18[i]));
        for (size_t i = 0; i < n * kSiiBands; ++i) o.push_back(quantPsd(frames->beat18[i]));
        for (size_t i = 0; i < n * kCutBands; ++i) o.push_back(Plan::quantise(frames->vocal10[i]));
        for (size_t i = 0; i < n * kCutBands; ++i) o.push_back(Plan::quantise(frames->beat10[i]));
        for (size_t i = 0; i < n; ++i) o.push_back(static_cast<uint8_t>(frames->cls[i]));
    }
    const uint32_t sum = checksum(o.data() + 12, o.size() - 12);
    for (int i = 0; i < 4; ++i) o[8 + static_cast<size_t>(i)] = static_cast<uint8_t>(sum >> (8 * i));
    return o;
}

bool SongSession::load(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 16) return false;
    Reader r(data, size);
    if (r.u32() != kMagic || r.u32() != kVersion) return false;
    const uint32_t sum = r.u32();
    if (checksum(data + 12, size - 12) != sum) return false;

    const uint32_t lyricLen = r.u32();
    const uint8_t* lyricBytes = r.bytes(lyricLen);
    if (!r.ok()) return false;
    std::string lyrics(reinterpret_cast<const char*>(lyricBytes), lyricLen);

    std::shared_ptr<SongResult> res;
    if (r.u32() != 0) {
        res = std::make_shared<SongResult>();
        const uint32_t flags = r.u32();
        res->hasText = (flags & 1u) != 0; res->hasBeat = (flags & 2u) != 0;
        res->endSeconds = r.f64(); res->lufs = r.f64();
        const uint32_t count = r.u32();
        if (!r.ok() || count > r.remaining() / 40u) return false;
        res->words.resize(count);
        for (Word& w : res->words) {
            const uint32_t len = r.u32();
            const uint8_t* t = r.bytes(len);
            if (!r.ok()) return false;
            w.text.assign(reinterpret_cast<const char*>(t), len);
            w.t0 = r.f64(); w.t1 = r.f64(); w.confidence = r.f32();
            const uint8_t* heardByte = r.bytes(1);
            if (heardByte == nullptr) return false;
            w.heardInMix = static_cast<int8_t>(std::min<int>(*heardByte, 2) - 1);
            for (auto& v : w.verdict) {
                v.score = r.f32(); v.baseScore = r.f32();
                const uint8_t* b = r.bytes(2);
                if (!r.ok()) return false;
                v.worstBand = static_cast<int8_t>(std::min<int>(b[0], kSiiBands) - 1);
                v.cause = static_cast<Cause>(std::min<int>(b[1], 2));
            }
            const uint8_t* flagsB = r.bytes(2);
            if (!r.ok()) return false;
            w.fix.active = flagsB[0] != 0;
            w.fix.target = static_cast<Device>(std::min<int>(flagsB[1], kNumDevices - 1));
            for (float& x : w.fix.vocalBoostDb) x = std::clamp(r.f32(), 0.0f, 12.0f);
            for (float& x : w.fix.beatCutDb) x = std::clamp(r.f32(), 0.0f, 12.0f);
            const uint32_t cons = r.u32();
            if (!r.ok() || cons > r.remaining() / 8u) return false;
            w.consonants.resize(cons);
            for (auto& c : w.consonants) { c.first = r.f32(); c.second = r.f32(); }
            if (!r.ok() || !(w.t1 >= w.t0) || w.t0 < 0.0) return false;
        }
    }
    const uint32_t frameCount = r.u32();
    if (!r.ok()) return false;
    std::shared_ptr<BandFrames> frames;
    if (frameCount > 0) {
        frames = std::make_shared<BandFrames>();
        frames->lufs = r.f64(); frames->lufsVocal = r.f64(); frames->lufsBeat = r.f64();
        const size_t n = frameCount;
        const size_t perFrame = 2 * kSiiBands + 2 * kCutBands + 1;
        if (!r.ok() || n > r.remaining() / perFrame) return false;
        frames->count = static_cast<int>(n);
        const auto readInto = [&](std::vector<float>& v, size_t len, auto expand) {
            const uint8_t* b = r.bytes(len);
            v.resize(len);
            for (size_t i = 0; i < len; ++i) v[i] = expand(b[i]);
        };
        readInto(frames->vocal18, n * kSiiBands, expandPsd);
        readInto(frames->beat18, n * kSiiBands, expandPsd);
        readInto(frames->vocal10, n * kCutBands, Plan::expand);
        readInto(frames->beat10, n * kCutBands, Plan::expand);
        const uint8_t* c = r.bytes(n);
        frames->cls.resize(n);
        for (size_t i = 0; i < n; ++i) frames->cls[i] = static_cast<SoundClass>(std::min<int>(c[i], 4));
    }
    if (!r.ok()) return false;

    {
        std::lock_guard<std::mutex> g(stateMutex_);
        lyrics_ = std::move(lyrics);
        result_ = res;
        frames_ = frames;
    }
    if (res) {
        publishPlan(res->words);
        phase_.store(SessionPhase::Ready);
    } else {
        phase_.store(SessionPhase::Idle);
    }
    generation_.fetch_add(1);
    return true;
}

} // namespace wers
