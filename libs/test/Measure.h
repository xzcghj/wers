#pragma once
// Measurements on sine tests: levels of single components, harmonics and what is neither.
#include "Fft.h"
#include "Loudness.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace wers::test {

inline constexpr std::array<double, 6> kRates{ 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

// Level of y against x delayed by the latency. A neutral path has nothing but silence here.
inline double errorAgainstDelayDb(const std::vector<float>& x, const std::vector<float>& y, int latency, int from = 0) {
    double error = 0.0, signal = 0.0;
    for (size_t i = static_cast<size_t>(latency + from); i < y.size(); ++i) {
        const double d = static_cast<double>(y[i]) - x[i - static_cast<size_t>(latency)];
        error += d * d;
        signal += static_cast<double>(x[i - static_cast<size_t>(latency)]) * x[i - static_cast<size_t>(latency)];
    }
    return error <= 0.0 ? -400.0 : 10.0 * std::log10(error / signal);
}

// The largest step between two samples, which is where a click shows.
inline double worstJump(const std::vector<float>& y, int from, int to) {
    float worst = 0.0f;
    for (int i = std::max(1, from); i < to; ++i) worst = std::max(worst, std::abs(y[static_cast<size_t>(i)] - y[static_cast<size_t>(i - 1)]));
    return static_cast<double>(worst);
}

// Level with the K weighting, the way loudness is measured.
inline double weightedDb(const std::vector<float>& x, int from, int to, double fs = 48000.0) {
    KWeighting weight;
    weight.prepare(fs);
    double energy = 0.0;
    for (int i = 0; i < to; ++i) {
        const double v = weight.tick(static_cast<double>(x[static_cast<size_t>(i)]));
        if (i >= from) energy += v * v;
    }
    return 10.0 * std::log10(energy / (to - from) + 1e-30);
}

// Energy in zones of the input, with a part after the start of each left out and the delay of the path added.
inline double zoneEnergy(const std::vector<float>& y, const std::vector<std::pair<int, int>>& zones, int skip, int late) {
    double energy = 0.0;
    for (const auto& [from, to] : zones)
        for (int i = from + skip + late; i < std::min(to + late, static_cast<int>(y.size())); ++i) energy += static_cast<double>(y[static_cast<size_t>(i)]) * y[static_cast<size_t>(i)];
    return energy;
}

inline double centsBetween(double a, double b) { return 1200.0 * std::log2(a / b); }

// The pitch of a frame from its autocorrelation, or 0 when nothing in it repeats. The first peak near the
// highest one is the period, because every multiple of the period is a peak as well.
inline double framePitchHz(const std::vector<float>& x, int from, int length, double fs, double lowHz, double highHz) {
    const int minLag = std::max(2, static_cast<int>(std::floor(fs / highHz)));
    const int maxLag = static_cast<int>(std::ceil(fs / lowHz));
    if (from < 0 || from + length + maxLag + 1 > static_cast<int>(x.size())) return 0.0;
    const float* a = x.data() + from;
    double own = 0.0;
    for (int i = 0; i < length; ++i) own += static_cast<double>(a[i]) * a[i];
    if (own < 1.0e-20) return 0.0;
    std::vector<double> r(static_cast<size_t>(maxLag) + 2, 0.0);
    double best = 0.0;
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        double sum = 0.0, other = 0.0;
        for (int i = 0; i < length; ++i) {
            sum += static_cast<double>(a[i]) * a[i + lag];
            other += static_cast<double>(a[i + lag]) * a[i + lag];
        }
        r[static_cast<size_t>(lag)] = sum / std::sqrt(own * other + 1.0e-30);
        if (lag >= minLag && lag <= maxLag) best = std::max(best, r[static_cast<size_t>(lag)]);
    }
    if (best < 0.5) return 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double b = r[static_cast<size_t>(lag - 1)], c = r[static_cast<size_t>(lag)], d = r[static_cast<size_t>(lag + 1)];
        if (c >= 0.95 * best && c >= b && c >= d) {
            const double curve = b - 2.0 * c + d;
            return fs / (lag + (curve < 0.0 ? 0.5 * (b - d) / curve : 0.0));
        }
    }
    return 0.0;
}

// How late the envelope of y is against the reference, in milliseconds, within the given range.
inline double envelopeLagMs(const std::vector<float>& reference, const std::vector<float>& y, double fs, double rangeMs = 50.0) {
    const int step = std::max(1, static_cast<int>(std::lround(fs * 0.00025)));
    // Long enough to hold whole periods of a voice an octave down, or their ripple would pull the result.
    const int window = std::max(1, static_cast<int>(std::lround(fs * 0.03 / step)));
    const auto envelope = [&](const std::vector<float>& x) {
        const size_t count = x.size() / static_cast<size_t>(step);
        std::vector<double> power(count, 0.0), smooth(count, 0.0);
        for (size_t i = 0; i < count; ++i)
            for (int k = 0; k < step; ++k) power[i] += static_cast<double>(x[i * static_cast<size_t>(step) + static_cast<size_t>(k)]) * x[i * static_cast<size_t>(step) + static_cast<size_t>(k)];
        double sum = 0.0, mean = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += power[i];
            if (i >= static_cast<size_t>(window)) sum -= power[i - static_cast<size_t>(window)];
            smooth[i] = sum;
            mean += sum;
        }
        for (auto& v : smooth) v -= mean / static_cast<double>(std::max<size_t>(1, count));
        return smooth;
    };
    const auto a = envelope(reference), b = envelope(y);
    const int range = static_cast<int>(rangeMs * 0.001 * fs / step);
    const int count = static_cast<int>(std::min(a.size(), b.size()));
    std::vector<double> c(static_cast<size_t>(2 * range + 1), 0.0);
    int bestAt = 0;
    for (int lag = -range; lag <= range; ++lag) {
        double sum = 0.0;
        for (int i = std::max(0, -lag); i < std::min(count, count - lag); ++i) sum += a[static_cast<size_t>(i)] * b[static_cast<size_t>(i + lag)];
        c[static_cast<size_t>(lag + range)] = sum;
        if (sum > c[static_cast<size_t>(bestAt)]) bestAt = lag + range;
    }
    double position = bestAt - range;
    if (bestAt > 0 && bestAt < 2 * range) {
        const double left = c[static_cast<size_t>(bestAt - 1)], middle = c[static_cast<size_t>(bestAt)], right = c[static_cast<size_t>(bestAt + 1)];
        const double curve = left - 2.0 * middle + right;
        if (curve < 0.0) position += 0.5 * (left - right) / curve;
    }
    return 1000.0 * position * step / fs;
}

// True when the result does not depend on the size of the blocks, to the last bit. makeEngine gives a new
// engine each time, as a callable taking (float* const* channels, int count, int samples).
template <typename MakeEngine>
inline bool sameInAnyBlockSize(const std::vector<std::vector<float>>& input, MakeEngine&& makeEngine) {
    const auto runWith = [&](auto&& nextBlock) {
        auto engine = makeEngine();
        auto y = input;
        std::vector<float*> channels(y.size());
        const size_t length = y.empty() ? 0 : y[0].size();
        for (size_t from = 0; from < length;) {
            const size_t n = std::min(static_cast<size_t>(nextBlock()), length - from);
            for (size_t c = 0; c < y.size(); ++c) channels[c] = y[c].data() + from;
            engine(channels.data(), static_cast<int>(y.size()), static_cast<int>(n));
            from += n;
        }
        return y;
    };
    const auto same = [](const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b) {
        for (size_t c = 0; c < a.size(); ++c)
            if (std::memcmp(a[c].data(), b[c].data(), a[c].size() * sizeof(float)) != 0) return false;
        return true;
    };
    const auto reference = runWith([] { return 32; });
    for (int block : { 256, 4096 })
        if (!same(reference, runWith([block] { return block; }))) return false;
    std::mt19937 random(7);
    return same(reference, runWith([&] { return static_cast<int>(random() % 700) + 1; }));
}

// Side lobes of this window sit far under anything we measure, so a loud tone hides no quiet alias.
inline std::vector<double> kaiserWindow(int n, double beta = 20.0) {
    const auto i0 = [](double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64 && term > 1e-18 * sum; ++k) { term *= (x * 0.5 / k) * (x * 0.5 / k); sum += term; }
        return sum;
    };
    std::vector<double> w(static_cast<size_t>(n));
    const double denominator = i0(beta);
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * i / (n - 1) - 1.0;
        w[static_cast<size_t>(i)] = i0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) / denominator;
    }
    return w;
}

// Level of one sine component against a full-scale sine, with a window as long as x.
inline double toneLevelDb(const std::vector<float>& x, const std::vector<double>& window, double f, double fs) {
    double re = 0.0, im = 0.0, sum = 0.0;
    const double step = 2.0 * 3.14159265358979323846 * f / fs;
    for (size_t i = 0; i < x.size(); ++i) {
        const double v = static_cast<double>(x[i]) * window[i];
        re += v * std::cos(step * static_cast<double>(i));
        im += v * std::sin(step * static_cast<double>(i));
        sum += window[i];
    }
    return 20.0 * std::log10(2.0 * std::sqrt(re * re + im * im) / sum + 1e-20);
}

inline double toneLevelDb(const std::vector<float>& x, double f, double fs) {
    return toneLevelDb(x, kaiserWindow(static_cast<int>(x.size())), f, fs);
}

// The k-th harmonic of f against a full-scale sine.
inline double harmonicDb(const std::vector<float>& x, double f, int k, double fs) { return toneLevelDb(x, f * k, fs); }

// All harmonics inside the band together, against the fundamental.
inline double thdDb(const std::vector<float>& x, double f, double fs) {
    const auto window = kaiserWindow(static_cast<int>(x.size()));
    double power = 0.0;
    for (int k = 2; f * k < 0.45 * fs; ++k) power += std::pow(10.0, toneLevelDb(x, window, f * k, fs) * 0.1);
    return 10.0 * std::log10(power + 1e-30) - toneLevelDb(x, window, f, fs);
}

// The strongest component up to upToHz that is not f, a harmonic of f or DC, against the fundamental.
// Reads up to 65536 samples from the start of x; x needs enough of them to tell the tones apart.
// A halfband filter is half way down exactly at the old Nyquist frequency, so what lies just above it
// folds to just below it. That is why the search stops at the edge of hearing.
inline double strongestNonHarmonicDb(const std::vector<float>& x, double f, double fs, double upToHz = 20000.0) {
    constexpr int kGuardBins = 12;
    int size = 1024;
    while (size < 65536 && static_cast<size_t>(size) * 2 <= x.size()) size *= 2;
    const auto window = kaiserWindow(size);
    std::vector<std::complex<double>> spectrum(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) spectrum[static_cast<size_t>(i)] = static_cast<double>(x[static_cast<size_t>(i)]) * window[static_cast<size_t>(i)];
    wers::Fft fft(size);
    fft.transform(spectrum);
    const double binsPerTone = f * size / fs;
    double fundamental = 1e-30, strongest = 1e-30;
    const int lastBin = std::min(size / 2, static_cast<int>(upToHz * size / fs));
    for (int bin = 0; bin <= lastBin; ++bin) {
        const double power = std::norm(spectrum[static_cast<size_t>(bin)]);
        if (std::abs(bin - binsPerTone) <= kGuardBins) { fundamental = std::max(fundamental, power); continue; }
        if (bin <= kGuardBins) continue;
        const double nearest = std::round(bin / binsPerTone) * binsPerTone;
        if (std::abs(bin - nearest) <= kGuardBins) continue;
        strongest = std::max(strongest, power);
    }
    return 10.0 * std::log10(strongest / fundamental);
}

// How much louder y is than x at one frequency.
inline double magnitudeAtDb(const std::vector<float>& x, const std::vector<float>& y, double f, double fs) {
    return toneLevelDb(y, f, fs) - toneLevelDb(x, f, fs);
}

} // namespace wers::test
