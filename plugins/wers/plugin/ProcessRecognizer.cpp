#include "ProcessRecognizer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace wers {

namespace {
long currentProcessId() {
#if JUCE_WINDOWS
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

std::filesystem::path toPath(const juce::File& file) {
#if JUCE_WINDOWS
    return std::filesystem::path(file.getFullPathName().toWideCharPointer());
#else
    return std::filesystem::path(file.getFullPathName().toStdString());
#endif
}

// Files left behind by a host that was killed in the middle of a job.
void removeOldTempFiles(const juce::File& folder) {
    const auto dayAgo = juce::Time::getCurrentTime() - juce::RelativeTime::days(1.0);
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "wers_job*"))
        if (f.getLastModificationTime() < dayAgo) f.deleteFile();
}
} // namespace

ProcessRecognizer::ProcessRecognizer(juce::File helperProgram, juce::String modelPath)
    : helper_(std::move(helperProgram)), model_(std::move(modelPath)) {}

juce::File ProcessRecognizer::bundledHelper() {
    const auto self = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#if JUCE_WINDOWS
    return self.getSiblingFile("wers_whisper.exe");
#else
    return self.getSiblingFile("wers_whisper");
#endif
}

std::vector<RecognizedWord> ProcessRecognizer::transcribe(const std::vector<float>& pcm, const std::vector<Region>& regions, const RecognizerOptions& options,
                                                          const std::atomic<bool>& cancel, const std::function<void(float)>& progress) {
    failed_ = true; // until the program reports that it finished
    if (!helper_.existsAsFile()) return {};

    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    temp.createDirectory();
    removeOldTempFiles(temp);
    const auto jobFile = temp.getNonexistentChildFile("wers_job", ".bin", false);
    const auto outFile = jobFile.withFileExtension(".txt");
    const struct Cleanup {
        juce::File a, b;
        ~Cleanup() { a.deleteFile(); b.deleteFile(); }
    } cleanup{ jobFile, outFile };

    {
        std::ofstream job(toPath(jobFile), std::ios::binary);
        if (!job || !helper::writeJob(job, pcm, regions, options)) return {};
    }
    if (!outFile.create().wasOk()) return {};

    juce::ChildProcess process;
    juce::StringArray command;
    command.add(helper_.getFullPathName());
    command.add(jobFile.getFullPathName());
    command.add(outFile.getFullPathName());
    command.add(juce::String(static_cast<juce::int64>(currentProcessId())));
    command.add(model_);
    if (!process.start(command, 0)) return {};

    std::vector<RecognizedWord> words;
    bool finished = false;
    std::ifstream in(toPath(outFile), std::ios::binary);
    std::string pending;
    const auto readNewLines = [&] {
        char buffer[8192];
        for (;;) {
            in.clear();
            in.read(buffer, sizeof(buffer));
            const auto got = in.gcount();
            if (got <= 0) break;
            pending.append(buffer, static_cast<size_t>(got));
        }
        size_t start = 0;
        for (size_t nl = pending.find('\n', start); nl != std::string::npos; nl = pending.find('\n', start)) {
            const std::string line = pending.substr(start, nl - start);
            start = nl + 1;
            float p = 0.0f;
            RecognizedWord w;
            std::string error;
            switch (helper::parseLine(line, p, w, error)) {
                case helper::LineKind::Progress: if (progress) progress(p); break;
                case helper::LineKind::Word: words.push_back(std::move(w)); break;
                case helper::LineKind::End: finished = true; break;
                case helper::LineKind::Error:
                case helper::LineKind::Unknown: break;
            }
        }
        pending.erase(0, start);
    };

    for (;;) {
        if (cancel.load()) {
            process.kill();
            failed_ = false; // stopped on purpose
            return {};
        }
        const bool running = process.isRunning();
        readNewLines();
        if (!running) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    readNewLines();
    in.close();
    if (!finished) return {};
    failed_ = false;
    return words;
}

} // namespace wers
