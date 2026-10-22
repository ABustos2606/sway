// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
#include "motion.hpp"
#include "ports.hpp"
#include <lv2/core/lv2.h>
#include <cmath>
#include <cstdint>
#include <new>

namespace sway {
namespace {
constexpr const char* uri = "https://github.com/ABustos2606/sway#sway";
constexpr uint32_t first_control = static_cast<uint32_t>(Port::shape), controls = static_cast<uint32_t>(Port::position) - first_control;
float control(const float* port, float fallback) { return port && std::isfinite(*port) ? *port : fallback; }
float within(const float* port, float fallback, float lo, float hi) { return std::fmin(hi, std::fmax(lo, control(port, fallback))); }
int whole(const float* port, float fallback, float lo, float hi) { return static_cast<int>(std::lround(within(port, fallback, lo, hi))); }
// Each instance its own random sequence, from its address.
uint32_t seed_from(const void* address) {
    const uint64_t x = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(address)) * 0x9E3779B97F4A7C15ull;
    return static_cast<uint32_t>(x >> 32) | 1;
}

struct Plugin {
    explicit Plugin(double rate) : motion(rate, seed_from(this)) {}
    Motion motion;
    float* out = nullptr;
    const float* go = nullptr;
    const float* settings[controls]{};
    float* position = nullptr;
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*) {
    if (!(rate > 0)) return nullptr;
    return new (std::nothrow) Plugin(rate);
}
void connect_port(LV2_Handle handle, uint32_t port, void* data) {
    auto& self = *static_cast<Plugin*>(handle);
    if (port == static_cast<uint32_t>(Port::out)) self.out = static_cast<float*>(data);
    else if (port == static_cast<uint32_t>(Port::go)) self.go = static_cast<const float*>(data);
    else if (port >= first_control && port < first_control + controls) self.settings[port - first_control] = static_cast<const float*>(data);
    else if (port == static_cast<uint32_t>(Port::position)) self.position = static_cast<float*>(data);
}
void activate(LV2_Handle handle) { static_cast<Plugin*>(handle)->motion.reset(); }
// Per sample, so a slice simply carries on where the last one ended
// (see docs/design.md: Ingen runs a block in slices).
void run(LV2_Handle handle, uint32_t frames) {
    auto& self = *static_cast<Plugin*>(handle);
    Settings s;
    s.shape = whole(self.settings[0], 0, 0, 5);
    s.point_a = within(self.settings[1], 0, -1, 1);
    s.point_b = within(self.settings[2], 1, -1, 1);
    s.time = within(self.settings[3], 1, 0.02f, 30);
    s.sync = control(self.settings[4], 0) >= 0.5f;
    s.bpm = within(self.settings[5], 120, 20, 300);
    s.beats = whole(self.settings[6], 4, 0, 7);
    s.start = control(self.settings[7], 1) >= 0.5f;
    float p = self.motion.position();
    for (uint32_t i = 0; i < frames; ++i) {
        p = self.motion.step(self.go ? self.go[i] : 0, s);
        if (self.out) self.out[i] = output(p, s);
    }
    if (self.position) *self.position = p;
}
void cleanup(LV2_Handle handle) { delete static_cast<Plugin*>(handle); }
const LV2_Descriptor descriptor = {uri, instantiate, connect_port, activate, run, nullptr, cleanup, nullptr};
static_assert(controls == 8, "eight controls between Go and Position");
}
}

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return index == 0 ? &sway::descriptor : nullptr;
}
