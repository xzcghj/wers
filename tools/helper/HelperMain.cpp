// Runs the speech model on one job file and writes the words to a text file.
// Usage: wers_whisper job.bin out.txt parentPid model.bin
#include "analysis/HelperProtocol.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <unistd.h>
#endif

using namespace wers;
namespace fs = std::filesystem;

namespace {

// It stays out of the way of audio, shows no error dialogs and never outlives its host.
void settle(long parentPid) {
#if defined(_WIN32)
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parentPid))) {
        std::thread([parent] {
            WaitForSingleObject(parent, INFINITE);
            std::_Exit(3);
        }).detach();
    }
#else
    if (nice(10) == -1) { /* a normal priority still works */ }
    std::thread([parentPid] {
        while (getppid() == static_cast<pid_t>(parentPid)) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        std::_Exit(3);
    }).detach();
#endif
}

class Output {
public:
    explicit Output(const fs::path& path) : out_(path, std::ios::binary | std::ios::app) {}
    bool ok() const { return static_cast<bool>(out_); }
    void line(const std::string& s) { out_ << s; out_.flush(); }
private:
    std::ofstream out_;
};

// Test modes stand in for the model, so the whole path can be tested on any machine.
int runTestMode(const std::string& mode, Output& out) {
    if (mode == ":crash:") { std::abort(); }
    if (mode == ":slow:") {
        out.line(helper::progressLine(0.1f));
        std::this_thread::sleep_for(std::chrono::minutes(5));
        return 4;
    }
    const char* words[] = { "księżyc", "szeptem", "źle", "żyję" };
    double t = 0.2;
    for (const char* w : words) {
        RecognizedWord r;
        r.text = w; r.t0 = t; r.t1 = t + 0.3; r.confidence = 0.75f;
        out.line(helper::wordLine(r));
        t += 0.4;
    }
    out.line(helper::progressLine(1.0f));
    out.line(helper::endLine());
    return 0;
}

int run(const fs::path& jobPath, const fs::path& outPath, long parentPid, const std::string& model) {
    settle(parentPid);
    Output out(outPath);
    if (!out.ok()) return 5;
    if (!model.empty() && model.front() == ':') return runTestMode(model, out);

    helper::Job job;
    {
        std::ifstream in(jobPath, std::ios::binary);
        if (!in || !helper::readJob(in, job)) { out.line(helper::errorLine("job")); return 6; }
    }
    std::string error;
    auto recognizer = makeWhisperRecognizer(model, &error);
    if (!recognizer) { out.line(helper::errorLine(error.empty() ? "model" : error)); return 7; }
    const std::atomic<bool> cancel{ false };
    const auto words = recognizer->transcribe(job.pcm, job.regions, job.options, cancel, [&out](float p) { out.line(helper::progressLine(p)); });
    for (const auto& w : words) out.line(helper::wordLine(w));
    out.line(helper::endLine());
    return 0;
}

} // namespace

#if defined(_WIN32)
namespace {
std::string utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n - 1) : 0u, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 5) return 2;
    return run(fs::path(argv[1]), fs::path(argv[2]), std::wcstol(argv[3], nullptr, 10), utf8(argv[4]));
}
#else
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    return run(fs::path(argv[1]), fs::path(argv[2]), std::strtol(argv[3], nullptr, 10), argv[4]);
}
#endif
