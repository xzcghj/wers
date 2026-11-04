#include "doctest.h"
#include "TestSignals.h"
#include "dsp/Filters.h"
using namespace wers;
using namespace wers::test;

TEST_CASE("svf bell has the requested gain at its centre and matches the analytic response") {
    const double fs = 48000;
    for (double gainDb : { -6.0, -3.0, 4.0 }) {
        Svf f; f.set(1000.0, kCutQ, fs);
        const double g = dbToGainD(gainDb);
        for (double probe : { 1000.0, 300.0, 3000.0 }) {
            f.reset();
            auto x = sine(probe, -20, 48000, fs);
            std::vector<float> y(x.size());
            for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(x[i] + (g - 1.0) * f.band(x[i]));
            const double measured = toneDb(y.data() + 24000, 24000, probe, fs) + 20.0;
            const double predicted = 20.0 * std::log10(svfBellMagnitude(probe, 1000.0, kCutQ, g, fs));
            CHECK(measured == doctest::Approx(predicted).epsilon(0.03));
        }
    }
}

TEST_CASE("oversampler round trip has the reported latency and passes the audio band") {
    Halfband2x os;
    auto x = sine(1000, -6, 4800, 48000);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) { float a, b; os.up(x[i], a, b); y[i] = os.down(a, b); }
    double err = 0;
    for (size_t i = 1000; i < x.size(); ++i) { const double d = y[i] - x[i - Halfband2x::kLatency]; err += d * d; }
    CHECK(10.0 * std::log10(err / 3800.0 + 1e-30) < -70.0);
}

TEST_CASE("delay line delays by exactly the requested amount") {
    DelayLine d; d.prepare(5);
    std::vector<float> out;
    for (int i = 0; i < 12; ++i) out.push_back(d.process(static_cast<float>(i + 1)));
    CHECK(out[4] == 0.0f);
    CHECK(out[5] == 1.0f);
    CHECK(out[11] == 7.0f);
}
