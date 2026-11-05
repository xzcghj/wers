#pragma once
// Measurements on sine tests: levels of single components, harmonics and what is neither.
#include "Fft.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace wers::test {

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

// The strongest component that is not f, a harmonic of f or DC, against the fundamental.
// Reads up to 65536 samples from the start of x; x needs enough of them to tell the tones apart.
inline double strongestNonHarmonicDb(const std::vector<float>& x, double f, double fs) {
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
    for (int bin = 0; bin <= size / 2; ++bin) {
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
