// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
#pragma once
#include <cstdint>

// Port indices of Sway. Permanent once a patch is saved with them: new ports
// go at the end. tests/ttl_check.py compares them with the Turtle.
namespace sway {
enum class Port : uint32_t {
    out = 0,        // cv_out
    go = 1,         // cv_in
    shape = 2,      // control_in
    point_a = 3,    // control_in
    point_b = 4,    // control_in
    time = 5,       // control_in
    sync = 6,       // control_in
    bpm = 7,        // control_in
    beats = 8,      // control_in
    start = 9,      // control_in
    position = 10,  // control_out
    count = 11
};
}
