#include "HelperProtocol.h"
#include <charconv>
#include <cstdint>
#include <istream>
#include <ostream>

namespace wers::helper {
namespace {
constexpr char kMagic[4] = { 'W', 'J', 'O', 'B' };
constexpr uint32_t kVersion = 1;
constexpr uint64_t kMaxSamples = 16000ull * 3600ull * 3ull; // three hours is far beyond any song
constexpr uint32_t kMaxText = 1u << 20;
constexpr uint32_t kMaxRegions = 1u << 20;

template <typename T> void put(std::ostream& o, T v) { o.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
template <typename T> bool get(std::istream& i, T& v) { return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T))); }

void putText(std::ostream& o, const std::string& s) {
    put<uint32_t>(o, static_cast<uint32_t>(s.size()));
    o.write(s.data(), static_cast<std::streamsize>(s.size()));
}
bool getText(std::istream& i, std::string& s) {
    uint32_t n = 0;
    if (!get(i, n) || n > kMaxText) return false;
    s.resize(n);
    return n == 0 || static_cast<bool>(i.read(s.data(), static_cast<std::streamsize>(n)));
}

void appendNumber(std::string& o, double v) {
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 4);
    if (r.ec == std::errc()) o.append(buf, r.ptr);
    else o.push_back('0');
}
bool readNumber(const std::string& s, size_t& pos, double& v) {
    while (pos < s.size() && s[pos] == ' ') ++pos;
    const auto r = std::from_chars(s.data() + pos, s.data() + s.size(), v);
    if (r.ec != std::errc()) return false;
    pos = static_cast<size_t>(r.ptr - s.data());
    return true;
}
} // namespace

bool writeJob(std::ostream& o, const std::vector<float>& pcm, const std::vector<Recognizer::Region>& regions, const RecognizerOptions& opt) {
    o.write(kMagic, 4);
    put<uint32_t>(o, kVersion);
    put<int32_t>(o, opt.threads);
    putText(o, opt.language);
    putText(o, opt.prompt);
    put<uint64_t>(o, static_cast<uint64_t>(pcm.size()));
    if (!pcm.empty()) o.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * sizeof(float)));
    put<uint32_t>(o, static_cast<uint32_t>(regions.size()));
    for (const auto& r : regions) {
        put<uint64_t>(o, static_cast<uint64_t>(r.start));
        put<uint64_t>(o, static_cast<uint64_t>(r.length));
    }
    return static_cast<bool>(o);
}

bool readJob(std::istream& i, Job& job) {
    char magic[4]{};
    uint32_t version = 0;
    if (!i.read(magic, 4) || std::char_traits<char>::compare(magic, kMagic, 4) != 0) return false;
    if (!get(i, version) || version != kVersion) return false;
    int32_t threads = 0;
    if (!get(i, threads)) return false;
    job.options.threads = threads;
    if (!getText(i, job.options.language) || !getText(i, job.options.prompt)) return false;
    uint64_t samples = 0;
    if (!get(i, samples) || samples > kMaxSamples) return false;
    job.pcm.resize(static_cast<size_t>(samples));
    if (samples > 0 && !i.read(reinterpret_cast<char*>(job.pcm.data()), static_cast<std::streamsize>(samples * sizeof(float)))) return false;
    uint32_t count = 0;
    if (!get(i, count) || count > kMaxRegions) return false;
    job.regions.resize(count);
    for (auto& r : job.regions) {
        uint64_t a = 0, b = 0;
        if (!get(i, a) || !get(i, b)) return false;
        r.start = static_cast<size_t>(a);
        r.length = static_cast<size_t>(b);
    }
    return true;
}

std::string progressLine(float p) {
    std::string o = "P ";
    appendNumber(o, static_cast<double>(p));
    o.push_back('\n');
    return o;
}

std::string wordLine(const RecognizedWord& w) {
    std::string o = "W ";
    appendNumber(o, w.t0); o.push_back(' ');
    appendNumber(o, w.t1); o.push_back(' ');
    appendNumber(o, static_cast<double>(w.confidence)); o.push_back(' ');
    for (char c : w.text) o.push_back(c == '\n' || c == '\r' ? ' ' : c);
    o.push_back('\n');
    return o;
}

std::string errorLine(const std::string& code) { return "X " + code + "\n"; }

LineKind parseLine(const std::string& line, float& progress, RecognizedWord& word, std::string& error) {
    if (line.size() < 2 || line[1] != ' ') return LineKind::Unknown;
    size_t pos = 2;
    switch (line[0]) {
        case 'P': {
            double v = 0.0;
            if (!readNumber(line, pos, v)) return LineKind::Unknown;
            progress = static_cast<float>(v);
            return LineKind::Progress;
        }
        case 'W': {
            double a = 0.0, b = 0.0, c = 0.0;
            if (!readNumber(line, pos, a) || !readNumber(line, pos, b) || !readNumber(line, pos, c)) return LineKind::Unknown;
            if (pos >= line.size() || line[pos] != ' ') return LineKind::Unknown;
            word.t0 = a; word.t1 = b; word.confidence = static_cast<float>(c);
            word.text = line.substr(pos + 1);
            return LineKind::Word;
        }
        case 'X':
            error = line.substr(2);
            return LineKind::Error;
        case 'E':
            return line == "E ok" ? LineKind::End : LineKind::Unknown;
        default:
            return LineKind::Unknown;
    }
}

} // namespace wers::helper
