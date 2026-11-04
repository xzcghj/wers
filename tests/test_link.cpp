#include "doctest.h"
#include "TestSignals.h"
#include "dsp/VocalFix.h"
#include "link/GroupControl.h"
#include "link/LinkHub.h"
#include <thread>
using namespace wers;
using namespace wers::test;

TEST_CASE("capture puts a signal at its timeline position at any host rate") {
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        TimelineStore st(30);
        st.prepare(fs);
        REQUIRE(st.arm());
        // A click-like burst at 2.0 s on the timeline; blocks start at 1.0 s.
        const int n = static_cast<int>(fs * 2);
        std::vector<float> x(static_cast<size_t>(n), 0.0f);
        const int at = static_cast<int>(fs * 1.0);
        for (int i = 0; i < static_cast<int>(fs * 0.01); ++i) x[static_cast<size_t>(at + i)] = 0.5f * std::sin(2.0f * 3.14159f * 1000.0f * i / static_cast<float>(fs));
        const auto start = static_cast<int64_t>(fs);
        for (int pos = 0; pos < n; pos += 512) st.capture(x.data() + pos, std::min(512, n - pos), start + pos);
        st.disarm();
        const auto out = st.copy();
        REQUIRE(out.size() >= 48000u);
        int peak = 0;
        for (int i = 0; i < static_cast<int>(out.size()); ++i) if (std::abs(out[static_cast<size_t>(i)]) > std::abs(out[static_cast<size_t>(peak)])) peak = i;
        CHECK(peak >= 31995); CHECK(peak <= 32190); // inside the 10 ms burst, filter delay included
        CHECK(st.heardSeconds() == doctest::Approx(2.0).epsilon(0.01));
        CHECK(std::abs(out[8000]) == 0.0f); // before the first block nothing was written
    }
}

TEST_CASE("loops and jumps overwrite the right place") {
    const double fs = 48000;
    TimelineStore st(30); st.prepare(fs); st.arm();
    auto a = sine(1000, -12, 48000, fs), b = sine(3000, -12, 48000, fs);
    st.capture(a.data(), 48000, 0);          // 0..1 s with tone a
    st.capture(a.data(), 48000, 96000);      // 2..3 s with tone a
    st.capture(b.data(), 48000, 96000);      // jump back: 2..3 s again with tone b
    st.disarm();
    const auto y = st.copy();
    REQUIRE(y.size() >= 48000u);
    CHECK(toneDb(y.data() + 4000, 8000, 1000, 16000) == doctest::Approx(-12.0).epsilon(0.05));
    CHECK(toneDb(y.data() + 36000, 8000, 3000, 16000) == doctest::Approx(-12.0).epsilon(0.05));
    CHECK(toneDb(y.data() + 36000, 8000, 1000, 16000) < -60.0);
    CHECK(rmsDb(y, 18000, 30000) < -120.0); // the unplayed second stays empty
}

TEST_CASE("arming and disarming from another thread while audio runs is safe") {
    TimelineStore st(10); st.prepare(48000);
    std::atomic<bool> stop{ false };
    std::thread audio([&] {
        std::array<float, 256> buf{};
        int64_t pos = 0;
        while (!stop.load()) { st.capture(buf.data(), 256, pos); pos += 256; if (pos > 48000 * 8) pos = 0; }
    });
    for (int i = 0; i < 20; ++i) {
        st.arm();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        st.disarm();
        const auto c = st.copy();
        CHECK(c.size() <= 160000u);
    }
    stop = true;
    audio.join();
}

TEST_CASE("plan fix ramps sit around the word and consonants") {
    Plan p;
    FixEvent e; e.t0 = 1.0; e.t1 = 1.5; e.beatCutDb.fill(4.0f); e.vocalBoostDb.fill(3.0f);
    e.consonants.emplace_back(1.1f, 1.2f);
    p.events.push_back(e);
    std::array<float, kCutBands> cut{}; std::array<float, kBoostBands> boost{};
    CHECK(!p.fixCutsAt(0.9, cut.data()));
    REQUIRE(p.fixCutsAt(1.0, cut.data()));   // already ramped up at the word start
    CHECK(cut[0] == doctest::Approx(4.0f));
    REQUIRE(p.fixCutsAt(1.3, cut.data()));
    CHECK(!p.fixCutsAt(1.6, cut.data()));
    CHECK(!p.vocalBoostAt(1.05, boost.data()));
    REQUIRE(p.vocalBoostAt(1.15, boost.data()));
    CHECK(boost[0] == doctest::Approx(3.0f));
    CHECK(!p.vocalBoostAt(1.3, boost.data()));
}

TEST_CASE("vocal band map looks ahead") {
    Plan p;
    p.mapFrames = 100;
    p.vocalMap.assign(100 * kCutBands, Plan::quantise(-80.0f));
    for (int f = 50; f < 60; ++f) for (int j = 0; j < kCutBands; ++j) p.vocalMap[static_cast<size_t>(f * kCutBands + j)] = Plan::quantise(-20.0f);
    std::array<float, kCutBands> v{};
    REQUIRE(p.vocalBandsAt(frameCentre(48), v.data()));
    CHECK(v[0] == doctest::Approx(-20.0f));
    REQUIRE(p.vocalBandsAt(frameCentre(45), v.data()));
    CHECK(v[0] == doctest::Approx(-80.0f));
    CHECK(!p.vocalBandsAt(frameCentre(200), v.data()));
    CHECK(!p.vocalBandsAt(-1.0, v.data()));
}

TEST_CASE("vocal fix applies only in its place and bypass is exact") {
    const double fs = 48000; const int n = 96000;
    Plan p;
    FixEvent e; e.t0 = 0.5; e.t1 = 1.5; e.vocalBoostDb = { 0.0f, 4.0f, 0.0f, 0.0f };
    p.events.push_back(e);
    auto x = sine(3500, -20, n, fs);
    SUBCASE("active") {
        VocalFix f; f.prepare(fs);
        auto y = x;
        inBlocks(y, 128, [&](float** c, int k, int64_t pos) { f.process(c, 1, k, pos, true, &p, true); });
        CHECK(toneDb(y.data() + 4000, 16000, 3500, fs) == doctest::Approx(-20.0).epsilon(0.002));
        CHECK(toneDb(y.data() + 40000, 16000, 3500, fs) == doctest::Approx(-16.0).epsilon(0.01));
        CHECK(toneDb(y.data() + 76000, 16000, 3500, fs) == doctest::Approx(-20.0).epsilon(0.002));
    }
    SUBCASE("disabled, stopped, or no position stay bit exact") {
        for (int mode = 0; mode < 4; ++mode) {
            VocalFix f; f.prepare(fs);
            auto y = x;
            inBlocks(y, 128, [&](float** c, int k, int64_t pos) {
                f.process(c, 1, k, mode == 2 ? -1 : pos, mode != 1, mode == 3 ? nullptr : &p, mode != 0);
            });
            CHECK(y == x);
        }
    }
}

TEST_CASE("group control combines plan, ring and stale state") {
    auto hub = LinkHub::get();
    LinkGroup& g = hub->group(7);
    const int slot = g.claimSlot();
    REQUIRE(slot >= 0);
    GroupPlaceControl ctl(g);
    std::array<float, kCutBands> v{};
    CHECK(!ctl.vocalBands(1.0, v.data()));
    std::array<float, kCutBands> lv{}; lv.fill(-33.0f);
    g.ring(slot).write(2.0, lv.data());
    REQUIRE(ctl.vocalBands(2.0, v.data()));
    CHECK(v[3] == doctest::Approx(-33.0f));
    CHECK(ctl.vocalBands(2.02, v.data()));
    CHECK(!ctl.vocalBands(2.2, v.data()));

    auto plan = std::make_shared<Plan>();
    plan->mapFrames = 1000;
    plan->vocalMap.assign(1000 * kCutBands, Plan::quantise(-25.0f));
    FixEvent e; e.t0 = 3.0; e.t1 = 3.5; e.beatCutDb.fill(5.0f);
    plan->events.push_back(e);
    g.publishPlan(slot, plan);
    REQUIRE(ctl.vocalBands(3.2, v.data()));
    CHECK(v[0] == doctest::Approx(-25.0f));
    REQUIRE(ctl.fixCuts(3.2, v.data()));
    CHECK(v[0] == doctest::Approx(5.0f));
    g.setFixesEnabled(false);
    CHECK(!ctl.fixCuts(3.2, v.data()));
    g.setFixesEnabled(true);
    g.setStale(slot, true);
    CHECK(!ctl.fixCuts(3.2, v.data()));
    CHECK(!ctl.vocalBands(3.2, v.data()));
    g.setStale(slot, false);
    g.releaseSlot(slot);
    CHECK(!ctl.fixCuts(3.2, v.data()));
}

TEST_CASE("stale detector stays quiet for the analysed vocal and fires for another one") {
    Plan p;
    Lcg r;
    const int frames = 3000;
    p.levelTrack.resize(frames);
    std::vector<float> other(frames);
    float a = -30, b = -30;
    for (int i = 0; i < frames; ++i) {
        if (i % 37 == 0) a = -25.0f - 25.0f * std::abs(r.next());
        if (i % 29 == 0) b = -25.0f - 25.0f * std::abs(r.next());
        p.levelTrack[static_cast<size_t>(i)] = a;
        other[static_cast<size_t>(i)] = b;
    }
    StaleDetector same, diff;
    same.prepare(48000); diff.prepare(48000);
    for (int i = 0; i < frames; ++i) {
        const double t = frameCentre(i);
        same.feed(t, p.levelTrack[static_cast<size_t>(i)] + 2.0f * r.next(), &p);
        diff.feed(t, other[static_cast<size_t>(i)], &p);
    }
    CHECK(!same.stale());
    CHECK(diff.stale());
}

TEST_CASE("open projects do not share their groups") {
    auto hub = LinkHub::get();
    int a = 0, b = 0;
    CHECK(&hub->group(2) == &hub->group(2));
    CHECK(&hub->group(2, &a) == &hub->group(2, &a));
    CHECK(&hub->group(2, &a) != &hub->group(2, &b));
    CHECK(&hub->group(2, &a) != &hub->group(2));
    CHECK(&hub->group(2, &a) != &hub->group(3, &a));
    CHECK(&hub->group(100, &a) == &hub->group(kLinkGroups - 1, &a));
}
