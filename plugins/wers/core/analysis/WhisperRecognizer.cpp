#include "Recognizer.h"
#include "whisper.h"
#include <algorithm>
#include <thread>

namespace wers {

namespace {

class WhisperRecognizer final : public Recognizer {
public:
    WhisperRecognizer(whisper_context* ctx, bool aligned) : ctx_(ctx), aligned_(aligned) {}
    ~WhisperRecognizer() override { whisper_free(ctx_); }

    std::vector<RecognizedWord> transcribe(const std::vector<float>& pcm, const std::vector<Region>& regions, const RecognizerOptions& opt,
                                           const std::atomic<bool>& cancel, const std::function<void(float)>& progress) override {
        std::vector<RecognizedWord> out;
        whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
        p.beam_search.beam_size = 3;
        p.n_threads = opt.threads > 0 ? opt.threads : std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);
        p.language = opt.language.empty() ? "auto" : opt.language.c_str();
        p.translate = false;
        p.print_progress = false; p.print_realtime = false; p.print_timestamps = false; p.print_special = false;
        p.token_timestamps = true;
        p.max_len = 1;              // one word per segment
        p.split_on_word = true;
        p.thold_pt = 0.01f;
        p.no_context = true;        // a region must not inherit invented text from the previous one
        p.suppress_nst = true;
        p.temperature_inc = 0.2f;
        p.no_speech_thold = 0.6f;
        p.initial_prompt = opt.prompt.empty() ? nullptr : opt.prompt.c_str();
        p.abort_callback = [](void* user) { return static_cast<const std::atomic<bool>*>(user)->load(); };
        p.abort_callback_user_data = const_cast<std::atomic<bool>*>(&cancel);

        size_t total = 0, done = 0;
        for (const auto& r : regions) total += r.length;
        for (const auto& r : regions) {
            if (cancel.load()) break;
            if (r.start >= pcm.size()) continue;
            const size_t len = std::min(r.length, pcm.size() - r.start);
            if (len < 1600) continue; // whisper needs at least 100 ms
            if (whisper_full(ctx_, p, pcm.data() + r.start, static_cast<int>(len)) != 0) { done += len; continue; }
            const size_t firstOfRegion = out.size();
            const double offset = static_cast<double>(r.start) / 16000.0;
            const double regionEnd = offset + static_cast<double>(len) / 16000.0;
            const int segs = whisper_full_n_segments(ctx_);
            for (int s = 0; s < segs; ++s) {
                const char* text = whisper_full_get_segment_text(ctx_, s);
                if (text == nullptr) continue;
                std::string t(text);
                const auto first = t.find_first_not_of(" \t\n");
                if (first == std::string::npos) continue;
                t = t.substr(first, t.find_last_not_of(" \t\n") - first + 1);
                if (normaliseWord(t).empty()) continue;
                RecognizedWord w;
                w.text = t;
                w.t0 = offset + static_cast<double>(whisper_full_get_segment_t0(ctx_, s)) * 0.01;
                w.t1 = offset + static_cast<double>(whisper_full_get_segment_t1(ctx_, s)) * 0.01;
                float sum = 0.0f; int n = 0;
                const int toks = whisper_full_n_tokens(ctx_, s);
                bool haveAligned = false;
                for (int k = 0; k < toks; ++k) {
                    if (whisper_full_get_token_id(ctx_, s, k) >= whisper_token_eot(ctx_)) continue;
                    if (aligned_ && !haveAligned) {
                        const auto data = whisper_full_get_token_data(ctx_, s, k);
                        if (data.t_dtw >= 0) { w.t0 = offset + static_cast<double>(data.t_dtw) * 0.01; haveAligned = true; }
                    }
                    sum += whisper_full_get_token_p(ctx_, s, k);
                    ++n;
                }
                w.confidence = n > 0 ? sum / static_cast<float>(n) : 0.0f;
                out.push_back(std::move(w));
            }
            // Aligned times mark where each word starts, so a word lasts until the next one.
            if (aligned_) {
                for (size_t i = firstOfRegion; i + 1 < out.size(); ++i) out[i].t1 = out[i + 1].t0;
            }
            if (out.size() > firstOfRegion) out.back().t1 = std::min(std::max(out.back().t1, out.back().t0 + 0.08), regionEnd);
            out.erase(std::remove_if(out.begin() + static_cast<std::ptrdiff_t>(firstOfRegion), out.end(), [](const RecognizedWord& w) { return w.t1 <= w.t0; }), out.end());
            done += len;
            if (progress && total > 0) progress(static_cast<float>(done) / static_cast<float>(total));
        }
        return out;
    }
private:
    whisper_context* ctx_;
    bool aligned_;
};

// Which attention heads to align with depends on the size of the model.
whisper_alignment_heads_preset alignmentHeads(const std::string& path) {
    if (path.find("large-v3-turbo") != std::string::npos) return WHISPER_AHEADS_LARGE_V3_TURBO;
    if (path.find("large-v3") != std::string::npos) return WHISPER_AHEADS_LARGE_V3;
    if (path.find("medium") != std::string::npos) return WHISPER_AHEADS_MEDIUM;
    if (path.find("base") != std::string::npos) return WHISPER_AHEADS_BASE;
    if (path.find("tiny") != std::string::npos) return WHISPER_AHEADS_TINY;
    return WHISPER_AHEADS_SMALL;
}

} // namespace

std::unique_ptr<Recognizer> makeWhisperRecognizer(const std::string& modelPath, std::string* error, bool alignedTimes) {
    if (!recognitionSupported()) {
        if (error != nullptr) *error = "cpu";
        return nullptr;
    }
    whisper_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    if (alignedTimes) {
        cp.flash_attn = false; // the library turns aligned times off otherwise
        cp.dtw_token_timestamps = true;
        cp.dtw_aheads_preset = alignmentHeads(modelPath);
    }
    whisper_context* ctx = whisper_init_from_file_with_params(modelPath.c_str(), cp);
    if (ctx == nullptr) {
        if (error != nullptr) *error = "model";
        return nullptr;
    }
    return std::make_unique<WhisperRecognizer>(ctx, alignedTimes);
}

} // namespace wers
