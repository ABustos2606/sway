// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
// The motion on its own: the trigger, move times, every shape, moving and
// stopped, changes while moving, Out, and extreme controls. Nothing in step()
// may allocate: operator new is counted.
#include "../src/motion.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace { long allocations = 0; }
void* operator new(std::size_t size) {
    ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace sway;
namespace {
constexpr int rate = 48000;
constexpr double one = 1.0 / rate;  // one sample of a 1 s move
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

struct Rig {
    explicit Rig(int shape, bool start = true, uint32_t seed = 1) : motion(rate, seed) { s.shape = shape; s.start = start; p.reserve(1 << 20); }
    Motion motion;
    Settings s;
    float go = 0;
    std::vector<float> p;
    // Runs `frames` samples and records p.
    void run(int frames) {
        const long before = allocations;
        for (int i = 0; i < frames; ++i) p.push_back(motion.step(go, s));
        require(allocations == before, "step allocates");
    }
    // A one-sample press, then Go low again.
    void press() { go = 1; run(1); go = 0; }
    float last() const { return p.back(); }
    size_t now() const { return p.size(); }
    // The biggest change of p from one sample to the next, from `from` on.
    double biggest_step(size_t from) const {
        double m = 0;
        for (size_t i = std::max<size_t>(from, 1); i < p.size(); ++i) m = std::max(m, std::fabs(double(p[i]) - p[i - 1]));
        return m;
    }
};

void trigger() {
    Rig r(int(Shape::one_sweep));
    r.go = 1; r.run(10); r.go = 0; r.run(10);
    require(r.last() == 0, "a Go already high at load is not a press");
    r.go = 0.4f; r.run(10);
    require(r.last() == 0, "0.4 is not a rise");
    r.go = 1; r.run(rate + 100);
    require(r.last() == 1, "a rise above 0.4 starts One sweep, and it rests at B");
    r.go = 0.2f; r.run(100); r.go = 1; r.run(100);
    require(r.last() == 1, "a held stomp is one press; a dip above 0.05 does not re-arm");
    r.go = 0.05f; r.run(10); r.go = 0.41f; r.run(10);
    require(r.last() < 1, "a fall to 0.05 re-arms");
}

void timing() {
    // Sweep, 1 s: B at sample 48000, A at 96000, and so on, within one sample.
    Rig sweep(int(Shape::sweep));
    sweep.run(10 * rate + 1);
    for (int k = 0; k <= 10; ++k)
        require(near(sweep.p[size_t(k) * rate], k % 2, one + 1e-6), "Sweep at " + std::to_string(k) + " s: " + std::to_string(sweep.p[size_t(k) * rate]));
    require(near(sweep.p[rate / 2], 0.5, 1e-6) && near(sweep.p[rate + rate / 4], 0.75, 1e-6), "Sweep is a straight line");
    // Jump, 1 s: 0 for 48000 samples, then 1, and so on.
    Rig jump(int(Shape::jump));
    jump.run(10 * rate + 2);
    for (int k = 1; k <= 10; ++k) {
        const size_t edge = size_t(k) * rate;
        require(jump.p[edge - 2] == (k + 1) % 2 && jump.p[edge + 1] == k % 2, "Jump edge " + std::to_string(k) + " within one sample");
    }
    // Sync: 1 beat at 120 BPM is 0.5 s; 1 bar at 60 BPM is 4 s.
    Rig beat(int(Shape::sweep));
    beat.s.sync = true; beat.s.bpm = 120; beat.s.beats = 2;
    beat.run(rate + 1);
    require(near(beat.p[rate / 2], 1, 2 * one) && near(beat.p[rate], 0, 2 * one), "Sync: 1 beat at 120 BPM");
    beat.s.time = 7;  // ignored with Sync on
    require(near(move_time(beat.s), 0.5, 1e-12), "Time is ignored with Sync on");
    beat.s.bpm = 60; beat.s.beats = 4;
    require(near(move_time(beat.s), 4, 1e-12), "a bar is 4 beats");
    beat.s.beats = 0; beat.s.bpm = 300;
    require(near(move_time(beat.s), 0.05, 1e-12), "1/4 beat at 300 BPM");
    beat.s.sync = false;
    require(near(move_time(beat.s), 7, 1e-12), "Time with Sync off");
}

void one_sweep() {
    Rig r(int(Shape::one_sweep));
    r.run(1000);
    require(r.last() == 0, "One sweep rests at A");
    r.press(); r.run(rate / 2 - 1);
    require(near(r.last(), 0.5, 2 * one), "half a move after Go, half way");
    const size_t turned = r.now();
    r.press();
    while (r.last() > 0 && r.now() < turned + 2 * size_t(rate)) r.run(1);
    require(near(double(r.now() - turned), rate / 2, 2), "the return takes as long as the way there: " + std::to_string(r.now() - turned));
    r.run(1000);
    require(r.last() == 0, "and rests at A");
    r.press(); r.run(rate + 10);
    require(r.last() == 1, "a full move to B");
    r.press(); r.run(rate + 10);
    require(r.last() == 0, "and back to A");
    require(r.biggest_step(0) <= one + 1e-6, "One sweep never jumps");
}

void randoms() {
    const int T = rate / 10;
    // Smooth random: continuous, a new target every T, targets 0.1 apart.
    Rig smooth(int(Shape::smooth_random));
    smooth.s.time = 0.1f;
    smooth.run(100 * T + 1);
    for (float v : smooth.p) require(v >= 0 && v <= 1, "Smooth random within 0 to 1");
    require(smooth.biggest_step(0) <= 1.0 / T + 1e-6, "Smooth random is continuous");
    for (int k = 1; k <= 100; ++k)
        require(std::fabs(double(smooth.p[size_t(k) * T]) - smooth.p[size_t(k - 1) * T]) >= 0.1 - 2.0 / T, "Smooth random targets at least 0.1 apart");
    // Step random: holds for T, then a new value at least 0.1 away.
    Rig steps(int(Shape::step_random));
    steps.s.time = 0.1f;
    steps.run(100 * T + 2);
    for (int k = 1; k <= 100; ++k) {
        const size_t edge = size_t(k) * T;
        require(steps.p[edge - 2] == steps.p[edge - T + 1], "Step random holds for T");
        require(std::fabs(double(steps.p[edge + 1]) - steps.p[edge - 2]) >= 0.1, "Step random values at least 0.1 apart, every T");
        require(steps.p[edge + 1] >= 0 && steps.p[edge + 1] <= 1, "Step random within 0 to 1");
    }
    // One random: rests; a Go glides to a new point over T; a Go mid-way starts a new glide from there.
    Rig once(int(Shape::one_random));
    once.s.time = 0.1f;
    once.run(1000);
    require(once.last() == 0, "One random rests at load");
    once.press(); once.run(T + 10);
    const float target = once.last();
    require(target >= 0.1f && target <= 1, "One random goes at least 0.1 away");
    require(once.p[once.now() - 1] == once.p[once.now() - 5], "and rests there");
    once.press(); once.run(T / 2);
    once.press(); once.run(T + 10);
    require(once.biggest_step(0) <= 1.0 / T + 1e-6, "One random glides, also when Go comes mid-way");
    // Two instances, two sequences.
    Rig other(int(Shape::step_random), true, 2);
    other.s.time = 0.1f;
    other.run(3 * T);
    require(other.p[2 * T + 1] != steps.p[2 * T + 1], "instances do not share a random sequence");
}

void moving_and_stopped() {
    Rig r(int(Shape::sweep), false);
    r.run(10000);
    require(r.last() == 0, "Start moving Off: Sweep rests at A");
    r.s.start = true; r.run(1000);
    require(r.last() == 0, "Start moving is read at load only");
    r.press(); r.run(rate / 2);
    require(near(r.last(), 0.5, 2 * one), "Go starts Sweep, heading for B");
    r.press(); r.run(rate / 4);
    require(!r.motion.moving() && near(r.last(), 0.25, 2 * one), "Go stops it; it glides back to A");
    r.press(); r.run(rate / 4);
    require(r.motion.moving() && near(r.last(), 0.5, 2 * one), "a restart goes on from where p is, heading for B");
    r.press(); r.run(rate);
    require(r.last() == 0, "stopped, it rests at A");
    require(r.biggest_step(0) <= one + 1e-6, "Sweep never jumps, stopping or starting");
    Rig started(int(Shape::sweep));
    started.run(100);
    require(started.motion.moving() && started.last() > 0, "Start moving On: Sweep moves at load");
    // Jump and Step random stop at A at once.
    for (const Shape shape : {Shape::jump, Shape::step_random}) {
        Rig j{int(shape)};
        j.run(rate + 100);
        require(j.last() > 0, "moving, away from A");
        j.press();
        require(j.last() == 0 && !j.motion.moving(), "stopped, at A at once");
        j.run(3 * rate);
        require(j.last() == 0, "and stays at A");
        j.press(); j.run(rate - 10);
        require(j.last() == 0, "a restart begins from A for one move");
        j.run(20);
        require(j.last() > 0, "then leaves it");
    }
    // Smooth random glides back.
    Rig s(int(Shape::smooth_random));
    s.run(rate / 2);
    s.press(); s.run(rate);
    require(s.last() == 0 && s.biggest_step(0) <= one + 1e-6, "Smooth random glides back to A when stopped");
}

void changes() {
    // Speed changes: p never jumps.
    Rig r(int(Shape::sweep));
    r.run(rate / 3);
    r.s.time = 0.05f; r.run(rate / 7);
    r.s.sync = true; r.s.beats = 7; r.run(rate / 5);
    r.s.beats = 0; r.s.bpm = 300; r.run(rate / 5);
    require(r.biggest_step(0) <= 1 / (0.05 * rate) + 1e-6, "speed changes never make p jump");
    Rig s(int(Shape::smooth_random));
    s.run(rate / 3);
    s.s.time = 0.2f; s.run(rate / 2);
    s.s.time = 3; s.run(rate);
    require(s.biggest_step(0) <= 1 / (0.2 * rate) + 1e-6, "nor in Smooth random");
    // Shape changes keep p and the moving or stopped state.
    Rig c(int(Shape::sweep));
    c.run(rate / 3);
    const float p = c.last();
    c.s.shape = int(Shape::smooth_random); c.run(1);
    require(std::fabs(c.last() - p) <= one + 1e-6, "Sweep to Smooth random keeps p");
    c.s.shape = int(Shape::one_sweep); c.run(1000);
    const float rest = c.last();
    c.run(1000);
    require(c.last() == rest && c.motion.moving(), "a one-shot shape rests where it is; moving is kept");
    c.s.shape = int(Shape::jump); c.run(rate - 10);
    require(c.last() == rest, "Jump holds p for one move");
    c.run(20);
    require(c.last() == 1 || c.last() == 0, "then jumps to an end");
    c.s.shape = int(Shape::sweep); c.press();
    require(!c.motion.moving(), "stopped");
    c.s.shape = int(Shape::step_random); c.run(1);
    require(c.last() == 0 && !c.motion.moving(), "a stopped Step random is at A");
}

void out() {
    Settings s;
    s.point_a = 0.2f; s.point_b = -0.6f;
    require(near(output(0, s), 0.2, 1e-6) && near(output(1, s), -0.6, 1e-6) && near(output(0.5f, s), -0.2, 1e-6), "Out = A + (B - A) * p, B below A");
    s.point_a = 5; s.point_b = -5;
    require(output(0, s) == 1 && output(1, s) == -1, "A and B within -1 to 1");
}

void extremes() {
    const float bad[] = {NAN, INFINITY, -INFINITY, -5, 0, 1e9f};
    for (int shape = -3; shape <= 9; ++shape)
        for (const float v : bad) {
            Rig r(shape);
            r.s.time = v; r.s.bpm = v; r.s.beats = shape * 3; r.go = v;
            r.run(500); r.go = 0; r.run(10); r.go = 1; r.run(500);
            r.s.sync = true; r.run(500);
            for (float p : r.p) require(std::isfinite(p) && p >= 0 && p <= 1, "extreme controls keep p within 0 to 1");
        }
    Settings s;
    s.point_a = NAN;
    s.time = NAN;
    require(near(move_time(s), 0.02, 1e-12), "a non-finite Time is clamped");
}
}

int main() {
    try {
        trigger(); timing(); one_sweep(); randoms(); moving_and_stopped(); changes(); out(); extremes();
        std::puts("PASS test_motion: trigger, timing, One sweep, random shapes, moving and stopped, changes, Out, extremes");
    } catch (const std::exception& e) { std::printf("FAIL test_motion: %s\n", e.what()); return 1; }
}
