// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
// The built plugin, run as Ingen runs it: a period split into slices at
// random points and at each control change, each slice's CV pointers moved to
// the slice. Out must match the motion stepped directly, sample for sample:
// each Go acts at its own frame, each control change at its own slice. Then
// the same without slicing, as other hosts run it.
//   test_lv2 build/sway.lv2/sway.so
#include "../src/motion.hpp"
#include "../src/ports.hpp"
#include <lv2/core/lv2.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using sway::Port;
namespace {
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
constexpr uint32_t block = 128, controls = 8;
// shape, point_a, point_b, time, sync, bpm, beats, start
using Controls = std::array<float, controls>;
sway::Settings settings(const Controls& c) {
    sway::Settings s;
    s.shape = int(c[0]); s.point_a = c[1]; s.point_b = c[2]; s.time = c[3];
    s.sync = c[4] >= 0.5f; s.bpm = c[5]; s.beats = int(c[6]); s.start = c[7] >= 0.5f;
    return s;
}
struct Change { uint64_t frame; uint32_t control; float value; };

struct Host {
    Host(const LV2_Descriptor* d, bool slicing) : descriptor(d), slicing(slicing) {
        handle = d->instantiate(d, 48000, ".", features);
        require(handle, "instantiate");
        d->activate(handle);
    }
    ~Host() { if (descriptor->deactivate) descriptor->deactivate(handle); descriptor->cleanup(handle); }
    // One period of Go. Sliced, a control change starts a slice at its frame,
    // as in Ingen; unsliced, it waits for the next period. Returns the frames
    // at which the changes were applied.
    std::vector<std::pair<uint64_t, Change>> period(const std::vector<float>& go, const std::vector<Change>& changes) {
        std::vector<std::pair<uint64_t, Change>> applied;
        std::vector<uint32_t> cuts;
        for (const auto& c : changes)
            if (c.frame >= frames && c.frame < frames + block) cuts.push_back(slicing ? uint32_t(c.frame - frames) : 0);
        for (uint32_t offset = 0; offset < block;) {
            for (size_t i = 0; i < changes.size(); ++i)
                if (changes[i].frame >= frames && changes[i].frame < frames + block && (slicing ? uint32_t(changes[i].frame - frames) : 0) == offset) {
                    values[changes[i].control] = changes[i].value;
                    applied.push_back({frames + offset, changes[i]});
                }
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            uint32_t count = slicing ? std::min(1 + rng % 40, block - offset) : block;
            for (uint32_t cut : cuts) if (cut > offset && cut < offset + count) count = cut - offset;
            descriptor->connect_port(handle, uint32_t(Port::out), out.data() + frames + offset);
            descriptor->connect_port(handle, uint32_t(Port::go), const_cast<float*>(go.data()) + offset);
            for (uint32_t c = 0; c < controls; ++c) descriptor->connect_port(handle, uint32_t(Port::shape) + c, &values[c]);
            descriptor->connect_port(handle, uint32_t(Port::position), &position);
            descriptor->run(handle, count);
            offset += count;
        }
        frames += block;
        return applied;
    }
    const LV2_Descriptor* descriptor;
    bool slicing;
    const LV2_Feature* features[1]{nullptr};
    LV2_Handle handle;
    Controls values{};
    std::vector<float> out = std::vector<float>(1 << 16, NAN);
    float position = -1;
    uint64_t frames = 0;
    uint32_t rng = 0x9E3779B9;
};

// Runs the plugin through `length` frames of Go pulses and control changes,
// and steps the motion directly with what the plugin saw, when it saw it.
// For the shapes with no randomness, Out must be the same, sample for sample.
void play(const LV2_Descriptor* d, bool slicing, const Controls& initial, const std::vector<uint64_t>& rises, const std::vector<Change>& changes) {
    const std::string how = slicing ? "sliced as Ingen runs it" : "unsliced";
    const uint64_t length = 40 * block;
    Host host(d, slicing);
    host.values = initial;
    std::vector<float> go(length, 0);
    for (uint64_t r : rises) for (uint64_t f = r; f < std::min(length, r + 20); ++f) go[f] = 1;
    std::vector<std::pair<uint64_t, Change>> applied;
    for (uint64_t start = 0; start < length; start += block) {
        const auto a = host.period(std::vector<float>(go.begin() + start, go.begin() + start + block), changes);
        applied.insert(applied.end(), a.begin(), a.end());
    }
    const bool deterministic = initial[0] <= 2 && std::all_of(changes.begin(), changes.end(), [](const Change& c) { return c.control != 0 || c.value <= 2; });
    sway::Motion motion(48000, 1);
    Controls values = initial;
    size_t next = 0;
    float p = 0;
    for (uint64_t f = 0; f < length; ++f) {
        while (next < applied.size() && applied[next].first == f) { values[applied[next].second.control] = applied[next].second.value; ++next; }
        const auto s = settings(values);
        p = motion.step(go[f], s);
        const float out = host.out[f];
        require(std::isfinite(out) && out >= std::min(s.point_a, s.point_b) - 1e-6f && out <= std::max(s.point_a, s.point_b) + 1e-6f,
                "Out between A and B at frame " + std::to_string(f) + ", " + how);
        if (deterministic) require(out == sway::output(p, s), "Out at frame " + std::to_string(f) + " matches the motion, " + how + ": got " +
                                   std::to_string(out) + ", want " + std::to_string(sway::output(p, s)));
    }
    require(host.position >= 0 && host.position <= 1, "Position within 0 to 1, " + how);
    if (deterministic) require(host.position == p, "Position is p at the last frame, " + how);
}
}

int main(int argc, char** argv) {
    try {
        require(argc > 1, "usage: test_lv2 PLUGIN.so");
        void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        require(library, dlerror() ? dlerror() : "dlopen");
        using Get = const LV2_Descriptor* (*)(uint32_t);
        const auto get = reinterpret_cast<Get>(dlsym(library, "lv2_descriptor"));
        require(get && get(0) && !get(1), "one descriptor");
        require(std::string(get(0)->URI) == "https://github.com/ABustos2606/sway#sway", "URI");
        // Short moves, so a 5120-frame run sees many of them.
        const Controls sweep{0, -0.5f, 0.8f, 0.02f, 0, 120, 4, 1};
        // Go pulses, three of them in one period (256 to 383).
        const std::vector<uint64_t> rises = {5, 200, 260, 300, 370, 640, 2000, 3001};
        // Control changes inside periods: Time, Sync and Beats, A and B, Shape.
        const std::vector<Change> changes = {{77, 3, 0.05f}, {500, 4, 1}, {501, 6, 0}, {900, 5, 300}, {1300, 1, 0.3f},
                                             {1301, 2, -1}, {1700, 0, 1}, {2500, 0, 2}, {3300, 4, 0}, {4100, 0, 0}};
        for (const bool slicing : {true, false}) {
            for (int shape = 0; shape <= 5; ++shape) {
                Controls c = sweep;
                c[0] = float(shape);
                play(get(0), slicing, c, rises, {});
                c[7] = 0;
                play(get(0), slicing, c, rises, {});
            }
            play(get(0), slicing, sweep, rises, changes);
        }
        std::puts("PASS test_lv2: sliced as Ingen runs it and unsliced, Out matches the motion; Go and control changes act at their own frames");
        dlclose(library);
    } catch (const std::exception& e) { std::printf("FAIL test_lv2: %s\n", e.what()); return 1; }
}
