#include <doctest/doctest.h>

#include <cmath>

#include <pdfview/ScrollMotion.h>

using namespace pdfview;

namespace {

using Vec = ScrollMotion::Vec;

// runs at a fixed frame rate until rest; returns the seconds taken
double settle(ScrollMotion& m, Vec& pos, double frame, double* peak = nullptr, double* furthest = nullptr)
{
    double t = 0;
    while (m.active() && t < 10) {
        m.step(pos, frame, 0.5);
        t += frame;
        if (peak) *peak = std::max(*peak, std::hypot(m.velocity().x, m.velocity().y));
        if (furthest) *furthest = std::max(*furthest, pos.y);
    }
    return t;
}

} // namespace

TEST_CASE("a step starts at full speed and lands without overshoot")
{
    ScrollMotion m;
    Vec pos;
    m.moveTo(pos, {0, 80}, {18, 1, 1600});
    CHECK(m.velocity().y == doctest::Approx(18 * 80));
    double furthest = 0;
    double t = settle(m, pos, 1 / 60.0, nullptr, &furthest);
    CHECK(pos.y == 80);
    CHECK(furthest <= 80);
    CHECK(t < 0.35);
}

TEST_CASE("a screenful speeds up from rest, keeps under the speed cap and lands")
{
    ScrollMotion m;
    Vec pos;
    m.moveTo(pos, {0, 900}, {20, 0, 1600});
    CHECK(m.velocity().y == 0);
    double peak = 0, furthest = 0;
    double t = settle(m, pos, 1 / 60.0, &peak, &furthest);
    CHECK(pos.y == 900);
    CHECK(furthest <= 900);
    CHECK(peak <= 1600 + 1e-9);
    CHECK(peak > 1500); // it reaches cruising speed
    CHECK(t < 1.2);
}

TEST_CASE("the path does not depend on the frame rate")
{
    auto at = [](int fps, double seconds) {
        ScrollMotion m;
        Vec pos;
        m.moveTo(pos, {0, 900}, {20, 0, 1600});
        m.step(pos, 0, 0.5); // past the first-frame cap
        for (int i = 0; i < static_cast<int>(std::lround(seconds * fps)); i++) m.step(pos, 1.0 / fps, 0.5);
        return pos.y;
    };
    CHECK(at(30, 0.3) == doctest::Approx(at(150, 0.3)).epsilon(0.002));
    CHECK(at(30, 0.6) == doctest::Approx(at(150, 0.6)).epsilon(0.002));
}

TEST_CASE("a new step mid-glide keeps the speed continuous")
{
    ScrollMotion m;
    Vec pos;
    m.moveTo(pos, {0, 900}, {20, 0, 1600});
    for (int i = 0; i < 10; i++) m.step(pos, 1 / 60.0, 0.5);
    double before = m.velocity().y;
    // a screenful adds no impulse; the target moves on
    m.moveTo(pos, {0, 1800}, {20, 0, 1600});
    CHECK(m.velocity().y == before);
    CHECK(m.target().y == 1800);
}

TEST_CASE("steps add up and a reversal turns back smoothly")
{
    ScrollMotion m;
    Vec pos;
    m.moveTo(pos, {0, 80}, {18, 1, 1600});
    m.step(pos, 1 / 60.0, 0.5);
    m.moveTo(pos, {0, 160}, {18, 1, 1600});
    CHECK(m.velocity().y <= 1600);
    CHECK(m.target().y == 160);
    // back past the start
    m.moveTo(pos, {0, -80}, {18, 1, 1600});
    CHECK(m.velocity().y < 0);
    settle(m, pos, 1 / 60.0);
    CHECK(pos.y == -80);
}

TEST_CASE("stop drops the motion")
{
    ScrollMotion m;
    Vec pos;
    m.moveTo(pos, {0, 80}, {});
    m.stop();
    CHECK(!m.active());
    CHECK(!m.step(pos, 1 / 60.0, 0.5));
    CHECK(pos.y == 0);
}

TEST_CASE("a target kept one braking distance ahead cruises and stops soon after release")
{
    ScrollMotion m;
    Vec pos;
    ScrollMotion::Params p{20, 0, 1600};
    double ahead = 1.25 * ScrollMotion::brakingDistance(p);
    // held for a second
    for (int i = 0; i < 60; i++) {
        if (pos.y + ahead > m.target().y || !m.active()) m.moveTo(pos, {0, pos.y + ahead}, p);
        m.step(pos, 1 / 60.0, 0.5);
    }
    CHECK(m.velocity().y == doctest::Approx(1600).epsilon(0.02));
    // released
    double released = pos.y, furthest = 0;
    double t = settle(m, pos, 1 / 60.0, nullptr, &furthest);
    CHECK(pos.y - released <= ahead);
    CHECK(furthest <= m.target().y + 1e-9);
    CHECK(t < 0.5);
}

TEST_CASE("the first step after rest counts as one 60 Hz frame at most")
{
    ScrollMotion a, b;
    Vec pa, pb;
    a.moveTo(pa, {0, 80}, {});
    b.moveTo(pb, {0, 80}, {});
    a.step(pa, 2.0, 0.5); // after a long idle wait
    b.step(pb, 1 / 60.0, 0.5);
    CHECK(pa.y == pb.y);
    CHECK(pa.y < 80);
}

TEST_CASE("the autoscroll speed has a dead zone, a rise and a limit")
{
    CHECK(autoscrollSpeed(5, 1) == 0);
    CHECK(autoscrollSpeed(-5, 1) == 0);
    CHECK(autoscrollSpeed(50, 1) > 0);
    CHECK(autoscrollSpeed(-50, 1) == -autoscrollSpeed(50, 1));
    CHECK(autoscrollSpeed(200, 1) > autoscrollSpeed(100, 1));
    CHECK(autoscrollSpeed(1e6, 1) == 5000);
    CHECK(autoscrollSpeed(100, 2) == doctest::Approx(2 * autoscrollSpeed(50, 1)));
}

TEST_CASE("a middle click latches autoscroll and a drag stops on release")
{
    Autoscroll a;
    a.start({100, 100});
    a.track({101, 101}, true);
    a.track({101, 101}, false);
    CHECK(a.on());
    a.track({100, 300}, false); // moving afterwards keeps it on
    CHECK(a.on());
    CHECK(a.velocity({100, 300}, 1).y > 0);
    CHECK(a.velocity({100, 300}, 1).x == 0);
    a.stop();
    CHECK(!a.on());
    CHECK(a.velocity({100, 300}, 1).y == 0);

    Autoscroll b;
    b.start({100, 100});
    b.track({100, 200}, true);
    CHECK(b.on());
    b.track({100, 200}, false);
    CHECK(!b.on());

    // the drag threshold follows the UI scale
    Autoscroll c;
    c.start({0, 0});
    c.track({0, 5}, true, 2);
    c.track({0, 5}, false, 2);
    CHECK(c.on());
}
