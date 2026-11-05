#pragma once
// Runs the speech model in its own process. If the model ever fails, the host does not notice:
// the result is simply no text, and the plugin shows fragments of time instead of words.
#include "analysis/HelperProtocol.h"
#include <juce_core/juce_core.h>

namespace wers {

class ProcessRecognizer final : public Recognizer {
public:
    ProcessRecognizer(juce::File helperProgram, juce::String modelPath);

    std::vector<RecognizedWord> transcribe(const std::vector<float>& pcm, const std::vector<Region>& regions, const RecognizerOptions& options,
                                           const std::atomic<bool>& cancel, const std::function<void(float)>& progress) override;
    bool failed() const noexcept override { return failed_; }

    // The program ships inside the plugin bundle, next to the plugin's own binary.
    static juce::File bundledHelper();
private:
    juce::File helper_;
    juce::String model_;
    bool failed_ = false;
};

} // namespace wers
