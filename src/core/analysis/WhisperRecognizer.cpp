#include "Recognizer.h"

#if WERS_HAS_WHISPER
 #include "whisper.h"
 #if defined(_MSC_VER)
  #include <intrin.h>
 #elif defined(__x86_64__)
  #include <cpuid.h>
 #endif
#endif
#include <algorithm>
#include <thread>

namespace wers {

#if WERS_HAS_WHISPER

bool recognitionSupported() noexcept {
#if defined(_MSC_VER)
    int info[4]{};
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuidex(info, 7, 0);
    const bool avx2 = (info[1] & (1 << 5)) != 0;
    __cpuid(info, 1);
    const bool fma = (info[2] & (1 << 12)) != 0, osxsave = (info[2] & (1 << 27)) != 0, f16c = (info[2] & (1 << 29)) != 0;
    if (!avx2 || !fma || !f16c || !osxsave) return false;
    return (_xgetbv(0) & 6) == 6;
#elif defined(__x86_64__)
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
    return true;
#endif
}

namespace {

class WhisperRecognizer final : public Recognizer {
public:
    explicit WhisperRecognizer(whisper_context* ctx) : ctx_(ctx) {}
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
                const double offset = static_cast<double>(r.start) / 16000.0;
                w.t0 = offset + static_cast<double>(whisper_full_get_segment_t0(ctx_, s)) * 0.01;
                w.t1 = offset + static_cast<double>(whisper_full_get_segment_t1(ctx_, s)) * 0.01;
                float sum = 0.0f; int n = 0;
                const int toks = whisper_full_n_tokens(ctx_, s);
                for (int k = 0; k < toks; ++k) {
                    if (whisper_full_get_token_id(ctx_, s, k) >= whisper_token_eot(ctx_)) continue;
                    sum += whisper_full_get_token_p(ctx_, s, k);
                    ++n;
                }
                w.confidence = n > 0 ? sum / static_cast<float>(n) : 0.0f;
                if (w.t1 > w.t0) out.push_back(std::move(w));
            }
            done += len;
            if (progress && total > 0) progress(static_cast<float>(done) / static_cast<float>(total));
        }
        return out;
    }
private:
    whisper_context* ctx_;
};

} // namespace

std::unique_ptr<Recognizer> makeWhisperRecognizer(const std::string& modelPath, std::string* error) {
    if (!recognitionSupported()) {
        if (error != nullptr) *error = "cpu";
        return nullptr;
    }
    whisper_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    whisper_context* ctx = whisper_init_from_file_with_params(modelPath.c_str(), cp);
    if (ctx == nullptr) {
        if (error != nullptr) *error = "model";
        return nullptr;
    }
    return std::make_unique<WhisperRecognizer>(ctx);
}

#else

bool recognitionSupported() noexcept { return false; }
std::unique_ptr<Recognizer> makeWhisperRecognizer(const std::string&, std::string* error) {
    if (error != nullptr) *error = "unavailable";
    return nullptr;
}

#endif

} // namespace wers
