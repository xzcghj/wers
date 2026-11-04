#include "ModelManager.h"

namespace wers {

juce::File ModelManager::modelFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("WERS").getChildFile("models").getChildFile("ggml-small-q5_1.bin");
}

juce::int64 ModelManager::expectedBytes() { return 190085487; }

ModelManager::ModelManager()
    : source_("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin"), target_(modelFile()), expected_(expectedBytes()) {
    if (target_.existsAsFile() && target_.getSize() == expected_) state_ = State::Ready;
}

ModelManager::~ModelManager() { cancelDownload(); }

void ModelManager::startDownload() {
    const juce::ScopedLock g(lock_);
    if (state_ == State::Downloading || state_ == State::Ready) return;
    target_.getParentDirectory().createDirectory();
    const auto part = target_.withFileExtension(".part");
    part.deleteFile();
    progress_ = 0.0f;
    state_ = State::Downloading;
    task_ = source_.downloadToFile(part, juce::URL::DownloadTaskOptions().withListener(this));
    if (task_ == nullptr) state_ = State::Failed;
}

void ModelManager::cancelDownload() {
    std::unique_ptr<juce::URL::DownloadTask> old;
    {
        const juce::ScopedLock g(lock_);
        old = std::move(task_);
        if (state_ == State::Downloading) state_ = State::Missing;
    }
    old.reset();
    target_.withFileExtension(".part").deleteFile();
}

void ModelManager::progress(juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total) {
    const auto all = total > 0 ? total : expected_;
    progress_ = static_cast<float>(static_cast<double>(downloaded) / static_cast<double>(juce::jmax<juce::int64>(1, all)));
}

void ModelManager::finished(juce::URL::DownloadTask* task, bool success) {
    const auto part = target_.withFileExtension(".part");
    const bool complete = success && !task->hadError() && part.getSize() == expected_;
    if (complete && part.moveFileTo(target_)) {
        state_ = State::Ready;
    } else {
        part.deleteFile();
        state_ = State::Failed;
    }
}

} // namespace wers
