// Tests of the WERS Pogłos engine on its own, without JUCE: measured on impulse responses and signals.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "AllocationCount.h"
#include "Reverb.h"
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

WERS_COUNT_ALLOCATIONS()

using namespace pog;

namespace {

constexpr double kFs = 48000.0;

struct Stereo {
    std::vector<float> l, r;
};

// A neutral wet-only setting: no early reflections, no predelay, open filters, nothing tilting the decay.
Params wetOnly(Algo a, float decay) {
    Params p;
    p.algo = a;
    p.mix = 1.0f;
    p.predelayMs = 0.0f;
    p.decay = decay;
    p.balance = 1.0f;
    p.bassMult = 1.0f;
    p.dampHz = 20000.0f;
    p.highCutHz = 20000.0f;
    p.lowCutHz = 20.0f;
    p.attack = 0.0f;
    return p;
}

std::unique_ptr<Reverb> makeReverb(const Params& p, double fs = kFs) {
    auto r = std::make_unique<Reverb>();
    r->setParams(p);
    r->prepare(fs);
    return r;
}

// Runs a signal through in blocks of uneven length.
void run(Reverb& rv, std::vector<float>& l, std::vector<float>& r, int block = 256) {
    size_t at = 0;
    int k = 0;
    while (at < l.size()) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block - (k++ % 3) * 37), l.size() - at));
        rv.process(l.data() + at, r.data() + at, n);
        at += static_cast<size_t>(n);
    }
}

// The response to a click in both channels.
Stereo impulse(const Params& p, double seconds, double fs = kFs) {
    auto rv = makeReverb(p, fs);
    Stereo s{ std::vector<float>(static_cast<size_t>(seconds * fs), 0.0f), {} };
    s.l[0] = 1.0f;
    s.r = s.l;
    run(*rv, s.l, s.r);
    return s;
}

std::vector<float> noise(size_t n, float amp, uint32_t seed) {
    Rng r(seed);
    std::vector<float> v(n);
    for (auto& x : v) x = amp * r.bipolar();
    return v;
}

std::vector<float> sine(double hz, size_t n, float amp, double fs = kFs) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs));
    return v;
}

double energy(const std::vector<float>& x, double from, double to, double fs = kFs) {
    const size_t a = static_cast<size_t>(from * fs), b = std::min(x.size(), static_cast<size_t>(to * fs));
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += static_cast<double>(x[i]) * x[i];
    return e;
}

double rms(const std::vector<float>& x, double from, double to, double fs = kFs) {
    const double n = std::max(1.0, (std::min(static_cast<double>(x.size()), to * fs) - from * fs));
    return std::sqrt(energy(x, from, to, fs) / n);
}

double db(double ratio) { return 20.0 * std::log10(std::max(1e-15, ratio)); }

// One octave around hz: two band-pass sections in series.
std::vector<float> octave(const std::vector<float>& x, double hz, double fs = kFs) {
    Biquad a, b;
    a.set(Biquad::LowPass, hz * 1.41, 0.7071, 0.0, fs);
    b.set(Biquad::HighPass, hz / 1.41, 0.7071, 0.0, fs);
    Biquad c, d;
    c.set(Biquad::LowPass, hz * 1.41, 0.7071, 0.0, fs);
    d.set(Biquad::HighPass, hz / 1.41, 0.7071, 0.0, fs);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = d.process(c.process(b.process(a.process(x[i]))));
    return y;
}

// RT60 from the Schroeder backward integral, fitted between -5 and -25 dB (T20).
double rt60(const std::vector<float>& x, double fs = kFs) {
    std::vector<double> edc(x.size());
    double acc = 0.0;
    for (size_t i = x.size(); i-- > 0;) {
        acc += static_cast<double>(x[i]) * x[i];
        edc[i] = acc;
    }
    const double total = edc[0];
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (size_t i = 0; i < x.size(); i += 16) {
        const double level = 10.0 * std::log10(std::max(edc[i] / total, 1e-30));
        if (level > -5.0) continue;
        if (level < -25.0) break;
        const double t = static_cast<double>(i) / fs;
        sx += t; sy += level; sxx += t * t; sxy += t * level; ++n;
    }
    if (n < 4) return 0.0;
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return -60.0 / slope;
}

void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Averaged power spectrum (Welch, Hann, half overlap) of x between two times.
std::vector<double> welch(const std::vector<float>& x, double from, double to, int size = 4096, double fs = kFs) {
    std::vector<double> psd(static_cast<size_t>(size / 2), 0.0);
    const size_t a = static_cast<size_t>(from * fs), b = std::min(x.size(), static_cast<size_t>(to * fs));
    int count = 0;
    for (size_t at = a; at + static_cast<size_t>(size) <= b; at += static_cast<size_t>(size / 2)) {
        std::vector<std::complex<double>> buf(static_cast<size_t>(size));
        for (int i = 0; i < size; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / size);
            buf[static_cast<size_t>(i)] = x[at + static_cast<size_t>(i)] * w;
        }
        fft(buf);
        for (int k = 0; k < size / 2; ++k) psd[static_cast<size_t>(k)] += std::norm(buf[static_cast<size_t>(k)]);
        ++count;
    }
    for (auto& v : psd) v /= std::max(1, count);
    return psd;
}

struct Flatness {
    double worstPeakDb = 0.0; // the highest bin above its smoothed neighbourhood
    double flatness = 1.0;    // geometric over arithmetic mean of the whitened spectrum
    double worstHz = 0.0;
};

// How far single bins stick out of the spectrum's smooth shape: ringing shows as narrow peaks that stay.
// Both channels are averaged: twice the data, so chance peaks shrink and real ones stay.
Flatness tailFlatness(const Stereo& x, double from, double to, double rt, double loHz = 150.0, double hiHz = 12000.0) {
    const int size = 4096;
    std::vector<double> psd(static_cast<size_t>(size / 2), 0.0);
    for (const auto* ch : { &x.l, &x.r }) {
        // Undo the decay so every part of the tail counts the same.
        std::vector<float> y(*ch);
        for (size_t i = 0; i < y.size(); ++i) y[i] *= static_cast<float>(std::exp(6.9078 * (static_cast<double>(i) / kFs) / rt));
        const auto one = welch(y, from, to, size);
        for (size_t k = 0; k < psd.size(); ++k) psd[k] += 0.5 * one[k];
    }
    const double binHz = kFs / size;
    Flatness f;
    double logSum = 0.0, sum = 0.0;
    int n = 0;
    for (size_t k = static_cast<size_t>(loHz / binHz); k < static_cast<size_t>(hiHz / binHz); ++k) {
        // A third of an octave around the bin.
        const size_t lo = static_cast<size_t>(k / 1.12), hi = static_cast<size_t>(k * 1.12) + 1;
        double smooth = 0.0;
        for (size_t j = lo; j <= hi; ++j) smooth += psd[j];
        smooth /= static_cast<double>(hi - lo + 1);
        const double ratio = psd[k] / smooth;
        if (10.0 * std::log10(ratio) > f.worstPeakDb) {
            f.worstPeakDb = 10.0 * std::log10(ratio);
            f.worstHz = static_cast<double>(k) * binHz;
        }
        logSum += std::log(ratio);
        sum += ratio;
        ++n;
    }
    f.flatness = std::exp(logSum / n) / (sum / n);
    return f;
}

// Normalised echo density (Abel and Huang): 1 means the response is as dense as Gaussian noise.
double echoDensity(const std::vector<float>& x, double from, double to, double fs = kFs) {
    const int win = static_cast<int>(0.02 * fs);
    double total = 0.0;
    int count = 0;
    for (size_t c = static_cast<size_t>(from * fs); c + static_cast<size_t>(win) < static_cast<size_t>(to * fs); c += static_cast<size_t>(win / 2)) {
        double sq = 0.0;
        for (int i = 0; i < win; ++i) sq += static_cast<double>(x[c + static_cast<size_t>(i)]) * x[c + static_cast<size_t>(i)];
        const double sd = std::sqrt(sq / win);
        int outside = 0;
        for (int i = 0; i < win; ++i) outside += std::abs(x[c + static_cast<size_t>(i)]) > sd ? 1 : 0;
        total += (static_cast<double>(outside) / win) / 0.3173;
        ++count;
    }
    return total / std::max(1, count);
}

double correlation(const std::vector<float>& a, const std::vector<float>& b, double from, double to, double fs = kFs) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = static_cast<size_t>(from * fs); i < std::min(a.size(), static_cast<size_t>(to * fs)); ++i) {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return ab / std::sqrt(std::max(aa * bb, 1e-30));
}

size_t onset(const std::vector<float>& x, float threshold) {
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs(x[i]) > threshold) return i;
    return x.size();
}

const Algo kNetworks[] = { Algo::Hall, Algo::Room, Algo::Plate, Algo::Chamber, Algo::Ambient, Algo::Cathedral };

} // namespace

TEST_CASE("RT60 in the middle of the spectrum is what the decay control says") {
    struct Case {
        Algo algo;
        float decay;
    };
    const Case cases[] = { { Algo::Hall, 0.6f }, { Algo::Hall, 2.0f }, { Algo::Hall, 6.0f }, { Algo::Room, 0.4f }, { Algo::Room, 1.2f },
                           { Algo::Plate, 1.5f }, { Algo::Plate, 4.0f }, { Algo::Chamber, 1.8f }, { Algo::Ambient, 0.8f },
                           { Algo::Cathedral, 3.0f }, { Algo::Cathedral, 9.0f } };
    for (const auto& c : cases) {
        CAPTURE(static_cast<int>(c.algo));
        CAPTURE(c.decay);
        const auto p = wetOnly(c.algo, c.decay);
        const auto ir = impulse(p, 2.4 * c.decay + 0.4);
        const double measured = rt60(octave(ir.l, 1000.0));
        MESSAGE(algoKey(c.algo) << " decay " << c.decay << " s: RT60 at 1 kHz " << measured << " s");
        CHECK(measured == doctest::Approx(c.decay).epsilon(0.08));
    }
}

TEST_CASE("bass multiplier and high damping set the RT60 at low and high frequencies") {
    for (Algo a : { Algo::Hall, Algo::Plate, Algo::Cathedral, Algo::Chamber }) {
        CAPTURE(static_cast<int>(a));
        auto p = wetOnly(a, 2.0f);
        p.bassMult = 2.0f;
        p.bassHz = 350.0f;
        p.dampHz = 2500.0f;
        const auto ir = impulse(p, 2.4 * 4.0 + 0.4);
        for (double hz : { 88.0, 1000.0, 10000.0 }) {
            CAPTURE(hz);
            const double expected = rt60At(p, static_cast<float>(hz));
            const double measured = rt60(octave(ir.l, hz));
            MESSAGE(algoKey(a) << " " << hz << " Hz: expected " << expected << " s, measured " << measured << " s");
            CHECK(measured == doctest::Approx(expected).epsilon(0.12));
        }
        // And what the display promises is what the controls say: twice as long in the bass, a quarter at the top.
        CHECK(rt60At(p, 40.0f) == doctest::Approx(4.0).epsilon(0.08));
        CHECK(rt60At(p, 20000.0f) == doctest::Approx(0.5).epsilon(0.12));
        // The middle stays on the decay control even with both shelves close to it.
        CHECK(rt60At(p, kMidHz) == doctest::Approx(2.0).epsilon(0.001));
    }
    // A short bass and a bright top: the shelves work the other way round too.
    auto p = wetOnly(Algo::Hall, 3.0f);
    p.bassMult = 0.4f;
    p.bassHz = 500.0f;
    const auto ir = impulse(p, 8.0);
    CHECK(rt60(octave(ir.l, 88.0)) == doctest::Approx(rt60At(p, 88.0f)).epsilon(0.12));
    CHECK(rt60(octave(ir.l, 8000.0)) == doctest::Approx(rt60At(p, 8000.0f)).epsilon(0.12));
    CHECK(rt60(octave(ir.l, 1000.0)) == doctest::Approx(3.0).epsilon(0.08));
}

TEST_CASE("the tail is dense: as many echoes as noise soon after the start") {
    for (Algo a : kNetworks) {
        CAPTURE(static_cast<int>(a));
        const auto ir = impulse(wetOnly(a, 2.5f), 1.0);
        const double from = a == Algo::Cathedral ? 0.25 : 0.12;
        const double ned = echoDensity(ir.l, from, from + 0.4);
        MESSAGE(algoKey(a) << ": echo density " << ned);
        CHECK(ned > 0.9);
    }
    // The gate is a cloud of echoes too, dense across its whole length.
    auto p = wetOnly(Algo::Nonlinear, 0.6f);
    p.attack = 0.5f;
    const auto gate = impulse(p, 1.0);
    const double ned = echoDensity(gate.l, 0.08, 0.5);
    MESSAGE("nonlinear: echo density " << ned);
    CHECK(ned > 0.85);
}

TEST_CASE("the tail does not ring: no tone sticks out of its spectrum") {
    for (Algo a : { Algo::Hall, Algo::Room, Algo::Plate, Algo::Chamber, Algo::Ambient, Algo::Cathedral, Algo::Shimmer }) {
        for (float depth : { 0.4f, 0.0f }) {
            CAPTURE(static_cast<int>(a));
            CAPTURE(depth);
            auto p = wetOnly(a, a == Algo::Room ? 1.2f : 2.5f);
            p.modDepth = depth;
            p.shimmer = 0.0f;
            const auto ir = impulse(p, 0.2 + 2.0 * p.decay);
            const auto f = tailFlatness(ir, 0.15, 0.15 + 1.6 * p.decay, p.decay);
            MESSAGE(algoKey(a) << " depth " << depth << ": worst bin " << f.worstPeakDb << " dB at " << f.worstHz << " Hz, flatness " << f.flatness);
            CHECK(f.worstPeakDb < 6.5);
            CHECK(f.flatness > 0.88);
        }
    }
}

TEST_CASE("left and right tails are decorrelated, from a mono source") {
    for (int a = 0; a < kAlgoCount; ++a) {
        CAPTURE(a);
        auto p = wetOnly(static_cast<Algo>(a), 2.0f);
        p.decay = static_cast<Algo>(a) == Algo::Nonlinear ? 0.8f : 2.0f;
        p.balance = 0.5f;
        const auto ir = impulse(p, 1.2);
        const double c = correlation(ir.l, ir.r, 0.05, 1.0);
        MESSAGE(algoKey(static_cast<Algo>(a)) << ": correlation " << c);
        CHECK(std::abs(c) < 0.25);
    }
    // Width at zero makes the wet sound mono.
    auto p = wetOnly(Algo::Hall, 2.0f);
    p.width = 0.0f;
    const auto ir = impulse(p, 1.0);
    CHECK(correlation(ir.l, ir.r, 0.05, 1.0) > 0.9999);
}

TEST_CASE("predelay moves the whole wet sound by exactly its time") {
    for (Algo a : { Algo::Hall, Algo::Room, Algo::Nonlinear }) {
        CAPTURE(static_cast<int>(a));
        // Early reflections only: they do not move with the modulation, so two runs compare sample by sample.
        auto p = wetOnly(a, 1.0f);
        p.balance = 0.0f;
        p.predelayMs = 0.0f;
        const auto ref = impulse(p, 1.0);
        for (float ms : { 12.5f, 100.0f, 333.0f }) {
            CAPTURE(ms);
            p.predelayMs = ms;
            const auto later = impulse(p, 1.0);
            const size_t shift = static_cast<size_t>(std::lround(ms * kFs / 1000.0));
            CHECK(onset(later.l, 1e-5f) == onset(ref.l, 1e-5f) + shift);
            // Not just the start: the response is the same, moved.
            float diff = 0.0f;
            for (size_t i = 0; i + shift < later.l.size(); i += 7) diff = std::max(diff, std::abs(later.l[i + shift] - ref.l[i]));
            CHECK(diff < 1e-3f);
        }
    }
}

TEST_CASE("freeze holds the tail and ignores what comes in, then lets go") {
    for (Algo a : { Algo::Hall, Algo::Plate, Algo::Shimmer, Algo::Nonlinear }) {
        CAPTURE(static_cast<int>(a));
        auto p = wetOnly(a, 1.5f);
        auto rv = makeReverb(p);
        const size_t n = static_cast<size_t>(16.0 * kFs);
        auto l = noise(n, 0.3f, 5), r = noise(n, 0.3f, 6);
        // Noise for a second, then freeze; more noise while frozen, which must not get in.
        for (size_t i = static_cast<size_t>(1.0 * kFs); i < static_cast<size_t>(4.0 * kFs); ++i) l[i] = r[i] = 0.0f;
        for (size_t i = static_cast<size_t>(8.0 * kFs); i < n; ++i) l[i] = r[i] = 0.0f;
        size_t at = 0;
        while (at < n) {
            const double t = static_cast<double>(at) / kFs;
            p.freeze = t >= 0.9 && t < 12.0;
            rv->setParams(p);
            const int len = static_cast<int>(std::min<size_t>(480, n - at));
            rv->process(l.data() + at, r.data() + at, len);
            at += static_cast<size_t>(len);
        }
        const double held1 = rms(l, 2.0, 3.0), held2 = rms(l, 10.0, 11.5);
        const double withInput = rms(l, 5.0, 7.0);
        MESSAGE(algoKey(a) << ": frozen " << db(held1) << " dB, later " << db(held2) << " dB, with new input " << db(withInput) << " dB");
        CHECK(held1 > 0.01);
        CHECK(std::abs(db(held2 / held1)) < 1.0);
        CHECK(std::abs(db(withInput / held1)) < 1.0);
        // Released, it decays again like a 1,5 s reverb.
        CHECK(db(rms(l, 14.5, 15.5) / held2) < -40.0);
    }
}

TEST_CASE("switching algorithms and eras and moving every control while it plays does not click") {
    const size_t n = static_cast<size_t>(10.0 * kFs);
    const auto tone = sine(330.0, n, 0.3f);
    auto measure = [&](bool moving) {
        Params p = wetOnly(Algo::Hall, 2.0f);
        p.mix = 0.7f;
        auto rv = makeReverb(p);
        auto l = tone, r = tone;
        size_t at = 0;
        int step = 0;
        while (at < n) {
            const double t = static_cast<double>(at) / kFs;
            if (moving) {
                p.algo = static_cast<Algo>((step / 6) % kAlgoCount);
                p.era = static_cast<Era>((step / 17) % kEraCount);
                const float s = static_cast<float>(0.5 + 0.5 * std::sin(t * 2.1));
                p.size = s;
                p.decay = 0.3f + 8.0f * s;
                p.predelayMs = 300.0f * (1.0f - s);
                p.attack = s;
                p.balance = 1.0f - s;
                p.diffEarly = s;
                p.diffLate = 1.0f - s;
                p.bassMult = 0.3f + 3.0f * s;
                p.bassHz = 60.0f + 800.0f * s;
                p.dampHz = 1500.0f + 15000.0f * s;
                p.highCutHz = 2000.0f + 18000.0f * s;
                p.lowCutHz = 20.0f + 500.0f * (1.0f - s);
                p.modRate = 0.1f + 4.0f * s;
                p.modDepth = s;
                p.width = 1.5f * s;
                p.mix = s;
                p.duck = 1.0f - s;
                p.shimmer = 0.0f;
                p.freeze = (step / 9) % 3 == 1;
                rv->setParams(p);
            }
            const int len = static_cast<int>(std::min<size_t>(400, n - at));
            rv->process(l.data() + at, r.data() + at, len);
            at += static_cast<size_t>(len);
            ++step;
        }
        // The largest second difference: a click is a sudden change of slope.
        float worst = 0.0f;
        for (size_t i = 2; i < n; ++i)
            for (const auto* x : { &l, &r }) worst = std::max(worst, std::abs((*x)[i] - 2.0f * (*x)[i - 1] + (*x)[i - 2]));
        return worst;
    };
    const float still = measure(false), moving = measure(true);
    MESSAGE("largest second difference: still " << still << ", moving " << moving);
    // A 330 Hz sine at 0.3 has a second difference of 0.0006; a click is a hundred times that.
    CHECK(moving < 0.02f);
}

TEST_CASE("wild settings stay finite and bounded at every sample rate") {
    Rng rng(99);
    auto u = [&] { return rng.unit(); };
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (int round = 0; round < 6; ++round) {
            CAPTURE(fs);
            CAPTURE(round);
            Params p;
            p.algo = static_cast<Algo>(static_cast<int>(u() * kAlgoCount) % kAlgoCount);
            p.era = static_cast<Era>(static_cast<int>(u() * kEraCount) % kEraCount);
            p.mix = u();
            p.predelayMs = u() > 0.5f ? 1000.0f : 0.0f;
            p.decay = u() > 0.5f ? kMaxDecay : kMinDecay;
            p.size = u() > 0.5f ? 1.0f : 0.0f;
            p.attack = u();
            p.balance = u();
            p.diffEarly = u() > 0.3f ? 1.0f : 0.0f;
            p.diffLate = u() > 0.3f ? 1.0f : 0.0f;
            p.bassMult = u() > 0.5f ? 4.0f : 0.25f;
            p.bassHz = u() > 0.5f ? 1000.0f : 50.0f;
            p.dampHz = u() > 0.5f ? 20000.0f : 1000.0f;
            p.highCutHz = u() > 0.5f ? 20000.0f : 1000.0f;
            p.lowCutHz = u() > 0.5f ? 20.0f : 1000.0f;
            p.modRate = u() > 0.5f ? 5.0f : 0.05f;
            p.modDepth = u() > 0.5f ? 1.0f : 0.0f;
            p.width = 1.5f * u();
            p.freeze = u() > 0.8f;
            p.duck = u();
            p.shimmer = 1.0f;
            p.shimmerSemis = u() > 0.5f ? 24.0f : -12.0f;
            auto rv = makeReverb(p, fs);
            const size_t n = static_cast<size_t>(3.0 * fs);
            auto l = noise(n, 0.9f, static_cast<uint32_t>(round + 1)), r = noise(n, 0.9f, static_cast<uint32_t>(round + 50));
            run(*rv, l, r, 512);
            bool finite = true;
            float peak = 0.0f;
            for (size_t i = 0; i < n; ++i) {
                finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
                peak = std::max({ peak, std::abs(l[i]), std::abs(r[i]) });
            }
            CHECK(finite);
            CHECK(peak < 8.0f);
        }
    }
}

TEST_CASE("shimmer at full feedback with the longest decay stays bounded") {
    auto p = wetOnly(Algo::Shimmer, kMaxDecay);
    p.shimmer = 1.0f;
    auto rv = makeReverb(p);
    const size_t n = static_cast<size_t>(30.0 * kFs);
    auto l = noise(n, 0.5f, 3), r = noise(n, 0.5f, 4);
    for (size_t i = static_cast<size_t>(10.0 * kFs); i < n; ++i) l[i] = r[i] = 0.0f;
    run(*rv, l, r, 512);
    float peak = 0.0f;
    for (size_t i = static_cast<size_t>(10.0 * kFs); i < n; ++i) peak = std::max(peak, std::abs(l[i]));
    MESSAGE("shimmer tail peak " << peak);
    CHECK(std::isfinite(peak));
    CHECK(peak < 3.0f);
    // It does decay, slowly.
    CHECK(rms(l, 27.0, 30.0) < rms(l, 10.5, 12.0));
}

TEST_CASE("shimmer adds the octave above; without it the octave stays quiet") {
    auto level880 = [&](float amount) {
        auto p = wetOnly(Algo::Shimmer, 3.0f);
        p.shimmer = amount;
        auto rv = makeReverb(p);
        const size_t n = static_cast<size_t>(4.0 * kFs);
        auto l = sine(440.0, n, 0.3f), r = l;
        run(*rv, l, r);
        const auto psd = welch(l, 2.0, 4.0, 8192);
        const double bin = kFs / 8192.0;
        auto at = [&](double hz) {
            double m = 0.0;
            for (int k = -3; k <= 3; ++k) m = std::max(m, psd[static_cast<size_t>(hz / bin) + static_cast<size_t>(k + 3) - 3]);
            return m;
        };
        return 10.0 * std::log10(at(880.0) / at(440.0));
    };
    const double with = level880(0.8f), without = level880(0.0f);
    MESSAGE("octave relative to the tone: shimmer " << with << " dB, none " << without << " dB");
    CHECK(with > -20.0);
    CHECK(with - without > 20.0);
}

TEST_CASE("the gate has its shape and really closes") {
    auto measure = [&](float attack) {
        auto p = wetOnly(Algo::Nonlinear, 0.5f);
        p.attack = attack;
        return impulse(p, 1.5);
    };
    for (float attack : { 0.0f, 0.5f, 1.0f }) {
        CAPTURE(attack);
        const auto ir = measure(attack);
        const double inside = energy(ir.l, 0.0, 0.52), after = energy(ir.l, 0.6, 1.5);
        CHECK(10.0 * std::log10(after / inside) < -50.0);
    }
    const auto falling = measure(0.0f), reverse = measure(1.0f);
    const double fallFirst = energy(falling.l, 0.0, 0.17), fallLast = energy(falling.l, 0.33, 0.5);
    const double revFirst = energy(reverse.l, 0.0, 0.17), revLast = energy(reverse.l, 0.33, 0.5);
    CHECK(10.0 * std::log10(fallFirst / fallLast) > 6.0);
    CHECK(10.0 * std::log10(revLast / revFirst) > 6.0);
    // The gate length follows decay.
    auto p = wetOnly(Algo::Nonlinear, 0.25f);
    p.attack = 0.5f;
    const auto shortGate = impulse(p, 1.0);
    CHECK(10.0 * std::log10(energy(shortGate.l, 0.33, 1.0) / energy(shortGate.l, 0.0, 0.26)) < -50.0);
}

TEST_CASE("ducking pushes the wet sound down while the input plays and lets it back after") {
    auto run2 = [&](float duck) {
        auto p = wetOnly(Algo::Hall, 3.0f);
        p.duck = duck;
        auto rv = makeReverb(p);
        const size_t n = static_cast<size_t>(4.0 * kFs);
        auto l = noise(n, 0.5f, 9), r = noise(n, 0.5f, 10);
        for (size_t i = static_cast<size_t>(2.0 * kFs); i < n; ++i) l[i] = r[i] = 0.0f;
        run(*rv, l, r);
        return l;
    };
    const auto none = run2(0.0f), full = run2(1.0f);
    const double during = db(rms(full, 1.0, 2.0) / rms(none, 1.0, 2.0));
    const double after = db(rms(full, 3.0, 3.8) / rms(none, 3.0, 3.8));
    MESSAGE("ducking: " << during << " dB while playing, " << after << " dB after");
    CHECK(during < -15.0);
    CHECK(after > -2.0);
}

TEST_CASE("eras: the seventies are darker, the eighties have a short word length") {
    auto tail = [&](Era e) {
        auto p = wetOnly(Algo::Hall, 2.0f);
        p.era = e;
        return impulse(p, 2.0);
    };
    const auto now = tail(Era::Now), seventies = tail(Era::Seventies), eighties = tail(Era::Eighties);
    const auto hiNow = octave(now.l, 10000.0), hi70 = octave(seventies.l, 10000.0);
    const double darker = 10.0 * std::log10(energy(hi70, 0.1, 1.0) / energy(hiNow, 0.1, 1.0));
    MESSAGE("seventies at 10 kHz: " << darker << " dB");
    CHECK(darker < -12.0);
    // The mid of the tail keeps its decay in every era.
    CHECK(rt60(octave(seventies.l, 1000.0)) == doctest::Approx(2.0).epsilon(0.12));
    // Every wet sample of the eighties is a multiple of the 13-bit step.
    double worst = 0.0;
    for (size_t i = 0; i < eighties.l.size(); i += 3) {
        const double steps = eighties.l[i] * 4096.0;
        worst = std::max(worst, std::abs(steps - std::round(steps)));
    }
    CHECK(worst < 0.01);
}

TEST_CASE("early/late balance, attack and size do what they say") {
    // Early reflections only: all the energy is in the first 100 ms (times the size).
    auto p = wetOnly(Algo::Room, 1.5f);
    p.balance = 0.0f;
    auto ir = impulse(p, 1.0);
    CHECK(10.0 * std::log10(energy(ir.l, 0.1, 1.0) / energy(ir.l, 0.0, 0.1)) < -40.0);
    // Late only: hardly anything before the network answers.
    p.balance = 1.0f;
    ir = impulse(p, 1.0);
    CHECK(energy(ir.l, 0.0, 0.003) < 1e-6 * energy(ir.l, 0.0, 1.0));

    // Attack: the late part builds slowly, its loudest moment comes later.
    auto peakTime = [&](const std::vector<float>& x) {
        double best = 0.0, at = 0.0;
        for (double t = 0.0; t < 1.0; t += 0.01) {
            const double e = energy(x, t, t + 0.02);
            if (e > best) {
                best = e;
                at = t;
            }
        }
        return at;
    };
    auto a = wetOnly(Algo::Hall, 3.0f);
    const auto quick = impulse(a, 1.2);
    a.attack = 1.0f;
    const auto slow = impulse(a, 1.2);
    MESSAGE("loudest at " << peakTime(quick.l) << " s without attack, " << peakTime(slow.l) << " s with");
    CHECK(peakTime(slow.l) > peakTime(quick.l) + 0.15);
    CHECK(10.0 * std::log10(energy(slow.l, 0.0, 0.06) / energy(quick.l, 0.0, 0.06)) < -6.0);

    // Size: a bigger space answers later.
    auto s = wetOnly(Algo::Hall, 2.0f);
    s.size = 0.2f;
    const auto small = impulse(s, 0.5);
    s.size = 1.0f;
    const auto big = impulse(s, 0.5);
    const auto first = [](const std::vector<float>& x) {
        float m = 0.0f;
        for (auto v : x) m = std::max(m, std::abs(v));
        return onset(x, m * 0.05f);
    };
    CHECK(first(big.l) > first(small.l) * 2);
}

TEST_CASE("the cut filters shape the wet sound") {
    auto p = wetOnly(Algo::Hall, 2.0f);
    const auto open = impulse(p, 1.5);
    p.highCutHz = 2000.0f;
    p.lowCutHz = 600.0f;
    const auto cut = impulse(p, 1.5);
    CHECK(10.0 * std::log10(energy(octave(cut.l, 8000.0), 0.0, 1.5) / energy(octave(open.l, 8000.0), 0.0, 1.5)) < -18.0);
    CHECK(10.0 * std::log10(energy(octave(cut.l, 100.0), 0.0, 1.5) / energy(octave(open.l, 100.0), 0.0, 1.5)) < -15.0);
    CHECK(std::abs(10.0 * std::log10(energy(octave(cut.l, 1100.0), 0.0, 1.5) / energy(octave(open.l, 1100.0), 0.0, 1.5))) < 3.0);
}

TEST_CASE("algorithms come out at similar levels") {
    // Noise through each algorithm at a 2 s decay, wet only: within a few dB of each other.
    std::vector<double> levels;
    for (int a = 0; a < kAlgoCount; ++a) {
        auto p = wetOnly(static_cast<Algo>(a), 2.0f);
        if (static_cast<Algo>(a) == Algo::Nonlinear) p.decay = 0.6f;
        p.balance = 0.5f;
        auto rv = makeReverb(p);
        const size_t n = static_cast<size_t>(3.0 * kFs);
        auto l = noise(n, 0.3f, 1), r = noise(n, 0.3f, 2);
        run(*rv, l, r);
        levels.push_back(db(rms(l, 1.5, 3.0) / 0.3 * std::sqrt(3.0)));
        MESSAGE(algoKey(static_cast<Algo>(a)) << ": " << levels.back() << " dB against the input");
    }
    for (double l : levels) {
        CHECK(l > -9.0);
        CHECK(l < 6.0);
    }
}

TEST_CASE("processing allocates nothing, even while the algorithm changes") {
    auto p = wetOnly(Algo::Hall, 2.0f);
    auto rv = makeReverb(p);
    auto l = noise(512, 0.5f, 1), r = noise(512, 0.5f, 2);
    long total = 0;
    for (int k = 0; k < 400; ++k) {
        p.algo = static_cast<Algo>((k / 20) % kAlgoCount);
        p.era = static_cast<Era>((k / 30) % kEraCount);
        p.freeze = (k / 50) % 2 == 1;
        rv->setParams(p);
        wers::test::AllocationCounter counter;
        rv->process(l.data(), r.data(), 512);
        total += counter.stop();
    }
    CHECK(total == 0);
}
