#pragma once
// How the plugin talks to the recognition process: a job file going in, a text file coming out.
// Numbers are written without locale rules, because a host may change the locale of its process.
#include "Recognizer.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace wers::helper {

struct Job {
    std::vector<float> pcm;
    std::vector<Recognizer::Region> regions;
    RecognizerOptions options;
};

bool writeJob(std::ostream& out, const std::vector<float>& pcm, const std::vector<Recognizer::Region>& regions, const RecognizerOptions& options);
bool readJob(std::istream& in, Job& job);

std::string progressLine(float progress);
std::string wordLine(const RecognizedWord& word);
std::string errorLine(const std::string& code);
inline std::string endLine() { return "E ok\n"; }

enum class LineKind { Unknown, Progress, Word, Error, End };
// line has no newline at the end.
LineKind parseLine(const std::string& line, float& progress, RecognizedWord& word, std::string& error);

} // namespace wers::helper
