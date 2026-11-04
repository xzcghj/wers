// Renders a vocal through Fonem and Przestrzen with given settings, block by block like a host.
// Usage: wers_render in.wav out.wav density clarity attack breath character space width [bpm] [block]
#include "WavIO.h"
#include "dsp/Fonem.h"
#include "dsp/Space.h"
#include <cstdlib>
#include <iostream>
using namespace wers;

int main(int argc, char** argv) {
    if (argc < 10) { std::cerr << "usage: wers_render in.wav out.wav density clarity attack breath character space width [bpm] [block]\n"; return 2; }
    wav::Audio in;
    if (!wav::read(argv[1], in)) { std::cerr << "cannot read input\n"; return 1; }
    const auto arg = [&](int i) { return static_cast<float>(std::atof(argv[i])); };
    const double bpm = argc > 10 ? std::atof(argv[10]) : 120.0;
    const int block = argc > 11 ? std::atoi(argv[11]) : 512;
    Fonem fonem; fonem.prepare(in.rate);
    Space space; space.prepare(in.rate);
    fonem.setParams({ arg(3), arg(4), arg(5), arg(6), arg(7) });
    space.setParams({ arg(8), arg(9) });
    std::vector<float> l = in.ch.at(0), r = in.ch.size() > 1 ? in.ch[1] : in.ch[0];
    // Silence after the end lets the delay and the tail finish.
    const size_t n = l.size() + static_cast<size_t>(in.rate) * 3;
    l.resize(n, 0.0f); r.resize(n, 0.0f);
    for (size_t p = 0; p < n; p += static_cast<size_t>(block)) {
        const int m = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), n - p));
        float* ch[2] = { l.data() + p, r.data() + p };
        fonem.process(ch, 2, m);
        space.process(ch, 2, m, bpm);
    }
    wav::Audio out; out.rate = in.rate; out.channels = 2; out.ch = { l, r };
    if (!wav::write(argv[2], out)) { std::cerr << "cannot write output\n"; return 1; }
    std::cout << "latency " << fonem.latencySamples() << "\n";
    return 0;
}
