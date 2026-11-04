#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace wers {

// Plain radix-2 FFT used by the offline analysis only.
class Fft {
public:
    explicit Fft(int size) : n_(size), rev_(static_cast<size_t>(size)), tw_(static_cast<size_t>(size / 2)) {
        int bits = 0;
        while ((1 << bits) < n_) ++bits;
        for (int i = 0; i < n_; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev_[static_cast<size_t>(i)] = r;
        }
        for (int i = 0; i < n_ / 2; ++i) {
            const double a = -2.0 * 3.14159265358979323846 * i / n_;
            tw_[static_cast<size_t>(i)] = { std::cos(a), std::sin(a) };
        }
    }
    void transform(std::vector<std::complex<double>>& x) const {
        for (int i = 0; i < n_; ++i) {
            const int j = rev_[static_cast<size_t>(i)];
            if (j > i) std::swap(x[static_cast<size_t>(i)], x[static_cast<size_t>(j)]);
        }
        for (int size = 2; size <= n_; size *= 2) {
            const int half = size / 2, step = n_ / size;
            for (int i = 0; i < n_; i += size) {
                for (int j = 0, k = 0; j < half; ++j, k += step) {
                    const auto t = x[static_cast<size_t>(i + j + half)] * tw_[static_cast<size_t>(k)];
                    x[static_cast<size_t>(i + j + half)] = x[static_cast<size_t>(i + j)] - t;
                    x[static_cast<size_t>(i + j)] += t;
                }
            }
        }
    }
private:
    int n_;
    std::vector<int> rev_;
    std::vector<std::complex<double>> tw_;
};

} // namespace wers
