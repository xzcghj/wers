#pragma once
// Text recognition behind one interface, so everything else works without a model.
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wers {

struct RecognizedWord {
    std::string text;
    double t0 = 0.0, t1 = 0.0;
    float confidence = 1.0f;
};

struct RecognizerOptions {
    std::string language = "pl";
    std::string prompt;                // pasted lyrics help with slang and names
    int threads = 0;                   // 0 means half of the available cores
};

class Recognizer {
public:
    virtual ~Recognizer() = default;
    // pcm is 16 kHz mono. Regions lets the caller skip silence, which also prevents invented text.
    struct Region { size_t start, length; };
    virtual std::vector<RecognizedWord> transcribe(const std::vector<float>& pcm, const std::vector<Region>& regions, const RecognizerOptions& opt,
                                                   const std::atomic<bool>& cancel, const std::function<void(float)>& progress) = 0;
};

// Returns null when the model cannot be loaded or the CPU lacks AVX2.
std::unique_ptr<Recognizer> makeWhisperRecognizer(const std::string& modelPath, std::string* error = nullptr);
bool recognitionSupported() noexcept;

// Replaces recognised words by the matching words of pasted lyrics, keeping recognised timing.
std::vector<RecognizedWord> alignToLyrics(const std::vector<RecognizedWord>& recognised, const std::string& lyrics);
// Lower case, no punctuation. UTF-8 aware for Polish letters.
std::string normaliseWord(const std::string& word);

} // namespace wers
