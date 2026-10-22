// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The motion of Sway without LV2: Go and the settings in, sample by sample,
// the position p out (0 at Point A, 1 at Point B). The tests drive it
// directly. Nothing allocates or locks. docs/design.md is the spec.
namespace sway {
enum class Shape : int { sweep, jump, one_sweep, smooth_random, step_random, one_random };
struct Settings {
    int shape = 0;           // Shape, 0 to 5
    float point_a = 0, point_b = 1;
    float time = 1;          // seconds, with Sync off
    bool sync = false;
    float bpm = 120;
    int beats = 4;           // 1/4 beat to 8 bars, 0 to 7
    bool start = true;       // Start moving: read once, at the first sample
};

// Seconds one move takes: Time, or with Sync on Beats at BPM.
inline double move_time(const Settings& s) {
    static constexpr double beats[] = {0.25, 0.5, 1, 2, 4, 8, 16, 32};
    if (s.sync) return beats[std::clamp(s.beats, 0, 7)] * 60 / std::fmin(300.0, std::fmax(20.0, double(s.bpm)));
    return std::fmin(30.0, std::fmax(0.02, double(s.time)));
}
// Out for a position: A + (B - A) * p, with A and B within -1 to 1.
inline float output(float p, const Settings& s) {
    const float a = std::fmin(1.f, std::fmax(-1.f, s.point_a)), b = std::fmin(1.f, std::fmax(-1.f, s.point_b));
    return a + (b - a) * p;
}

// A trigger input: a rise above 0.4 fires, a fall to 0.05 or below re-arms.
struct Trigger {
    bool high = false;
    bool rise(float v) {
        if (!high && v > 0.4f) return high = true;
        if (high && v <= 0.05f) high = false;
        return false;
    }
};

class Motion {
public:
    Motion(double rate, uint32_t seed) : rate_(rate > 0 ? rate : 48000), rng_(seed ? seed : 0x9E3779B9u) {}
    // Back to A, as a patch loads; the random generator carries on.
    void reset() { const uint32_t rng = rng_; *this = Motion(rate_, rng); }

    // One sample: Go acts at this sample, then the position for this sample
    // is returned and the motion moves on by one sample.
    float step(float go, const Settings& s) {
        const auto shape = static_cast<Shape>(std::clamp(s.shape, 0, 5));
        if (!started_) {
            // The first sample learns Start moving, and a Go already high at
            // load is not a press.
            started_ = true;
            moving_ = s.start;
            go_.high = go > 0.4f;
            enter(shape);
        } else if (shape != shape_) enter(shape);
        if (go_.rise(go)) press();
        if (!moving_ && (shape_ == Shape::jump || shape_ == Shape::step_random)) p_ = 0;
        const auto now = static_cast<float>(p_);
        advance(1 / (move_time(s) * rate_));
        return now;
    }
    float position() const { return static_cast<float>(p_); }
    bool moving() const { return moving_; }

private:
    // A new shape keeps p and the moving or stopped state; a one-shot shape
    // rests where it is until the next Go.
    void enter(Shape shape) {
        shape_ = shape;
        elapsed_ = 0;
        resting_ = true;
        if (p_ >= 1) toward_ = 0;
        else if (p_ <= 0) toward_ = 1;
        if (shape == Shape::smooth_random) { from_ = p_; target_ = pick(p_); }
    }
    void press() {
        switch (shape_) {
        case Shape::one_sweep:
            // From rest, to the other end; mid-way, back the way it came.
            toward_ = resting_ ? (p_ < 0.5 ? 1 : 0) : 1 - toward_;
            resting_ = false;
            break;
        case Shape::one_random:
            from_ = p_; target_ = pick(p_); elapsed_ = 0;
            resting_ = false;
            break;
        default:
            // The repeating shapes start from where p is, heading for B.
            moving_ = !moving_;
            if (moving_) {
                toward_ = 1; elapsed_ = 0;
                if (shape_ == Shape::smooth_random) { from_ = p_; target_ = pick(p_); }
            }
        }
    }
    // `inc` is one sample as a fraction of a move.
    void advance(double inc) {
        switch (shape_) {
        case Shape::sweep:
            if (!moving_) glide(0, inc);
            else if (toward_ > 0.5) { p_ += inc; if (p_ >= 1) { p_ = std::max(0.0, 2 - p_); toward_ = 0; } }
            else { p_ -= inc; if (p_ <= 0) { p_ = std::min(1.0, -p_); toward_ = 1; } }
            break;
        case Shape::jump:
            if (moving_ && tick(inc)) { p_ = toward_; toward_ = 1 - toward_; }
            break;
        case Shape::one_sweep:
            if (!resting_ && glide(toward_, inc)) resting_ = true;
            break;
        case Shape::smooth_random:
            if (!moving_) { glide(0, inc); break; }
            if (tick(inc)) { from_ = target_; target_ = pick(from_); }
            p_ = from_ + (target_ - from_) * elapsed_;
            break;
        case Shape::step_random:
            if (moving_ && tick(inc)) p_ = pick(p_);
            break;
        case Shape::one_random:
            if (resting_) break;
            if (tick(inc)) { p_ = target_; resting_ = true; }
            else p_ = from_ + (target_ - from_) * elapsed_;
            break;
        }
    }
    // Moves p toward `to` at the move speed; true once there.
    bool glide(double to, double inc) {
        p_ = p_ < to ? std::min(to, p_ + inc) : std::max(to, p_ - inc);
        return p_ == to;
    }
    // Advances the timed shapes; true when a move of T ends. The remainder
    // carries on, so the timing holds over many moves.
    bool tick(double inc) {
        elapsed_ += inc;
        if (elapsed_ < 1 - 1e-9) return false;
        elapsed_ = std::clamp(elapsed_ - 1, 0.0, 0.999);
        return true;
    }
    // A random value in 0 to 1, at least 0.1 from `around`: one draw over
    // what is left once the values too close are cut out, so no loop.
    double pick(double around) {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        const double lo = std::max(0.0, around - 0.1), gap = std::min(1.0, around + 0.1) - lo;
        const double u = (rng_ >> 8) * (1.0 / 16777216) * (1 - gap);
        return u < lo ? u : u + gap;
    }

    double rate_;
    uint32_t rng_;
    bool started_ = false, moving_ = false, resting_ = true;
    Shape shape_ = Shape::sweep;
    Trigger go_;
    double p_ = 0, toward_ = 1, elapsed_ = 0, from_ = 0, target_ = 0;
};
}
